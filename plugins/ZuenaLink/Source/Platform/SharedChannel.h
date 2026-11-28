#pragma once
// Bloque de memoria compartida de un canal "Link N" (Win32). Lo abren igual el plugin y la fuente de OBS:
// el primero que llega lo crea, el resto lo abre; el orden de arranque no importa.
// El descriptor de seguridad deja entrar a cualquier usuario interactivo, también con integridad baja,
// para que funcione aunque OBS corra como administrador o el DAW aísle sus plugins.
#include "../Core/LinkProtocol.h"
#include <cstdint>

namespace zlink
{
class SharedChannel
{
  public:
    SharedChannel() = default;
    ~SharedChannel();
    SharedChannel(const SharedChannel &) = delete;
    SharedChannel &operator=(const SharedChannel &) = delete;

    /** Abre (o crea) el canal `index` (0…7). No bloquea: si otro proceso lo está inicializando, falla y se reintenta. */
    bool open(std::uint32_t index);
    void close() noexcept;
    bool isOpen() const noexcept { return header_ != nullptr; }
    std::uint32_t index() const noexcept { return index_; }

    SharedHeader *header() const noexcept { return header_; }
    float *samples() const noexcept { return samples_; }
    RingWriter writer() const noexcept { return {&header_->cursor, samples_, kSharedCapacityFrames}; }
    RingReader reader() const noexcept { return {&header_->cursor, samples_, kSharedCapacityFrames}; }

  private:
    void *mapping_ = nullptr;
    void *view_ = nullptr;
    SharedHeader *header_ = nullptr;
    float *samples_ = nullptr;
    std::uint32_t index_ = 0;
};

/** Milisegundos desde el arranque de Windows (el mismo reloj en todos los procesos). */
std::uint64_t nowMs() noexcept;
std::uint32_t currentPid() noexcept;
/** Nombre del programa que carga el plugin ("FL64.exe" → "FL Studio"), en UTF-8. */
const char *hostDisplayName() noexcept;
} // namespace zlink
