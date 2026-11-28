#pragma once
// Parámetros automatizables de ZUENA Link. El orden y los ID no cambian entre versiones (proyectos guardados).
// Lo que no se automatiza (dispositivo, modo, carpeta y formato de grabación, tema) vive en el estado del plugin.
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cstddef>

namespace zlink
{
enum class Param : std::size_t
{
    obs,     // Enviar a OBS
    channel, // Link 1…8
    device,  // Enviar al dispositivo
    send,    // Nivel de envío (dB)
    mono,    // Envío en mono
    count
};
inline constexpr std::size_t index(Param p) noexcept
{
    return static_cast<std::size_t>(p);
}
inline constexpr std::array<const char *, index(Param::count)> kParamIds{"obs", "channel", "device", "send", "mono"};
inline constexpr float kSendMinDb = -60.0f, kSendMaxDb = 12.0f;

inline juce::String utf(const char *s)
{
    return juce::String::fromUTF8(s);
}
juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
} // namespace zlink
