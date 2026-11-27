#pragma once
#include "CompressionMath.h"

#include <array>
#include <span>

namespace luma
{
struct Controls
{
    double reduction = 48.0;
    double makeupDb = 0.0;
    double highpassHz = 0.0;
    double emphasisDb = 0.0;
    double recoveryPercent = 100.0;
    double mixPercent = 100.0;
    bool limit = false;
    bool external = false;
    bool bypass = false;
    double attackMs = 8.0;
    double stereoLinkPercent = 100.0;
    double rangeDb = 40.0;
    bool audition = false;
    bool precision = false;
    bool autoMakeup = false;
    bool operator==(const Controls &) const noexcept = default;
};

struct MeterValues
{
    float inputVu = -90.0f;
    float outputVu = -90.0f;
    float reductionDb = 0.0f;
    float peakDb = -108.0f;
    float inputPeakDb = -108.0f;
    float reductionLeftDb = 0.0f;
    float reductionRightDb = 0.0f;
};

class OpticalEngine
{
  public:
    void prepare(double sampleRate, const Controls &) noexcept;
    void clear() noexcept;
    void stopAudition() noexcept { current.audition = 0.0; }
    void setControls(const Controls &) noexcept;
    // Empty right = mono. Empty keyLeft = internal fallback. Silent routed key = silence.
    void process(std::span<float> left, std::span<float> right = {}, std::span<const float> keyLeft = {},
                 std::span<const float> keyRight = {}) noexcept;
    void process(std::span<double> left, std::span<double> right = {}, std::span<const double> keyLeft = {},
                 std::span<const double> keyRight = {}) noexcept;
    [[nodiscard]] MeterValues meters() const noexcept;
    // Static reduction (dB) for the reference program: a 1 kHz sine at -18 dBFS RMS (0 VU), i.e. -15 dBFS peak.
    // Automatic makeup adds exactly this gain, so that level leaves the compressor at the level it entered.
    [[nodiscard]] static double referenceReductionDb(const Controls &) noexcept;
    // Static reduction (dB) for a steady sine whose peak is `peakDbfs`, with the engine's own curve.
    // The editor draws the transfer curve from this, so the drawing can never disagree with the sound.
    [[nodiscard]] static double staticReductionDb(const Controls &, double peakDbfs) noexcept;
    static constexpr double referencePeakDbfs = -15.0;

  private:
    struct DetectorFilter
    {
        double low = 0.0, presence = 0.0;
        double highpassIntegrator = 0.0, shelfIntegrator = 0.0;
    };
    struct Needle
    {
        double level = 0.0, speed = 0.0;
    };
    struct Targets
    {
        double depth = 0, makeup = 1, highpass = 0, highpassMix = 0, emphasis = 0;
        double fastRelease = 0, slowRelease = 0, mix = 1, slope = 2.0 / 3.0, keyMix = 0;
        double attack = 0, link = 1, range = 40, audition = 0;
        double precision = 0, preciseHighpass = 0, preciseShelf = 0, cutoffMix = 0;
    };
    struct Cell
    {
        double power = 0, fast = 0, memory = 0;
        math::PowerEnvelope precise{};
    };
    struct DetectorSample { double classic = 0, precise = 0; };
    std::array<DetectorFilter, 2> inputFilter{}, keyFilter{};
    std::array<Cell, 2> individual{};
    Cell linked{};
    std::array<Needle, 2> inputMeter{}, outputMeter{};
    double rate = 48000, smoothing = 1, detectorSpeed = 1, charge = 1, presenceSpeed = 1;
    math::PowerWeights preciseWeights{};
    double a11 = 0, a12 = 0, a21 = 0, a22 = 0, peakDecay = 1;
    double reductionLeft = 0, reductionRight = 0, inputPeak = 0, outputPeak = 0;
    Targets target, current;
    Controls previousControls;
    bool haveControls = false;
    void updateNeedle(Needle &, double amplitude) const noexcept;
    [[nodiscard]] DetectorSample detect(DetectorFilter &, double sample) const noexcept;
    [[nodiscard]] double compress(Cell &, double precisePower, double threshold, double slope) const noexcept;
    template <typename Sample> void processSamples(std::span<Sample>, std::span<Sample>,
                                                   std::span<const Sample>, std::span<const Sample>) noexcept;
};
} // namespace luma
