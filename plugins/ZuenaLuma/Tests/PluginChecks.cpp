#include "../Source/Processor.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>

namespace
{
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
void loop(int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}
void set(juce::AudioProcessor &plugin, const char *name, float normalized)
{
    for (auto *p : plugin.getParameters())
        if (p->getName(64) == luma::utf(name))
        {
            p->setValueNotifyingHost(normalized);
            return;
        }
    throw std::runtime_error(name);
}
template <typename Sample = float>
double render(juce::AudioProcessor &p, double rate, int block, int mode = 0)
{
    juce::MidiBuffer midi;
    const int channels = p.getTotalNumInputChannels(), n = static_cast<int>(rate);
    int offset = 0;
    double in = 0, out = 0;
    juce::AudioBuffer<Sample> buffer(channels, block);
    while (offset < n)
    {
        const int size = std::min(block, n - offset);
        buffer.setSize(channels, size, false, false, true);
        for (int i = 0; i < size; ++i)
        {
            const auto wave = static_cast<Sample>(std::sin(2 * std::numbers::pi * 1000 * (offset + i) / rate));
            const auto main = wave * static_cast<Sample>(mode == 0 ? 0.316227766 : 0.0316227766);
            for (int ch = 0; ch < channels; ++ch)
                buffer.setSample(
                    ch, i, ch < p.getMainBusNumInputChannels() ? main : (mode == 2 ? Sample{} : wave * static_cast<Sample>(0.65)));
            if (offset + i > n * 3 / 4)
                in += main * main;
        }
        p.processBlock(buffer, midi);
        for (int i = 0; i < size; ++i)
        {
            const auto x = buffer.getSample(0, i);
            require(std::isfinite(x), "non-finite output");
            if (offset + i > n * 3 / 4)
                out += x * x;
        }
        offset += size;
    }
    return 10.0 * std::log10(out / in);
}
std::unique_ptr<juce::AudioPluginInstance> loadPlugin(juce::AudioPluginFormatManager &manager,
                                                      const juce::String &path)
{
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto *f : manager.getFormats())
        if (f->getName() == "VST3")
            f->findAllTypesForFile(types, path);
    require(types.size() == 1, "VST3 discovery");
    juce::String error;
    auto p = manager.createPluginInstance(*types[0], 48000, 127, error);
    require(p != nullptr, "VST3 instantiation");
    require(p->getName() == "ZUENA Luma", "product name");
    return p;
}
void host(const juce::String &path)
{
    juce::AudioPluginFormatManager manager;
    juce::addDefaultFormatsToManager(manager);
    auto p = loadPlugin(manager, path);
    require(p->getParameters().size() == 15, "15 public parameters");
    for (const int channels : {1, 2})
        for (const int rate : {44100, 48000, 96000})
        {
            auto layout = p->getBusesLayout();
            const auto bus = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            layout.inputBuses.set(0, bus);
            layout.outputBuses.set(0, bus);
            layout.inputBuses.set(1, juce::AudioChannelSet::disabled());
            require(p->setBusesLayout(layout), "mono/stereo main bus");
            const int block = channels == 1 ? 37 : 512;
            p->setRateAndBufferSizeDetails(rate, block);
            p->prepareToPlay(rate, block);
            set(*p, "Reducción", 0.5f);
            set(*p, "Modo Limit", 0.0f);
            set(*p, "Bypass", 0.0f);
            require(std::abs(render(*p, rate, block) + 8.0) < 0.20, "host 3:1 steady-state gain");
            set(*p, "Modo Limit", 1);
            require(std::abs(render(*p, rate, block) + 11.0) < 0.22, "host 12:1 steady-state gain");
            set(*p, "Bypass", 1);
            require(std::abs(render(*p, rate, block)) < 0.005, "host bypass unity");
            require(p->getLatencySamples() == 0, "reported latency");
            p->releaseResources();
        }
    auto layout = p->getBusesLayout();
    layout.inputBuses.set(1, juce::AudioChannelSet::mono());
    require(p->setBusesLayout(layout), "external mono key");
    p->setRateAndBufferSizeDetails(48000, 256);
    p->prepareToPlay(48000, 256);
    set(*p, "Bypass", 0);
    set(*p, "Sidechain externo", 1);
    set(*p, "Reducción", 0.65f);
    require(render(*p, 48000, 256, 1) < -15, "external sidechain compression");
    // Let the optical memory recover before asserting unity on a silent key.
    for (int i = 0; i < 8; ++i)
        render(*p, 48000, 256, 2);
    require(std::abs(render(*p, 48000, 256, 2)) < 0.02, "silent external key");
    set(*p, "Sidechain externo", 0);
    set(*p, "Modo Limit", 0);
    set(*p, "Reducción", 0.5f);
    render(*p, 48000, 256);
    juce::MemoryBlock saved;
    p->getStateInformation(saved);
    require(saved.getSize() > 50, "saved VST3 state");
    set(*p, "Reducción", 0);
    for (int i = 0; i < 8; ++i)
        render(*p, 48000, 256);
    require(std::abs(render(*p, 48000, 256)) < 0.02, "reduction at zero");
    p->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    loop(80);
    require(std::abs(render(*p, 48000, 256) + 8.0) < 0.2, "VST3 state restores measured audio");
    p->releaseResources();
    require(p->supportsDoublePrecisionProcessing(), "64-bit processing advertised to the VST3 host");
    p->setProcessingPrecision(juce::AudioProcessor::doublePrecision);
    p->prepareToPlay(48000, 256);
    require(std::abs(render<double>(*p, 48000, 256) + 8.0) < 0.2, "64-bit steady-state gain");
    set(*p, "Rango", 0.10f);
    require(std::abs(render<double>(*p, 48000, 256) + 4.0) < 0.03, "64-bit range limits gain reduction");
    p->releaseResources();
    std::cout << "VST3 OK: 15 parameters, 6 bus/rate combinations, 32/64-bit, sidechain, gain, range, bypass, state and latency.\n";
}

