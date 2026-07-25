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
#include <array>
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
        Encoder = 3
    };

    enum class GpioPull : int32_t
    {
        None = 0,
        Up = 1,
        Down = 2
    };

    // Optional global roles for a conventional four-encoder controller. The
    // role belongs to the physical encoder, while Parameter 1/2 assignments
    // remain part of each preset.
    enum class GpioEncoderRole : int32_t
    {
        None = 0,
        PresetBrowser = 1,
        EffectSelector = 2,
        Parameter1 = 3,
        Parameter2 = 4
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

        // Adafruit seesaw I2C rotary encoder. The push switch is exposed as a
        // separate event source by each Encoder input.
        std::string i2cDevice_ = "/dev/i2c-1";
        int32_t i2cAddress_ = 0x36;
        bool encoderReversed_ = true; // makes clockwise positive on the Adafruit board.
        int32_t encoderPollIntervalMs_ = 10;
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

        DECLARE_JSON_MAP(GpioDisplaySettings);
    };

    class GpioSettings
    {
    public:
        bool enabled_ = false;
        bool encoderRolesConfigured_ = false;
        std::vector<GpioInputConfiguration> inputs_;
        GpioDisplaySettings display_;

        // Assign the standard four-encoder workflow to an older configuration
        // that predates roles. Returns true when the settings were changed.
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

        // If set, this mapping is one item in a list selected by the named
        // encoder. Mappings with the same inputId/selectorInputId form a group.
        std::string selectorInputId_;

        // 0 is an advanced/free-form mapping. 1 and 2 are the standard
        // Parameter 1/2 slots selected by the global Effect Selector role.
        int32_t parameterSlot_ = 0;

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
        std::string error_;

        DECLARE_JSON_MAP(GpioInputStatus);
    };

    enum class GpioInputEventType : int32_t
    {
        Value = 0,
        EncoderTurn = 1,
        EncoderButton = 2
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
        Tuner = 2
    };

    class GpioDisplayControl
    {
    public:
        bool assigned = false;
        std::string label;
        std::string value;
        float normalizedValue = 0.0f;
    };

    class GpioDisplayDashboard
    {
    public:
        std::string effectName;
        std::array<GpioDisplayControl, 2> controls;
        int32_t activeSlot = 0; // 0 = neither, otherwise 1 or 2.
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
        virtual GpioDisplayMode CycleDisplayMode() = 0;
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
