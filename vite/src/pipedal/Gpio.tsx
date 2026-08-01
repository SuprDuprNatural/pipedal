// Copyright (c) 2026 Robin Davies
// SPDX-License-Identifier: MIT

export enum GpioInputType {
    Momentary = 0,
    Latching = 1,
    Analog = 2,
    Encoder = 3,
    Navigation = 4
}

export enum GpioPull {
    None = 0,
    Up = 1,
    Down = 2
}

export enum GpioEncoderRole {
    None = 0,
    PresetBrowser = 1,
    ParameterScroll = 2,
    Parameter1 = 3,
    Parameter2 = 4,
    Parameter3 = 5,
    Parameter4 = 6
}

export class GpioInputConfiguration {
    deserialize(input: any): GpioInputConfiguration {
        this.id = input.id ?? "";
        this.name = input.name ?? "";
        this.enabled = input.enabled ?? true;
        this.inputType = input.inputType ?? GpioInputType.Momentary;
        this.chip = input.chip ?? "/dev/gpiochip0";
        this.line = input.line ?? 17;
        this.pull = input.pull ?? GpioPull.Up;
        this.activeLow = input.activeLow ?? true;
        this.debounceMs = input.debounceMs ?? 30;
        this.analogPath = input.analogPath ?? "";
        this.analogMin = input.analogMin ?? 0;
        this.analogMax = input.analogMax ?? 4095;
        this.smoothing = input.smoothing ?? 0.15;
        this.deadband = input.deadband ?? 0.005;
        this.pollIntervalMs = input.pollIntervalMs ?? 20;
        this.i2cDevice = input.i2cDevice ?? "/dev/i2c-1";
        this.i2cAddress = input.i2cAddress ?? 0x36;
        this.encoderReversed = input.encoderReversed ?? true;
        this.encoderPollIntervalMs = input.encoderPollIntervalMs ?? 1;
        this.encoderRole = input.encoderRole ?? GpioEncoderRole.None;
        return this;
    }

    clone(): GpioInputConfiguration { return new GpioInputConfiguration().deserialize(this); }

    id: string = "";
    name: string = "";
    enabled: boolean = true;
    inputType: GpioInputType = GpioInputType.Momentary;
    chip: string = "/dev/gpiochip0";
    line: number = 17;
    pull: GpioPull = GpioPull.Up;
    activeLow: boolean = true;
    debounceMs: number = 30;
    analogPath: string = "";
    analogMin: number = 0;
    analogMax: number = 4095;
    smoothing: number = 0.15;
    deadband: number = 0.005;
    pollIntervalMs: number = 20;
    i2cDevice: string = "/dev/i2c-1";
    i2cAddress: number = 0x36;
    encoderReversed: boolean = true;
    encoderPollIntervalMs: number = 1;
    encoderRole: GpioEncoderRole = GpioEncoderRole.None;
}

export class GpioDisplaySettings {
    deserialize(input: any): GpioDisplaySettings {
        this.enabled = input?.enabled ?? false;
        this.i2cDevice = input?.i2cDevice ?? "/dev/i2c-1";
        this.i2cAddress = input?.i2cAddress ?? 0x3C;
        this.overlayTimeoutMs = input?.overlayTimeoutMs ?? 3000;
        this.waveformEnabled = input?.waveformEnabled ?? true;
        this.waveformOutput = input?.waveformOutput ?? true;
        this.refreshIntervalMs = input?.refreshIntervalMs ?? 200;
        this.rotate180 = input?.rotate180 ?? false;
        this.contrast = input?.contrast ?? 160;
        this.passiveMode = input?.passiveMode ?? GpioDisplayMode.Controls;
        return this;
    }
    clone(): GpioDisplaySettings { return new GpioDisplaySettings().deserialize(this); }
    enabled: boolean = false;
    i2cDevice: string = "/dev/i2c-1";
    i2cAddress: number = 0x3C;
    overlayTimeoutMs: number = 3000;
    waveformEnabled: boolean = true;
    waveformOutput: boolean = true;
    refreshIntervalMs: number = 200;
    rotate180: boolean = false;
    contrast: number = 160;
    passiveMode: GpioDisplayMode = GpioDisplayMode.Controls;
}

export enum GpioDisplayMode {
    Controls = 0,
    Waveform = 1,
    Tuner = 2,
    Blank = 3
}

export class GpioLedMatrixSettings {
    deserialize(input: any): GpioLedMatrixSettings {
        this.enabled = input?.enabled ?? false;
        this.i2cDevice = input?.i2cDevice ?? "/dev/i2c-1";
        this.i2cAddress = input?.i2cAddress ?? 0x70;
        this.brightness = input?.brightness ?? 6;
        this.refreshIntervalMs = input?.refreshIntervalMs ?? 16;
        this.mode = input?.mode ?? GpioLedMatrixMode.Spectrum;
        this.floorDb = input?.floorDb ?? -48;
        this.decay = input?.decay ?? 0.65;
        this.originX = input?.originX ?? 1;
        this.originY = input?.originY ?? 1;
        this.rotation = input?.rotation ?? 0;
        this.mirror = input?.mirror ?? false;
        this.calibrationMode = input?.calibrationMode ?? false;
        return this;
    }
    clone(): GpioLedMatrixSettings { return new GpioLedMatrixSettings().deserialize(this); }
    enabled: boolean = false;
    i2cDevice: string = "/dev/i2c-1";
    i2cAddress: number = 0x70;
    brightness: number = 6;
    refreshIntervalMs: number = 16;
    mode: GpioLedMatrixMode = GpioLedMatrixMode.Spectrum;
    floorDb: number = -48;
    decay: number = 0.65;
    originX: number = 1;
    originY: number = 1;
    rotation: number = 0;
    mirror: boolean = false;
    calibrationMode: boolean = false;
}

