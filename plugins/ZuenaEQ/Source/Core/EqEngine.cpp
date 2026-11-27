#include "EqEngine.h"

#include <algorithm>
#include <cmath>

namespace zeq
{
namespace
{
constexpr double kSmoothSeconds = 0.015;  // suavizado de frecuencia, ganancia y Q
constexpr double kFadeSeconds = 0.005;    // fundido de entrada/salida de banda, modo y solo
constexpr double kKneeDb = 6.0;
constexpr double kButterworthQ = 0.70710678118654752;

/** Paso de una sección TPT State Variable Filter en estado estacionario (factores a1..a3 precalculados). */
inline double tickSvf (const SvfSection& c, std::array<double, 2>& s, double x) noexcept
{
    if (c.order == 1)
    {
        const double v = c.a1 * (x - s[0]);
        const double lp = v + s[0];
        s[0] = lp + v;
        return c.chp * x + c.cbp * lp;
    }
    const double v3 = x - s[1];
    const double v1 = c.a1 * s[0] + c.a2 * v3;
    const double v2 = s[1] + c.a2 * s[0] + c.a3 * v3;
    const double vhp = x - c.k * v1 - v2;
    s[0] = 2.0 * v1 - s[0];
    s[1] = 2.0 * v2 - s[1];
    return c.chp * vhp + c.cbp * v1 + c.clp * v2;
}

/** Interpolación muestra a muestra sobre los parámetros físicos (g, k, chp, cbp, clp) del SVF:
    preserva energía de los integradores y es incondicionalmente estable (cono convexo g > 0, k > 0). */
inline double tickSvfLerp (const SvfSection& a, const SvfSection& b, double t, std::array<double, 2>& s, double x) noexcept
{
    const double g = a.g + (b.g - a.g) * t;
    const double chp = a.chp + (b.chp - a.chp) * t;
    const double cbp = a.cbp + (b.cbp - a.cbp) * t;
    if (a.order == 1)
    {
        const double G = g / (1.0 + g);
        const double v = G * (x - s[0]);
        const double lp = v + s[0];
        s[0] = lp + v;
        return chp * x + cbp * lp;
    }
    const double k = a.k + (b.k - a.k) * t;
    const double clp = a.clp + (b.clp - a.clp) * t;
    const double a1 = 1.0 / (1.0 + g * (g + k));
    const double a2 = g * a1;
    const double a3 = g * a2;
    const double v3 = x - s[1];
    const double v1 = a1 * s[0] + a2 * v3;
    const double v2 = s[1] + a2 * s[0] + a3 * v3;
    const double vhp = x - k * v1 - v2;
    s[0] = 2.0 * v1 - s[0];
    s[1] = 2.0 * v2 - s[1];
    return chp * vhp + cbp * v1 + clp * v2;
}

inline double stepToward (double value, double target, double step) noexcept
{
    return value < target ? std::min (value + step, target) : std::max (value - step, target);
}

inline double clampFreq (double f) noexcept { return std::clamp (f, kMinFreq, kMaxFreq); }
inline double clampQ (double q) noexcept { return std::clamp (q, kMinQ, kMaxQ); }
inline int clampSlope (int s) noexcept { return std::clamp (s, 0, (int) kSlopesDbPerOct.size() - 1); }
} // namespace

EqEngine::EqEngine()
{
    for (auto& g : dynGainDb)
        g.store (0.0f, std::memory_order_relaxed);
}

int EqEngine::latencySamples() const noexcept
{
    return (targetQuality == QualityMode::NaturalPhase && sampleRate < 88200.0) ? kNaturalPhaseLatency : 0;
}

double EqEngine::effectiveSampleRate() const noexcept
{
    return (targetQuality == QualityMode::NaturalPhase && sampleRate < 88200.0) ? sampleRate * 2.0 : sampleRate;
}

void EqEngine::updateRates() noexcept
{
    dspRate = activeOversampled ? sampleRate * 2.0 : sampleRate;
    const double subSamples = activeOversampled ? (double) (kSubBlock * 2) : (double) kSubBlock;
    smoothAlpha = 1.0 - std::exp (-subSamples / (kSmoothSeconds * dspRate));
    mixStep = 1.0 / (kFadeSeconds * dspRate);
    baseMixStep = 1.0 / (kFadeSeconds * sampleRate);
}

void EqEngine::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    activeOversampled = (targetQuality == QualityMode::NaturalPhase && sampleRate < 88200.0);
    updateRates();
    reset();
    firstBlockAfterPrepare = true;
}

