#pragma once
#include "State/Parameters.h"
#include <cstdint>

namespace luma
{
class Processor final : public juce::AudioProcessor
{
  public:
    Processor();
    const juce::String getName() const override { return "ZUENA Luma"; }
    void prepareToPlay(double sampleRate, int) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    void processBlock(juce::AudioBuffer<double> &, juce::MidiBuffer &) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }
    juce::AudioProcessorParameter *getBypassParameter() const override { return params[index(Param::bypass)]; }
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
    void applyFactory(std::size_t);
    MeterValues readMeters() const noexcept;
    /** Controles vigentes (los mismos que recibe el motor): la interfaz dibuja la curva estática con ellos. */
    Controls liveControls() const noexcept { return controls(); }
    std::uint32_t meterFrameCount() const noexcept { return processedFrames.load(std::memory_order_acquire); }
    bool keyConnected() const noexcept { return routed.load(std::memory_order_relaxed); }
    bool isDark() const noexcept { return dark.load(std::memory_order_relaxed); }
    void setDark(bool enabled) noexcept { dark.store(enabled, std::memory_order_relaxed); }
    void setAudition(bool enabled) noexcept { audition.store(enabled, std::memory_order_relaxed); }
    bool isAuditioning() const noexcept { return audition.load(std::memory_order_relaxed); }

  private:
    juce::AudioProcessorValueTreeState state;
    OpticalEngine engine;
    std::array<std::atomic<float> *, index(Param::count)> raw{};
    std::array<juce::RangedAudioParameter *, index(Param::count)> params{};
    std::atomic<float> inputVu{-90}, outputVu{-90}, peakIn{-108}, peakOut{-108}, gainLeft{0}, gainRight{0};
    std::atomic<bool> routed{false};
    std::atomic<std::uint32_t> processedFrames{0};
    std::atomic<bool> clearRequested{false}, dark{false}, audition{false};
    Controls controls() const noexcept;
    void storeMeters(MeterValues) noexcept;
    template <typename Sample> void run(juce::AudioBuffer<Sample> &) noexcept;
    using juce::AudioProcessor::processBlock;
};
} // namespace luma
