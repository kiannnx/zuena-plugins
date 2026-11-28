#include "ClockBridge.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace zlink
{
namespace
{
constexpr double kBeta = 10.0;          // ventana de Kaiser: ~100 dB de rechazo
constexpr double kSmoothSeconds = 0.5;  // filtrado del error
// Controlador PI sobre el error en segundos: lazo lento (ωn ≈ 0,1 rad/s, ζ ≈ 0,75). La corrección
// cambia a lo sumo unas cientos de ppm en decenas de segundos: muy por debajo de lo audible.
constexpr double kProportional = 0.15;
constexpr double kIntegral = 0.01;
constexpr double kReleaseSeconds = 2.0; // el seguidor de mínimo baja al instante y sube despacio
constexpr double kMaxAdjust = 0.003;    // ±3000 ppm: ningún reloj real se aparta tanto
constexpr double kMaxIntegral = 0.002;

double besselI0(double x) noexcept
{
    double sum = 1, term = 1;
    for (int k = 1; k < 60; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1e-17)
            break;
    }
    return sum;
}
} // namespace

void ClockBridge::prepare(const Config &c)
{
    config_ = c;
    config_.inputRate = std::clamp(c.inputRate, 8000.0, 768000.0);
    config_.outputRate = std::clamp(c.outputRate, 8000.0, 768000.0);
    config_.targetMs = std::clamp(c.targetMs, 1.0, 400.0);
    config_.blockMs = std::clamp(c.blockMs, 0.0, 200.0);
    config_.capacityMs = std::max(c.capacityMs, config_.targetMs * 2 + config_.blockMs * 4 + 50);
    nominal_ = config_.inputRate / config_.outputRate;
    capacity_ = static_cast<std::size_t>(std::ceil(config_.inputRate * config_.capacityMs / 1000.0)) +
                4 * kHalfTaps + 16;
    buffer_.assign(capacity_ * 2, 0.0f);
    buildTable();
    reset();
}

void ClockBridge::buildTable()
{
    constexpr int taps = 2 * kHalfTaps;
    table_.assign(static_cast<std::size_t>(kPhases + 1) * taps, 0.0f);
    // Corte relativo a la frecuencia de entrada: al bajar de frecuencia se corta antes (anti-alias).
    const double cutoff = 0.47 * std::min(1.0, 1.0 / nominal_);
    const double norm = besselI0(kBeta);
    std::vector<double> row(taps);
    for (int p = 0; p <= kPhases; ++p)
    {
        const double frac = static_cast<double>(p) / kPhases;
        double sum = 0;
        for (int k = 0; k < taps; ++k)
        {
            const double t = (k - (kHalfTaps - 1)) - frac; // distancia a la posición de salida
            const double x = 2.0 * cutoff * t;
            const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
            const double u = t / kHalfTaps;
            const double window = std::abs(u) >= 1.0 ? 0.0 : besselI0(kBeta * std::sqrt(1.0 - u * u)) / norm;
            row[static_cast<std::size_t>(k)] = 2.0 * cutoff * sinc * window;
            sum += row[static_cast<std::size_t>(k)];
        }
        for (int k = 0; k < taps; ++k) // ganancia exacta 1 en continua para cada fase
            table_[static_cast<std::size_t>(p * taps + k)] = static_cast<float>(row[static_cast<std::size_t>(k)] / sum);
    }
}

void ClockBridge::reset() noexcept
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    count_ = kHalfTaps; // historia en silencio para el primer núcleo
    pos_ = kHalfTaps - 1;
    integral_ = adjust_ = smoothedError_ = 0;
    haveError_ = false;
    targetFrames_ = config_.inputRate * config_.targetMs / 1000.0;
    blockFrames_ = config_.inputRate * config_.blockMs / 1000.0;
    maxFrames_ = targetFrames_ * 2 + blockFrames_ * 3 + config_.inputRate * 0.040 + kHalfTaps;
    windowMin_ = 1e300;
    fadeOut_ = std::max(1.0, config_.outputRate * kFadeMs / 1000.0);
    gainStep_ = 1.0 / fadeOut_;
    gain_ = 0;
    fadingIn_ = false;
    pendingCut_ = false;
    silentOutput_ = 0;
    state_ = State::buffering;
    cuts_ = drops_ = 0;
}

