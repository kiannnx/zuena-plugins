#include "PluginProcessor.h"

#include "UI/PluginEditor.h"

ZuenaEqProcessor::ZuenaEqProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Entrada", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Salida", juce::AudioChannelSet::stereo(), true)),
      state (*this, &undoManager, "ZuenaEQ", zeq::params::createLayout())
{
    bandValues = zeq::params::bindBandValues (state);
    outputGain = state.getRawParameterValue (zeq::params::outputGainId);
    gainScale = state.getRawParameterValue (zeq::params::gainScaleId);
    qualityMode = state.getRawParameterValue (zeq::params::qualityModeId);
    autoGain = state.getRawParameterValue (zeq::params::autoGainId);
    globalBypass = state.getRawParameterValue (zeq::params::globalBypassId);
    state.state.setProperty ("ui_analyzer", 3, nullptr);
    state.state.setProperty ("ui_range", 12, nullptr);
    state.state.setProperty ("ui_phase", false, nullptr);
    state.state.setProperty ("ui_autolisten", false, nullptr);
    const auto prefFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                              .getChildFile ("ZUENA")
                              .getChildFile ("ZuenaEQ.settings");
    const bool defaultDark = prefFile.existsAsFile() && prefFile.loadFileAsString().containsIgnoreCase ("dark=1");
    state.state.setProperty ("ui_dark", defaultDark, nullptr);
    state.state.setProperty ("ab_slot", 0, nullptr);
    startTimerHz (15);
}

ZuenaEqProcessor::~ZuenaEqProcessor()
{
    stopTimer();
}

bool ZuenaEqProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void ZuenaEqProcessor::syncEngineGlobals() noexcept
{
    const auto qm = qualityMode->load (std::memory_order_relaxed) > 0.5f ? zeq::QualityMode::NaturalPhase : zeq::QualityMode::ZeroLatency;
    engine.setQualityMode (qm);
    engine.setAutoGain (autoGain->load (std::memory_order_relaxed) > 0.5f);
    engine.setGainScale (gainScale->load (std::memory_order_relaxed) * 0.01);
    engine.setGlobalBypass (globalBypass->load (std::memory_order_relaxed) > 0.5f);
    engine.setOutputGainDb (outputGain->load (std::memory_order_relaxed));
    desiredLatency.store (engine.latencySamples(), std::memory_order_relaxed);
}

void ZuenaEqProcessor::timerCallback()
{
    const int lat = desiredLatency.load (std::memory_order_relaxed);
    if (lat != reportedLatency)
    {
        reportedLatency = lat;
        setLatencySamples (lat);
    }
}

void ZuenaEqProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    syncEngineGlobals();
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    reportedLatency = engine.latencySamples();
    desiredLatency.store (reportedLatency, std::memory_order_relaxed);
    setLatencySamples (reportedLatency);
}

void ZuenaEqProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    for (int c = numIn; c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    for (int b = 0; b < zeq::kMaxBands; ++b)
        engine.setBand (b, bandValues[(size_t) b].read());
    syncEngineGlobals();
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (buffer.getNumChannels(), zeq::kMaxChannels), buffer.getNumSamples());
}

juce::AudioProcessorEditor* ZuenaEqProcessor::createEditor()
{
    return new zeq::ui::ZuenaEqEditor (*this);
}

void ZuenaEqProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ZuenaEqProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZuenaEqProcessor();
}
