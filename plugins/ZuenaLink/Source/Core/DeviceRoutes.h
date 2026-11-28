#pragma once
// Reconoce, por los textos que publica cada driver, qué salidas de Windows sirven para transmitir:
// cables virtuales, mezcladores de streaming y los canales virtuales / loopback que traen las placas
// actuales (Focusrite, Universal Audio, RME, MOTU, Audient, SSL, Steinberg, Arturia…).
// Es una tabla de patrones pura y probada: nada de lo que dice acá depende de Windows.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zlink
{
enum class RouteKind : std::uint8_t
{
    hardware,        // salida común (parlantes, auriculares)
    interfaceVirtual, // canal virtual o de loopback del driver de una placa de audio
    virtualCable,    // cable virtual (VB-Audio, Virtual Audio Cable)
    streamMixer      // mezclador de streaming (Voicemeeter, Elgato Wave Link, SteelSeries Sonar…)
};

struct DeviceText
{
    std::string name;    // nombre del endpoint, p. ej. "Altavoces"
    std::string adapter; // nombre del dispositivo/driver, p. ej. "Focusrite USB Audio"
};

struct RouteInfo
{
    RouteKind kind = RouteKind::hardware;
    std::string vendor; // "Focusrite", "Universal Audio"… (vacío si no se reconoce)
    bool interfaceHasLoopback = false; // la placa ofrece loopback o canales virtuales en su software
    /** Qué elegir en OBS o Streamlabs para recibir este audio (texto para el usuario). */
    std::string obsHint;
};

RouteInfo classifyRender(const DeviceText &);
/** Una entrada de grabación que devuelve lo que suena (loopback de la placa o salida de un cable virtual). */
bool isLoopbackCapture(const DeviceText &);
/** Las salidas que conviene mostrar primero: cables, mezcladores y canales virtuales de placas. */
inline bool isRecommended(const RouteInfo &r) noexcept
{
    return r.kind != RouteKind::hardware || r.interfaceHasLoopback;
}
std::string lowerAscii(std::string_view);
} // namespace zlink
