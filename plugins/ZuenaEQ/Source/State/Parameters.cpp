#include "Parameters.h"
#include "Text.h"

#include <cmath>

namespace zeq::params
{
namespace
{
const char* const kSuffix[] = { "used", "bypass", "type", "freq", "gain", "q", "slope", "place", "solo",
                                "dyn", "thr", "range", "ratio", "atk", "rel" };

/** Rango logarítmico (frecuencia, Q, tiempos). */
juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float start, float end, float n) { return start * std::pow (end / start, n); },
             [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
             [] (float start, float end, float v) { return juce::jlimit (start, end, v); } };
}

juce::String formatFreq (float hz, int)
{
    return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz";
}

float parseFreq (const juce::String& text)
{
    auto t = text.trim();
    const auto up = t.toUpperCase();
    if (up.isNotEmpty() && up[0] >= 'A' && up[0] <= 'G')
    {
        static const int baseSemis[7] = { 9, 11, 0, 2, 4, 5, 7 };
        int semi = baseSemis[up[0] - 'A'];
        int pos = 1;
        if (pos < up.length() && up[pos] == '#') { ++semi; ++pos; }
        else if (pos < up.length() && up[pos] == 'B') { --semi; ++pos; }
        const auto octStr = up.substring (pos).trim();
        if (octStr.isNotEmpty() && octStr.containsOnly ("-0123456789"))
        {
            const int midi = (octStr.getIntValue() + 1) * 12 + semi;
            return juce::jlimit (10.0f, 30000.0f, (float) (440.0 * std::pow (2.0, ((double) midi - 69.0) / 12.0)));
        }
    }
    const auto lo = t.toLowerCase();
    const bool khz = lo.contains ("k");
    const auto clean = lo.retainCharacters ("0123456789.,-+").replaceCharacter (',', '.');
    const float v = clean.getFloatValue() * (khz ? 1000.0f : 1.0f);
    return juce::jlimit (10.0f, 30000.0f, v > 0.0f ? v : 1000.0f);
}
} // namespace

const juce::StringArray& typeNames()
{
    static const juce::StringArray names {
        "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
        "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt", "All Pass"
    };
    return names;
}

const juce::StringArray& slopeNames()
{
    static const juce::StringArray names { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };
    return names;
}

const juce::StringArray& placementNames()
{
    static const juce::StringArray names { zeq::utf8 ("Estéreo"), "Izquierdo", "Derecho", "Mid", "Side" };
    return names;
}

const juce::StringArray& qualityNames()
{
    static const juce::StringArray names { "Latencia cero", "Fase natural" };
    return names;
}

juce::String bandId (int band, BandParam p)
{
    return "b" + juce::String (band + 1) + "_" + kSuffix[(int) p];
}

