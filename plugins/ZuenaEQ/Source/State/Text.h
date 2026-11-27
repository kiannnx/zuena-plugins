#pragma once
// JUCE interpreta los literales `const char*` como ASCII: todo texto con acentos o símbolos
// (á, é, ±, ·) debe pasar por utf8(). Ver EB-14 en la lista de errores básicos.

#include <juce_core/juce_core.h>

namespace zeq
{
inline juce::String utf8 (const char* text)
{
    return juce::String::fromUTF8 (text);
}
} // namespace zeq
