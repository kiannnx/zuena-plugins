#pragma once

#include "../PluginProcessor.h"
#include "../State/Presets.h"
#include "../State/Text.h"
#include "BandAccess.h"
#include "BandCard.h"
#include "EqGraph.h"
#include "Knob.h"
#include "Motion.h"
#include "ZuenaLookAndFeel.h"

namespace zeq::ui
{
/** Botón de banda en la barra inferior: clic selecciona, Alt+clic o doble clic hace bypass, clic derecho abre menú. */
class BandChip final : public juce::Button
{
public:
    explicit BandChip (int b) : juce::Button ("Banda " + juce::String (b + 1)), band (b)
    {
        setTooltip (zeq::utf8 ("Banda ") + juce::String (b + 1) + zeq::utf8 (" · Alt+clic o doble clic: bypass · Clic derecho: opciones"));
    }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void()> onToggleBypass;
    std::function<void()> onContextMenu;
    bool bypassed = false;

private:
    int band;
};

/** Panel flotante de consulta rápida con todos los gestos de ratón y atajos de teclado. */
class ShortcutsOverlay final : public juce::Component
{
public:
    ShortcutsOverlay();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClose) onClose(); }
    std::function<void()> onClose;
};

class ZuenaEqEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit ZuenaEqEditor (ZuenaEqProcessor&);
    ~ZuenaEqEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Un fotograma de la UI (sincronizado con VBlankAttachment). Público para herramientas fuera de pantalla. */
    void frame();

    /** Tamaño de la interfaz (0.7 … 1.3). Escala todo el contenido como un bloque, sin tocar la escala del host. */
    void setUiScale (float scale, bool persist = true);
    float getUiScale() const noexcept { return uiScale; }
    /** Componente raíz con todo el contenido (coordenadas lógicas, sin la escala aplicada). */
    juce::Component& getContentRoot() noexcept { return root; }

    static constexpr std::array<float, 6> kUiScales { 0.7f, 0.8f, 0.9f, 1.0f, 1.15f, 1.3f };
    static constexpr int kBaseW = 1360, kBaseH = 780, kMinW = 1320, kMinH = 700, kMaxW = 2400, kMaxH = 1500;
    /** Escala más grande (≤ 100 %) con la que el tamaño inicial entra en `area`. */
    static float fitScale (juce::Rectangle<int> area) noexcept;

private:
    /** Respaldo de EB-15: si el host no envía vblank (ventana minimizada, escritorio remoto), avanza la UI igual. */
    void timerCallback() override;

public:

private:
    void select (int band);
    juce::Rectangle<float> targetCardBounds() const;
    void layoutCard (bool snap);
    void setAnalyzer (AnalyzerMode m);
    void setRange (int db, bool animate);
    void setPhaseVisible (bool show);
    void setAutoListen (bool on);
    void setFrozen (bool freeze);
    void setDarkMode (bool dark, bool persistGlobal = true);
    void setShortcutsVisible (bool show);
    void showChipMenu (int band);
    void showSizeMenu();
    void refreshAbButtons();
    void paintContent (juce::Graphics&);
    void layoutContent();

    /** Contenedor de toda la interfaz: JUCE pide no transformar el editor (lo usa el host para su escala). */
    struct Root final : juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        std::function<void()> onResized;
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
        void resized() override { if (onResized) onResized(); }
    };

    ZuenaEqProcessor& proc;
    ZuenaLookAndFeel lnf;
    Root root; // antes que los hijos: se destruye después de ellos
    float uiScale = 1.0f;
    BandAccess access;
    EqGraph graph;
    BandCard card;
    ShortcutsOverlay shortcutsOverlay;

    // Barra superior
    juce::ComboBox presetBox, qualityBox;
    juce::TextButton slotABtn { "A" }, slotBBtn { "B" }, copyAbBtn { zeq::utf8 ("A→B") };
    std::array<juce::TextButton, 4> analyzerButtons {
        juce::TextButton ("Off"), juce::TextButton ("Pre"),
        juce::TextButton ("Post"), juce::TextButton ("Pre + Post")
    };
    std::array<juce::TextButton, 3> rangeButtons {
        juce::TextButton (zeq::utf8 ("±6")),
        juce::TextButton (zeq::utf8 ("±12")),
        juce::TextButton (zeq::utf8 ("±30"))
    };
    juce::TextButton undo { "Deshacer" }, redo { "Rehacer" };
    juce::TextButton autoGainBtn { "Auto" }, globalBypassBtn { "Bypass" };
    Knob gainScale { "Escala" }, output { "Salida" };

    // Barra inferior
    juce::TextButton addBandBtn { "+ Banda" };
    juce::TextButton autoListenBtn { zeq::utf8 ("Audición") }, freezeBtn { "Congelar" }, phaseBtn { "Fase" }, darkBtn { "Oscuro" }, helpBtn { "?" };
    juce::TextButton sizeBtn { "100 %" };
    juce::Label hint;

    std::unique_ptr<juce::ComboBoxParameterAttachment> qualityAtt;
    std::unique_ptr<juce::ButtonParameterAttachment> autoGainAtt, globalBypassAtt;

    std::vector<std::unique_ptr<BandChip>> chips;
    juce::TooltipWindow tooltips { this, 500 };
    std::unique_ptr<juce::VBlankAttachment> vblank;

    double lastFrameMs = 0.0;
    bool appliedDark = false; // modo que ESTE editor tiene aplicado (el tema es una preferencia global compartida)
    bool cardWasVisible = false;
    bool cardPinnedByUser = false;
    juce::Point<float> pinnedCardPos {};
    juce::Rectangle<float> animCard {};
    float cardAlpha = 1.0f;
    float meterDb = -100.0f;
    juce::Rectangle<float> meterBounds {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZuenaEqEditor)
};
} // namespace zeq::ui
