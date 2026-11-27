#include "ZuenaLookAndFeel.h"

#include <BinaryData.h>

namespace zeq::ui
{
namespace theme
{
juce::Font font (float height, bool semibold)
{
    static const juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    static const juce::Typeface::Ptr bold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
    return juce::Font (juce::FontOptions (semibold ? bold : regular).withHeight (height));
}

void drawFilterIcon (juce::Graphics& g, FilterType type, juce::Rectangle<float> r, juce::Colour colour, float stroke)
{
    const float x0 = r.getX(), x1 = r.getRight(), xm = r.getCentreX();
    const float y0 = r.getY() + 2.0f, y1 = r.getBottom() - 2.0f, ym = r.getCentreY();
    const float w = r.getWidth();
    juce::Path p;

    switch (type)
    {
        case FilterType::Bell:
            p.startNewSubPath (x0, ym + 2.0f);
            p.cubicTo (x0 + w * 0.28f, ym + 2.0f, xm - w * 0.16f, y0, xm, y0);
            p.cubicTo (xm + w * 0.16f, y0, x1 - w * 0.28f, ym + 2.0f, x1, ym + 2.0f);
            break;
        case FilterType::LowShelf:
            p.startNewSubPath (x0, y0 + 1.0f);
            p.lineTo (x0 + w * 0.28f, y0 + 1.0f);
            p.cubicTo (xm, y0 + 1.0f, xm, ym + 2.5f, x1 - w * 0.28f, ym + 2.5f);
            p.lineTo (x1, ym + 2.5f);
            break;
        case FilterType::HighShelf:
            p.startNewSubPath (x0, ym + 2.5f);
            p.lineTo (x0 + w * 0.28f, ym + 2.5f);
            p.cubicTo (xm, ym + 2.5f, xm, y0 + 1.0f, x1 - w * 0.28f, y0 + 1.0f);
            p.lineTo (x1, y0 + 1.0f);
            break;
        case FilterType::LowCut:
            p.startNewSubPath (x0 + w * 0.12f, y1);
            p.cubicTo (x0 + w * 0.26f, y0 + 2.0f, xm - w * 0.08f, ym - 1.0f, xm + w * 0.12f, ym - 1.0f);
            p.lineTo (x1, ym - 1.0f);
            break;
        case FilterType::HighCut:
            p.startNewSubPath (x0, ym - 1.0f);
            p.lineTo (xm - w * 0.12f, ym - 1.0f);
            p.cubicTo (xm + w * 0.08f, ym - 1.0f, x1 - w * 0.26f, y0 + 2.0f, x1 - w * 0.12f, y1);
            break;
        case FilterType::Notch:
            p.startNewSubPath (x0, ym - 2.0f);
            p.cubicTo (xm - w * 0.16f, ym - 2.0f, xm - w * 0.07f, y1, xm, y1);
            p.cubicTo (xm + w * 0.07f, y1, xm + w * 0.16f, ym - 2.0f, x1, ym - 2.0f);
            break;
        case FilterType::BandPass:
            p.startNewSubPath (x0, y1);
            p.cubicTo (x0 + w * 0.26f, y1, xm - w * 0.14f, y0, xm, y0);
            p.cubicTo (xm + w * 0.14f, y0, x1 - w * 0.26f, y1, x1, y1);
            break;
        case FilterType::TiltShelf:
            p.startNewSubPath (x0, y1 - 1.0f);
            p.cubicTo (xm - w * 0.12f, y1 - 1.0f, xm + w * 0.12f, y0 + 1.0f, x1, y0 + 1.0f);
            break;
        case FilterType::FlatTilt:
            p.startNewSubPath (x0, y1 - 1.5f);
            p.lineTo (x1, y0 + 1.5f);
            break;
        case FilterType::AllPass:
            p.addCentredArc (xm, ym, (y1 - y0) * 0.36f, (y1 - y0) * 0.36f, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
            p.startNewSubPath (xm, y0);
            p.lineTo (xm, y1);
            break;
        case FilterType::Count:
            break;
    }

    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace theme

ZuenaLookAndFeel::ZuenaLookAndFeel()
{
    applyTheme();
}

void ZuenaLookAndFeel::applyTheme()
{
    using namespace theme;
    setColour (juce::ResizableWindow::backgroundColourId, paper);
    setColour (juce::Label::textColourId, ink);
    setColour (juce::Label::backgroundWhenEditingColourId, surface);
    setColour (juce::Label::outlineWhenEditingColourId, ink);
    setColour (juce::TextEditor::textColourId, ink);
    setColour (juce::TextEditor::backgroundColourId, surface);
    setColour (juce::TextEditor::highlightColourId, iri2.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, ink);
    setColour (juce::TextButton::buttonColourId, raised);
    setColour (juce::TextButton::buttonOnColourId, ink);
    setColour (juce::TextButton::textColourOffId, ink);
    setColour (juce::TextButton::textColourOnId, raised);
    setColour (juce::ComboBox::backgroundColourId, raised);
    setColour (juce::ComboBox::textColourId, ink);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::arrowColourId, muted);
    setColour (juce::PopupMenu::backgroundColourId, raised);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::headerTextColourId, muted);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ink);
    setColour (juce::PopupMenu::highlightedTextColourId, raised);
    setColour (juce::Slider::rotarySliderFillColourId, ink);
    setColour (juce::Slider::rotarySliderOutlineColourId, line);
    setColour (juce::TooltipWindow::backgroundColourId, ink);
    setColour (juce::TooltipWindow::textColourId, raised);
}

void ZuenaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider& s)
{
    using namespace theme;
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.5f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float thickness = juce::jmax (2.2f, r * 0.145f);
    const bool active = s.isMouseOverOrDragging() && s.isEnabled();

    if (active)
    {
        // Luz iridiscente tenue al interactuar (solo luz, nunca relleno sólido).
        juce::ColourGradient halo (iri1.withAlpha (0.22f), c.x - r, c.y - r, iri2.withAlpha (0.22f), c.x + r, c.y + r, false);
        g.setGradientFill (halo);
        g.fillEllipse (c.x - r - 1.5f, c.y - r - 1.5f, (r + 1.5f) * 2.0f, (r + 1.5f) * 2.0f);
    }

    juce::Path track;
    track.addCentredArc (c.x, c.y, r - thickness, r - thickness, 0.0f, startAngle, endAngle, true);
    g.setColour (line);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Parámetros bipolares (ganancia, rango): el arco nace en cero.
    const double lo = s.getMinimum(), hi = s.getMaximum();
    const float zeroPos = (lo < 0.0 && hi > 0.0) ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
    const float a0 = startAngle + zeroPos * (endAngle - startAngle);
    const float a1 = startAngle + pos * (endAngle - startAngle);
    juce::Path value;
    value.addCentredArc (c.x, c.y, r - thickness, r - thickness, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
    // Acento opcional (color de la banda en la ficha); sin acento, la tinta del tema.
    const auto accentVar = s.getProperties()["accent"];
    const auto arc = accentVar.isVoid() ? ink : juce::Colour ((juce::uint32) (juce::int64) accentVar);
    g.setColour (s.isEnabled() ? arc : muted);
    g.strokePath (value, juce::PathStrokeType (thickness + (active ? 0.4f : 0.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = r - thickness * 2.5f;
    g.setColour (active ? surface : raised);
    g.fillEllipse (c.x - knobR, c.y - knobR, knobR * 2.0f, knobR * 2.0f);
    g.setColour (active ? ink.withAlpha (0.28f) : line);
    g.drawEllipse (c.x - knobR, c.y - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);
    const auto tip = c.getPointOnCircumference (knobR * 0.74f, a1);
    g.setColour (ink);
    g.drawLine ({ c.getPointOnCircumference (knobR * 0.22f, a1), tip }, 1.85f);
}

void ZuenaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    using namespace theme;
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = juce::jmin (radiusSmall * 0.8f, r.getHeight() * 0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? ink : raised;
    if (down)
        fill = on ? (isDark() ? ink.darker (0.12f) : ink.brighter (0.12f))
                  : (isDark() ? raised.brighter (0.12f) : surface.darker (0.04f));
    else if (highlighted && ! on)
        fill = isDark() ? raised.brighter (0.07f) : surface.darker (0.015f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, corner);
    if (! on)
    {
        g.setColour (highlighted ? ink.withAlpha (0.24f) : line);
        g.drawRoundedRectangle (r, corner, 1.0f);
    }
}

void ZuenaLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    using namespace theme;
    g.setFont (getTextButtonFont (b, b.getHeight()));
    const bool isDanger = b.getProperties().contains ("danger");
    const auto colour = b.getToggleState() ? raised : (isDanger ? danger : ink);
    g.setColour (colour.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.42f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
}

juce::Font ZuenaLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return theme::font (juce::jlimit (11.0f, 13.5f, (float) buttonHeight * 0.43f), true);
}

void ZuenaLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace theme;
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.isMouseOver() ? surface : raised);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (box.isMouseOver() ? ink.withAlpha (0.24f) : line);
    g.drawRoundedRectangle (r, 10.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 15.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.5f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (muted);
    g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font ZuenaLookAndFeel::getComboBoxFont (juce::ComboBox&) { return theme::font (12.5f, true); }

void ZuenaLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font ZuenaLookAndFeel::getPopupMenuFont() { return theme::font (13.5f); }

void ZuenaLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (theme::raised);
    g.setColour (theme::line);
    g.drawRect (0, 0, w, h, 1);
}

juce::Font ZuenaLookAndFeel::getLabelFont (juce::Label& l) { return theme::font (juce::jmax (11.0f, l.getFont().getHeight())); }
} // namespace zeq::ui