float defaultQFor (zeq::FilterType type) noexcept
{
    switch (type)
    {
        case zeq::FilterType::LowCut:
        case zeq::FilterType::HighCut:
        case zeq::FilterType::LowShelf:
        case zeq::FilterType::HighShelf:
        case zeq::FilterType::TiltShelf:
        case zeq::FilterType::FlatTilt:
        case zeq::FilterType::AllPass:
            return 0.7071f;
        default:
            return 1.0f;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int b = 0; b < kMaxBands; ++b)
    {
        const String n = "Banda " + String (b + 1) + " ";
        auto group = std::make_unique<AudioProcessorParameterGroup> ("band" + String (b + 1), "Banda " + String (b + 1), " | ");
        auto id = [b] (BandParam p) { return ParameterID { bandId (b, p), 1 }; };
        auto attr = [] (const String& unit, int decimals = 1)
        {
            return AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction ([decimals] (float v, int) { return String (v, decimals); });
        };

        group->addChild (std::make_unique<AudioParameterBool> (id (BandParam::Used), n + "activa", false));
        group->addChild (std::make_unique<AudioParameterBool> (id (BandParam::Bypass), n + "bypass", false));
        group->addChild (std::make_unique<AudioParameterChoice> (id (BandParam::Type), n + "tipo", typeNames(), 0));
        group->addChild (std::make_unique<AudioParameterFloat> (
            id (BandParam::Freq), n + "frecuencia", logRange (10.0f, 30000.0f), 1000.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (formatFreq).withValueFromStringFunction (parseFreq)));
        group->addChild (std::make_unique<AudioParameterFloat> (id (BandParam::Gain), n + "ganancia", NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f, attr ("dB")));
        group->addChild (std::make_unique<AudioParameterFloat> (id (BandParam::Q), n + "Q", logRange (0.025f, 40.0f), 1.0f, attr ("", 2)));
        group->addChild (std::make_unique<AudioParameterChoice> (id (BandParam::Slope), n + "pendiente", slopeNames(), 1));
        group->addChild (std::make_unique<AudioParameterChoice> (id (BandParam::Placement), n + zeq::utf8 ("estéreo"), placementNames(), 0));
        group->addChild (std::make_unique<AudioParameterBool> (id (BandParam::Solo), n + "solo", false));
        group->addChild (std::make_unique<AudioParameterBool> (id (BandParam::DynOn), n + zeq::utf8 ("dinámica"), false));
        group->addChild (std::make_unique<AudioParameterFloat> (id (BandParam::Threshold), n + "umbral", NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -30.0f, attr ("dB")));
        group->addChild (std::make_unique<AudioParameterFloat> (id (BandParam::Range), n + "rango", NormalisableRange<float> (-30.0f, 30.0f, 0.1f), -6.0f, attr ("dB")));
        group->addChild (std::make_unique<AudioParameterFloat> (id (BandParam::Ratio), n + "ratio", logRange (1.0f, 20.0f), 4.0f, attr (":1")));
        group->addChild (std::make_unique<AudioParameterFloat> (id (BandParam::Attack), n + "ataque", logRange (0.1f, 200.0f), 10.0f, attr ("ms")));
        group->addChild (std::make_unique<AudioParameterFloat> (id (BandParam::Release), n + zeq::utf8 ("relajación"), logRange (5.0f, 2000.0f), 120.0f, attr ("ms")));
        layout.add (std::move (group));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { outputGainId, 1 }, "Salida",
                                                       NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { gainScaleId, 1 }, "Escala ganancia",
                                                       NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")
                                                           .withStringFromValueFunction ([] (float v, int) { return String ((int) std::lround (v)); })));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { qualityModeId, 1 }, "Modo de fase", qualityNames(), 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { autoGainId, 1 }, "Auto ganancia", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { globalBypassId, 1 }, "Bypass global", false));
    return layout;
}

std::array<BandValues, kMaxBands> bindBandValues (juce::AudioProcessorValueTreeState& state)
{
    std::array<BandValues, kMaxBands> out;
    for (int b = 0; b < kMaxBands; ++b)
        for (int p = 0; p < (int) BandParam::Count; ++p)
        {
            auto* v = state.getRawParameterValue (bandId (b, (BandParam) p));
            jassert (v != nullptr);
            out[(size_t) b].v[(size_t) p] = v;
        }
    return out;
}

zeq::BandParams BandValues::read() const noexcept
{
    auto get = [this] (BandParam p) { return v[(size_t) p]->load (std::memory_order_relaxed); };
    zeq::BandParams b;
    b.used = get (BandParam::Used) > 0.5f;
    b.bypass = get (BandParam::Bypass) > 0.5f;
    b.type = (zeq::FilterType) juce::jlimit (0, (int) zeq::FilterType::Count - 1, (int) get (BandParam::Type));
    b.freqHz = get (BandParam::Freq);
    b.gainDb = get (BandParam::Gain);
    b.q = get (BandParam::Q);
    b.slopeIndex = (int) get (BandParam::Slope);
    b.placement = (zeq::Placement) juce::jlimit (0, (int) zeq::Placement::Count - 1, (int) get (BandParam::Placement));
    b.solo = get (BandParam::Solo) > 0.5f;
    b.dynOn = get (BandParam::DynOn) > 0.5f;
    b.thresholdDb = get (BandParam::Threshold);
    b.rangeDb = get (BandParam::Range);
    b.ratio = get (BandParam::Ratio);
    b.attackMs = get (BandParam::Attack);
    b.releaseMs = get (BandParam::Release);
    return b;
}
} // namespace zeq::params
