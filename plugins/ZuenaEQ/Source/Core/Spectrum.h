#pragma once
// Analizador de espectro de alta resolución (consumidor, hilo de UI).
// - Ventana Blackman-Harris de 4 términos (-92 dB de lóbulos laterales) sobre FFT radix-2 de 8192 puntos (solape 75 %).
// - Inclinación perceptual de +3 dB/oct centrada en 1 kHz (el ruido rosa queda horizontal).
// - Retención de picos (Peak Hold) con caída suave para lectura clara de transitorios y resonancias.
// Sin JUCE.

#include "SpscRing.h"

#include <complex>
#include <vector>

namespace zeq
{
class Spectrum
{
public:
    static constexpr int kOrder = 13;
    static constexpr int kSize = 1 << kOrder;
    static constexpr int kHop = kSize / 4;

    Spectrum();

    void setSampleRate (double sr) noexcept { sampleRate = sr > 0.0 ? sr : 48000.0; }
    void setFrozen (bool freeze) noexcept { frozen = freeze; }
    bool isFrozen() const noexcept { return frozen; }

    /** Consume lo disponible en la cola. Devuelve true si se calculó un espectro nuevo. */
    bool update (SpscRing& source);

    /** Nivel suavizado en dBFS en la frecuencia dada, con interpolación parabólica / lineal e inclinación perceptual. */
    float levelDbAt (double hz) const noexcept;
    float peakDbAt (double hz) const noexcept;

    /** Nivel máximo suavizado o de retención de picos entre dos frecuencias (para columnas de píxel). */
    float maxLevelDbBetween (double loHz, double hiHz) const noexcept;
    float maxPeakDbBetween (double loHz, double hiHz) const noexcept;

    void clear();

private:
    void compute();
    float binTiltDb (int bin) const noexcept;

    double sampleRate = 48000.0;
    std::vector<float> history;
    std::vector<float> incoming;
    std::vector<float> window;
    std::vector<std::complex<float>> fft;
    std::vector<std::complex<float>> twiddles;
    std::vector<int> bitReverse;
    std::vector<float> smoothedDb;
    std::vector<float> peakHoldDb;
    std::vector<int> peakHoldFrames;
    int pendingSinceLast = 0;
    bool frozen = false;
};
} // namespace zeq
