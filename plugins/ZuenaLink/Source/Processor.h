#pragma once
#include "Platform/LinkEngine.h"
#include "State/Parameters.h"
#include <array>
#include <atomic>

namespace zlink
{
class Processor final : public juce::AudioProcessor
{
  public:
    Processor();
    ~Processor() override;
    const juce::String getName() const override { return "ZUENA Link"; }
    void prepareToPlay(double sampleRate, int maximumBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    void processBlock(juce::AudioBuffer<double> &, juce::MidiBuffer &) override;
    void processBlockBypassed(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    void processBlockBypassed(juce::AudioBuffer<double> &, juce::MidiBuffer &) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor *createEditor() override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;

    juce::RangedAudioParameter &parameter(Param p) const noexcept { return *params[index(p)]; }
    float value(Param p) const noexcept { return raw[index(p)]->load(std::memory_order_relaxed); }
    void gesture(Param, float);
    LinkEngine &engine() noexcept { return link; }
    bool isDark() const noexcept { return dark.load(std::memory_order_relaxed); }
    void setDark(bool enabled) noexcept { dark.store(enabled, std::memory_order_relaxed); }
    /** Bloques procesados (la interfaz lo usa para saber si el DAW está mandando audio). */
    std::uint64_t processedBlocks() const noexcept { return blocks.load(std::memory_order_relaxed); }

  private:
    juce::AudioProcessorValueTreeState state;
    LinkEngine link;
    std::array<std::atomic<float> *, index(Param::count)> raw{};
    std::array<juce::RangedAudioParameter *, index(Param::count)> params{};
    std::atomic<bool> dark{false};
    std::atomic<std::uint64_t> blocks{0};
    std::array<float, LinkEngine::kMaxChunk> convertL{}, convertR{};
    LinkControls controls() const noexcept;
    using juce::AudioProcessor::processBlock;
};
} // namespace zlink
