#pragma once
// Grabación a WAV desde el hilo de servicio (nunca desde el hilo de audio): lee el anillo local,
// codifica y escribe al disco. El encabezado se actualiza cada segundo (archivo válido aunque el DAW
// se cierre de golpe) y pasa a RF64 si supera 4 GiB. Si el disco se llena, se detiene limpio y avisa.
#include "../Core/BroadcastRing.h"
#include "../Core/WavFormat.h"
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace zlink
{
class Recorder
{
  public:
    struct Status
    {
        bool recording = false;
        std::filesystem::path file;  // último archivo (grabando o terminado)
        double seconds = 0;
        std::uint64_t bytes = 0;
        std::uint32_t cuts = 0;      // el disco no dio abasto y se perdió audio (no debería pasar)
        std::uint32_t clipped = 0;   // muestras recortadas por pasar de 0 dBFS (formatos enteros)
        std::string error;
    };

    Recorder();
    ~Recorder();
    Recorder(const Recorder &) = delete;
    Recorder &operator=(const Recorder &) = delete;

    /** Empieza a grabar desde lo último que escribió el DAW. */
    bool start(const std::filesystem::path &folder, const WavLayout &, const RingCursor *, const float *, std::uint32_t capacity);
    /** Lo llama el hilo de servicio seguido (cada ~10 ms). */
    void drain();
    void stop(const std::string &reason = {});
    bool recording() const noexcept { return file_ != nullptr; }
    Status status() const;

  private:
    std::FILE *file_ = nullptr;
    WavLayout layout_;
    SampleEncoder encoder_;
    RingReader reader_;
    std::vector<float> chunk_, mono_;
    std::vector<std::uint8_t> bytes_;
    std::uint64_t frames_ = 0, lastHeaderFrames_ = 0;
    mutable std::mutex mutex_;
    Status status_;
    bool writeHeader();
    void fail(const std::string &);
};

std::filesystem::path defaultRecordingFolder();
/** "ZUENA Link 2026-11-28 14-03-22.wav", con " (2)"… si ya existe. */
std::filesystem::path nextRecordingPath(const std::filesystem::path &folder);
} // namespace zlink
