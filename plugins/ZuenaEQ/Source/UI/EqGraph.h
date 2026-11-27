#pragma once

#include "../Core/Spectrum.h"
#include "../PluginProcessor.h"
#include "BandAccess.h"
#include "Motion.h"
#include "Theme.h"

namespace zeq::ui
{
enum class AnalyzerMode : int { Off = 0, Pre = 1, Post = 2, Both = 3 };

class EqGraph final : public juce::Component
{
public:
    EqGraph (ZuenaEqProcessor& processor, BandAccess& access);

    std::function<void (int)> onSelectionChanged;
    std::function<void()> onToggleFreezeRequested;
    std::function<void()> onToggleDarkRequested;

    int getSelected() const noexcept { return selected; }
    void setSelected (int band);
    void selectAllBands();
    bool isBandSelected (int band) const noexcept;
    int selectedCount() const noexcept;

    /** Acciones de teclado sobre la selección actual (1 o varias bandas). */
    void nudgeSelection (float freqOctaves, float gainDeltaDb, float qFactor);
    void toggleBypassSelection();
    void toggleDynamicSelection();
    void deleteSelection();

    void setAnalyzerMode (AnalyzerMode m) { analyzerMode = m; repaint(); }
    void setRangeDb (float r, bool animate = true);
    void setShowPhase (bool show) { showPhase = show; repaint(); }
    bool getShowPhase() const noexcept { return showPhase; }
    void setFrozen (bool freeze);
    bool isFrozen() const noexcept { return spectrumFrozen; }
    void setAutoListen (bool enabled) { autoListen = enabled; }
    bool getAutoListen() const noexcept { return autoListen; }

    /** Llamado por el editor en cada fotograma: actualiza espectros, resortes y redibuja solo si algo cambió. */
    void tick (float dt);
    /** Cuántas veces tick() pidió redibujar (para medir el costo en reposo). */
    std::uint32_t getTickRepaints() const noexcept { return tickRepaints; }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> plotArea() const;
    float xForFreq (double hz) const;
    double freqForX (float x) const;
    float yForDb (double db) const;
    double dbForY (float y) const;
    float yForPhase (double deg) const;
    float yForSpectrum (float dbfs) const;
    double effectiveSampleRate() const;
    double baseSampleRate() const;
    double currentGainScale() const;

    juce::Point<float> nodePosition (int band, const BandParams& p) const;
    juce::Point<float> dynRangePosition (const BandParams& p) const;
    int nodeAt (juce::Point<float> pos) const;
    int dynHandleAt (juce::Point<float> pos) const;
    void showBandMenu (int band);
    void showEmptySpaceMenu (juce::Point<float> pos);
    void startDraggingSelection (int primaryBand, juce::Point<float> pos, const juce::ModifierKeys& mods);

    void drawGrid (juce::Graphics&, juce::Rectangle<float> area);
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float> area, const Spectrum& s, bool post);
    void drawCurves (juce::Graphics&, juce::Rectangle<float> area);
    void drawGhostPreview (juce::Graphics&, juce::Rectangle<float> area);
    void drawLasso (juce::Graphics&);
    void drawNodes (juce::Graphics&);
    void drawCursorReadout (juce::Graphics&, juce::Rectangle<float> area);

    ZuenaEqProcessor& proc;
    BandAccess& access;
    Spectrum preSpectrum, postSpectrum;

    AnalyzerMode analyzerMode = AnalyzerMode::Both;
    bool showPhase = false;
    bool spectrumFrozen = false;
    bool autoListen = false;
    float targetRangeDb = 12.0f;
    float displayedRangeDb = 12.0f;
    int selected = -1;
    std::array<bool, kMaxBands> multiSelected {};
    int hovered = -1;
    int hoveredRangeBand = -1;

    bool firstTick = true;
    bool mouseInside = false;
    juce::Point<float> cursorPos {};
    float cursorAlpha = 0.0f;
    float ghostAlpha = 0.0f;

    bool lassoActive = false;
    juce::Point<float> lassoStart {}, lassoEnd {};

    struct Drag
    {
        int band = -1;
        bool rangeHandle = false;
        bool qDrag = false;         // Ctrl + Alt + arrastre vertical ajusta Q directamente
        bool axisLockMode = false;  // Alt + arrastre bloquea a solo frecuencia o solo ganancia
        int lockedAxis = 0;         // 0 = sin decidir, 1 = horizontal (frecuencia), 2 = vertical (ganancia)
        bool tempSoloActive = false;
        bool prevSoloState = false;
        juce::Point<float> start {};
        std::array<double, kMaxBands> startFreq {};
        std::array<double, kMaxBands> startGain {};
        std::array<double, kMaxBands> startQ {};
        double startRange = -6.0;
        bool moved = false;
    } drag;

    /** Respuesta de una banda en las frecuencias de las columnas del gráfico; se recalcula solo si cambia. */
    struct CurveCache
    {
        FilterType type = FilterType::Count;
        double freq = 0.0, gain = 0.0, q = 0.0, sr = 0.0;
        int slope = 0;
        std::vector<double> db;
        std::vector<std::complex<double>> h;
    };
    const CurveCache& cachedCurve (CurveCache& c, FilterType type, double freq, double gain, double q, int slope, double sr);
    std::array<CurveCache, kMaxBands> staticCurves {}, dynCurves {};
    std::vector<double> curveFreqs;
    float curveAreaX = -1.0f, curveAreaW = -1.0f;

    // Redibujo solo cuando algo visible cambió (antes: cada refresco del monitor, aunque nada se moviera).
    std::array<BandParams, kMaxBands> lastTickParams {};
    juce::Point<float> lastCursor {};
    bool lastMouseInside = false;
    double lastGainScale = -1.0, lastSampleRate = -1.0;
    bool lastGlobalBypass = false;
    double lastRepaintMs = 0.0;
    std::uint32_t tickRepaints = 0;

    std::array<BandParams, kMaxBands> snapshot {};
    std::array<bool, kMaxBands> wasUsed {};
    std::array<float, kMaxBands> dynSnapshot {};
    std::array<float, kMaxBands> dynVisual {};
    std::array<motion::Spring, kMaxBands> nodeAppear {};
    std::array<float, kMaxBands> nodeEmphasis {};
};
} // namespace zeq::ui
