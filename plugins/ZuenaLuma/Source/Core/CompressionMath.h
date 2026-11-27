#pragma once

#include <cmath>
#include <numbers>

namespace luma::math
{
inline double sampleWeight(double rate, double seconds) noexcept
{
    return -std::expm1(-1.0 / (rate * seconds));
}

// Integral of quintic smoothstep, normalized to a 6 dB knee.
// Its value and first three derivatives agree with the adjoining lines.
inline double precisionKnee(double over) noexcept
{
    if (over <= -3.0)
        return 0.0;
    if (over >= 3.0)
        return over;
    const double t = (over + 3.0) / 6.0;
    const double t2 = t * t;
    return 6.0 * t2 * t2 * (2.5 + t * (t - 3.0));
}

// Program-dependent attack of the optical cell (Precision). A one-pole weight `base` (time constant T)
// is raised to a faster constant T / speed with the exact discrete equivalent 1 - (1 - base)^speed.
// speed = 1 + precision * min(3, rise / 12 dB): a 12 dB jump in demanded reduction responds twice as fast,
// 36 dB or more four times as fast; small corrections keep the time set with the Attack knob.
inline double programAttackWeight(double base, double riseDb, double precision) noexcept
{
    if (riseDb <= 0.0 || precision <= 0.0 || base <= 0.0 || base >= 1.0)
        return base;
    const double speed = 1.0 + precision * std::fmin(3.0, riseDb / 12.0);
    return -std::expm1(speed * std::log1p(-base));
}

inline double warpedFrequency(double hz, double rate) noexcept
{
    return std::tan(std::numbers::pi * hz / rate);
}

inline double integratorCoefficient(double warped) noexcept
{
    return warped / (1.0 + warped);
}

struct PowerWeights
{
    double first{}, upstream{}, source{};
    static PowerWeights at(double rate, double seconds) noexcept
    {
        const double h = 1.0 / (rate * seconds);
        const double first = -std::expm1(-h);
        const double upstream = h * std::exp(-h);
        return {first, upstream, first - upstream};
    }
};

struct PowerEnvelope
{
    double first{}, meanSquare{};
    void advance(double power, PowerWeights weights) noexcept
    {
        // Closed-form ZOH transition for two cascaded, equal continuous-time poles.
        meanSquare += weights.upstream * (first - meanSquare) + weights.source * (power - meanSquare);
        first += weights.first * (power - first);
    }
};
} // namespace luma::math
