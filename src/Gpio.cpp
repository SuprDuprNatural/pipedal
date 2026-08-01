// Copyright (c) 2026 Robin Davies
// SPDX-License-Identifier: MIT

#include "pch.h"
#include "Gpio.hpp"
#include "Lv2Log.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

using namespace pipedal;
namespace fs = std::filesystem;

JSON_MAP_BEGIN(GpioInputConfiguration)
JSON_MAP_REFERENCE(GpioInputConfiguration, id)
JSON_MAP_REFERENCE(GpioInputConfiguration, name)
JSON_MAP_REFERENCE(GpioInputConfiguration, enabled)
JSON_MAP_REFERENCE(GpioInputConfiguration, inputType)
JSON_MAP_REFERENCE(GpioInputConfiguration, chip)
JSON_MAP_REFERENCE(GpioInputConfiguration, line)
JSON_MAP_REFERENCE(GpioInputConfiguration, pull)
JSON_MAP_REFERENCE(GpioInputConfiguration, activeLow)
JSON_MAP_REFERENCE(GpioInputConfiguration, debounceMs)
JSON_MAP_REFERENCE(GpioInputConfiguration, analogPath)
JSON_MAP_REFERENCE(GpioInputConfiguration, analogMin)
JSON_MAP_REFERENCE(GpioInputConfiguration, analogMax)
JSON_MAP_REFERENCE(GpioInputConfiguration, smoothing)
JSON_MAP_REFERENCE(GpioInputConfiguration, deadband)
JSON_MAP_REFERENCE(GpioInputConfiguration, pollIntervalMs)
JSON_MAP_REFERENCE(GpioInputConfiguration, i2cDevice)
JSON_MAP_REFERENCE(GpioInputConfiguration, i2cAddress)
JSON_MAP_REFERENCE(GpioInputConfiguration, encoderReversed)
JSON_MAP_REFERENCE(GpioInputConfiguration, encoderPollIntervalMs)
JSON_MAP_REFERENCE(GpioInputConfiguration, encoderRole)
JSON_MAP_END()

JSON_MAP_BEGIN(GpioDisplaySettings)
JSON_MAP_REFERENCE(GpioDisplaySettings, enabled)
JSON_MAP_REFERENCE(GpioDisplaySettings, i2cDevice)
JSON_MAP_REFERENCE(GpioDisplaySettings, i2cAddress)
JSON_MAP_REFERENCE(GpioDisplaySettings, overlayTimeoutMs)
JSON_MAP_REFERENCE(GpioDisplaySettings, waveformEnabled)
JSON_MAP_REFERENCE(GpioDisplaySettings, waveformOutput)
JSON_MAP_REFERENCE(GpioDisplaySettings, refreshIntervalMs)
JSON_MAP_REFERENCE(GpioDisplaySettings, rotate180)
JSON_MAP_REFERENCE(GpioDisplaySettings, contrast)
JSON_MAP_REFERENCE(GpioDisplaySettings, passiveMode)
JSON_MAP_END()

JSON_MAP_BEGIN(GpioLedMatrixSettings)
JSON_MAP_REFERENCE(GpioLedMatrixSettings, enabled)
JSON_MAP_REFERENCE(GpioLedMatrixSettings, i2cDevice)
JSON_MAP_REFERENCE(GpioLedMatrixSettings, i2cAddress)
JSON_MAP_REFERENCE(GpioLedMatrixSettings, brightness)
JSON_MAP_REFERENCE(GpioLedMatrixSettings, refreshIntervalMs)
JSON_MAP_REFERENCE(GpioLedMatrixSettings, mode)
JSON_MAP_REFERENCE(GpioLedMatrixSettings, floorDb)
JSON_MAP_REFERENCE(GpioLedMatrixSettings, decay)
JSON_MAP_REFERENCE(GpioLedMatrixSettings, originX)
JSON_MAP_REFERENCE(GpioLedMatrixSettings, originY)
JSON_MAP_REFERENCE(GpioLedMatrixSettings, rotation)
JSON_MAP_REFERENCE(GpioLedMatrixSettings, mirror)
JSON_MAP_REFERENCE(GpioLedMatrixSettings, calibrationMode)
JSON_MAP_END()

JSON_MAP_BEGIN(GpioSettings)
JSON_MAP_REFERENCE(GpioSettings, enabled)
JSON_MAP_REFERENCE(GpioSettings, encoderRolesConfigured)
JSON_MAP_REFERENCE(GpioSettings, encoderRoleVersion)
JSON_MAP_REFERENCE(GpioSettings, encoderStepsPerRange)
JSON_MAP_REFERENCE(GpioSettings, inputs)
JSON_MAP_REFERENCE(GpioSettings, display)
JSON_MAP_REFERENCE(GpioSettings, ledMatrix)
JSON_MAP_END()

JSON_MAP_BEGIN(GpioBinding)
JSON_MAP_REFERENCE(GpioBinding, enabled)
JSON_MAP_REFERENCE(GpioBinding, inputId)
JSON_MAP_REFERENCE(GpioBinding, actionType)
JSON_MAP_REFERENCE(GpioBinding, mode)
JSON_MAP_REFERENCE(GpioBinding, eventType)
JSON_MAP_REFERENCE(GpioBinding, instanceId)
JSON_MAP_REFERENCE(GpioBinding, symbol)
JSON_MAP_REFERENCE(GpioBinding, targetId)
JSON_MAP_REFERENCE(GpioBinding, minValue)
JSON_MAP_REFERENCE(GpioBinding, maxValue)
JSON_MAP_REFERENCE(GpioBinding, curve)
JSON_MAP_REFERENCE(GpioBinding, stepValue)
JSON_MAP_END()

JSON_MAP_BEGIN(GpioLineInfo)
JSON_MAP_REFERENCE(GpioLineInfo, offset)
JSON_MAP_REFERENCE(GpioLineInfo, name)
JSON_MAP_REFERENCE(GpioLineInfo, consumer)
JSON_MAP_REFERENCE(GpioLineInfo, used)
JSON_MAP_END()

JSON_MAP_BEGIN(GpioChipInfo)
JSON_MAP_REFERENCE(GpioChipInfo, path)
JSON_MAP_REFERENCE(GpioChipInfo, name)
JSON_MAP_REFERENCE(GpioChipInfo, label)
JSON_MAP_REFERENCE(GpioChipInfo, lineCount)
JSON_MAP_REFERENCE(GpioChipInfo, lines)
JSON_MAP_END()

JSON_MAP_BEGIN(GpioAnalogChannel)
JSON_MAP_REFERENCE(GpioAnalogChannel, path)
JSON_MAP_REFERENCE(GpioAnalogChannel, deviceName)
JSON_MAP_REFERENCE(GpioAnalogChannel, channelName)
JSON_MAP_END()

JSON_MAP_BEGIN(GpioCapabilities)
JSON_MAP_REFERENCE(GpioCapabilities, supported)
JSON_MAP_REFERENCE(GpioCapabilities, error)
JSON_MAP_REFERENCE(GpioCapabilities, chips)
JSON_MAP_REFERENCE(GpioCapabilities, analogChannels)
JSON_MAP_REFERENCE(GpioCapabilities, i2cDevices)
JSON_MAP_END()

JSON_MAP_BEGIN(GpioInputStatus)
JSON_MAP_REFERENCE(GpioInputStatus, inputId)
JSON_MAP_REFERENCE(GpioInputStatus, connected)
JSON_MAP_REFERENCE(GpioInputStatus, value)
JSON_MAP_REFERENCE(GpioInputStatus, encoderPosition)
JSON_MAP_REFERENCE(GpioInputStatus, buttonPressed)
JSON_MAP_REFERENCE(GpioInputStatus, navigationButtons)
JSON_MAP_REFERENCE(GpioInputStatus, error)
JSON_MAP_END()

bool pipedal::IsFiniteGpioValue(float value)
{
    static_assert(sizeof(float) == sizeof(uint32_t));
    // A NaN or an infinity is exactly an all-ones exponent field.
    return (std::bit_cast<uint32_t>(value) & 0x7F800000u) != 0x7F800000u;
}

float pipedal::ResolveGpioDisplayValue(
    std::optional<float> suppliedValue,
    float currentValue,
    float fallbackValue)
{
    const float candidate = suppliedValue.value_or(currentValue);
    if (IsFiniteGpioValue(candidate))
        return candidate;
    return IsFiniteGpioValue(fallbackValue) ? fallbackValue : 0.0f;
}

namespace
{
    std::string ErrnoText(const std::string &operation)
    {
        return operation + ": " + std::strerror(errno);
    }

    bool IsGpioChipPath(const std::string &path)
    {
        static const std::regex pattern("^/dev/gpiochip[0-9]+$");
        return std::regex_match(path, pattern);
    }

    bool IsIioRawPath(const std::string &path)
    {
        static const std::regex pattern("^/sys/bus/iio/devices/iio:device[0-9]+/in_voltage[0-9]+_raw$");
        return std::regex_match(path, pattern);
    }

    bool IsI2cDevicePath(const std::string &path)
    {
        static const std::regex pattern("^/dev/i2c-[0-9]+$");
        return std::regex_match(path, pattern);
    }

    std::string ReadFirstLine(const fs::path &path)
    {
        std::ifstream stream(path);
        std::string result;
        if (stream)
        {
            std::getline(stream, result);
        }
        return result;
    }

#if defined(__linux__)
    class GpioLineHandle
    {
    public:
        GpioLineHandle() = default;
        GpioLineHandle(const GpioLineHandle &) = delete;
        GpioLineHandle &operator=(const GpioLineHandle &) = delete;

        ~GpioLineHandle()
        {
            Close();
        }

        bool Open(const GpioInputConfiguration &configuration, std::string *error)
        {
            Close();
            int chipFd = ::open(configuration.chip_.c_str(), O_RDONLY | O_CLOEXEC);
            if (chipFd < 0)
            {
                *error = ErrnoText("Unable to open " + configuration.chip_);
                return false;
            }

#if defined(GPIO_V2_GET_LINE_IOCTL)
            gpio_v2_line_request request{};
            request.offsets[0] = static_cast<__u32>(configuration.line_);
            request.num_lines = 1;
            std::strncpy(request.consumer, "pipedald", sizeof(request.consumer) - 1);
            request.config.flags = GPIO_V2_LINE_FLAG_INPUT;
            if (configuration.pull() == GpioPull::Up)
            {
                request.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
            }
            else if (configuration.pull() == GpioPull::Down)
            {
                request.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
            }
            else
            {
                request.config.flags |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
            }
            if (::ioctl(chipFd, GPIO_V2_GET_LINE_IOCTL, &request) == 0)
            {
                ::close(chipFd);
                fd_ = request.fd;
                v2_ = true;
                return true;
            }
#endif

            // Compatibility fallback for kernels that only implement the v1
            // character-device ABI. Bias selection requires the v2 API.
            gpiohandle_request requestV1{};
            requestV1.lineoffsets[0] = static_cast<__u32>(configuration.line_);
            requestV1.lines = 1;
            requestV1.flags = GPIOHANDLE_REQUEST_INPUT;
            std::strncpy(requestV1.consumer_label, "pipedald", sizeof(requestV1.consumer_label) - 1);
            if (::ioctl(chipFd, GPIO_GET_LINEHANDLE_IOCTL, &requestV1) == 0)
            {
                ::close(chipFd);
                fd_ = requestV1.fd;
                v2_ = false;
                return true;
            }

            *error = ErrnoText("Unable to request GPIO line " + std::to_string(configuration.line_));
            ::close(chipFd);
            return false;
        }

        bool Read(bool *value, std::string *error) const
        {
            if (fd_ < 0)
            {
                *error = "GPIO line is not open.";
                return false;
            }
#if defined(GPIO_V2_LINE_GET_VALUES_IOCTL)
            if (v2_)
            {
                gpio_v2_line_values values{};
                values.mask = 1;
                if (::ioctl(fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) != 0)
                {
                    *error = ErrnoText("Unable to read GPIO line");
                    return false;
                }
                *value = (values.bits & 1) != 0;
                return true;
            }
#endif
            gpiohandle_data values{};
            if (::ioctl(fd_, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &values) != 0)
            {
                *error = ErrnoText("Unable to read GPIO line");
                return false;
            }
            *value = values.values[0] != 0;
            return true;
        }

        void Close()
        {
            if (fd_ >= 0)
            {
                ::close(fd_);
                fd_ = -1;
            }
        }

    private:
        int fd_ = -1;
        bool v2_ = false;
    };

    class I2cDevice
    {
    public:
        I2cDevice() = default;
        I2cDevice(const I2cDevice &) = delete;
        I2cDevice &operator=(const I2cDevice &) = delete;
        ~I2cDevice() { Close(); }

