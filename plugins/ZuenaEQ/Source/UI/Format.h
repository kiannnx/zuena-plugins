#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <optional>

namespace zeq::ui::format
{
inline juce::String freq (double hz)
{
    if (hz >= 1000.0)
        return juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz";
    return juce::String (hz, hz < 100.0 ? 1 : 0) + " Hz";
}

inline juce::String gain (double db)
{
    return (db > 0.005 ? "+" : "") + juce::String (db, 1) + " dB";
}

/** Nota musical más cercana con desviación en cents (A4 = 440 Hz). */
inline juce::String note (double hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int n = (int) std::lround (midi);
    const int cents = (int) std::lround ((midi - n) * 100.0);
    const int idx = ((n % 12) + 12) % 12;
    const int octave = n / 12 - 1;
    juce::String s = juce::String (names[idx]) + juce::String (octave);
    if (cents != 0)
        s << (cents > 0 ? " +" : " ") << cents << "c";
    return s;
}

/** Interpreta frecuencias ("440", "2.5k", "2,5 kHz") o notas musicales ("A4", "F#2", "Bb3", "Do4", "La4"). */
inline std::optional<float> parseFrequencyOrNote (const juce::String& raw)
{
    auto t = raw.trim();
    if (t.isEmpty())
        return std::nullopt;

    const auto up = t.toUpperCase();
    // Intento 1: nota musical anglosajona (C, C#, Db, D, ..., B) + octava (-1 a 9).
    if (up[0] >= 'A' && up[0] <= 'G')
    {
        static const int baseSemis[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A, B, C, D, E, F, G
        int semi = baseSemis[up[0] - 'A'];
        int pos = 1;
        if (pos < up.length() && (up[pos] == '#' || up[pos] == 'S'))
        {
            ++semi;
            ++pos;
        }
        else if (pos < up.length() && up[pos] == 'B')
        {
            --semi;
            ++pos;
        }
        const auto octStr = up.substring (pos).trim();
        if (octStr.isNotEmpty() && octStr.containsOnly ("-0123456789"))
        {
            const int octave = octStr.getIntValue();
            const int midi = (octave + 1) * 12 + semi;
            const double hz = 440.0 * std::pow (2.0, ((double) midi - 69.0) / 12.0);
            return juce::jlimit (10.0f, 30000.0f, (float) hz);
        }
    }

    // Intento 2: número con sufijo opcional k / kHz / Hz.
    const auto lo = t.toLowerCase();
    const bool khz = lo.contains ("k");
    const auto digits = lo.retainCharacters ("0123456789.,-+").replaceCharacter (',', '.');
    if (digits.isEmpty())
        return std::nullopt;
    const float v = digits.getFloatValue() * (khz ? 1000.0f : 1.0f);
    if (v <= 0.0f)
        return std::nullopt;
    return juce::jlimit (10.0f, 30000.0f, v);
}
} // namespace zeq::ui::format
