// Copyright (c) 2026 SuprPedals contributors
// SPDX-License-Identifier: MIT
// The web tuner reads the same main-input copy and analyzer as the OLED tuner.
#pragma once

#include "GpioTuner.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace pipedal
{
    class BuiltInTuner
    {
    public:
        using SampleProvider = std::function<size_t(
            uint64_t *, float *, size_t, uint32_t *)>;

        explicit BuiltInTuner(SampleProvider provider)
            : provider_(std::move(provider)), worker_([this](std::stop_token stop) { Run(stop); }) {}

        ~BuiltInTuner()
        {
            worker_.request_stop();
            worker_.join();
        }

        GpioTunerFrame GetFrame()
        {
            requestedAt_.store(NowMs(), std::memory_order_relaxed);
            std::lock_guard lock(frameMutex_);
            return frame_;
        }

    private:
        static int64_t NowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        void Run(std::stop_token stop)
        {
            GpioTunerAnalyzer analyzer;
            uint64_t readIndex = std::numeric_limits<uint64_t>::max();
            uint32_t currentRate = 0;
            bool active = false;
            int64_t lastSampleAt = 0;
            std::array<float, 4096> samples{};
            while (!stop.stop_requested())
            {
                const bool wanted = NowMs() - requestedAt_.load(std::memory_order_relaxed) < 1000;
                if (!wanted && active)
                {
                    active = false;
                    currentRate = 0;
                    readIndex = std::numeric_limits<uint64_t>::max();
                    std::lock_guard lock(frameMutex_);
                    frame_ = {};
                }
                if (wanted)
                {
                    active = true;
                    bool receivedSamples = false;
                    for (int i = 0; i < 12 && !stop.stop_requested(); ++i)
                    {
                        uint32_t sampleRate = 0;
                        size_t count = provider_(&readIndex, samples.data(), samples.size(), &sampleRate);
                        if (!count || !sampleRate) break;
                        receivedSamples = true;
                        if (sampleRate != currentRate)
                        {
                            analyzer.Initialize(sampleRate);
                            currentRate = sampleRate;
                        }
                        analyzer.Process(samples.data(), count);
                        if (count < samples.size()) break;
                    }
                    if (receivedSamples) lastSampleAt = NowMs();
                    if (currentRate && NowMs() - lastSampleAt > 500)
                    {
                        currentRate = 0;
                        readIndex = std::numeric_limits<uint64_t>::max();
                    }
                    std::lock_guard lock(frameMutex_);
                    frame_ = currentRate ? analyzer.Frame() : GpioTunerFrame{};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        }

        SampleProvider provider_;
        std::atomic<int64_t> requestedAt_ = 0;
        std::mutex frameMutex_;
        GpioTunerFrame frame_;
        std::jthread worker_;
    };
}
