// Copyright (c) 2026 Robin Davies
// SPDX-License-Identifier: MIT

#include "pch.h"
#include "catch.hpp"
#include "Gpio.hpp"
#include "GpioTuner.hpp"
#include "Pedalboard.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace pipedal;

TEST_CASE("GPIO settings validation", "[gpio]")
{
    GpioSettings settings;
    settings.enabled_ = true;

    GpioInputConfiguration input;
    input.id_ = "footswitch-1";
    input.name_ = "Footswitch 1";
    input.chip_ = "/dev/gpiochip0";
    input.line_ = 17;
    settings.inputs_.push_back(input);

    REQUIRE_NOTHROW(GpioManager::Validate(settings));

    auto duplicate = input;
    duplicate.id_ = "footswitch-2";
    settings.inputs_.push_back(duplicate);
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);

    settings.inputs_[1].enabled_ = false;
    REQUIRE_NOTHROW(GpioManager::Validate(settings));
}

TEST_CASE("GPIO analog channels are constrained to IIO", "[gpio]")
{
    GpioSettings settings;
    settings.enabled_ = true;

    GpioInputConfiguration input;
    input.id_ = "expression";
    input.inputType_ = static_cast<int32_t>(GpioInputType::Analog);
    input.analogPath_ = "/tmp/not-an-adc";
    settings.inputs_.push_back(input);
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);

    settings.inputs_[0].analogPath_ = "/sys/bus/iio/devices/iio:device0/in_voltage0_raw";
    REQUIRE_NOTHROW(GpioManager::Validate(settings));

    settings.inputs_[0].analogMax_ = settings.inputs_[0].analogMin_;
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);
}

TEST_CASE("I2C encoder and display addresses are validated", "[gpio]")
{
    GpioSettings settings;
    settings.enabled_ = true;
    GpioInputConfiguration encoder;
    encoder.id_ = "encoder-1";
    encoder.inputType_ = static_cast<int32_t>(GpioInputType::Encoder);
    encoder.i2cDevice_ = "/dev/i2c-1";
    encoder.i2cAddress_ = 0x36;
    settings.inputs_.push_back(encoder);
    settings.display_.enabled_ = true;
    settings.display_.i2cDevice_ = "/dev/i2c-1";
    settings.display_.i2cAddress_ = 0x3C;
    REQUIRE_NOTHROW(GpioManager::Validate(settings));

    settings.display_.i2cAddress_ = 0x36;
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);
    settings.display_.i2cAddress_ = 0x3C;
    settings.inputs_[0].i2cDevice_ = "/tmp/not-i2c";
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);
}

TEST_CASE("ANO navigation and HT16K33 matrix settings are validated", "[gpio]")
{
    GpioSettings settings;
    settings.enabled_ = true;

    GpioInputConfiguration navigation;
    navigation.id_ = "navigation";
    navigation.inputType_ = static_cast<int32_t>(GpioInputType::Navigation);
    navigation.i2cAddress_ = 0x49;
    settings.inputs_.push_back(navigation);
    settings.ledMatrix_.enabled_ = true;
    settings.ledMatrix_.i2cAddress_ = 0x70;
    settings.display_.passiveMode_ = static_cast<int32_t>(GpioDisplayMode::Blank);
    REQUIRE_NOTHROW(GpioManager::Validate(settings));

    settings.ledMatrix_.refreshIntervalMs_ = 10;
    REQUIRE_NOTHROW(GpioManager::Validate(settings));
    settings.ledMatrix_.refreshIntervalMs_ = 9;
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);
    settings.ledMatrix_.refreshIntervalMs_ = 16;

    settings.ledMatrix_.i2cAddress_ = 0x49;
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);
    settings.ledMatrix_.i2cAddress_ = 0x70;
    settings.ledMatrix_.originX_ = 4;
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);
    settings.ledMatrix_.originX_ = 1;
    settings.ledMatrix_.floorDb_ = -3.0f;
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);
    settings.ledMatrix_.floorDb_ = -48.0f;
    settings.ledMatrix_.mode_ = 2;
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);
}

