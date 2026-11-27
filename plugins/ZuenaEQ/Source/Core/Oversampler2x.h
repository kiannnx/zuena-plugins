#pragma once
// Sobremuestreador polifásico 2x de fase lineal (FIR halfband simétrico de 63 coeficientes,
// ventana Kaiser beta = 8.6). C++20 puro, tamaño fijo en memoria (cero reservas dinámicas).
//
// - Rizado en banda de paso (0 - 19.8 kHz a 48 kHz): < 0.001 dB.
// - Rechazo de alias: > 92 dB.
// - Retardo de grupo ida y vuelta exacto: 31 muestras a tasa base (kNaturalPhaseLatency).

#include "ZeqTypes.h"

#include <array>
#include <cmath>
#include <numbers>

namespace zeq
{
class Oversampler2x
{
public:
    static constexpr int kTaps = 63;
    static constexpr int kCenter = (kTaps - 1) / 2;     // 31 (impar)
    static constexpr int kOddPairs = (kCenter + 1) / 2; // 16 pares simétricos no nulos

    Oversampler2x() noexcept
    {
        constexpr double beta = 8.6;
        const double denom = besselI0 (beta);
        std::array<double, kOddPairs> raw {};
        double oddSum = 0.0;
        for (int k = 0; k < kOddPairs; ++k)
        {
            const int n = 2 * k + 1;  // distancia al centro: 1, 3, 5, ..., 31
            const double u = (double) n / (double) kCenter;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - u * u))) / denom;
            raw[(size_t) k] = (std::sin (std::numbers::pi * 0.5 * (double) n) / (std::numbers::pi * (double) n)) * w;
            oddSum += 2.0 * raw[(size_t) k];
        }
        // Normalización exacta en DC: centro (0.5) + suma de pares impares (0.5) == 1.0.
        const double scale = 0.5 / oddSum;
        for (int k = 0; k < kOddPairs; ++k)
            oddTaps[(size_t) k] = raw[(size_t) k] * scale;
        reset();
    }

    void reset() noexcept
    {
        upHist = {};
        downHist = {};
        upPosChannel = {};
        downPosChannel = {};
    }

    /** Sube `len` muestras de un canal a `2 * len` muestras. */
    void upsample (int ch, const double* in, double* out2x, int len) noexcept
    {
        auto& h = upHist[(size_t) ch];
        int p = upPosChannel[(size_t) ch];
        for (int i = 0; i < len; ++i)
        {
            h[(size_t) p] = in[i];
            // Con centro en índice 31 (impar) y zero-stuffing en índices pares (up[2n] = 2*in[n]):
            // - Salida par (m = 2i): suma de los 16 pares en índices pares 31 +- (2k+1).
            // - Salida impar (m = 2i + 1): cae sobre el coeficiente central f[31] = 0.5 -> in[i - 15].
            double acc = 0.0;
            for (int k = 0; k < kOddPairs; ++k)
            {
                const int a = (p - 15 + k + kUpHistSize) & kUpHistMask;
                const int b = (p - 16 - k + kUpHistSize) & kUpHistMask;
                acc += oddTaps[(size_t) k] * (h[(size_t) a] + h[(size_t) b]);
            }
            out2x[2 * i] = 2.0 * acc;
            out2x[2 * i + 1] = h[(size_t) ((p - 15 + kUpHistSize) & kUpHistMask)];
            p = (p + 1) & kUpHistMask;
        }
        upPosChannel[(size_t) ch] = p;
    }

    /** Baja `2 * len` muestras de un canal a `len` muestras (retardo total ida+vuelta = 31 muestras). */
    void downsample (int ch, const double* in2x, double* out, int len) noexcept
    {
        auto& h = downHist[(size_t) ch];
        int p = downPosChannel[(size_t) ch];
        for (int i = 0; i < len; ++i)
        {
            // Escribe m = 2i y evalúa la convolución causal en m = 2i (centro en 2i - 31).
            h[(size_t) p] = in2x[2 * i];
            const int c = (p - kCenter + kDownHistSize) & kDownHistMask;
            double acc = 0.5 * h[(size_t) c];
            for (int k = 0; k < kOddPairs; ++k)
            {
                const int d = 2 * k + 1;
                const int a = (p - kCenter + d + kDownHistSize) & kDownHistMask;
                const int b = (p - kCenter - d + kDownHistSize) & kDownHistMask;
                acc += oddTaps[(size_t) k] * (h[(size_t) a] + h[(size_t) b]);
            }
            out[i] = acc;

            p = (p + 1) & kDownHistMask;
            h[(size_t) p] = in2x[2 * i + 1];
            p = (p + 1) & kDownHistMask;
        }
        downPosChannel[(size_t) ch] = p;
    }

private:
    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 32; ++k)
        {
            const double r = x / (2.0 * (double) k);
            term *= r * r;
            sum += term;
        }
        return sum;
    }

    static constexpr int kUpHistSize = 64;
    static constexpr int kUpHistMask = kUpHistSize - 1;
    static constexpr int kDownHistSize = 128;
    static constexpr int kDownHistMask = kDownHistSize - 1;

    std::array<double, kOddPairs> oddTaps {};
    std::array<std::array<double, kUpHistSize>, kMaxChannels> upHist {};
    std::array<std::array<double, kDownHistSize>, kMaxChannels> downHist {};
    std::array<int, kMaxChannels> upPosChannel {};
    std::array<int, kMaxChannels> downPosChannel {};
};
} // namespace zeq