void EqEngine::reset() noexcept
{
    for (size_t i = 0; i < bands.size(); ++i)
    {
        auto& r = bands[i];
        r.configured = false;
        r.mix = 0.0;
        r.mixTarget = 0.0;
        r.state = {};
        r.detectorState = {};
        r.envelope = 0.0;
        r.rmsEnv = 0.0;
        r.dynamicDb = 0.0;
        r.detectorFreq = -1.0;
        dynGainDb[i].store (0.0f, std::memory_order_relaxed);
    }
    oversampler.reset();
    soloBand = -1;
    soloMix = 0.0;
    soloState = {};
    modeFade = 1.0;
    wetMix = globalBypass ? 0.0 : 1.0;
    gainMix = wetMix;
    peakHoldLin = 0.0;
    outPeakDb.store (-100.0f, std::memory_order_relaxed);
    lastAutoGainSignature = 1.0e30;
    outGain = outGainTarget;
}

void EqEngine::setBand (int index, const BandParams& params) noexcept
{
    if (index >= 0 && index < kMaxBands)
        bands[(size_t) index].target = params;
}

void EqEngine::setOutputGainDb (double gainDb) noexcept
{
    userOutGainDb = std::clamp (gainDb, -kMaxGainDb, kMaxGainDb);
}

void EqEngine::setGainScale (double scale) noexcept
{
    gainScale = std::clamp (scale, 0.0, 2.0);
}

void EqEngine::setGlobalBypass (bool bypassed) noexcept
{
    globalBypass = bypassed;
}

void EqEngine::setQualityMode (QualityMode mode) noexcept
{
    targetQuality = mode;
}

void EqEngine::setAutoGain (bool enabled) noexcept
{
    autoGainEnabled = enabled;
}

void EqEngine::updateAutoGainIfNeeded() noexcept
{
    if (! autoGainEnabled)
    {
        autoGainDbCache = 0.0;
        lastAutoGainSignature = 1.0e30;
        outGainTarget = std::pow (10.0, userOutGainDb / 20.0);
        return;
    }

    double sig = gainScale * 17.3;
    for (int b = 0; b < kMaxBands; ++b)
    {
        auto t = bands[(size_t) b].target;
        t.gainDb = std::clamp (t.gainDb * gainScale, -kMaxGainDb * 1.5, kMaxGainDb * 1.5);
        autoGainSnap[(size_t) b] = t;
        if (t.active() && hasGain (t.type))
            sig += (double) (b + 1) * (t.freqHz * 0.013 + t.gainDb * 1.7 + t.q * 0.31 + (double) t.type * 3.1 + (double) t.placement * 0.7);
    }
    if (std::abs (sig - lastAutoGainSignature) > 1.0e-6)
    {
        lastAutoGainSignature = sig;
        autoGainDbCache = computeAutoGainDb (autoGainSnap, dspRate);
    }
    const double totalDb = std::clamp (userOutGainDb + autoGainDbCache, -36.0, 36.0);
    outGainTarget = std::pow (10.0, totalDb / 20.0);
}

