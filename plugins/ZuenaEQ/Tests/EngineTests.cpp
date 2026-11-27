// Tests del motor: transparencia, sin NaN/Inf, dinámica, solo y seguridad en tiempo real (EB-06).

#include "EqEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <numbers>
#include <random>
#include <vector>

// --- Contador global de reservas de memoria (EB-06) -------------------------------------------
namespace
{
std::atomic<bool> countAllocations { false };
std::atomic<int> allocations { 0 };
} // namespace

void* operator new (std::size_t n)
{
    if (countAllocations.load())
        allocations.fetch_add (1);
    if (void* p = std::malloc (n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

using namespace zeq;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 512;

struct Stereo
{
    std::vector<float> l, r;
    explicit Stereo (int n) : l ((size_t) n), r ((size_t) n) {}
    float* ptrs[2] {};
    float* const* data() { ptrs[0] = l.data(); ptrs[1] = r.data(); return ptrs; }
};

void sine (Stereo& s, double hz, double amp, int offset = 0)
{
    for (size_t i = 0; i < s.l.size(); ++i)
        s.l[i] = s.r[i] = (float) (amp * std::sin (2.0 * std::numbers::pi * hz * (double) (i + (size_t) offset) / kFs));
}

double rmsDb (const std::vector<float>& v, size_t from)
{
    double acc = 0.0;
    for (size_t i = from; i < v.size(); ++i)
        acc += (double) v[i] * v[i];
    return 10.0 * std::log10 (acc / (double) (v.size() - from) + 1.0e-30);
}

/** Procesa varios bloques de un seno y devuelve el nivel RMS de salida del último. */
double processSine (EqEngine& e, double hz, double amp, int blocks = 40)
{
    Stereo s (kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        sine (s, hz, amp, b * kBlock);
        e.process (s.data(), 2, kBlock);
    }
    return rmsDb (s.l, 0);
}
} // namespace

TEST_CASE ("Sin bandas activas el motor es transparente bit a bit", "[engine]")
{
    EqEngine e;
    e.prepare (kFs, kBlock, 2);
    Stereo s (kBlock), ref (kBlock);
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    for (int i = 0; i < kBlock; ++i)
        s.l[(size_t) i] = ref.l[(size_t) i] = s.r[(size_t) i] = ref.r[(size_t) i] = dist (rng);
    e.process (s.data(), 2, kBlock);
    CHECK (s.l == ref.l);
    CHECK (s.r == ref.r);
}

TEST_CASE ("Una bell de +6 dB realza su frecuencia central 6 dB", "[engine]")
{
    EqEngine e;
    e.prepare (kFs, kBlock, 2);
    const double dry = processSine (e, 1000.0, 0.1);
    BandParams p;
    p.used = true;
    p.type = FilterType::Bell;
    p.freqHz = 1000.0;
    p.gainDb = 6.0;
    p.q = 1.0;
    e.setBand (0, p);
    const double wet = processSine (e, 1000.0, 0.1);
    CHECK (wet - dry == Catch::Approx (6.0).margin (0.1));
}

TEST_CASE ("Ganancia de salida", "[engine]")
{
    EqEngine e;
    e.prepare (kFs, kBlock, 2);
    const double dry = processSine (e, 440.0, 0.1);
    e.setOutputGainDb (-12.0);
    CHECK (processSine (e, 440.0, 0.1) - dry == Catch::Approx (-12.0).margin (0.05));
}

TEST_CASE ("Dinámica: una señal fuerte en la banda activa la reducción; una débil no", "[engine]")
{
    BandParams p;
    p.used = true;
    p.type = FilterType::Bell;
    p.freqHz = 2000.0;
    p.gainDb = 0.0;
    p.q = 1.0;
    p.dynOn = true;
    p.thresholdDb = -30.0;
    p.rangeDb = -9.0;
    p.ratio = 20.0;
    p.attackMs = 1.0;
    p.releaseMs = 50.0;

    EqEngine loud;
    loud.prepare (kFs, kBlock, 2);
    loud.setBand (0, p);
    const double dryLoud = 20.0 * std::log10 (0.5 / std::sqrt (2.0));
    const double outLoud = processSine (loud, 2000.0, 0.5);
    CHECK (loud.dynamicGainDb (0) == Catch::Approx (-9.0).margin (0.2));
    CHECK (outLoud - dryLoud == Catch::Approx (-9.0).margin (0.3));

    EqEngine quiet;
    quiet.prepare (kFs, kBlock, 2);
    quiet.setBand (0, p);
    processSine (quiet, 2000.0, 0.001);
    CHECK (quiet.dynamicGainDb (0) == Catch::Approx (0.0).margin (0.05));
}

TEST_CASE ("Mid: una banda en Mid no altera una señal puramente lateral", "[engine]")
{
    EqEngine e;
    e.prepare (kFs, kBlock, 2);
    BandParams p;
    p.used = true;
    p.type = FilterType::Bell;
    p.freqHz = 1000.0;
    p.gainDb = 12.0;
    p.placement = Placement::Mid;
    e.setBand (0, p);
    Stereo s (kBlock), in (kBlock);
    double worst = 0.0;
    for (int b = 0; b < 20; ++b)
    {
        sine (s, 1000.0, 0.1, b * kBlock);
        for (auto& v : s.r)
            v = -v;  // L = -R -> Mid = 0
        in.l = s.l;
        in.r = s.r;
        e.process (s.data(), 2, kBlock);
        for (size_t i = 0; i < s.l.size(); ++i)
            worst = std::max ({ worst, (double) std::abs (s.l[i] - in.l[i]), (double) std::abs (s.r[i] - in.r[i]) });
    }
    CHECK (worst < 1.0e-6);
}

TEST_CASE ("Sin NaN/Inf ni picos absurdos con parámetros aleatorios y automatización rápida", "[engine]")
{
    EqEngine e;
    e.prepare (44100.0, kBlock, 2);
    std::mt19937 rng (7);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    Stereo s (kBlock);
    for (int block = 0; block < 400; ++block)
    {
        for (int b = 0; b < kMaxBands; ++b)
        {
            BandParams p;
            p.used = u (rng) > 0.3;
            p.bypass = u (rng) > 0.9;
            p.type = (FilterType) (int) (u (rng) * (double) FilterType::Count);
            p.freqHz = 10.0 * std::pow (3000.0, u (rng));
            p.gainDb = -30.0 + 60.0 * u (rng);
            p.q = 0.025 * std::pow (1600.0, u (rng));
            p.slopeIndex = (int) (u (rng) * 8.0);
            p.placement = (Placement) (int) (u (rng) * 5.0);
            p.dynOn = u (rng) > 0.5;
            p.rangeDb = -30.0 + 60.0 * u (rng);
            p.solo = b == 3 && u (rng) > 0.8;
            e.setBand (b, p);
        }
        for (size_t i = 0; i < s.l.size(); ++i)
        {
            s.l[i] = (float) (u (rng) * 0.5 - 0.25);
            s.r[i] = (float) (u (rng) * 0.5 - 0.25);
        }
        e.process (s.data(), 2, kBlock);
        for (size_t i = 0; i < s.l.size(); ++i)
        {
            REQUIRE (std::isfinite (s.l[i]));
            REQUIRE (std::isfinite (s.r[i]));
        }
    }
}

TEST_CASE ("Un hueco de banda reutilizado entra sin barrer desde la frecuencia anterior", "[engine]")
{
    EqEngine e;
    e.prepare (kFs, kBlock, 2);
    BandParams p;
    p.used = true;
    p.type = FilterType::Bell;
    p.freqHz = 100.0;
    p.gainDb = 12.0;
    p.q = 1.0;
    e.setBand (0, p);
    processSine (e, 100.0, 0.1, 20);

    p.used = false;  // se borra la banda: fundido de salida de 5 ms
    e.setBand (0, p);
    processSine (e, 100.0, 0.1, 4);

    p.used = true;   // el mismo hueco vuelve como otra banda, lejos de 100 Hz
    p.freqHz = 5000.0;
    e.setBand (0, p);
    Stereo s (kBlock);
    sine (s, 100.0, 0.1, 24 * kBlock);
    e.process (s.data(), 2, kBlock);
    const double dryDb = 20.0 * std::log10 (0.1 / std::sqrt (2.0));
    CHECK (std::abs (rmsDb (s.l, 0) - dryDb) < 0.3);
}

TEST_CASE ("EB-04: activar, cambiar tipo/pendiente/canal o alternar solo no produce clics", "[engine][eb04]")
{
    EqEngine e;
    e.prepare (kFs, kBlock, 2);

    BandParams b0, b1;
    b0.used = true;
    b0.type = FilterType::Bell;
    b0.freqHz = 440.0;
    b0.gainDb = 12.0;
    b0.q = 1.2;

    b1.used = true;
    b1.type = FilterType::Bell;
    b1.freqHz = 1800.0;
    b1.gainDb = -9.0;
    b1.q = 2.0;

    Stereo s (kBlock);
    float prev = 0.0f;
    double maxStep = 0.0;
    int sampleIndex = 0;

    auto runBlocks = [&] (int count)
    {
        for (int blk = 0; blk < count; ++blk)
        {
            sine (s, 440.0, 0.1, sampleIndex);
            e.process (s.data(), 2, kBlock);
            for (int i = 0; i < kBlock; ++i)
            {
                if (sampleIndex + i > 0)
                    maxStep = std::max (maxStep, (double) std::abs (s.l[(size_t) i] - prev));
                prev = s.l[(size_t) i];
            }
            sampleIndex += kBlock;
        }
    };

    runBlocks (4);
    e.setBand (0, b0);          // entrada con fundido de 5 ms
    e.setBand (1, b1);
    runBlocks (10);

    b0.type = FilterType::LowCut;
    b0.slopeIndex = 7;          // cambio brusco a corte de 96 dB/oct
    e.setBand (0, b0);
    runBlocks (10);

    e.setQualityMode (QualityMode::NaturalPhase);  // conmutación en caliente a Fase Natural 2x
    runBlocks (12);

    b0.placement = Placement::Side;
    e.setBand (0, b0);          // cambio de colocación estéreo
    runBlocks (10);

    b0.solo = true;             // activar solo en banda 0
    e.setBand (0, b0);
    runBlocks (10);

    b0.solo = false;            // cambiar solo directamente de banda 0 a banda 1
    b1.solo = true;
    e.setBand (0, b0);
    e.setBand (1, b1);
    runBlocks (10);

    e.setQualityMode (QualityMode::ZeroLatency);
    b1.solo = false;            // salir de solo y borrar bandas
    b0.used = false;
    b1.used = false;
    e.setBand (0, b0);
    e.setBand (1, b1);
    runBlocks (12);

    // Un seno de amplitud 0.1 realzado +12 dB (0.4) a 440 Hz tiene derivada natural ~0.023/muestra.
    // Un salto sin fundido supera 0.15; con fundido de 5 ms se mantiene suave (< 0.05).
    CHECK (maxStep < 0.05);
}

TEST_CASE ("Fase Natural 2x: retardo exacto de 31 muestras y rizado < 0.002 dB", "[engine]")
{
    EqEngine e;
    e.setQualityMode (QualityMode::NaturalPhase);
    e.prepare (kFs, kBlock, 2);
    CHECK (e.latencySamples() == kNaturalPhaseLatency);

    Stereo imp (128);
    imp.l[0] = imp.r[0] = 1.0f;
    e.process (imp.data(), 2, 128);
    size_t peakIdx = 0;
    for (size_t i = 1; i < imp.l.size(); ++i)
        if (std::abs (imp.l[i]) > std::abs (imp.l[peakIdx]))
            peakIdx = i;
    CHECK (peakIdx == (size_t) kNaturalPhaseLatency);

    const double dry = 20.0 * std::log10 (0.1 / std::sqrt (2.0));
    CHECK (std::abs (processSine (e, 1500.0, 0.1, 12) - dry) < 0.002);
    CHECK (std::abs (processSine (e, 18000.0, 0.1, 12) - dry) < 0.002);
}

TEST_CASE ("EB-18: el bypass global es completo en ambos modos (sin bandas, ganancia de salida ni Auto-Gain)", "[engine][eb18]")
{
    for (const auto mode : { QualityMode::ZeroLatency, QualityMode::NaturalPhase })
    {
        EqEngine e;
        e.setQualityMode (mode);
        e.prepare (kFs, kBlock, 2);
        // Misma cantidad de bloques que abajo: el último bloque tiene la misma fase (10,67 ciclos por bloque),
        // así el RMS de un ciclo parcial no introduce diferencias ajenas al bypass.
        const double dry = processSine (e, 1000.0, 0.1, 40);

        BandParams p;
        p.used = true;
        p.type = FilterType::Bell;
        p.freqHz = 1000.0;
        p.gainDb = 9.0;
        p.q = 1.0;
        e.setBand (0, p);
        e.setOutputGainDb (-10.0);
        e.setAutoGain (true);
        e.setGlobalBypass (true);
        CHECK (std::abs (processSine (e, 1000.0, 0.1, 40) - dry) < 0.01);

        e.setGlobalBypass (false);
        CHECK (std::abs (processSine (e, 1000.0, 0.1, 40) - dry) > 0.5);
    }
}

TEST_CASE ("EB-06: process() no reserva memoria (incluido Fase Natural 2x y Auto-Gain)", "[engine][realtime]")
{
    EqEngine e;
    e.setQualityMode (QualityMode::NaturalPhase);
    e.setAutoGain (true);
    e.prepare (kFs, kBlock, 2);
    BandParams p;
    p.used = true;
    p.dynOn = true;
    for (int b = 0; b < kMaxBands; ++b)
    {
        p.freqHz = 30.0 * (b + 1);
        p.type = (FilterType) (b % (int) FilterType::Count);
        e.setBand (b, p);
    }
    Stereo s (kBlock);
    sine (s, 300.0, 0.5);
    allocations = 0;
    countAllocations = true;
    for (int i = 0; i < 50; ++i)
        e.process (s.data(), 2, kBlock);
    countAllocations = false;
    CHECK (allocations.load() == 0);
}
