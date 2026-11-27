#pragma once
// Motor del ecualizador dinámico ZUENA EQ.
// - Topología TPT State Variable Filter (integradores trapezoidales en doble precisión, A. Simper).
// - Modo dual: Latencia Cero (1x) y Fase Natural (sobremuestreo polifásico FIR 2x de 63 coeficientes).
// - Compensación perceptual Auto-Gain y medidor de pico de salida atómico.
// Todo lo que se llama desde el hilo de audio es noexcept, sin reservas dinámicas, bloqueos ni E/S.

#include "FilterDesign.h"
#include "Oversampler2x.h"
#include "SpscRing.h"
#include "ZeqTypes.h"

#include <array>
#include <atomic>

namespace zeq
{
class EqEngine
{
public:
    EqEngine();

    /** Fuera del hilo de audio (prepareToPlay). */
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;

    /** Hilo de audio, antes de process(). */
    void setBand (int index, const BandParams& params) noexcept;
    void setOutputGainDb (double gainDb) noexcept;
    void setGainScale (double scale) noexcept;
    void setGlobalBypass (bool bypassed) noexcept;
    void setQualityMode (QualityMode mode) noexcept;
    void setAutoGain (bool enabled) noexcept;

    /** Hilo de audio. Procesa en sitio. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    /** Cualquier hilo: reducción/realce dinámico actual de la banda (para la UI). */
    float dynamicGainDb (int band) const noexcept { return dynGainDb[(size_t) band].load (std::memory_order_relaxed); }

    /** Cualquier hilo: pico de salida reciente en dBFS (para el medidor de la barra superior). */
    float outputPeakDbfs() const noexcept { return outPeakDb.load (std::memory_order_relaxed); }

    /** Latencia actual en muestras a la frecuencia base del proyecto. */
    int latencySamples() const noexcept;

    /** Frecuencia de muestreo efectiva del banco de filtros (2x en Fase Natural si base < 88.2 kHz). */
    double effectiveSampleRate() const noexcept;
    double getSampleRate() const noexcept { return sampleRate; }

    /** Espectros: entrada (pre) y salida (post), mono. Consumidor: la UI. */
    SpscRing& preTap() noexcept { return pre; }
    SpscRing& postTap() noexcept { return post; }

private:
    static constexpr int kMaxWork = kSubBlock * 2;

    struct BandRuntime
    {
        BandParams target;
        bool configured = false;
        FilterType type = FilterType::Bell;
        int slopeIndex = 1;
        Placement placement = Placement::Stereo;

        double freqLog = 0.0, gainDb = 0.0, qLog = 0.0;  // valores suavizados
        double designedFreqLog = -1.0, designedGain = 1.0e9, designedQLog = -1.0;
        SvfDesign current, next;
        bool interpolate = false;
        std::array<std::array<std::array<double, 2>, kMaxSections>, kMaxChannels> state {};

        double mix = 0.0, mixTarget = 0.0;

        SvfSection detector;
        double detectorFreq = -1.0, detectorQ = -1.0;
        std::array<std::array<double, 2>, kMaxChannels> detectorState {};
        double envelope = 0.0;
        double rmsEnv = 0.0;
        double dynamicDb = 0.0;
    };

    void updateRates() noexcept;
    void updateAutoGainIfNeeded() noexcept;
    void processSubBlock (int numChannels, int len) noexcept;
    void processBand (int index, BandRuntime& r, int numChannels, int len) noexcept;
    void applyStructure (BandRuntime& r) noexcept;
    double runDetector (BandRuntime& r, int numChannels, int len) noexcept;
    void filterChannel (BandRuntime& r, int stateSlot, double* x, int len, double mixStart, double mixTarget) noexcept;

    double sampleRate = 48000.0;
    double dspRate = 48000.0;
    double smoothAlpha = 0.0;   // por sub-bloque
    double mixStep = 0.0;       // por muestra a dspRate (fundido de 5 ms)
    double baseMixStep = 0.0;   // por muestra a sampleRate

    QualityMode targetQuality = QualityMode::ZeroLatency;
    bool activeOversampled = false;
    bool firstBlockAfterPrepare = true;
    double modeFade = 1.0;
    Oversampler2x oversampler;

    bool autoGainEnabled = false;
    double autoGainDbCache = 0.0;
    double lastAutoGainSignature = 1.0e30;

    std::array<BandRuntime, kMaxBands> bands {};
    std::array<BandParams, kMaxBands> autoGainSnap {};
    std::array<std::atomic<float>, kMaxBands> dynGainDb {};
    std::atomic<float> outPeakDb { -100.0f };
    double peakHoldLin = 0.0;

    // Buffers de un sub-bloque (doble precisión, hasta 2x = 32 muestras) — sin reservas en process().
    std::array<std::array<double, kMaxWork>, kMaxChannels> work {}, input {};
    std::array<std::array<double, kSubBlock>, kMaxChannels> baseBuf {};
    std::array<float, kSubBlock> tapScratch {};

    double userOutGainDb = 0.0;
    double gainScale = 1.0;
    bool globalBypass = false;
    double wetMix = 1.0;
    double gainMix = 1.0;       // 0 = bypass global (sin ganancia de salida ni Auto-Gain), 1 = activo; a velocidad base
    double outGain = 1.0, outGainTarget = 1.0;

    // Solo
    int soloBand = -1;
    double soloMix = 0.0, soloFreqLog = 0.0, soloQLog = 0.0;
    SvfSection soloFilter;
    std::array<std::array<double, 2>, kMaxChannels> soloState {};

    SpscRing pre, post;
};
} // namespace zeq
