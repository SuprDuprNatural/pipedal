// Short-term output loudness (BS.1770 K-weighting, ungated 3 s window).
// Owned by AudioHost, not a pedalboard or VU subscription. No audio is modified.
// MIT license, (c) 2026 SuprPedals contributors.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pipedal {
class OutputLoudness {
public:
    static constexpr float Silence = -120.0f; // finite on the JSON wire

    void Prepare(uint32_t sampleRate) {
        *this = OutputLoudness{};
        blockFrames_ = std::max<uint32_t>(1, (sampleRate + 5) / 10);
        const double rate = std::max<uint32_t>(8000, sampleRate);
        // BS.1770 shelf and RLB high-pass; bilinear coefficients for the
        // device rate. Parameterisation also used by libebur128.
        constexpr double pi = 3.14159265358979323846;
        double k = std::tan(pi * 1681.974450955533 / rate);
        const double q = 0.7071752369554196;
        const double vh = std::pow(10.0, 3.999843853973347 / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        double d = 1 + k / q + k * k;
        Biquad shelf{(vh + vb * k / q + k * k) / d,
                     2 * (k * k - vh) / d,
                     (vh - vb * k / q + k * k) / d,
                     2 * (k * k - 1) / d, (1 - k / q + k * k) / d};
        k = std::tan(pi * 38.13547087602444 / rate);
        d = 1 + k / 0.5003270373238773 + k * k;
        Biquad highPass{1, -2, 1, 2 * (k * k - 1) / d,
                       (1 - k / 0.5003270373238773 + k * k) / d};
        shelves_.fill(shelf);
        highPasses_.fill(highPass);
    }

    // Null channels are silence. Sum channel energies, never waveforms:
    // a stereo phase inversion must not disappear from a loudness reading.
    void Process(const float* left, const float* right, size_t frames) {
        for (size_t i = 0; i < frames; ++i) {
            const float input[2] = {left ? left[i] : 0, right ? right[i] : 0};
            for (size_t c = 0; c < 2; ++c) {
                // AudioHost uses -ffast-math. Inspect bits so a broken plugin's
                // NaN/Inf cannot poison the filters even in a release build.
                uint32_t bits;
                std::memcpy(&bits, &input[c], sizeof(bits));
                double x = (bits & 0x7f800000u) == 0x7f800000u ? 0 : input[c];
                double y = highPasses_[c].Tick(shelves_[c].Tick(x));
                blockEnergy_ += y * y;
            }
            if (++framesInBlock_ == blockFrames_) {
                energies_[nextBlock_] = blockEnergy_;
                nextBlock_ = (nextBlock_ + 1) % energies_.size();
                // Re-sum 30 blocks to avoid accumulated subtraction error
                // leaving a residual reading after prolonged silence.
                double total = 0;
                for (double energy : energies_) total += energy;
                double mean = total / (blockFrames_ * double(energies_.size()));
                lufs_ = mean > 1e-12 ? float(-0.691 + 10 * std::log10(mean)) : Silence;
                framesInBlock_ = 0;
                blockEnergy_ = 0;
                for (auto& filter : shelves_) filter.FlushTinyState();
                for (auto& filter : highPasses_) filter.FlushTinyState();
            }
        }
    }

    float ShortTermLufs() const { return lufs_; }

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        double Tick(double x) {
            double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void FlushTinyState() {
            if (std::abs(z1) < 1e-30) z1 = 0;
            if (std::abs(z2) < 1e-30) z2 = 0;
        }
    };
    std::array<Biquad, 2> shelves_{}, highPasses_{};
    std::array<double, 30> energies_{};
    size_t nextBlock_ = 0;
    uint32_t blockFrames_ = 4800, framesInBlock_ = 0;
    double blockEnergy_ = 0;
    float lufs_ = Silence;
};
} // namespace pipedal
