#pragma once
// Ficha flotante ergonómica de la banda seleccionada:
// - Arrastrable libremente por su cabecera para colocarla donde más cómodo quede.
// - Navegación rápida ‹ / › entre bandas activas, botón × para cerrar sin borrar,
//   botón ± para invertir ganancia en un clic (técnica boost-buscar-invertir) y auto-escucha en Frecuencia.

#include "BandAccess.h"
#include "../State/Text.h"
#include "Knob.h"

namespace zeq::ui
{
class IconMiniButton final : public juce::Button
{
public:
    enum class Glyph { Prev, Next, Close };
    IconMiniButton (const juce::String& name, Glyph g) : juce::Button (name), glyph (g) { setTooltip (name); }
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    Glyph glyph;
};

class BandCard final : public juce::Component
{
public:
    BandCard (BandAccess& access, juce::UndoManager& undo);

    /** Vincula los controles a la banda (o -1 para ocultar la ficha). */
    void showBand (int band);
    int getBand() const noexcept { return band; }

    /** Llamado por el editor en cada fotograma: adapta controles y actualiza nota/reducción en vivo. */
    void refresh (float liveDynDb = 0.0f);

    /** Alto que necesita la ficha para el estado actual de la banda. */
    int preferredHeight() const;

    /** Actualiza etiquetas y perillas tras cambiar entre Modo Claro y Modo Oscuro. */
    void refreshTheme();

    void setAutoListenProvider (std::function<bool()> fn) { isAutoListenOn = std::move (fn); }

    std::function<void()> onDeleted;
    std::function<void()> onClosed;
    std::function<void (int delta)> onStepBand;
    std::function<void (juce::Point<int> newTopLeft)> onUserMoved;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void updateVisibility();

    BandAccess& access;
    juce::UndoManager& undoManager;
    int band = -1;

    juce::Label title, noteBadge;
    IconMiniButton prevBtn { "Banda anterior (Shift+Tab)", IconMiniButton::Glyph::Prev };
    IconMiniButton nextBtn { "Banda siguiente (Tab)", IconMiniButton::Glyph::Next };
    IconMiniButton closeBtn { "Cerrar ficha (Esc)", IconMiniButton::Glyph::Close };
    juce::ComboBox type, slope, placement;
    juce::TextButton bypass { "Bypass" }, solo { "Solo" }, dynamic { zeq::utf8 ("Dinámica") },
                     invertGain { zeq::utf8 ("± Inv") }, remove { "Eliminar" };
    Knob freq { "Frecuencia" }, gain { "Ganancia" }, q { "Q" };
    Knob threshold { "Umbral" }, range { "Rango" }, ratio { "Ratio" }, attack { "Ataque" }, release { zeq::utf8 ("Relajación") };

    std::unique_ptr<juce::ComboBoxParameterAttachment> typeAtt, slopeAtt, placeAtt;
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAtt, dynAtt;

    std::function<bool()> isAutoListenOn;
    bool freqTempSolo = false;
    bool freqPrevSolo = false;

    bool draggingCard = false;
    juce::Point<int> cardDragStartOffset {};

    FilterType shownType = FilterType::Count;
    int shownSlope = -1;
    bool shownDyn = false;
    float currentDynDb = 0.0f;

    // Geometría calculada en resized() y usada en paint() / mouseDown().
    juce::Rectangle<float> iconBounds {};      // icono de filtro (clic = siguiente tipo)
    juce::Rectangle<float> dynMeterBounds {};  // medidor en vivo de la ganancia dinámica
    juce::Rectangle<int> dynHeaderBounds {};   // título de la sección Dinámica
    float dividerY = -1.0f;                    // separador entre cuerpo y dinámica
};
} // namespace zeq::ui