double ClockBridge::fillFrames() const noexcept
{
    return static_cast<double>(count_) - 1.0 - pos_;
}

void ClockBridge::compact() noexcept
{
    const auto first = static_cast<std::ptrdiff_t>(std::floor(pos_)) - (kHalfTaps - 1);
    if (first <= 0)
        return;
    const auto k = static_cast<std::size_t>(first);
    std::memmove(buffer_.data(), buffer_.data() + k * 2, (count_ - k) * 2 * sizeof(float));
    count_ -= k;
    pos_ -= static_cast<double>(k);
}

void ClockBridge::push(std::span<const float> interleaved) noexcept
{
    if (!prepared())
        return;
    std::size_t frames = interleaved.size() / 2;
    const float *src = interleaved.data();
    if (count_ + frames > capacity_)
        compact();
    if (count_ + frames > capacity_)
    {
        // El destino dejó de pedir (o la fuente mandó una ráfaga enorme): se conserva lo más nuevo.
        const auto keep = std::min(frames, static_cast<std::size_t>(targetFrames_ + blockFrames_) + kHalfTaps);
        src += (frames - keep) * 2;
        frames = keep;
        std::fill(buffer_.begin(), buffer_.begin() + kHalfTaps * 2, 0.0f);
        count_ = kHalfTaps;
        pos_ = kHalfTaps - 1;
        ++drops_;
        if (state_ == State::running)
        {
            ++cuts_;
            gain_ = 0;
            fadingIn_ = true;
        }
    }
    std::memcpy(buffer_.data() + count_ * 2, src, frames * 2 * sizeof(float));
    count_ += frames;
}

void ClockBridge::measure(std::uint32_t frames) noexcept
{
    // El colchón sube un bloque cuando llega uno y baja con cada pedido. Lo que protege de un vacío es su
    // mínimo en los momentos de pedido: un seguidor que baja al instante y sube con una constante de
    // 2 s lo mide sin depender de la fase (ni de la frecuencia de batido) entre los dos ritmos.
    const double fill = fillFrames();
    if (windowMin_ > 1e299 || fill < windowMin_)
        windowMin_ = fill;
    else
        windowMin_ += (fill - windowMin_) * std::min(1.0, frames / config_.outputRate / kReleaseSeconds);
    control(frames);
}

void ClockBridge::control(std::uint32_t frames) noexcept
{
    if (state_ != State::running)
        return;
    const double dt = frames / config_.outputRate;
    const double error = (windowMin_ - (targetFrames_ + kHalfTaps)) / config_.inputRate; // segundos
    if (!haveError_)
    {
        smoothedError_ = smoothedError2_ = error;
        haveError_ = true;
    }
    // Tres polos de 0,5 s (dos sobre el error, uno sobre la razón): lo que quede del batido entre bloques
    // y pedidos no llega a modular el tono. El lazo PI es mucho más lento, así que sigue estable.
    const double a = std::min(1.0, dt / kSmoothSeconds);
    smoothedError_ += a * (error - smoothedError_);
    smoothedError2_ += a * (smoothedError_ - smoothedError2_);
    integral_ = std::clamp(integral_ + kIntegral * smoothedError2_ * dt, -kMaxIntegral, kMaxIntegral);
    const double wanted = std::clamp(kProportional * smoothedError2_ + integral_, -kMaxAdjust, kMaxAdjust);
    adjust_ += a * (wanted - adjust_);
}

void ClockBridge::render(float *out, std::uint32_t frames, double step) noexcept
{
    constexpr int taps = 2 * kHalfTaps;
    for (std::uint32_t j = 0; j < frames; ++j)
    {
        const double base = std::floor(pos_);
        const double frac = pos_ - base;
        const double row = frac * kPhases;
        const int p = std::min(static_cast<int>(row), kPhases - 1);
        const float t = static_cast<float>(row - p);
        const float *c0 = table_.data() + static_cast<std::size_t>(p) * taps;
        const float *c1 = c0 + taps;
        const float *x = buffer_.data() + (static_cast<std::size_t>(base) - (kHalfTaps - 1)) * 2;
        float l = 0, r = 0;
        for (int k = 0; k < taps; ++k)
        {
            const float c = c0[k] + t * (c1[k] - c0[k]);
            l += x[2 * k] * c;
            r += x[2 * k + 1] * c;
        }
        float g = 1.0f;
        if (fadingIn_)
        {
            gain_ = std::min(1.0, gain_ + gainStep_);
            g = static_cast<float>(gain_);
            fadingIn_ = gain_ < 1.0;
        }
        out[2 * j] = l * g;
        out[2 * j + 1] = r * g;
        pos_ += step;
    }
}

