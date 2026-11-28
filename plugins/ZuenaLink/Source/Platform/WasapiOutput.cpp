#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include "WasapiOutput.h"
#include "../Core/ClockBridge.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace zlink
{
namespace
{
// GUID de subformato de WAVEFORMATEXTENSIBLE (ksmedia.h), definidos acá para no depender de INITGUID.
constexpr GUID kSubtypePcm{0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID kSubtypeFloat{0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

template <typename T> struct Com
{
    T *p = nullptr;
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    ~Com() { reset(); }
    void reset() noexcept
    {
        if (p != nullptr)
            p->Release();
        p = nullptr;
    }
    T **out() noexcept
    {
        reset();
        return &p;
    }
    T *operator->() const noexcept { return p; }
    explicit operator bool() const noexcept { return p != nullptr; }
};

enum class Sample : std::uint8_t
{
    f32,
    i16,
    i24,
    i32
};

struct DeviceFormat
{
    Sample sample = Sample::f32;
    std::uint32_t channels = 2, rate = 48000, bytes = 4;
};

bool describe(const WAVEFORMATEX *f, DeviceFormat &out)
{
    if (f == nullptr)
        return false;
    out.channels = f->nChannels;
    out.rate = f->nSamplesPerSec;
    out.bytes = f->wBitsPerSample / 8;
    bool isFloat = f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool isPcm = f->wFormatTag == WAVE_FORMAT_PCM;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22)
    {
        const auto *x = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(f); // diseño documentado de WASAPI
        isFloat = x->SubFormat == kSubtypeFloat;
        isPcm = x->SubFormat == kSubtypePcm;
    }
    if (isFloat && out.bytes == 4)
        out.sample = Sample::f32;
    else if (isPcm && out.bytes == 2)
        out.sample = Sample::i16;
    else if (isPcm && out.bytes == 3)
        out.sample = Sample::i24;
    else if (isPcm && out.bytes == 4)
        out.sample = Sample::i32;
    else
        return false;
    return out.channels > 0;
}

WAVEFORMATEXTENSIBLE makeFormat(std::uint32_t rate, std::uint32_t channels, Sample s)
{
    WAVEFORMATEXTENSIBLE x{};
    const WORD bits = s == Sample::i16 ? 16 : (s == Sample::i24 ? 24 : 32);
    x.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    x.Format.nChannels = static_cast<WORD>(channels);
    x.Format.nSamplesPerSec = rate;
    x.Format.wBitsPerSample = bits;
    x.Format.nBlockAlign = static_cast<WORD>(channels * bits / 8);
    x.Format.nAvgBytesPerSec = rate * x.Format.nBlockAlign;
    x.Format.cbSize = 22;
    x.Samples.wValidBitsPerSample = s == Sample::i32 ? 24 : bits;
    x.dwChannelMask = channels == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0;
    x.SubFormat = s == Sample::f32 ? kSubtypeFloat : kSubtypePcm;
    return x;
}

std::string describeError(HRESULT hr)
{
    switch (hr)
    {
    case AUDCLNT_E_DEVICE_IN_USE:
        return "Otro programa usa el dispositivo en modo exclusivo. Probá en modo compartido.";
    case AUDCLNT_E_DEVICE_INVALIDATED:
        return "El dispositivo se desconectó o cambió de formato.";
    case AUDCLNT_E_UNSUPPORTED_FORMAT:
        return "El dispositivo no acepta ningún formato compatible en modo exclusivo.";
    case AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED:
        return "Windows no permite el modo exclusivo en este dispositivo (Propiedades → Opciones avanzadas).";
    case AUDCLNT_E_SERVICE_NOT_RUNNING:
        return "El servicio de audio de Windows no está funcionando.";
    case E_NOTFOUND:
        return "No se encuentra el dispositivo. ¿Está conectado?";
    default:
        break;
    }
    char text[96];
    std::snprintf(text, sizeof(text), "Error de audio de Windows (0x%08lX).", static_cast<unsigned long>(hr));
    return text;
}

void writeFrames(const float *src, std::uint32_t frames, BYTE *dst, const DeviceFormat &f, std::uint32_t pair)
{
    const std::uint32_t left = f.channels >= 2 ? std::min(pair * 2, f.channels - 2) : 0;
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        const float l = src[2 * i], r = src[2 * i + 1];
        for (std::uint32_t c = 0; c < f.channels; ++c)
        {
            float v = 0.0f;
            if (f.channels == 1)
                v = 0.5f * (l + r);
            else if (c == left)
                v = l;
            else if (c == left + 1)
                v = r;
            v = std::clamp(v, -1.0f, 1.0f);
            switch (f.sample)
            {
            case Sample::f32:
                std::memcpy(dst, &v, 4);
                break;
            case Sample::i16:
            {
                const auto q = static_cast<std::int16_t>(std::lrint(v * 32767.0f));
                std::memcpy(dst, &q, 2);
                break;
            }
            case Sample::i24:
            {
                const auto q = static_cast<std::int32_t>(std::lrint(v * 8388607.0f));
                dst[0] = static_cast<BYTE>(q);
                dst[1] = static_cast<BYTE>(q >> 8);
                dst[2] = static_cast<BYTE>(q >> 16);
                break;
            }
            case Sample::i32:
            {
                const auto q = static_cast<std::int32_t>(std::lrint(static_cast<double>(v) * 2147483392.0));
                std::memcpy(dst, &q, 4);
                break;
            }
            }
            dst += f.bytes;
        }
    }
}
} // namespace

WasapiOutput::~WasapiOutput()
{
    stop();
}

void WasapiOutput::start(const Request &r, const Source &s)
{
    stop();
    request_ = r;
    source_ = s;
    cuts_.store(0);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    setPhase(Phase::opening);
    thread_ = std::thread([this] { run(); });
}

void WasapiOutput::stop()
{
    if (thread_.joinable())
    {
        SetEvent(stopEvent_);
        thread_.join();
    }
    if (stopEvent_ != nullptr)
        CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
    setPhase(Phase::stopped);
}

void WasapiOutput::setPhase(Phase p, const std::string &error)
{
    std::scoped_lock lock(mutex_);
    status_.phase = p;
    if (!error.empty() || p == Phase::running)
        status_.error = error;
    phase_.store(static_cast<std::uint8_t>(p));
}

WasapiOutput::Status WasapiOutput::status() const
{
    std::scoped_lock lock(mutex_);
    auto s = status_;
    s.cuts = cuts_.load(std::memory_order_relaxed);
    s.latencyMs = latencyMs_.load(std::memory_order_relaxed);
    return s;
}

void WasapiOutput::run()
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    DWORD backoff = 250;
    while (true)
    {
        if (session())
            break;
        setPhase(Phase::retrying);
        if (WaitForSingleObject(stopEvent_, backoff) == WAIT_OBJECT_0)
            break;
        backoff = std::min<DWORD>(backoff * 2, 4000);
    }
    if (task != nullptr)
        AvRevertMmThreadCharacteristics(task);
    if (SUCCEEDED(com))
        CoUninitialize();
}

