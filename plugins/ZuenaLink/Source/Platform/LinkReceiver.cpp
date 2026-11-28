#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <avrt.h>

#include "LinkReceiver.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace zlink
{
namespace
{
constexpr double kChunkMs = 2.0;          // cada cuánto se entrega audio
constexpr std::uint32_t kMaxLate = 48000; // si el hilo se congeló más de esto (cuadros), se reinicia el reloj
} // namespace

LinkReceiver::~LinkReceiver()
{
    stop();
}

std::uint64_t LinkReceiver::systemClockNs() noexcept
{
    static const double toNs = []
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1e9 / static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<std::uint64_t>(static_cast<double>(c.QuadPart) * toNs);
}

void LinkReceiver::start(std::uint32_t channel, Buffer buffer, std::uint32_t outputRate, Clock clock, Output output)
{
    stop();
    channel_ = channel % kLinkChannels;
    buffer_ = buffer;
    outputRate_ = std::clamp<std::uint32_t>(outputRate, 8000, 384000);
    clock_ = clock != nullptr ? clock : &LinkReceiver::systemClockNs;
    output_ = std::move(output);
    quit_.store(false);
    thread_ = std::thread([this] { run(); });
}

void LinkReceiver::stop()
{
    quit_.store(true);
    if (thread_.joinable())
        thread_.join();
}

LinkReceiver::Status LinkReceiver::status() const
{
    std::scoped_lock lock(mutex_);
    return status_;
}

void LinkReceiver::run()
{
    DWORD taskIndex = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer == nullptr)
        timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);

    SharedChannel ch;
    ClockBridge bridge;
    RingReader reader;
    std::vector<float> chunk(8192 * 2);
    const auto chunkFrames = static_cast<std::uint32_t>(std::lround(outputRate_ * kChunkMs / 1000.0));
    std::vector<float> out(static_cast<std::size_t>(kMaxLate) * 2);
    std::uint32_t epoch = 0, rate = 0, block = 0, baseCuts = 0;
    bool primed = false;
    std::uint64_t startNs = 0, produced = 0;
    std::array<char, kNameBytes> host{};
    std::uint64_t lastName = 0;

    while (!quit_.load(std::memory_order_relaxed))
    {
        if (!ch.isOpen() && !ch.open(channel_))
        {
            {
                std::scoped_lock lock(mutex_);
                status_.phase = Phase::failed;
            }
            Sleep(200);
            continue;
        }
        SharedHeader &h = *ch.header();
        const auto now = nowMs();
        h.readerBeatMs.store(now, std::memory_order_release);
        h.readerPid.store(currentPid(), std::memory_order_relaxed);
        h.readerRate.store(outputRate_, std::memory_order_relaxed);

        const bool owner = ownerAlive(h, now);
        const auto e = h.epoch.load(std::memory_order_acquire);
        const auto r = h.sampleRate.load(std::memory_order_acquire);
        const auto b = h.blockFrames.load(std::memory_order_acquire);
        if (owner && r >= 8000 && (!primed || e != epoch || r != rate || b > block))
        {
            // Dueño nuevo, otra frecuencia o bloques más grandes: colchón nuevo y lectura desde lo último.
            if (primed)
                baseCuts += bridge.stats().cuts;
            epoch = e;
            rate = r;
            block = b;
            const double blockMs = 1000.0 * b / r;
            bridge.prepare({static_cast<double>(r), static_cast<double>(outputRate_), bufferMs(buffer_), 600, blockMs});
            reader = ch.reader();
            primed = true;
        }
        const bool live = owner && primed && h.streaming.load(std::memory_order_acquire) != 0;
        if (primed)
        {
            for (int guard = 0; guard < 64; ++guard)
            {
                const auto got = reader.read(chunk);
                if (got.frames > 0 && live)
                    bridge.push({chunk.data(), static_cast<std::size_t>(got.frames) * 2});
                if (got.frames < chunk.size() / 2)
                    break;
            }
            bridge.setInputActive(live);
        }

        // Cuánto audio corresponde entregar según el reloj de OBS.
        const auto t = clock_();
        if (startNs == 0)
            startNs = t;
        auto due = static_cast<std::uint64_t>(static_cast<double>(t - startNs) * outputRate_ / 1e9);
        if (due > produced + kMaxLate)
        {
            // El sistema estuvo suspendido o el hilo frenado: se retoma desde ahora, sin ráfagas.
            startNs = t;
            produced = 0;
            due = 0;
        }
        while (due >= produced + chunkFrames)
        {
            const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(due - produced, kMaxLate));
            const std::span<float> piece{out.data(), static_cast<std::size_t>(frames) * 2};
            if (primed)
                bridge.pull(piece);
            else
                std::fill(piece.begin(), piece.end(), 0.0f);
            const auto ts = startNs + static_cast<std::uint64_t>(static_cast<double>(produced) * 1e9 / outputRate_);
            if (output_)
                output_(out.data(), frames, ts);
            produced += frames;
        }

        const auto st = bridge.stats();
        const double latency = primed ? st.fillMs + kChunkMs : 0.0;
        h.readerLatencyUs.store(static_cast<std::uint32_t>(latency * 1000.0), std::memory_order_relaxed);
        h.readerCuts.store(baseCuts + st.cuts, std::memory_order_relaxed);
        if (now - lastName > 500)
        {
            lastName = now;
            readOwnerName(h, host);
        }
        {
            std::scoped_lock lock(mutex_);
            status_.phase = !owner ? Phase::idle : (live ? Phase::receiving : Phase::paused);
            status_.host = owner ? host.data() : "";
            status_.sourceRate = rate;
            status_.latencyMs = live ? latency : 0.0;
            status_.cuts = baseCuts + st.cuts;
        }

        if (timer != nullptr)
        {
            LARGE_INTEGER due100ns;
            due100ns.QuadPart = -static_cast<LONGLONG>(kChunkMs * 10000.0 / 2); // medio período
            SetWaitableTimer(timer, &due100ns, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer, 50);
        }
        else
            Sleep(1);
    }
    if (timer != nullptr)
        CloseHandle(timer);
    if (task != nullptr)
        AvRevertMmThreadCharacteristics(task);
}
} // namespace zlink
