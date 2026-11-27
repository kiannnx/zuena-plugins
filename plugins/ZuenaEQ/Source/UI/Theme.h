#pragma once
// Tokens de la marca ZUENA (sistema "Punto", brand-system.css) para la UI JUCE en Modo Claro y Modo Oscuro.
// Iridiscente solo como luz: brillos, espectro y anillos de nodo, nunca rellenos sólidos de superficie.

#include "../Core/ZeqTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace zeq::ui::theme
{
inline bool darkScheme = false;

inline juce::Colour paper    { 0xfff5f5f3 };
inline juce::Colour surface  { 0xfffbfbfa };
inline juce::Colour raised   { 0xffffffff };
inline juce::Colour ink      { 0xff181a1c };
inline juce::Colour muted    { 0xff62666a };
inline juce::Colour line     { 0x211a1e22 };
inline juce::Colour danger   { 0xffb33d55 };
inline juce::Colour positive { 0xff24734e };
inline juce::Colour caution  { 0xff895b19 };
inline juce::Colour shadow   { 0x24101418 };

inline const juce::Colour iri1 { 0xff5ff0ff }, iri2 { 0xffb9a8ff }, iri3 { 0xffffb3e1 }, iri4 { 0xffffd28a };

inline bool isDark() noexcept { return darkScheme; }

/** Aplica los tokens oficiales de brand-system.css (Claro / Oscuro) en el hilo de interfaz. */
inline void setDark (bool dark) noexcept
{
    darkScheme = dark;
    if (dark)
    {
        paper    = juce::Colour (0xff101112); // --z-paper: #101112
        surface  = juce::Colour (0xff191b1d); // --z-surface: #191b1d
        raised   = juce::Colour (0xff222528); // --z-raised: #222528
        ink      = juce::Colour (0xfff1f1ef); // --z-ink: #f1f1ef
        muted    = juce::Colour (0xffa6a9ad); // --z-muted: #a6a9ad
        line     = juce::Colour (0x24eef1f4); // --z-line: rgb(238 241 244 / .14)
        danger   = juce::Colour (0xffed9bac); // --z-danger: #ed9bac
        positive = juce::Colour (0xff8dc7a8); // --z-positive: #8dc7a8
        caution  = juce::Colour (0xffdec08d); // --z-caution: #dec08d
        shadow   = juce::Colour (0x8c000000);
    }
    else
    {
        paper    = juce::Colour (0xfff5f5f3); // --z-paper: #f5f5f3
        surface  = juce::Colour (0xfffbfbfa); // --z-surface: #fbfbfa
        raised   = juce::Colour (0xffffffff); // --z-raised: #ffffff
        ink      = juce::Colour (0xff181a1c); // --z-ink: #181a1c
        muted    = juce::Colour (0xff62666a); // --z-muted: #62666a
        line     = juce::Colour (0x211a1e22); // --z-line: rgb(26 30 34 / .13)
        danger   = juce::Colour (0xffb33d55); // --z-danger: #b33d55
        positive = juce::Colour (0xff24734e); // --z-positive: #24734e
        caution  = juce::Colour (0xff895b19); // --z-caution: #895b19
        shadow   = juce::Colour (0x24101418);
    }
}

/** Colores de banda: familia iridiscente calibrada para leerse con nitidez en Claro y en Oscuro. */
inline juce::Colour bandColour (int band)
{
    static const juce::Colour lightColours[] = {
        juce::Colour (0xff22b8cf), juce::Colour (0xff5a9dff), juce::Colour (0xff8f7cf0),
        juce::Colour (0xffbd72e6), juce::Colour (0xffe96fb4), juce::Colour (0xfff0886f),
        juce::Colour (0xffd9973a), juce::Colour (0xff3fbf94)
    };
    static const juce::Colour darkColours[] = {
        juce::Colour (0xff52dcf2), juce::Colour (0xff7eb6ff), juce::Colour (0xffaf9fff),
        juce::Colour (0xffd894fa), juce::Colour (0xffff91ca), juce::Colour (0xffffa48c),
        juce::Colour (0xffffbf69), juce::Colour (0xff63e2b7)
    };
    const size_t idx = (size_t) (band % 8);
    return darkScheme ? darkColours[idx] : lightColours[idx];
}

juce::Font font (float height, bool semibold = false);

/** Dibuja el glifo vectorial monocromo de la curva de un tipo de filtro. */
void drawFilterIcon (juce::Graphics& g, FilterType type, juce::Rectangle<float> bounds, juce::Colour colour, float stroke = 1.65f);

inline constexpr float radius = 24.0f;
inline constexpr float radiusSmall = 14.0f;
} // namespace zeq::ui::theme
