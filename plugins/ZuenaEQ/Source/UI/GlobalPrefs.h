#pragma once
// Preferencias de interfaz compartidas por todas las instancias de ZUENA EQ (tema, tamaño).
// Archivo de texto `clave=valor` en %APPDATA%\ZUENA\ZuenaEQ.settings. Escribir una clave conserva las demás
// (antes se sobrescribía el archivo entero con `dark=…`).

#include <juce_core/juce_core.h>

namespace zeq::ui::prefs
{
inline juce::File file()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("ZUENA")
        .getChildFile ("ZuenaEQ.settings");
}

/** Valor de `key`, o cadena vacía si no existe. */
inline juce::String get (const juce::String& key)
{
    juce::StringArray lines;
    lines.addLines (file().loadFileAsString());
    for (const auto& l : lines)
        if (l.upToFirstOccurrenceOf ("=", false, false).trim().equalsIgnoreCase (key))
            return l.fromFirstOccurrenceOf ("=", false, false).trim();
    return {};
}

inline void set (const juce::String& key, const juce::String& value)
{
    const auto f = file();
    juce::StringArray lines;
    lines.addLines (f.loadFileAsString());
    lines.removeEmptyStrings();
    bool found = false;
    for (auto& l : lines)
        if (l.upToFirstOccurrenceOf ("=", false, false).trim().equalsIgnoreCase (key))
        {
            l = key + "=" + value;
            found = true;
        }
    if (! found)
        lines.add (key + "=" + value);
    f.getParentDirectory().createDirectory();
    f.replaceWithText (lines.joinIntoString ("\n") + "\n");
}
} // namespace zeq::ui::prefs