void legacy(const juce::String &oldPath, const juce::String &candidatePath)
{
    juce::AudioPluginFormatManager manager;
    juce::addDefaultFormatsToManager(manager);
    auto old = loadPlugin(manager, oldPath);
    auto candidate = loadPlugin(manager, candidatePath);
    require(old->getParameters().size() == 10 || old->getParameters().size() == 13 || old->getParameters().size() == 14, "expected Luma 0.1.0, 0.2.0 or 0.3.0");
    require(candidate->getParameters().size() == 15, "expected the Luma 0.4.0 candidate");
    for (int index = 0; index < old->getParameters().size(); ++index)
    {
        const auto *before = old->getParameters()[index];
        const auto *after = candidate->getParameters()[index];
        require(before->getName(64) == after->getName(64), "legacy parameter order");
        require(std::abs(before->getDefaultValue() - after->getDefaultValue()) < 1.0e-6f, "legacy default");
        auto *beforeId = dynamic_cast<const juce::HostedAudioProcessorParameter *>(before);
        auto *afterId = dynamic_cast<const juce::HostedAudioProcessorParameter *>(after);
        require(beforeId && afterId && beforeId->getParameterID() == afterId->getParameterID(), "legacy VST3 ID");
    }
    for (const auto strength : {0.25f, 0.62f, 0.9f})
    {
        set(*old, "Reducción", strength);
        set(*old, "Ganancia", 0.60f);
        set(*old, "Paso alto", 0.37f);
        set(*old, "Énfasis", 0.28f);
        set(*old, "Mezcla", 0.67f);
        juce::MemoryBlock state;
        old->getStateInformation(state);
        set(*candidate, "Rango", 0);
        candidate->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        loop(85);
        for (auto *p : {old.get(), candidate.get()})
        {
            p->setRateAndBufferSizeDetails(48000, 127);
            p->prepareToPlay(48000, 127);
        }
        const auto before = render(*old, 48000, 127), after = render(*candidate, 48000, 127);
        std::cout << "Legacy state, reduction " << strength << ": delta " << std::abs(before - after) << " dB\n";
        require(std::abs(before - after) < 0.005, "legacy state audio changes");
        old->releaseResources();
        candidate->releaseResources();
    }
}

double benchmark(juce::AudioProcessor &plugin)
{
    constexpr int blockSize = 512, blocks = 1600;
    constexpr double rate = 48000.0;
    juce::AudioBuffer<float> input(2, blockSize), work(2, blockSize);
    for (int i = 0; i < blockSize; ++i)
    {
        const auto wave = [i](double cycles) { return std::sin(2 * std::numbers::pi * cycles * i / blockSize); };
        input.setSample(0, i, static_cast<float>(0.32 * wave(11.0) + 0.08 * wave(27.0)));
        input.setSample(1, i, static_cast<float>(0.28 * wave(7.0) + 0.12 * wave(13.0)));
    }
    set(plugin, "Reducción", 0.6f);
    plugin.prepareToPlay(rate, blockSize);
    juce::MidiBuffer midi;
    const auto run = [&](int count)
    {
        for (int n = 0; n < count; ++n)
        {
            work.copyFrom(0, 0, input, 0, 0, blockSize);
            work.copyFrom(1, 0, input, 1, 0, blockSize);
            plugin.processBlock(work, midi);
        }
    };
    run(80);
    std::array<double, 3> microseconds{};
    for (auto &measurement : microseconds)
    {
        const auto start = std::chrono::steady_clock::now();
        run(blocks);
        const std::chrono::duration<double, std::micro> time = std::chrono::steady_clock::now() - start;
        measurement = time.count() / blocks;
    }
    plugin.releaseResources();
    std::sort(microseconds.begin(), microseconds.end());
    return microseconds[1];
}

