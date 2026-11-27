#include "OpticalEngine.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace
{
constexpr int sr = 48000;
constexpr double tau = 2.0 * std::numbers::pi;
float tone(int i, double hz, double db, int rate = sr)
{
    return static_cast<float>(std::pow(10.0, db / 20.0) * std::sin(tau * hz * i / rate));
}
void feed(luma::OpticalEngine &e, int count, double hz = 1000, double db = -6, int rate = sr)
{
    std::array<float, 127> a{};
    for (int first = 0; first < count;)
    {
        const int n = std::min(static_cast<int>(a.size()), count - first);
        for (int j = 0; j < n; ++j)
            a[static_cast<std::size_t>(j)] = tone(first + j, hz, db, rate);
        e.process(std::span(a).first(static_cast<std::size_t>(n)));
        first += n;
    }
}
float steady(luma::Controls c, double hz, double db)
{
    luma::OpticalEngine e;
    e.prepare(sr, c);
    feed(e, sr, hz, db);
    return e.meters().reductionDb;
}
} // namespace

TEST_CASE("Calibración de entrada y salida y balística VU de 300 ms")
{
    for (const int rate : {44100, 48000, 96000, 192000})
    {
        luma::OpticalEngine e;
        luma::Controls c;
        c.reduction = 0;
        e.prepare(rate, c);
        feed(e, rate * 3 / 10, 1000, -18, rate);
        CHECK(std::abs(e.meters().inputVu - 20.0 * std::log10(0.99)) < 0.05);
        feed(e, rate, 1000, -18, rate);
        CHECK(std::abs(e.meters().inputVu) < 0.04f);
        CHECK(std::abs(e.meters().outputVu) < 0.04f);
        CHECK(std::abs(e.meters().peakDb + 18.0f) < 0.08f);
    }
}
TEST_CASE("La curva estática coincide con Compress y Limit y la rodilla es continua")
{
    luma::Controls c;
    c.reduction = 50;
    CHECK(steady(c, 1000, -32) < 0.01);
    CHECK(std::abs(steady(c, 1000, -10) - 8.0f) < 0.16);
    CHECK(std::abs(steady(c, 1000, -22) - 0.5f) < 0.10);
    c.limit = true;
    CHECK(std::abs(steady(c, 1000, -10) - 11.0f) < 0.20);
}
TEST_CASE("Dos constantes de recuperación y memoria de la duración")
{
    luma::Controls c;
    c.reduction = 65;
    const auto tail = [c](int hold, int release, double time = 100.0)
    {
        auto setup = c;
        setup.recoveryPercent = time;
        luma::OpticalEngine e;
        e.prepare(sr, setup);
        feed(e, hold);
        feed(e, release, 1000, -130);
        return e.meters().reductionDb;
    };
    const float shortTail = tail(sr / 10, sr / 3), longTail = tail(sr * 2, sr / 3);
    CHECK(shortTail < 2);
    CHECK(longTail > 4);
    CHECK(longTail > 3 * shortTail);
    CHECK(tail(sr * 2, sr, 200) > tail(sr * 2, sr, 50) + 2);
    luma::OpticalEngine e;
    e.prepare(sr, c);
    feed(e, sr / 100);
    auto early = e.meters().reductionDb;
    feed(e, sr);
    CHECK(early > 0.42 * e.meters().reductionDb);
    CHECK(early < 0.88 * e.meters().reductionDb);
}
TEST_CASE("El detector distingue grave y presencia sin ecualizar la señal")
{
    luma::Controls c;
    c.reduction = 65;
    const auto bass = steady(c, 40, -12), mid = steady(c, 3000, -12);
    c.highpassHz = 150;
    CHECK(steady(c, 40, -12) < bass - 6);
    CHECK(std::abs(steady(c, 3000, -12) - mid) < 0.25);
    c.highpassHz = 0;
    c.emphasisDb = 9;
    CHECK(steady(c, 10000, -12) > steady(luma::Controls{65}, 10000, -12) + 2.5);
    c.reduction = 0;
    luma::OpticalEngine e;
    e.prepare(sr, c);
    std::array<float, 17> a{};
    for (std::size_t i = 0; i < a.size(); ++i)
        a[i] = tone(static_cast<int>(i), 7000, -1);
    const auto source = a;
    e.process(a);
    CHECK(a == source);
}
TEST_CASE("Enlace estéreo conserva amplitud relativa y oposición de fase")
{
    luma::Controls c;
    c.reduction = 68;
    luma::OpticalEngine e;
    e.prepare(sr, c);
    std::vector<float> l(sr / 2), r(l.size());
    for (int i = 0; i < sr / 2; ++i)
    {
        l[i] = tone(i, 1000, -6);
        r[i] = -0.5f * l[i];
    }
    e.process(l, r);
    CHECK(e.meters().reductionDb > 10);
    for (std::size_t i = 0; i < r.size(); i += 53)
        CHECK(r[i] == -0.5f * l[i]);
}
TEST_CASE("Sidechain externo mono, estéreo, silencio y retorno a la entrada")
{
    luma::Controls c;
    c.reduction = 65;
    c.external = true;
    const auto gr = [&](bool routed, bool rightDriven, bool silent)
    {
        luma::OpticalEngine e;
        e.prepare(sr, c);
        std::vector<float> l(sr / 2), r(l.size()), kl(l.size()), kr(l.size());
        for (int i = 0; i < sr / 2; ++i)
        {
            l[i] = r[i] = tone(i, 440, -24);
            if (!silent)
                (rightDriven ? kr : kl)[i] = tone(i, 1000, -2);
        }
        if (routed)
            e.process(l, r, kl, rightDriven ? std::span<const float>(kr) : std::span<const float>{});
        else
            e.process(l, r);
        return e.meters().reductionDb;
    };
    CHECK(gr(true, false, false) > 14);
    CHECK(gr(true, true, false) > 14);
    CHECK(gr(true, false, true) < 0.01);
    CHECK(gr(false, false, true) > 1);
}
TEST_CASE("Mezcla paralela, ganancia manual y bypass son coherentes")
{
    luma::Controls c;
    c.reduction = 0;
    c.makeupDb = 6;
    for (const auto mix : {0.0, 40.0, 100.0})
    {
        c.mixPercent = mix;
        luma::OpticalEngine e;
        e.prepare(sr, c);
        std::array<float, 2> v{0.5f, -0.2f};
        e.process(v);
        CHECK(std::abs(v[0] - 0.5 * (1.0 + mix / 100.0 * (std::pow(10.0, 0.3) - 1.0))) < 1.0e-6);
    }
    c.reduction = 90;
    c.bypass = true;
    luma::OpticalEngine e;
    e.prepare(sr, c);
    std::array<float, 3> a{0.6f, -0.7f, 0.1f};
    const auto original = a;
    e.process(a);
    CHECK(a == original);
}
TEST_CASE("Tamaño de bloque variable, silencio, reinicio y automatización")
{
    luma::Controls c;
    luma::OpticalEngine a, b;
    a.prepare(sr, c);
    b.prepare(sr, c);
    std::vector<float> x(12391);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = tone(static_cast<int>(i), 300, -3);
    auto y = x;
    a.process(x);
    for (std::size_t i = 0; i < y.size(); i += 37)
        b.process(std::span(y).subspan(i, std::min<std::size_t>(37, y.size() - i)));
    CHECK(x == y);
    a.process(std::span<float>{});
    for (int i = 0; i < 80; ++i)
    {
        c.reduction = (i % 2) * 100;
        c.limit = i % 2;
        c.highpassHz = (i % 2) * 300;
        c.mixPercent = (i % 3) * 50;
        c.makeupDb = i % 2 ? 24 : -18;
        a.setControls(c);
        std::array<float, 73> buf{};
        buf.fill(0.2f);
        a.process(buf);
        CHECK(std::all_of(buf.begin(), buf.end(), [](float v) { return std::isfinite(v) && std::abs(v) < 4; }));
        for (std::size_t j = 1; j < buf.size(); ++j)
            REQUIRE(std::abs(buf[j] - buf[j - 1]) < 0.04f);
    }
    a.prepare(sr, c);
    std::array<float, 50> silent{};
    a.process(silent);
    CHECK(std::all_of(silent.begin(), silent.end(), [](float v) { return v == 0; }));
    CHECK(a.meters().reductionDb == 0);
}
TEST_CASE("Entrada extrema permanece finita y no contamina el siguiente bloque")
{
    luma::Controls c;
    c.reduction = 0;
    c.makeupDb = 24;
    luma::OpticalEngine e;
    e.prepare(sr, c);
    std::array<float, 4> block{std::numeric_limits<float>::max(), std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::quiet_NaN(), -0.02f};
    e.process(block);
    CHECK(std::all_of(block.begin(), block.end(), [](float v) { return std::isfinite(v); }));
    CHECK(block[1] == 0);
    CHECK(block[2] == 0);
    CHECK(std::isfinite(e.meters().peakDb));
}

