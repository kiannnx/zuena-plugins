#pragma once
// Perilla ergonómica ZUENA:
// - Arrastre vertical/horizontal suave; Shift = ajuste fino (5x más preciso).
// - Rueda del ratón con paso fino (Shift + rueda = micro-ajuste).
// - Doble clic, Ctrl+clic o clic central restablece el valor por defecto.
// - Clic derecho abre menú contextual con valores rápidos y entrada exacta.
// - Clic en la etiqueta numérica abre edición por teclado (acepta notas musicales como "A4", "F#2", o "2.5k").

#include "../State/Text.h"
#include "Format.h"
#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace zeq::ui
{
class Knob final : public juce::Component
{
public:
    explicit Knob (const juce::String& caption) : titleText (caption)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setVelocityBasedMode (false);
        slider.setSliderSnapsToMousePosition (false);
        slider.setMouseDragSensitivity (210);
        slider.onValueChange = [this] { refreshValue(); };
        slider.onDragStart = [this]
        {
            slider.setMouseDragSensitivity (juce::ModifierKeys::currentModifiers.isShiftDown() ? 950 : 210);
            if (onGestureStart)
                onGestureStart();
        };
        slider.onDragEnd = [this]
        {
            slider.setMouseDragSensitivity (210);
            if (onGestureEnd)
                onGestureEnd();
        };
        slider.onResetRequested = [this] { resetToDefault(); };
        slider.onContextMenu = [this] { showQuickMenu(); };
        slider.onFineWheel = [this] (float deltaY, bool fine) { stepWheel (deltaY, fine); };
        slider.setName (caption);
        slider.setTitle (caption);
        addAndMakeVisible (slider);

        name.setText (caption, juce::dontSendNotification);
        name.setJustificationType (juce::Justification::centred);
        name.setColour (juce::Label::textColourId, theme::muted);
        name.setFont (theme::font (11.0f));
        name.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (name);

        value.setJustificationType (juce::Justification::centred);
        value.setColour (juce::Label::textColourId, theme::ink);
        value.setFont (theme::font (12.0f, true));
        value.setEditable (true, true, false);
        value.setTooltip (zeq::utf8 ("Clic para escribir valor o nota (ej. A4, 2.5k, -3.5) · Clic derecho en perilla: valores rápidos"));
        value.onTextChange = [this] { applyTypedValue(); };
        addAndMakeVisible (value);
    }

    std::function<void()> onGestureStart, onGestureEnd;

    /** Tamaño tipográfico: Compact (barra superior), Medium (dinámica de la ficha), Large (perillas principales). */
    enum class Style { Compact, Medium, Large };
    void setStyle (Style s)
    {
        style = s;
        name.setFont (theme::font (s == Style::Large ? 11.5f : 11.0f));
        value.setFont (theme::font (s == Style::Large ? 15.0f : (s == Style::Medium ? 12.5f : 12.0f), true));
        resized();
    }

    /** Color del arco de valor (el de la banda en la ficha). Sin acento, el arco usa la tinta del tema. */
    void setAccent (juce::Colour c)
    {
        slider.getProperties().set ("accent", (juce::int64) c.getARGB());
        slider.repaint();
    }

    void refreshTheme()
    {
        name.setColour (juce::Label::textColourId, theme::muted);
        value.setColour (juce::Label::textColourId, theme::ink);
        repaint();
    }

    void attach (juce::RangedAudioParameter& p, juce::UndoManager* undo)
    {
        attachment.reset();
        param = &p;
        undoMgr = undo;
        slider.textFromValueFunction = [&p] (double v)
        {
            return p.getText (p.convertTo0to1 ((float) v), 32) + (p.getLabel().isNotEmpty() ? " " + p.getLabel() : juce::String());
        };
        attachment = std::make_unique<juce::SliderParameterAttachment> (p, slider, undo);
        slider.setDoubleClickReturnValue (true, p.convertFrom0to1 (p.getDefaultValue()));
        refreshValue();
    }

    void detach()
    {
        attachment.reset();
        param = nullptr;
        undoMgr = nullptr;
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const bool large = style == Style::Large;
        name.setBounds (r.removeFromBottom (large ? 16 : 15));
        value.setBounds (r.removeFromBottom (large ? 21 : 17));
        // El dial queda cuadrado y centrado: con fichas anchas ya no se estira ni queda chico.
        const int side = juce::jmin (r.getWidth(), r.getHeight()) - 2;
        slider.setBounds (r.withSizeKeepingCentre (side, side));
    }

private:
    class Dial final : public juce::Slider
    {
    public:
        std::function<void()> onResetRequested;
        std::function<void()> onContextMenu;
        std::function<void (float, bool)> onFineWheel;

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                if (onContextMenu)
                    onContextMenu();
                return;
            }
            if (e.mods.isCommandDown() || e.mods.isMiddleButtonDown())
            {
                if (onResetRequested)
                    onResetRequested();
                return;
            }
            setMouseDragSensitivity (e.mods.isShiftDown() ? 950 : 210);
            juce::Slider::mouseDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            setMouseDragSensitivity (e.mods.isShiftDown() ? 950 : 210);
            juce::Slider::mouseDrag (e);
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (! isEnabled() || std::abs (w.deltaY) < 1.0e-5f)
                return;
            if (onFineWheel)
                onFineWheel (w.deltaY, e.mods.isShiftDown());
        }
    };

    void setPlainWithUndo (float plain)
    {
        if (param == nullptr)
            return;
        if (undoMgr != nullptr)
            undoMgr->beginNewTransaction();
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
        param->endChangeGesture();
        refreshValue();
    }

    void resetToDefault()
    {
        if (param == nullptr)
            return;
        if (undoMgr != nullptr)
            undoMgr->beginNewTransaction();
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->getDefaultValue());
        param->endChangeGesture();
        refreshValue();
    }

    void stepWheel (float deltaY, bool fine)
    {
        if (param == nullptr)
            return;
        if (undoMgr != nullptr)
            undoMgr->beginNewTransaction();
        const float step = deltaY * (fine ? 0.014f : 0.065f);
        const float next = juce::jlimit (0.0f, 1.0f, param->getValue() + step);
        param->beginChangeGesture();
        param->setValueNotifyingHost (next);
        param->endChangeGesture();
        refreshValue();
    }

    void showQuickMenu()
    {
        if (param == nullptr)
            return;
        juce::PopupMenu menu;
        menu.addSectionHeader (titleText);
        const float defPlain = param->convertFrom0to1 (param->getDefaultValue());
        menu.addItem (1, "Restablecer (" + param->getText (param->getDefaultValue(), 24)
                         + (param->getLabel().isNotEmpty() ? " " + param->getLabel() : "") + ")");
        menu.addItem (2, zeq::utf8 ("Escribir valor exacto…"));

        struct Preset { juce::String label; float val; };
        std::vector<Preset> presets;
        const auto id = param->getParameterID();
        if (id.endsWith ("_freq"))
            presets = { { "30 Hz (Sub)", 30.0f }, { "80 Hz (Cuerpo)", 80.0f }, { "250 Hz (Medios-graves)", 250.0f },
                        { "1 kHz (Centro)", 1000.0f }, { "3.2 kHz (Presencia)", 3200.0f }, { "10 kHz (Aire)", 10000.0f } };
        else if (id.endsWith ("_q"))
            presets = { { "0.50 (Suave)", 0.5f }, { "0.71 (Butterworth)", 0.7071f }, { "1.00 (Musical)", 1.0f },
                        { "2.00 (Focalizado)", 2.0f }, { "4.00 (Estrecho)", 4.0f }, { zeq::utf8 ("10.0 (Quirúrgico)"), 10.0f } };
        else if (param->getLabel() == "%")
            presets = { { "50 %", 50.0f }, { "75 %", 75.0f }, { "100 %", 100.0f }, { "125 %", 125.0f }, { "150 %", 150.0f } };
        else if (param->getLabel() == "dB" && defPlain == 0.0f)
            presets = { { "+6 dB", 6.0f }, { "+3 dB", 3.0f }, { "0 dB", 0.0f }, { "-3 dB", -3.0f }, { "-6 dB", -6.0f }, { "-12 dB", -12.0f } };

        if (! presets.empty())
        {
            menu.addSeparator();
            for (int i = 0; i < (int) presets.size(); ++i)
                menu.addItem (100 + i, presets[(size_t) i].label);
        }

        menu.setLookAndFeel (&getLookAndFeel());
        juce::Component::SafePointer<Knob> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, presets] (int r)
        {
            if (safe == nullptr || r == 0)
                return;
            if (r == 1)
                safe->resetToDefault();
            else if (r == 2)
                safe->value.showEditor();
            else if (r >= 100 && r - 100 < (int) presets.size())
                safe->setPlainWithUndo (presets[(size_t) (r - 100)].val);
        });
    }

    void refreshValue()
    {
        if (param != nullptr && ! value.isBeingEdited())
            value.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification);
    }

    void applyTypedValue()
    {
        if (param == nullptr)
            return;
        const auto txt = value.getText().trim();
        if (param->getParameterID().endsWith ("_freq"))
        {
            if (const auto hz = format::parseFrequencyOrNote (txt))
            {
                setPlainWithUndo (*hz);
                return;
            }
        }
        float norm = param->getValueForText (txt);
        const auto clean = txt.toLowerCase();
        const bool hasK = clean.contains ("k");
        const auto digits = clean.retainCharacters ("0123456789.,-+").replaceCharacter (',', '.');
        if (digits.isNotEmpty())
            norm = param->convertTo0to1 (digits.getFloatValue() * (hasK ? 1000.0f : 1.0f));
        if (undoMgr != nullptr)
            undoMgr->beginNewTransaction();
        param->beginChangeGesture();
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
        param->endChangeGesture();
        refreshValue();
    }

    juce::String titleText;
    Style style = Style::Compact;
    Dial slider;
    juce::Label name, value;
    juce::RangedAudioParameter* param = nullptr;
    juce::UndoManager* undoMgr = nullptr;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};
} // namespace zeq::ui