void performance(const juce::String &oldPath, const juce::String &candidatePath)
{
    juce::AudioPluginFormatManager manager;
    juce::addDefaultFormatsToManager(manager);
    auto old = loadPlugin(manager, oldPath);
    auto candidate = loadPlugin(manager, candidatePath);
    const auto before = benchmark(*old), after = benchmark(*candidate);
    set(*candidate, "Enlace L/R", 0.50f);
    const auto partial = benchmark(*candidate);
    std::cout << "48 kHz, stereo, 512 samples. Median microseconds per block\n"
              << "Reference: " << before << " | Precision linked: " << after << " | Precision at 50%: " << partial << '\n';
}

void auditBounds(juce::AudioProcessorEditor &editor)
{
    std::vector<juce::Rectangle<int>> controls;
    for (auto *child : editor.getChildren())
    {
        if (!dynamic_cast<const juce::Slider *>(child) && !dynamic_cast<const juce::TextButton *>(child)
            && !dynamic_cast<const juce::ComboBox *>(child))
            continue;
        const auto bounds = child->getBounds();
        require(editor.getLocalBounds().contains(bounds) && bounds.getWidth() >= 52 && bounds.getHeight() >= 23,
                "control outside panel or too small");
        for (const auto &previous : controls)
            require(!bounds.intersects(previous), "overlapping control bounds");
        controls.push_back(bounds);
    }
    require(controls.size() == 18, "visible interactive controls");
}
void auditLegacyState(luma::Processor &p)
{
    require(p.value(luma::Param::detectorModel) == 1, "new instance defaults to Precision");
    juce::XmlElement xml("ZuenaLuma");
    xml.setAttribute("dark", true);
    auto *previousReduction = xml.createNewChildElement("PARAM");
    previousReduction->setAttribute("id", "reduction");
    previousReduction->setAttribute("value", 67.0);
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary(xml, data);
    p.gesture(luma::Param::attack, 40);
    p.gesture(luma::Param::stereoLink, 0);
    p.gesture(luma::Param::range, 2);
    p.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    require(p.isDark(), "legacy theme restores");
    require(p.value(luma::Param::reduction) == 67, "legacy knob restores");
    require(p.value(luma::Param::attack) == 8, "missing attack defaults to 8 ms");
    require(p.value(luma::Param::stereoLink) == 100, "missing link defaults to 100%");
    require(p.value(luma::Param::range) == 40, "missing range defaults to 40 dB");
    require(p.value(luma::Param::detectorModel) == 0, "legacy project defaults to Classic");
}

void auditOfflineMonitor()
{
    luma::Processor processor;
    processor.gesture(luma::Param::reduction, 0);
    processor.gesture(luma::Param::highpass, 200);
    processor.setAudition(true);
    processor.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    for (int pass = 0; pass < 12; ++pass)
    {
        for (int channel = 0; channel < 2; ++channel)
            std::fill_n(buffer.getWritePointer(channel), 256, 0.25f);
        processor.processBlock(buffer, midi);
    }
    require(std::abs(buffer.getSample(0, 255)) < 0.01f, "live audition filters the detector signal");
    processor.setNonRealtime(true);
    for (int channel = 0; channel < 2; ++channel)
        std::fill_n(buffer.getWritePointer(channel), 256, 0.25f);
    processor.processBlock(buffer, midi);
    require(buffer.getSample(0, 0) == 0.25f, "offline render excludes monitoring from the first sample");
}