void EqEngine::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    const int nch = std::min (numChannels, kMaxChannels);
    if (nch <= 0 || numSamples <= 0)
        return;

    const bool wantOversampled = (targetQuality == QualityMode::NaturalPhase && sampleRate < 88200.0);
    if (firstBlockAfterPrepare)
    {
        firstBlockAfterPrepare = false;
        if (wantOversampled != activeOversampled)
        {
            activeOversampled = wantOversampled;
            oversampler.reset();
            updateRates();
        }
    }

    updateAutoGainIfNeeded();

    double blockPeak = 0.0;

    for (int offset = 0; offset < numSamples; offset += kSubBlock)
    {
        const int len = std::min (kSubBlock, numSamples - offset);

        // Transición sin clics (EB-04) al conmutar Latencia Cero <-> Fase Natural en caliente.
        if (modeFade <= 0.0 && wantOversampled != activeOversampled)
        {
            activeOversampled = wantOversampled;
            oversampler.reset();
            for (auto& r : bands)
            {
                r.configured = false;
                r.state = {};
                r.detectorState = {};
                r.detectorFreq = -1.0;
            }
            soloState = {};
            soloBand = -1;
            updateRates();
        }

        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < len; ++i)
                baseBuf[(size_t) c][(size_t) i] = (double) channels[c][offset + i];

        for (int i = 0; i < len; ++i)
            tapScratch[(size_t) i] = (float) (nch == 2 ? 0.5 * (baseBuf[0][(size_t) i] + baseBuf[1][(size_t) i]) : baseBuf[0][(size_t) i]);
        pre.push (tapScratch.data(), (size_t) len);

        std::array<std::array<double, kSubBlock>, kMaxChannels> dryDelayed {};
        if (activeOversampled)
        {
            for (int c = 0; c < nch; ++c)
            {
                oversampler.upsample (c, baseBuf[(size_t) c].data(), input[(size_t) c].data(), len);
                std::copy_n (input[(size_t) c].data(), len * 2, work[(size_t) c].data());
            }
            processSubBlock (nch, len * 2);
            // En Fase Natural, el bypass global mantiene la misma latencia de 31 muestras para no desfasar el PDC del DAW.
            const double wTarget = globalBypass ? 0.0 : 1.0;
            for (int c = 0; c < nch; ++c)
            {
                double wm = wetMix;
                for (int i = 0; i < len * 2; ++i)
                {
                    wm = stepToward (wm, wTarget, mixStep);
                    work[(size_t) c][(size_t) i] = input[(size_t) c][(size_t) i] + wm * (work[(size_t) c][(size_t) i] - input[(size_t) c][(size_t) i]);
                }
                if (c == nch - 1)
                    wetMix = wm;
                oversampler.downsample (c, work[(size_t) c].data(), baseBuf[(size_t) c].data(), len);
            }
        }
        else
        {
            for (int c = 0; c < nch; ++c)
                for (int i = 0; i < len; ++i)
                {
                    dryDelayed[(size_t) c][(size_t) i] = baseBuf[(size_t) c][(size_t) i];
                    work[(size_t) c][(size_t) i] = input[(size_t) c][(size_t) i] = baseBuf[(size_t) c][(size_t) i];
                }
            processSubBlock (nch, len);
            for (int c = 0; c < nch; ++c)
                for (int i = 0; i < len; ++i)
                    baseBuf[(size_t) c][(size_t) i] = work[(size_t) c][(size_t) i];
        }

        // Ganancia de salida + fundido de bypass global (en 1x) + fundido de cambio de modo.
        const double g0 = outGain;
        const double gStep = (outGainTarget - g0) / (double) len;
        double mf = modeFade;
        double wm = wetMix;
        double gm = gainMix;
        const double wTarget = globalBypass ? 0.0 : 1.0;
        const double mfTarget = (wantOversampled == activeOversampled) ? 1.0 : 0.0;
        for (int i = 0; i < len; ++i)
        {
            mf = stepToward (mf, mfTarget, baseMixStep);
            if (! activeOversampled)
                wm = stepToward (wm, wTarget, baseMixStep);
            gm = stepToward (gm, wTarget, baseMixStep);
            const double g = g0 + gStep * (double) (i + 1);
            // En Fase Natural la mezcla seco/húmedo ya se hizo a 2x; la ganancia de salida y el Auto-Gain
            // se funden aparte para que el bypass global sea completo (EB-18).
            const double wetGain = (activeOversampled ? 1.0 + gm * (g - 1.0) : g) * mf;
            for (int c = 0; c < nch; ++c)
            {
                const double wet = baseBuf[(size_t) c][(size_t) i] * wetGain;
                const double y = activeOversampled ? wet : (dryDelayed[(size_t) c][(size_t) i] * mf + wm * (wet - dryDelayed[(size_t) c][(size_t) i] * mf));
                baseBuf[(size_t) c][(size_t) i] = y;
                channels[c][offset + i] = (float) y;
                blockPeak = std::max (blockPeak, std::abs (y));
            }
        }
        outGain = outGainTarget;
        modeFade = mf;
        gainMix = gm;
        if (! activeOversampled)
            wetMix = wm;

        for (int i = 0; i < len; ++i)
            tapScratch[(size_t) i] = (float) (nch == 2 ? 0.5 * (baseBuf[0][(size_t) i] + baseBuf[1][(size_t) i]) : baseBuf[0][(size_t) i]);
        post.push (tapScratch.data(), (size_t) len);
    }

    const double decay = std::exp (-(double) numSamples / (0.18 * sampleRate));
    peakHoldLin = std::max (blockPeak, peakHoldLin * decay);
    const float pkDb = peakHoldLin > 1.0e-5 ? (float) (20.0 * std::log10 (peakHoldLin)) : -100.0f;
    outPeakDb.store (pkDb, std::memory_order_relaxed);
}

