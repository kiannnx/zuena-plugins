// Pruebas de la capa Windows de ZUENA Link: memoria compartida entre procesos, motor → receptor (el camino
// a OBS completo, sin OBS), canal compartido entre dos instancias, grabación WAV, dispositivos y tiempo real.
// Usan el canal Link 8 para no cruzarse con un uso real en el mismo PC.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "LinkEngine.h"
#include "LinkReceiver.h"
#include "SharedChannel.h"
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

using namespace zlink;

namespace
{
thread_local bool measuring = false;
thread_local std::size_t allocations = 0;
constexpr std::uint32_t kTestChannel = 7; // Link 8

std::wstring selfPath()
{
    std::wstring p(MAX_PATH, L'\0');
    p.resize(GetModuleFileNameW(nullptr, p.data(), static_cast<DWORD>(p.size())));
    return p;
}

int runChild(const std::wstring &args)
{
    std::wstring cmd = L"\"" + selfPath() + L"\" " + args;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, 20000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

/** Proceso hijo: toma el canal, escribe una rampa y la deja publicada. */
int childWriter(std::uint32_t frames)
{
    SharedChannel ch;
    if (!ch.open(kTestChannel))
        return 2;
    if (claimChannel(*ch.header(), 0x5151, currentPid(), nowMs()) == Claim::busy)
        return 3;
    std::vector<float> l(frames), r(frames);
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        l[i] = static_cast<float>(i);
        r[i] = -static_cast<float>(i);
    }
    ch.writer().write(l, r);
    ch.header()->sampleRate.store(44100);
    // El objeto vive mientras alguien lo tenga abierto: el padre lo abre antes de que el hijo termine.
    Sleep(1500);
    releaseChannel(*ch.header(), 0x5151);
    return 0;
}

/** Un "DAW" que llama a process() con bloques fijos y ritmo de tiempo real (reloj de alta resolución). */
class FakeDaw
{
  public:
    FakeDaw(LinkEngine &e, double rate, std::uint32_t block, LinkControls c, double freq = 1000)
        : engine_(e), rate_(rate), block_(block), controls_(c), freq_(freq)
    {
        engine_.prepare(rate, static_cast<int>(block));
        thread_ = std::thread([this] { run(); });
    }
    ~FakeDaw() { stop(); }
    void stop()
    {
        quit_ = true;
        if (thread_.joinable())
            thread_.join();
    }
    void setControls(const LinkControls &c)
    {
        std::scoped_lock lock(m_);
        controls_ = c;
    }
    std::size_t allocationsInProcess() const { return rtAllocations_.load(); }
    std::uint64_t blocks() const { return blocks_.load(); }

  private:
    LinkEngine &engine_;
    double rate_;
    std::uint32_t block_;
    LinkControls controls_;
    double freq_, phase_ = 0;
    std::mutex m_;
    std::atomic<bool> quit_{false};
    std::atomic<std::size_t> rtAllocations_{0};
    std::atomic<std::uint64_t> blocks_{0};
    std::thread thread_;

    void run()
    {
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        std::vector<float> l(block_), r(block_);
        const auto period = std::chrono::duration<double>(block_ / rate_);
        auto next = std::chrono::steady_clock::now();
        while (!quit_)
        {
            for (std::uint32_t i = 0; i < block_; ++i)
            {
                l[i] = static_cast<float>(0.5 * std::sin(phase_));
                r[i] = -l[i];
                phase_ += 2 * std::numbers::pi * freq_ / rate_;
            }
            LinkControls c;
            {
                std::scoped_lock lock(m_);
                c = controls_;
            }
            allocations = 0;
            measuring = true;
            engine_.process(l, r, c, false);
            measuring = false;
            rtAllocations_ += allocations;
            ++blocks_;
            next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
            const auto wait = next - std::chrono::steady_clock::now();
            if (wait > std::chrono::microseconds(0))
            {
                LARGE_INTEGER due;
                due.QuadPart = -static_cast<LONGLONG>(std::chrono::duration_cast<std::chrono::nanoseconds>(wait).count() / 100);
                SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(timer, 1000);
            }
        }
        CloseHandle(timer);
    }
};

template <typename F> bool waitFor(F &&condition, int ms)
{
    for (int i = 0; i < ms / 10; ++i)
    {
        if (condition())
            return true;
        Sleep(10);
    }
    return condition();
}
} // namespace