TEST_CASE("Ataque regulable y límite de reducción medidos con señal real")
{
    luma::Controls c;
    c.reduction = 80;
    luma::OpticalEngine fast, slow;
    c.attackMs = 1;
    fast.prepare(sr, c);
    c.attackMs = 40;
    slow.prepare(sr, c);
    feed(fast, sr / 100);
    feed(slow, sr / 100);
    CHECK(fast.meters().reductionDb > slow.meters().reductionDb + 8.0f);
    feed(fast, sr);
    feed(slow, sr);
    CHECK(std::abs(fast.meters().reductionDb - slow.meters().reductionDb) < 0.85f);
    c.rangeDb = 3.5;
    CHECK(std::abs(steady(c, 1000, -1) - 3.5f) < 0.001f);
    c.rangeDb = 0;
    CHECK(steady(c, 1000, -1) == 0);
}

TEST_CASE("El enlace L/R interpola la reducción sin mezclar los canales")
{
    const auto measured = [](double link)
    {
        luma::Controls c;
        c.reduction = 75;
        c.stereoLinkPercent = link;
        luma::OpticalEngine e;
        e.prepare(sr, c);
        std::vector<float> left(sr), right(sr), original(sr);
        for (int i = 0; i < sr; ++i)
        {
            left[i] = tone(i, 1000, -5);
            right[i] = original[i] = tone(i, 750, -37);
        }
        e.process(left, right);
        if (link == 0)
            CHECK(right == original);
        return e.meters();
    };
    const auto dual = measured(0), half = measured(50), linked = measured(100);
    CHECK(dual.reductionLeftDb > 15);
    CHECK(dual.reductionRightDb == 0);
    CHECK(linked.reductionLeftDb == linked.reductionRightDb);
    CHECK(std::abs(half.reductionRightDb - linked.reductionRightDb * 0.5f) < 0.001f);
}