void EqEngine::processSubBlock (int nch, int len) noexcept
{
    for (int b = 0; b < kMaxBands; ++b)
        processBand (b, bands[(size_t) b], nch, len);

    // Solo: se escucha la zona de la banda (paso banda SVF sobre la entrada), con fundido de 5 ms.
    int solo = -1;
    for (int b = 0; b < kMaxBands; ++b)
        if (bands[(size_t) b].target.used && bands[(size_t) b].target.solo)
        {
            solo = b;
            break;
        }

    auto targetSoloFreqLog = [&] (int b) noexcept { return std::log (clampFreq (bands[(size_t) b].target.freqHz)); };
    auto targetSoloQLog = [&] (int b) noexcept
    {
        const auto& t = bands[(size_t) b].target;
        const bool usesQ = usesBandQ (t.type, t.slopeIndex);
        return std::log (std::clamp (usesQ ? clampQ (t.q) : kButterworthQ, 0.3, 10.0));
    };

    double soloTarget = 0.0;
    if (solo >= 0)
    {
        if ((soloBand < 0 || solo != soloBand) && soloMix <= 0.0)
        {
            soloBand = solo;
            soloState = {};
            soloFreqLog = targetSoloFreqLog (solo);
            soloQLog = targetSoloQLog (solo);
            soloFilter = toSvf (designBandPass (std::exp (soloFreqLog), std::exp (soloQLog), dspRate));
            soloTarget = 1.0;
        }
        else if (solo == soloBand)
        {
            soloTarget = 1.0;
        }
    }

    if (soloMix > 0.0 || soloTarget > 0.0)
    {
        if (soloBand >= 0)
        {
            soloFreqLog += (targetSoloFreqLog (soloBand) - soloFreqLog) * smoothAlpha;
            soloQLog += (targetSoloQLog (soloBand) - soloQLog) * smoothAlpha;
        }
        const SvfSection nextSolo = toSvf (designBandPass (std::exp (soloFreqLog), std::exp (soloQLog), dspRate));
        const double start = soloMix;
        for (int c = 0; c < nch; ++c)
        {
            double m = start;
            for (int i = 0; i < len; ++i)
            {
                const double t = (double) (i + 1) / (double) len;
                m = stepToward (m, soloTarget, mixStep);
                const double s = tickSvfLerp (soloFilter, nextSolo, t, soloState[(size_t) c], input[(size_t) c][(size_t) i]);
                auto& y = work[(size_t) c][(size_t) i];
                y = y * (1.0 - m) + s * m;
            }
            soloMix = m;
        }
        soloFilter = nextSolo;
        if (soloMix <= 0.0 && soloTarget <= 0.0)
        {
            soloBand = -1;
            soloState = {};
        }
    }
    else
    {
        soloBand = -1;
        soloState = {};
    }
}

void EqEngine::applyStructure (BandRuntime& r) noexcept
{
    const BandParams& p = r.target;
    r.type = p.type;
    r.slopeIndex = clampSlope (p.slopeIndex);
    r.placement = p.placement;
    r.configured = true;
    r.freqLog = std::log (clampFreq (p.freqHz));
    r.qLog = std::log (clampQ (p.q));
    r.gainDb = std::clamp (p.gainDb * gainScale, -kMaxGainDb * 1.5, kMaxGainDb * 1.5);
    r.state = {};
    r.detectorState = {};
    r.envelope = 0.0;
    r.rmsEnv = 0.0;
    r.dynamicDb = 0.0;
    r.current = toSvf (designBand (r.type, std::exp (r.freqLog), r.gainDb, std::exp (r.qLog), kSlopesDbPerOct[(size_t) r.slopeIndex], dspRate));
    r.next = r.current;
    r.designedFreqLog = r.freqLog;
    r.designedGain = r.gainDb;
    r.designedQLog = r.qLog;
    r.interpolate = false;
}