TEST_CASE("Standard encoder roles migrate once and remain unique", "[gpio]")
{
    GpioSettings settings;
    settings.enabled_ = true;
    for (int32_t index = 0; index < 4; ++index)
    {
        GpioInputConfiguration encoder;
        encoder.id_ = "encoder-" + std::to_string(index + 1);
        encoder.inputType_ = static_cast<int32_t>(GpioInputType::Encoder);
        encoder.i2cAddress_ = 0x39 - index; // migration sorts by address.
        settings.inputs_.push_back(encoder);
    }

    REQUIRE(settings.EnsureStandardEncoderRoles());
    REQUIRE(settings.encoderRolesConfigured_);
    REQUIRE(settings.encoderRoleVersion_ == 4);
    REQUIRE(settings.inputs_[3].encoderRole() == GpioEncoderRole::Parameter1);
    REQUIRE(settings.inputs_[2].encoderRole() == GpioEncoderRole::Parameter2);
    REQUIRE(settings.inputs_[1].encoderRole() == GpioEncoderRole::Parameter3);
    REQUIRE(settings.inputs_[0].encoderRole() == GpioEncoderRole::Parameter4);
    REQUIRE_FALSE(settings.EnsureStandardEncoderRoles());
    REQUIRE_NOTHROW(GpioManager::Validate(settings));

    settings.inputs_[0].encoderRole_ = settings.inputs_[1].encoderRole_;
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);
}

TEST_CASE("Encoder reliability migration preserves assigned roles", "[gpio]")
{
    GpioSettings settings;
    settings.enabled_ = true;
    settings.encoderRolesConfigured_ = true;
    settings.encoderRoleVersion_ = 3;
    for (int32_t index = 0; index < 4; ++index)
    {
        GpioInputConfiguration encoder;
        encoder.id_ = "encoder-" + std::to_string(index + 1);
        encoder.inputType_ = static_cast<int32_t>(GpioInputType::Encoder);
        encoder.i2cAddress_ = 0x3D - index;
        encoder.encoderRole_ = static_cast<int32_t>(GpioEncoderRole::Parameter1) + index;
        encoder.encoderPollIntervalMs_ = 5;
        encoder.debounceMs_ = 30;
        settings.inputs_.push_back(encoder);
    }
    GpioInputConfiguration navigation;
    navigation.id_ = "navigation";
    navigation.inputType_ = static_cast<int32_t>(GpioInputType::Navigation);
    navigation.i2cAddress_ = 0x49;
    navigation.encoderPollIntervalMs_ = 5;
    navigation.debounceMs_ = 30;
    settings.inputs_.push_back(navigation);

    REQUIRE(settings.EnsureStandardEncoderRoles());
    REQUIRE(settings.encoderRoleVersion_ == 4);
    for (int32_t index = 0; index < 4; ++index)
    {
        REQUIRE(settings.inputs_[static_cast<size_t>(index)].encoderRole_ ==
                static_cast<int32_t>(GpioEncoderRole::Parameter1) + index);
        REQUIRE(settings.inputs_[static_cast<size_t>(index)].encoderPollIntervalMs_ == 1);
        REQUIRE(settings.inputs_[static_cast<size_t>(index)].debounceMs_ == 10);
    }
    REQUIRE(settings.inputs_.back().encoderPollIntervalMs_ == 1);
    REQUIRE(settings.inputs_.back().debounceMs_ == 10);
    REQUIRE_FALSE(settings.EnsureStandardEncoderRoles());
}

TEST_CASE("GPIO configuration and mappings round-trip through JSON", "[gpio]")
{
    GpioSettings source;
    source.enabled_ = true;
    GpioInputConfiguration input;
    input.id_ = "switch-a";
    input.name_ = "Switch A";
    input.inputType_ = static_cast<int32_t>(GpioInputType::Latching);
    input.line_ = 27;
    input.activeLow_ = false;
    source.inputs_.push_back(input);
    source.display_.enabled_ = true;
    source.display_.overlayTimeoutMs_ = 4250;
    source.encoderRolesConfigured_ = true;
    source.encoderRoleVersion_ = 2;
    source.encoderStepsPerRange_ = 64;
    source.ledMatrix_.enabled_ = true;
    source.ledMatrix_.brightness_ = 9;
    source.ledMatrix_.mode_ = static_cast<int32_t>(GpioLedMatrixMode::Droplets);
    source.ledMatrix_.rotation_ = 3;
    source.ledMatrix_.calibrationMode_ = true;

    std::stringstream json;
    json_writer writer(json, true);
    writer.write(source);

    GpioSettings restored;
    json_reader reader(json);
    reader.read(&restored);

    REQUIRE(restored.enabled_);
    REQUIRE(restored.encoderRolesConfigured_);
    REQUIRE(restored.encoderRoleVersion_ == 2);
    REQUIRE(restored.encoderStepsPerRange_ == 64);
    REQUIRE(restored.inputs_.size() == 1);
    REQUIRE(restored.inputs_[0].id_ == "switch-a");
    REQUIRE(restored.inputs_[0].inputType() == GpioInputType::Latching);
    REQUIRE(restored.inputs_[0].line_ == 27);
    REQUIRE_FALSE(restored.inputs_[0].activeLow_);
    REQUIRE(restored.display_.enabled_);
    REQUIRE(restored.display_.overlayTimeoutMs_ == 4250);
    REQUIRE(restored.ledMatrix_.enabled_);
    REQUIRE(restored.ledMatrix_.brightness_ == 9);
    REQUIRE(restored.ledMatrix_.mode_ == static_cast<int32_t>(GpioLedMatrixMode::Droplets));
    REQUIRE(restored.ledMatrix_.rotation_ == 3);
    REQUIRE(restored.ledMatrix_.calibrationMode_);

    GpioBinding binding;
    binding.inputId_ = "switch-a";
    binding.actionType_ = static_cast<int32_t>(GpioActionType::Control);
    binding.instanceId_ = 42;
    binding.symbol_ = "gain";
    binding.minValue_ = 10.0f;
    binding.maxValue_ = -10.0f; // reversed ranges are intentionally supported.
    binding.curve_ = 2.0f;
    binding.mode_ = static_cast<int32_t>(GpioBindingMode::Relative);
    binding.eventType_ = static_cast<int32_t>(GpioBindingEventType::EncoderTurn);
    binding.stepValue_ = 0.25f;

    std::stringstream bindingJson;
    json_writer bindingWriter(bindingJson, true);
    bindingWriter.write(binding);
    GpioBinding restoredBinding;
    json_reader bindingReader(bindingJson);
    bindingReader.read(&restoredBinding);
    REQUIRE(restoredBinding.inputId_ == binding.inputId_);
    REQUIRE(restoredBinding.symbol_ == binding.symbol_);
    REQUIRE(restoredBinding.minValue_ == 10.0f);
    REQUIRE(restoredBinding.maxValue_ == -10.0f);
    REQUIRE(restoredBinding.curve_ == 2.0f);
    REQUIRE(restoredBinding.mode() == GpioBindingMode::Relative);
    REQUIRE(restoredBinding.eventType() == GpioBindingEventType::EncoderTurn);
    REQUIRE(restoredBinding.stepValue_ == 0.25f);
}

