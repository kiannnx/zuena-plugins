// Prints 30 Hz telemetry from the native DSP for the launcher presentation.
#include "OpticalEngine.h"
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numbers>

int main()
{
    luma::Controls c;
    c.reduction = 60.0;
    luma::OpticalEngine engine;
    engine.prepare(48000, c);
    std::array<float, 1600> block{};
    std::cout << std::fixed << std::setprecision(3) << "[";
    for (int frame = 0; frame < 360; ++frame)
    {
        for (std::size_t i = 0; i < block.size(); ++i)
        {
            const auto sample = static_cast<double>(frame) * 1600 + static_cast<double>(i);
            const auto t = sample / 48000.0;
            const bool burst = (t >= 1 && t < 1.20) || (t >= 2 && t < 2.20) || (t >= 3 && t < 3.20);
            const bool held = (t >= 4.5 && t < 7.5) || (t >= 9.0 && t < 9.5);
            const double amp = (burst || held) ? 0.48 : 0.025;
            block[i] = static_cast<float>(amp * std::sin(2 * std::numbers::pi * 220 * t));
        }
        engine.process(block);
        const auto m = engine.meters();
        if (frame)
            std::cout << ",";
        std::cout << "[" << m.inputVu << "," << m.outputVu << "," << m.reductionDb << "]";
    }
    std::cout << "]\n";
}
