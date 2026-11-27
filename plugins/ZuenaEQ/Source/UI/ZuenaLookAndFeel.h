#pragma once

#include "Theme.h"

namespace zeq::ui
{
class ZuenaLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ZuenaLookAndFeel();

    /** Reaplica los colores de componentes JUCE tras cambiar entre Modo Claro y Modo Oscuro. */
    void applyTheme();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getLabelFont (juce::Label&) override;
};
} // namespace zeq::ui