TEST_CASE("EB-35: cambiar a un sidechain en oposición no cancela la detección")
{
    luma::Controls c;
    c.reduction = 70;
    luma::OpticalEngine switched, control;
    switched.prepare(sr, c);
    control.prepare(sr, c);
    std::array<float, 48> intended{}, key{}, actual{};
    for (int n = 0; n < 800; ++n)
    {
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            const auto value = tone(n * 48 + static_cast<int>(i), 1000, -7);
            actual[i] = intended[i] = value;
            key[i] = -value;
        }
        if (n == 500)
        {
            c.external = true;
            switched.setControls(c);
        }
        switched.process(actual, {}, key);
        control.process(intended, {}, key);
        REQUIRE(std::abs(switched.meters().reductionDb - control.meters().reductionDb) < 0.001);
    }
}

TEST_CASE("Escucha del detector filtrado y prioridad del bypass")
{
    luma::Controls c;
    c.external = true;
    c.audition = true;
    c.highpassHz = 150;
    c.makeupDb = 24;
    luma::OpticalEngine e;
    e.prepare(sr, c);
    std::vector<float> main(sr), key(sr);
    for (int i = 0; i < sr; ++i)
        key[i] = tone(i, 40, -6);
    e.process(main, {}, key);
    CHECK(e.meters().inputPeakDb == -108);
    CHECK(e.meters().peakDb > -21);
    CHECK(e.meters().peakDb < -15);
    c.bypass = true;
    e.prepare(sr, c);
    std::array<float, 2> dry{0.123f, -0.3f};
    const auto expected = dry;
    e.process(dry);
    CHECK(dry == expected);
}