TEST_CASE("A preset carries the parameter scroll position", "[gpio]")
{
    // The position is stored as the first shown parameter, not an index, so
    // that it survives editing the effect chain. It is preset data: the same
    // physical encoders land somewhere different in every rig.
    Pedalboard source;
    source.gpioScrollInstanceId(174);
    source.gpioScrollSymbol("treble");

    std::stringstream json;
    json_writer writer(json, true);
    writer.write(source);

    Pedalboard restored;
    json_reader reader(json);
    reader.read(&restored);

    REQUIRE(restored.gpioScrollInstanceId() == 174);
    REQUIRE(restored.gpioScrollSymbol() == "treble");

    // A preset written before this feature simply starts at the top.
    Pedalboard legacy;
    std::stringstream legacyJson{"{\"name\": \"Legacy\"}"};
    json_reader legacyReader(legacyJson);
    legacyReader.read(&legacy);
    REQUIRE(legacy.gpioScrollInstanceId() == -1);
    REQUIRE(legacy.gpioScrollSymbol().empty());
}

TEST_CASE("Encoder resolution is validated", "[gpio]")
{
    GpioSettings settings;
    settings.enabled_ = true;
    REQUIRE_NOTHROW(GpioManager::Validate(settings));

    settings.encoderStepsPerRange_ = 3;
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);
    settings.encoderStepsPerRange_ = 1001;
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);
    settings.encoderStepsPerRange_ = 250;
    REQUIRE_NOTHROW(GpioManager::Validate(settings));
}

TEST_CASE("Built-in GPIO tuner locks accurately on bass notes", "[gpio]")
{
    constexpr uint32_t sampleRate = 44100;
    constexpr float frequency = 55.0f; // A1 / MIDI 33.
    constexpr float pi = 3.14159265358979323846f;
    std::vector<float> samples(sampleRate * 3 / 4);
    for (size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] =
            0.25f * std::sin(
                        2.0f * pi * frequency *
                        static_cast<float>(i) /
                        static_cast<float>(sampleRate));
    }

    GpioTunerAnalyzer analyzer;
    analyzer.Initialize(sampleRate);
    for (size_t position = 0; position < samples.size(); position += 257)
    {
        size_t count = std::min<size_t>(257, samples.size() - position);
        analyzer.Process(samples.data() + position, count);
    }

    const auto &frame = analyzer.Frame();
    REQUIRE(frame.Locked());
    REQUIRE(frame.note == 33);
    REQUIRE(frame.NoteName() == "A");
    REQUIRE(std::abs(frame.frequency - frequency) < 0.03f);
    REQUIRE(std::abs(frame.cents) < 0.5f);

    GpioTunerFrame sharpFrame;
    sharpFrame.note = 42;
    sharpFrame.confidence = 1.0f;
    REQUIRE(sharpFrame.NoteName() == "F#");
}

