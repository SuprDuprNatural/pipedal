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
#include <cstdint>
#include <string>
#include "json.hpp"

namespace pipedal
{
    // FIFO through which shairport-sync streams raw PCM to pipedald.
    // Referenced both by the pipedald FIFO reader and by the ALSA config
    // written by pipedaladmind.
    constexpr char AIRPLAY_FIFO_PATH[] = "/var/pipedal/airplay_fifo";

    // User-facing AirPlay receiver settings, persisted to AirplayConfig.json.
    class AirplaySettings
    {
    public:
        bool enabled_ = false;
        float volume_ = 0.7f; // 0..1

        DECLARE_JSON_MAP(AirplaySettings);
    };

    // Payload of the pipedaladmind "setAirplayConfiguration" command.
    class AirplayServiceConfiguration
    {
    public:
        bool enabled_ = false;
        std::string name_;             // AirPlay service name shown to senders.
        uint32_t sampleRate_ = 48000;  // current audio device sample rate.
        std::string fifoPath_ = AIRPLAY_FIFO_PATH;

        DECLARE_JSON_MAP(AirplayServiceConfiguration);
    };

} // namespace pipedal