TEST_CASE("La ruta de 64 bits conserva resolución y rechaza controles no finitos")
{
    luma::Controls c;
    c.reduction = 0;
    c.makeupDb = std::numeric_limits<double>::quiet_NaN();
    c.attackMs = std::numeric_limits<double>::infinity();
    luma::OpticalEngine e;
    e.prepare(std::numeric_limits<double>::quiet_NaN(), c);
    std::array<double, 3> block{0.123456789123, -0.123456789124, 1.0e-18};
    const auto source = block;
    e.process(block);
    CHECK(block == source);
    CHECK(std::isfinite(e.meters().outputVu));
}

TEST_CASE("La energía filtrada iguala la solución discreta exacta de dos polos", "[precision]")
{
    constexpr double tauSeconds = 0.006;
    for (const int rate : {44100, 48000, 96000, 192000})
    {
        const auto weights = luma::math::PowerWeights::at(rate, tauSeconds);
        luma::math::PowerEnvelope detector;
        for (int i = 1; i <= rate / 6; ++i)
        {
            detector.advance(0.25, weights);
            if (i % 37 == 0)
            {
                const double t = static_cast<double>(i) / (rate * tauSeconds);
                const double analytic = 0.25 * (1.0 - (1.0 + t) * std::exp(-t));
                REQUIRE(std::abs(detector.meanSquare - analytic) < 2.0e-13);
            }
        }
        double cell = 0;
        constexpr double attackSeconds = 0.008;
        const int count = static_cast<int>(std::lround(rate * attackSeconds));
        for (int i = 0; i < count; ++i)
            cell += luma::math::sampleWeight(rate, attackSeconds) * (1.0 - cell);
        CHECK(std::abs(cell - (1.0 - std::exp(-count / (rate * attackSeconds)))) < 2.0e-13);
    }
}

TEST_CASE("La rodilla de Precisión une ambas pendientes y conserva los ratios", "[precision]")
{
    CHECK(luma::math::precisionKnee(-3.0) == 0.0);
    CHECK(luma::math::precisionKnee(3.0) == 3.0);
    CHECK(luma::math::precisionKnee(0.0) == 0.46875);
    constexpr double step = 0.002;
    for (int i = -2500; i <= 2500; ++i)
    {
        const double x = static_cast<double>(i) * step;
        const double increment = luma::math::precisionKnee(x + step) - luma::math::precisionKnee(x);
        REQUIRE(increment >= -1.0e-13);
        REQUIRE(increment <= step + 1.0e-13);
    }
    const double approach = 0.05;
    CHECK(luma::math::precisionKnee(-3.0 + approach) < 4.0e-7);
    CHECK(std::abs(luma::math::precisionKnee(3.0 - approach) - (3.0 - approach)) < 4.0e-7);
    luma::Controls c;
    c.precision = true;
    c.reduction = 50;
    CHECK(std::abs(steady(c, 1000, -22) - 0.3125) < 0.001);
    CHECK(std::abs((steady(c, 1000, -4) - steady(c, 1000, -16)) - 8.0) < 0.002);
    c.limit = true;
    CHECK(std::abs((steady(c, 1000, -4) - steady(c, 1000, -16)) - 11.0) < 0.002);
}

namespace
{
double measureDetector(int rate, double frequency, luma::Controls controls)
{
    constexpr int blockSize = 128, settleBlocks = 1024, analysisBlocks = 256;
    luma::OpticalEngine engine;
    controls.precision = true;
    controls.audition = true;
    engine.prepare(rate, controls);
    std::array<double, blockSize> test{};
    double inSin = 0, inCos = 0, outSin = 0, outCos = 0;
    const double omega = 2 * std::numbers::pi * frequency / rate;
    // Read out the fundamental. Since the window does not always hold whole periods,
    // solve the two-component least-squares system instead of using peak or plain RMS.
    double ss = 0, cc = 0, sc = 0;
    for (int block = 0; block < settleBlocks + analysisBlocks; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
            test[i] = 0.25 * std::sin(omega * (block * blockSize + i));
        engine.process(test);
        if (block < settleBlocks)
            continue;
        for (int i = 0; i < blockSize; ++i)
        {
            const double phase = omega * (block * blockSize + i);
            const double s = std::sin(phase), c = std::cos(phase);
            ss += s * s;
            cc += c * c;
            sc += s * c;
            inSin += s * 0.25 * s;
            inCos += s * 0.25 * c;
            outSin += test[i] * s;
            outCos += test[i] * c;
        }
    }
    const auto amplitude = [&](double sinValue, double cosValue)
    {
        return std::hypot(cc * sinValue - sc * cosValue, ss * cosValue - sc * sinValue);
    };
    return 20.0 * std::log10(amplitude(outSin, outCos) / amplitude(inSin, inCos));
}
} // namespace