void *operator new(std::size_t n)
{
    if (measuring)
        ++allocations;
    if (auto *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc{};
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}
void *operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete[](void *p) noexcept
{
    std::free(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
    std::free(p);
}

TEST_CASE("Memoria compartida: otro proceso escribe y este lee lo mismo")
{
    // El hijo abre primero; este proceso lo abre mientras el hijo sigue vivo.
    std::thread child([] { CHECK(runChild(L"--child-writer 1000") == 0); });
    SharedChannel ch;
    REQUIRE(waitFor([&] { return ch.open(kTestChannel) && ch.header()->cursor.written.load() >= 1000; }, 8000));
    CHECK(ch.header()->sampleRate.load() == 44100);
    auto reader = ch.reader();
    reader.syncToLatest(1000);
    std::vector<float> out(1000 * 2);
    const auto got = reader.read(out);
    REQUIRE(got.frames == 1000);
    for (std::uint32_t i = 0; i < 1000; ++i)
    {
        REQUIRE(out[2 * i] == static_cast<float>(i));
        REQUIRE(out[2 * i + 1] == -static_cast<float>(i));
    }
    child.join();
    CHECK(ch.header()->ownerToken.load() == 0); // el hijo soltó el canal al terminar
}

TEST_CASE("Camino a OBS completo: el motor manda, el receptor recibe a su reloj sin cortes")
{
    LinkEngine engine;
    LinkControls c;
    c.obs = true;
    c.channel = kTestChannel;
    FakeDaw daw(engine, 48000, 512, c);

    std::mutex m;
    std::vector<float> got;
    std::uint64_t lastTs = 0, gaps = 0;
    LinkReceiver rx;
    rx.start(kTestChannel, LinkReceiver::Buffer::normal, 48000, nullptr,
             [&](const float *data, std::uint32_t frames, std::uint64_t ts)
             {
                 std::scoped_lock lock(m);
                 // Las marcas de tiempo son continuas: cada bloque empieza donde terminó el anterior.
                 if (lastTs != 0 && ts < lastTs)
                     ++gaps;
                 lastTs = ts + static_cast<std::uint64_t>(frames * 1e9 / 48000);
                 got.insert(got.end(), data, data + frames * 2);
             });

    REQUIRE(waitFor([&] { return engine.snapshot().obs == ObsState::connected; }, 5000));
    REQUIRE(waitFor([&] { return rx.status().phase == LinkReceiver::Phase::receiving; }, 3000));
    Sleep(4000);
    const auto st = rx.status();
    const auto snap = engine.snapshot();
    rx.stop();
    daw.stop();

    WARN("latencia " << st.latencyMs << " ms, cortes " << st.cuts << ", host " << st.host);
    CHECK(st.cuts == 0);
    CHECK(snap.obsCuts == 0);
    CHECK(gaps == 0);
    CHECK(st.sourceRate == 48000);
    CHECK(st.latencyMs > 1);
    CHECK(st.latencyMs < 40);
    CHECK(snap.obsLatencyMs > 1);
    CHECK(daw.allocationsInProcess() == 0);

    // La señal llega entera: el último segundo tiene la energía de un seno de 0,5 (RMS 0,354) y es finita.
    std::scoped_lock lock(m);
    REQUIRE(got.size() > 48000 * 2 * 2);
    double sum = 0;
    for (std::size_t i = got.size() - 48000 * 2; i < got.size(); i += 2)
    {
        REQUIRE(std::isfinite(got[i]));
        sum += got[i] * got[i];
        CHECK(std::abs(got[i] + got[i + 1]) < 1e-5f); // el derecho es el izquierdo invertido
    }
    const double rms = std::sqrt(sum / 48000);
    WARN("RMS " << rms);
    CHECK(std::abs(rms - 0.5 / std::sqrt(2.0)) < 0.01);
}

TEST_CASE("Camino a OBS: frecuencias distintas (DAW a 44,1 kHz, OBS a 48 kHz)")
{
    LinkEngine engine;
    LinkControls c;
    c.channel = kTestChannel;
    FakeDaw daw(engine, 44100, 256, c);
    std::atomic<std::uint64_t> frames{0};
    LinkReceiver rx;
    rx.start(kTestChannel, LinkReceiver::Buffer::minimum, 48000, nullptr,
             [&](const float *, std::uint32_t n, std::uint64_t) { frames += n; });
    REQUIRE(waitFor([&] { return rx.status().phase == LinkReceiver::Phase::receiving; }, 5000));
    const auto start = frames.load();
    Sleep(3000);
    const auto st = rx.status();
    rx.stop();
    daw.stop();
    CHECK(st.cuts == 0);
    CHECK(st.sourceRate == 44100);
    // El receptor entrega a su propio reloj: 3 s a 48 kHz.
    CHECK(std::abs(static_cast<double>(frames.load() - start) - 144000.0) < 4800.0);
}

TEST_CASE("Dos pistas en el mismo canal: la segunda espera y las dos se enteran")
{
    LinkEngine a, b;
    LinkControls c;
    c.channel = kTestChannel;
    FakeDaw da(a, 48000, 512, c);
    REQUIRE(waitFor([&] { return a.snapshot().obs == ObsState::waiting || a.snapshot().obs == ObsState::connected; }, 3000));
    FakeDaw db(b, 48000, 512, c);
    REQUIRE(waitFor([&] { return b.snapshot().obs == ObsState::busy; }, 3000));
    CHECK(waitFor([&] { return a.snapshot().obsContender; }, 2000));
    CHECK(b.snapshot().otherOwner == hostDisplayName());
    CHECK(b.snapshot().channelInUse[kTestChannel]);

    // La segunda cambia a otro canal libre: deja de estar ocupada y la primera deja de ver aspirante.
    LinkControls other = c;
    other.channel = kTestChannel - 1;
    db.setControls(other);
    CHECK(waitFor([&] { return b.snapshot().obs == ObsState::waiting; }, 3000));
    CHECK(waitFor([&] { return !a.snapshot().obsContender; }, 3000));

    // Cuando la primera se apaga, el canal queda libre para otra.
    da.stop();
    LinkControls off = c;
    off.obs = false;
    a.process({}, {}, off, false);
    CHECK(waitFor([&] { return a.snapshot().obs == ObsState::off; }, 2000));
    db.setControls(c);
    CHECK(waitFor([&] { return b.snapshot().obs == ObsState::waiting; }, 3000));
    db.stop();
}

TEST_CASE("Grabación WAV: lo grabado es exactamente lo que se envió")
{
    const auto folder = std::filesystem::temp_directory_path() / L"zuena-link-tests";
    std::filesystem::remove_all(folder);
    LinkEngine engine;
    engine.setRecording({folder, SampleFormat::float32});
    LinkControls c;
    c.obs = false;
    FakeDaw daw(engine, 48000, 480, c, 440);
    Sleep(200);
    engine.startRecording();
    REQUIRE(waitFor([&] { return engine.snapshot().rec.recording; }, 2000));
    Sleep(1500);
    engine.stopRecording();
    REQUIRE(waitFor([&] { return !engine.snapshot().rec.recording; }, 2000));
    daw.stop();
    const auto rec = engine.snapshot().rec;
    INFO(rec.error);
    CHECK(rec.error.empty());
    CHECK(rec.cuts == 0);
    REQUIRE(std::filesystem::exists(rec.file));
    CHECK(rec.file.filename().wstring().rfind(L"ZUENA Link ", 0) == 0);

    std::ifstream f(rec.file, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    WavContents w;
    REQUIRE(parseWav(bytes, w));
    CHECK(w.layout.sampleRate == 48000);
    CHECK(w.layout.channels == 2);
    CHECK(w.frames > 48000);
    CHECK(w.frames < 48000 * 3);
    // Cada muestra continúa el seno (sin huecos ni repeticiones): la diferencia con el seno ideal
    // ajustado en fase es mínima en todo el archivo.
    const double step = 2 * std::numbers::pi * 440 / 48000;
    double best = 1e9, bestPhase = 0;
    for (int k = 0; k < 2000; ++k)
    {
        const double ph = k * 2 * std::numbers::pi / 2000;
        double e = 0;
        for (std::size_t i = 0; i < 200; ++i)
            e += std::abs(w.samples[2 * i] - 0.5 * std::sin(ph + i * step));
        if (e < best)
        {
            best = e;
            bestPhase = ph;
        }
    }
    double worst = 0;
    for (std::size_t i = 0; i < w.frames; ++i)
    {
        worst = std::max(worst, std::abs(w.samples[2 * i] - 0.5 * std::sin(bestPhase + static_cast<double>(i) * step)));
        REQUIRE(w.samples[2 * i + 1] == -w.samples[2 * i]);
    }
    WARN("error máximo " << worst);
    CHECK(worst < 0.01);
    std::filesystem::remove_all(folder);
}

TEST_CASE("Grabación WAV: carpeta imposible da un error claro, no un cuelgue")
{
    LinkEngine engine;
    engine.setRecording({L"Z:\\no\\existe\\\x01", SampleFormat::pcm24});
    LinkControls c;
    c.obs = false;
    FakeDaw daw(engine, 48000, 480, c);
    engine.startRecording();
    REQUIRE(waitFor([&] { return !engine.snapshot().rec.error.empty(); }, 3000));
    CHECK_FALSE(engine.snapshot().rec.recording);
    daw.stop();
}

TEST_CASE("Dispositivos: la lista se arma y, si hay una salida, WASAPI la abre y la sostiene")
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto list = listRenderEndpoints();
    if (SUCCEEDED(com))
        CoUninitialize();
    for (const auto &e : list)
    {
        CHECK_FALSE(e.id.empty());
        CHECK(e.channels >= 1);
    }
    if (list.empty())
    {
        WARN("Este equipo no tiene salidas de audio: se omite la prueba de WASAPI.");
        return;
    }
    // Se manda silencio (el motor sin señal): no suena nada aunque sea la salida por defecto.
    LinkEngine engine;
    const auto def = std::find_if(list.begin(), list.end(), [](const Endpoint &e) { return e.isDefault; });
    const auto &target = def != list.end() ? *def : list.front();
    engine.setDevice({target.id, target.name, false, 0});
    LinkControls c;
    c.obs = false;
    c.device = true;
    c.sendDb = -60;
    FakeDaw daw(engine, 48000, 512, c, 0);
    REQUIRE(waitFor([&] { return engine.snapshot().device.phase == WasapiOutput::Phase::running; }, 5000));
    Sleep(2000);
    const auto st = engine.snapshot().device;
    daw.stop();
    WARN(target.name << ": " << st.deviceRate << " Hz, período " << st.periodMs << " ms, latencia " << st.latencyMs
                     << " ms, compartido de baja latencia " << st.lowLatencyShared << ", error «" << st.error << "»");
    CHECK(st.deviceRate >= 8000);
    CHECK(st.periodMs > 0);
    CHECK(st.cuts == 0);
    CHECK(st.latencyMs < 80);
}

TEST_CASE("Tiempo real: process() no reserva memoria con los tres destinos activos")
{
    LinkEngine engine;
    engine.setRecording({std::filesystem::temp_directory_path() / L"zuena-link-rt", SampleFormat::pcm16});
    LinkControls c;
    c.channel = kTestChannel;
    std::vector<float> l(4096, 0.1f), r(4096, -0.1f);
    engine.prepare(48000, 4096);
    engine.startRecording();
    allocations = 0;
    measuring = true;
    for (int n = 0; n < 400; ++n)
    {
        c.sendDb = static_cast<float>(n % 24 - 12);
        c.mono = n % 2 == 0;
        c.channel = static_cast<std::uint32_t>(n % 8);
        engine.process({l.data(), static_cast<std::size_t>(64 + n * 9 % 4000)}, {r.data(), static_cast<std::size_t>(64 + n * 9 % 4000)}, c, n % 50 == 0);
        engine.process({l.data(), 100}, {}, c, false); // pista mono
    }
    measuring = false;
    CHECK(allocations == 0);
    engine.stopRecording();
    Sleep(100);
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / L"zuena-link-rt");
}

int main(int argc, char **argv)
{
    if (argc >= 3 && std::string(argv[1]) == "--child-writer")
        return childWriter(static_cast<std::uint32_t>(std::atoi(argv[2])));
    return Catch::Session().run(argc, argv);
}
