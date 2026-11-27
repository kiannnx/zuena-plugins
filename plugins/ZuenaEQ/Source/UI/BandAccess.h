#pragma once
// Lectura y escritura ergonómica de los parámetros de banda desde la UI, siempre con gestos de cambio
// (el DAW registra la automatización y el UndoManager agrupa cada acción).

#include "../State/Parameters.h"

#include <cmath>
#include <vector>

namespace zeq::ui
{
using params::BandParam;

class BandAccess
{
public:
    explicit BandAccess (juce::AudioProcessorValueTreeState& s) : state (s) {}

    juce::RangedAudioParameter& param (int band, BandParam p) const
    {
        return *state.getParameter (params::bandId (band, p));
    }

    float get (int band, BandParam p) const
    {
        return state.getRawParameterValue (params::bandId (band, p))->load();
    }

    /** Cambio puntual completo (gesto de inicio y fin). */
    void set (int band, BandParam p, float value) const
    {
        auto& pr = param (band, p);
        pr.beginChangeGesture();
        pr.setValueNotifyingHost (pr.convertTo0to1 (value));
        pr.endChangeGesture();
    }

    void begin (int band, BandParam p) const { param (band, p).beginChangeGesture(); }
    void update (int band, BandParam p, float value) const { auto& pr = param (band, p); pr.setValueNotifyingHost (pr.convertTo0to1 (value)); }
    void end (int band, BandParam p) const { param (band, p).endChangeGesture(); }

    BandParams read (int band) const
    {
        BandParams b;
        b.used = get (band, BandParam::Used) > 0.5f;
        b.bypass = get (band, BandParam::Bypass) > 0.5f;
        b.type = (FilterType) juce::jlimit (0, (int) FilterType::Count - 1, (int) get (band, BandParam::Type));
        b.freqHz = get (band, BandParam::Freq);
        b.gainDb = get (band, BandParam::Gain);
        b.q = get (band, BandParam::Q);
        b.slopeIndex = (int) get (band, BandParam::Slope);
        b.placement = (Placement) juce::jlimit (0, (int) Placement::Count - 1, (int) get (band, BandParam::Placement));
        b.solo = get (band, BandParam::Solo) > 0.5f;
        b.dynOn = get (band, BandParam::DynOn) > 0.5f;
        b.thresholdDb = get (band, BandParam::Threshold);
        b.rangeDb = get (band, BandParam::Range);
        b.ratio = get (band, BandParam::Ratio);
        b.attackMs = get (band, BandParam::Attack);
        b.releaseMs = get (band, BandParam::Release);
        return b;
    }

    /** Sugiere automáticamente el tipo de filtro más natural según la zona del gráfico donde apunta el ratón. */
    static FilterType smartTypeFor (double freqHz, double gainDb) noexcept
    {
        if (freqHz < 35.0 && std::abs (gainDb) < 3.5)
            return FilterType::LowCut;
        if (freqHz < 85.0 && std::abs (gainDb) >= 3.5)
            return FilterType::LowShelf;
        if (freqHz > 17500.0 && std::abs (gainDb) < 3.5)
            return FilterType::HighCut;
        if (freqHz > 10000.0 && std::abs (gainDb) >= 3.5)
            return FilterType::HighShelf;
        return FilterType::Bell;
    }

    /** Crea una banda en el primer hueco libre. Devuelve su índice o -1 si las 24 están en uso. */
    int create (FilterType type, float freqHz, float gainDb) const
    {
        for (int b = 0; b < kMaxBands; ++b)
        {
            if (get (b, BandParam::Used) > 0.5f)
                continue;
            set (b, BandParam::Type, (float) type);
            set (b, BandParam::Freq, freqHz);
            set (b, BandParam::Gain, hasGain (type) ? gainDb : 0.0f);
            set (b, BandParam::Q, params::defaultQFor (type));
            set (b, BandParam::Slope, type == FilterType::LowCut || type == FilterType::HighCut ? 3.0f : 1.0f); // 24 dB/oct por defecto en cortes
            set (b, BandParam::Placement, 0.0f);
            set (b, BandParam::Bypass, 0.0f);
            set (b, BandParam::Solo, 0.0f);
            set (b, BandParam::DynOn, 0.0f);
            set (b, BandParam::Used, 1.0f);
            return b;
        }
        return -1;
    }