TEST_CASE("EB-37: el paso alto y el estante del detector coinciden con el diseño", "[precision]")
{
    for (const int rate : {44100, 48000, 96000, 192000})
    {
        luma::Controls c;
        c.highpassHz = 150.0;
        for (const double frequency : {20.0, 150.0, 2000.0})
        {
            const double at = std::tan(std::numbers::pi * frequency / rate);
            const double cut = std::tan(std::numbers::pi * 150.0 / rate);
            const double expected = 20.0 * std::log10(at / std::hypot(at, cut));
            CHECK(std::abs(measureDetector(rate, frequency, c) - expected) < 0.0001);
        }
        c.highpassHz = 0;
        c.emphasisDb = 9;
        CHECK(std::abs(measureDetector(rate, 3500.0, c) - 4.5) < 0.0001);
        CHECK(std::abs(measureDetector(rate, rate * 0.49, c) - 9.0) < 0.002);
    }
}

TEST_CASE("EB-38: Precisión reduce el sesgo del enlace ante señales en cuadratura", "[precision]")
{
    const auto run = [](bool precision, double phase)
    {
        luma::Controls c;
        c.reduction = 75.0;
        c.precision = precision;
        luma::OpticalEngine e;
        e.prepare(sr, c);
        std::array<double, 48> left{}, right{};
        for (int cycle = 0; cycle < 1100; ++cycle)
        {
            for (std::size_t i = 0; i < left.size(); ++i)
            {
                const double t = 2.0 * std::numbers::pi * static_cast<double>(i) / left.size();
                left[i] = 0.32 * std::sin(t);
                right[i] = 0.32 * std::sin(t + phase);
            }
            e.process(left, right);
        }
        const auto m = e.meters();
        REQUIRE(m.reductionLeftDb == m.reductionRightDb);
        return m.reductionDb;
    };
    const auto delta = [&](bool precision)
    { return std::abs(run(precision, 0.0) - run(precision, std::numbers::pi / 2.0)); };
    CHECK(delta(false) > 1.3);
    CHECK(delta(true) < 0.003);
}

TEST_CASE("La automatización del modelo funde los filtros muestra a muestra", "[precision]")
{
    luma::Controls c;
    c.audition = true;
    c.highpassHz = 280;
    c.emphasisDb = 9;
    luma::OpticalEngine classic, precise, switched;
    classic.prepare(sr, c);
    switched.prepare(sr, c);
    c.precision = true;
    precise.prepare(sr, c);
    std::array<double, 113> samplesA{}, samplesB{}, actual{};
    constexpr int change = 35;
    for (int block = 0; block < 85; ++block)
    {
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            const double t = (block * static_cast<double>(actual.size()) + i) / sr;
            const double x = 0.25 * std::sin(tau * 73 * t) + 0.11 * std::sin(tau * 9631 * t);
            samplesA[i] = samplesB[i] = actual[i] = x;
        }
        if (block == change)
            switched.setControls(c);
        classic.process(samplesA);
        precise.process(samplesB);
        switched.process(actual);
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            const double elapsedSamples = (block - change) * static_cast<double>(actual.size()) + i + 1;
            const double blend = -std::expm1(-std::max(0.0, elapsedSamples) / (0.008 * sr));
            REQUIRE(std::abs(actual[i] - std::lerp(samplesA[i], samplesB[i], blend)) < 1.0e-11);
        }
    }
}

TEST_CASE("Ataque dependiente del programa: fórmula exacta y tope", "[precision][0.4.0]")
{
    const double base = luma::math::sampleWeight(sr, 0.008);
    CHECK(luma::math::programAttackWeight(base, 0.0, 1.0) == base);
    CHECK(luma::math::programAttackWeight(base, 24.0, 0.0) == base);
    CHECK(luma::math::programAttackWeight(base, -6.0, 1.0) == base);
    // 12 dB de salto: el doble de rápido = constante de 4 ms.
    CHECK(std::abs(luma::math::programAttackWeight(base, 12.0, 1.0) - luma::math::sampleWeight(sr, 0.004)) < 1.0e-12);
    // Tope: desde 36 dB, cuatro veces más rápido (2 ms).
    CHECK(std::abs(luma::math::programAttackWeight(base, 36.0, 1.0) - luma::math::sampleWeight(sr, 0.002)) < 1.0e-12);
    CHECK(luma::math::programAttackWeight(base, 80.0, 1.0) == luma::math::programAttackWeight(base, 36.0, 1.0));
    // El fundido entre modelos es continuo.
    const double half = luma::math::programAttackWeight(base, 12.0, 0.5);
    CHECK(half > base);
    CHECK(half < luma::math::programAttackWeight(base, 12.0, 1.0));
}

