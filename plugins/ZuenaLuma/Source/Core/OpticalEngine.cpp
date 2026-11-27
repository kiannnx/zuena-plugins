#include "OpticalEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace luma
{
namespace
{
constexpr double pi = std::numbers::pi;
double linear(double dB) noexcept
{
    return std::exp(dB * (std::numbers::ln10 / 20.0));
}
double db(double amplitude) noexcept
{
    return 20.0 * std::log10(std::max(amplitude, 1.0e-9));
}
double signal(double x) noexcept
{
    // Keep squared detector levels and filtered peaks finite even with malformed double input.
    return std::isfinite(x) ? std::clamp(x, -1.0e100, 1.0e100) : 0.0;
}
double control(double value, double low, double high, double fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
double classicKnee(double over) noexcept
{
    constexpr double half = 3.0;
    if (over <= -half)
        return 0.0;
    if (over >= half)
        return over;
    return (over + half) * (over + half) / (4.0 * half);
}
} // namespace

void OpticalEngine::prepare(double sampleRate, const Controls &controls) noexcept
{
    rate = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000.0, 768000.0) : 48000.0;
    smoothing = math::sampleWeight(rate, 0.008);
    detectorSpeed = math::sampleWeight(rate, 0.002);
    preciseWeights = math::PowerWeights::at(rate, 0.006);
    charge = math::sampleWeight(rate, 0.48);
    presenceSpeed = -std::expm1(-2.0 * pi * 3500.0 / rate);
    peakDecay = std::exp(-1.0 / (rate * 0.72));
    // Exact sampled damped movement. 1.25% overshoot and first arrival at 99% in 300 ms.
    const double logOvershoot = -std::log(0.0125);
    const double damping = logOvershoot / std::hypot(pi, logOvershoot);
    const double k = std::sqrt(1.0 - damping * damping);
    const auto response = [k, damping](double t)
    { return 1.0 - std::exp(-damping * t) * (std::cos(k * t) + damping * std::sin(k * t) / k); };
    double lo = 0.0, hi = pi / k;
    for (int i = 0; i < 36; ++i)
    {
        const double mid = (lo + hi) * 0.5;
        if (response(mid) < 0.99)
            lo = mid;
        else
            hi = mid;
    }
    const double omega = (lo + hi) / 0.60;
    const double angle = omega * k / rate;
    const double decay = std::exp(-damping * omega / rate);
    a11 = decay * (std::cos(angle) + damping * std::sin(angle) / k);
    a12 = decay * std::sin(angle) / (omega * k);
    a21 = -omega * omega * a12;
    a22 = decay * (std::cos(angle) - damping * std::sin(angle) / k);
    haveControls = false;
    setControls(controls);
    clear();
}

void OpticalEngine::clear() noexcept
{
    linked = {};
    individual = {};
    inputFilter = {};
    keyFilter = {};
    inputMeter = {};
    outputMeter = {};
    reductionLeft = reductionRight = inputPeak = outputPeak = 0;
    current = target;
}

void OpticalEngine::setControls(const Controls &c) noexcept
{
    if (haveControls && c == previousControls)
        return;
    previousControls = c;
    haveControls = true;
    const double recovery = control(c.recoveryPercent, 50.0, 200.0, 100.0) / 100.0;
    const double highpassHz = control(c.highpassHz, 0.0, 300.0, 0.0);
    target.depth = control(c.reduction, 0.0, 100.0, 48.0);
    target.makeup = linear(control(c.makeupDb, -18.0, 24.0, 0.0) + (c.autoMakeup ? referenceReductionDb(c) : 0.0));
    target.highpass = -std::expm1(-2.0 * pi * std::max(10.0, highpassHz) / rate);
    target.highpassMix = std::min(highpassHz / 10.0, 1.0);
    target.emphasis = linear(control(c.emphasisDb, 0.0, 9.0, 0.0)) - 1.0;
    target.preciseHighpass = math::integratorCoefficient(math::warpedFrequency(std::max(1.0, highpassHz), rate));
    target.preciseShelf = math::integratorCoefficient(math::warpedFrequency(3500.0, rate)
                                                      * std::sqrt(1.0 + target.emphasis));
    target.cutoffMix = std::min(highpassHz, 1.0);
    target.fastRelease = math::sampleWeight(rate, 0.080 * recovery);
    target.slowRelease = math::sampleWeight(rate, 1.15 * recovery);
    target.mix = c.bypass ? 0.0 : control(c.mixPercent, 0.0, 100.0, 100.0) / 100.0;
    target.slope = c.limit ? 11.0 / 12.0 : 2.0 / 3.0;
    target.keyMix = c.external ? 1.0 : 0.0;
    target.attack = math::sampleWeight(rate, 0.001 * control(c.attackMs, 1.0, 40.0, 8.0));
    target.link = control(c.stereoLinkPercent, 0.0, 100.0, 100.0) / 100.0;
    target.range = control(c.rangeDb, 0.0, 40.0, 40.0);
    target.audition = c.audition && !c.bypass ? 1.0 : 0.0;
    target.precision = c.precision ? 1.0 : 0.0;
}

void OpticalEngine::updateNeedle(Needle &m, double amp) const noexcept
{
    const double offset = m.level - amp;
    const double speed = a21 * offset + a22 * m.speed;
    m.level = amp + a11 * offset + a12 * m.speed;
    m.speed = speed;
}

OpticalEngine::DetectorSample OpticalEngine::detect(DetectorFilter &f, double x) const noexcept
{
    f.low += current.highpass * (x - f.low);
    const double hp = x - current.highpassMix * f.low;
    f.presence += presenceSpeed * (hp - f.presence);
    const auto lowpass = [](double sample, double coefficient, double &integrator)
    {
        const double delta = coefficient * (sample - integrator);
        const double low = integrator + delta;
        integrator = low + delta;
        return low;
    };
    const double preciseHp = x - current.cutoffMix * lowpass(x, current.preciseHighpass, f.highpassIntegrator);
    const double preciseLow = lowpass(preciseHp, current.preciseShelf, f.shelfIntegrator);
    return {hp + current.emphasis * (hp - f.presence),
            preciseHp + current.emphasis * (preciseHp - preciseLow)};
}

double OpticalEngine::compress(Cell &cell, double precisePower, double threshold, double slope) const noexcept
{
    // Calibration is expressed as the peak of a sine at the detector.
    const double power = std::lerp(cell.power, precisePower, current.precision);
    const double over = 10.0 * std::log10(std::max(2.0 * power, 1.0e-15)) - threshold;
    const double knee = std::lerp(classicKnee(over), math::precisionKnee(over), current.precision);
    const double demand = std::min(40.0, knee * slope);
    // Precision: the cell answers large jumps faster (program-dependent attack). Classic keeps the fixed attack.
    const double rise = demand - cell.fast;
    const double attack = math::programAttackWeight(current.attack, rise, current.precision);
    cell.fast += (rise > 0.0 ? attack : current.fastRelease) * rise;
    cell.memory += (demand > cell.memory ? charge : current.slowRelease) * (demand - cell.memory);
    const double depth = std::max(cell.fast, 0.52 * cell.memory);
    return depth < 1.0e-9 ? 0.0 : depth;
}

template <typename Sample>
void OpticalEngine::processSamples(std::span<Sample> left, std::span<Sample> right, std::span<const Sample> keyLeft,
                                   std::span<const Sample> keyRight) noexcept
{
    const bool stereo = !right.empty();
    const auto samples = stereo ? std::min(left.size(), right.size()) : left.size();
    const bool hasKey = keyLeft.size() >= samples && !keyLeft.empty();
    const bool stereoKey = hasKey && keyRight.size() >= samples;
    const auto follow = [this](double &x, double goal)
    {
        x += smoothing * (goal - x);
        if (std::abs(goal - x) < 1.0e-12)
            x = goal;
    };
    for (std::size_t i = 0; i < samples; ++i)
    {
        follow(current.depth, target.depth);
        follow(current.makeup, target.makeup);
        follow(current.highpass, target.highpass);
        follow(current.highpassMix, target.highpassMix);
        follow(current.emphasis, target.emphasis);
        follow(current.mix, target.mix);
        follow(current.slope, target.slope);
        follow(current.keyMix, hasKey ? target.keyMix : 0.0);
        follow(current.fastRelease, target.fastRelease);
        follow(current.slowRelease, target.slowRelease);
        follow(current.attack, target.attack);
        follow(current.link, target.link);
        follow(current.range, target.range);
        follow(current.audition, target.audition);
        follow(current.precision, target.precision);
        follow(current.preciseHighpass, target.preciseHighpass);
        follow(current.preciseShelf, target.preciseShelf);
        follow(current.cutoffMix, target.cutoffMix);

        const double l = signal(left[i]), r = stereo ? signal(right[i]) : l;
        const auto il = detect(inputFilter[0], l);
        const auto ir = stereo ? detect(inputFilter[1], r) : il;
        const auto el = hasKey ? detect(keyFilter[0], signal(keyLeft[i])) : il;
        const auto er = stereoKey ? detect(keyFilter[1], signal(keyRight[i])) : (hasKey ? el : ir);
        // Crossfade power after the filters. Opposite polarities in the two buses cannot cancel detection.
        const auto power = [this](double main, double key) { return std::lerp(main * main, key * key, current.keyMix); };
        const double pl = power(il.classic, el.classic);
        const double pr = power(ir.classic, er.classic);
        const auto meter = [this](Cell &cell, double classic, double precise)
        {
            cell.power += detectorSpeed * (classic - cell.power);
            cell.precise.advance(precise, preciseWeights);
        };
        meter(individual[0], pl, power(il.precise, el.precise));
        meter(individual[1], pr, power(ir.precise, er.precise));
        const auto peakPower = std::max(pl, pr);
        linked.power += detectorSpeed * (peakPower - linked.power);
        const double preciseL = individual[0].precise.meanSquare;
        const double preciseR = individual[1].precise.meanSquare;
        const double threshold = -2.0 - 0.40 * current.depth;
        const double slope = current.slope * std::min(1.0, current.depth / 5.0);
        const double shared = compress(linked, std::max(preciseL, preciseR), threshold, slope);
        const double localL = compress(individual[0], preciseL, threshold, slope);
        const double localR = stereo ? compress(individual[1], preciseR, threshold, slope) : localL;
        reductionLeft = std::min(current.range, stereo ? std::lerp(localL, shared, current.link) : shared);
        reductionRight = std::min(current.range, stereo ? std::lerp(localR, shared, current.link) : shared);
        const double wetL = current.makeup * linear(-reductionLeft);
        const double wetR = reductionLeft == reductionRight ? wetL : current.makeup * linear(-reductionRight);
        const double normalL = l * std::lerp(1.0, wetL, current.mix);
        const double normalR = r * std::lerp(1.0, wetR, current.mix);
        double outL = normalL, outR = normalR;
        if (current.audition > 0.0)
        {
            const auto listen = [this](DetectorSample x) { return std::lerp(x.classic, x.precise, current.precision); };
            const double listenL = std::lerp(listen(il), stereo ? listen(el) : (listen(el) + listen(er)) * 0.5, current.keyMix);
            outL = std::lerp(normalL, listenL, current.audition);
            if (stereo)
                outR = std::lerp(normalR, std::lerp(listen(ir), listen(er), current.keyMix), current.audition);
        }
        constexpr double ceiling = std::numeric_limits<Sample>::max();
        const double yl = std::clamp(outL, -ceiling, ceiling);
        const double yr = stereo ? std::clamp(outR, -ceiling, ceiling) : yl;
        left[i] = static_cast<Sample>(yl);
        if (stereo)
            right[i] = static_cast<Sample>(yr);
        updateNeedle(inputMeter[0], std::abs(l));
        updateNeedle(inputMeter[1], std::abs(r));
        updateNeedle(outputMeter[0], std::abs(yl));
        updateNeedle(outputMeter[1], std::abs(yr));
        inputPeak = std::max({std::abs(l), std::abs(r), inputPeak * peakDecay});
        outputPeak = std::max({std::abs(yl), std::abs(yr), outputPeak * peakDecay});
    }
}

void OpticalEngine::process(std::span<float> left, std::span<float> right, std::span<const float> kl,
                            std::span<const float> kr) noexcept
{
    processSamples(left, right, kl, kr);
}

void OpticalEngine::process(std::span<double> left, std::span<double> right, std::span<const double> kl,
                            std::span<const double> kr) noexcept
{
    processSamples(left, right, kl, kr);
}

double OpticalEngine::referenceReductionDb(const Controls &c) noexcept
{
    return staticReductionDb(c, referencePeakDbfs);
}

double OpticalEngine::staticReductionDb(const Controls &c, double peakDbfs) noexcept
{
    // Same static path as compress(): threshold and slope from Reduction, knee of the selected model, Range.
    const double depth = control(c.reduction, 0.0, 100.0, 48.0);
    const double threshold = -2.0 - 0.40 * depth;
    const double over = control(peakDbfs, -200.0, 60.0, -200.0) - threshold;
    const double knee = c.precision ? math::precisionKnee(over) : classicKnee(over);
    const double slope = (c.limit ? 11.0 / 12.0 : 2.0 / 3.0) * std::min(1.0, depth / 5.0);
    return std::min({40.0, knee * slope, control(c.rangeDb, 0.0, 40.0, 40.0)});
}

MeterValues OpticalEngine::meters() const noexcept
{
    const auto vu = [](const auto &m)
    { return static_cast<float>(std::max(-90.0, db(std::max(m[0].level, m[1].level) * pi * 0.5) + 18.0)); };
    return {vu(inputMeter), vu(outputMeter), static_cast<float>(std::max(reductionLeft, reductionRight)),
            static_cast<float>(std::max(-108.0, db(outputPeak))),
            static_cast<float>(std::max(-108.0, db(inputPeak))),
            static_cast<float>(reductionLeft), static_cast<float>(reductionRight)};
}
} // namespace luma
