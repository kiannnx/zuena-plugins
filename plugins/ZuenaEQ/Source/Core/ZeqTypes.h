#pragma once
// Tipos compartidos del núcleo DSP de ZUENA EQ. C++20 puro: sin JUCE ni cabeceras de GUI.

#include <array>
#include <cstdint>

namespace zeq
{
inline constexpr int kMaxBands = 24;
inline constexpr int kMaxChannels = 2;
inline constexpr int kSubBlock = 16;             // muestras (a tasa base) entre recálculos de coeficientes
inline constexpr int kMaxSections = 9;           // hasta 8 secciones de 2.º orden (96 dB/oct) + 1 de 1.º orden
inline constexpr int kNaturalPhaseLatency = 31;  // muestras de latencia del sobremuestreador FIR 2x de fase lineal (< 88.2 kHz)

enum class FilterType : std::uint8_t
{
    Bell,
    LowShelf,
    HighShelf,
    LowCut,
    HighCut,
    Notch,
    BandPass,
    TiltShelf,
    FlatTilt,
    AllPass,
    Count
};

enum class Placement : std::uint8_t
{
    Stereo,
    Left,
    Right,
    Mid,
    Side,
    Count
};

enum class QualityMode : std::uint8_t
{
    ZeroLatency = 0,
    NaturalPhase = 1
};

inline constexpr std::array<int, 8> kSlopesDbPerOct { 6, 12, 18, 24, 36, 48, 72, 96 };

inline constexpr double kMinFreq = 10.0;
inline constexpr double kMaxFreq = 30000.0;
inline constexpr double kMaxGainDb = 30.0;
inline constexpr double kMinQ = 0.025;
inline constexpr double kMaxQ = 40.0;

/** true si el tipo tiene ganancia (y por tanto admite dinámica). */
constexpr bool hasGain (FilterType t) noexcept
{
    return t == FilterType::Bell || t == FilterType::LowShelf || t == FilterType::HighShelf
           || t == FilterType::TiltShelf || t == FilterType::FlatTilt;
}

/** true si el tipo usa la pendiente (dB/oct). */
constexpr bool hasSlope (FilterType t) noexcept
{
    return t == FilterType::LowCut || t == FilterType::HighCut;
}

/** true si la combinación de tipo y pendiente utiliza el parámetro Q. */
constexpr bool usesBandQ (FilterType t, int slopeIndex) noexcept
{
    if (t == FilterType::FlatTilt)
        return false;
    if (hasSlope (t) && slopeIndex == 0)
        return false;
    return true;
}

struct BandParams
{
    bool used = false;          // la banda existe en el gráfico
    bool bypass = false;        // existe pero no procesa
    FilterType type = FilterType::Bell;
    double freqHz = 1000.0;
    double gainDb = 0.0;
    double q = 1.0;
    int slopeIndex = 1;         // índice en kSlopesDbPerOct
    Placement placement = Placement::Stereo;
    bool solo = false;

    bool dynOn = false;
    double thresholdDb = -30.0;
    double rangeDb = -6.0;      // negativo: recorta cuando la banda supera el umbral; positivo: realza
    double ratio = 4.0;
    double attackMs = 10.0;
    double releaseMs = 120.0;

    bool active() const noexcept { return used && ! bypass; }
    bool operator== (const BandParams&) const = default; // la UI redibuja solo si algo cambió
};
} // namespace zeq
