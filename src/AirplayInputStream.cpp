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

#include "pch.h"
#include "AirplayInputStream.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Lv2Log.hpp"
#include "ss.hpp"
#include "util.hpp"

using namespace pipedal;
namespace fs = std::filesystem;

AirplayInputStream::AirplayInputStream(const fs::path &fifoPath, uint32_t sampleRate)
    : fifoPath(fifoPath),
      sampleRate(sampleRate),
      ringBuffer(RING_BUFFER_BYTES)
{
    // ~100ms of buffered audio before playback starts; absorbs FIFO scheduling jitter.
    primeFrames = sampleRate / 10;
    // stop buffering (and skip ahead) if more than ~1s accumulates due to clock drift.
    highWaterBytes = sampleRate * FRAME_BYTES;
    // ~20ms volume smoothing.
    gainSmoothingCoefficient = 1.0f - std::exp(-1.0f / (0.02f * sampleRate));

    mixBuffer.resize(MIX_CHUNK_FRAMES * 2);
    readBuffer.resize(READ_BYTES);
    convertBuffer.resize(READ_BYTES / sizeof(int16_t) + 2);
}

AirplayInputStream::~AirplayInputStream()
{
    Stop();
}

void AirplayInputStream::SetVolume(float volume)
{
    volume = std::clamp(volume, 0.0f, 1.0f);
    // square-law taper so the slider feels roughly linear in loudness.
    targetGain = volume * volume;
}

void AirplayInputStream::Start()
{
    if (thread)
    {
        return;
    }
    OpenFifo();

    int cancelPipe[2];
    if (pipe(cancelPipe) == -1)
    {
        CloseFifo();
        throw std::runtime_error("AirplayInputStream: can't create cancel pipe.");
    }
    this->cancelRead = cancelPipe[0];
    this->cancelWrite = cancelPipe[1];

    carryBytes = 0;
    dropping = false;
    this->thread = std::make_unique<std::thread>([this]() { ThreadProc(); });
    streamRunning = true;
}

void AirplayInputStream::Stop()
{
    streamRunning = false;
    if (thread)
    {
        char buff[1] = {0};
        ssize_t nWritten = write(cancelWrite, buff, 1);
        (void)nWritten;
        thread->join();
        thread = nullptr;
    }
    if (cancelWrite != -1)
    {
        close(cancelWrite);
        cancelWrite = -1;
    }
    if (cancelRead != -1)
    {
        close(cancelRead);
        cancelRead = -1;
    }
    CloseFifo();
    // remaining ring buffer content is discarded on the realtime thread.
}

void AirplayInputStream::OpenFifo()
{
    if (!fs::exists(fifoPath))
    {
        if (mkfifo(fifoPath.c_str(), 0660) != 0)
        {
            throw std::runtime_error(SS("AirplayInputStream: can't create FIFO " << fifoPath));
        }
    }
    else if (!fs::is_fifo(fifoPath))
    {
        throw std::runtime_error(SS("AirplayInputStream: " << fifoPath << " exists but is not a FIFO."));
    }

    // non-blocking: opens successfully even when shairport-sync isn't writing yet.
    fd = open(fifoPath.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd == -1)
    {
        throw std::runtime_error(SS("AirplayInputStream: can't open FIFO " << fifoPath));
    }
}

void AirplayInputStream::CloseFifo()
{
    if (fd != -1)
    {
        close(fd);
        fd = -1;
    }
}

void AirplayInputStream::PushSamples(const uint8_t *data, size_t length)
{
    // FIFO reads aren't necessarily frame-aligned; carry partial frames forward.
    uint8_t aligned[READ_BYTES + sizeof(carryBuffer)];
    size_t totalBytes = carryBytes + length;
    std::memcpy(aligned, carryBuffer, carryBytes);
    std::memcpy(aligned + carryBytes, data, length);

    constexpr size_t SOURCE_FRAME_BYTES = 2 * sizeof(int16_t);
    size_t wholeBytes = totalBytes - (totalBytes % SOURCE_FRAME_BYTES);
    carryBytes = totalBytes - wholeBytes;
    std::memcpy(carryBuffer, aligned + wholeBytes, carryBytes);
    if (wholeBytes == 0)
    {
        return;
    }

    // bound latency: if the ring buffer backs up (clock drift over a very long
    // session, or audio stopped), drop the newest audio instead of buffering it.
    size_t nSamples = wholeBytes / sizeof(int16_t);
    bool dropNow = ringBuffer.readSpace() > highWaterBytes;
    if (!dropNow)
    {
        const int16_t *sourceSamples = reinterpret_cast<const int16_t *>(aligned);
        constexpr float scale = 1.0f / 32768.0f;
        for (size_t i = 0; i < nSamples; ++i)
        {
            convertBuffer[i] = sourceSamples[i] * scale;
        }
        dropNow = !ringBuffer.write(nSamples * sizeof(float), (uint8_t *)convertBuffer.data());
    }
    if (dropNow != dropping)
    {
        dropping = dropNow;
        if (dropping)
        {
            Lv2Log::info("AirPlay input: buffer full. Dropping audio.");
        }
    }
}

