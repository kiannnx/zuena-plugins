#include "DeviceRoutes.h"
#include <array>

namespace zlink
{
std::string lowerAscii(std::string_view s)
{
    std::string out(s);
    for (auto &c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}

namespace
{
bool has(const std::string &text, std::string_view needle)
{
    return text.find(needle) != std::string::npos;
}

struct Vendor
{
    std::string_view name;
    std::array<std::string_view, 6> needles;
    std::string_view hint;
};

// Placas con loopback o canales virtuales en su propio software (driver del fabricante).
constexpr std::array<Vendor, 8> vendors{{
    {"Universal Audio",
     {"universal audio", "apollo", "uad ", "volt", "", ""},
     "En UA Console mandá este canal a la mezcla que querés transmitir y en OBS capturá la entrada de la "
     "Apollo que la recibe."},
    {"Focusrite",
     {"focusrite", "scarlett", "clarett", "vocaster", "", ""},
     "En Focusrite Control elegí estas salidas como fuente del Loopback y en OBS capturá la entrada "
     "«Loopback» de la placa."},
    {"RME",
     {"rme ", "fireface", "babyface", "madiface", "digiface", "ucx"},
     "En TotalMix activá Loopback en el canal de salida que uses y en OBS capturá esa misma entrada."},
    {"MOTU", {"motu", "", "", "", "", ""}, "Activá el loopback de la placa en CueMix y en OBS capturá la entrada «Loopback»."},
    {"Audient",
     {"audient", "evo ", "evo4", "evo8", "evo16", ""},
     "Activá el loopback en el panel de Audient y en OBS capturá la entrada de loopback de la placa."},
    {"Solid State Logic",
     {"solid state logic", "ssl 2", "ssl 12", "ssl usb", "", ""},
     "Activá Loopback en SSL 360 y en OBS capturá la entrada «Loopback» de la placa."},
    {"Steinberg / Yamaha",
     {"steinberg", "yamaha", " ur22", " ur44", "ur-c", ""},
     "Activá Loopback en dspMixFx y en OBS capturá la entrada de la placa."},
    {"Arturia",
     {"minifuse", "arturia", "", "", "", ""},
     "Activá el loopback en MiniFuse Control Center y en OBS capturá la entrada «Loopback» de la placa."},
}};

std::string replaceInputWithOutput(const std::string &name)
{
    std::string out = name;
    for (std::string_view from : {std::string_view("Input"), std::string_view("input")})
        if (const auto at = out.find(from); at != std::string::npos)
        {
            out.replace(at, from.size(), from[0] == 'I' ? "Output" : "output");
            return out;
        }
    return out;
}
} // namespace

RouteInfo classifyRender(const DeviceText &d)
{
    const auto name = lowerAscii(d.name);
    const auto adapter = lowerAscii(d.adapter);
    const auto both = name + " " + adapter + " ";
    RouteInfo r;

    if (has(both, "vb-audio") || has(name, "cable input") || has(name, "cable-a input") || has(name, "cable-b input") ||
        has(name, "cable-c input") || has(name, "cable-d input"))
    {
        r.kind = RouteKind::virtualCable;
        r.vendor = "VB-Audio";
        const auto capture = replaceInputWithOutput(d.name);
        r.obsHint = "En OBS o Streamlabs agregá una captura de entrada de audio y elegí «" + capture +
                    (d.adapter.empty() ? std::string{} : " (" + d.adapter + ")") + "».";
        return r;
    }
    if (has(both, "virtual audio cable"))
    {
        r.kind = RouteKind::virtualCable;
        r.vendor = "Virtual Audio Cable";
        r.obsHint = "En OBS o Streamlabs agregá una captura de entrada de audio y elegí «" + d.name + "» de Virtual Audio Cable.";
        return r;
    }
    if (has(both, "voicemeeter"))
    {
        r.kind = RouteKind::streamMixer;
        r.vendor = "Voicemeeter";
        r.obsHint = "Asigná esta entrada de Voicemeeter a un bus y en OBS capturá la salida de ese bus (por ejemplo «Voicemeeter Out B1»).";
        return r;
    }
    if (has(both, "wave link"))
    {
        r.kind = RouteKind::streamMixer;
        r.vendor = "Elgato Wave Link";
        r.obsHint = "En Wave Link sumá este canal a la mezcla Stream y en OBS capturá «Wave Link Stream».";
        return r;
    }
    if (has(both, "sonar") && has(both, "steelseries"))
    {
        r.kind = RouteKind::streamMixer;
        r.vendor = "SteelSeries Sonar";
        r.obsHint = "En Sonar sumá este canal a la mezcla de streaming y en OBS capturá la salida Stream de Sonar.";
        return r;
    }

    for (const auto &v : vendors)
        for (const auto needle : v.needles)
            if (!needle.empty() && has(both, needle))
            {
                r.vendor = std::string(v.name);
                r.interfaceHasLoopback = true;
                r.kind = (has(name, "virtual") || has(name, "loopback")) ? RouteKind::interfaceVirtual : RouteKind::hardware;
                r.obsHint = std::string(v.hint);
                return r;
            }

    if (has(name, "loopback") || has(name, "virtual"))
    {
        r.kind = RouteKind::interfaceVirtual;
        r.obsHint = "En OBS capturá la entrada que devuelve este canal (suele llamarse «Loopback» o «Virtual»).";
    }
    return r;
}

bool isLoopbackCapture(const DeviceText &d)
{
    const auto name = lowerAscii(d.name);
    return has(name, "loopback") || has(name, "cable output") || has(name, "cable-a output") ||
           has(name, "cable-b output") || has(name, "voicemeeter out") || has(name, "wave link stream") ||
           (has(name, "virtual") && has(lowerAscii(d.adapter), "universal audio"));
}
} // namespace zlink