TEST_CASE("GPIO display values use explicit missing state under fast math", "[gpio]")
{
    constexpr float currentValue = 5.7f;
    constexpr float suppliedValue = 0.9f;
    const float notFinite = std::numeric_limits<float>::quiet_NaN();

    REQUIRE(ResolveGpioDisplayValue(
                std::nullopt, currentValue, 0.0f) == Approx(currentValue));
    REQUIRE(ResolveGpioDisplayValue(
                suppliedValue, currentValue, 0.0f) == Approx(suppliedValue));
    REQUIRE(ResolveGpioDisplayValue(
                notFinite, currentValue, 1.0f) == Approx(1.0f));
    REQUIRE(ResolveGpioDisplayValue(
                std::nullopt, notFinite, 1.0f) == Approx(1.0f));
}

TEST_CASE("Non-finite values are rejected without std::isfinite", "[gpio]")
{
    // Release builds are compiled -ffast-math. std::isfinite may be folded to
    // true there, so these checks must not depend on it.
    const float quietNan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    REQUIRE(IsFiniteGpioValue(0.0f));
    REQUIRE(IsFiniteGpioValue(-1234.5f));
    REQUIRE(IsFiniteGpioValue(std::numeric_limits<float>::denorm_min()));
    REQUIRE(IsFiniteGpioValue(std::numeric_limits<float>::max()));
    REQUIRE_FALSE(IsFiniteGpioValue(quietNan));
    REQUIRE_FALSE(IsFiniteGpioValue(infinity));
    REQUIRE_FALSE(IsFiniteGpioValue(-infinity));

    // The same guard keeps a non-finite analog calibration out of the settings
    // file, where every later range comparison would silently succeed.
    GpioSettings settings;
    settings.enabled_ = true;
    GpioInputConfiguration input;
    input.id_ = "expression";
    input.inputType_ = static_cast<int32_t>(GpioInputType::Analog);
    input.analogPath_ = "/sys/bus/iio/devices/iio:device0/in_voltage0_raw";
    settings.inputs_.push_back(input);
    REQUIRE_NOTHROW(GpioManager::Validate(settings));

    settings.inputs_[0].analogMax_ = quietNan;
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);
    settings.inputs_[0].analogMax_ = 4095.0f;
    settings.inputs_[0].smoothing_ = quietNan;
    REQUIRE_THROWS_AS(GpioManager::Validate(settings), std::invalid_argument);
}

TEST_CASE("A persistent dashboard does not restart the overlay timeout", "[gpio]")
{
    // The temporary dashboard is what an encoder turn shows over Waveform or
    // Tuner; refreshing the persistent one must not extend or cancel it, and
    // neither may change the mode that resumes afterwards.
    auto manager = GpioManager::Create();
    REQUIRE(manager->CycleDisplayMode() == GpioDisplayMode::Waveform);

    GpioDisplayDashboard dashboard;
    dashboard.controls[0].assigned = true;
    dashboard.controls[0].effectName = "TEST EFFECT";
    manager->ShowTemporaryControlDashboard(dashboard);
    REQUIRE(manager->GetDisplayMode() == GpioDisplayMode::Waveform);

    dashboard.controls[0].effectName = "UPDATED";
    manager->ShowControlDashboard(dashboard);
    REQUIRE(manager->GetDisplayMode() == GpioDisplayMode::Waveform);

    manager->ClearDisplayMessage();
    REQUIRE(manager->GetDisplayMode() == GpioDisplayMode::Waveform);
}

TEST_CASE("OLED mode button cycles controls waveform and tuner", "[gpio]")
{
    auto manager = GpioManager::Create();
    REQUIRE(manager->GetDisplayMode() == GpioDisplayMode::Controls);
    REQUIRE(manager->CycleDisplayMode() == GpioDisplayMode::Waveform);
    GpioDisplayDashboard dashboard;
    dashboard.controls[0].assigned = true;
    dashboard.controls[0].effectName = "TEST EFFECT";
    dashboard.controls[0].label = "GAIN";
    dashboard.controls[0].value = "0.5";
    dashboard.controls[0].normalizedValue = 0.5f;
    manager->ShowTemporaryControlDashboard(dashboard);
    // A temporary parameter view must not change the mode that resumes after
    // the configured overlay timeout.
    REQUIRE(manager->GetDisplayMode() == GpioDisplayMode::Waveform);
    REQUIRE(manager->CycleDisplayMode() == GpioDisplayMode::Tuner);
    REQUIRE(manager->CycleDisplayMode() == GpioDisplayMode::Controls);

    GpioSettings settings;
    settings.display_.waveformEnabled_ = false;
    manager->Configure(settings);
    REQUIRE(manager->CycleDisplayMode() == GpioDisplayMode::Tuner);
    REQUIRE(manager->CycleDisplayMode() == GpioDisplayMode::Controls);
}