export enum GpioLedMatrixMode {
    Spectrum = 0,
    Droplets = 1
}

export class GpioSettings {
    deserialize(input: any): GpioSettings {
        this.enabled = input?.enabled ?? false;
        this.encoderRolesConfigured = input?.encoderRolesConfigured ?? false;
        this.encoderRoleVersion = input?.encoderRoleVersion ?? 0;
        this.encoderStepsPerRange = input?.encoderStepsPerRange ?? 100;
        this.inputs = (input?.inputs ?? []).map((item: any) => new GpioInputConfiguration().deserialize(item));
        this.display = new GpioDisplaySettings().deserialize(input?.display);
        this.ledMatrix = new GpioLedMatrixSettings().deserialize(input?.ledMatrix);
        return this;
    }
    clone(): GpioSettings { return new GpioSettings().deserialize(this); }

    enabled: boolean = false;
    encoderRolesConfigured: boolean = false;
    encoderRoleVersion: number = 0;
    encoderStepsPerRange: number = 100;
    inputs: GpioInputConfiguration[] = [];
    display: GpioDisplaySettings = new GpioDisplaySettings();
    ledMatrix: GpioLedMatrixSettings = new GpioLedMatrixSettings();
}

export enum GpioBindingMode {
    Direct = 0,
    Toggle = 1,
    Trigger = 2,
    Relative = 3
}

export enum GpioBindingEventType {
    Value = 0,
    EncoderTurn = 1,
    EncoderButton = 2
}

export enum GpioActionType {
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
}

export class GpioBinding {
    deserialize(input: any): GpioBinding {
        this.enabled = input.enabled ?? true;
        this.inputId = input.inputId ?? "";
        this.actionType = input.actionType ?? GpioActionType.Control;
        this.mode = input.mode ?? GpioBindingMode.Direct;
        this.eventType = input.eventType ?? GpioBindingEventType.Value;
        this.instanceId = input.instanceId ?? -1;
        this.symbol = input.symbol ?? "";
        this.targetId = input.targetId ?? -1;
        this.minValue = input.minValue ?? 0;
        this.maxValue = input.maxValue ?? 1;
        this.curve = input.curve ?? 1;
        this.stepValue = input.stepValue ?? 0.01;
        return this;
    }
    clone(): GpioBinding { return new GpioBinding().deserialize(this); }
    static deserializeArray(input: any): GpioBinding[] {
        return (input ?? []).map((item: any) => new GpioBinding().deserialize(item));
    }

    enabled: boolean = true;
    inputId: string = "";
    actionType: GpioActionType = GpioActionType.Control;
    mode: GpioBindingMode = GpioBindingMode.Direct;
    eventType: GpioBindingEventType = GpioBindingEventType.Value;
    instanceId: number = -1;
    symbol: string = "";
    targetId: number = -1;
    minValue: number = 0;
    maxValue: number = 1;
    curve: number = 1;
    stepValue: number = 0.01;
}

export class GpioLineInfo {
    deserialize(input: any): GpioLineInfo {
        this.offset = input.offset;
        this.name = input.name ?? "";
        this.consumer = input.consumer ?? "";
        this.used = input.used ?? false;
        return this;
    }
    offset: number = -1;
    name: string = "";
    consumer: string = "";
    used: boolean = false;
}

export class GpioChipInfo {
    deserialize(input: any): GpioChipInfo {
        this.path = input.path;
        this.name = input.name ?? "";
        this.label = input.label ?? "";
        this.lineCount = input.lineCount ?? 0;
        this.lines = (input.lines ?? []).map((line: any) => new GpioLineInfo().deserialize(line));
        return this;
    }
    path: string = "";
    name: string = "";
    label: string = "";
    lineCount: number = 0;
    lines: GpioLineInfo[] = [];
}

export class GpioAnalogChannel {
    deserialize(input: any): GpioAnalogChannel {
        this.path = input.path;
        this.deviceName = input.deviceName ?? "";
        this.channelName = input.channelName ?? "";
        return this;
    }
    path: string = "";
    deviceName: string = "";
    channelName: string = "";
}

export class GpioCapabilities {
    deserialize(input: any): GpioCapabilities {
        this.supported = input?.supported ?? false;
        this.error = input?.error ?? "";
        this.chips = (input?.chips ?? []).map((chip: any) => new GpioChipInfo().deserialize(chip));
        this.analogChannels = (input?.analogChannels ?? []).map((channel: any) => new GpioAnalogChannel().deserialize(channel));
        this.i2cDevices = input?.i2cDevices ?? [];
        return this;
    }
    supported: boolean = false;
    error: string = "";
    chips: GpioChipInfo[] = [];
    analogChannels: GpioAnalogChannel[] = [];
    i2cDevices: string[] = [];
}

export class GpioInputStatus {
    deserialize(input: any): GpioInputStatus {
        this.inputId = input.inputId;
        this.connected = input.connected ?? false;
        this.value = input.value ?? 0;
        this.encoderPosition = input.encoderPosition ?? 0;
        this.buttonPressed = input.buttonPressed ?? false;
        this.navigationButtons = input.navigationButtons ?? 0;
        this.error = input.error ?? "";
        return this;
    }
    static deserializeArray(input: any): GpioInputStatus[] {
        return (input ?? []).map((status: any) => new GpioInputStatus().deserialize(status));
    }
    inputId: string = "";
    connected: boolean = false;
    value: number = 0;
    encoderPosition: number = 0;
    buttonPressed: boolean = false;
    navigationButtons: number = 0;
    error: string = "";
}
