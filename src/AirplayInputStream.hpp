// Copyright (c) 2026 SuprDuprNatural
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include "RingBuffer.hpp"

namespace pipedal
{
    // Streams AirPlay audio from shairport-sync into the realtime audio thread.
    //
    // shairport-sync plays into an ALSA plug device (written by pipedaladmind)
    // that converts to the current device sample rate and writes raw S16_LE
    // stereo PCM to a named pipe. A reader thread converts the PCM to float
    // and buffers it in a ring buffer; the realtime thread pulls it out of the
    // ring buffer and adds it to the main output buffers.
    class AirplayInputStream
    {
    public:
        using ptr = std::shared_ptr<AirplayInputStream>;

        AirplayInputStream(const std::filesystem::path &fifoPath, uint32_t sampleRate);
        ~AirplayInputStream();

        AirplayInputStream(const AirplayInputStream &) = delete;
        AirplayInputStream &operator=(const AirplayInputStream &) = delete;

        void Start(); // start the FIFO reader thread. Idempotent.
        void Stop();  // stop the FIFO reader thread. Idempotent.

        void SetVolume(float volume); // 0..1, any non-realtime thread.

        // Realtime thread only: add buffered stream audio to the output buffers.
        void MixOutput(std::vector<float *> &outputBuffers, size_t nFrames);

    private:
        static constexpr size_t FRAME_BYTES = 2 * sizeof(float); // stereo float in the ring buffer.
        static constexpr size_t RING_BUFFER_BYTES = 1024 * 1024; // ~2.7s at 48000.
        static constexpr size_t READ_BYTES = 16 * 1024;          // FIFO read chunk (S16_LE stereo).
        static constexpr size_t MIX_CHUNK_FRAMES = 1024;

        void ThreadProc();
        void OpenFifo();
        void CloseFifo();
        void PushSamples(const uint8_t *data, size_t length);
        void DiscardRing();

        std::filesystem::path fifoPath;
        uint32_t sampleRate;

        RingBuffer<false, false> ringBuffer;

        std::atomic<float> targetGain = 0;
        std::atomic<bool> streamRunning = false;

        // realtime-thread state.
        bool primed = false;
        float currentGain = 0;
        float gainSmoothingCoefficient = 0;
        size_t primeFrames = 0;
        std::vector<float> mixBuffer;

        // reader-thread state.
        size_t highWaterBytes = 0;
        bool dropping = false;
        std::vector<uint8_t> readBuffer;
        std::vector<float> convertBuffer;
        size_t carryBytes = 0;
        uint8_t carryBuffer[4];

        std::unique_ptr<std::thread> thread;
        int fd = -1;
        int cancelWrite = -1;
        int cancelRead = -1;
    };

} // namespace pipedal