double EqEngine::runDetector (BandRuntime& r, int nch, int len) noexcept
{
    const BandParams& p = r.target;
    const double f = std::exp (r.freqLog);
    const double q = std::clamp (r.type == FilterType::FlatTilt ? kButterworthQ : std::exp (r.qLog), 0.3, 10.0);
    if (std::abs (f - r.detectorFreq) > 1.0e-3 * f || std::abs (q - r.detectorQ) > 1.0e-3 * q)
    {
        r.detector = toSvf (designBandPass (f, q, dspRate));
        r.detectorFreq = f;
        r.detectorQ = q;
    }
    const double atk = std::exp (-1.0 / (std::max (p.attackMs, 0.05) * 1.0e-3 * dspRate));
    const double rel = std::exp (-1.0 / (std::max (p.releaseMs, 1.0) * 1.0e-3 * dspRate));
    const double rmsAlpha = 1.0 - std::exp (-1.0 / (0.008 * dspRate));
    const bool stereo = nch > 1;

    for (int i = 0; i < len; ++i)
    {
        const double l = input[0][(size_t) i];
        const double rr = stereo ? input[1][(size_t) i] : l;
        double d = 0.0;
        switch (stereo ? r.placement : Placement::Left)
        {
            case Placement::Stereo:
                d = std::max (std::abs (tickSvf (r.detector, r.detectorState[0], l)),
                              std::abs (tickSvf (r.detector, r.detectorState[1], rr)));
                break;
            case Placement::Left:  d = std::abs (tickSvf (r.detector, r.detectorState[0], l)); break;
            case Placement::Right: d = std::abs (tickSvf (r.detector, r.detectorState[0], rr)); break;
            case Placement::Mid:   d = std::abs (tickSvf (r.detector, r.detectorState[0], 0.5 * (l + rr))); break;
            case Placement::Side:  d = std::abs (tickSvf (r.detector, r.detectorState[0], 0.5 * (l - rr))); break;
            case Placement::Count: break;
        }
        // Detector híbrido pico + RMS (2 * varianza para coincidir con amplitud de seno en régimen permanente):
        // evita modulación sobre el ciclo en subgraves manteniendo respuesta inmediata a transitorios.
        r.rmsEnv += (d * d - r.rmsEnv) * rmsAlpha;
        const double hybrid = std::max (d, std::sqrt (2.0 * r.rmsEnv) * 0.85);
        r.envelope = hybrid > r.envelope ? atk * r.envelope + (1.0 - atk) * hybrid : rel * r.envelope + (1.0 - rel) * hybrid;
    }

    const double levelDb = 20.0 * std::log10 (r.envelope + 1.0e-12);
    const double over = levelDb - p.thresholdDb;
    double comp = 0.0;
    if (over >= kKneeDb * 0.5)
        comp = over;
    else if (over > -kKneeDb * 0.5)
        comp = (over + kKneeDb * 0.5) * (over + kKneeDb * 0.5) / (2.0 * kKneeDb);
    const double amount = comp * (1.0 - 1.0 / std::max (p.ratio, 1.0));
    const double range = std::clamp (p.rangeDb * gainScale, -kMaxGainDb * 1.5, kMaxGainDb * 1.5);
    return std::min (amount, std::abs (range)) * (range < 0.0 ? -1.0 : 1.0);
}

void EqEngine::filterChannel (BandRuntime& r, int slot, double* x, int len, double mixStart, double mixTarget) noexcept
{
    auto& st = r.state[(size_t) slot];
    double m = mixStart;
    if (r.interpolate)
    {
        const double invLen = 1.0 / (double) len;
        for (int i = 0; i < len; ++i)
        {
            const double t = (double) (i + 1) * invLen;
            double v = x[i];
            for (int s = 0; s < r.current.count; ++s)
                v = tickSvfLerp (r.current.sections[(size_t) s], r.next.sections[(size_t) s], t, st[(size_t) s], v);
            m = stepToward (m, mixTarget, mixStep);
            x[i] += m * (v - x[i]);
        }
    }
    else
    {
        for (int i = 0; i < len; ++i)
        {
            double v = x[i];
            for (int s = 0; s < r.current.count; ++s)
                v = tickSvf (r.current.sections[(size_t) s], st[(size_t) s], v);
            m = stepToward (m, mixTarget, mixStep);
            x[i] += m * (v - x[i]);
        }
    }
}

