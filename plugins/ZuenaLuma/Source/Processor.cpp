#include "Processor.h"
#include "UI/Editor.h"

namespace luma
{
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
Processor::Processor()
    : AudioProcessor(BusesProperties()
                         .withInput("Entrada", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Salida", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "ZuenaLuma", makeLayout())
{
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        const auto id = paramID(static_cast<Param>(i));
        raw[i] = state.getRawParameterValue(id);
        params[i] = state.getParameter(id);
    }
}
Controls Processor::controls() const noexcept
{
    return {value(Param::reduction),     value(Param::makeup),           value(Param::highpass),
            value(Param::emphasis),      value(Param::recovery),         value(Param::mix),
            value(Param::limit) >= 0.5f, value(Param::external) >= 0.5f, value(Param::bypass) >= 0.5f,
            value(Param::attack), value(Param::stereoLink), value(Param::range), isAuditioning() && !isNonRealtime(),
            value(Param::detectorModel) >= static_cast<float>(DetectorModel::precision),
            value(Param::autoMakeup) >= 0.5f};
}
void Processor::prepareToPlay(double sampleRate, int)
{
    setLatencySamples(0);
    engine.prepare(sampleRate, controls());
    clearRequested.store(false, std::memory_order_relaxed);
    routed.store(false, std::memory_order_relaxed);
    storeMeters({});
}
void Processor::reset()
{
    clearRequested.store(true, std::memory_order_relaxed);
}
bool Processor::isBusesLayoutSupported(const BusesLayout &b) const
{
    if (b.inputBuses.size() != 2 || b.outputBuses.size() != 1)
        return false;
    const auto main = b.getMainInputChannelSet();
    if ((main != juce::AudioChannelSet::mono() && main != juce::AudioChannelSet::stereo()) ||
        b.getMainOutputChannelSet() != main)
        return false;
    const auto key = b.getChannelSet(true, 1);
    return key.isDisabled() || key == juce::AudioChannelSet::mono() || key == juce::AudioChannelSet::stereo();
}
void Processor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &)
{
    run(buffer);
}
void Processor::processBlock(juce::AudioBuffer<double> &buffer, juce::MidiBuffer &)
{
    run(buffer);
}
template <typename Sample>
void Processor::run(juce::AudioBuffer<Sample> &buffer) noexcept
{
    juce::ScopedNoDenormals scope;
    const auto count = static_cast<std::size_t>(buffer.getNumSamples());
    const int channels = getMainBusNumInputChannels();
    if (count == 0 || channels < 1 || buffer.getNumChannels() < channels)
        return;
    const int keys = getChannelCountOfBus(true, 1);
    const int keyOffset = keys > 0 ? getChannelIndexInProcessBlockBuffer(true, 1, 0) : 0;
    const bool hasKey = keys > 0 && keyOffset >= channels && buffer.getNumChannels() >= keyOffset + keys;
    routed.store(hasKey, std::memory_order_relaxed);
    std::span<Sample> right;
    std::span<const Sample> kl, kr;
    if (channels == 2)
        right = {buffer.getWritePointer(1), count};
    if (hasKey)
        kl = {buffer.getReadPointer(keyOffset), count};
    if (hasKey && keys == 2)
        kr = {buffer.getReadPointer(keyOffset + 1), count};
    engine.setControls(controls());
    if (clearRequested.exchange(false, std::memory_order_relaxed))
        engine.clear();
    if (isNonRealtime())
        engine.stopAudition();
    engine.process({buffer.getWritePointer(0), count}, right, kl, kr);
    storeMeters(engine.meters());
    processedFrames.fetch_add(static_cast<std::uint32_t>(count), std::memory_order_release);
}
void Processor::storeMeters(MeterValues m) noexcept
{
    inputVu.store(m.inputVu, std::memory_order_relaxed);
    outputVu.store(m.outputVu, std::memory_order_relaxed);
    peakIn.store(m.inputPeakDb, std::memory_order_relaxed);
    peakOut.store(m.peakDb, std::memory_order_relaxed);
    gainLeft.store(m.reductionLeftDb, std::memory_order_relaxed);
    gainRight.store(m.reductionRightDb, std::memory_order_relaxed);
}
MeterValues Processor::readMeters() const noexcept
{
    const auto l = gainLeft.load(std::memory_order_relaxed), r = gainRight.load(std::memory_order_relaxed);
    return {inputVu.load(std::memory_order_relaxed), outputVu.load(std::memory_order_relaxed), std::max(l, r),
            peakOut.load(std::memory_order_relaxed), peakIn.load(std::memory_order_relaxed), l, r};
}
void Processor::gesture(Param p, float v)
{
    auto &a = parameter(p);
    a.beginChangeGesture();
    a.setValueNotifyingHost(a.convertTo0to1(v));
    a.endChangeGesture();
}
void Processor::applyFactory(std::size_t n)
{
    if (n >= presets.size())
        return;
    for (std::size_t i = 0; i < dials.size(); ++i)
        gesture(dials[i].parameter, presets[n].dial[i]);
    gesture(Param::limit, presets[n].limit ? 1.0f : 0.0f);
}
void Processor::getStateInformation(juce::MemoryBlock &data)
{
    auto snapshot = state.copyState();
    snapshot.setProperty("dark", isDark(), nullptr);
    if (auto xml = snapshot.createXml())
        copyXmlToBinary(*xml, data);
}
void Processor::setStateInformation(const void *data, int size)
{
    if (auto xml = getXmlFromBinary(data, size); xml && xml->hasTagName("ZuenaLuma"))
    {
        auto saved = juce::ValueTree::fromXml(*xml);
        const auto addDefault = [&saved](const char *id, float value)
        {
            if (saved.getChildWithProperty("id", id).isValid())
                return;
            juce::ValueTree param("PARAM");
            param.setProperty("id", id, nullptr);
            param.setProperty("value", value, nullptr);
            saved.appendChild(param, nullptr);
        };
        for (const auto &dial : dials)
            if (dial.parameter > Param::meter)
                addDefault(dial.id, dial.initial);
        addDefault(paramID(Param::detectorModel), static_cast<float>(DetectorModel::classic));
        addDefault(paramID(Param::autoMakeup), 0.0f); // proyectos anteriores a 0.4.0: sin compensación
        setDark(static_cast<bool>(saved.getProperty("dark", false)));
        setAudition(false);
        state.replaceState(saved);
    }
}
juce::AudioProcessorEditor *Processor::createEditor()
{
    return std::make_unique<Editor>(*this).release();
}
} // namespace luma
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return std::make_unique<luma::Processor>().release();
}
