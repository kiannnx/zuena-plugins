#pragma once
// Lista de salidas de audio de Windows (MMDevice/WASAPI) con su clasificación para transmitir,
// y un vigilante que avisa cuando se conecta, desconecta o cambia un dispositivo.
#include "../Core/DeviceRoutes.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zlink
{
struct Endpoint
{
    std::wstring id;       // identificador estable de Windows (se guarda en el proyecto)
    std::string name;      // "CABLE Input (VB-Audio Virtual Cable)"
    std::string shortName; // "CABLE Input"
    std::string adapter;   // "VB-Audio Virtual Cable"
    std::uint32_t channels = 2;
    std::uint32_t sampleRate = 48000;
    bool isDefault = false;
    RouteInfo route;
};

/** Requiere COM inicializado en el hilo que llama. Las recomendadas quedan primero. */
std::vector<Endpoint> listRenderEndpoints();

class DeviceWatcher
{
  public:
    DeviceWatcher();
    ~DeviceWatcher();
    DeviceWatcher(const DeviceWatcher &) = delete;
    DeviceWatcher &operator=(const DeviceWatcher &) = delete;
    /** Sube cada vez que Windows informa un cambio de dispositivos. */
    std::uint32_t generation() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::string toUtf8(const std::wstring &);
std::wstring fromUtf8(const std::string &);
} // namespace zlink