TEST_CASE("Precisión acelera los saltos grandes; Clásico mantiene su ataque", "[precision][0.4.0]")
{
    // Muestras hasta llegar al 63,2 % de la reducción final.
    const auto rise = [](bool precision, double level)
    {
        luma::Controls c;
        c.reduction = 100;
        c.attackMs = 20;
        c.precision = precision;
        const double final = steady(c, 1000, level);
        luma::OpticalEngine e;
        e.prepare(sr, c);
        std::array<float, 8> block{};
        for (int done = 0; done < sr; done += static_cast<int>(block.size()))
        {
            for (std::size_t j = 0; j < block.size(); ++j)
                block[j] = tone(done + static_cast<int>(j), 1000, level);
            e.process(std::span(block));
            if (e.meters().reductionDb >= 0.632 * final)
                return done + static_cast<int>(block.size());
        }
        return sr;
    };
    // Umbral -42 dBFS: -33 dBFS pide unos 6 dB de reducción; -6 dBFS pide unos 24 dB.
    const double classicRatio = static_cast<double>(rise(false, -6)) / rise(false, -33);
    const double precisionRatio = static_cast<double>(rise(true, -6)) / rise(true, -33);
    INFO("Clásico grande/chico " << classicRatio << " · Precisión grande/chico " << precisionRatio);
    CHECK(precisionRatio < classicRatio * 0.8);
}

TEST_CASE("Ganancia automática devuelve la referencia de -18 dBFS RMS a su nivel", "[0.4.0]")
{
    const double inputDb = luma::OpticalEngine::referencePeakDbfs; // pico de un seno de -18 dBFS RMS
    for (const bool precision : {false, true})
        for (const bool limit : {false, true})
            for (const double depth : {30.0, 60.0, 90.0})
            {
                luma::Controls c;
                c.reduction = depth;
                c.precision = precision;
                c.limit = limit;
                const auto outputChange = [&](bool automatic)
                {
                    c.autoMakeup = automatic;
                    luma::OpticalEngine e;
                    e.prepare(sr, c);
                    feed(e, 2 * sr, 1000, inputDb);
                    std::vector<float> block(static_cast<std::size_t>(sr / 10));
                    double in = 0, out = 0;
                    for (std::size_t j = 0; j < block.size(); ++j)
                    {
                        block[j] = tone(2 * sr + static_cast<int>(j), 1000, inputDb);
                        in += static_cast<double>(block[j]) * block[j];
                    }
                    e.process(std::span(block));
                    for (const float y : block)
                        out += static_cast<double>(y) * y;
                    return 10.0 * std::log10(out / in);
                };
                const double reduction = luma::OpticalEngine::referenceReductionDb(c);
                INFO("Precisión " << precision << " · Limit " << limit << " · Reducción " << depth << " % · "
                                  << reduction << " dB");
                CHECK(std::abs(steady(c, 1000, inputDb) - reduction) < 0.15);
                CHECK(std::abs(outputChange(true)) < 0.1);
                CHECK(std::abs(outputChange(false) + reduction) < 0.15);
            }
}

TEST_CASE("Ganancia automática apagada no cambia nada y sigue a Reducción sin saltos", "[0.4.0]")
{
    luma::Controls c;
    c.reduction = 70;
    CHECK(luma::OpticalEngine::referenceReductionDb(c) > 6.0);
    c.reduction = 0;
    CHECK(luma::OpticalEngine::referenceReductionDb(c) == 0.0);
    c.reduction = 70;
    c.rangeDb = 2.5;
    CHECK(std::abs(luma::OpticalEngine::referenceReductionDb(c) - 2.5) < 1.0e-12);
}
