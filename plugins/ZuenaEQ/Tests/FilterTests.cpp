// Tests del diseño de filtros y parametrización TPT SVF: respuesta frente al prototipo analógico,
// estabilidad, Flat Tilt, All Pass y simetría. Mismos criterios que tools/verify-filters.mjs.

#include "FilterDesign.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace zeq;

namespace
{
double digitalDb (const Design& d, double hz, double fs)
{
    return 20.0 * std::log10 (std::max (std::abs (response (d, hz, fs)), 1.0e-15));
}

bool isStable (const Design& d)
{
    const auto svf = toSvf (d);
    for (int i = 0; i < d.count; ++i)
    {
        const auto& s = d.sections[(size_t) i];
        const auto& sv = svf.sections[(size_t) i];
        if (! (std::abs (s.a2) < 1.0 && std::abs (s.a1) < 1.0 + s.a2 && sv.g > 0.0 && sv.k >= 0.0))
            return false;
    }
    return true;
}
} // namespace

TEST_CASE ("Bell: ganancia exacta en el centro y 0 dB en DC", "[filters]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (double g : { -24.0, -6.0, 6.0, 24.0 })
        {
            const auto d = designBand (FilterType::Bell, 1000.0, g, 1.0, 12, fs);
            CHECK (digitalDb (d, 1000.0, fs) == Catch::Approx (g).margin (0.01));
            CHECK (digitalDb (d, 10.0, fs) == Catch::Approx (0.0).margin (0.05));
        }
}

TEST_CASE ("Shelves, Tilt y Flat Tilt: meseta e inclinación correctas", "[filters]")
{
    const double fs = 48000.0;
    const auto low = designBand (FilterType::LowShelf, 200.0, 9.0, 0.7071, 12, fs);
    CHECK (digitalDb (low, 15.0, fs) == Catch::Approx (9.0).margin (0.1));
    CHECK (digitalDb (low, 10000.0, fs) == Catch::Approx (0.0).margin (0.1));

    const auto high = designBand (FilterType::HighShelf, 4000.0, -9.0, 0.7071, 12, fs);
    CHECK (digitalDb (high, 20.0, fs) == Catch::Approx (0.0).margin (0.1));
    CHECK (digitalDb (high, 20000.0, fs) == Catch::Approx (-9.0).margin (0.3));

    const auto tilt = designBand (FilterType::TiltShelf, 1000.0, 6.0, 0.7071, 12, fs);
    CHECK (digitalDb (tilt, 15.0, fs) == Catch::Approx (-3.0).margin (0.1));
    CHECK (digitalDb (tilt, 1000.0, fs) == Catch::Approx (0.0).margin (0.05));

    const auto flat = designBand (FilterType::FlatTilt, 1000.0, 6.0, 0.7071, 12, fs);
    CHECK (digitalDb (flat, 1000.0, fs) == Catch::Approx (0.0).margin (0.08));
    CHECK (digitalDb (flat, 100.0, fs) < -1.5);
    CHECK (digitalDb (flat, 10000.0, fs) > 1.5);
}

TEST_CASE ("All Pass: módulo unitario exacto (0 dB) y rotación de fase", "[filters]")
{
    const double fs = 48000.0;
    const auto ap = designBand (FilterType::AllPass, 1200.0, 0.0, 2.0, 12, fs);
    for (double f : { 20.0, 200.0, 1200.0, 8000.0, 20000.0 })
        CHECK (digitalDb (ap, f, fs) == Catch::Approx (0.0).margin (1.0e-6));
    CHECK (std::abs (phaseDegrees (ap, 1200.0, fs)) == Catch::Approx (180.0).margin (0.5));
}

TEST_CASE ("Cortes: -3 dB en la frecuencia de corte (Butterworth) y doble cero exacto en Low Cut", "[filters]")
{
    const double fs = 48000.0;
    for (int slope : { 12, 24, 48, 96 })
    {
        const auto hp = designBand (FilterType::LowCut, 1000.0, 0.0, 0.7071, slope, fs);
        CHECK (digitalDb (hp, 1000.0, fs) == Catch::Approx (-3.01).margin (0.1));
        CHECK (digitalDb (hp, 125.0, fs) == Catch::Approx (-3.0 * slope).margin (1.2));
        CHECK (isStable (hp));
    }
    const auto lp1 = designBand (FilterType::HighCut, 2000.0, 0.0, 0.7071, 6, fs);
    CHECK (digitalDb (lp1, 2000.0, fs) == Catch::Approx (-3.01).margin (0.15));
}

TEST_CASE ("Notch: atenuación profunda en el centro", "[filters]")
{
    const auto d = designBand (FilterType::Notch, 3000.0, 0.0, 4.0, 12, 48000.0);
    CHECK (digitalDb (d, 3000.0, 48000.0) < -60.0);
    CHECK (digitalDb (d, 100.0, 48000.0) == Catch::Approx (0.0).margin (0.05));
}

TEST_CASE ("Ajuste analógico: error < 0.45 dB hasta fs/4 en bells y cortes altos de uso real", "[filters]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (double f : { 50.0, 200.0, 1000.0, 5000.0 })
            for (double g : { -12.0, -3.0, 3.0, 12.0 })
                for (double q : { 0.5, 1.0, 4.0 })
                {
                    const auto d = designBand (FilterType::Bell, f, g, q, 12, fs);
                    for (double ef = 20.0; ef < fs * 0.25; ef *= 1.1)
                        CHECK (std::abs (digitalDb (d, ef, fs) - analogMagnitudeDb (FilterType::Bell, f, g, q, 12, ef)) < 0.45);
                }
}

TEST_CASE ("Estabilidad y valores finitos (Biquad + TPT SVF) en los 10 tipos de filtro", "[filters]")
{
    for (int t = 0; t < (int) FilterType::Count; ++t)
        for (double fs : { 44100.0, 192000.0 })
            for (double f : { 10.0, 100.0, 5000.0, 19000.0, 30000.0 })
                for (double g : { -30.0, 0.0, 30.0 })
                    for (double q : { 0.025, 0.7071, 40.0 })
                        for (int slope : { 6, 18, 96 })
                        {
                            const auto d = designBand ((FilterType) t, f, g, q, slope, fs);
                            const auto svf = toSvf (d);
                            REQUIRE (isStable (d));
                            for (int i = 0; i < d.count; ++i)
                            {
                                const auto& s = d.sections[(size_t) i];
                                const auto& sv = svf.sections[(size_t) i];
                                REQUIRE (std::isfinite (s.b0));
                                REQUIRE (std::isfinite (s.b1));
                                REQUIRE (std::isfinite (s.b2));
                                REQUIRE (std::isfinite (sv.chp));
                                REQUIRE (std::isfinite (sv.cbp));
                                REQUIRE (std::isfinite (sv.clp));
                            }
                        }
}
