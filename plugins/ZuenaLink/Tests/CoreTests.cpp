// Pruebas del núcleo de ZUENA Link (sin Windows): anillo, puente de relojes, WAV, rutas y protocolo.
#include "BroadcastRing.h"
#include "ClockBridge.h"
#include "DeviceRoutes.h"
#include "LinkProtocol.h"
#include "WavFormat.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <complex>
#include <cmath>
#include <memory>
#include <numbers>
#include <thread>
#include <vector>

using namespace zlink;

namespace
{
constexpr double kPi = std::numbers::pi;

/** FFT radix-2 en el lugar (solo para medir en las pruebas). */
void fft(std::vector<std::complex<double>> &a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * kPi / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

double besselI0(double x)
{
    double sum = 1, term = 1;
    for (int k = 1; k < 80; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

/**
 * THD+N como lo mide un analizador: ventana de Kaiser (β = 20, lóbulos laterales por debajo de −150 dB),
 * 65 536 puntos, y todo lo que no sea la fundamental (±30 Hz) cuenta como distorsión más ruido.
 * Así un deslizamiento de unas ppm de la corrección de reloj (inaudible) no se confunde con distorsión.
 */
double residualDb(const std::vector<float> &x, std::size_t startFrame, std::size_t, double freq, double rate, int stride = 2)
{
    constexpr std::size_t n = 1 << 16;
    REQUIRE((startFrame + n) * static_cast<std::size_t>(stride) <= x.size());
    std::vector<std::complex<double>> a(n);
    const double norm = besselI0(20.0);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double u = 2.0 * static_cast<double>(i) / (n - 1) - 1.0;
        const double w = besselI0(20.0 * std::sqrt(std::max(0.0, 1.0 - u * u))) / norm;
        a[i] = x[(startFrame + i) * static_cast<std::size_t>(stride)] * w;
    }
    fft(a);
    double fundamental = 0, rest = 0;
    const double binHz = rate / n;
    for (std::size_t k = 1; k < n / 2; ++k) // sin continua
    {
        const double p = std::norm(a[k]);
        if (std::abs(static_cast<double>(k) * binHz - freq) <= 30.0)
            fundamental += p;
        else
            rest += p;
    }
    return 10 * std::log10(rest / fundamental);
}

/** Simula un DAW (bloques fijos a su reloj) y un destino (pide a su reloj) durante `seconds`. */
struct Simulation
{
    double inRate = 48000, outRate = 48000, driftPpm = 0, freq = 997, targetMs = 10;
    std::uint32_t block = 512, pullFrames = 480;
    double seconds = 5;
    bool keepOutput = true;
    std::vector<float> output;
    ClockBridge bridge;
    double maxFillErrorMs = 0, finalPpm = 0;
    double settleSeconds = 60;
    double windowMin = 1e300, ppmSum = 0;
    std::uint32_t windowPulls = 0, ppmCount = 0;

    void run()
    {
        bridge.prepare({inRate, outRate, targetMs, 500, 1000.0 * block / inRate});
        const double realIn = inRate * (1 + driftPpm * 1e-6);
        double phase = 0, owed = 0;
        std::vector<float> in(block * 2), out(pullFrames * 2);
        const auto pulls = static_cast<std::size_t>(seconds * outRate / pullFrames);
        if (keepOutput)
            output.reserve(pulls * pullFrames * 2);
        for (std::size_t p = 0; p < pulls; ++p)
        {
            owed += realIn * pullFrames / outRate; // cuadros que el DAW produjo mientras el destino pedía
            while (owed >= block)
            {
                for (std::uint32_t i = 0; i < block; ++i)
                {
                    const float v = static_cast<float>(0.5 * std::sin(phase));
                    in[2 * i] = v;
                    in[2 * i + 1] = -v;
                    phase += 2 * kPi * freq / inRate;
                }
                bridge.push(in);
                owed -= block;
            }
            bridge.pull(out);
            if (static_cast<double>(p) * pullFrames / outRate > seconds - 60) // promedio del último minuto
            {
                ppmSum += bridge.stats().ratioPpm;
                ++ppmCount;
            }
            if (keepOutput)
                output.insert(output.end(), out.begin(), out.end());
            const double t = static_cast<double>(p) * pullFrames / outRate;
            // El colchón sube un bloque entero cuando llega uno: lo que importa es el mínimo de cada ventana.
            windowMin = std::min(windowMin, bridge.fillFrames());
            const auto perWindow = static_cast<std::uint32_t>(std::ceil(1.0 * outRate / pullFrames)); // 1 s: abarca el batido
            if (++windowPulls >= perWindow)
            {
                if (t > settleSeconds && bridge.stats().state == ClockBridge::State::running)
                {
                    const double expected = targetMs + 1000.0 * ClockBridge::kHalfTaps / inRate;
                    maxFillErrorMs = std::max(maxFillErrorMs, std::abs(windowMin * 1000.0 / inRate - expected));
                }
                windowMin = 1e300;
                windowPulls = 0;
            }
        }
        finalPpm = ppmCount > 0 ? ppmSum / ppmCount : 0;
    }
};
} // namespace

TEST_CASE("Anillo: lo escrito se lee igual, también al dar la vuelta")
{
    constexpr std::uint32_t cap = 256;
    std::vector<float> data(cap * 2);
    RingCursor cursor;
    RingWriter w(&cursor, data.data(), cap);
    RingReader r(&cursor, data.data(), cap);
    std::vector<float> l(100), rr(100), out(200 * 2);
    float k = 0;
    for (int round = 0; round < 10; ++round)
    {
        for (int i = 0; i < 100; ++i)
        {
            l[i] = k;
            rr[i] = -k;
            k += 1;
        }
        w.write(l, rr);
        const auto got = r.read(out);
        REQUIRE_FALSE(got.overrun);
        REQUIRE(got.frames == 100);
        for (int i = 0; i < 100; ++i)
        {
            REQUIRE(out[2 * i] == l[i]);
            REQUIRE(out[2 * i + 1] == rr[i]);
        }
    }
    // Mono: el canal derecho repite el izquierdo.
    w.write(l, {});
    const auto got = r.read(out);
    REQUIRE(got.frames == 100);
    CHECK(out[1] == out[0]);
}

TEST_CASE("Anillo: un lector atrasado detecta el desborde y se resincroniza")
{
    constexpr std::uint32_t cap = 128;
    std::vector<float> data(cap * 2);
    RingCursor cursor;
    RingWriter w(&cursor, data.data(), cap);
    RingReader slow(&cursor, data.data(), cap), fast(&cursor, data.data(), cap);
    std::vector<float> block(64, 1.0f), out(cap * 2);
    for (int i = 0; i < 5; ++i)
    {
        w.write(block, block);
        CHECK(fast.read(out).frames == 64); // otro lector, independiente
    }
    const auto r = slow.read(out);
    CHECK(r.overrun);
    CHECK(r.frames <= cap);
    CHECK(slow.available() == 0);
    w.write(block, block);
    const auto again = slow.read(out);
    CHECK_FALSE(again.overrun);
    CHECK(again.frames == 64);
}

TEST_CASE("Anillo: escritor y lector en hilos distintos nunca entregan datos rotos")
{
    constexpr std::uint32_t cap = 1024;
    std::vector<float> data(cap * 2);
    RingCursor cursor;
    RingWriter w(&cursor, data.data(), cap);
    RingReader r(&cursor, data.data(), cap);
    std::atomic<bool> done{false};
    constexpr int total = 1 << 21;
    std::thread writer(
        [&]
        {
            std::vector<float> l(97), rr(97);
            float v = 0;
            for (int n = 0; n < total; n += 97)
            {
                for (int i = 0; i < 97; ++i, v += 1)
                {
                    l[i] = std::fmod(v, 16777216.0f);
                    rr[i] = l[i];
                }
                w.write(l, rr);
            }
            done = true;
        });
    std::vector<float> out(300 * 2);
    float expected = -1;
    std::uint64_t frames = 0, overruns = 0, broken = 0;
    while (!done || r.available() > 0)
    {
        const auto got = r.read(out);
        if (got.overrun)
        {
            ++overruns;
            expected = -1;
        }
        for (std::uint32_t i = 0; i < got.frames; ++i)
        {
            if (expected >= 0 && out[2 * i] != expected)
                ++broken;
            if (out[2 * i] != out[2 * i + 1])
                ++broken;
            expected = out[2 * i] + 1;
        }
        frames += got.frames;
    }
    writer.join();
    CHECK(broken == 0);
    CHECK(frames > 0);
    INFO("desbordes " << overruns);
}

TEST_CASE("Puente de relojes: mismo reloj, la señal sale limpia (THD+N < −100 dB)")
{
    Simulation s;
    s.seconds = 4;
    s.settleSeconds = 1;
    s.run();
    const auto st = s.bridge.stats();
    CHECK(st.cuts == 0);
    CHECK(st.drops == 0);
    const double db = residualDb(s.output, 48000 * 2, 0, s.freq, 48000);
    INFO("residuo " << db << " dB");
    CHECK(db < -100);
}

TEST_CASE("Puente de relojes: ±200 ppm durante 10 minutos, sin cortes y con el colchón estable")
{
    for (const double drift : {-200.0, -50.0, 80.0, 200.0})
    {
        Simulation s;
        s.driftPpm = drift;
        s.seconds = 600;
        s.keepOutput = false;
        s.run();
        const auto st = s.bridge.stats();
        INFO("deriva " << drift << " ppm, corrección " << s.finalPpm << " ppm, error " << s.maxFillErrorMs << " ms");
        CHECK(st.cuts == 0);
        CHECK(st.drops == 0);
        CHECK(s.maxFillErrorMs < 1.0);
        // La corrección converge a la deriva real (el destino tiene que consumir esa diferencia).
        CHECK(std::abs(s.finalPpm - drift) < 15);
    }
}

TEST_CASE("Puente de relojes: también con bloques grandes del DAW (2048) y pedidos chicos (64)")
{
    Simulation s;
    s.block = 2048;
    s.pullFrames = 64;
    s.driftPpm = 150;
    s.seconds = 180;
    s.keepOutput = false;
    s.run();
    CHECK(s.bridge.stats().cuts == 0);
    CHECK(s.bridge.stats().drops == 0);
}

TEST_CASE("Puente de relojes: 44,1 kHz → 48 kHz y 48 kHz → 44,1 kHz conservan la señal")
{
    for (const auto [in, out] : {std::pair{44100.0, 48000.0}, std::pair{48000.0, 44100.0}, std::pair{96000.0, 48000.0}})
    {
        Simulation s;
        s.inRate = in;
        s.outRate = out;
        s.pullFrames = static_cast<std::uint32_t>(out / 100);
        s.seconds = 4;
        s.settleSeconds = 1;
        s.run();
        CHECK(s.bridge.stats().cuts == 0);
        const double db = residualDb(s.output, static_cast<std::size_t>(out) * 2, static_cast<std::size_t>(out), s.freq, out);
        INFO(in << " → " << out << ": residuo " << db << " dB");
        CHECK(db < -95);
    }
}

TEST_CASE("Puente de relojes: un hueco real se cuenta y suena como fundido, no como clic")
{
    ClockBridge b;
    b.prepare({48000, 48000, 10, 500, 512 / 48.0});
    std::vector<float> in(512 * 2), out(480 * 2), all;
    double phase = 0;
    const auto feed = [&]
    {
        for (int i = 0; i < 512; ++i)
        {
            const float v = static_cast<float>(0.5 * std::sin(phase));
            in[2 * i] = in[2 * i + 1] = v;
            phase += 2 * kPi * 1000 / 48000;
        }
        b.push(in);
    };
    double owed = 0;
    for (int p = 0; p < 300; ++p)
    {
        const bool gap = p >= 150 && p < 160; // 100 ms sin audio con la fuente activa
        owed += 480;
        while (owed >= 512)
        {
            if (!gap)
                feed();
            owed -= 512;
        }
        b.pull(out);
        all.insert(all.end(), out.begin(), out.end());
    }
    CHECK(b.stats().cuts == 1);
    float maxStep = 0;
    for (std::size_t i = 2; i < all.size(); i += 2)
        maxStep = std::max(maxStep, std::abs(all[i] - all[i - 2]));
    // Un seno de 1 kHz a 0,5 cambia como mucho 0,066 por muestra; un corte seco saltaría hasta 0,5.
    INFO("salto máximo " << maxStep);
    CHECK(maxStep < 0.1f);
    for (const float v : all)
        REQUIRE(std::isfinite(v));
}

TEST_CASE("Puente de relojes: si la fuente se detiene a propósito no hay corte")
{
    ClockBridge b;
    b.prepare({48000, 48000, 10, 500, 10});
    std::vector<float> in(480 * 2, 0.25f), out(480 * 2);
    for (int p = 0; p < 50; ++p)
    {
        b.push(in);
        b.pull(out);
    }
    b.setInputActive(false);
    for (int p = 0; p < 200; ++p)
        b.pull(out);
    b.setInputActive(true);
    for (int p = 0; p < 50; ++p)
    {
        b.push(in);
        b.pull(out);
    }
    CHECK(b.stats().cuts == 0);
    CHECK(b.stats().state == ClockBridge::State::running);
}

TEST_CASE("Puente de relojes: una ráfaga enorme se descarta con fundido y vuelve al colchón")
{
    ClockBridge b;
    b.prepare({48000, 48000, 10, 400, 10});
    std::vector<float> in(480 * 2, 0.25f), burst(48000 * 2, 0.25f), out(480 * 2);
    for (int p = 0; p < 50; ++p)
    {
        b.push(in);
        b.pull(out);
    }
    b.push(burst); // un segundo de golpe (el DAW se destrabó)
    b.pull(out);
    CHECK(b.stats().drops >= 1);
    for (int p = 0; p < 20; ++p)
    {
        b.push(in);
        b.pull(out);
    }
    CHECK(b.stats().fillMs < 30);
}

TEST_CASE("WAV: 24 bits y 32 bits flotante se leen de vuelta exactos")
{
    std::vector<float> samples;
    for (int i = -1000; i <= 1000; ++i)
        samples.push_back(static_cast<float>(i * 8191 / 8388607.0));
    samples.push_back(1.0f);
    samples.push_back(-1.0f);
    samples.push_back(0.0f); // cantidad par: cuadros estéreo completos
    for (const auto fmt : {SampleFormat::pcm24, SampleFormat::float32})
    {
        WavLayout layout{48000, 2, fmt};
        SampleEncoder enc(fmt);
        std::vector<std::uint8_t> file = wavHeader(layout, samples.size() / 2);
        CHECK(file.size() == wavHeaderBytes(layout));
        std::vector<std::uint8_t> body(samples.size() * 4);
        body.resize(enc.encode(samples, body));
        file.insert(file.end(), body.begin(), body.end());
        WavContents c;
        REQUIRE(parseWav(file, c));
        CHECK(c.layout.format == fmt);
        CHECK(c.layout.sampleRate == 48000);
        CHECK(c.frames == samples.size() / 2);
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            if (fmt == SampleFormat::float32)
                REQUIRE(c.samples[i] == samples[i]);
            else
                REQUIRE(std::lround(c.samples[i] * 8388607.0) == std::lround(samples[i] * 8388607.0));
        }
    }
}

TEST_CASE("WAV: 16 bits con dither queda a ±1 LSB y recorta contando")
{
    SampleEncoder enc(SampleFormat::pcm16);
    std::vector<float> in{0.0f, 0.25f, -0.25f, 0.999f, 1.5f, -1.5f, std::nanf("")};
    std::vector<std::uint8_t> out(in.size() * 2);
    REQUIRE(enc.encode(in, out) == in.size() * 2);
    for (std::size_t i = 0; i < 4; ++i)
    {
        const auto v = static_cast<std::int16_t>(out[2 * i] | (out[2 * i + 1] << 8));
        CHECK(std::abs(v - in[i] * 32767.0f) <= 1.5f);
    }
    CHECK(enc.clipped() == 2);
    const auto nan = static_cast<std::int16_t>(out[12] | (out[13] << 8));
    CHECK(std::abs(nan) <= 1);
}

TEST_CASE("WAV: pasados los 4 GiB el encabezado es RF64 con tamaños de 64 bits")
{
    WavLayout layout{96000, 2, SampleFormat::float32};
    const std::uint64_t frames = 600'000'000ull; // 4,8 GB
    const auto h = wavHeader(layout, frames);
    CHECK(std::string(h.begin(), h.begin() + 4) == "RF64");
    CHECK(std::string(h.begin() + 12, h.begin() + 16) == "ds64");
    const auto small = wavHeader(layout, 1000);
    CHECK(std::string(small.begin(), small.begin() + 4) == "RIFF");
    CHECK(std::string(small.begin() + 12, small.begin() + 16) == "JUNK");
    CHECK(h.size() == small.size()); // mismo tamaño: el encabezado se reescribe en el lugar

    // Un RF64 chico armado a mano (tamaño de datos en ds64) se lee bien.
    WavLayout l2{48000, 1, SampleFormat::pcm24};
    auto file = wavHeader(l2, 0);
    file[0] = 'R'; file[1] = 'F'; file[2] = '6'; file[3] = '4';
    file[12] = 'd'; file[13] = 's'; file[14] = '6'; file[15] = '4';
    const std::uint64_t bytes = 3 * 3;
    for (int i = 0; i < 8; ++i)
        file[20 + 8 + i] = static_cast<std::uint8_t>(bytes >> (8 * i));
    for (int i = 0; i < 4; ++i)
        file[file.size() - 4 + i] = 0xFF;
    for (int i = 0; i < 9; ++i)
        file.push_back(0x10);
    WavContents c;
    REQUIRE(parseWav(file, c));
    CHECK(c.rf64);
    CHECK(c.frames == 3);
}

TEST_CASE("Rutas: reconoce cables virtuales, mezcladores y placas con loopback")
{
    const auto cable = classifyRender({"CABLE Input", "VB-Audio Virtual Cable"});
    CHECK(cable.kind == RouteKind::virtualCable);
    CHECK(cable.obsHint.find("CABLE Output") != std::string::npos);
    CHECK(isRecommended(cable));

    CHECK(classifyRender({"VoiceMeeter Input", "VB-Audio VoiceMeeter VAIO"}).kind == RouteKind::virtualCable);
    CHECK(classifyRender({"Voicemeeter AUX Input", "Voicemeeter"}).kind == RouteKind::streamMixer);
    CHECK(classifyRender({"Music", "Elgato Wave Link"}).kind == RouteKind::streamMixer);

    const auto ua = classifyRender({"Virtual 1-2", "Universal Audio Thunderbolt"});
    CHECK(ua.kind == RouteKind::interfaceVirtual);
    CHECK(ua.vendor == "Universal Audio");

    const auto focus = classifyRender({"Altavoces", "Focusrite USB Audio"});
    CHECK(focus.vendor == "Focusrite");
    CHECK(focus.interfaceHasLoopback);
    CHECK(isRecommended(focus));
    CHECK(focus.obsHint.find("Loopback") != std::string::npos);

    CHECK(classifyRender({"Speakers", "Fireface UCX II"}).vendor == "RME");
    CHECK(classifyRender({"Line Out", "MOTU M Series"}).vendor == "MOTU");
    CHECK(classifyRender({"Altavoces", "Audient EVO4"}).vendor == "Audient");

    const auto plain = classifyRender({"Altavoces", "Realtek(R) Audio"});
    CHECK(plain.kind == RouteKind::hardware);
    CHECK_FALSE(isRecommended(plain));
    CHECK(plain.obsHint.empty());

    CHECK(isLoopbackCapture({"Loopback", "Focusrite USB Audio"}));
    CHECK(isLoopbackCapture({"CABLE Output", "VB-Audio Virtual Cable"}));
    CHECK_FALSE(isLoopbackCapture({"Micrófono", "Realtek(R) Audio"}));
}

TEST_CASE("Protocolo: un solo dueño por canal, aviso al segundo y liberación por abandono")
{
    auto h = std::make_unique<SharedHeader>();
    initializeHeader(*h);
    REQUIRE(headerReady(*h));
    const std::uint64_t a = 0xA1, b = 0xB2;
    CHECK(claimChannel(*h, a, 10, 1000) == Claim::acquired);
    CHECK(claimChannel(*h, a, 10, 1010) == Claim::owned);
    CHECK(claimChannel(*h, b, 11, 1020) == Claim::busy);
    CHECK(hasContender(*h, a, 1030));
    CHECK_FALSE(channelAvailable(*h, b, 1030));
    CHECK(channelAvailable(*h, a, 1030));
    // b no puede soltar el canal de a.
    releaseChannel(*h, b);
    CHECK(h->ownerToken.load() == a);
    // a se congela más de un segundo: b lo toma.
    CHECK(claimChannel(*h, b, 11, 1010 + kOwnerStaleMs + 5) == Claim::acquired);
    CHECK(claimChannel(*h, a, 10, 1010 + kOwnerStaleMs + 10) == Claim::busy);
    releaseChannel(*h, b);
    CHECK(h->ownerToken.load() == 0);
    CHECK(claimChannel(*h, a, 10, 5000) == Claim::acquired);

    writeOwnerName(*h, "FL Studio");
    std::array<char, kNameBytes> name{};
    REQUIRE(readOwnerName(*h, name));
    CHECK(std::string(name.data()) == "FL Studio");
}

TEST_CASE("Protocolo: el nombre del objeto compartido incluye la versión y el canal")
{
    wchar_t name[48];
    sharedObjectName(0, name);
    CHECK(std::wstring(name) == L"Local\\ZUENA.Link.v1.1");
    sharedObjectName(7, name);
    CHECK(std::wstring(name) == L"Local\\ZUENA.Link.v1.8");
}
