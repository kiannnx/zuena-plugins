#pragma once
// El motor de ZUENA Link: recibe el audio de la pista y lo copia a tres destinos independientes.
//   process()  → hilo de audio del DAW: solo copia a anillos sin bloqueos (cero reservas, cero esperas).
//   servicio   → hilo propio: toma y renueva el canal de OBS, abre y vigila el dispositivo, escribe el WAV,
//                arma el estado que muestra la interfaz.
// La pista nunca se modifica: el plugin deja pasar el audio tal cual, con 0 muestras de latencia.
#include "../Core/BroadcastRing.h"
#include "../Core/LinkProtocol.h"
#include "../Core/WavFormat.h"
#include "DeviceCatalog.h"
#include "Recorder.h"
#include "SharedChannel.h"
#include "WasapiOutput.h"
#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace zlink
{
struct LinkControls
{
    bool obs = true;
    std::uint32_t channel = 0; // 0…7 → Link 1…8
    bool device = false;
    float sendDb = 0;
    bool mono = false;
};

struct DeviceSettings
{
    std::wstring id;
    std::string name; // para mostrarlo aunque el dispositivo no esté conectado
    bool exclusive = false;
    std::uint32_t pair = 0;
    bool operator==(const DeviceSettings &) const = default;
};

struct RecordSettings
{
    std::filesystem::path folder;
    SampleFormat format = SampleFormat::pcm24;
};

enum class ObsState : std::uint8_t
{
    off,       // destino apagado
    opening,   // preparando el canal
    waiting,   // el canal es nuestro, OBS todavía no lo lee
    connected, // OBS está recibiendo
    busy,      // otra pista ya usa este canal
    exporting, // el DAW está exportando: los destinos en vivo se pausan
    failed
};

struct LinkSnapshot
{
    ObsState obs = ObsState::off;
    std::uint32_t channel = 0;
    double obsLatencyMs = 0;
    std::uint32_t obsCuts = 0;
    bool obsContender = false;          // otra pista pide el mismo canal
    std::string otherOwner;             // programa que usa el canal cuando está ocupado
    std::array<bool, kLinkChannels> channelInUse{}; // usado por otra instancia
    bool deviceOn = false;
    WasapiOutput::Status device;
    DeviceSettings deviceSettings;
    bool deviceMissing = false;         // el dispositivo guardado no está conectado
    std::vector<Endpoint> endpoints;
    std::uint32_t endpointsVersion = 0;
    Recorder::Status rec;
    RecordSettings recordSettings;
    double sampleRate = 0;
    bool offline = false;
};

class LinkEngine
{
  public:
    static constexpr std::uint32_t kLocalCapacity = 1u << 18; // 5,4 s a 48 kHz para dispositivo y grabación
    static constexpr std::size_t kMaxChunk = 2048;

    LinkEngine();
    ~LinkEngine();
    LinkEngine(const LinkEngine &) = delete;
    LinkEngine &operator=(const LinkEngine &) = delete;

    void prepare(double sampleRate, int maxBlock);
    /** Hilo de audio. `right` vacío para pistas mono. */
    void process(std::span<const float> left, std::span<const float> right, const LinkControls &, bool offline) noexcept;
    /** Hilo de audio, con el plugin en bypass: se deja de mandar. */
    void processBypassed() noexcept;

    void setDevice(const DeviceSettings &);
    DeviceSettings device() const;
    void setRecording(const RecordSettings &);
    RecordSettings recording() const;
    void startRecording();
    void stopRecording();
    void refreshDevices();
    LinkSnapshot snapshot() const;
    /** Pico de envío desde la última lectura (lo consume la interfaz). */
    float takePeak(int channel) noexcept;

  private:
    // --- lado de audio ---
    std::vector<float> localData_;
    RingCursor localCursor_;
    RingWriter localWriter_;
    std::array<float, kMaxChunk> scratchL_{}, scratchR_{};
    float gain_ = 0;
    std::atomic<std::uint32_t> rate_{48000}, block_{0};
    std::atomic<bool> obsWanted_{false}, deviceWanted_{false}, offline_{false}, liveInput_{false};
    std::atomic<std::uint32_t> desiredChannel_{0};
    std::atomic<std::uint64_t> rtTicks_{0};
    std::atomic<float> peak_[2]{};
    std::atomic<std::uint32_t> rtBusy_{0};
    std::atomic<SharedChannel *> active_{nullptr};
    std::atomic<bool> mono_{false};

    // --- servicio ---
    std::thread service_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> recordStart_{false}, recordStop_{false}, refresh_{true};
    std::array<std::unique_ptr<SharedChannel>, kLinkChannels> channels_;
    std::uint64_t token_ = 0;
    int owned_ = -1;
    WasapiOutput output_;
    Recorder recorder_;
    std::uint32_t recordRate_ = 0;
    mutable std::mutex mutex_;
    LinkSnapshot shared_; // lo que arma el servicio para la interfaz
    DeviceSettings deviceSettings_;
    RecordSettings recordSettings_;

    void serviceLoop();
    void tickObs(std::uint64_t now, std::uint64_t ticks, bool alive);
    void tickDevice();
    void tickRecorder();
    void deactivate();
    SharedChannel *channel(std::uint32_t index);
};
} // namespace zlink