void ClockBridge::pull(std::span<float> out) noexcept
{
    const auto frames = static_cast<std::uint32_t>(out.size() / 2);
    if (!prepared() || frames == 0)
    {
        std::fill(out.begin(), out.end(), 0.0f);
        return;
    }
    if (state_ == State::buffering)
    {
        const double start = targetFrames_ + kHalfTaps + std::max(blockFrames_, frames * nominal_);
        if (fillFrames() >= start)
        {
            // Arranca con el colchón justo (después de este pedido queda el margen): lo que sobre se salta.
            const double excess = fillFrames() - start;
            if (excess > 0)
                pos_ += std::floor(excess);
            compact();
            if (pendingCut_ && inputActive_ && silentOutput_ < static_cast<std::uint64_t>(config_.outputRate))
                ++cuts_;
            pendingCut_ = false;
            state_ = State::running;
            gain_ = 0;
            fadingIn_ = true;
            haveError_ = false;
            windowMin_ = 1e300;
        }
        else
        {
            std::fill(out.begin(), out.end(), 0.0f);
            silentOutput_ += frames;
            if (!inputActive_)
                pendingCut_ = false;
            return;
        }
    }
    const double step = nominal_ * (1.0 + adjust_);
    // Cuántas salidas se pueden calcular con lo que hay (el núcleo mira kHalfTaps cuadros adelante).
    const double room = static_cast<double>(count_) - 1.0 - kHalfTaps - pos_;
    const auto possible = room < 0 ? 0u : static_cast<std::uint32_t>(std::min<double>(frames, std::floor(room / step) + 1));
    if (possible >= frames)
    {
        render(out.data(), frames, step);
        if (fillFrames() > maxFrames_)
        {
            // Sobra colchón (la fuente se adelantó de golpe): fundido de salida, salto y fundido de entrada.
            const auto f = std::min<std::uint32_t>(frames, static_cast<std::uint32_t>(fadeOut_));
            for (std::uint32_t i = 0; i < f; ++i)
            {
                const auto idx = frames - f + i;
                const float g = static_cast<float>(f - i) / static_cast<float>(f + 1);
                out[2 * idx] *= g;
                out[2 * idx + 1] *= g;
            }
            pos_ += std::floor(fillFrames() - (targetFrames_ + blockFrames_ + kHalfTaps));
            gain_ = 0;
            windowMin_ = 1e300;
            fadingIn_ = true;
            haveError_ = false;
            integral_ = 0;
            ++drops_;
            ++cuts_;
        }
    }
    else
    {
        // Hueco: se entrega lo que hay con un fundido de salida y se vuelve a juntar colchón.
        render(out.data(), possible, step);
        const auto f = std::min<std::uint32_t>(possible, static_cast<std::uint32_t>(fadeOut_));
        for (std::uint32_t i = 0; i < f; ++i)
        {
            const auto idx = possible - f + i;
            const float g = static_cast<float>(f - i) / static_cast<float>(f + 1);
            out[2 * idx] *= g;
            out[2 * idx + 1] *= g;
        }
        std::fill(out.begin() + possible * 2, out.end(), 0.0f);
        state_ = State::buffering;
        pendingCut_ = inputActive_;
        silentOutput_ = frames - possible;
    }
    if (state_ == State::running)
        measure(frames);
    if (std::floor(pos_) > static_cast<double>(capacity_ / 4))
        compact();
}

ClockBridge::Stats ClockBridge::stats() const noexcept
{
    Stats s;
    s.state = state_;
    s.fillMs = std::max(0.0, fillFrames() * 1000.0 / config_.inputRate); // latencia real del colchón
    s.ratioPpm = adjust_ * 1e6;
    s.cuts = cuts_;
    s.drops = drops_;
    return s;
}
} // namespace zlink
