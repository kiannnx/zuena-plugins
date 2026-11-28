#pragma once
// Anillo de difusión de audio estéreo intercalado: un escritor (el hilo de audio del DAW) y cualquier
// cantidad de lectores, cada uno con su propia posición. El escritor nunca espera ni sabe quién lee:
// si un lector se atrasa más que la capacidad, lo detecta él mismo (desborde) y se resincroniza.
// Sirve igual en memoria del proceso (dispositivo y grabación) y en memoria compartida (OBS).
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <span>

namespace zlink
{
inline constexpr std::uint32_t kRingChannels = 2;

/** Cantidad total de cuadros escritos. Vive junto a los datos (también en memoria compartida). */
struct RingCursor
{
    std::atomic<std::uint64_t> written{0};
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic_ref<float>::is_always_lock_free);

class RingWriter
{
  public:
    RingWriter() = default;
    RingWriter(RingCursor *cursor, float *storage, std::uint32_t capacityFrames) noexcept
        : cursor_(cursor), data_(storage), mask_(capacityFrames - 1)
    {
    }
    bool valid() const noexcept { return cursor_ != nullptr; }
    std::uint32_t capacity() const noexcept { return static_cast<std::uint32_t>(mask_ + 1); }

    /** Seguro en tiempo real: sin reservas, sin bloqueos. `right` puede venir vacío (se copia `left`). */
    void write(std::span<const float> left, std::span<const float> right) noexcept
    {
        if (!valid())
            return;
        const auto frames = left.size();
        const bool stereo = right.size() == frames;
        std::uint64_t w = cursor_->written.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < frames; ++i, ++w)
        {
            const auto slot = static_cast<std::size_t>(w & mask_) * kRingChannels;
            std::atomic_ref<float>(data_[slot]).store(left[i], std::memory_order_relaxed);
            std::atomic_ref<float>(data_[slot + 1]).store(stereo ? right[i] : left[i], std::memory_order_relaxed);
        }
        cursor_->written.store(w, std::memory_order_release);
    }

  private:
    RingCursor *cursor_ = nullptr;
    float *data_ = nullptr;
    std::uint64_t mask_ = 0;
};

class RingReader
{
  public:
    struct Result
    {
        std::uint32_t frames = 0;
        bool overrun = false; // el lector se atrasó y perdió audio: ya quedó resincronizado
    };

    RingReader() = default;
    RingReader(const RingCursor *cursor, const float *storage, std::uint32_t capacityFrames) noexcept
        : cursor_(cursor), data_(storage), capacity_(capacityFrames)
    {
        syncToLatest();
    }
    bool valid() const noexcept { return cursor_ != nullptr; }

    /** Se para en lo último escrito, dejando `keepFrames` cuadros pendientes de leer. */
    void syncToLatest(std::uint32_t keepFrames = 0) noexcept
    {
        if (!valid())
            return;
        const auto w = cursor_->written.load(std::memory_order_acquire);
        pos_ = w - std::min<std::uint64_t>(w, std::min(keepFrames, capacity_ / 2));
    }
    std::uint64_t available() const noexcept
    {
        return valid() ? cursor_->written.load(std::memory_order_acquire) - pos_ : 0;
    }
    std::uint64_t position() const noexcept { return pos_; }

    /** Copia hasta out.size()/2 cuadros intercalados. Verifica después de copiar que nada se pisó. */
    Result read(std::span<float> out) noexcept
    {
        Result r;
        if (!valid())
            return r;
        const auto w = cursor_->written.load(std::memory_order_acquire);
        if (w - pos_ > capacity_)
        {
            pos_ = w - capacity_ / 4;
            r.overrun = true;
        }
        const auto n = static_cast<std::uint32_t>(std::min<std::uint64_t>(w - pos_, out.size() / kRingChannels));
        const std::uint64_t mask = capacity_ - 1;
        for (std::uint32_t i = 0; i < n; ++i)
        {
            const auto slot = static_cast<std::size_t>((pos_ + i) & mask) * kRingChannels;
            out[i * kRingChannels] = loadSample(slot);
            out[i * kRingChannels + 1] = loadSample(slot + 1);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        const auto after = cursor_->written.load(std::memory_order_acquire);
        if (after - pos_ > capacity_)
        {
            // El escritor dio la vuelta mientras copiábamos: lo copiado no es confiable.
            pos_ = after - capacity_ / 4;
            r.overrun = true;
            r.frames = 0;
            return r;
        }
        pos_ += n;
        r.frames = n;
        return r;
    }

  private:
    // atomic_ref<const T> recién existe en C++26: la memoria es nuestra y se lee sin modificarla.
    float loadSample(std::size_t i) const noexcept
    {
        return std::atomic_ref<float>(const_cast<float &>(data_[i])).load(std::memory_order_relaxed);
    }
    const RingCursor *cursor_ = nullptr;
    const float *data_ = nullptr;
    std::uint32_t capacity_ = 0;
    std::uint64_t pos_ = 0;
};

constexpr bool isRingCapacity(std::uint32_t frames) noexcept
{
    return frames >= 64 && std::has_single_bit(frames);
}
} // namespace zlink
