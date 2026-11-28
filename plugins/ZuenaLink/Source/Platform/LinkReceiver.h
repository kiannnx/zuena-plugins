#pragma once
// Receptor de un canal "Link N": lo que corre dentro de OBS (y en las pruebas, sin OBS).
// Su propio hilo entrega audio al ritmo del reloj del sistema (el mismo que usa OBS para mezclar) con
// marcas de tiempo continuas; ClockBridge absorbe la deriva entre el reloj del DAW y ese reloj.
// El receptor informa en el canal su latencia y sus cortes, y el plugin los muestra.
#include "../Core/ClockBridge.h"
#include "SharedChannel.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace zlink
{
class LinkReceiver
{
  public:
    enum class Buffer : std::uint8_t
    {
        minimum = 0, // 5 ms además del bloque del DAW
        normal = 1,  // 10 ms
        safe = 2     // 25 ms (PCs exigidas)
    };
    static double bufferMs(Buffer b) noexcept { return b == Buffer::minimum ? 5.0 : (b == Buffer::safe ? 25.0 : 10.0); }

    enum class Phase : std::uint8_t
    {
        idle,      // canal sin dueño: no hay ningún ZUENA Link mandando a este canal
        paused,    // el plugin existe pero no manda audio en vivo (apagado, bypass, exportando, transporte quieto)
        receiving,
        failed     // no se pudo abrir la memoria compartida
    };
    struct Status
    {
        Phase phase = Phase::idle;
        std::string host;        // "FL Studio"
        std::uint32_t sourceRate = 0;
        double latencyMs = 0;
        std::uint32_t cuts = 0;
    };

    /** Recibe cuadros intercalados L/R a `outputRate`, con la marca de tiempo del primer cuadro. */
    using Output = std::function<void(const float *interleaved, std::uint32_t frames, std::uint64_t timestampNs)>;
    using Clock = std::uint64_t (*)();

    LinkReceiver() = default;
    ~LinkReceiver();
    LinkReceiver(const LinkReceiver &) = delete;
    LinkReceiver &operator=(const LinkReceiver &) = delete;

    void start(std::uint32_t channel, Buffer, std::uint32_t outputRate, Clock, Output);
    void stop();
    bool running() const noexcept { return thread_.joinable(); }
    Status status() const;

    static std::uint64_t systemClockNs() noexcept; // QueryPerformanceCounter en nanosegundos

  private:
    std::thread thread_;
    std::atomic<bool> quit_{false};
    std::uint32_t channel_ = 0, outputRate_ = 48000;
    Buffer buffer_ = Buffer::normal;
    Clock clock_ = nullptr;
    Output output_;
    mutable std::mutex mutex_;
    Status status_;
    void run();
};
} // namespace zlink
