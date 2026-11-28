#include "Processor.h"
#include "UI/Editor.h"

namespace zlink
{
static_assert(std::atomic<float>::is_always_lock_free);

Processor::Processor()
    : AudioProcessor(BusesProperties()
                         .withInput("Entrada", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Salida", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "ZuenaLink", makeLayout())
{
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        raw[i] = state.getRawParameterValue(kParamIds[i]);
        params[i] = state.getParameter(kParamIds[i]);
    }
}

Processor::~Processor() = default;

LinkControls Processor::controls() const noexcept
{
    LinkControls c;
    c.obs = value(Param::obs) >= 0.5f;
    c.channel = static_cast<std::uint32_t>(std::clamp(static_cast<int>(value(Param::channel) + 0.5f), 0,
                                                      static_cast<int>(kLinkChannels) - 1));
    c.device = value(Param::device) >= 0.5f;
    c.sendDb = value(Param::send);
    c.mono = value(Param::mono) >= 0.5f;
    return c;
}

void Processor::prepareToPlay(double sampleRate, int maximumBlock)
{
    setLatencySamples(0); // el audio de la pista pasa tal cual
    link.prepare(sampleRate, maximumBlock);
}

bool Processor::isBusesLayoutSupported(const BusesLayout &b) const
{
    const auto in = b.getMainInputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) &&
           b.getMainOutputChannelSet() == in;
}

void Processor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &)
{
    const auto n = static_cast<std::size_t>(buffer.getNumSamples());
    const int channels = getMainBusNumInputChannels();
    blocks.fetch_add(1, std::memory_order_relaxed);
    if (n == 0 || channels < 1 || buffer.getNumChannels() < channels)
        return;
    const std::span<const float> left{buffer.getReadPointer(0), n};
    const std::span<const float> right = channels > 1 ? std::span<const float>{buffer.getReadPointer(1), n} : std::span<const float>{};
    link.process(left, right, controls(), isNonRealtime());
}

void Processor::processBlock(juce::AudioBuffer<double> &buffer, juce::MidiBuffer &)
{
    // Doble precisión: se convierte de a tramos a float (sin reservar memoria) y se envía igual.
    const auto n = static_cast<std::size_t>(buffer.getNumSamples());
    const int channels = getMainBusNumInputChannels();
    blocks.fetch_add(1, std::memory_order_relaxed);
    if (n == 0 || channels < 1 || buffer.getNumChannels() < channels)
        return;
    const auto c = controls();
    const bool offline = isNonRealtime();
    for (std::size_t start = 0; start < n; start += convertL.size())
    {
        const auto m = std::min(convertL.size(), n - start);
        const double *l = buffer.getReadPointer(0) + start;
        const double *r = channels > 1 ? buffer.getReadPointer(1) + start : nullptr;
        for (std::size_t i = 0; i < m; ++i)
        {
            convertL[i] = static_cast<float>(l[i]);
            if (r != nullptr)
                convertR[i] = static_cast<float>(r[i]);
        }
        link.process({convertL.data(), m}, r != nullptr ? std::span<const float>{convertR.data(), m} : std::span<const float>{}, c, offline);
    }
}

void Processor::processBlockBypassed(juce::AudioBuffer<float> &, juce::MidiBuffer &)
{
    link.processBypassed(); // en bypass la pista suena igual y no se envía nada
}

void Processor::processBlockBypassed(juce::AudioBuffer<double> &, juce::MidiBuffer &)
{
    link.processBypassed();
}

void Processor::gesture(Param p, float v)
{
    auto &a = parameter(p);
    a.beginChangeGesture();
    a.setValueNotifyingHost(a.convertTo0to1(v));
    a.endChangeGesture();
}

void Processor::getStateInformation(juce::MemoryBlock &data)
{
    auto snapshot = state.copyState();
    const auto d = link.device();
    const auto r = link.recording();
    snapshot.setProperty("dark", isDark(), nullptr);
    snapshot.setProperty("deviceId", juce::String(d.id.c_str()), nullptr);
    snapshot.setProperty("deviceName", juce::String::fromUTF8(d.name.c_str()), nullptr);
    snapshot.setProperty("exclusive", d.exclusive, nullptr);
    snapshot.setProperty("pair", static_cast<int>(d.pair), nullptr);
    snapshot.setProperty("recordFolder", juce::String(r.folder.wstring().c_str()), nullptr);
    snapshot.setProperty("recordFormat", static_cast<int>(r.format), nullptr);
    if (auto xml = snapshot.createXml())
        copyXmlToBinary(*xml, data);
}

void Processor::setStateInformation(const void *data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName("ZuenaLink"))
        return;
    auto saved = juce::ValueTree::fromXml(*xml);
    setDark(static_cast<bool>(saved.getProperty("dark", false)));
    DeviceSettings d;
    d.id = saved.getProperty("deviceId", "").toString().toWideCharPointer();
    d.name = saved.getProperty("deviceName", "").toString().toStdString();
    d.exclusive = static_cast<bool>(saved.getProperty("exclusive", false));
    d.pair = static_cast<std::uint32_t>(std::clamp(static_cast<int>(saved.getProperty("pair", 0)), 0, 31));
    link.setDevice(d);
    RecordSettings r;
    r.folder = std::filesystem::path(saved.getProperty("recordFolder", "").toString().toWideCharPointer());
    r.format = static_cast<SampleFormat>(std::clamp(static_cast<int>(saved.getProperty("recordFormat", 1)), 0, 2));
    link.setRecording(r);
    state.replaceState(saved);
}

juce::AudioProcessorEditor *Processor::createEditor()
{
    return std::make_unique<Editor>(*this).release();
}
} // namespace zlink

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return std::make_unique<zlink::Processor>().release();
}
