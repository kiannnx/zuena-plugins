#pragma once
// Cola circular de un productor y un consumidor sin bloqueos. Tamaño fijo reservado en el
// constructor (nunca se realoja), así el hilo de audio y la UI no compiten por la memoria.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace zeq
{
class SpscRing
{
public:
    explicit SpscRing (std::size_t capacityPow2 = 1u << 15) : buffer (capacityPow2), mask (capacityPow2 - 1) {}

    /** Hilo de audio. Si no cabe, descarta lo que sobra y lo cuenta (nunca bloquea). */
    void push (const float* data, std::size_t n) noexcept
    {
        const std::size_t w = writeIndex.load (std::memory_order_relaxed);
        const std::size_t r = readIndex.load (std::memory_order_acquire);
        const std::size_t free = buffer.size() - (w - r);
        const std::size_t count = n < free ? n : free;
        for (std::size_t i = 0; i < count; ++i)
            buffer[(w + i) & mask] = data[i];
        writeIndex.store (w + count, std::memory_order_release);
        if (count < n)
            dropped.fetch_add (n - count, std::memory_order_relaxed);
    }

    /** Hilo consumidor (UI). Devuelve cuántas muestras copió. */
    std::size_t pop (float* out, std::size_t maxCount) noexcept
    {
        const std::size_t r = readIndex.load (std::memory_order_relaxed);
        const std::size_t w = writeIndex.load (std::memory_order_acquire);
        const std::size_t available = w - r;
        const std::size_t count = available < maxCount ? available : maxCount;
        for (std::size_t i = 0; i < count; ++i)
            out[i] = buffer[(r + i) & mask];
        readIndex.store (r + count, std::memory_order_release);
        return count;
    }

    std::uint64_t droppedSamples() const noexcept { return dropped.load (std::memory_order_relaxed); }

private:
    std::vector<float> buffer;
    std::size_t mask;
    std::atomic<std::size_t> writeIndex { 0 }, readIndex { 0 };
    std::atomic<std::uint64_t> dropped { 0 };
};
} // namespace zeq
