#pragma once
#include "../Core/OpticalEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace luma
{
enum class Param : std::size_t
{
    reduction,
    makeup,
    highpass,
    emphasis,
    recovery,
    mix,
    limit,
    external,
    bypass,
    meter,
    attack,
    stereoLink,
    range,
    detectorModel,
    autoMakeup, // 0.4.0: siempre al final para conservar el orden y los ID de las versiones anteriores
    count
};
enum class DetectorModel : int { classic = 0, precision = 1 };
inline constexpr std::size_t index(Param p) noexcept
{
    return static_cast<std::size_t>(p);
}
struct Dial
{
    Param parameter;
    const char *id;
    const char *name;
    float low, high, step, initial;
    const char *unit;
};
inline constexpr std::array<Dial, 9> dials{{
    {Param::reduction, "reduction", "Reducción", 0, 100, 0.1f, 48, " %"},
    {Param::makeup, "makeup", "Ganancia", -18, 24, 0.1f, 0, " dB"},
    {Param::highpass, "highpass", "Paso alto", 0, 300, 1, 0, " Hz"},
    {Param::emphasis, "emphasis", "Énfasis", 0, 9, 0.1f, 0, " dB"},
    {Param::recovery, "recovery", "Recuperación", 50, 200, 1, 100, " %"},
    {Param::mix, "mix", "Mezcla", 0, 100, 0.1f, 100, " %"},
    {Param::attack, "attack", "Ataque", 1, 40, 0.1f, 8, " ms"},
    {Param::stereoLink, "stereoLink", "Enlace L/R", 0, 100, 1, 100, " %"},
    {Param::range, "range", "Rango", 0, 40, 0.1f, 40, " dB"},
}};
struct Preset
{
    const char *name;
    std::array<float, dials.size()> dial;
    bool limit;
};
inline constexpr std::array<Preset, 4> presets{{
    {"Inicio", {48, 0, 0, 0, 100, 100, 8, 100, 40}, false},
    {"Voz al frente", {61, 4, 85, 2, 85, 100, 8, 100, 40}, false},
    {"Bajo sostenido", {55, 3, 32, 0, 145, 100, 8, 100, 40}, false},
    {"Bus paralelo", {72, 6, 115, 1, 65, 45, 8, 100, 40}, true},
}};
inline juce::String utf(const char *s)
{
    return juce::String::fromUTF8(s);
}
const char *paramID(Param);
juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
} // namespace luma
