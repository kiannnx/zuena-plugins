#include "FilterDesign.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace zeq
{
namespace
{
constexpr double kPi = std::numbers::pi;
constexpr double kMaxPoleW = 0.98 * kPi;  // los polos se limitan por debajo de Nyquist
constexpr double kMaxFitW = 0.92 * kPi;   // frecuencia máxima del punto de ajuste
constexpr double kButterworthQ = 0.70710678118654752;

/** Sección analógica: (n2 s² + n1 s + n0) / (d2 s² + d1 s + d0). Con d2 = n2 = 0 es de primer orden. */
struct Analog
{
    double n2, n1, n0, d2, d1, d0;
};

struct Proto
{
    std::array<Analog, kMaxSections> s {};
    int count = 0;
    double gain = 1.0;
};

double mag2 (const Analog& a, double w) noexcept
{
    const double w2 = w * w;
    const double nr = a.n0 - a.n2 * w2, ni = a.n1 * w;
    const double dr = a.d0 - a.d2 * w2, di = a.d1 * w;
    return (nr * nr + ni * ni) / (dr * dr + di * di);
}

int orderFromSlope (int slopeDbPerOct) noexcept
{
    return std::clamp (slopeDbPerOct / 6, 1, 16);
}

void addCut (Proto& p, bool highPass, double w0, double q, int order) noexcept
{
    const int pairs = order / 2;
    const double resonance = q / kButterworthQ;
    for (int k = 0; k < pairs; ++k)
    {
        double qk = 1.0 / (2.0 * std::sin ((2.0 * k + 1.0) * kPi / (2.0 * order)));
        if (k == 0)
            qk *= resonance;  // la sección más resonante lleva el Q del usuario
        p.s[(size_t) p.count++] = highPass ? Analog { 1.0, 0.0, 0.0, 1.0, w0 / qk, w0 * w0 }
                                           : Analog { 0.0, 0.0, w0 * w0, 1.0, w0 / qk, w0 * w0 };
    }
    if (order % 2 == 1)
        p.s[(size_t) p.count++] = highPass ? Analog { 0.0, 1.0, 0.0, 0.0, 1.0, w0 }
                                           : Analog { 0.0, 0.0, w0, 0.0, 1.0, w0 };
}

/** Cascada logarítmica de 4 polos/ceros reales para inclinación espectral lineal en dB/octava (Flat Tilt). */
void addFlatTilt (Proto& p, double wPivot, double gainDb, double wScale) noexcept
{
    constexpr std::array<double, 4> kPivotsHz { 22.0, 140.0, 900.0, 5800.0 };
    const double alpha = std::pow (10.0, gainDb / 160.0);
    for (double hz : kPivotsHz)
    {
        const double wk = hz * wScale;
        const double wz = wk / alpha;
        const double wp = wk * alpha;
        p.s[(size_t) p.count++] = { 0.0, 1.0 / wz, 1.0, 0.0, 1.0 / wp, 1.0 };
    }
    double m2 = 1.0;
    for (int i = 0; i < p.count; ++i)
        m2 *= mag2 (p.s[(size_t) i], wPivot);
    p.gain = 1.0 / std::sqrt (std::max (m2, 1.0e-30));
}

/** Prototipo analógico. wScale = 2*pi/fs en síntesis digital, o 1.0 cuando w0 está en Hz. */
Proto makeProto (FilterType type, double w0, double gainDb, double q, int slopeDbPerOct, double wScale = 1.0) noexcept
{
    q = std::clamp (q, kMinQ, kMaxQ);
    gainDb = std::clamp (gainDb, -kMaxGainDb * 2.0, kMaxGainDb * 2.0);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double sA = std::sqrt (A);
    const double w02 = w0 * w0;
    Proto p;
    switch (type)
    {
        case FilterType::Bell:
            p.s[0] = { 1.0, A * w0 / q, w02, 1.0, w0 / (A * q), w02 };
            p.count = 1;
            break;
        case FilterType::LowShelf:
            p.s[0] = { A, A * sA * w0 / q, A * A * w02, A, sA * w0 / q, w02 };
            p.count = 1;
            break;
        case FilterType::HighShelf:
            p.s[0] = { A * A, A * sA * w0 / q, A * w02, 1.0, sA * w0 / q, A * w02 };
            p.count = 1;
            break;
        case FilterType::TiltShelf:
            // Prototipo simétrico autoinverso: H(-g) == 1 / H(+g) y |H(jw0)| == 1 (0 dB).
            p.s[0] = { A, sA * w0 / q, w02, 1.0, sA * w0 / q, A * w02 };
            p.count = 1;
            break;
        case FilterType::FlatTilt:
            addFlatTilt (p, w0, gainDb, wScale);
            break;
        case FilterType::Notch:
            p.s[0] = { 1.0, 0.0, w02, 1.0, w0 / q, w02 };
            p.count = 1;
            break;
        case FilterType::BandPass:
            p.s[0] = { 0.0, w0 / q, 0.0, 1.0, w0 / q, w02 };
            p.count = 1;
            break;
        case FilterType::LowCut:
            addCut (p, true, w0, q, orderFromSlope (slopeDbPerOct));
            break;
        case FilterType::HighCut:
            addCut (p, false, w0, q, orderFromSlope (slopeDbPerOct));
            break;
        case FilterType::AllPass:
            p.s[0] = { 1.0, -w0 / q, w02, 1.0, w0 / q, w02 };
            p.count = 1;
            break;
        case FilterType::Count:
            break;
    }
    return p;
}

Biquad matchFirstOrder (const Analog& a) noexcept
{
    const double wp = std::min (a.d0 / a.d1, kMaxPoleW);
    const double pole = std::exp (-wp);
    Biquad s;
    s.a1 = -pole;
    const double W = std::sqrt (mag2 (a, 0.0)) * (1.0 + s.a1);
    const double V = std::sqrt (mag2 (a, kPi)) * (1.0 - s.a1);
    s.b0 = 0.5 * (W + V);
    s.b1 = 0.5 * (W - V);
    return s;
}

Biquad matchSecondOrder (const Analog& a, double wFit) noexcept
{
    const double wp = std::sqrt (a.d0 / a.d2);
    const double zeta = a.d1 / (2.0 * std::sqrt (a.d0 * a.d2));
    const double w = std::min (wp, kMaxPoleW);

    Biquad s;
    const double e = std::exp (-zeta * w);
    s.a1 = zeta <= 1.0 ? -2.0 * e * std::cos (w * std::sqrt (1.0 - zeta * zeta))
                       : -2.0 * e * std::cosh (w * std::sqrt (zeta * zeta - 1.0));
    s.a2 = e * e;

    const double A0 = (1.0 + s.a1 + s.a2) * (1.0 + s.a1 + s.a2);
    const double A1 = (1.0 - s.a1 + s.a2) * (1.0 - s.a1 + s.a2);
    const double A2 = -4.0 * s.a2;

    const double wf = std::clamp (wFit, 1.0e-6, kMaxFitW);
    const double sh = std::sin (0.5 * wf);
    const double phi1 = sh * sh;
    const double phi0 = 1.0 - phi1;
    const double phi2 = 4.0 * phi0 * phi1;
    const double Dwf = A0 * phi0 + A1 * phi1 + A2 * phi2;

    // Paso alto puro de 2.º orden: doble cero exacto en DC (z = 1) + coincidencia exacta en wf.
    if (a.n0 == 0.0 && a.n1 == 0.0 && a.n2 > 0.0)
    {
        const double b0 = std::sqrt (std::max (mag2 (a, wf) * Dwf, 0.0)) / (4.0 * phi1);
        s.b0 = b0;
        s.b1 = -2.0 * b0;
        s.b2 = b0;
        return s;
    }

    const double B0 = mag2 (a, 0.0) * A0;
    const double B1 = mag2 (a, kPi) * A1;
    const double B2 = (mag2 (a, wf) * Dwf - B0 * phi0 - B1 * phi1) / phi2;

    const double W = std::sqrt (std::max (B0, 0.0));
    const double V = std::sqrt (std::max (B1, 0.0));
    const double S = 0.5 * (W + V);
    s.b1 = 0.5 * (W - V);

    // Límite inferior de amortiguamiento de los ceros: evita ceros espurios sobre el círculo unidad
    // cerca de Nyquist que al invertirse se convertirían en polos resonantes (EB-13).
    double rMin = 0.0;
    if (a.n2 > 0.0 && a.n0 > 0.0)
    {
        const double wz = std::min (std::sqrt (a.n0 / a.n2), kMaxPoleW);
        const double zetaz = a.n1 / (2.0 * std::sqrt (a.n0 * a.n2));
        rMin = S * std::tanh (zetaz * wz);
    }

    const double r = std::max (std::sqrt (std::max (S * S + B2, 0.0)), rMin);
    s.b0 = 0.5 * (S + r);
    s.b2 = 0.5 * (S - r);
    if (std::abs (s.b2) > std::abs (s.b0))
        std::swap (s.b0, s.b2);  // fase mínima: ceros dentro del círculo unidad
    return s;
}

Biquad match (const Analog& a, double wFit) noexcept
{
    return a.d2 == 0.0 ? matchFirstOrder (a) : matchSecondOrder (a, wFit);
}

/** 1/H(z). Válido porque el numerador se diseña de fase mínima (ceros dentro del círculo unidad). */
Biquad invert (const Biquad& s) noexcept
{
    const double k = 1.0 / s.b0;
    Biquad r;
    r.b0 = k;
    r.b1 = s.a1 * k;
    r.b2 = s.a2 * k;
    r.a1 = s.b1 * k;
    r.a2 = std::clamp (s.b2 * k, -0.999999, 0.999999);
    return r;
}

/** Muesca con los ceros exactamente sobre el círculo unidad y DC normalizado a 0 dB. */
Biquad designNotch (double w0, double q) noexcept
{
    const Proto p = makeProto (FilterType::Notch, w0, 0.0, q, 12);
    Biquad s = matchSecondOrder (p.s[0], w0);
    const double wz = std::clamp (w0, 1.0e-6, 0.999 * kPi);
    const double c = std::cos (wz);
    const double k = (1.0 + s.a1 + s.a2) / (2.0 - 2.0 * c);
    s.b0 = k;
    s.b1 = -2.0 * c * k;
    s.b2 = k;
    return s;
}

/** All-Pass de 2.º orden de módulo unitario exacto (0.0000 dB) en todo el espectro. */
Biquad designAllPass (double w0, double q) noexcept
{
    const double w = std::clamp (w0, 1.0e-6, 0.995 * kPi);
    const double g = std::tan (0.5 * w);
    const double k = 1.0 / std::clamp (q, kMinQ, kMaxQ);
    const double d0 = 1.0 + g * (g + k);
    const double a1 = 2.0 * (g * g - 1.0) / d0;
    const double a2 = (1.0 + g * (g - k)) / d0;
    return { a2, a1, 1.0, a1, a2 };
}

std::complex<double> sectionResponse (const Biquad& s, std::complex<double> z1) noexcept
{
    const std::complex<double> z2 = z1 * z1;
    return (s.b0 + s.b1 * z1 + s.b2 * z2) / (1.0 + s.a1 * z1 + s.a2 * z2);
}
} // namespace

Design designBand (FilterType type, double freqHz, double gainDb, double q, int slopeDbPerOct, double sampleRate) noexcept
{
    const double wScale = 2.0 * kPi / sampleRate;
    const double w0 = std::clamp (freqHz, kMinFreq, kMaxFreq) * wScale;
    Design d;

    if (type == FilterType::Notch)
    {
        d.sections[0] = designNotch (w0, q);
        d.count = 1;
        return d;
    }

    if (type == FilterType::AllPass)
    {
        d.sections[0] = designAllPass (w0, q);
        d.count = 1;
        return d;
    }

    // Bell, shelves y Tilt son autoinversos (corte = inverso del realce). Se diseña la variante
    // cuyos polos quedan más bajos en frecuencia (representables) y se invierte si hace falta.
    bool invertResult = false;
    double g = gainDb;
    if ((type == FilterType::Bell || type == FilterType::LowShelf) && gainDb < 0.0)
    {
        g = -gainDb;
        invertResult = true;
    }
    else if ((type == FilterType::HighShelf || type == FilterType::TiltShelf) && gainDb > 0.0)
    {
        g = -gainDb;
        invertResult = true;
    }

    const Proto p = makeProto (type, w0, g, q, slopeDbPerOct, wScale);
    const bool isShelf = type == FilterType::LowShelf || type == FilterType::HighShelf || type == FilterType::TiltShelf;
    d.count = p.count;
    for (int i = 0; i < p.count; ++i)
    {
        const auto& a = p.s[(size_t) i];
        // En shelves, los polos ya fijan su esquina por invariancia; ajustar el numerador en la
        // esquina de los ceros clava la resonancia y reduce el error a casi la mitad.
        const double wf = (isShelf && a.n2 > 0.0 && a.n0 > 0.0) ? std::sqrt (a.n0 / a.n2) : w0;
        d.sections[(size_t) i] = match (a, wf);
    }
    if (invertResult)
        d.sections[0] = invert (d.sections[0]);
    if (p.gain != 1.0 && d.count > 0)
    {
        auto& s0 = d.sections[0];
        s0.b0 *= p.gain;
        s0.b1 *= p.gain;
        s0.b2 *= p.gain;
    }
    return d;
}

Biquad designBandPass (double freqHz, double q, double sampleRate) noexcept
{
    const double w0 = 2.0 * kPi * std::clamp (freqHz, kMinFreq, kMaxFreq) / sampleRate;
    const Proto p = makeProto (FilterType::BandPass, w0, 0.0, q, 12);
    return match (p.s[0], w0);
}

SvfSection toSvf (const Biquad& b) noexcept
{
    SvfSection s;
    if (b.a2 == 0.0 && b.b2 == 0.0)
    {
        const double dm = std::max (1.0 - b.a1, 1.0e-15);
        const double dp = std::max (1.0 + b.a1, 1.0e-15);
        s.order = 1;
        s.g = dp / dm;
        s.a1 = s.g / (1.0 + s.g);
        s.chp = (b.b0 - b.b1) / dm;
        s.cbp = (b.b0 + b.b1) / dp - s.chp;
        s.clp = 0.0;
        return s;
    }

    const double dp = std::max (1.0 + b.a1 + b.a2, 1.0e-18);
    const double dm = std::max (1.0 - b.a1 + b.a2, 1.0e-18);
    const double root = std::sqrt (dp * dm);
    s.order = 2;
    s.g = std::sqrt (dp / dm);
    s.k = 2.0 * (1.0 - b.a2) / root;
    s.a1 = 1.0 / (1.0 + s.g * (s.g + s.k));
    s.a2 = s.g * s.a1;
    s.a3 = s.g * s.a2;
    s.chp = (b.b0 - b.b1 + b.b2) / dm;
    s.cbp = 2.0 * (b.b0 - b.b2) / root;
    s.clp = (b.b0 + b.b1 + b.b2) / dp;
    return s;
}

SvfDesign toSvf (const Design& d) noexcept
{
    SvfDesign out;
    out.count = d.count;
    for (int i = 0; i < d.count; ++i)
        out.sections[(size_t) i] = toSvf (d.sections[(size_t) i]);
    return out;
}

std::complex<double> response (const Biquad& s, double freqHz, double sampleRate) noexcept
{
    const double w = 2.0 * kPi * freqHz / sampleRate;
    return sectionResponse (s, std::polar (1.0, -w));
}

std::complex<double> response (const Design& d, double freqHz, double sampleRate) noexcept
{
    const double w = 2.0 * kPi * freqHz / sampleRate;
    const std::complex<double> z1 = std::polar (1.0, -w);
    std::complex<double> h { 1.0, 0.0 };
    for (int i = 0; i < d.count; ++i)
        h *= sectionResponse (d.sections[(size_t) i], z1);
    return h;
}

double phaseDegrees (const Design& d, double freqHz, double sampleRate) noexcept
{
    const auto h = response (d, freqHz, sampleRate);
    return std::arg (h) * (180.0 / kPi);
}

double computeAutoGainDb (std::span<const BandParams> bands, double sampleRate) noexcept
{
    // Integra la energía en escala logarítmica (equivalente a ponderación 1/f de ruido rosa)
    // entre 40 Hz y 14 kHz solo sobre bandas con ganancia estática (ignora cortes puros y all-pass).
    constexpr int kPoints = 24;
    std::array<Design, kMaxBands> designs {};
    std::array<double, kMaxBands> weights {};
    int activeCount = 0;

    for (const auto& b : bands)
    {
        if (! b.active() || ! hasGain (b.type) || std::abs (b.gainDb) < 1.0e-3)
            continue;
        designs[(size_t) activeCount] = designBand (b.type, b.freqHz, b.gainDb, b.q, 12, sampleRate);
        // Side aporta menos sonoridad percibida global que Estéreo/Mid; L o R aporta mitad de potencia.
        weights[(size_t) activeCount] = (b.placement == Placement::Left || b.placement == Placement::Right) ? 0.5
                                      : (b.placement == Placement::Side ? 0.35 : 1.0);
        ++activeCount;
    }
    if (activeCount == 0)
        return 0.0;

    double powerSum = 0.0;
    for (int k = 0; k < kPoints; ++k)
    {
        const double f = 40.0 * std::pow (14000.0 / 40.0, (double) k / (double) (kPoints - 1));
        double db = 0.0;
        for (int i = 0; i < activeCount; ++i)
        {
            const double m = std::max (std::abs (response (designs[(size_t) i], f, sampleRate)), 1.0e-6);
            db += weights[(size_t) i] * (20.0 * std::log10 (m));
        }
        powerSum += std::pow (10.0, db / 10.0);
    }
    const double meanDb = 10.0 * std::log10 (std::max (powerSum / (double) kPoints, 1.0e-6));
    return std::clamp (-meanDb, -12.0, 12.0);
}

double analogMagnitudeDb (FilterType type, double freqHz, double gainDb, double q, int slopeDbPerOct, double evalHz) noexcept
{
    const Proto p = makeProto (type, std::clamp (freqHz, kMinFreq, kMaxFreq), gainDb, q, slopeDbPerOct, 1.0);
    double m2 = p.gain * p.gain;
    for (int i = 0; i < p.count; ++i)
        m2 *= mag2 (p.s[(size_t) i], evalHz);
    return 10.0 * std::log10 (std::max (m2, 1.0e-30));
}
} // namespace zeq