        bool Open(const std::string &path, int32_t address, std::string *error)
        {
            Close();
            fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
            if (fd_ < 0)
            {
                *error = ErrnoText("Unable to open " + path);
                return false;
            }
            if (::ioctl(fd_, I2C_SLAVE, address) < 0)
            {
                *error = ErrnoText("Unable to select I2C address " + AddressText(address));
                Close();
                return false;
            }
            return true;
        }

        bool Write(const uint8_t *data, size_t size, std::string *error)
        {
            if (fd_ < 0 || ::write(fd_, data, size) != static_cast<ssize_t>(size))
            {
                *error = ErrnoText("I2C write failed");
                return false;
            }
            return true;
        }

        bool ReadRegister(uint8_t module, uint8_t function, uint8_t *data, size_t size, std::string *error)
        {
            uint8_t request[2]{module, function};
            // A seesaw can briefly stretch or NACK a register transaction while
            // it services its encoder interrupt. Retry the complete pointer
            // write + read so a transient does not become a lost control poll.
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                if (::write(fd_, request, sizeof(request)) ==
                    static_cast<ssize_t>(sizeof(request)))
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(300));
                    if (::read(fd_, data, size) == static_cast<ssize_t>(size))
                        return true;
                }
                if (attempt != 2)
                    std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
            *error = ErrnoText("I2C register read failed");
            return false;
        }

        void Close()
        {
            if (fd_ >= 0)
            {
                ::close(fd_);
                fd_ = -1;
            }
        }

    private:
        static std::string AddressText(int32_t address)
        {
            std::ostringstream stream;
            stream << "0x" << std::hex << address;
            return stream.str();
        }
        int fd_ = -1;
    };

    class SeesawEncoder
    {
    public:
        bool Open(const GpioInputConfiguration &configuration, std::string *error)
        {
            if (!device_.Open(configuration.i2cDevice_, configuration.i2cAddress_, error))
                return false;
            positionInitialized_ = false;

            // The encoder push switch is seesaw GPIO 24. Configure it as an
            // input with pull-up, matching Adafruit's reference implementation.
            constexpr uint32_t buttonMask = 1u << 24;
            uint8_t direction[6]{0x01, 0x03,
                                 static_cast<uint8_t>(buttonMask >> 24), static_cast<uint8_t>(buttonMask >> 16),
                                 static_cast<uint8_t>(buttonMask >> 8), static_cast<uint8_t>(buttonMask)};
            uint8_t pull[6]{0x01, 0x0B,
                            static_cast<uint8_t>(buttonMask >> 24), static_cast<uint8_t>(buttonMask >> 16),
                            static_cast<uint8_t>(buttonMask >> 8), static_cast<uint8_t>(buttonMask)};
            uint8_t high[6]{0x01, 0x05,
                            static_cast<uint8_t>(buttonMask >> 24), static_cast<uint8_t>(buttonMask >> 16),
                            static_cast<uint8_t>(buttonMask >> 8), static_cast<uint8_t>(buttonMask)};
            uint8_t interrupts[6]{0x01, 0x08,
                                  static_cast<uint8_t>(buttonMask >> 24), static_cast<uint8_t>(buttonMask >> 16),
                                  static_cast<uint8_t>(buttonMask >> 8), static_cast<uint8_t>(buttonMask)};
            return device_.Write(direction, sizeof(direction), error) &&
                   device_.Write(pull, sizeof(pull), error) &&
                   device_.Write(high, sizeof(high), error) &&
                   device_.Write(interrupts, sizeof(interrupts), error);
        }

        bool Read(
            int32_t *delta,
            bool *buttonPressed,
            bool *buttonActivity,
            std::string *error)
        {
            uint8_t gpioBytes[4]{};
            if (!device_.ReadRegister(0x01, 0x04, gpioBytes, sizeof(gpioBytes), error))
                return false;
            uint32_t gpio = (static_cast<uint32_t>(gpioBytes[0]) << 24) |
                            (static_cast<uint32_t>(gpioBytes[1]) << 16) |
                            (static_cast<uint32_t>(gpioBytes[2]) << 8) |
                            gpioBytes[3];
            *buttonPressed = (gpio & (1u << 24)) == 0;

            if (!ReadEncoderPosition(delta, error))
                return false;

            // INTFLAG latches GPIO activity until read. Polling it recovers a
            // complete short press/release that occurred between host samples.
            uint8_t flagBytes[4]{};
            if (!device_.ReadRegister(0x01, 0x0A, flagBytes, sizeof(flagBytes), error))
                return false;
            const uint32_t flags =
                (static_cast<uint32_t>(flagBytes[0]) << 24) |
                (static_cast<uint32_t>(flagBytes[1]) << 16) |
                (static_cast<uint32_t>(flagBytes[2]) << 8) |
                flagBytes[3];
            *buttonActivity = (flags & (1u << 24)) != 0;
            return true;
        }

    private:
        bool ReadEncoderPosition(int32_t *delta, std::string *error)
        {
            // Follow Adafruit's reference usage and sample the lifetime
            // position register. Unlike the delta register, a failed host read
            // cannot consume movement; the next successful sample catches up.
            constexpr int32_t maxPlausibleDelta = 64;
            for (int attempt = 0; attempt < 2; ++attempt)
            {
                uint8_t positionBytes[4]{};
                if (!device_.ReadRegister(0x11, 0x30, positionBytes, sizeof(positionBytes), error))
                    return false;
                const uint32_t rawPosition =
                    (static_cast<uint32_t>(positionBytes[0]) << 24) |
                    (static_cast<uint32_t>(positionBytes[1]) << 16) |
                    (static_cast<uint32_t>(positionBytes[2]) << 8) |
                    positionBytes[3];
                if (!positionInitialized_)
                {
                    position_ = rawPosition;
                    positionInitialized_ = true;
                    *delta = 0;
                    return true;
                }
                const int32_t candidate = static_cast<int32_t>(rawPosition - position_);
                if (candidate >= -maxPlausibleDelta && candidate <= maxPlausibleDelta)
                {
                    position_ = rawPosition;
                    *delta = candidate;
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }

            // Resynchronize after a persistent implausible word so one bad
            // sample cannot strand this encoder forever.
            positionInitialized_ = false;
            *delta = 0;
            return true;
        }

        I2cDevice device_;
        uint32_t position_ = 0;
        bool positionInitialized_ = false;
    };

    class SeesawNavigationEncoder
    {
    public:
        bool Open(const GpioInputConfiguration &configuration, std::string *error)
        {
            if (!device_.Open(configuration.i2cDevice_, configuration.i2cAddress_, error))
                return false;
            positionInitialized_ = false;

            // ANO adapter firmware exposes select/up/left/down/right as seesaw
            // GPIO pins 1..5. All five switches close to ground.
            constexpr uint32_t buttonMask = 0x3Eu;
            uint8_t direction[6]{0x01, 0x03,
                                 static_cast<uint8_t>(buttonMask >> 24), static_cast<uint8_t>(buttonMask >> 16),
                                 static_cast<uint8_t>(buttonMask >> 8), static_cast<uint8_t>(buttonMask)};
            uint8_t pull[6]{0x01, 0x0B,
                            static_cast<uint8_t>(buttonMask >> 24), static_cast<uint8_t>(buttonMask >> 16),
                            static_cast<uint8_t>(buttonMask >> 8), static_cast<uint8_t>(buttonMask)};
            uint8_t high[6]{0x01, 0x05,
                            static_cast<uint8_t>(buttonMask >> 24), static_cast<uint8_t>(buttonMask >> 16),
                            static_cast<uint8_t>(buttonMask >> 8), static_cast<uint8_t>(buttonMask)};
            uint8_t interrupts[6]{0x01, 0x08,
                                  static_cast<uint8_t>(buttonMask >> 24), static_cast<uint8_t>(buttonMask >> 16),
                                  static_cast<uint8_t>(buttonMask >> 8), static_cast<uint8_t>(buttonMask)};
            return device_.Write(direction, sizeof(direction), error) &&
                   device_.Write(pull, sizeof(pull), error) &&
                   device_.Write(high, sizeof(high), error) &&
                   device_.Write(interrupts, sizeof(interrupts), error);
        }

        bool Read(
            int32_t *delta,
            uint32_t *pressedButtons,
            uint32_t *buttonActivity,
            std::string *error)
        {
            uint8_t gpioBytes[4]{};
            if (!device_.ReadRegister(0x01, 0x04, gpioBytes, sizeof(gpioBytes), error))
                return false;
            const uint32_t gpio = (static_cast<uint32_t>(gpioBytes[0]) << 24) |
                                  (static_cast<uint32_t>(gpioBytes[1]) << 16) |
                                  (static_cast<uint32_t>(gpioBytes[2]) << 8) |
                                  gpioBytes[3];
            *pressedButtons = ((~gpio) >> 1) & 0x1Fu;

            if (!ReadEncoderPosition(delta, error))
                return false;

            uint8_t flagBytes[4]{};
            if (!device_.ReadRegister(0x01, 0x0A, flagBytes, sizeof(flagBytes), error))
                return false;
            const uint32_t flags =
                (static_cast<uint32_t>(flagBytes[0]) << 24) |
                (static_cast<uint32_t>(flagBytes[1]) << 16) |
                (static_cast<uint32_t>(flagBytes[2]) << 8) |
                flagBytes[3];
            *buttonActivity = (flags >> 1) & 0x1Fu;
            return true;
        }

    private:
        bool ReadEncoderPosition(int32_t *delta, std::string *error)
        {
            uint8_t positionBytes[4]{};
            if (!device_.ReadRegister(0x11, 0x30, positionBytes, sizeof(positionBytes), error))
                return false;
            const uint32_t rawPosition =
                (static_cast<uint32_t>(positionBytes[0]) << 24) |
                (static_cast<uint32_t>(positionBytes[1]) << 16) |
                (static_cast<uint32_t>(positionBytes[2]) << 8) |
                positionBytes[3];
            const auto now = std::chrono::steady_clock::now();
            if (!positionInitialized_)
            {
                position_ = rawPosition;
                positionInitialized_ = true;
                pendingPositionInitialized_ = false;
                *delta = 0;
                return true;
            }

            const int64_t candidate = static_cast<int32_t>(rawPosition - position_);
            if (candidate == 0)
            {
                pendingPositionInitialized_ = false;
                *delta = 0;
                return true;
            }

            // While the player is already turning, pass normal motion through
            // without adding latency. At idle, require either the same new
            // position twice or two small steps in the same direction. This
            // rejects the occasional corrupted low byte that otherwise looks
            // like several unsolicited navigation detents.
            constexpr int64_t maximumActiveDelta = 8;
            constexpr int64_t maximumIdleStep = 2;
            if (now < activeUntil_ && std::abs(candidate) <= maximumActiveDelta)
            {
                position_ = rawPosition;
                pendingPositionInitialized_ = false;
                activeUntil_ = now + std::chrono::milliseconds(120);
                *delta = static_cast<int32_t>(candidate);
                return true;
            }

            if (pendingPositionInitialized_ && rawPosition != pendingPosition_)
            {
                const int64_t first = static_cast<int32_t>(pendingPosition_ - position_);
                const int64_t continuation =
                    static_cast<int32_t>(rawPosition - pendingPosition_);
                const bool sameDirection =
                    (first > 0 && continuation > 0) ||
                    (first < 0 && continuation < 0);
                if (sameDirection &&
                    std::abs(first) <= maximumIdleStep &&
                    std::abs(continuation) <= maximumIdleStep &&
                    std::abs(candidate) <= maximumActiveDelta)
                {
                    position_ = rawPosition;
                    pendingPositionInitialized_ = false;
                    activeUntil_ = now + std::chrono::milliseconds(120);
                    *delta = static_cast<int32_t>(candidate);
                    return true;
                }
            }

            if (!pendingPositionInitialized_ || rawPosition != pendingPosition_)
            {
                pendingPosition_ = rawPosition;
                pendingPositionInitialized_ = true;
                *delta = 0;
                return true;
            }

            // A persistent jump larger than a person can produce between two
            // idle samples is used only to re-establish the baseline. Never
            // replay it as a burst of menu movement.
            pendingPositionInitialized_ = false;
            position_ = rawPosition;
            if (std::abs(candidate) <= maximumIdleStep)
            {
                activeUntil_ = now + std::chrono::milliseconds(120);
                *delta = static_cast<int32_t>(candidate);
                return true;
            }
            *delta = 0;
            return true;
        }

        I2cDevice device_;
        uint32_t position_ = 0;
        uint32_t pendingPosition_ = 0;
        bool positionInitialized_ = false;
        bool pendingPositionInitialized_ = false;
        std::chrono::steady_clock::time_point activeUntil_{};
    };

    // Small SSD1306 renderer. Text is deliberately uppercase: this compact
    // font remains legible on a 128x64 display from pedalboard distance.
    class Ssd1306Display
    {
    public:
        bool Open(const GpioDisplaySettings &settings, std::string *error)
        {
            if (!device_.Open(settings.i2cDevice_, settings.i2cAddress_, error))
                return false;
            const uint8_t init[]{
                0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
                0x8D, 0x14, 0x20, 0x00,
                static_cast<uint8_t>(settings.rotate180_ ? 0xA0 : 0xA1),
                static_cast<uint8_t>(settings.rotate180_ ? 0xC0 : 0xC8),
                0xDA, 0x12, 0x81, static_cast<uint8_t>(settings.contrast_),
                0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
            return SendCommands(init, sizeof(init), error);
        }

        bool DrawMessage(const GpioDisplayMessage &message, std::string *error)
        {
            Clear();
            DrawText(0, 0, message.title, 1);
            DrawHorizontal(0, 9, 128);
            DrawText(0, 14, message.label, 1);
            DrawText(0, 28, message.value, 2);
            if (message.hasNormalizedValue)
            {
                DrawRect(0, 55, 128, 9);
                int width = static_cast<int>(std::clamp(message.normalizedValue, 0.0f, 1.0f) * 124.0f);
                FillRect(2, 57, width, 5);
            }
            return Flush(error);
        }

        bool DrawWaveform(const std::array<float, 128> &samples, bool output, std::string *error)
        {
            Clear();
            DrawText(0, 0, output ? "OUTPUT WAVEFORM" : "INPUT WAVEFORM", 1);
            DrawHorizontal(0, 11, 128);
            DrawHorizontal(0, 38, 128);
            float peak = 0.02f;
            for (float value : samples)
                peak = std::max(peak, std::abs(value));
            float gain = std::min(1.0f / peak, 8.0f);
            int previousY = 38;
            for (int x = 0; x < 128; ++x)
            {
                int y = 38 - static_cast<int>(std::clamp(samples[x] * gain, -1.0f, 1.0f) * 24.0f);
                DrawLine(x == 0 ? x : x - 1, previousY, x, y);
                previousY = y;
            }
            return Flush(error);
        }

        bool DrawDashboard(const GpioDisplayDashboard &dashboard, std::string *error)
        {
            Clear();
            if (std::none_of(
                    dashboard.controls.begin(), dashboard.controls.end(),
                    [](const GpioDisplayControl &control) { return control.assigned; }))
            {
                DrawTextCentered(64, 20, "NO PARAMETERS", 1, 21);
                DrawTextCentered(64, 34, "SELECT AN EFFECT", 1, 21);
                return Flush(error);
            }

            // Give each contiguous effect its own header spanning exactly the
            // parameter columns it owns. Effect-boundary dividers extend into
            // the heading; ordinary parameter dividers begin below it.
            size_t firstSlot = 0;
            while (firstSlot < dashboard.controls.size())
            {
                if (!dashboard.controls[firstSlot].assigned)
                {
                    ++firstSlot;
                    continue;
                }
                size_t lastSlot = firstSlot;
                while (lastSlot + 1 < dashboard.controls.size() &&
                       dashboard.controls[lastSlot + 1].assigned &&
                       dashboard.controls[lastSlot + 1].effectName ==
                           dashboard.controls[firstSlot].effectName)
                    ++lastSlot;
                const int left = static_cast<int>(firstSlot) * 32;
                const int right = static_cast<int>(lastSlot + 1) * 32;
                const size_t maxCharacters = static_cast<size_t>(
                    std::max(1, (right - left - 4) / 6));
                DrawTextCentered(
                    (left + right) / 2, 0,
                    dashboard.controls[firstSlot].effectName,
                    1, maxCharacters);
                firstSlot = lastSlot + 1;
            }
            DrawHorizontal(0, 8, 128);
            for (int column = 1; column < 4; ++column)
            {
                const auto &left = dashboard.controls[static_cast<size_t>(column - 1)];
                const auto &right = dashboard.controls[static_cast<size_t>(column)];
                const bool effectBoundary =
                    left.assigned && right.assigned &&
                    left.effectName != right.effectName;
                for (int y = effectBoundary ? 0 : 12; y <= 52; ++y)
                    Pixel(column * 32, y);
            }
            for (size_t slot = 0; slot < dashboard.controls.size(); ++slot)
                DrawKnob(
                    16 + static_cast<int>(slot) * 32,
                    dashboard.controls[slot],
                    dashboard.activeSlot == static_cast<int32_t>(slot + 1));
            DrawScrollBar(dashboard.scrollIndex, dashboard.scrollPositions);
            return Flush(error);
        }

        bool DrawMenu(const GpioDisplayMenu &menu, std::string *error)
        {
            Clear();
            if (menu.items.empty())
            {
                DrawTextCentered(64, 0, menu.title, 1, 21);
                DrawHorizontal(0, 9, 128);
                DrawTextCentered(64, 27, "NONE", 1, 21);
            }
            else if (menu.title == "PRESETS")
            {
                // The fixed vertical label costs only eight pixels. Two
                // columns then fit eight presets per page, with the selector
                // consuming horizontal space only on the active row.
                constexpr std::string_view label = "PRESET";
                for (size_t index = 0; index < label.size(); ++index)
                    DrawText(
                        0, 1 + static_cast<int>(index) * 9,
                        std::string(1, label[index]), 1);
                for (int y = 0; y < 64; ++y)
                    Pixel(7, y);
                for (int y = 0; y < 64; ++y)
                    Pixel(68, y);

                const int32_t selected = std::clamp<int32_t>(
                    menu.selectedIndex, 0,
                    static_cast<int32_t>(menu.items.size()) - 1);
                constexpr int32_t itemsPerPage = 8;
                const int32_t pageStart = selected / itemsPerPage * itemsPerPage;
                for (int32_t slot = 0; slot < itemsPerPage; ++slot)
                {
                    const int32_t item = pageStart + slot;
                    if (item >= static_cast<int32_t>(menu.items.size()))
                        break;
                    const int column = slot / 4;
                    const int row = slot % 4;
                    const int lineStart = column == 0 ? 10 : 71;
                    const int lineEnd = column == 0 ? 67 : 127;
                    const int y = 3 + row * 15;
                    const bool active = item == selected;
                    int textX = lineStart;
                    if (active)
                    {
                        DrawText(lineStart, y, ">", 1);
                        textX += 7;
                    }
                    const size_t maxCharacters = static_cast<size_t>(
                        std::max(1, (lineEnd - textX + 1) / 6));
                    DrawText(
                        textX, y,
                        menu.items[static_cast<size_t>(item)].substr(0, maxCharacters),
                        1);
                }
            }
            else
            {
                DrawTextCentered(64, 0, menu.title, 1, 21);
                DrawHorizontal(0, 9, 128);
                const int32_t selected = std::clamp<int32_t>(
                    menu.selectedIndex, 0, static_cast<int32_t>(menu.items.size()) - 1);
                constexpr int32_t visibleRows = 4;
                const int32_t maximumStart = std::max<int32_t>(
                    0, static_cast<int32_t>(menu.items.size()) - visibleRows);
                const int32_t first = std::clamp<int32_t>(
                    selected - 1, 0, maximumStart);
                for (int32_t row = 0; row < visibleRows; ++row)
                {
                    const int32_t item = first + row;
                    if (item >= static_cast<int32_t>(menu.items.size()))
                        break;
                    const int y = 13 + row * 12;
                    const bool active = item == selected;
                    const int textX = active ? 9 : 1;
                    if (active)
                        DrawText(1, y, ">", 1);
                    DrawText(
                        textX, y,
                        menu.items[static_cast<size_t>(item)].substr(
                            0, active ? 19 : 21),
                        1);
                }
            }
            return Flush(error);
        }

        bool DrawTuner(const GpioTunerFrame &frame, std::string *error)
        {
            Clear();
            const bool locked = frame.Locked();
            const float cents = std::clamp(frame.cents, -50.0f, 50.0f);
            const bool inTune = locked && std::abs(cents) <= 0.8f;
            constexpr int dotCount = 41;
            constexpr int laneStart = 4;
            constexpr int laneSpacing = 3;
            constexpr int laneY = 12;
            constexpr int centreX = 64;

            if (locked)
            {
                const float period = std::clamp(
                    10.0f * 55.0f / std::clamp(frame.frequency, 27.5f, 220.0f),
                    2.8f, 15.0f);
                const float phaseDots = frame.strobePhase * period * 2.0f;
                for (int i = 0; i < dotCount; ++i)
                {
                    constexpr float pi = 3.14159265358979323846f;
                    const float wave =
                        0.5f + 0.5f * std::cos(
                                           2.0f * pi *
                                           (static_cast<float>(i) - phaseDots) /
                                           period);
                    if (wave > 0.56f)
                        FillRect(laneStart + i * laneSpacing - 1, laneY - 1, 3, 3);
                }
            }
            else
            {
                for (int i = 0; i < dotCount; i += 4)
                    Pixel(laneStart + i * laneSpacing, laneY);
            }

            // Fixed centre reference and a separate cents needle.
            for (int y : {5, 12, 19})
                DrawCircle(centreX, y, 1);
            if (locked)
            {
                const int needleX = inTune
                                        ? centreX
                                        : centreX + static_cast<int>(
                                                        std::lround(cents / 50.0f * 60.0f));
                for (int y : {5, 12, 19})
                {
                    if (inTune)
                        FillRect(needleX - 2, y - 2, 5, 5);
                    else
                        FillRect(needleX - 1, y - 1, 3, 3);
                }
            }

            if (locked)
            {
                DrawTextCentered(64, 27, frame.NoteName(), 2, 3);
                std::ostringstream frequency;
                frequency << std::fixed << std::setprecision(2) << frame.frequency;
                DrawText(0, 56, frequency.str(), 1);

                std::ostringstream errorText;
                errorText << std::showpos << std::fixed << std::setprecision(2)
                          << cents / 100.0f;
                const std::string text = errorText.str();
                DrawText(128 - static_cast<int>(text.size()) * 6, 56, text, 1);
            }
            return Flush(error);
        }

        bool DrawIdle(std::string *error)
        {
            Clear();
            DrawText(22, 24, "PIPEDAL", 2);
            return Flush(error);
        }

        bool DrawBlank(std::string *error)
        {
            Clear();
            return Flush(error);
        }

    private:
        static std::array<uint8_t, 5> Glyph(char c)
        {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            switch (c)
            {
            case ' ': return {0,0,0,0,0}; case '!': return {0,0,0x5F,0,0};
            case '#': return {0x14,0x7F,0x14,0x7F,0x14};
            case '-': return {0x08,0x08,0x08,0x08,0x08}; case '.': return {0,0x60,0x60,0,0};
            case '/': return {0x20,0x10,0x08,0x04,0x02}; case ':': return {0,0x36,0x36,0,0};
            case '%': return {0x23,0x13,0x08,0x64,0x62}; case '+': return {0x08,0x08,0x3E,0x08,0x08};
            case '0': return {0x3E,0x51,0x49,0x45,0x3E}; case '1': return {0,0x42,0x7F,0x40,0};
            case '2': return {0x42,0x61,0x51,0x49,0x46}; case '3': return {0x21,0x41,0x45,0x4B,0x31};
            case '4': return {0x18,0x14,0x12,0x7F,0x10}; case '5': return {0x27,0x45,0x45,0x45,0x39};
            case '6': return {0x3C,0x4A,0x49,0x49,0x30}; case '7': return {0x01,0x71,0x09,0x05,0x03};
            case '8': return {0x36,0x49,0x49,0x49,0x36}; case '9': return {0x06,0x49,0x49,0x29,0x1E};
            case 'A': return {0x7E,0x11,0x11,0x11,0x7E}; case 'B': return {0x7F,0x49,0x49,0x49,0x36};
            case 'C': return {0x3E,0x41,0x41,0x41,0x22}; case 'D': return {0x7F,0x41,0x41,0x22,0x1C};
            case 'E': return {0x7F,0x49,0x49,0x49,0x41}; case 'F': return {0x7F,0x09,0x09,0x09,0x01};
            case 'G': return {0x3E,0x41,0x49,0x49,0x7A}; case 'H': return {0x7F,0x08,0x08,0x08,0x7F};
            case 'I': return {0,0x41,0x7F,0x41,0}; case 'J': return {0x20,0x40,0x41,0x3F,0x01};
            case 'K': return {0x7F,0x08,0x14,0x22,0x41}; case 'L': return {0x7F,0x40,0x40,0x40,0x40};
            case 'M': return {0x7F,0x02,0x0C,0x02,0x7F}; case 'N': return {0x7F,0x04,0x08,0x10,0x7F};
            case 'O': return {0x3E,0x41,0x41,0x41,0x3E}; case 'P': return {0x7F,0x09,0x09,0x09,0x06};
            case 'Q': return {0x3E,0x41,0x51,0x21,0x5E}; case 'R': return {0x7F,0x09,0x19,0x29,0x46};
            case 'S': return {0x46,0x49,0x49,0x49,0x31}; case 'T': return {0x01,0x01,0x7F,0x01,0x01};
            case 'U': return {0x3F,0x40,0x40,0x40,0x3F}; case 'V': return {0x1F,0x20,0x40,0x20,0x1F};
            case 'W': return {0x3F,0x40,0x38,0x40,0x3F}; case 'X': return {0x63,0x14,0x08,0x14,0x63};
            case 'Y': return {0x07,0x08,0x70,0x08,0x07}; case 'Z': return {0x61,0x51,0x49,0x45,0x43};
            default: return {0x02,0x01,0x51,0x09,0x06};
            }
        }

        void Clear() { framebuffer_.fill(0); }
        void Pixel(int x, int y, bool on = true)
        {
            if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
            uint8_t &cell = framebuffer_[x + (y / 8) * 128];
            uint8_t mask = static_cast<uint8_t>(1u << (y & 7));
            if (on) cell |= mask; else cell &= static_cast<uint8_t>(~mask);
        }
        void DrawText(int x, int y, const std::string &text, int scale)
        {
            for (char c : text)
            {
                if (x + 6 * scale > 128) break;
                auto glyph = Glyph(c);
                for (int gx = 0; gx < 5; ++gx)
                    for (int gy = 0; gy < 7; ++gy)
                        if (glyph[gx] & (1u << gy))
                            FillRect(x + gx * scale, y + gy * scale, scale, scale);
                x += 6 * scale;
            }
        }
        void DrawTextCentered(
            int centreX,
            int y,
            const std::string &text,
            int scale,
            size_t maximumCharacters)
        {
            std::string clipped = text.substr(0, maximumCharacters);
            const int width = static_cast<int>(clipped.size()) * 6 * scale;
            DrawText(centreX - width / 2, y, clipped, scale);
        }
        void DrawKnob(int centreX, const GpioDisplayControl &control, bool active)
        {
            constexpr int centreY = 21;
            constexpr int radius = 8;
            DrawCircle(centreX, centreY, radius);
            if (control.assigned)
            {
                constexpr float pi = 3.14159265358979323846f;
                const float normalized = std::clamp(control.normalizedValue, 0.0f, 1.0f);
                const float angle = (-225.0f + normalized * 270.0f) * pi / 180.0f;
                const int x = centreX + static_cast<int>(
                                              std::lround(std::cos(angle) * (radius - 2)));
                const int y = centreY + static_cast<int>(
                                              std::lround(std::sin(angle) * (radius - 2)));
                DrawLine(centreX, centreY, x, y);
                if (active)
                    FillRect(centreX - 2, centreY - 2, 5, 5);
                else
                    FillRect(centreX - 1, centreY - 1, 3, 3);
            }
            else
            {
                DrawHorizontal(centreX - 4, centreY, 9);
            }

            DrawTextCentered(centreX, 33,
                             control.assigned ? control.label : "NONE",
                             1, 5);
            DrawTextCentered(centreX, 43,
                             control.assigned ? control.value : "--",
                             1, 5);
            if (active)
                DrawHorizontal(centreX - 13, 53, 27);
        }

        // A whole-width bar along the bottom edge, showing how far through the
        // complete loaded parameter sequence the four shown knobs are.
        void DrawScrollBar(int32_t index, int32_t positions)
        {
            if (positions <= 1)
                return;
            const int lastPosition = positions - 1;
            const int clamped = std::clamp<int>(index, 0, lastPosition);
            const int width = std::max(8, 128 / positions);
            const int x = clamped * (128 - width) / lastPosition;
            DrawHorizontal(0, 62, 128);
            FillRect(x, 61, width, 3);
        }
        void FillRect(int x, int y, int width, int height)
        {
            for (int px = x; px < x + width; ++px)
                for (int py = y; py < y + height; ++py) Pixel(px, py);
        }
        void DrawHorizontal(int x, int y, int width) { for (int px=x; px<x+width; ++px) Pixel(px,y); }
        void DrawRect(int x, int y, int width, int height)
        {
            DrawHorizontal(x,y,width); DrawHorizontal(x,y+height-1,width);
            for (int py=y; py<y+height; ++py) { Pixel(x,py); Pixel(x+width-1,py); }
        }
        void DrawCircle(int centreX, int centreY, int radius)
        {
            int x = radius;
            int y = 0;
            int error = 1 - radius;
            while (x >= y)
            {
                Pixel(centreX + x, centreY + y);
                Pixel(centreX + y, centreY + x);
                Pixel(centreX - y, centreY + x);
                Pixel(centreX - x, centreY + y);
                Pixel(centreX - x, centreY - y);
                Pixel(centreX - y, centreY - x);
                Pixel(centreX + y, centreY - x);
                Pixel(centreX + x, centreY - y);
                ++y;
                if (error < 0)
                    error += 2 * y + 1;
                else
                {
                    --x;
                    error += 2 * (y - x) + 1;
                }
            }
        }
        void DrawLine(int x0, int y0, int x1, int y1)
        {
            int dx=std::abs(x1-x0), sx=x0<x1?1:-1, dy=-std::abs(y1-y0), sy=y0<y1?1:-1, error=dx+dy;
            while (true) { Pixel(x0,y0); if (x0==x1 && y0==y1) break; int e2=2*error; if(e2>=dy){error+=dy;x0+=sx;} if(e2<=dx){error+=dx;y0+=sy;} }
        }
        bool SendCommands(const uint8_t *commands, size_t count, std::string *error)
        {
            std::vector<uint8_t> data(count + 1); data[0] = 0x00;
            std::copy(commands, commands + count, data.begin() + 1);
            return device_.Write(data.data(), data.size(), error);
        }
        bool Flush(std::string *error)
        {
            const uint8_t window[]{0x21,0,127,0x22,0,7};
            if (!SendCommands(window,sizeof(window),error)) return false;
            for (size_t offset=0; offset<framebuffer_.size(); offset+=31)
            {
                size_t count=std::min<size_t>(31,framebuffer_.size()-offset);
                uint8_t data[32]; data[0]=0x40;
                std::copy_n(framebuffer_.data()+offset,count,data+1);
                if (!device_.Write(data,count+1,error)) return false;
            }
            return true;
        }

        I2cDevice device_;
        std::array<uint8_t, 1024> framebuffer_{};
    };

    class Ht16k33Matrix
    {
    public:
        ~Ht16k33Matrix()
        {
            if (opened_)
            {
                std::string ignored;
                Clear(&ignored);
            }
        }

        bool Open(const GpioLedMatrixSettings &settings, std::string *error)
        {
            if (!device_.Open(settings.i2cDevice_, settings.i2cAddress_, error))
                return false;
            const uint8_t oscillatorOn = 0x21;
            const uint8_t displayOn = 0x81;
            const uint8_t brightness = static_cast<uint8_t>(
                0xE0 | std::clamp(settings.brightness_, 0, 15));
            opened_ = device_.Write(&oscillatorOn, 1, error) &&
                      device_.Write(&displayOn, 1, error) &&
                      device_.Write(&brightness, 1, error) &&
                      Clear(error);
            return opened_;
        }

        bool Draw(
            const std::array<float, 128> &samples,
            const GpioLedMatrixSettings &settings,
            std::string *error)
        {
            std::array<bool, 25> pixels{};
            if (settings.calibrationMode_)
            {
                // Asymmetric arrow: the point is logical top and the short
                // tail is logical left, making rotation and mirroring obvious.
                constexpr std::array<const char *, 5> pattern{
                    ".#...", "###..", ".#...", "##...", ".#..."};
                for (int y = 0; y < 5; ++y)
                    for (int x = 0; x < 5; ++x)
                        pixels[static_cast<size_t>(y * 5 + x)] = pattern[y][x] == '#';
            }
            else if (settings.mode_ == static_cast<int32_t>(GpioLedMatrixMode::Droplets))
                DrawDroplets(samples, settings, &pixels);
            else
                DrawSpectrum(samples, settings, &pixels);

            // The enclosure hides the four logical corners.
            pixels[0] = pixels[4] = pixels[20] = pixels[24] = false;
            std::array<uint16_t, 8> rows{};
            for (int logicalY = 0; logicalY < 5; ++logicalY)
            {
                for (int logicalX = 0; logicalX < 5; ++logicalX)
                {
                    if (!pixels[static_cast<size_t>(logicalY * 5 + logicalX)])
                        continue;
                    int x = settings.mirror_ ? 4 - logicalX : logicalX;
                    int y = logicalY;
                    for (int turn = 0; turn < settings.rotation_; ++turn)
                    {
                        const int oldX = x;
                        x = 4 - y;
                        y = oldX;
                    }
                    x += settings.originX_;
                    y += settings.originY_;
                    if (x < 0 || x >= 8 || y < 0 || y >= 8)
                        continue;
                    // Adafruit's 8x8 backpack has its x wiring shifted by one.
                    const int ramX = (x + 7) % 8;
                    rows[static_cast<size_t>(y)] |= static_cast<uint16_t>(1u << ramX);
                }
            }
            return Flush(rows, error);
        }

    private:
        void DrawSpectrum(
            const std::array<float, 128> &samples,
            const GpioLedMatrixSettings &settings,
            std::array<bool, 25> *pixels)
        {
            constexpr float pi = 3.14159265358979323846f;
            constexpr std::array<float, 5> frequencies{
                62.5f, 125.0f, 250.0f, 500.0f, 1000.0f};
            constexpr float sampleRate = 4000.0f;
            float windowSum = 0.0f;
            for (size_t i = 0; i < samples.size(); ++i)
                windowSum += 0.5f - 0.5f * std::cos(
                    2.0f * pi * static_cast<float>(i) /
                    static_cast<float>(samples.size() - 1));

            for (size_t band = 0; band < frequencies.size(); ++band)
            {
                float real = 0.0f;
                float imaginary = 0.0f;
                for (size_t i = 0; i < samples.size(); ++i)
                {
                    const float window = 0.5f - 0.5f * std::cos(
                        2.0f * pi * static_cast<float>(i) /
                        static_cast<float>(samples.size() - 1));
                    const float angle = 2.0f * pi * frequencies[band] *
                                        static_cast<float>(i) / sampleRate;
                    real += samples[i] * window * std::cos(angle);
                    imaginary -= samples[i] * window * std::sin(angle);
                }
                const float amplitude = std::max(
                    1.0e-6f,
                    2.0f * std::sqrt(real * real + imaginary * imaginary) /
                        std::max(windowSum, 1.0f));
                const float db = 20.0f * std::log10(amplitude);
                const float target = std::clamp(
                    (db - settings.floorDb_) / -settings.floorDb_, 0.0f, 1.0f);
                levels_[band] = std::max(target, levels_[band] * settings.decay_);
                const int height = static_cast<int>(std::ceil(levels_[band] * 5.0f));
                for (int y = 5 - height; y < 5; ++y)
                    if (y >= 0)
                        (*pixels)[static_cast<size_t>(y * 5 + static_cast<int>(band))] = true;
            }
        }

        void DrawDroplets(
            const std::array<float, 128> &samples,
            const GpioLedMatrixSettings &settings,
            std::array<bool, 25> *pixels)
        {
            float sumSquares = 0.0f;
            float peak = 0.0f;
            for (float sample : samples)
            {
                sumSquares += sample * sample;
                peak = std::max(peak, std::abs(sample));
            }
            const float rms = std::sqrt(sumSquares / static_cast<float>(samples.size()));
            // Peak gives a plucked bass note an immediate splash; RMS keeps
            // the display coupled to the body of the held note.
            const float level = std::max(rms * 1.45f, peak * 0.78f);
            const float db = 20.0f * std::log10(std::max(level, 1.0e-6f));
            const float targetEnergy = std::clamp(
                (db - settings.floorDb_) / -settings.floorDb_, 0.0f, 1.0f);
            const float frameSeconds = std::clamp(
                static_cast<float>(settings.refreshIntervalMs_) / 1000.0f,
                0.005f, 0.1f);

            const float attackAlpha = 1.0f - std::exp(-frameSeconds / 0.008f);
            const float releaseSeconds = 0.075f + settings.decay_ * 0.28f;
            const float releaseAlpha = 1.0f - std::exp(-frameSeconds / releaseSeconds);
            const float previousEnergy = smoothedDropletEnergy_;
            const float envelopeAlpha =
                targetEnergy > smoothedDropletEnergy_ ? attackAlpha : releaseAlpha;
            smoothedDropletEnergy_ +=
                (targetEnergy - smoothedDropletEnergy_) * envelopeAlpha;
            const float slowAlpha = 1.0f - std::exp(-frameSeconds / 0.14f);
            slowDropletEnergy_ +=
                (smoothedDropletEnergy_ - slowDropletEnergy_) * slowAlpha;
            const float onset = std::max(
                0.0f, smoothedDropletEnergy_ - slowDropletEnergy_);
            const float attack = std::max(
                0.0f, smoothedDropletEnergy_ - previousEnergy);

            const bool active = dropletWasActive_
                                    ? smoothedDropletEnergy_ > 0.025f
                                    : smoothedDropletEnergy_ > 0.055f;
            const bool started = active && !dropletWasActive_;
            dropletWasActive_ = active;
            dropletCooldown_ = std::max(0.0f, dropletCooldown_ - frameSeconds);

            if (active &&
                (started || (attack > 0.055f && dropletCooldown_ <= 0.0f)))
            {
                // A note hit lands in the middle and lights a small immediate
                // crown before the height field carries it out as a ripple.
                AddDropletImpact(
                    12,
                    1.05f + smoothedDropletEnergy_ * 1.35f + onset * 0.55f,
                    true);
                dropletAccumulator_ = 0.35f;
                dropletCooldown_ = 0.045f;
            }

            if (active)
            {
                // Keep adding smaller impacts while a note is held. Louder
                // playing raises the ripple rate as well as the visible area.
                dropletAccumulator_ += frameSeconds *
                    (2.5f + smoothedDropletEnergy_ * 8.5f);
                while (dropletAccumulator_ >= 1.0f)
                {
                    constexpr std::array<size_t, 5> innerCells{7, 11, 12, 13, 17};
                    const uint32_t audioSeed = static_cast<uint32_t>(
                        std::lround(
                            std::abs(samples[dropletRandom_ % samples.size()]) *
                            65535.0f));
                    dropletRandom_ =
                        dropletRandom_ * 1664525u + 1013904223u + audioSeed;
                    AddDropletImpact(
                        innerCells[dropletRandom_ % innerCells.size()],
                        0.32f + smoothedDropletEnergy_ * 0.72f,
                        false);
                    dropletAccumulator_ -= 1.0f;
                }
            }
            else
            {
                dropletAccumulator_ = 0.0f;
            }

            // The simulation is normalized to a 60 Hz frame. Substeps keep it
            // stable if the I2C worker is temporarily delayed.
            const float frameScale = frameSeconds * 60.0f;
            const int substeps = std::clamp(
                static_cast<int>(std::ceil(frameScale)), 1, 12);
            const float dt = frameScale / static_cast<float>(substeps);
            for (int step = 0; step < substeps; ++step)
            {
                std::array<float, 25> nextHeight{};
                std::array<float, 25> nextVelocity{};
                for (int y = 0; y < 5; ++y)
                {
                    for (int x = 0; x < 5; ++x)
                    {
                        const size_t cell = static_cast<size_t>(y * 5 + x);
                        const float centre = dropletHeight_[cell];
                        float laplacian = -4.0f * centre;
                        if (x > 0) laplacian += dropletHeight_[cell - 1];
                        if (x < 4) laplacian += dropletHeight_[cell + 1];
                        if (y > 0) laplacian += dropletHeight_[cell - 5];
                        if (y < 4) laplacian += dropletHeight_[cell + 5];
                        const float velocity =
                            (dropletVelocity_[cell] + laplacian * 0.28f * dt) *
                            std::pow(0.78f + settings.decay_ * 0.18f, dt);
                        nextVelocity[cell] = std::clamp(velocity, -4.0f, 4.0f);
                        nextHeight[cell] = std::clamp(
                            (centre + velocity * dt) *
                                std::pow(0.94f + settings.decay_ * 0.05f, dt),
                            -4.0f, 4.0f);
                    }
                }
                nextHeight[0] = nextHeight[4] = nextHeight[20] = nextHeight[24] = 0.0f;
                nextVelocity[0] = nextVelocity[4] = nextVelocity[20] = nextVelocity[24] = 0.0f;
                dropletHeight_ = nextHeight;
                dropletVelocity_ = nextVelocity;
            }

            // The matrix is monochrome, so use the number of lit surface
            // points as its amplitude axis. Their positions still come from
            // the wave field, preserving the moving splash/ripple shape.
            constexpr std::array<size_t, 21> visibleCells{
                1, 2, 3,
                5, 6, 7, 8, 9,
                10, 11, 12, 13, 14,
                15, 16, 17, 18, 19,
                21, 22, 23};
            std::array<std::pair<float, size_t>, visibleCells.size()> surface{};
            for (size_t index = 0; index < visibleCells.size(); ++index)
            {
                const size_t cell = visibleCells[index];
                surface[index] = {std::abs(dropletHeight_[cell]), cell};
            }
            std::stable_sort(
                surface.begin(), surface.end(),
                [](const auto &left, const auto &right)
                { return left.first > right.first; });
            const int visibleCount = smoothedDropletEnergy_ < 0.008f
                                         ? 0
                                         : std::clamp(
                                               static_cast<int>(std::ceil(
                                                   std::pow(
                                                       smoothedDropletEnergy_, 0.78f) *
                                                   14.0f)),
                                               1, 14);
            for (int index = 0; index < visibleCount; ++index)
                (*pixels)[surface[static_cast<size_t>(index)].second] = true;
        }

        void AddDropletImpact(size_t cell, float strength, bool splash)
        {
            dropletHeight_[cell] += strength * 1.12f;
            dropletVelocity_[cell] += strength * 0.82f;
            if (!splash)
                return;

            const int x = static_cast<int>(cell % 5);
            const int y = static_cast<int>(cell / 5);
            for (int offsetY = -1; offsetY <= 1; ++offsetY)
            {
                for (int offsetX = -1; offsetX <= 1; ++offsetX)
                {
                    if (offsetX == 0 && offsetY == 0)
                        continue;
                    const int neighbourX = x + offsetX;
                    const int neighbourY = y + offsetY;
                    if (neighbourX < 0 || neighbourX >= 5 ||
                        neighbourY < 0 || neighbourY >= 5)
                        continue;
                    const size_t neighbour = static_cast<size_t>(
                        neighbourY * 5 + neighbourX);
                    const bool cardinal = offsetX == 0 || offsetY == 0;
                    dropletHeight_[neighbour] +=
                        strength * (cardinal ? 0.38f : 0.14f);
                    dropletVelocity_[neighbour] +=
                        strength * (cardinal ? 0.16f : 0.06f);
                }
            }
        }

        bool Clear(std::string *error)
        {
            std::array<uint16_t, 8> rows{};
            return Flush(rows, error);
        }

        bool Flush(const std::array<uint16_t, 8> &rows, std::string *error)
        {
            uint8_t data[17]{};
            data[0] = 0x00;
            for (size_t row = 0; row < rows.size(); ++row)
            {
                data[1 + row * 2] = static_cast<uint8_t>(rows[row] & 0xFF);
                data[2 + row * 2] = static_cast<uint8_t>(rows[row] >> 8);
            }
            return device_.Write(data, sizeof(data), error);
        }

        I2cDevice device_;
        std::array<float, 5> levels_{};
        std::array<float, 25> dropletHeight_{};
        std::array<float, 25> dropletVelocity_{};
        float smoothedDropletEnergy_ = 0.0f;
        float slowDropletEnergy_ = 0.0f;
        float dropletAccumulator_ = 0.0f;
        float dropletCooldown_ = 0.0f;
        bool dropletWasActive_ = false;
        uint32_t dropletRandom_ = 0x50495045u;
        bool opened_ = false;
    };
#else
    class Ssd1306Display {};
    class Ht16k33Matrix {};
#endif

    struct RuntimeInput
    {
        explicit RuntimeInput(const GpioInputConfiguration &configuration)
            : configuration(configuration)
        {
            status.inputId_ = configuration.id_;
        }

        GpioInputConfiguration configuration;
        GpioInputStatus status;
#if defined(__linux__)
        GpioLineHandle lineHandle;
        SeesawEncoder encoder;
        SeesawNavigationEncoder navigation;
#endif
        bool initialized = false;
        bool available = true;
        bool stableDigitalValue = false;
        bool candidateDigitalValue = false;
        uint32_t stableNavigationButtons = 0;
        uint32_t candidateNavigationButtons = 0;
        uint32_t navigationActivityPending = 0;
        std::array<std::chrono::steady_clock::time_point, 5> navigationCandidateSince{};
        bool encoderButtonActivityPending = false;
        bool emittedPressedInitialized = false;
        bool lastEmittedPressed = false;
        float filteredValue = 0.0f;
        float emittedValue = 0.0f;
        std::chrono::steady_clock::time_point candidateSince{};
        std::chrono::steady_clock::time_point nextAnalogRead{};
        std::chrono::steady_clock::time_point nextEncoderRead{};
    };

    class GpioManagerImpl final : public GpioManager
    {
    public:
        ~GpioManagerImpl() override
        {
            Close();
        }

        void SetEventCallback(EventCallback callback) override
        {
            std::lock_guard lock(callbackMutex_);
            eventCallback_ = std::move(callback);
        }

        void SetStatusCallback(StatusCallback callback) override
        {
            std::lock_guard lock(callbackMutex_);
            statusCallback_ = std::move(callback);
        }

        void SetWaveformProvider(WaveformProvider callback) override
        {
            std::lock_guard lock(callbackMutex_);
            waveformProvider_ = std::move(callback);
        }

        void SetTunerSampleProvider(TunerSampleProvider callback) override
        {
            std::lock_guard lock(callbackMutex_);
            tunerSampleProvider_ = std::move(callback);
        }

        void ShowDisplayMessage(const GpioDisplayMessage &message) override
        {
            std::lock_guard lock(displayMutex_);
            displayMessage_ = message;
            displayOverlayType_ = 1;
            displayMessageSequence_++;
        }

        // Overlay changes bump the message sequence, which makes the worker draw
        // on its next 5ms tick. None of the display entry points may set
        // refreshRequested_: that flag re-emits every input's current state, and
        // a Direct-mode mapping aimed at the effect currently on the dashboard
        // would then drive an endless input -> SetControl -> dashboard cycle.
        void ClearDisplayMessage() override
        {
            std::lock_guard lock(displayMutex_);
            displayMessage_ = {};
            displayOverlayType_ = 0;
            displayMessageSequence_++;
        }

        // The persistent dashboard has no sequence number, so it is picked up by
        // the next scheduled draw rather than pre-empting one. Web-UI parameter
        // edits therefore reach the OLED within one refresh interval instead of
        // pushing a full 1KB frame onto the encoders' I2C bus per change.
        void ShowControlDashboard(const GpioDisplayDashboard &dashboard) override
        {
            std::lock_guard lock(displayMutex_);
            displayDashboard_ = dashboard;
        }

        void ShowTemporaryControlDashboard(
            const GpioDisplayDashboard &dashboard) override
        {
            std::lock_guard lock(displayMutex_);
            displayDashboard_ = dashboard;
            displayOverlayType_ = 2;
            displayMessageSequence_++;
        }

        void ShowTemporaryMenu(const GpioDisplayMenu &menu) override
        {
            std::lock_guard lock(displayMutex_);
            displayMenu_ = menu;
            displayOverlayType_ = 3;
            displayMessageSequence_++;
        }

        GpioDisplayMode CycleDisplayMode() override
        {
            GpioDisplayMode result;
            {
                std::lock_guard lock(displayMutex_);
                do
                {
                    int32_t next =
                        (static_cast<int32_t>(displayMode_) + 1) % 3;
                    displayMode_ = static_cast<GpioDisplayMode>(next);
                } while (displayMode_ == GpioDisplayMode::Waveform &&
                         !waveformModeEnabled_);
                result = displayMode_;
            }
            if (result == GpioDisplayMode::Tuner)
                tunerResetRequested_.store(true);
            redrawRequested_.store(true);
            return result;
        }

        GpioDisplayMode GetDisplayMode() const override
        {
            std::lock_guard lock(displayMutex_);
            return displayMode_;
        }

        void SetDisplayMode(GpioDisplayMode mode) override
        {
            if (mode < GpioDisplayMode::Controls || mode > GpioDisplayMode::Blank)
                mode = GpioDisplayMode::Controls;
            {
                std::lock_guard lock(displayMutex_);
                displayMode_ = mode;
            }
            if (mode == GpioDisplayMode::Tuner)
                tunerResetRequested_.store(true);
            redrawRequested_.store(true);
        }

        void Configure(const GpioSettings &settings) override
        {
            GpioManager::Validate(settings);
            Close();
            {
                std::lock_guard lock(displayMutex_);
                displayMode_ = static_cast<GpioDisplayMode>(settings.display_.passiveMode_);
                waveformModeEnabled_ = settings.display_.waveformEnabled_;
                displayMessage_ = {};
                displayMenu_ = {};
                displayOverlayType_ = 0;
                displayMessageSequence_ = 0;
            }
            tunerResetRequested_.store(true);
            {
                std::lock_guard lock(statusMutex_);
                statuses_.clear();
                for (const auto &input : settings.inputs_)
                {
                    if (input.enabled_)
                    {
                        GpioInputStatus status;
                        status.inputId_ = input.id_;
                        statuses_.push_back(status);
                    }
                }
            }
            if (!settings.enabled_)
            {
                return;
            }
            inputThread_ = std::make_unique<std::jthread>(
                [this, settings](std::stop_token stopToken)
                {
                    RunInputs(stopToken, settings);
                });
            if (settings.display_.enabled_ || settings.ledMatrix_.enabled_)
            {
                displayThread_ = std::make_unique<std::jthread>(
                    [this, settings](std::stop_token stopToken)
                    {
                        RunDisplays(stopToken, settings);
                    });
            }
        }

        void Refresh() override
        {
            refreshRequested_.store(true);
        }

        std::vector<GpioInputStatus> GetStatuses() const override
        {
            std::lock_guard lock(statusMutex_);
            return statuses_;
        }

        void Close() override
        {
            if (inputThread_)
                inputThread_->request_stop();
            if (displayThread_)
                displayThread_->request_stop();
            inputThread_.reset();
            displayThread_.reset();
            refreshRequested_.store(false);
            redrawRequested_.store(false);
        }

    private:
        void EmitEvent(RuntimeInput &runtime, bool initial,
                       GpioInputEventType eventType = GpioInputEventType::Value, int32_t delta = 0)
        {
            EventCallback callback;
            {
                std::lock_guard lock(callbackMutex_);
                callback = eventCallback_;
            }
            if (callback)
            {
                GpioInputEvent event;
                event.inputId = runtime.configuration.id_;
                event.value = eventType == GpioInputEventType::EncoderButton
                                  ? (runtime.status.buttonPressed_ ? 1.0f : 0.0f)
                                  : runtime.status.value_;
                event.pressed = event.value >= 0.5f;
                event.risingEdge = eventType != GpioInputEventType::EncoderTurn &&
                                   !initial && runtime.emittedPressedInitialized &&
                                   event.pressed && !runtime.lastEmittedPressed;
                event.initial = initial;
                event.eventType = eventType;
                event.delta = delta;
                if (eventType != GpioInputEventType::EncoderTurn)
                {
                    runtime.lastEmittedPressed = event.pressed;
                    runtime.emittedPressedInitialized = true;
                }
                callback(event);
            }
        }

        void EmitNavigationButton(
            RuntimeInput &runtime,
            GpioNavigationButton button,
            bool pressed,
            bool initial)
        {
            EventCallback callback;
            {
                std::lock_guard lock(callbackMutex_);
                callback = eventCallback_;
            }
            if (!callback)
                return;
            GpioInputEvent event;
            event.inputId = runtime.configuration.id_;
            event.value = pressed ? 1.0f : 0.0f;
            event.pressed = pressed;
            event.risingEdge = pressed && !initial;
            event.initial = initial;
            event.eventType = GpioInputEventType::EncoderButton;
            event.navigationButton = button;
            callback(event);
        }

        void UpdateStatus(RuntimeInput &runtime, bool notify)
        {
            {
                std::lock_guard lock(statusMutex_);
                auto found = std::find_if(
                    statuses_.begin(), statuses_.end(),
                    [&runtime](const GpioInputStatus &status)
                    {
                        return status.inputId_ == runtime.status.inputId_;
                    });
                if (found != statuses_.end())
                {
                    *found = runtime.status;
                }
            }
            if (notify)
            {
                StatusCallback callback;
                {
                    std::lock_guard lock(callbackMutex_);
                    callback = statusCallback_;
                }
                if (callback)
                {
                    callback(runtime.status);
                }
            }
        }

        void SetError(RuntimeInput &runtime, const std::string &error)
        {
            bool changed = runtime.status.connected_ || runtime.status.error_ != error;
            runtime.status.connected_ = false;
            runtime.status.error_ = error;
            UpdateStatus(runtime, changed);
        }

        void ProcessDigital(RuntimeInput &runtime, std::chrono::steady_clock::time_point now, bool refresh)
        {
#if defined(__linux__)
            bool electricalValue = false;
            std::string error;
            if (!runtime.lineHandle.Read(&electricalValue, &error))
            {
                SetError(runtime, error);
                return;
            }
            bool logicalValue = runtime.configuration.activeLow_ ? !electricalValue : electricalValue;
            if (!runtime.initialized)
            {
                runtime.initialized = true;
                runtime.stableDigitalValue = logicalValue;
                runtime.candidateDigitalValue = logicalValue;
                runtime.candidateSince = now;
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                runtime.status.value_ = logicalValue ? 1.0f : 0.0f;
                UpdateStatus(runtime, true);
                EmitEvent(runtime, true);
                return;
            }

            if (logicalValue != runtime.candidateDigitalValue)
            {
                runtime.candidateDigitalValue = logicalValue;
                runtime.candidateSince = now;
            }
            else if (runtime.candidateDigitalValue != runtime.stableDigitalValue &&
                     now - runtime.candidateSince >= std::chrono::milliseconds(runtime.configuration.debounceMs_))
            {
                runtime.stableDigitalValue = runtime.candidateDigitalValue;
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                runtime.status.value_ = runtime.stableDigitalValue ? 1.0f : 0.0f;
                UpdateStatus(runtime, true);
                EmitEvent(runtime, false);
            }
            else if (!runtime.status.connected_)
            {
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                UpdateStatus(runtime, true);
            }

            if (refresh)
            {
                EmitEvent(runtime, true);
            }
#else
            (void)runtime;
            (void)now;
            (void)refresh;
#endif
        }

        void ProcessAnalog(RuntimeInput &runtime, std::chrono::steady_clock::time_point now, bool refresh)
        {
            if (!refresh && now < runtime.nextAnalogRead)
            {
                return;
            }
            runtime.nextAnalogRead = now + std::chrono::milliseconds(runtime.configuration.pollIntervalMs_);

            std::ifstream stream(runtime.configuration.analogPath_);
            double rawValue = 0.0;
            if (!(stream >> rawValue))
            {
                SetError(runtime, "Unable to read analog channel " + runtime.configuration.analogPath_);
                return;
            }

            double range = static_cast<double>(runtime.configuration.analogMax_ - runtime.configuration.analogMin_);
            float normalized = static_cast<float>((rawValue - runtime.configuration.analogMin_) / range);
            normalized = std::clamp(normalized, 0.0f, 1.0f);

            if (!runtime.initialized)
            {
                runtime.initialized = true;
                runtime.filteredValue = normalized;
                runtime.emittedValue = normalized;
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                runtime.status.value_ = normalized;
                UpdateStatus(runtime, true);
                EmitEvent(runtime, true);
                return;
            }

            runtime.filteredValue = runtime.configuration.smoothing_ * runtime.filteredValue +
                                    (1.0f - runtime.configuration.smoothing_) * normalized;
            bool changed = std::abs(runtime.filteredValue - runtime.emittedValue) >= runtime.configuration.deadband_;
            if (changed || refresh)
            {
                if (changed)
                {
                    runtime.emittedValue = runtime.filteredValue;
                }
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                runtime.status.value_ = runtime.emittedValue;
                UpdateStatus(runtime, changed);
                EmitEvent(runtime, refresh);
            }
            else if (!runtime.status.connected_)
            {
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                UpdateStatus(runtime, true);
            }
        }

        void ProcessEncoder(RuntimeInput &runtime, std::chrono::steady_clock::time_point now, bool refresh)
        {
#if defined(__linux__)
            if (!refresh && now < runtime.nextEncoderRead)
                return;
            runtime.nextEncoderRead = now + std::chrono::milliseconds(runtime.configuration.encoderPollIntervalMs_);
            int32_t delta = 0;
            bool pressed = false;
            bool buttonActivity = false;
            std::string error;
            if (!runtime.encoder.Read(&delta, &pressed, &buttonActivity, &error))
            {
                SetError(runtime, error);
                return;
            }
            if (!runtime.initialized)
            {
                runtime.initialized = true;
                runtime.stableDigitalValue = pressed;
                runtime.candidateDigitalValue = pressed;
                runtime.candidateSince = now;
                runtime.encoderButtonActivityPending = false;
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                runtime.status.encoderPosition_ = 0;
                runtime.status.buttonPressed_ = pressed;
                runtime.status.value_ = pressed ? 1.0f : 0.0f;
                UpdateStatus(runtime, true);
                EmitEvent(runtime, true, GpioInputEventType::EncoderButton);
                return;
            }

            if (buttonActivity)
                runtime.encoderButtonActivityPending = true;

            if (delta != 0)
            {
                if (runtime.configuration.encoderReversed_) delta = -delta;
                int32_t unitDelta = delta > 0 ? 1 : -1;
                int32_t count = delta > 0 ? delta : -delta;
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                runtime.status.encoderPosition_ = 0;
                UpdateStatus(runtime, true);
                for (int32_t i = 0; i < count; ++i)
                {
                    EmitEvent(runtime, false, GpioInputEventType::EncoderTurn, unitDelta);
                }
            }

            if (pressed != runtime.candidateDigitalValue)
            {
                runtime.candidateDigitalValue = pressed;
                runtime.candidateSince = now;
            }
            else if (runtime.candidateDigitalValue != runtime.stableDigitalValue &&
                     now - runtime.candidateSince >= std::chrono::milliseconds(runtime.configuration.debounceMs_))
            {
                runtime.stableDigitalValue = pressed;
                runtime.status.buttonPressed_ = pressed;
                runtime.status.value_ = pressed ? 1.0f : 0.0f;
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                UpdateStatus(runtime, true);
                EmitEvent(runtime, false, GpioInputEventType::EncoderButton);
                runtime.encoderButtonActivityPending = false;
            }
            else if (!runtime.status.connected_)
            {
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                UpdateStatus(runtime, true);
            }

            // If both edges occurred between samples, the GPIO interrupt flag
            // is the only surviving evidence. Emit one complete click so quick
            // taps cannot disappear behind OLED I2C traffic.
            if (runtime.encoderButtonActivityPending &&
                !pressed &&
                !runtime.candidateDigitalValue &&
                !runtime.stableDigitalValue)
            {
                runtime.encoderButtonActivityPending = false;
                runtime.status.buttonPressed_ = true;
                runtime.status.value_ = 1.0f;
                UpdateStatus(runtime, true);
                EmitEvent(runtime, false, GpioInputEventType::EncoderButton);
                runtime.status.buttonPressed_ = false;
                runtime.status.value_ = 0.0f;
                UpdateStatus(runtime, true);
                EmitEvent(runtime, false, GpioInputEventType::EncoderButton);
            }
            if (refresh)
                EmitEvent(runtime, true, GpioInputEventType::EncoderButton);
#else
            (void)runtime; (void)now; (void)refresh;
#endif
        }

        void ProcessNavigation(
            RuntimeInput &runtime,
            std::chrono::steady_clock::time_point now,
            bool refresh)
        {
#if defined(__linux__)
            if (!refresh && now < runtime.nextEncoderRead)
                return;
            runtime.nextEncoderRead = now +
                                      std::chrono::milliseconds(runtime.configuration.encoderPollIntervalMs_);
            int32_t delta = 0;
            uint32_t buttons = 0;
            uint32_t buttonActivity = 0;
            std::string error;
            if (!runtime.navigation.Read(&delta, &buttons, &buttonActivity, &error))
            {
                SetError(runtime, error);
                return;
            }
            if (!runtime.initialized)
            {
                runtime.initialized = true;
                runtime.stableNavigationButtons = buttons;
                runtime.candidateNavigationButtons = buttons;
                runtime.navigationActivityPending = 0;
                runtime.navigationCandidateSince.fill(now);
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                runtime.status.navigationButtons_ = buttons;
                runtime.status.buttonPressed_ = buttons != 0;
                UpdateStatus(runtime, true);
                return;
            }

            runtime.navigationActivityPending |= buttonActivity;

            if (delta != 0)
            {
                if (runtime.configuration.encoderReversed_)
                    delta = -delta;
                const int32_t unitDelta = delta > 0 ? 1 : -1;
                const int32_t count = std::abs(delta);
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                UpdateStatus(runtime, true);
                for (int32_t i = 0; i < count; ++i)
                    EmitEvent(runtime, false, GpioInputEventType::EncoderTurn, unitDelta);
            }

            std::array<std::pair<GpioNavigationButton, bool>, 5> events{};
            size_t eventCount = 0;
            bool stableStateChanged = false;
            for (int bit = 0; bit < 5; ++bit)
            {
                const uint32_t mask = 1u << bit;
                const bool sampled = (buttons & mask) != 0;
                const bool candidate = (runtime.candidateNavigationButtons & mask) != 0;
                const bool stable = (runtime.stableNavigationButtons & mask) != 0;
                if (sampled != candidate)
                {
                    if (sampled)
                        runtime.candidateNavigationButtons |= mask;
                    else
                        runtime.candidateNavigationButtons &= ~mask;
                    runtime.navigationCandidateSince[static_cast<size_t>(bit)] = now;
                }
                else if (candidate != stable &&
                         now - runtime.navigationCandidateSince[static_cast<size_t>(bit)] >=
                             std::chrono::milliseconds(runtime.configuration.debounceMs_))
                {
                    if (candidate)
                        runtime.stableNavigationButtons |= mask;
                    else
                        runtime.stableNavigationButtons &= ~mask;
                    runtime.navigationActivityPending &= ~mask;
                    stableStateChanged = true;
                    events[eventCount++] = {
                        static_cast<GpioNavigationButton>(bit + 1), candidate};
                }
            }

            if (stableStateChanged)
            {
                runtime.status.navigationButtons_ = runtime.stableNavigationButtons;
                runtime.status.buttonPressed_ = runtime.stableNavigationButtons != 0;
                runtime.status.value_ = runtime.status.buttonPressed_ ? 1.0f : 0.0f;
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                UpdateStatus(runtime, true);
                for (size_t index = 0; index < eventCount; ++index)
                    EmitNavigationButton(
                        runtime, events[index].first, events[index].second, false);
            }
            else if (!runtime.status.connected_)
            {
                runtime.status.connected_ = true;
                runtime.status.error_.clear();
                UpdateStatus(runtime, true);
            }

            // INTFLAG preserves a press that began and ended between polls.
            // Navigation only acts on the rising edge, but emit the matching
            // release too so event consumers always see a complete gesture.
            for (int bit = 0; bit < 5; ++bit)
            {
                const uint32_t mask = 1u << bit;
                if ((runtime.navigationActivityPending & mask) == 0 ||
                    (buttons & mask) != 0 ||
                    (runtime.candidateNavigationButtons & mask) != 0 ||
                    (runtime.stableNavigationButtons & mask) != 0)
                    continue;
                runtime.navigationActivityPending &= ~mask;
                const auto button = static_cast<GpioNavigationButton>(bit + 1);
                EmitNavigationButton(runtime, button, true, false);
                EmitNavigationButton(runtime, button, false, false);
            }
#else
            (void)runtime;
            (void)now;
            (void)refresh;
#endif
        }

        void ProcessDisplay(Ssd1306Display &display, const GpioDisplaySettings &settings,
                            std::chrono::steady_clock::time_point now, bool force,
                            std::chrono::steady_clock::time_point *nextDraw,
                            uint64_t *seenSequence,
                            std::chrono::steady_clock::time_point *overlayUntil)
        {
#if defined(__linux__)
            GpioDisplayMessage message;
            GpioDisplayDashboard dashboard;
            GpioDisplayMenu menu;
            GpioDisplayMode mode;
            int32_t overlayType;
            uint64_t sequence;
            {
                std::lock_guard lock(displayMutex_);
                message = displayMessage_;
                dashboard = displayDashboard_;
                menu = displayMenu_;
                mode = displayMode_;
                overlayType = displayOverlayType_;
                sequence = displayMessageSequence_;
            }
            if (sequence != *seenSequence)
            {
                *seenSequence = sequence;
                *overlayUntil = now + std::chrono::milliseconds(settings.overlayTimeoutMs_);
                force = true;
            }
            if (!force && now < *nextDraw) return;
            *nextDraw = now + std::chrono::milliseconds(settings.refreshIntervalMs_);
            std::string error;
            if (overlayType != 0 && now < *overlayUntil)
            {
                if (overlayType == 2)
                    display.DrawDashboard(dashboard, &error);
                else if (overlayType == 3)
                    display.DrawMenu(menu, &error);
                else
                    display.DrawMessage(message, &error);
                return;
            }
            if (mode == GpioDisplayMode::Controls)
            {
                display.DrawDashboard(dashboard, &error);
                return;
            }
            if (mode == GpioDisplayMode::Waveform)
            {
                if (settings.waveformEnabled_)
                {
                    WaveformProvider provider;
                    {
                        std::lock_guard lock(callbackMutex_);
                        provider = waveformProvider_;
                    }
                    std::array<float, 128> samples{};
                    if (provider && provider(settings.waveformOutput_, &samples))
                    {
                        display.DrawWaveform(samples, settings.waveformOutput_, &error);
                        return;
                    }
                }
                display.DrawIdle(&error);
                return;
            }
            if (mode == GpioDisplayMode::Tuner)
            {
                TunerSampleProvider provider;
                {
                    std::lock_guard lock(callbackMutex_);
                    provider = tunerSampleProvider_;
                }
                if (tunerResetRequested_.exchange(false))
                {
                    tunerReadIndex_ = std::numeric_limits<uint64_t>::max();
                    tunerSampleRate_ = 0;
                }
                if (provider)
                {
                    std::array<float, 4096> samples{};
                    for (int iteration = 0; iteration < 12; ++iteration)
                    {
                        uint32_t sampleRate = 0;
                        const size_t count = provider(
                            &tunerReadIndex_,
                            samples.data(),
                            samples.size(),
                            &sampleRate);
                        if (count == 0 || sampleRate == 0)
                            break;
                        if (sampleRate != tunerSampleRate_)
                        {
                            tunerAnalyzer_.Initialize(sampleRate);
                            tunerSampleRate_ = sampleRate;
                        }
                        tunerAnalyzer_.Process(samples.data(), count);
                        if (count < samples.size())
                            break;
                    }
                }
                display.DrawTuner(tunerAnalyzer_.Frame(), &error);
                return;
            }
            if (mode == GpioDisplayMode::Blank)
            {
                display.DrawBlank(&error);
                return;
            }
            display.DrawIdle(&error);
#else
            (void)display; (void)settings; (void)now; (void)force; (void)nextDraw; (void)seenSequence; (void)overlayUntil;
#endif
        }

        void RunInputs(std::stop_token stopToken, const GpioSettings &settings)
        {
            std::vector<std::unique_ptr<RuntimeInput>> runtimes;
            for (const auto &input : settings.inputs_)
            {
                if (!input.enabled_)
                {
                    continue;
                }
                auto runtime = std::make_unique<RuntimeInput>(input);
                if (input.inputType() == GpioInputType::Navigation)
                {
#if defined(__linux__)
                    std::string error;
                    if (!runtime->navigation.Open(input, &error))
                    {
                        runtime->available = false;
                        runtime->status.error_ = error;
                        UpdateStatus(*runtime, true);
                    }
#else
                    runtime->available = false;
                    runtime->status.error_ = "I2C navigation is only supported on Linux.";
                    UpdateStatus(*runtime, true);
#endif
                }
                else if (input.inputType() == GpioInputType::Encoder)
                {
#if defined(__linux__)
                    std::string error;
                    if (!runtime->encoder.Open(input, &error))
                    {
                        runtime->available = false;
                        runtime->status.error_ = error;
                        UpdateStatus(*runtime, true);
                    }
#else
                    runtime->available = false;
                    runtime->status.error_ = "I2C encoders are only supported on Linux.";
                    UpdateStatus(*runtime, true);
#endif
                }
                else if (input.inputType() != GpioInputType::Analog)
                {
#if defined(__linux__)
                    std::string error;
                    if (!runtime->lineHandle.Open(input, &error))
                    {
                        runtime->available = false;
                        runtime->status.error_ = error;
                        UpdateStatus(*runtime, true);
                    }
#else
                    runtime->available = false;
                    runtime->status.error_ = "GPIO inputs are only supported on Linux.";
                    UpdateStatus(*runtime, true);
#endif
                }
                runtimes.push_back(std::move(runtime));
            }

            while (!stopToken.stop_requested())
            {
                bool refresh = refreshRequested_.exchange(false);
                if (refresh)
                    redrawRequested_.store(true);
                auto now = std::chrono::steady_clock::now();
                for (auto &runtime : runtimes)
                {
                    if (!runtime->available)
                    {
                        continue;
                    }
                    if (runtime->configuration.inputType() == GpioInputType::Analog)
                    {
                        ProcessAnalog(*runtime, now, refresh);
                    }
                    else if (runtime->configuration.inputType() == GpioInputType::Navigation)
                    {
                        ProcessNavigation(*runtime, now, refresh);
                    }
                    else if (runtime->configuration.inputType() == GpioInputType::Encoder)
                    {
                        ProcessEncoder(*runtime, now, refresh);
                    }
                    else
                    {
                        ProcessDigital(*runtime, now, refresh);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        void RunDisplays(std::stop_token stopToken, const GpioSettings &settings)
        {
#if defined(__linux__)
            Ssd1306Display display;
            bool displayAvailable = false;
            if (settings.display_.enabled_)
            {
                std::string error;
                displayAvailable = display.Open(settings.display_, &error);
                if (!displayAvailable)
                    Lv2Log::warning("Unable to open GPIO OLED display: %s", error.c_str());
            }
            auto nextDisplayDraw = std::chrono::steady_clock::time_point{};
            auto overlayUntil = std::chrono::steady_clock::time_point{};
            uint64_t seenDisplaySequence = 0;

            Ht16k33Matrix ledMatrix;
            bool ledMatrixAvailable = false;
            if (settings.ledMatrix_.enabled_)
            {
                std::string error;
                ledMatrixAvailable = ledMatrix.Open(settings.ledMatrix_, &error);
                if (!ledMatrixAvailable)
                    Lv2Log::warning("Unable to open GPIO LED matrix: %s", error.c_str());
            }
            auto nextLedMatrixDraw = std::chrono::steady_clock::time_point{};

            while (!stopToken.stop_requested())
            {
                const bool redraw = redrawRequested_.exchange(false);
                const auto now = std::chrono::steady_clock::now();
                if (displayAvailable)
                    ProcessDisplay(
                        display, settings.display_, now, redraw,
                        &nextDisplayDraw, &seenDisplaySequence, &overlayUntil);
                if (ledMatrixAvailable && now >= nextLedMatrixDraw)
                {
                    nextLedMatrixDraw = now +
                        std::chrono::milliseconds(settings.ledMatrix_.refreshIntervalMs_);
                    std::array<float, 128> samples{};
                    WaveformProvider provider;
                    {
                        std::lock_guard lock(callbackMutex_);
                        provider = waveformProvider_;
                    }
                    if (settings.ledMatrix_.calibrationMode_ ||
                        (provider && provider(true, &samples)))
                    {
                        std::string error;
                        ledMatrix.Draw(samples, settings.ledMatrix_, &error);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
#else
            (void)stopToken;
            (void)settings;
#endif
        }

        mutable std::mutex statusMutex_;
        std::vector<GpioInputStatus> statuses_;

        std::mutex callbackMutex_;
        EventCallback eventCallback_;
        StatusCallback statusCallback_;
        WaveformProvider waveformProvider_;
        TunerSampleProvider tunerSampleProvider_;

        mutable std::mutex displayMutex_;
        GpioDisplayMessage displayMessage_;
        GpioDisplayDashboard displayDashboard_;
        GpioDisplayMenu displayMenu_;
        GpioDisplayMode displayMode_ = GpioDisplayMode::Controls;
        bool waveformModeEnabled_ = true;
        int32_t displayOverlayType_ = 0;
        uint64_t displayMessageSequence_ = 0;
        GpioTunerAnalyzer tunerAnalyzer_;
        uint64_t tunerReadIndex_ = std::numeric_limits<uint64_t>::max();
        uint32_t tunerSampleRate_ = 0;
        std::atomic<bool> tunerResetRequested_ = true;

        std::unique_ptr<std::jthread> inputThread_;
        std::unique_ptr<std::jthread> displayThread_;
        std::atomic<bool> refreshRequested_ = false;
        std::atomic<bool> redrawRequested_ = false;
    };
}

bool GpioSettings::EnsureStandardEncoderRoles()
{
    constexpr int32_t currentRoleVersion = 4;
    if (encoderRoleVersion_ >= currentRoleVersion)
        return false;

    if (encoderRoleVersion_ < 2)
    {
        std::vector<GpioInputConfiguration *> encoders;
        for (auto &input : inputs_)
        {
            if (input.enabled_ && input.inputType() == GpioInputType::Encoder)
                encoders.push_back(&input);
        }
        if (encoders.size() < 4)
            return false;

        std::stable_sort(
            encoders.begin(), encoders.end(),
            [](const GpioInputConfiguration *left, const GpioInputConfiguration *right)
            {
                if (left->i2cDevice_ != right->i2cDevice_)
                    return left->i2cDevice_ < right->i2cDevice_;
                return left->i2cAddress_ < right->i2cAddress_;
            });
        for (auto *encoder : encoders)
            encoder->encoderRole_ = static_cast<int32_t>(GpioEncoderRole::None);
        encoders[0]->encoderRole_ = static_cast<int32_t>(GpioEncoderRole::Parameter1);
        encoders[1]->encoderRole_ = static_cast<int32_t>(GpioEncoderRole::Parameter2);
        encoders[2]->encoderRole_ = static_cast<int32_t>(GpioEncoderRole::Parameter3);
        encoders[3]->encoderRole_ = static_cast<int32_t>(GpioEncoderRole::Parameter4);
        encoderRolesConfigured_ = true;
    }

    // Version 4 runs the control boards as quickly as the shared worker and
    // documented 400 kHz bus allow, while preserving deliberately assigned
    // left-to-right roles from the version 2 workflow.
    for (auto &input : inputs_)
    {
        if (input.enabled_ &&
            (input.inputType() == GpioInputType::Encoder ||
             input.inputType() == GpioInputType::Navigation))
        {
            input.encoderPollIntervalMs_ = 1;
            input.debounceMs_ = std::min(input.debounceMs_, 10);
        }
    }
    encoderRoleVersion_ = currentRoleVersion;
    return true;
}

void GpioManager::Validate(const GpioSettings &settings)
{
    std::set<std::string> ids;
    std::set<std::pair<std::string, int32_t>> digitalLines;
    std::set<std::pair<std::string, int32_t>> i2cAddresses;
    std::set<int32_t> encoderRoles;
    if (settings.encoderStepsPerRange_ < 4 || settings.encoderStepsPerRange_ > 1000)
    {
        throw std::invalid_argument("Encoder clicks per parameter range must be between 4 and 1000.");
    }
    for (const auto &input : settings.inputs_)
    {
        if (input.id_.empty() || input.id_.size() > 80)
        {
            throw std::invalid_argument("Each GPIO input must have a valid id.");
        }
        if (!ids.insert(input.id_).second)
        {
            throw std::invalid_argument("GPIO input ids must be unique.");
        }
        if (input.inputType_ < static_cast<int32_t>(GpioInputType::Momentary) ||
            input.inputType_ > static_cast<int32_t>(GpioInputType::Navigation) ||
            input.encoderRole_ < static_cast<int32_t>(GpioEncoderRole::None) ||
            input.encoderRole_ > static_cast<int32_t>(GpioEncoderRole::Parameter4) ||
            (input.encoderRole() != GpioEncoderRole::None && input.inputType() != GpioInputType::Encoder))
        {
            throw std::invalid_argument("Invalid GPIO input type.");
        }
        if (!input.enabled_)
        {
            continue;
        }
        if (input.inputType() == GpioInputType::Encoder ||
            input.inputType() == GpioInputType::Navigation)
        {
            if (!IsI2cDevicePath(input.i2cDevice_) || input.i2cAddress_ < 0x08 || input.i2cAddress_ > 0x77 ||
                input.encoderPollIntervalMs_ < 1 || input.encoderPollIntervalMs_ > 1000 ||
                input.debounceMs_ < 0 || input.debounceMs_ > 2000)
            {
                throw std::invalid_argument("Invalid I2C encoder settings.");
            }
            if (!i2cAddresses.insert({input.i2cDevice_, input.i2cAddress_}).second)
                throw std::invalid_argument("Two enabled I2C devices cannot use the same bus address.");
            if (input.inputType() == GpioInputType::Encoder &&
                input.encoderRole() != GpioEncoderRole::None &&
                !encoderRoles.insert(input.encoderRole_).second)
                throw std::invalid_argument("Each standard encoder role can be assigned only once.");
        }
        else if (input.inputType() == GpioInputType::Analog)
        {
            if (!IsIioRawPath(input.analogPath_))
            {
                throw std::invalid_argument("Analog inputs must use a discovered Linux IIO raw channel.");
            }
            // Range comparisons alone would let a NaN through: under -ffast-math
            // every comparison against a NaN is false, so test finiteness first.
            if (!IsFiniteGpioValue(input.analogMin_) || !IsFiniteGpioValue(input.analogMax_) ||
                input.analogMin_ == input.analogMax_)
            {
                throw std::invalid_argument("Analog minimum and maximum must be different finite values.");
            }
            if (!IsFiniteGpioValue(input.smoothing_) || !IsFiniteGpioValue(input.deadband_) ||
                input.smoothing_ < 0.0f || input.smoothing_ >= 1.0f ||
                input.deadband_ < 0.0f || input.deadband_ > 1.0f ||
                input.pollIntervalMs_ < 5 || input.pollIntervalMs_ > 1000)
            {
                throw std::invalid_argument("Invalid analog filtering settings.");
            }
        }
        else
        {
            if (!IsGpioChipPath(input.chip_) || input.line_ < 0 || input.line_ > 1023)
            {
                throw std::invalid_argument("Invalid GPIO chip or line.");
            }
            if (input.pull_ < static_cast<int32_t>(GpioPull::None) ||
                input.pull_ > static_cast<int32_t>(GpioPull::Down) ||
                input.debounceMs_ < 0 || input.debounceMs_ > 2000)
            {
                throw std::invalid_argument("Invalid GPIO pull or debounce setting.");
            }
            if (!digitalLines.insert({input.chip_, input.line_}).second)
            {
                throw std::invalid_argument("A GPIO line cannot be assigned to more than one enabled input.");
            }
        }
    }

    const auto &display = settings.display_;
    if (display.enabled_)
    {
        if (!IsI2cDevicePath(display.i2cDevice_) || display.i2cAddress_ < 0x08 || display.i2cAddress_ > 0x77 ||
            display.overlayTimeoutMs_ < 250 || display.overlayTimeoutMs_ > 60000 ||
            display.refreshIntervalMs_ < 50 || display.refreshIntervalMs_ > 5000 ||
            display.contrast_ < 0 || display.contrast_ > 255 ||
            display.passiveMode_ < static_cast<int32_t>(GpioDisplayMode::Controls) ||
            display.passiveMode_ > static_cast<int32_t>(GpioDisplayMode::Blank))
        {
            throw std::invalid_argument("Invalid SSD1306 display settings.");
        }
        if (!i2cAddresses.insert({display.i2cDevice_, display.i2cAddress_}).second)
            throw std::invalid_argument("Enabled I2C devices cannot use the same bus address.");
    }

    const auto &matrix = settings.ledMatrix_;
    if (matrix.enabled_)
    {
        if (!IsI2cDevicePath(matrix.i2cDevice_) ||
            matrix.i2cAddress_ < 0x08 || matrix.i2cAddress_ > 0x77 ||
            matrix.brightness_ < 0 || matrix.brightness_ > 15 ||
            matrix.refreshIntervalMs_ < 10 || matrix.refreshIntervalMs_ > 1000 ||
            matrix.mode_ < static_cast<int32_t>(GpioLedMatrixMode::Spectrum) ||
            matrix.mode_ > static_cast<int32_t>(GpioLedMatrixMode::Droplets) ||
            !IsFiniteGpioValue(matrix.floorDb_) ||
            matrix.floorDb_ < -96.0f || matrix.floorDb_ > -6.0f ||
            !IsFiniteGpioValue(matrix.decay_) ||
            matrix.decay_ < 0.0f || matrix.decay_ >= 1.0f ||
            matrix.originX_ < 0 || matrix.originX_ > 3 ||
            matrix.originY_ < 0 || matrix.originY_ > 3 ||
            matrix.rotation_ < 0 || matrix.rotation_ > 3)
        {
            throw std::invalid_argument("Invalid HT16K33 LED matrix settings.");
        }
        if (!i2cAddresses.insert({matrix.i2cDevice_, matrix.i2cAddress_}).second)
            throw std::invalid_argument("Enabled I2C devices cannot use the same bus address.");
    }
}

GpioCapabilities GpioManager::Discover()
{
    GpioCapabilities result;
#if !defined(__linux__)
    result.error_ = "GPIO inputs are only available on Linux.";
    return result;
#else
    try
    {
        std::vector<fs::path> chipPaths;
        for (const auto &entry : fs::directory_iterator("/dev"))
        {
            std::string fileName = entry.path().filename().string();
            if (fileName.starts_with("gpiochip") && !entry.is_symlink())
            {
                std::string path = entry.path().string();
                if (IsGpioChipPath(path))
                {
                    chipPaths.push_back(entry.path());
                }
            }
        }
        std::sort(chipPaths.begin(), chipPaths.end());

        for (const auto &entry : fs::directory_iterator("/dev"))
        {
            std::string path = entry.path().string();
            if (IsI2cDevicePath(path)) result.i2cDevices_.push_back(path);
        }
        std::sort(result.i2cDevices_.begin(), result.i2cDevices_.end());

        for (const auto &chipPath : chipPaths)
        {
            int fd = ::open(chipPath.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0)
            {
                continue;
            }
            gpiochip_info chipInfo{};
            if (::ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &chipInfo) == 0)
            {
                GpioChipInfo output;
                output.path_ = chipPath.string();
                output.name_ = chipInfo.name;
                output.label_ = chipInfo.label;
                output.lineCount_ = static_cast<int32_t>(chipInfo.lines);
                output.lines_.reserve(chipInfo.lines);
                for (__u32 line = 0; line < chipInfo.lines; ++line)
                {
                    gpioline_info lineInfo{};
                    lineInfo.line_offset = line;
                    if (::ioctl(fd, GPIO_GET_LINEINFO_IOCTL, &lineInfo) == 0)
                    {
                        GpioLineInfo outputLine;
                        outputLine.offset_ = static_cast<int32_t>(line);
                        outputLine.name_ = lineInfo.name;
                        outputLine.consumer_ = lineInfo.consumer;
                        outputLine.used_ = (lineInfo.flags & GPIOLINE_FLAG_KERNEL) != 0 ||
                                           lineInfo.consumer[0] != '\0';
                        output.lines_.push_back(std::move(outputLine));
                    }
                }
                result.chips_.push_back(std::move(output));
            }
            ::close(fd);
        }

        const fs::path iioRoot("/sys/bus/iio/devices");
        if (fs::exists(iioRoot))
        {
            static const std::regex devicePattern("^iio:device[0-9]+$");
            static const std::regex channelPattern("^in_voltage([0-9]+)_raw$");
            for (const auto &device : fs::directory_iterator(iioRoot))
            {
                if (!device.is_directory() || !std::regex_match(device.path().filename().string(), devicePattern))
                {
                    continue;
                }
                std::string deviceName = ReadFirstLine(device.path() / "name");
                for (const auto &entry : fs::directory_iterator(device.path()))
                {
                    std::smatch match;
                    std::string fileName = entry.path().filename().string();
                    if (std::regex_match(fileName, match, channelPattern))
                    {
                        GpioAnalogChannel channel;
                        channel.path_ = entry.path().string();
                        channel.deviceName_ = deviceName.empty() ? device.path().filename().string() : deviceName;
                        channel.channelName_ = "Channel " + match[1].str();
                        result.analogChannels_.push_back(std::move(channel));
                    }
                }
            }
            std::sort(
                result.analogChannels_.begin(), result.analogChannels_.end(),
                [](const GpioAnalogChannel &left, const GpioAnalogChannel &right)
                {
                    return left.path_ < right.path_;
                });
        }

        result.supported_ = !result.chips_.empty() || !result.i2cDevices_.empty();
        if (!result.supported_)
        {
            result.error_ = "No GPIO or I2C character devices were found, or pipedald cannot open them.";
        }
    }
    catch (const std::exception &e)
    {
        result.error_ = e.what();
    }
    return result;
#endif
}

std::unique_ptr<GpioManager> GpioManager::Create()
{
    return std::make_unique<GpioManagerImpl>();
}
