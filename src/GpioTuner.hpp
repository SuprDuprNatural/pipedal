// Copyright (c) 2026 SuprPedals contributors
// SPDX-License-Identifier: MIT
//
// Bass-first pitch and strobe analysis for PiPedal's small hardware display.
// Adapted from SuprPedals TunerDsp. Unlike the LV2 wrapper, this class only
// consumes a copy of the input stream and never participates in the audio
// signal path.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace pipedal
{
    struct GpioTunerFrame
    {
        float frequency = 0.0f;
        int32_t note = -1;
        float cents = 0.0f;
        float confidence = 0.0f;
        float strobePhase = 0.0f;

        bool Locked() const
        {
            return note >= 0 && confidence > 0.55f;
        }

        std::string NoteName() const
        {
            static constexpr const char *names[]{
                "C", "C#", "D", "D#", "E", "F",
                "F#", "G", "G#", "A", "A#", "B"};
            if (note < 0)
                return "";
            return names[(note % 12 + 12) % 12];
        }
    };

    namespace gpio_tuner_detail
    {
        inline float Clamp(float value, float minimum, float maximum)
        {
            return value < minimum ? minimum : (value > maximum ? maximum : value);
        }

        struct Biquad
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
            float a1 = 0.0f, a2 = 0.0f;
            float z1 = 0.0f, z2 = 0.0f;

            void Reset() { z1 = z2 = 0.0f; }

            void SetLowpass(float sampleRate, float cutoff, float q)
            {
                constexpr float pi = 3.14159265358979323846f;
                const float w = 2.0f * pi * cutoff / sampleRate;
                const float cosine = std::cos(w);
                const float alpha = std::sin(w) / (2.0f * q);
                const float a0 = 1.0f + alpha;
                b0 = ((1.0f - cosine) * 0.5f) / a0;
                b1 = (1.0f - cosine) / a0;
                b2 = b0;
                a1 = (-2.0f * cosine) / a0;
                a2 = (1.0f - alpha) / a0;
            }

            float Process(float input)
            {
                const float output = b0 * input + z1;
                z1 = b1 * input - a1 * output + z2;
                z2 = b2 * input - a2 * output;
                return output;
            }
        };

        struct DcBlocker
        {
            float r = 0.999f;
            float x1 = 0.0f;
            float y1 = 0.0f;

            void Set(float sampleRate, float cutoff)
            {
                constexpr float pi = 3.14159265358979323846f;
                r = 1.0f - 2.0f * pi * cutoff / sampleRate;
            }
            void Reset() { x1 = y1 = 0.0f; }
            float Process(float input)
            {
                const float output = input - x1 + r * y1;
                x1 = input;
                y1 = output;
                return output;
            }
        };

        struct EnvelopeFollower
        {
            float attack = 0.01f;
            float release = 0.0005f;
            float value = 0.0f;

            void Set(float sampleRate, float attackMs, float releaseMs)
            {
                attack = 1.0f - std::exp(-1.0f / (sampleRate * attackMs * 0.001f));
                release = 1.0f - std::exp(-1.0f / (sampleRate * releaseMs * 0.001f));
            }
            void Reset() { value = 0.0f; }
            float Process(float input)
            {
                const float magnitude = std::fabs(input);
                value += (magnitude - value) * (magnitude > value ? attack : release);
                return value;
            }
        };
    }

    class GpioTunerAnalyzer
    {
    public:
        static constexpr float MinimumFrequency = 18.0f;
        static constexpr float MaximumFrequency = 500.0f;

        void Initialize(uint32_t sampleRate)
        {
            sampleRate_ = std::max(8000.0f, static_cast<float>(sampleRate));
            decimation_ = std::max(1, static_cast<int>(std::lround(sampleRate_ / 6000.0f)));
            analysisRate_ = sampleRate_ / static_cast<float>(decimation_);

            const float cutoff = std::min(1200.0f, analysisRate_ * 0.205f);
            antiAlias1_.SetLowpass(sampleRate_, cutoff, 0.5411961f);
            antiAlias2_.SetLowpass(sampleRate_, cutoff, 1.3065630f);
            dcBlock_.Set(sampleRate_, 8.0f);

            levelAttack_ = 1.0f - std::exp(-1.0f / (sampleRate_ * 0.010f));
            levelRelease_ = 1.0f - std::exp(-1.0f / (sampleRate_ * 0.350f));
            fastEnvelope_.Set(sampleRate_, 1.5f, 18.0f);
            slowEnvelope_.Set(sampleRate_, 35.0f, 300.0f);
            Reset();
        }

        void Reset()
        {
            antiAlias1_.Reset();
            antiAlias2_.Reset();
            dcBlock_.Reset();
            fastEnvelope_.Reset();
            slowEnvelope_.Reset();
            ring_.fill(0.0f);
            analysis_.fill(0.0f);
            nsdf_.fill(0.0f);
            recentMidi_.fill(0.0f);

            writeIndex_ = 0;
            validSamples_ = 0;
            decimationCounter_ = 0;
            hopCounter_ = 0;
            midiCount_ = 0;
            midiIndex_ = 0;
            jumpCandidateMidi_ = 0.0f;
            jumpCount_ = 0;
            levelSquare_ = 0.0f;
            frame_ = {};
            filteredMidi_ = 0.0f;
            hasPitch_ = false;
            samplesSincePitch_ = static_cast<uint64_t>(sampleRate_);
            attackState_ = false;
            strobeRate_ = 0.0f;
            lastStrobeNote_ = -1;
        }

        void Process(const float *input, size_t sampleCount)
        {
            if (!input || sampleRate_ <= 0.0f)
                return;

            for (size_t i = 0; i < sampleCount; ++i)
            {
                const float value = input[i];
                const float square = value * value;
                levelSquare_ += (square - levelSquare_) *
                                (square > levelSquare_ ? levelAttack_ : levelRelease_);
                const float magnitude = std::fabs(value);
                const float fast = fastEnvelope_.Process(magnitude);
                const float slow = slowEnvelope_.Process(magnitude);

                const bool aboveGate = levelSquare_ > attackGatePower_;
                if (!attackState_ && aboveGate && fast > slow * 1.85f && fast > 1.0e-5f)
                {
                    attackState_ = true;
                    ClearAnalysisWindow();
                }
                else if (attackState_ && fast < slow * 1.20f)
                {
                    attackState_ = false;
                }

                float filtered = dcBlock_.Process(value);
                filtered = antiAlias2_.Process(antiAlias1_.Process(filtered));
                if (++decimationCounter_ >= decimation_)
                {
                    decimationCounter_ = 0;
                    PushAnalysisSample(filtered);
                }

                if (samplesSincePitch_ < static_cast<uint64_t>(sampleRate_ * 4.0f))
                    ++samplesSincePitch_;

                if (hasPitch_)
                {
                    frame_.strobePhase += strobeRate_ / sampleRate_;
                    if (frame_.strobePhase >= 1.0f)
                        frame_.strobePhase -= 1.0f;
                    else if (frame_.strobePhase < 0.0f)
                        frame_.strobePhase += 1.0f;
                }
            }

            if (samplesSincePitch_ > static_cast<uint64_t>(sampleRate_ * 0.28f))
            {
                hasPitch_ = false;
                frame_.note = -1;
                frame_.frequency = 0.0f;
                frame_.cents = 0.0f;
                frame_.confidence *= 0.85f;
                strobeRate_ = 0.0f;
            }
        }

        const GpioTunerFrame &Frame() const { return frame_; }
        float AnalysisRate() const { return analysisRate_; }

    private:
        static constexpr int RingSize = 1536;
        static constexpr int WindowSize = 1152;
        static constexpr int MaximumLag = 384;
        static constexpr int HopSize = 96;
        static constexpr int MedianSize = 5;

        static float DbToPower(float db)
        {
            return std::pow(10.0f, db * 0.1f);
        }

        void ClearAnalysisWindow()
        {
            writeIndex_ = 0;
            validSamples_ = 0;
            hopCounter_ = 0;
            midiCount_ = 0;
            midiIndex_ = 0;
            jumpCount_ = 0;
        }

        void PushAnalysisSample(float value)
        {
            ring_[writeIndex_] = value;
            writeIndex_ = (writeIndex_ + 1) % RingSize;
            validSamples_ = std::min(validSamples_ + 1, RingSize);
            if (++hopCounter_ >= HopSize)
            {
                hopCounter_ = 0;
                Analyze();
            }
        }

        float InterpolatedLag(int lag) const
        {
            const float left = nsdf_[lag - 1];
            const float centre = nsdf_[lag];
            const float right = nsdf_[lag + 1];
            const float denominator = left - 2.0f * centre + right;
            if (std::fabs(denominator) < 1.0e-9f)
                return static_cast<float>(lag);
            const float offset = 0.5f * (left - right) / denominator;
            return static_cast<float>(lag) +
                   gpio_tuner_detail::Clamp(offset, -0.5f, 0.5f);
        }

        static float Median(std::array<float, MedianSize> values, int count)
        {
            count = std::max(1, std::min(count, MedianSize));
            for (int i = 1; i < count; ++i)
            {
                const float value = values[i];
                int j = i;
                while (j > 0 && values[j - 1] > value)
                {
                    values[j] = values[j - 1];
                    --j;
                }
                values[j] = value;
            }
            return values[count / 2];
        }

        void AcceptPitch(float rawFrequency, float clarity)
        {
            const float rawMidi =
                69.0f + 12.0f * std::log2(rawFrequency / reference_);

            if (hasPitch_ && std::fabs(rawMidi - filteredMidi_) > 7.0f)
            {
                if (jumpCount_ == 0 ||
                    std::fabs(rawMidi - jumpCandidateMidi_) > 0.55f)
                {
                    jumpCandidateMidi_ = rawMidi;
                    jumpCount_ = 1;
                    return;
                }
                if (++jumpCount_ < 2)
                    return;
                midiCount_ = 0;
                midiIndex_ = 0;
                filteredMidi_ = rawMidi;
            }
            else
            {
                jumpCount_ = 0;
            }

            recentMidi_[midiIndex_] = rawMidi;
            midiIndex_ = (midiIndex_ + 1) % MedianSize;
            midiCount_ = std::min(midiCount_ + 1, MedianSize);
            const float stableMidi = Median(recentMidi_, midiCount_);
            if (!hasPitch_ || midiCount_ == 1)
                filteredMidi_ = stableMidi;
            else
            {
                const float delta = stableMidi - filteredMidi_;
                filteredMidi_ += (std::fabs(delta) > 0.60f ? 0.72f : 0.30f) * delta;
            }

            const int32_t note = std::max(
                0, std::min(127, static_cast<int>(std::lround(filteredMidi_))));
            if (note != lastStrobeNote_)
            {
                frame_.strobePhase = 0.0f;
                lastStrobeNote_ = note;
            }

            frame_.note = note;
            frame_.cents = gpio_tuner_detail::Clamp(
                (filteredMidi_ - static_cast<float>(note)) * 100.0f, -50.0f, 50.0f);
            frame_.frequency =
                reference_ * std::pow(2.0f, (filteredMidi_ - 69.0f) / 12.0f);
            frame_.confidence +=
                (gpio_tuner_detail::Clamp(clarity, 0.0f, 1.0f) -
                 frame_.confidence) *
                0.45f;
            samplesSincePitch_ = 0;
            hasPitch_ = true;

            const float target =
                reference_ * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
            float octaveDivisor = 1.0f;
            float normalizedTarget = target;
            while (normalizedTarget >= 60.0f)
            {
                normalizedTarget *= 0.5f;
                octaveDivisor *= 2.0f;
            }
            while (normalizedTarget < 30.0f)
            {
                normalizedTarget *= 2.0f;
                octaveDivisor *= 0.5f;
            }
            strobeRate_ = (frame_.frequency - target) / octaveDivisor;
        }

        void Analyze()
        {
            if (levelSquare_ < gatePower_)
            {
                frame_.confidence *= 0.78f;
                return;
            }

            const int count = std::min(validSamples_, WindowSize);
            if (count < 256)
                return;

            int start = writeIndex_ - count;
            if (start < 0)
                start += RingSize;

            float mean = 0.0f;
            for (int i = 0; i < count; ++i)
            {
                const float value = ring_[(start + i) % RingSize];
                analysis_[i] = value;
                mean += value;
            }
            mean /= static_cast<float>(count);

            float energy = 0.0f;
            for (int i = 0; i < count; ++i)
            {
                analysis_[i] -= mean;
                energy += analysis_[i] * analysis_[i];
            }
            if (energy < 1.0e-12f)
                return;

            const int minimumLag = std::max(
                2, static_cast<int>(std::floor(analysisRate_ / MaximumFrequency)));
            const int maximumLag = std::min(
                {MaximumLag - 1,
                 static_cast<int>(std::ceil(analysisRate_ / MinimumFrequency)),
                 (count - 2) / 3});
            if (maximumLag <= minimumLag + 2)
                return;

            nsdf_.fill(-1.0f);
            for (int lag = minimumLag - 1; lag <= maximumLag + 1; ++lag)
            {
                double correlation = 0.0;
                double divisor = 0.0;
                const int pairs = count - lag;
                for (int i = 0; i < pairs; ++i)
                {
                    const float a = analysis_[i];
                    const float b = analysis_[i + lag];
                    correlation += static_cast<double>(a) * b;
                    divisor += static_cast<double>(a) * a +
                               static_cast<double>(b) * b;
                }
                nsdf_[lag] =
                    divisor > 1.0e-18 ? static_cast<float>(2.0 * correlation / divisor)
                                      : -1.0f;
            }

            float globalPeak = -1.0f;
            int globalLag = -1;
            for (int lag = minimumLag; lag <= maximumLag; ++lag)
            {
                if (nsdf_[lag] > 0.0f &&
                    nsdf_[lag] >= nsdf_[lag - 1] &&
                    nsdf_[lag] > nsdf_[lag + 1] &&
                    nsdf_[lag] > globalPeak)
                {
                    globalPeak = nsdf_[lag];
                    globalLag = lag;
                }
            }
            if (globalLag < 0 || globalPeak < 0.72f)
            {
                frame_.confidence *= 0.82f;
                return;
            }

            const float keyThreshold = std::max(0.76f, globalPeak * 0.93f);
            int chosenLag = globalLag;
            for (int lag = minimumLag; lag <= maximumLag; ++lag)
            {
                if (nsdf_[lag] >= keyThreshold &&
                    nsdf_[lag] >= nsdf_[lag - 1] &&
                    nsdf_[lag] > nsdf_[lag + 1])
                {
                    chosenLag = lag;
                    break;
                }
            }

            const float frequency = analysisRate_ / InterpolatedLag(chosenLag);
            if (frequency >= MinimumFrequency && frequency <= MaximumFrequency)
                AcceptPitch(frequency, nsdf_[chosenLag]);
        }

        float sampleRate_ = 0.0f;
        int decimation_ = 8;
        float analysisRate_ = 6000.0f;
        float reference_ = 440.0f;
        float gatePower_ = DbToPower(-65.0f);
        float attackGatePower_ = DbToPower(-68.0f);

        gpio_tuner_detail::Biquad antiAlias1_;
        gpio_tuner_detail::Biquad antiAlias2_;
        gpio_tuner_detail::DcBlocker dcBlock_;
        gpio_tuner_detail::EnvelopeFollower fastEnvelope_;
        gpio_tuner_detail::EnvelopeFollower slowEnvelope_;
        float levelAttack_ = 0.002f;
        float levelRelease_ = 0.00005f;
        float levelSquare_ = 0.0f;

        std::array<float, RingSize> ring_{};
        std::array<float, WindowSize> analysis_{};
        std::array<float, MaximumLag + 2> nsdf_{};
        int writeIndex_ = 0;
        int validSamples_ = 0;
        int decimationCounter_ = 0;
        int hopCounter_ = 0;

        std::array<float, MedianSize> recentMidi_{};
        int midiCount_ = 0;
        int midiIndex_ = 0;
        float filteredMidi_ = 0.0f;
        float jumpCandidateMidi_ = 0.0f;
        int jumpCount_ = 0;

        GpioTunerFrame frame_;
        bool hasPitch_ = false;
        uint64_t samplesSincePitch_ = 0;
        bool attackState_ = false;
        float strobeRate_ = 0.0f;
        int32_t lastStrobeNote_ = -1;
    };
}
