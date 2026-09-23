// Compile with: c++ -std=c++20 -O2 -pthread -Isrc test/builtin_tuner_smoke.cpp
#include "BuiltInTuner.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <thread>

int main()
{
    uint64_t sample = 0;
    std::atomic<bool> inputAvailable = true;
    pipedal::BuiltInTuner tuner([&](uint64_t *, float *values, size_t capacity, uint32_t *rate) {
        if (!inputAvailable.load()) { *rate = 0; return size_t(0); }
        *rate = 48000;
        const size_t count = std::min<size_t>(capacity, 2048);
        for (size_t i = 0; i < count; ++i, ++sample)
            values[i] = 0.2f * std::sin(2.0 * 3.141592653589793 * 110.0 * sample / 48000.0);
        return count;
    });
    pipedal::GpioTunerFrame frame;
    for (int i = 0; i < 80; ++i)
    {
        frame = tuner.GetFrame();
        if (frame.Locked()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    assert(frame.Locked() && frame.note == 45 && std::abs(frame.cents) < 5);
    inputAvailable = false;
    for (int i = 0; i < 40 && tuner.GetFrame().note != -1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    assert(tuner.GetFrame().note == -1);
    inputAvailable = true;
    for (int i = 0; i < 80; ++i)
    {
        frame = tuner.GetFrame();
        if (frame.Locked()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    assert(frame.Locked());
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    assert(tuner.GetFrame().note == -1);
}
