#pragma once
// Presets de fábrica y comparación A/B sobre APVTS + UndoManager (anti EB-03).

#include "Parameters.h"
#include "Text.h"

#include <vector>

namespace zeq::presets
{
struct BandDef
{
    FilterType type = FilterType::Bell;
    float freqHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 1.0f;
    int slope = 1;
    Placement placement = Placement::Stereo;
    bool dynOn = false;
    float thresholdDb = -28.0f;
    float rangeDb = -4.5f;
};

struct FactoryPreset
{
    juce::String name;
    QualityMode quality = QualityMode::ZeroLatency;
    bool autoGain = false;
    std::vector<BandDef> bands;
};

inline const std::vector<FactoryPreset>& factoryList()
{
    static const std::vector<FactoryPreset> list {
        { "Plano (Reset)", QualityMode::ZeroLatency, false, {} },
        { "Voz presente", QualityMode::NaturalPhase, false, {
            { FilterType::LowCut, 85.0f, 0.0f, 0.7071f, 3, Placement::Stereo, false, -30.0f, -6.0f },
            { FilterType::Bell, 280.0f, -2.8f, 1.6f, 1, Placement::Stereo, true, -26.0f, -3.5f },
            { FilterType::Bell, 3400.0f, 2.4f, 1.1f, 1, Placement::Stereo, false, -30.0f, -6.0f },
            { FilterType::HighShelf, 10500.0f, 2.2f, 0.7071f, 1, Placement::Stereo, false, -30.0f, -6.0f },
        } },
        { "Limpieza subgraves", QualityMode::NaturalPhase, false, {
            { FilterType::LowCut, 32.0f, 0.0f, 0.75f, 4, Placement::Mid, false, -30.0f, -6.0f },
            { FilterType::LowCut, 110.0f, 0.0f, 0.7071f, 3, Placement::Side, false, -30.0f, -6.0f },
            { FilterType::Bell, 62.0f, 1.8f, 1.3f, 1, Placement::Mid, false, -30.0f, -6.0f },
        } },
        { "Master aire natural", QualityMode::NaturalPhase, true, {
            { FilterType::LowCut, 22.0f, 0.0f, 0.7071f, 2, Placement::Stereo, false, -30.0f, -6.0f },
            { FilterType::FlatTilt, 650.0f, 1.6f, 0.7071f, 1, Placement::Stereo, false, -30.0f, -6.0f },
            { FilterType::HighShelf, 12000.0f, 1.8f, 0.65f, 1, Placement::Side, false, -30.0f, -6.0f },
        } },
        { zeq::utf8 ("Control dinámico 300 Hz"), QualityMode::ZeroLatency, false, {
            { FilterType::Bell, 310.0f, -1.5f, 1.8f, 1, Placement::Stereo, true, -24.0f, -6.5f },
            { FilterType::Bell, 4200.0f, 0.0f, 2.4f, 1, Placement::Stereo, true, -22.0f, -4.5f },
        } },
        { zeq::utf8 ("Amplitud Mid / Side"), QualityMode::NaturalPhase, false, {
            { FilterType::Bell, 240.0f, -2.2f, 1.2f, 1, Placement::Side, false, -30.0f, -6.0f },
            { FilterType::Bell, 1450.0f, 1.5f, 1.1f, 1, Placement::Mid, false, -30.0f, -6.0f },
            { FilterType::HighShelf, 7500.0f, 2.8f, 0.7071f, 1, Placement::Side, false, -30.0f, -6.0f },
        } },
    };
    return list;
}

inline void setPlain (juce::AudioProcessorValueTreeState& state, const juce::String& id, float plain)
{
    if (auto* p = state.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

inline void applyFactoryPreset (juce::AudioProcessorValueTreeState& state, juce::UndoManager& undo, int index)
{
    const auto& list = factoryList();
    if (index < 0 || index >= (int) list.size())
        return;
    const auto& pr = list[(size_t) index];
    undo.beginNewTransaction ("Preset: " + pr.name);

    using P = params::BandParam;
    for (int b = 0; b < kMaxBands; ++b)
    {
        if (b < (int) pr.bands.size())
        {
            const auto& d = pr.bands[(size_t) b];
            setPlain (state, params::bandId (b, P::Bypass), 0.0f);
            setPlain (state, params::bandId (b, P::Solo), 0.0f);
            setPlain (state, params::bandId (b, P::Type), (float) d.type);
            setPlain (state, params::bandId (b, P::Freq), d.freqHz);
            setPlain (state, params::bandId (b, P::Gain), d.gainDb);
            setPlain (state, params::bandId (b, P::Q), d.q);
            setPlain (state, params::bandId (b, P::Slope), (float) d.slope);
            setPlain (state, params::bandId (b, P::Placement), (float) d.placement);
            setPlain (state, params::bandId (b, P::DynOn), d.dynOn ? 1.0f : 0.0f);
            setPlain (state, params::bandId (b, P::Threshold), d.thresholdDb);
            setPlain (state, params::bandId (b, P::Range), d.rangeDb);
            setPlain (state, params::bandId (b, P::Ratio), 4.0f);
            setPlain (state, params::bandId (b, P::Attack), 10.0f);
            setPlain (state, params::bandId (b, P::Release), 120.0f);
            setPlain (state, params::bandId (b, P::Used), 1.0f);
        }
        else
        {
            setPlain (state, params::bandId (b, P::Solo), 0.0f);
            setPlain (state, params::bandId (b, P::DynOn), 0.0f);
            setPlain (state, params::bandId (b, P::Used), 0.0f);
        }
    }
    setPlain (state, params::outputGainId, 0.0f);
    setPlain (state, params::gainScaleId, 100.0f);
    setPlain (state, params::qualityModeId, (float) pr.quality);
    setPlain (state, params::autoGainId, pr.autoGain ? 1.0f : 0.0f);
    setPlain (state, params::globalBypassId, 0.0f);
}

// ------------------------------------------------------------------------- Comparación A / B

inline juce::String serializeParams (juce::AudioProcessor& proc)
{
    juce::StringArray lines;
    for (auto* param : proc.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            lines.add (r->getParameterID() + "=" + juce::String (r->getValue(), 7));
    return lines.joinIntoString ("\n");
}

inline void deserializeParams (juce::AudioProcessorValueTreeState& state, const juce::String& snapshot)
{
    for (const auto& line : juce::StringArray::fromLines (snapshot))
    {
        const auto id = line.upToFirstOccurrenceOf ("=", false, false);
        const auto val = line.fromFirstOccurrenceOf ("=", false, false);
        if (id.isEmpty())
            continue;
        if (auto* p = state.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, val.getFloatValue()));
            p->endChangeGesture();
        }
    }
}

inline int activeSlot (const juce::AudioProcessorValueTreeState& state)
{
    return (int) state.state.getProperty ("ab_slot", 0) == 1 ? 1 : 0;
}

inline void switchSlot (juce::AudioProcessor& proc, juce::AudioProcessorValueTreeState& state, juce::UndoManager& undo, int targetSlot)
{
    targetSlot = targetSlot == 1 ? 1 : 0;
    const int cur = activeSlot (state);
    if (targetSlot == cur)
        return;

    const juce::Identifier curProp (cur == 0 ? "ab_state_a" : "ab_state_b");
    const juce::Identifier dstProp (targetSlot == 0 ? "ab_state_a" : "ab_state_b");
    const auto nowSnap = serializeParams (proc);
    state.state.setProperty (curProp, nowSnap, nullptr);
    state.state.setProperty ("ab_slot", targetSlot, nullptr);

    const auto dstSnap = state.state.getProperty (dstProp).toString();
    undo.beginNewTransaction (targetSlot == 0 ? "Cambiar a A" : "Cambiar a B");
    if (dstSnap.isNotEmpty())
        deserializeParams (state, dstSnap);
    else
        applyFactoryPreset (state, undo, 0);
}

inline void copyCurrentToOther (juce::AudioProcessor& proc, juce::AudioProcessorValueTreeState& state)
{
    const int cur = activeSlot (state);
    const juce::Identifier otherProp (cur == 0 ? "ab_state_b" : "ab_state_a");
    state.state.setProperty (otherProp, serializeParams (proc), nullptr);
}
} // namespace zeq::presets