    /** Crea una banda inteligente eligiendo una frecuencia libre cómoda (útil para el botón '+ Banda'). */
    int createSmartDefault() const
    {
        static const float kSuggestedFreqs[] = { 100.0f, 350.0f, 1000.0f, 3200.0f, 8500.0f, 60.0f, 650.0f, 2000.0f, 5500.0f, 12000.0f };
        float chosen = 1000.0f;
        for (float cand : kSuggestedFreqs)
        {
            bool taken = false;
            for (int b = 0; b < kMaxBands; ++b)
                if (get (b, BandParam::Used) > 0.5f && std::abs ( std::log2 (get (b, BandParam::Freq) / cand)) < 0.35f)
                    taken = true;
            if (! taken)
            {
                chosen = cand;
                break;
            }
        }
        return create (FilterType::Bell, chosen, 0.0f);
    }

    void remove (int band) const
    {
        if (band < 0 || band >= kMaxBands)
            return;
        set (band, BandParam::Solo, 0.0f);
        set (band, BandParam::DynOn, 0.0f);
        set (band, BandParam::Used, 0.0f);
    }

    /** Solo exclusivo: al activar una banda se desactiva el solo de las demás. */
    void setSolo (int band, bool on) const
    {
        for (int b = 0; b < kMaxBands; ++b)
            if (b != band && get (b, BandParam::Solo) > 0.5f)
                set (b, BandParam::Solo, 0.0f);
        if (band >= 0 && band < kMaxBands)
            set (band, BandParam::Solo, on ? 1.0f : 0.0f);
    }

    /** Lista ordenada (por frecuencia ascendente) de los índices de bandas en uso: ideal para recorrer con Tab / flechas ‹ ›. */
    std::vector<int> usedBandsByFreq() const
    {
        std::vector<int> list;
        for (int b = 0; b < kMaxBands; ++b)
            if (get (b, BandParam::Used) > 0.5f)
                list.push_back (b);
        std::sort (list.begin(), list.end(), [this] (int a, int b) { return get (a, BandParam::Freq) < get (b, BandParam::Freq); });
        return list;
    }

    /** Banda anterior o siguiente en orden de frecuencia (-1 o +1). */
    int stepBand (int current, int delta) const
    {
        const auto list = usedBandsByFreq();
        if (list.empty())
            return -1;
        auto it = std::find (list.begin(), list.end(), current);
        if (it == list.end())
            return delta >= 0 ? list.front() : list.back();
        const int idx = (int) std::distance (list.begin(), it);
        const int next = (idx + delta + (int) list.size()) % (int) list.size();
        return list[(size_t) next];
    }

    /** Invierte la ganancia estática y el rango dinámico de todas las bandas activas. */
    void invertAllGains() const
    {
        for (int b = 0; b < kMaxBands; ++b)
        {
            if (get (b, BandParam::Used) < 0.5f)
                continue;
            const auto t = (FilterType) (int) get (b, BandParam::Type);
            if (hasGain (t))
            {
                set (b, BandParam::Gain, -get (b, BandParam::Gain));
                if (get (b, BandParam::DynOn) > 0.5f)
                    set (b, BandParam::Range, -get (b, BandParam::Range));
            }
        }
    }

    void clearAllBands() const
    {
        for (int b = 0; b < kMaxBands; ++b)
            if (get (b, BandParam::Used) > 0.5f)
                remove (b);
    }

    juce::AudioProcessorValueTreeState& state;
};
} // namespace zeq::ui