bool WasapiOutput::session()
{
    Com<IMMDeviceEnumerator> en;
    Com<IMMDevice> device;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void **>(en.out())); // firma de COM
    if (SUCCEEDED(hr))
        hr = en->GetDevice(request_.deviceId.c_str(), device.out());
    if (FAILED(hr))
    {
        setPhase(Phase::retrying, describeError(hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) ? E_NOTFOUND : hr));
        return false;
    }

    Com<IAudioClient> client;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(client.out()));
    if (FAILED(hr))
    {
        setPhase(Phase::retrying, describeError(hr));
        return false;
    }

    WAVEFORMATEX *mix = nullptr;
    hr = client->GetMixFormat(&mix);
    DeviceFormat fmt;
    if (FAILED(hr) || !describe(mix, fmt))
    {
        if (mix != nullptr)
            CoTaskMemFree(mix);
        setPhase(Phase::retrying, "El dispositivo informa un formato que no se reconoce.");
        return false;
    }

    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    bool lowLatency = false;
    double periodMs = 10;
    WAVEFORMATEXTENSIBLE chosen{};
    if (request_.exclusive)
    {
        // El formato nativo más fino que acepte la placa, a la frecuencia del mezclador de Windows.
        hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
        for (const auto s : {Sample::f32, Sample::i32, Sample::i24, Sample::i16})
        {
            chosen = makeFormat(fmt.rate, fmt.channels, s);
            if (client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &chosen.Format, nullptr) == S_OK)
            {
                hr = S_OK;
                break;
            }
        }
        REFERENCE_TIME def = 0, minimum = 0;
        if (SUCCEEDED(hr))
            hr = client->GetDevicePeriod(&def, &minimum);
        if (SUCCEEDED(hr))
        {
            hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minimum, minimum,
                                    &chosen.Format, nullptr);
            if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
            {
                UINT32 frames = 0;
                client->GetBufferSize(&frames);
                const auto aligned = static_cast<REFERENCE_TIME>(10000.0 * 1000 * frames / fmt.rate + 0.5);
                hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(client.out()));
                if (SUCCEEDED(hr))
                    hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, aligned,
                                            aligned, &chosen.Format, nullptr);
            }
            if (SUCCEEDED(hr))
                describe(&chosen.Format, fmt);
        }
    }
    else
    {
        Com<IAudioClient3> client3;
        if (SUCCEEDED(client->QueryInterface(__uuidof(IAudioClient3), reinterpret_cast<void **>(client3.out()))))
        {
            UINT32 def = 0, fundamental = 0, minimum = 0, maximum = 0;
            if (SUCCEEDED(client3->GetSharedModeEnginePeriod(mix, &def, &fundamental, &minimum, &maximum)) &&
                SUCCEEDED(client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minimum, mix, nullptr)))
            {
                lowLatency = true;
                periodMs = 1000.0 * minimum / fmt.rate;
            }
        }
        if (!lowLatency)
        {
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, mix, nullptr);
            REFERENCE_TIME def = 0, minimum = 0;
            if (SUCCEEDED(hr) && SUCCEEDED(client->GetDevicePeriod(&def, &minimum)))
                periodMs = def / 10000.0;
        }
        else
            hr = S_OK;
    }
    CoTaskMemFree(mix);

    UINT32 bufferFrames = 0;
    Com<IAudioRenderClient> render;
    if (SUCCEEDED(hr))
        hr = client->SetEventHandle(event);
    if (SUCCEEDED(hr))
        hr = client->GetBufferSize(&bufferFrames);
    if (SUCCEEDED(hr))
        hr = client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void **>(render.out()));
    if (FAILED(hr) || bufferFrames == 0)
    {
        CloseHandle(event);
        setPhase(Phase::retrying, describeError(FAILED(hr) ? hr : E_FAIL));
        return false;
    }
    if (request_.exclusive)
        periodMs = 1000.0 * bufferFrames / fmt.rate;

    // Todo lo que se reserva, se reserva acá, antes de arrancar.
    ClockBridge bridge;
    std::uint32_t sourceRate = source_.sampleRate->load(std::memory_order_acquire);
    std::uint32_t sourceBlock = source_.blockFrames->load(std::memory_order_acquire);
    const auto prepareBridge = [&]
    {
        const double blockMs = sourceRate > 0 ? 1000.0 * sourceBlock / sourceRate : 0.0;
        bridge.prepare({static_cast<double>(std::max<std::uint32_t>(sourceRate, 8000)), static_cast<double>(fmt.rate),
                        request_.targetMs + periodMs, 600, blockMs});
    };
    prepareBridge();
    RingReader reader(source_.cursor, source_.data, source_.capacity);
    std::vector<float> chunk(4096 * 2), out(static_cast<std::size_t>(bufferFrames) * 2);

    {
        std::scoped_lock lock(mutex_);
        status_.deviceRate = fmt.rate;
        status_.deviceChannels = fmt.channels;
        status_.periodMs = periodMs;
        status_.exclusive = request_.exclusive;
        status_.lowLatencyShared = lowLatency;
    }

    BYTE *data = nullptr;
    if (SUCCEEDED(render->GetBuffer(bufferFrames, &data)))
        render->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
    hr = client->Start();
    if (FAILED(hr))
    {
        CloseHandle(event);
        setPhase(Phase::retrying, describeError(hr));
        return false;
    }
    setPhase(Phase::running);

    std::uint32_t baseCuts = cuts_.load();
    bool stopRequested = false;
    HANDLE waits[2] = {stopEvent_, event};
    while (true)
    {
        const DWORD w = WaitForMultipleObjects(2, waits, FALSE, 500);
        if (w == WAIT_OBJECT_0)
        {
            stopRequested = true;
            break;
        }
        if (w == WAIT_TIMEOUT)
        {
            hr = AUDCLNT_E_DEVICE_INVALIDATED; // el dispositivo dejó de pedir audio
            break;
        }
        const auto rate = source_.sampleRate->load(std::memory_order_acquire);
        const auto block = source_.blockFrames->load(std::memory_order_acquire);
        if (rate != sourceRate || block > sourceBlock)
        {
            // Cambió la frecuencia del proyecto o el DAW manda bloques más grandes: nuevo colchón.
            sourceRate = rate;
            sourceBlock = block;
            baseCuts += bridge.stats().cuts;
            prepareBridge();
            reader.syncToLatest();
        }
        UINT32 frames = bufferFrames;
        if (!request_.exclusive)
        {
            UINT32 padding = 0;
            hr = client->GetCurrentPadding(&padding);
            if (FAILED(hr))
                break;
            frames = bufferFrames - padding;
        }
        if (frames == 0)
            continue;
        for (int guard = 0; guard < 64; ++guard)
        {
            const auto r = reader.read(chunk);
            if (r.frames > 0)
                bridge.push({chunk.data(), static_cast<std::size_t>(r.frames) * 2});
            if (r.frames < chunk.size() / 2)
                break;
        }
        bridge.setInputActive(source_.active->load(std::memory_order_relaxed));
        bridge.pull({out.data(), static_cast<std::size_t>(frames) * 2});
        hr = render->GetBuffer(frames, &data);
        if (FAILED(hr))
            break;
        writeFrames(out.data(), frames, data, fmt, request_.pair);
        hr = render->ReleaseBuffer(frames, 0);
        if (FAILED(hr))
            break;
        const auto st = bridge.stats();
        cuts_.store(baseCuts + st.cuts, std::memory_order_relaxed);
        latencyMs_.store(st.fillMs + periodMs * (request_.exclusive ? 1.0 : 2.0), std::memory_order_relaxed);
    }
    client->Stop();
    CloseHandle(event);
    if (stopRequested)
        return true;
    setPhase(Phase::retrying, describeError(hr));
    return false;
}
} // namespace zlink
