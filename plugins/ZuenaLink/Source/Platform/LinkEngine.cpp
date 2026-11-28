#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

#include "LinkEngine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace zlink
{
namespace
{
constexpr std::uint64_t kAliveMs = 250; // sin bloques del DAW durante este tiempo = no hay audio en vivo

std::uint64_t makeToken()
{
    std::random_device rd;
    const std::uint64_t r = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
    return (r ^ (static_cast<std::uint64_t>(currentPid()) << 17) ^ nowMs()) | 1u; // nunca 0
}

void raisePeak(std::atomic<float> &slot, float v) noexcept
{
    float current = slot.load(std::memory_order_relaxed);
    while (v > current && !slot.compare_exchange_weak(current, v, std::memory_order_relaxed))
    {
    }
}
} // namespace

LinkEngine::LinkEngine()
    : localData_(std::size_t{kLocalCapacity} * kRingChannels, 0.0f),
      localWriter_(&localCursor_, localData_.data(), kLocalCapacity), token_(makeToken())
{
    recordSettings_.folder = defaultRecordingFolder();
    service_ = std::thread([this] { serviceLoop(); });
}

LinkEngine::~LinkEngine()
{
    quit_.store(true);
    if (service_.joinable())
        service_.join();
}

void LinkEngine::prepare(double sampleRate, int maxBlock)
{
    rate_.store(static_cast<std::uint32_t>(std::lround(std::clamp(sampleRate, 8000.0, 768000.0))), std::memory_order_release);
    block_.store(static_cast<std::uint32_t>(std::max(maxBlock, 0)), std::memory_order_release);
}

void LinkEngine::processBypassed() noexcept
{
    liveInput_.store(false, std::memory_order_relaxed);
    rtTicks_.fetch_add(1, std::memory_order_relaxed);
}

void LinkEngine::process(std::span<const float> left, std::span<const float> right, const LinkControls &c,
                         bool offline) noexcept
{
    obsWanted_.store(c.obs, std::memory_order_relaxed);
    deviceWanted_.store(c.device, std::memory_order_relaxed);
    desiredChannel_.store(c.channel % kLinkChannels, std::memory_order_relaxed);
    mono_.store(c.mono, std::memory_order_relaxed);
    offline_.store(offline, std::memory_order_relaxed);
    rtTicks_.fetch_add(1, std::memory_order_relaxed);
    if (offline || left.empty())
    {
        liveInput_.store(false, std::memory_order_relaxed);
        return;
    }
    liveInput_.store(true, std::memory_order_relaxed);
    const auto frames = left.size();
    // El bloque más grande visto define el colchón mínimo de los lectores.
    if (frames > block_.load(std::memory_order_relaxed))
        block_.store(static_cast<std::uint32_t>(frames), std::memory_order_relaxed);

    const float target = std::pow(10.0f, std::clamp(c.sendDb, -60.0f, 12.0f) / 20.0f);
    const bool stereo = right.size() == frames;
    float pl = 0, pr = 0;
    for (std::size_t start = 0; start < frames; start += kMaxChunk)
    {
        const auto n = std::min(kMaxChunk, frames - start);
        // Rampa lineal de ganancia por tramo: un cambio de nivel nunca produce un escalón.
        const float step = (target - gain_) / static_cast<float>(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            gain_ += step;
            // Un NaN o un infinito de otro plugin no puede llegar a OBS ni al archivo.
            const float l = std::isfinite(left[start + i]) ? left[start + i] : 0.0f;
            const float r = stereo ? (std::isfinite(right[start + i]) ? right[start + i] : 0.0f) : l;
            const float a = c.mono ? 0.5f * (l + r) : l;
            const float b = c.mono ? a : r;
            scratchL_[i] = a * gain_;
            scratchR_[i] = b * gain_;
            pl = std::max(pl, std::abs(scratchL_[i]));
            pr = std::max(pr, std::abs(scratchR_[i]));
        }
        gain_ = target;
        const std::span<const float> l{scratchL_.data(), n}, r{scratchR_.data(), n};
        localWriter_.write(l, r);
        if (c.obs)
        {
            // Orden seq_cst: o el servicio ve rtBusy_ y espera, o nosotros vemos el canal ya retirado.
            rtBusy_.store(1, std::memory_order_seq_cst);
            if (SharedChannel *ch = active_.load(std::memory_order_seq_cst))
            {
                RingWriter w = ch->writer();
                w.write(l, r);
            }
            rtBusy_.store(0, std::memory_order_release);
        }
    }
    raisePeak(peak_[0], pl);
    raisePeak(peak_[1], pr);
}

float LinkEngine::takePeak(int channel) noexcept
{
    return peak_[channel != 0 ? 1 : 0].exchange(0.0f, std::memory_order_relaxed);
}

SharedChannel *LinkEngine::channel(std::uint32_t index)
{
    auto &slot = channels_[index % kLinkChannels];
    if (!slot)
    {
        auto ch = std::make_unique<SharedChannel>();
        if (!ch->open(index % kLinkChannels))
            return nullptr;
        slot = std::move(ch);
    }
    return slot.get();
}

void LinkEngine::deactivate()
{
    if (owned_ < 0)
        return;
    active_.store(nullptr, std::memory_order_seq_cst);
    // Espera a que el hilo de audio termine la copia en curso (unos microsegundos como mucho).
    for (int i = 0; i < 2000 && rtBusy_.load(std::memory_order_seq_cst) != 0; ++i)
        std::this_thread::yield();
    if (auto *ch = channels_[static_cast<std::size_t>(owned_)].get())
        releaseChannel(*ch->header(), token_);
    owned_ = -1;
}

void LinkEngine::tickObs(std::uint64_t now, std::uint64_t ticks, bool alive)
{
    const bool offline = offline_.load(std::memory_order_relaxed);
    const bool want = obsWanted_.load(std::memory_order_relaxed);
    const auto desired = desiredChannel_.load(std::memory_order_relaxed);
    LinkSnapshot s;
    s.channel = desired;

    if (!want || (owned_ >= 0 && static_cast<std::uint32_t>(owned_) != desired))
        deactivate();

    // Qué canales usa otra instancia (cada medio segundo alcanza).
    if (ticks % 50 == 0 || ticks < 2)
        for (std::uint32_t i = 0; i < kLinkChannels; ++i)
            if (auto *ch = channel(i))
                s.channelInUse[i] = !channelAvailable(*ch->header(), token_, now);
    {
        std::scoped_lock lock(mutex_);
        if (!(ticks % 50 == 0 || ticks < 2))
            s.channelInUse = shared_.channelInUse;
    }

    if (!want)
        s.obs = ObsState::off;
    else if (SharedChannel *ch = channel(desired); ch == nullptr)
        s.obs = ObsState::failed;
    else
    {
        SharedHeader &h = *ch->header();
        const auto claim = claimChannel(h, token_, currentPid(), now);
        if (claim == Claim::busy)
        {
            if (owned_ == static_cast<int>(desired))
                deactivate(); // otra instancia lo tomó (esta estuvo congelada más de un segundo)
            s.obs = ObsState::busy;
            std::array<char, kNameBytes> name{};
            readOwnerName(h, name);
            s.otherOwner = name.data();
        }
        else
        {
            const auto rate = rate_.load(std::memory_order_acquire);
            if (claim == Claim::acquired)
            {
                writeOwnerName(h, hostDisplayName());
                h.sampleRate.store(rate, std::memory_order_release);
                h.blockFrames.store(block_.load(std::memory_order_relaxed), std::memory_order_release);
                owned_ = static_cast<int>(desired);
                active_.store(ch, std::memory_order_seq_cst);
            }
            if (h.sampleRate.load(std::memory_order_relaxed) != rate)
            {
                h.sampleRate.store(rate, std::memory_order_release);
                h.epoch.fetch_add(1, std::memory_order_acq_rel);
            }
            h.blockFrames.store(block_.load(std::memory_order_relaxed), std::memory_order_release);
            const bool live = alive && !offline && liveInput_.load(std::memory_order_relaxed);
            h.streaming.store(live ? 1u : 0u, std::memory_order_release);
            s.obsContender = hasContender(h, token_, now);
            if (offline)
                s.obs = ObsState::exporting;
            else if (readerAlive(h, now))
            {
                s.obs = ObsState::connected;
                s.obsLatencyMs = h.readerLatencyUs.load(std::memory_order_relaxed) / 1000.0;
                s.obsCuts = h.readerCuts.load(std::memory_order_relaxed);
            }
            else
                s.obs = ObsState::waiting;
        }
    }
    std::scoped_lock lock(mutex_);
    shared_.obs = s.obs;
    shared_.channel = s.channel;
    shared_.obsLatencyMs = s.obsLatencyMs;
    shared_.obsCuts = s.obsCuts;
    shared_.obsContender = s.obsContender;
    shared_.otherOwner = s.otherOwner;
    shared_.channelInUse = s.channelInUse;
    shared_.offline = offline;
    shared_.sampleRate = rate_.load(std::memory_order_relaxed);
}

void LinkEngine::tickDevice()
{
    DeviceSettings settings;
    {
        std::scoped_lock lock(mutex_);
        settings = deviceSettings_;
    }
    const bool want = deviceWanted_.load(std::memory_order_relaxed) && !settings.id.empty();
    if (!want)
    {
        if (output_.running())
            output_.stop();
    }
    else
    {
        const auto &r = output_.request();
        if (!output_.running() || r.deviceId != settings.id || r.exclusive != settings.exclusive || r.pair != settings.pair)
        {
            WasapiOutput::Request req;
            req.deviceId = settings.id;
            req.exclusive = settings.exclusive;
            req.pair = settings.pair;
            output_.start(req, {&localCursor_, localData_.data(), kLocalCapacity, &rate_, &block_, &liveInput_});
        }
    }
    std::scoped_lock lock(mutex_);
    shared_.deviceOn = want;
    shared_.device = output_.status();
    shared_.deviceSettings = settings;
    shared_.deviceMissing = want && std::none_of(shared_.endpoints.begin(), shared_.endpoints.end(),
                                                 [&](const Endpoint &e) { return e.id == settings.id; });
}

void LinkEngine::tickRecorder()
{
    const auto rate = rate_.load(std::memory_order_acquire);
    if (recordStop_.exchange(false))
        recorder_.stop();
    if (recordStart_.exchange(false) && !recorder_.recording())
    {
        RecordSettings settings;
        {
            std::scoped_lock lock(mutex_);
            settings = recordSettings_;
        }
        WavLayout layout{rate, static_cast<std::uint16_t>(mono_.load() ? 1 : 2), settings.format};
        recorder_.start(settings.folder, layout, &localCursor_, localData_.data(), kLocalCapacity);
        recordRate_ = rate;
    }
    if (recorder_.recording())
    {
        if (offline_.load(std::memory_order_relaxed))
            recorder_.stop("Se detuvo: el DAW empezó a exportar.");
        else if (rate != recordRate_)
            recorder_.stop("Se detuvo: cambió la frecuencia del proyecto.");
        else
            recorder_.drain();
    }
    std::scoped_lock lock(mutex_);
    shared_.rec = recorder_.status();
    shared_.recordSettings = recordSettings_;
}

void LinkEngine::serviceLoop()
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        DeviceWatcher watcher;
        std::uint32_t seen = watcher.generation() + 1;
        std::uint64_t ticks = 0, lastRt = 0, lastRtMs = 0;
        while (!quit_.load())
        {
            const auto now = nowMs();
            const auto rt = rtTicks_.load(std::memory_order_relaxed);
            if (rt != lastRt)
            {
                lastRt = rt;
                lastRtMs = now;
            }
            const bool alive = now - lastRtMs < kAliveMs;
            if (watcher.generation() != seen || refresh_.exchange(false))
            {
                seen = watcher.generation();
                auto list = listRenderEndpoints();
                std::scoped_lock lock(mutex_);
                shared_.endpoints = std::move(list);
                ++shared_.endpointsVersion;
            }
            tickObs(now, ticks, alive);
            tickDevice();
            tickRecorder();
            ++ticks;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        deactivate();
        output_.stop();
        recorder_.stop();
    }
    if (SUCCEEDED(com))
        CoUninitialize();
}

void LinkEngine::setDevice(const DeviceSettings &d)
{
    std::scoped_lock lock(mutex_);
    deviceSettings_ = d;
}
DeviceSettings LinkEngine::device() const
{
    std::scoped_lock lock(mutex_);
    return deviceSettings_;
}
void LinkEngine::setRecording(const RecordSettings &r)
{
    std::scoped_lock lock(mutex_);
    recordSettings_ = r;
    if (recordSettings_.folder.empty())
        recordSettings_.folder = defaultRecordingFolder();
}
RecordSettings LinkEngine::recording() const
{
    std::scoped_lock lock(mutex_);
    return recordSettings_;
}
void LinkEngine::startRecording()
{
    recordStart_.store(true);
}
void LinkEngine::stopRecording()
{
    recordStop_.store(true);
}
void LinkEngine::refreshDevices()
{
    refresh_.store(true);
}
LinkSnapshot LinkEngine::snapshot() const
{
    std::scoped_lock lock(mutex_);
    return shared_;
}
} // namespace zlink
