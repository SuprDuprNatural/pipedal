// Copyright (c) 2026 Robin Davies
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "GpioTuner.hpp"
#include "json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pipedal
{
    enum class GpioInputType : int32_t
    {
        Momentary = 0,
        Latching = 1,
        Analog = 2,
        Encoder = 3,
        Navigation = 4
    };

    enum class GpioPull : int32_t
    {
        None = 0,
        Up = 1,
        Down = 2
    };

    // Optional global roles for the four effect-parameter encoders. Navigation
    // is handled by a separate ANO navigation input, so parameter knob turns
    // never change screens or presets.
    enum class GpioEncoderRole : int32_t
    {
        None = 0,
        // Values 1 and 2 were used by the original four-encoder workflow.
        // Keep them readable so old settings can be migrated once.
        PresetBrowser = 1,
        ParameterScroll = 2,
        Parameter1 = 3,
        Parameter2 = 4,
        Parameter3 = 5,
        Parameter4 = 6
    };

    class GpioInputConfiguration
    {
    public:
        std::string id_;
        std::string name_;
        bool enabled_ = true;
        int32_t inputType_ = static_cast<int32_t>(GpioInputType::Momentary);

        // Digital inputs. The line number is a gpiochip line offset. On a
        // Raspberry Pi's primary gpiochip this is the familiar BCM GPIO number.
        std::string chip_ = "/dev/gpiochip0";
        int32_t line_ = 17;
        int32_t pull_ = static_cast<int32_t>(GpioPull::Up);
        bool activeLow_ = true;
        int32_t debounceMs_ = 30;

        // Analog inputs. Raspberry Pis do not have an ADC; analog controls are
        // read from an IIO raw channel supplied by an external ADC driver.
        std::string analogPath_;
        float analogMin_ = 0.0f;
        float analogMax_ = 4095.0f;
        float smoothing_ = 0.15f;
        float deadband_ = 0.005f;
        int32_t pollIntervalMs_ = 20;

        // Adafruit seesaw I2C rotary encoder. Encoder is the QT Rotary Encoder
        // board; Navigation is the ANO wheel with select/up/left/down/right.
        std::string i2cDevice_ = "/dev/i2c-1";
        int32_t i2cAddress_ = 0x36;
        bool encoderReversed_ = true; // makes clockwise positive on the Adafruit board.
        int32_t encoderPollIntervalMs_ = 1;
        int32_t encoderRole_ = static_cast<int32_t>(GpioEncoderRole::None);

        GpioInputType inputType() const { return static_cast<GpioInputType>(inputType_); }
        GpioPull pull() const { return static_cast<GpioPull>(pull_); }
        GpioEncoderRole encoderRole() const { return static_cast<GpioEncoderRole>(encoderRole_); }

        DECLARE_JSON_MAP(GpioInputConfiguration);
    };

    class GpioDisplaySettings
    {
    public:
        bool enabled_ = false;
        std::string i2cDevice_ = "/dev/i2c-1";
        int32_t i2cAddress_ = 0x3C;
        int32_t overlayTimeoutMs_ = 3000;
        bool waveformEnabled_ = true;
        bool waveformOutput_ = true;
        int32_t refreshIntervalMs_ = 200;
        bool rotate180_ = false;
        int32_t contrast_ = 160;
        int32_t passiveMode_ = 0; // GpioDisplayMode wire value.

        DECLARE_JSON_MAP(GpioDisplaySettings);
    };

    enum class GpioLedMatrixMode : int32_t
    {
        Spectrum = 0,
        Droplets = 1
    };

    class GpioLedMatrixSettings
    {
    public:
        bool enabled_ = false;
        std::string i2cDevice_ = "/dev/i2c-1";
        int32_t i2cAddress_ = 0x70;
        int32_t brightness_ = 6; // HT16K33 brightness, 0..15.
        int32_t refreshIntervalMs_ = 16;
        int32_t mode_ = 0; // GpioLedMatrixMode wire value.
        float floorDb_ = -48.0f;
        float decay_ = 0.65f;

        // The enclosure exposes a rounded 5x5 portion of the physical 8x8.
        // These settings place and orient that logical grid. Calibration mode
        // draws an asymmetric pattern so every transform can be verified.
        int32_t originX_ = 1;
        int32_t originY_ = 1;
        int32_t rotation_ = 0; // clockwise quarter turns.
        bool mirror_ = false;
        bool calibrationMode_ = false;

        DECLARE_JSON_MAP(GpioLedMatrixSettings);
    };

    class GpioSettings
    {
    public:
        bool enabled_ = false;
        bool encoderRolesConfigured_ = false;
        int32_t encoderRoleVersion_ = 0;
        // Detents needed to move a continuous parameter across its whole range.
        // Parameters that declare their own steps, and integer, toggled and
        // enumerated parameters, use their declared resolution instead.
        int32_t encoderStepsPerRange_ = 100;
        std::vector<GpioInputConfiguration> inputs_;
        GpioDisplaySettings display_;
        GpioLedMatrixSettings ledMatrix_;

        // Assign four parameter roles and migrate the earlier preset/scroll/
        // two-parameter layout. Returns true when settings were changed.
        bool EnsureStandardEncoderRoles();

        DECLARE_JSON_MAP(GpioSettings);
    };

    enum class GpioBindingMode : int32_t
    {
        Direct = 0,
        Toggle = 1,
        Trigger = 2,
        Relative = 3
    };

    enum class GpioBindingEventType : int32_t
    {
        Value = 0,
        EncoderTurn = 1,
        EncoderButton = 2
    };

    enum class GpioActionType : int32_t
    {
        Control = 0,
        Bypass = 1,
        LoadPreset = 2,
        NextPreset = 3,
        PreviousPreset = 4,
        SelectSnapshot = 5,
        NextSnapshot = 6,
        PreviousSnapshot = 7,
        NextBank = 8,
        PreviousBank = 9
    };

    // A mapping is stored in the pedalboard, so the same physical controls can
    // do completely different things in different presets. Multiple mappings
    // may reference the same input.
    //
    // Encoders that hold a standard role handle their own turns; only their
    // push buttons reach mappings. Everything else -- footswitches, maintained
    // switches, analog controls, and encoders left unassigned -- is mapped
    // here and nowhere else.
    class GpioBinding
    {
    public:
        bool enabled_ = true;
        std::string inputId_;
        int32_t actionType_ = static_cast<int32_t>(GpioActionType::Control);
        int32_t mode_ = static_cast<int32_t>(GpioBindingMode::Direct);
        int32_t eventType_ = static_cast<int32_t>(GpioBindingEventType::Value);

        int64_t instanceId_ = -1;
        std::string symbol_;
        int64_t targetId_ = -1; // preset id or snapshot index, depending on actionType.

        float minValue_ = 0.0f;
        float maxValue_ = 1.0f;
        float curve_ = 1.0f;
        float stepValue_ = 0.01f;

        GpioActionType actionType() const { return static_cast<GpioActionType>(actionType_); }
        GpioBindingMode mode() const { return static_cast<GpioBindingMode>(mode_); }
        GpioBindingEventType eventType() const { return static_cast<GpioBindingEventType>(eventType_); }

        DECLARE_JSON_MAP(GpioBinding);
    };

    class GpioLineInfo
    {
    public:
        int32_t offset_ = -1;
        std::string name_;
        std::string consumer_;
        bool used_ = false;

        DECLARE_JSON_MAP(GpioLineInfo);
    };

    class GpioChipInfo
    {
    public:
        std::string path_;
        std::string name_;
        std::string label_;
        int32_t lineCount_ = 0;
        std::vector<GpioLineInfo> lines_;

        DECLARE_JSON_MAP(GpioChipInfo);
    };

    class GpioAnalogChannel
    {
    public:
        std::string path_;
        std::string deviceName_;
        std::string channelName_;

        DECLARE_JSON_MAP(GpioAnalogChannel);
    };

    class GpioCapabilities
    {
    public:
        bool supported_ = false;
        std::string error_;
        std::vector<GpioChipInfo> chips_;
        std::vector<GpioAnalogChannel> analogChannels_;
        std::vector<std::string> i2cDevices_;

        DECLARE_JSON_MAP(GpioCapabilities);
    };

    class GpioInputStatus
    {
    public:
        std::string inputId_;
        bool connected_ = false;
        float value_ = 0.0f; // normalized 0..1
        // Encoders are event sources, not absolute controls. This remains in
        // the wire format for compatibility but is always zero.
        int64_t encoderPosition_ = 0;
        bool buttonPressed_ = false;
        uint32_t navigationButtons_ = 0;
        std::string error_;

        DECLARE_JSON_MAP(GpioInputStatus);
    };

    enum class GpioInputEventType : int32_t
    {
        Value = 0,
        EncoderTurn = 1,
        EncoderButton = 2
    };

    enum class GpioNavigationButton : int32_t
    {
        None = 0,
        Select = 1,
        Up = 2,
        Left = 3,
        Down = 4,
        Right = 5
    };

    // Turns the ANO's raw cumulative-position words into applied movement.
    //
    // Two failure modes have to be told apart. A corrupt I2C word looks like a
    // large jump that the following sample contradicts; real movement persists.
    // So from idle one sample is held back and released only once a second
    // sample agrees with it -- either the position stayed put or it carried on
    // in the same direction. Confirmation then releases the whole accumulated
    // movement, so a brisk turn costs one sample of latency instead of being
    // dropped. Once movement is confirmed the filter stays open briefly, and a
    // continuing turn is applied with no further delay.
    //
    // Deliberately free of I2C so the rules can be tested directly.
    class GpioNavigationPositionFilter
    {
    public:
        using clock = std::chrono::steady_clock;

        // Movement stays applied without re-confirmation for this long after a
        // confirmed sample, which is what makes a sustained turn feel direct.
        static constexpr auto activeWindow = std::chrono::milliseconds(120);

        // Returns the movement to apply, in encoder counts. Zero means either
        // no movement or movement that is not yet trusted.
        int32_t Apply(uint32_t rawPosition, clock::time_point now)
        {
            if (!positionInitialized_)
            {
                Rebaseline(rawPosition, now);
                positionInitialized_ = true;
                return 0;
            }

            // A corrupt word appears as a jump no hand could produce in the time
            // available. That budget has to run from the last *trusted* position
            // rather than the last sample: while a sample is held for
            // confirmation the baseline deliberately stays put, so measuring
            // against the previous sample would let a fast turn outrun its own
            // ceiling and be rejected -- which is exactly how the earlier gate
            // discarded brisk gestures. The floor covers the normal fast
            // cadence; the cap bounds how far one confirmation can move a menu.
            const int64_t sinceBaseline =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - baselineTime_)
                    .count();
            const int64_t plausibleDelta = std::clamp<int64_t>(sinceBaseline * 2, 8, 64);
            const int64_t candidate = static_cast<int32_t>(rawPosition - position_);

            if (candidate == 0)
            {
                pendingValid_ = false;
                implausibleSamples_ = 0;
                return 0;
            }

            if (candidate > plausibleDelta || candidate < -plausibleDelta)
            {
                pendingValid_ = false;
                // Never let one bad baseline strand the encoder permanently.
                if (++implausibleSamples_ >= 8)
                {
                    Rebaseline(rawPosition, now);
                    implausibleSamples_ = 0;
                }
                return 0;
            }
            implausibleSamples_ = 0;

            if (now < activeUntil_)
            {
                // Already turning: apply movement at once. Latency here is what
                // the player feels as responsiveness.
                return Accept(rawPosition, now, candidate);
            }

            if (!pendingValid_)
            {
                pendingPosition_ = rawPosition;
                pendingValid_ = true;
                return 0;
            }

            const int64_t held = static_cast<int32_t>(pendingPosition_ - position_);
            const int64_t continuation =
                static_cast<int32_t>(rawPosition - pendingPosition_);
            const bool stillThere = continuation == 0;
            const bool keptGoing = (held > 0 && continuation > 0) ||
                                   (held < 0 && continuation < 0);
            if (stillThere || keptGoing)
                return Accept(rawPosition, now, candidate);

            // Disagreement: discard the unconfirmed word and keep the trusted
            // baseline, but let this sample stand as the new candidate.
            pendingPosition_ = rawPosition;
            return 0;
        }

    private:
        // Adopts rawPosition as the trusted baseline and restarts the
        // plausibility budget. Does not by itself imply the player is turning.
        void Rebaseline(uint32_t rawPosition, clock::time_point now)
        {
            position_ = rawPosition;
            baselineTime_ = now;
            pendingValid_ = false;
        }

        // Confirmed movement: adopt the position and hold the filter open so a
        // continuing turn is applied without further confirmation.
        int32_t Accept(uint32_t rawPosition, clock::time_point now, int64_t candidate)
        {
            Rebaseline(rawPosition, now);
            activeUntil_ = now + activeWindow;
            return static_cast<int32_t>(candidate);
        }

        uint32_t position_ = 0;
        uint32_t pendingPosition_ = 0;
        bool positionInitialized_ = false;
        bool pendingValid_ = false;
        int implausibleSamples_ = 0;
        clock::time_point baselineTime_{};
        clock::time_point activeUntil_{};
    };

    class GpioInputEvent
    {
    public:
        std::string inputId;
        float value = 0.0f;
        bool pressed = false;
        bool risingEdge = false;
        bool initial = false;
        GpioInputEventType eventType = GpioInputEventType::Value;
        int32_t delta = 0;
        GpioNavigationButton navigationButton = GpioNavigationButton::None;
    };

    class GpioDisplayMessage
    {
    public:
        std::string title;
        std::string label;
        std::string value;
        float normalizedValue = 0.0f;
        bool hasNormalizedValue = false;
    };

    enum class GpioDisplayMode : int32_t
    {
        Controls = 0,
        Waveform = 1,
        Tuner = 2,
        Blank = 3
    };


    // One parameter that the web interface would show a control for. The
    // navigation wheel scrolls within the selected effect; the four parameter
    // knobs edit the four currently shown.
    class GpioParameter
    {
    public:
        int64_t instanceId = -1;
        std::string symbol;

        bool operator==(const GpioParameter &other) const = default;
    };

    class GpioDisplayControl
    {
    public:
        bool assigned = false;
        // Every control carries its effect name so transient mapping messages
        // can reuse the same display formatting.
        std::string effectName;
        std::string label;
        std::string value;
        float normalizedValue = 0.0f;
    };

    class GpioDisplayDashboard
    {
    public:
        std::array<GpioDisplayControl, 4> controls;
        int32_t activeSlot = 0; // 0 = neither, otherwise 1..4.
        int32_t scrollIndex = 0;
        int32_t scrollPositions = 0; // 0 when the chain has no parameters.
    };

    class GpioDisplayMenu
    {
    public:
        std::string title;
        std::string detail;
        std::vector<std::string> items;
        int32_t selectedIndex = 0;
    };

    // std::isfinite is not usable anywhere in this feature: PiPedal release
    // builds use -ffast-math, and -ffinite-math-only lets the compiler fold the
    // check away to `true`. Test the IEEE-754 exponent field directly instead.
    bool IsFiniteGpioValue(float value);

    // Resolve an explicitly supplied display value or a current model value,
    // sanitising non-finite input.
    float ResolveGpioDisplayValue(
        std::optional<float> suppliedValue,
        float currentValue,
        float fallbackValue);

    class GpioManager
    {
    public:
        using EventCallback = std::function<void(const GpioInputEvent &)>;
        using StatusCallback = std::function<void(const GpioInputStatus &)>;
        using WaveformProvider = std::function<bool(bool output, std::array<float, 128> *values)>;
        using TunerSampleProvider = std::function<size_t(
            uint64_t *readIndex,
            float *values,
            size_t capacity,
            uint32_t *sampleRate)>;

        virtual ~GpioManager() = default;

        virtual void SetEventCallback(EventCallback callback) = 0;
        virtual void SetStatusCallback(StatusCallback callback) = 0;
        virtual void SetWaveformProvider(WaveformProvider callback) = 0;
        virtual void SetTunerSampleProvider(TunerSampleProvider callback) = 0;
        virtual void ShowDisplayMessage(const GpioDisplayMessage &message) = 0;
        virtual void ClearDisplayMessage() = 0;
        virtual void ShowControlDashboard(const GpioDisplayDashboard &dashboard) = 0;
        virtual void ShowTemporaryControlDashboard(const GpioDisplayDashboard &dashboard) = 0;
        virtual void ShowTemporaryMenu(const GpioDisplayMenu &menu) = 0;
        virtual GpioDisplayMode CycleDisplayMode() = 0;
        virtual void SetDisplayMode(GpioDisplayMode mode) = 0;
        virtual GpioDisplayMode GetDisplayMode() const = 0;
        virtual void Configure(const GpioSettings &settings) = 0;
        virtual void Refresh() = 0;
        virtual std::vector<GpioInputStatus> GetStatuses() const = 0;
        virtual void Close() = 0;

        static void Validate(const GpioSettings &settings);
        static GpioCapabilities Discover();
        static std::unique_ptr<GpioManager> Create();
    };
}
