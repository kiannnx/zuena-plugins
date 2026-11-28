#pragma once
// Puente entre dos relojes de audio: el del DAW (que empuja bloques) y el del destino (OBS o una placa,
// que pide cuadros a su propio ritmo). Dos relojes nunca coinciden: 50–200 ppm de diferencia vacían o
// llenan cualquier colchón en minutos y eso suena como clics. Acá:
//   - un colchón de entrada con un nivel objetivo (la latencia que elige el usuario),
//   - un controlador PI que ajusta una razón fina de remuestreo para sostener ese nivel,
//   - un interpolador polifásico de sinc con ventana de Kaiser (también hace 44,1 ↔ 48 kHz),
//   - fundidos de 2 ms ante un hueco real o una resincronización, nunca un corte seco.
// prepare() reserva memoria; push() y pull() no reservan ni bloquean.
#include <cstdint>
#include <span>
#include <vector>

namespace zlink
{
class ClockBridge
{
  public:
    struct Config
    {
        double inputRate = 48000;
        double outputRate = 48000;
        double targetMs = 10;    // margen mínimo que se sostiene por encima de cero (además del bloque)
        double capacityMs = 500; // máximo que se acepta antes de descartar
        double blockMs = 0;      // tamaño de los bloques que llegan de la fuente (el colchón sube y baja eso)
    };
    enum class State : std::uint8_t
    {
        buffering, // esperando juntar el colchón (sale silencio)
        running
    };
    struct Stats
    {
        State state = State::buffering;
        double fillMs = 0;     // latencia del colchón en este momento
        double ratioPpm = 0;   // corrección aplicada sobre la razón nominal
        std::uint32_t cuts = 0; // huecos reales con audio en curso + resincronizaciones
        std::uint32_t drops = 0; // veces que se descartó exceso de colchón
    };

    static constexpr int kHalfTaps = 32;      // 64 coeficientes
    static constexpr int kPhases = 512;       // filas de la tabla polifásica
    static constexpr double kFadeMs = 2.0;

    void prepare(const Config &);
    void reset() noexcept;
    bool prepared() const noexcept { return capacity_ > 0; }
    const Config &config() const noexcept { return config_; }

    /** Cuadros intercalados (L, R). Si no entran, se descarta lo más viejo y se cuenta un corte. */
    void push(std::span<const float> interleaved) noexcept;
    /** Llena `out` (out.size()/2 cuadros intercalados) al ritmo del destino. */
    void pull(std::span<float> out) noexcept;
    /** Avisa si la fuente está mandando audio en vivo: un vacío sin fuente activa no es un corte. */
    void setInputActive(bool active) noexcept { inputActive_ = active; }

    Stats stats() const noexcept;
    /** Cuadros de entrada disponibles por delante de la posición de lectura. */
    double fillFrames() const noexcept;

  private:
    Config config_{};
    std::vector<float> table_;  // (kPhases + 1) × 2·kHalfTaps
    std::vector<float> buffer_; // cuadros intercalados; [0, count_) válidos
    std::size_t capacity_ = 0;  // en cuadros
    std::size_t count_ = 0;
    double pos_ = 0;            // posición de lectura (cuadros) dentro del buffer
    double nominal_ = 1;        // inputRate / outputRate
    double integral_ = 0, adjust_ = 0, smoothedError_ = 0, smoothedError2_ = 0;
    double targetFrames_ = 0, blockFrames_ = 0, maxFrames_ = 0, fadeOut_ = 0;
    double windowMin_ = 0; // seguidor del mínimo del colchón (ver measure)
    double gain_ = 0, gainStep_ = 0;
    bool fadingIn_ = false, inputActive_ = true, pendingCut_ = false, haveError_ = false;
    std::uint64_t silentOutput_ = 0;
    State state_ = State::buffering;
    std::uint32_t cuts_ = 0, drops_ = 0;

    void buildTable();
    void compact() noexcept;
    void render(float *out, std::uint32_t frames, double step) noexcept;
    void control(std::uint32_t frames) noexcept;
    void measure(std::uint32_t frames) noexcept;
};
} // namespace zlink