void editor(const juce::String &images)
{
    auditOfflineMonitor();
    luma::Processor p;
    auditLegacyState(p);
    p.setRateAndBufferSizeDetails(48000, 256);
    p.prepareToPlay(48000, 256);
    std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor());
    e->setVisible(true);
    std::size_t sliders = 0;
    for (auto *c : e->getChildren())
        if (auto *s = dynamic_cast<juce::Slider *>(c))
        {
            const auto id = luma::dials[sliders].parameter;
            require(s->getName() == luma::utf(luma::dials[sliders].name), "dial name");
            s->setValue(s->getMaximum(), juce::sendNotificationSync);
            require(std::abs(p.value(id) - s->getMaximum()) < 0.01, "dial writes processor");
            p.gesture(id, static_cast<float>(s->getMinimum()));
            loop(60);
            require(std::abs(s->getValue() - s->getMinimum()) < 0.01, "host automation reaches dial");
            ++sliders;
        }
    require(sliders == luma::dials.size(), "all dials are visible");
    for (auto *c : e->getChildren())
        if (auto *b = dynamic_cast<juce::ComboBox *>(c))
        {
            if (b->getTitle() == "Preset de mezcla")
                b->setSelectedId(2, juce::sendNotificationSync);
            else if (b->getTitle() == "Modelo del detector")
                b->setSelectedId(2, juce::sendNotificationSync);
        }
    require(p.value(luma::Param::highpass) == 85, "factory preset applies");
    require(p.value(luma::Param::detectorModel) == 1, "detector selector selects Precision");
    juce::MemoryBlock preciseState;
    p.getStateInformation(preciseState);
    p.gesture(luma::Param::detectorModel, 0);
    p.setStateInformation(preciseState.getData(), static_cast<int>(preciseState.getSize()));
    require(p.value(luma::Param::detectorModel) == 1, "Precision selection survives state save and restore");
    for (auto *c : e->getChildren())
        if (auto *b = dynamic_cast<juce::TextButton *>(c); b && b->getButtonText() == "Escuchar")
            b->triggerClick();
    loop(100);
    require(p.isAuditioning(), "audition button changes the processor");
    p.setNonRealtime(true);
    require(render(p, 48000, 256) < -1.0, "offline bypasses audition and keeps compression");
    p.setNonRealtime(false);
    p.setAudition(false);
    render(p, 48000, 256);
    // 0.4.0: botón de ganancia automática conectado en los dos sentidos.
    juce::TextButton *autoButton = nullptr;
    for (auto *c : e->getChildren())
        if (auto *b = dynamic_cast<juce::TextButton *>(c); b && b->getTitle() == luma::utf("Ganancia automática"))
            autoButton = b;
    require(autoButton != nullptr, "auto makeup button is visible");
    require(p.value(luma::Param::autoMakeup) == 0, "auto makeup defaults to off");
    autoButton->triggerClick();
    loop(60);
    require(p.value(luma::Param::autoMakeup) == 1, "auto makeup button changes the processor");
    require(autoButton->getButtonText().contains("dB"), "auto makeup button shows the applied gain");
    p.gesture(luma::Param::autoMakeup, 0);
    loop(60);
    require(!autoButton->getToggleState(), "host automation reaches the auto makeup button");
    for (int night : {0, 1})
    {
        p.setDark(night != 0);
        loop(75);
        if (images.isNotEmpty())
        {
            juce::AudioBuffer<float> block(2, 1600);
            juce::MidiBuffer midi;
            p.gesture(luma::Param::stereoLink, 75.0f);
            for (int n = 0; n < 84; ++n)
            {
                for (int i = 0; i < block.getNumSamples(); ++i)
                {
                    const double time = (n * block.getNumSamples() + i) / 48000.0;
                    const float amplitude = n % 26 < 13 ? 0.24f : 0.035f;
                    const float tone = amplitude * static_cast<float>(std::sin(2 * std::numbers::pi * 195 * time));
                    block.setSample(0, i, tone);
                    block.setSample(1, i, tone * 0.60f);
                }
                p.processBlock(block, midi);
                loop(34);
            }
            const auto dir = juce::File(images);
            require(dir.createDirectory().wasOk(), "gallery directory");
            auto file = dir.getChildFile(night ? "zuena-luma-shot-oscuro.jpg" : "zuena-luma-shot-claro.jpg");
            juce::FileOutputStream out(file);
            out.setPosition(0);
            out.truncate();
            juce::JPEGImageFormat jpg;
            jpg.setQuality(0.92f);
            // 2x: nítido en pantallas de alta densidad y al ampliar en la galería del launcher.
            require(jpg.writeImageToStream(e->createComponentSnapshot(e->getLocalBounds(), true, 2.0f), out),
                    "gallery JPEG");
        }
    }
    for (const auto &size : {juce::Point<int>{820, 550}, {1020, 642}, {1380, 840}})
    {
        e->setSize(size.x, size.y);
        auditBounds(*e);
    }
    p.setAudition(true);
    e.reset();
    require(!p.isAuditioning(), "monitor is off after closing the editor");
    std::cout << "Editor OK: controls, host automation, presets, audition, both themes and three sizes.\n";
}
} // namespace
int main(int argc, char **argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        const juce::StringArray a(argv + 1, argc - 1);
        if (a[0] == "host" && a.size() == 2)
            host(a[1]);
        else if (a[0] == "legacy" && a.size() == 3)
            legacy(a[1], a[2]);
        else if (a[0] == "bench" && a.size() == 3)
            performance(a[1], a[2]);
        else if (a[0] == "editor")
            editor(a[1]);
        else
            return 2;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
