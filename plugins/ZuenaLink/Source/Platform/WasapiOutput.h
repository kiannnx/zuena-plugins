#pragma once
// Salida a un dispositivo de Windows por WASAPI, en su propio hilo con prioridad "Pro Audio" (MMCSS).
//   - Compartido de baja latencia: IAudioClient3 con el período mínimo del motor de audio cuando el
//     driver lo permite (2,67 ms a 48 kHz en placas modernas); si no, el período por defecto.
//   - Exclusivo: el dispositivo queda solo para nosotros, con el período mínimo del hardware.
//   - Par de canales a elección en endpoints multicanal (salidas 3-4 de una placa, por ejemplo).
//   - ClockBridge corrige la deriva entre el reloj del DAW y el del dispositivo (no hay clics).
//   - Si el dispositivo se desconecta o cambia de formato, se reconecta solo, con espera progresiva.
#include "../Core/BroadcastRing.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace zlink
{
class WasapiOutput
{
  public:
    struct Request
    {
        std::wstring deviceId;
        bool exclusive = false;
        std::uint32_t pair = 0; // 0 → canales 1-2, 1 → 3-4…
        double targetMs = 8;    // colchón además del bloque del DAW
    };
    struct Source
    {
        const RingCursor *cursor = nullptr;
        const float *data = nullptr;
        std::uint32_t capacity = 0;
        const std::atomic<std::uint32_t> *sampleRate = nullptr;
        const std::atomic<std::uint32_t> *blockFrames = nullptr;
        const std::atomic<bool> *active = nullptr; // el DAW está mandando audio en vivo
    };
    enum class Phase : std::uint8_t
    {
        stopped,
        opening,
        running,
        retrying
    };
    struct Status
    {
        Phase phase = Phase::stopped;
        std::string error;       // último problema, en palabras para el usuario
        std::uint32_t deviceRate = 0, deviceChannels = 0;
        double periodMs = 0, latencyMs = 0;
        std::uint32_t cuts = 0;
        bool exclusive = false;
        bool lowLatencyShared = false;
    };

    WasapiOutput() = default;
    ~WasapiOutput();
    WasapiOutput(const WasapiOutput &) = delete;
    WasapiOutput &operator=(const WasapiOutput &) = delete;

    void start(const Request &, const Source &);
    void stop();
    bool running() const noexcept { return thread_.joinable(); }
    const Request &request() const noexcept { return request_; }
    Status status() const;

  private:
    Request request_;
    Source source_;
    std::thread thread_;
    void *stopEvent_ = nullptr;
    mutable std::mutex mutex_;
    Status status_;
    std::atomic<std::uint32_t> cuts_{0};
    std::atomic<double> latencyMs_{0};
    std::atomic<std::uint8_t> phase_{0};

    void run();
    bool session(); // true = se pidió detener; false = error (se reintenta)
    void setPhase(Phase, const std::string &error = {});
};
} // namespace zlink
