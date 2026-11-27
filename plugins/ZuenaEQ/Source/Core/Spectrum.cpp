#include "Spectrum.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace zeq
{
namespace
{
constexpr float kFloorDb = -120.0f;
constexpr float kReleasePerFrameDb = 1.4f;
constexpr float kPeakFallPerFrameDb = 0.45f;
constexpr int kPeakHoldCount = 28;            // ~1.2 s de retención antes de caer
constexpr float kTiltDbPerOct = 3.0f;         // ruido rosa horizontal alrededor de 1 kHz
} // namespace

Spectrum::Spectrum()
    : history ((size_t) kSize, 0.0f),
      incoming ((size_t) kSize, 0.0f),
      window ((size_t) kSize),
      fft ((size_t) kSize),
      twiddles ((size_t) kSize / 2),
      bitReverse ((size_t) kSize),
      smoothedDb ((size_t) kSize / 2 + 1, kFloorDb),
      peakHoldDb ((size_t) kSize / 2 + 1, kFloorDb),
      peakHoldFrames ((size_t) kSize / 2 + 1, 0)
{
    const double pi = std::numbers::pi;
    // Ventana Blackman-Harris de 4 términos (-92 dB de rechazo lateral, ganancia coherente = 0.35875).
    for (int i = 0; i < kSize; ++i)
    {
        const double ph = 2.0 * pi * (double) i / (double) (kSize - 1);
        window[(size_t) i] = (float) (0.35875 - 0.48829 * std::cos (ph) + 0.14128 * std::cos (2.0 * ph) - 0.01168 * std::cos (3.0 * ph));
    }
    for (int i = 0; i < kSize / 2; ++i)
        twiddles[(size_t) i] = std::polar (1.0f, (float) (-2.0 * pi * (double) i / (double) kSize));
    for (int i = 0; i < kSize; ++i)
    {
        int r = 0;
        for (int b = 0; b < kOrder; ++b)
            r |= ((i >> b) & 1) << (kOrder - 1 - b);
        bitReverse[(size_t) i] = r;
    }
}

void Spectrum::clear()
{
    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (smoothedDb.begin(), smoothedDb.end(), kFloorDb);
    std::fill (peakHoldDb.begin(), peakHoldDb.end(), kFloorDb);
    std::fill (peakHoldFrames.begin(), peakHoldFrames.end(), 0);
    pendingSinceLast = 0;
}

float Spectrum::binTiltDb (int bin) const noexcept
{
    const double hz = std::max (20.0, (double) bin * sampleRate / (double) kSize);
    return kTiltDbPerOct * (float) std::log2 (hz / 1000.0);
}

bool Spectrum::update (SpscRing& source)
{
    if (frozen)
    {
        // Drena la cola para no acumular audio viejo mientras la imagen espectral está congelada.
        while (source.pop (incoming.data(), (size_t) kHop) > 0) {}
        return false;
    }

    bool computed = false;
    for (;;)
    {
        const auto got = (int) source.pop (incoming.data(), (size_t) kHop);
        if (got <= 0)
            break;
        std::move (history.begin() + got, history.end(), history.begin());
        std::copy (incoming.begin(), incoming.begin() + got, history.end() - got);
        pendingSinceLast += got;
        if (pendingSinceLast >= kHop)
        {
            pendingSinceLast = 0;
            compute();
            computed = true;
        }
    }
    return computed;
}

void Spectrum::compute()
{
    for (int i = 0; i < kSize; ++i)
        fft[(size_t) bitReverse[(size_t) i]] = { history[(size_t) i] * window[(size_t) i], 0.0f };

    for (int len = 2; len <= kSize; len <<= 1)
    {
        const int half = len / 2;
        const int stride = kSize / len;
        for (int start = 0; start < kSize; start += len)
            for (int k = 0; k < half; ++k)
            {
                const auto w = twiddles[(size_t) (k * stride)];
                auto& a = fft[(size_t) (start + k)];
                auto& b = fft[(size_t) (start + k + half)];
                const auto t = w * b;
                b = a - t;
                a = a + t;
            }
    }

    // Normalización exacta para Blackman-Harris (ganancia coherente = 0.35875): seno a 0 dBFS en 1 kHz -> 0.0 dB.
    const float norm = 2.0f / (0.35875f * (float) kSize);
    for (int i = 0; i <= kSize / 2; ++i)
    {
        const float mag = std::abs (fft[(size_t) i]) * norm;
        const float rawDb = 20.0f * std::log10 (mag + 1.0e-9f);
        const float db = rawDb <= kFloorDb + 1.0f ? kFloorDb : std::max (kFloorDb, rawDb + binTiltDb (i));

        auto& s = smoothedDb[(size_t) i];
        s = db > s ? s + 0.65f * (db - s) : std::max (db, s - kReleasePerFrameDb);

        auto& pk = peakHoldDb[(size_t) i];
        auto& hold = peakHoldFrames[(size_t) i];
        if (s >= pk)
        {
            pk = s;
            hold = kPeakHoldCount;
        }
        else if (hold > 0)
        {
            --hold;
        }
        else
        {
            pk = std::max (s, pk - kPeakFallPerFrameDb);
        }
    }
}

float Spectrum::levelDbAt (double hz) const noexcept
{
    const double bin = hz * (double) kSize / sampleRate;
    const int i = (int) bin;
    if (i < 0 || i >= kSize / 2)
        return kFloorDb;
    const float frac = (float) (bin - (double) i);
    return smoothedDb[(size_t) i] + (smoothedDb[(size_t) i + 1] - smoothedDb[(size_t) i]) * frac;
}

float Spectrum::peakDbAt (double hz) const noexcept
{
    const double bin = hz * (double) kSize / sampleRate;
    const int i = (int) bin;
    if (i < 0 || i >= kSize / 2)
        return kFloorDb;
    const float frac = (float) (bin - (double) i);
    return peakHoldDb[(size_t) i] + (peakHoldDb[(size_t) i + 1] - peakHoldDb[(size_t) i]) * frac;
}

float Spectrum::maxLevelDbBetween (double loHz, double hiHz) const noexcept
{
    const int a = std::clamp ((int) (loHz * (double) kSize / sampleRate), 0, kSize / 2);
    const int b = std::clamp ((int) (hiHz * (double) kSize / sampleRate), 0, kSize / 2);
    if (b <= a)
        return levelDbAt (0.5 * (loHz + hiHz));
    float m = kFloorDb;
    for (int i = a; i <= b; ++i)
        m = std::max (m, smoothedDb[(size_t) i]);
    return m;
}

float Spectrum::maxPeakDbBetween (double loHz, double hiHz) const noexcept
{
    const int a = std::clamp ((int) (loHz * (double) kSize / sampleRate), 0, kSize / 2);
    const int b = std::clamp ((int) (hiHz * (double) kSize / sampleRate), 0, kSize / 2);
    if (b <= a)
        return peakDbAt (0.5 * (loHz + hiHz));
    float m = kFloorDb;
    for (int i = a; i <= b; ++i)
        m = std::max (m, peakHoldDb[(size_t) i]);
    return m;
}
} // namespace zeq
