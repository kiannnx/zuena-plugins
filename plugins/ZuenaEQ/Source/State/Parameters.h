#pragma once
// Parámetros APVTS de ZUENA EQ: 24 bandas x 15 parámetros + ganancia de salida, modo de fase y auto-ganancia (363 total).

#include "../Core/ZeqTypes.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace zeq::params
{
enum class BandParam : int
{
    Used, Bypass, Type, Freq, Gain, Q, Slope, Placement, Solo, DynOn, Threshold, Range, Ratio, Attack, Release, Count
};

juce::String bandId (int band, BandParam p);
inline const juce::String outputGainId { "out_gain" };
inline const juce::String gainScaleId { "gain_scale" };
inline const juce::String qualityModeId { "proc_mode" };
inline const juce::String autoGainId { "auto_gain" };
inline const juce::String globalBypassId { "global_bypass" };

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

const juce::StringArray& typeNames();
const juce::StringArray& slopeNames();
const juce::StringArray& placementNames();
const juce::StringArray& qualityNames();

/** Punteros a los valores atómicos de una banda (lectura sin bloqueos en el hilo de audio). */
struct BandValues
{
    std::array<std::atomic<float>*, (size_t) BandParam::Count> v {};
    zeq::BandParams read() const noexcept;
};

std::array<BandValues, kMaxBands> bindBandValues (juce::AudioProcessorValueTreeState& state);

/** Valores por defecto de una banda nueva según su tipo. */
float defaultQFor (zeq::FilterType type) noexcept;
} // namespace zeq::params
