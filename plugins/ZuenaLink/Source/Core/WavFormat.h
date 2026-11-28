#pragma once
// Codificación WAV sin dependencias: encabezado RIFF que se convierte en RF64 al pasar los 4 GiB
// (el espacio queda reservado desde el principio con un bloque JUNK), y conversión de muestras.
// El encabezado se reescribe cada segundo: si el DAW se cierra de golpe el archivo sigue siendo válido.
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace zlink
{
enum class SampleFormat : std::uint8_t
{
    pcm16 = 0, // con dither TPDF
    pcm24 = 1,
    float32 = 2
};

constexpr std::uint32_t bytesPerSample(SampleFormat f) noexcept
{
    return f == SampleFormat::pcm16 ? 2u : (f == SampleFormat::pcm24 ? 3u : 4u);
}

struct WavLayout
{
    std::uint32_t sampleRate = 48000;
    std::uint16_t channels = 2;
    SampleFormat format = SampleFormat::pcm24;
};

/** Tamaño fijo del encabezado que escribe WavHeader (los datos empiezan en este byte). */
std::size_t wavHeaderBytes(const WavLayout &) noexcept;
/** Encabezado completo para `frames` cuadros ya escritos (RIFF o RF64 según el tamaño). */
std::vector<std::uint8_t> wavHeader(const WavLayout &, std::uint64_t frames);

/** Convierte cuadros float intercalados al formato del archivo. Devuelve los bytes escritos en `out`. */
class SampleEncoder
{
  public:
    explicit SampleEncoder(SampleFormat f = SampleFormat::pcm24) noexcept : format_(f) {}
    std::size_t encode(std::span<const float> interleaved, std::span<std::uint8_t> out) noexcept;
    std::uint32_t clipped() const noexcept { return clipped_; }

  private:
    SampleFormat format_;
    std::uint32_t seed_ = 0x9E3779B9u, clipped_ = 0;
    float dither() noexcept;
};

/** Lectura mínima (para pruebas y verificación): devuelve los cuadros como float, o vacío si no es válido. */
struct WavContents
{
    WavLayout layout;
    std::uint64_t frames = 0;
    bool rf64 = false;
    std::vector<float> samples;
};
bool parseWav(std::span<const std::uint8_t> file, WavContents &out);
} // namespace zlink
