#pragma once

#include "Core/EqEngine.h"
#include "State/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

class ZuenaEqProcessor final : public juce::AudioProcessor,
                               private juce::Timer
{
public:
    ZuenaEqProcessor();
    ~ZuenaEqProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }
    zeq::EqEngine& getEngine() noexcept { return engine; }

private:
    void syncEngineGlobals() noexcept;
    void timerCallback() override;

    juce::UndoManager undoManager { 30000, 60 };
    juce::AudioProcessorValueTreeState state;
    std::array<zeq::params::BandValues, zeq::kMaxBands> bandValues;
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* gainScale = nullptr;
    std::atomic<float>* qualityMode = nullptr;
    std::atomic<float>* autoGain = nullptr;
    std::atomic<float>* globalBypass = nullptr;
    std::atomic<int> desiredLatency { 0 };
    int reportedLatency = 0;
    zeq::EqEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZuenaEqProcessor)
};