void AirplayInputStream::ThreadProc()
{
    SetThreadName("airplay_in");
    try
    {
        while (true)
        {
            struct pollfd pollFds[2];
            pollFds[0].fd = fd;
            pollFds[0].events = POLLIN;
            pollFds[0].revents = 0;
            pollFds[1].fd = cancelRead;
            pollFds[1].events = POLLIN;
            pollFds[1].revents = 0;

            int rc = poll(pollFds, 2, -1);
            if (rc < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                throw std::runtime_error("poll failed.");
            }
            if (pollFds[1].revents)
            {
                break; // cancelled.
            }
            bool writerGone = false;
            if (pollFds[0].revents & POLLIN)
            {
                ssize_t bytesRead = read(fd, readBuffer.data(), readBuffer.size());
                if (bytesRead > 0)
                {
                    PushSamples(readBuffer.data(), (size_t)bytesRead);
                    continue;
                }
                if (bytesRead < 0 && (errno == EAGAIN || errno == EINTR))
                {
                    continue;
                }
                writerGone = true; // EOF or error.
            }
            else if (pollFds[0].revents & (POLLHUP | POLLERR))
            {
                writerGone = true;
            }
            if (writerGone)
            {
                // No writer connected (no active AirPlay session). The FIFO read
                // end stays valid; wait a while (or until cancelled) before
                // polling again so we don't spin on POLLHUP.
                carryBytes = 0;
                struct pollfd cancelPollFd;
                cancelPollFd.fd = cancelRead;
                cancelPollFd.events = POLLIN;
                cancelPollFd.revents = 0;
                if (poll(&cancelPollFd, 1, 250) > 0)
                {
                    break; // cancelled.
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        Lv2Log::error(SS("AirPlay input stream terminated. " << e.what()));
    }
}

void AirplayInputStream::DiscardRing()
{
    size_t staleBytes = ringBuffer.readSpace();
    while (staleBytes != 0)
    {
        size_t chunk = std::min(staleBytes, mixBuffer.size() * sizeof(float));
        if (!ringBuffer.read(chunk, (uint8_t *)mixBuffer.data()))
        {
            break;
        }
        staleBytes -= chunk;
    }
}

void AirplayInputStream::MixOutput(std::vector<float *> &outputBuffers, size_t nFrames)
{
    if (!streamRunning.load(std::memory_order_relaxed))
    {
        // keep the ring buffer drained so no stale audio plays on re-enable.
        primed = false;
        currentGain = 0;
        DiscardRing();
        return;
    }
    if (outputBuffers.size() == 0)
    {
        return;
    }
    size_t availableFrames = ringBuffer.readSpace() / FRAME_BYTES;
    if (!primed)
    {
        if (availableFrames < primeFrames)
        {
            return; // keep buffering.
        }
        primed = true;
    }
    size_t framesWanted = std::min(nFrames, availableFrames);
    if (framesWanted < nFrames)
    {
        primed = false; // underrun: re-buffer before resuming.
    }

    float *left = outputBuffers[0];
    float *right = outputBuffers.size() >= 2 ? outputBuffers[1] : nullptr;
    float target = targetGain.load(std::memory_order_relaxed);
    float gain = currentGain;
    const float coefficient = gainSmoothingCoefficient;

    size_t framesDone = 0;
    while (framesDone < framesWanted)
    {
        size_t chunkFrames = std::min(framesWanted - framesDone, MIX_CHUNK_FRAMES);
        if (!ringBuffer.read(chunkFrames * FRAME_BYTES, (uint8_t *)mixBuffer.data()))
        {
            break; // can't happen: we are the only reader.
        }
        if (right != nullptr)
        {
            for (size_t i = 0; i < chunkFrames; ++i)
            {
                gain += (target - gain) * coefficient;
                size_t frame = framesDone + i;
                left[frame] += mixBuffer[i * 2] * gain;
                right[frame] += mixBuffer[i * 2 + 1] * gain;
            }
        }
        else
        {
            for (size_t i = 0; i < chunkFrames; ++i)
            {
                gain += (target - gain) * coefficient;
                left[framesDone + i] += (mixBuffer[i * 2] + mixBuffer[i * 2 + 1]) * (0.5f * gain);
            }
        }
        framesDone += chunkFrames;
    }
    currentGain = gain;
}