void EqEngine::processBand (int index, BandRuntime& r, int nch, int len) noexcept
{
    const BandParams& p = r.target;
    const bool wantOn = p.active();
    auto& dynOut = dynGainDb[(size_t) index];

    if (! r.configured)
    {
        if (! wantOn)
            return;
        applyStructure (r);
        r.mix = 0.0;
    }

    const bool structureDiffers = p.type != r.type || clampSlope (p.slopeIndex) != r.slopeIndex || p.placement != r.placement;
    if (structureDiffers)
    {
        if (r.mix <= 0.0)
        {
            applyStructure (r);
            r.mixTarget = wantOn ? 1.0 : 0.0;
        }
        else
        {
            r.mixTarget = 0.0;
        }
    }
    else
    {
        r.mixTarget = wantOn ? 1.0 : 0.0;
    }

    if (r.mix <= 0.0 && r.mixTarget <= 0.0)
    {
        r.configured = false;
        r.envelope = 0.0;
        r.rmsEnv = 0.0;
        r.dynamicDb = 0.0;
        dynOut.store (0.0f, std::memory_order_relaxed);
        return;
    }

    r.freqLog += (std::log (clampFreq (p.freqHz)) - r.freqLog) * smoothAlpha;
    r.qLog += (std::log (clampQ (p.q)) - r.qLog) * smoothAlpha;
    r.gainDb += (std::clamp (p.gainDb * gainScale, -kMaxGainDb * 1.5, kMaxGainDb * 1.5) - r.gainDb) * smoothAlpha;

    double dyn = 0.0;
    if (p.dynOn && hasGain (r.type))
        dyn = runDetector (r, nch, len);
    else
    {
        r.envelope = 0.0;
        r.rmsEnv = 0.0;
    }
    r.dynamicDb = dyn;
    dynOut.store ((float) dyn, std::memory_order_relaxed);

    const double totalGain = r.gainDb + dyn;
    const bool changed = std::abs (r.freqLog - r.designedFreqLog) > 1.0e-7 || std::abs (r.qLog - r.designedQLog) > 1.0e-7
                         || std::abs (totalGain - r.designedGain) > 1.0e-6;
    r.interpolate = changed;
    if (changed)
    {
        r.next = toSvf (designBand (r.type, std::exp (r.freqLog), totalGain, std::exp (r.qLog), kSlopesDbPerOct[(size_t) r.slopeIndex], dspRate));
        r.designedFreqLog = r.freqLog;
        r.designedQLog = r.qLog;
        r.designedGain = totalGain;
    }

    const double mixStart = r.mix;
    const bool stereo = nch > 1;
    const Placement place = stereo ? r.placement : Placement::Stereo;
    switch (place)
    {
        case Placement::Stereo:
            for (int c = 0; c < nch; ++c)
                filterChannel (r, c, work[(size_t) c].data(), len, mixStart, r.mixTarget);
            break;
        case Placement::Left:
            filterChannel (r, 0, work[0].data(), len, mixStart, r.mixTarget);
            break;
        case Placement::Right:
            filterChannel (r, 1, work[1].data(), len, mixStart, r.mixTarget);
            break;
        case Placement::Mid:
        case Placement::Side:
        {
            std::array<double, kMaxWork> mid {}, side {};
            for (int i = 0; i < len; ++i)
            {
                mid[(size_t) i] = 0.5 * (work[0][(size_t) i] + work[1][(size_t) i]);
                side[(size_t) i] = 0.5 * (work[0][(size_t) i] - work[1][(size_t) i]);
            }
            filterChannel (r, 0, place == Placement::Mid ? mid.data() : side.data(), len, mixStart, r.mixTarget);
            for (int i = 0; i < len; ++i)
            {
                work[0][(size_t) i] = mid[(size_t) i] + side[(size_t) i];
                work[1][(size_t) i] = mid[(size_t) i] - side[(size_t) i];
            }
            break;
        }
        case Placement::Count:
            break;
    }

    double m = mixStart;
    for (int i = 0; i < len; ++i)
        m = stepToward (m, r.mixTarget, mixStep);
    r.mix = m;
    if (r.interpolate)
        r.current = r.next;
}
} // namespace zeq
