// Standalone: c++ -std=c++17 -O3 -ffast-math -UNDEBUG src/OutputLoudnessTest.cpp -o /tmp/output-loudness-test
#include "OutputLoudness.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using pipedal::OutputLoudness;
constexpr double pi = 3.14159265358979323846;

static std::vector<float> Tone(unsigned rate, double hz, double amplitude, unsigned seconds) {
    std::vector<float> result(rate * seconds);
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = float(amplitude * std::sin(2 * pi * hz * i / rate));
    return result;
}

static void Feed(OutputLoudness& meter, const std::vector<float>& l,
                 const std::vector<float>* r = nullptr, size_t block = 127) {
    for (size_t i = 0; i < l.size(); i += block)
        meter.Process(l.data() + i, r ? r->data() + i : nullptr, std::min(block, l.size() - i));
}

int main() {
    for (unsigned rate : {44100u, 48000u, 96000u}) {
        OutputLoudness meter;
        meter.Prepare(rate);
        assert(meter.ShortTermLufs() == OutputLoudness::Silence);
        auto tone = Tone(rate, 1000, .1, 6);
        const auto original = tone;
        Feed(meter, tone);
        const float mono = meter.ShortTermLufs();
        // BS.1770: -20 dBFS peak, 1 kHz mono sine is approximately -23 LUFS.
        assert(std::abs(mono + 23.0f) < .06f);
        assert(tone == original);
        OutputLoudness whole; whole.Prepare(rate);
        Feed(whole, tone, nullptr, tone.size());
        assert(std::abs(whole.ShortTermLufs() - mono) < 1e-5);
        Feed(meter, tone, &tone);
        assert(std::abs(meter.ShortTermLufs() - mono - 3.0103f) < .001f);
        auto inverted = tone;
        for (float& v : inverted) v = -v;
        Feed(meter, tone, &inverted);
        assert(std::abs(meter.ShortTermLufs() - mono - 3.0103f) < .001f);
        meter.Prepare(rate);
        Feed(meter, tone);
        // Model a preset changing gain without resetting the host analyser.
        auto quiet = Tone(rate, 1000, .05, 1);
        Feed(meter, quiet);
        assert(std::abs(meter.ShortTermLufs() - mono - 10 * std::log10(.75)) < .01);
        Feed(meter, quiet); Feed(meter, quiet);
        assert(std::abs(meter.ShortTermLufs() - mono + 6.0206f) < .01);
        meter.Process(nullptr, nullptr, rate * 4);
        assert(meter.ShortTermLufs() == OutputLoudness::Silence);
        // Construct runtime bit patterns: fast-math compilers may reject
        // literal floating-point infinities before our input guard sees them.
        volatile uint32_t invalidBits[] = {0x7fc00000u, 0x7f800000u, 0xff800000u};
        std::vector<float> invalid(3);
        for (size_t i = 0; i < invalid.size(); ++i) {
            uint32_t bits = invalidBits[i];
            std::memcpy(&invalid[i], &bits, sizeof(bits));
        }
        Feed(meter, invalid);
        Feed(meter, tone);
        assert(std::abs(meter.ShortTermLufs() - mono) < .01);
        meter.Prepare(rate);
        assert(meter.ShortTermLufs() == OutputLoudness::Silence);
        // Reference figures independently checked with FFmpeg's ebur128 filter.
        const double references[] = {-26.6, rate == 96000 ? -24.9 : -24.8, -19.7};
        size_t referenceIndex = 0;
        for (double hz : {60., 100., 10000.}) {
            meter.Prepare(rate);
            Feed(meter, Tone(rate, hz, .1, 6));
            assert(std::abs(meter.ShortTermLufs() - references[referenceIndex++]) < .051);
            std::cout << rate << " Hz, " << hz << " Hz tone: " << meter.ShortTermLufs() << " LUFS\n";
        }
    }
    std::cout << "PASS output loudness: calibration, stereo energy/phase, block boundaries, continuous preset changes, silence, invalid samples, reset.\n";
}
