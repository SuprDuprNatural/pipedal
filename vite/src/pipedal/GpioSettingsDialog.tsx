// Copyright (c) 2026 Robin Davies
// SPDX-License-Identifier: MIT

import { useEffect, useState } from 'react';
import AppBar from '@mui/material/AppBar';
import Toolbar from '@mui/material/Toolbar';
import Typography from '@mui/material/Typography';
import Button from '@mui/material/Button';
import IconButton from '@mui/material/IconButton';
import ArrowBackIcon from '@mui/icons-material/ArrowBack';
import DeleteOutlineIcon from '@mui/icons-material/DeleteOutline';
import AddIcon from '@mui/icons-material/Add';
import Alert from '@mui/material/Alert';
import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Divider from '@mui/material/Divider';
import FormControlLabel from '@mui/material/FormControlLabel';
import MenuItem from '@mui/material/MenuItem';
import Stack from '@mui/material/Stack';
import Switch from '@mui/material/Switch';
import TextField from '@mui/material/TextField';

import DialogEx from './DialogEx';
import { PiPedalModelFactory } from './PiPedalModel';
import {
    GpioCapabilities,
    GpioEncoderRole,
    GpioInputConfiguration,
    GpioInputStatus,
    GpioInputType,
    GpioPull,
    GpioSettings
} from './Gpio';

interface GpioSettingsDialogProps {
    open: boolean;
    onClose: () => void;
}

const headerPins: Record<number, number> = {
    2: 3, 3: 5, 4: 7, 14: 8, 15: 10, 17: 11, 18: 12, 27: 13,
    22: 15, 23: 16, 24: 18, 10: 19, 9: 21, 25: 22, 11: 23, 8: 24,
    7: 26, 0: 27, 1: 28, 5: 29, 6: 31, 12: 32, 13: 33, 19: 35,
    16: 36, 26: 37, 20: 38, 21: 40
};

let nextInputId = 1;
function makeInputId(settings: GpioSettings): string {
    while (true) {
        const id = `gpio-input-${Date.now().toString(36)}-${nextInputId++}`;
        if (!settings.inputs.some(input => input.id === id)) return id;
    }
}

function inputTypeName(inputType: GpioInputType): string {
    switch (inputType) {
        case GpioInputType.Momentary: return "Momentary button / footswitch";
        case GpioInputType.Latching: return "Latching / maintained switch";
        case GpioInputType.Analog: return "Potentiometer via external ADC";
        case GpioInputType.Encoder: return "Adafruit I2C rotary encoder + button";
    }
}

function encoderRoleName(role: GpioEncoderRole): string {
    switch (role) {
        case GpioEncoderRole.None: return "Unassigned — free for mappings";
        case GpioEncoderRole.PresetBrowser: return "Browse presets; press to load";
        case GpioEncoderRole.ParameterScroll: return "Scroll parameters; press to change OLED view";
        case GpioEncoderRole.Parameter1: return "Change the left shown parameter";
        case GpioEncoderRole.Parameter2: return "Change the right shown parameter";
    }
}

export default function GpioSettingsDialog(props: GpioSettingsDialogProps) {
    const model = PiPedalModelFactory.getInstance();
    const [settings, setSettings] = useState<GpioSettings>(model.gpioSettings.get().clone());
    const [capabilities, setCapabilities] = useState<GpioCapabilities | null>(null);
    const [statuses, setStatuses] = useState<GpioInputStatus[]>(model.gpioInputStatuses.get());
    const [saving, setSaving] = useState(false);

    useEffect(() => {
        if (!props.open) return;
        setSettings(model.gpioSettings.get().clone());
        setStatuses(model.gpioInputStatuses.get());
        model.getGpioCapabilities()
            .then(setCapabilities)
            .catch(error => model.showAlert(error));

        const statusChanged = (value: GpioInputStatus[]) => setStatuses(value);
        model.gpioInputStatuses.addOnChangedHandler(statusChanged);
        return () => model.gpioInputStatuses.removeOnChangedHandler(statusChanged);
    }, [props.open]); // eslint-disable-line react-hooks/exhaustive-deps

    const updateInput = (index: number, update: (input: GpioInputConfiguration) => void) => {
        const copy = settings.clone();
        update(copy.inputs[index]);
        setSettings(copy);
    };

    const addInput = () => {
        const copy = settings.clone();
        const input = new GpioInputConfiguration();
        input.id = makeInputId(copy);
        input.name = `Control ${copy.inputs.length + 1}`;
        input.line = [17, 27, 22, 23, 24, 25, 5, 6, 12, 13, 16, 19, 20, 21]
            .find(line => !copy.inputs.some(existing => existing.inputType !== GpioInputType.Analog &&
                existing.inputType !== GpioInputType.Encoder && existing.line === line)) ?? 17;
        if (capabilities?.chips.length) input.chip = capabilities.chips[0].path;
        copy.inputs.push(input);
        setSettings(copy);
    };

    const addEncoderRig = () => {
        const copy = settings.clone();
        const bus = capabilities?.i2cDevices.find(device => device === "/dev/i2c-1") ?? "/dev/i2c-1";
        copy.enabled = true;
        // A role may only be claimed once, so release any encoder outside this
        // rig that already holds one before handing the four roles out.
        for (const existing of copy.inputs) {
            existing.encoderRole = GpioEncoderRole.None;
        }
        for (let index = 0; index < 4; ++index) {
            const address = 0x36 + index;
            let input = copy.inputs.find(value => value.inputType === GpioInputType.Encoder &&
                value.i2cDevice === bus && value.i2cAddress === address);
            if (!input) {
                input = new GpioInputConfiguration();
                input.id = makeInputId(copy);
                input.name = `Encoder ${index + 1}`;
                input.inputType = GpioInputType.Encoder;
                input.i2cDevice = bus;
                input.i2cAddress = address;
                copy.inputs.push(input);
            }
            input.encoderRole = (GpioEncoderRole.PresetBrowser + index) as GpioEncoderRole;
        }
        copy.encoderRolesConfigured = true;
        copy.display.enabled = true;
        copy.display.i2cDevice = bus;
        copy.display.i2cAddress = 0x3C;
        setSettings(copy);
    };

    const save = () => {
        setSaving(true);
        model.setGpioSettings(settings)
            .then(() => props.onClose())
            .catch(error => model.showAlert(error))
            .finally(() => setSaving(false));
    };

    const statusFor = (id: string) => statuses.find(status => status.inputId === id);

    return (
        <DialogEx tag="gpioSettings" open={props.open} fullScreen onClose={props.onClose} onEnterKey={() => { }}>
            <Box sx={{ display: "flex", flexDirection: "column", width: "100%", height: "100%", overflow: "hidden" }}>
                <AppBar position="relative">
                    <Toolbar>
                        <IconButton edge="start" color="inherit" onClick={props.onClose} aria-label="back">
                            <ArrowBackIcon />
                        </IconButton>
                        <Typography variant="h6" sx={{ ml: 2, flex: 1 }}>GPIO / I2C hardware controls</Typography>
                        <Button color="inherit" disabled={saving} onClick={save}>Save</Button>
                    </Toolbar>
                </AppBar>

                <Box sx={{ overflow: "auto", flex: 1, p: { xs: 2, sm: 3 } }}>
                    <Stack spacing={2} sx={{ maxWidth: 920, mx: "auto" }}>
                        <Alert severity="warning">
                            Raspberry Pi GPIO is 3.3 V only and is not 5 V tolerant. Never connect a potentiometer directly:
                            the Pi has no analog input. Use a supported external ADC and enable its Linux IIO driver.
                        </Alert>

                        {capabilities && !capabilities.supported && (
                            <Alert severity="error">{capabilities.error || "No accessible GPIO controller was found."}</Alert>
                        )}
                        {capabilities && !capabilities.i2cDevices.includes("/dev/i2c-1") && (
                            <Alert severity="warning">
                                The Raspberry Pi header bus /dev/i2c-1 is not available. Enable I2C in raspi-config and reboot before connecting the encoders and OLED.
                            </Alert>
                        )}

                        <Card variant="outlined">
                            <CardContent sx={{ display: "flex", alignItems: "center" }}>
                                <Box sx={{ flex: 1 }}>
                                    <Typography variant="subtitle1">Enable hardware inputs</Typography>
                                    <Typography variant="body2" color="text.secondary">
                                        Encoder roles are global. Which two parameters the knobs edit follows the scroll position, which is saved with each preset.
                                    </Typography>
                                </Box>
                                <Switch checked={settings.enabled}
                                    onChange={event => {
                                        const copy = settings.clone();
                                        copy.enabled = event.target.checked;
                                        setSettings(copy);
                                    }} />
                            </CardContent>
                        </Card>

                        <Card variant="outlined">
                            <CardContent>
                                <Typography variant="subtitle1">Parameter knob resolution</Typography>
                                <Typography variant="body2" color="text.secondary" sx={{ mb: 2 }}>
                                    Clicks needed to take a continuous parameter from its minimum to its maximum. Parameters that declare
                                    their own steps, and switched, integer and enumerated ones, always move by one of their own steps.
                                </Typography>
                                <TextField size="small" type="number" label="Clicks per full range"
                                    value={settings.encoderStepsPerRange}
                                    inputProps={{ min: 4, max: 1000, step: 1 }}
                                    onChange={event => {
                                        const copy = settings.clone();
                                        copy.encoderStepsPerRange = Number(event.target.value);
                                        setSettings(copy);
                                    }} />
                            </CardContent>
                        </Card>

                        <Card variant="outlined">
                            <CardContent>
                                <Stack spacing={2}>
                                    <Box sx={{ display: "flex", alignItems: "center" }}>
                                        <Box sx={{ flex: 1 }}>
                                            <Typography variant="subtitle1">SSD1306 OLED display</Typography>
                                            <Typography variant="body2" color="text.secondary">
                                                Starts on the two shown parameters. Press the scroll encoder to cycle through parameters, waveform, and the built-in strobe tuner.
                                            </Typography>
                                        </Box>
                                        <Switch checked={settings.display.enabled} onChange={event => {
                                            const copy = settings.clone(); copy.display.enabled = event.target.checked; setSettings(copy);
                                        }} />
                                    </Box>
                                    {settings.display.enabled && <>
                                        <Box sx={{ display: "grid", gridTemplateColumns: { xs: "1fr", sm: "1fr 1fr" }, gap: 2 }}>
                                            <TextField select size="small" label="I2C bus" value={settings.display.i2cDevice}
                                                onChange={event => { const copy = settings.clone(); copy.display.i2cDevice = event.target.value; setSettings(copy); }}>
                                                {(capabilities?.i2cDevices ?? []).map(device => <MenuItem key={device} value={device}>{device}</MenuItem>)}
                                                {!capabilities?.i2cDevices.includes(settings.display.i2cDevice) && <MenuItem value={settings.display.i2cDevice}>{settings.display.i2cDevice} (not currently available)</MenuItem>}
                                            </TextField>
                                            <TextField select size="small" label="OLED address" value={settings.display.i2cAddress}
                                                onChange={event => { const copy = settings.clone(); copy.display.i2cAddress = Number(event.target.value); setSettings(copy); }}>
                                                <MenuItem value={0x3C}>0x3C (address switch off)</MenuItem>
                                                <MenuItem value={0x3D}>0x3D (address switch on)</MenuItem>
                                            </TextField>
                                            <TextField size="small" type="number" label="Temporary message time (ms)" value={settings.display.overlayTimeoutMs}
                                                inputProps={{ min: 250, max: 60000, step: 250 }}
                                                onChange={event => { const copy = settings.clone(); copy.display.overlayTimeoutMs = Number(event.target.value); setSettings(copy); }} />
                                            <TextField size="small" type="number" label="OLED refresh (ms)" value={settings.display.refreshIntervalMs}
                                                inputProps={{ min: 50, max: 5000, step: 25 }}
                                                onChange={event => { const copy = settings.clone(); copy.display.refreshIntervalMs = Number(event.target.value); setSettings(copy); }} />
                                        </Box>
                                        <Box sx={{ display: "flex", flexWrap: "wrap", gap: 2 }}>
                                            <FormControlLabel label="Include waveform view in the OLED cycle" control={<Switch checked={settings.display.waveformEnabled}
                                                onChange={event => { const copy = settings.clone(); copy.display.waveformEnabled = event.target.checked; setSettings(copy); }} />} />
                                            <FormControlLabel label="Use output waveform (off = input)" control={<Switch checked={settings.display.waveformOutput}
                                                onChange={event => { const copy = settings.clone(); copy.display.waveformOutput = event.target.checked; setSettings(copy); }} />} />
                                            <FormControlLabel label="Rotate display 180°" control={<Switch checked={settings.display.rotate180}
                                                onChange={event => { const copy = settings.clone(); copy.display.rotate180 = event.target.checked; setSettings(copy); }} />} />
                                        </Box>
                                    </>}
                                </Stack>
                            </CardContent>
                        </Card>

                        {settings.inputs.map((input, index) => {
                            const chip = capabilities?.chips.find(item => item.path === input.chip);
                            const status = statusFor(input.id);
                            return (
                                <Card variant="outlined" key={input.id} sx={{ opacity: input.enabled ? 1 : 0.65 }}>
                                    <CardContent>
                                        <Stack spacing={2}>
                                            <Box sx={{ display: "flex", gap: 1, alignItems: "center" }}>
                                                <TextField label="Name" value={input.name} size="small" sx={{ flex: 1 }}
                                                    onChange={event => updateInput(index, value => value.name = event.target.value)} />
                                                <FormControlLabel label="Enabled" control={
                                                    <Switch checked={input.enabled}
                                                        onChange={event => updateInput(index, value => value.enabled = event.target.checked)} />
                                                } />
                                                <IconButton aria-label="Remove input" onClick={() => {
                                                    const copy = settings.clone();
                                                    copy.inputs.splice(index, 1);
                                                    setSettings(copy);
                                                }}><DeleteOutlineIcon /></IconButton>
                                            </Box>

                                            <TextField select label="Input type" size="small" value={input.inputType}
                                                onChange={event => updateInput(index, value => {
                                                    value.inputType = Number(event.target.value) as GpioInputType;
                                                    if (value.inputType !== GpioInputType.Encoder) {
                                                        value.encoderRole = GpioEncoderRole.None;
                                                    }
                                                    if (value.inputType === GpioInputType.Analog && !value.analogPath && capabilities?.analogChannels.length) {
                                                        value.analogPath = capabilities.analogChannels[0].path;
                                                    }
                                                })}>
                                                {[GpioInputType.Momentary, GpioInputType.Latching, GpioInputType.Encoder, GpioInputType.Analog].map(type =>
                                                    <MenuItem key={type} value={type}>{inputTypeName(type)}</MenuItem>)}
                                            </TextField>

                                            {input.inputType === GpioInputType.Encoder ? (
                                                <>
                                                    <Box sx={{ display: "grid", gridTemplateColumns: { xs: "1fr", sm: "1fr 1fr" }, gap: 2 }}>
                                                        <TextField select label="I2C bus" size="small" value={input.i2cDevice}
                                                            onChange={event => updateInput(index, value => value.i2cDevice = event.target.value)}>
                                                            {(capabilities?.i2cDevices ?? []).map(device => <MenuItem key={device} value={device}>{device}</MenuItem>)}
                                                            {!capabilities?.i2cDevices.includes(input.i2cDevice) && <MenuItem value={input.i2cDevice}>{input.i2cDevice} (not currently available)</MenuItem>}
                                                        </TextField>
                                                        <TextField select label="Encoder address" size="small" value={input.i2cAddress}
                                                            onChange={event => updateInput(index, value => value.i2cAddress = Number(event.target.value))}>
                                                            {[0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D].map(address =>
                                                                <MenuItem key={address} value={address}>0x{address.toString(16).toUpperCase()}</MenuItem>)}
                                                        </TextField>
                                                        <TextField label="Button debounce (ms)" type="number" size="small" value={input.debounceMs}
                                                            inputProps={{ min: 0, max: 2000 }}
                                                            onChange={event => updateInput(index, value => value.debounceMs = Number(event.target.value))} />
                                                        <TextField label="Poll interval (ms)" type="number" size="small" value={input.encoderPollIntervalMs}
                                                            inputProps={{ min: 5, max: 1000 }}
                                                            onChange={event => updateInput(index, value => value.encoderPollIntervalMs = Number(event.target.value))} />
                                                    </Box>
                                                    <TextField select label="Controller role" size="small" value={input.encoderRole}
                                                        helperText="A standard role reserves this encoder's turn action. Parameter encoder push buttons remain available to advanced mappings."
                                                        onChange={event => {
                                                            const copy = settings.clone();
                                                            const role = Number(event.target.value) as GpioEncoderRole;
                                                            if (role !== GpioEncoderRole.None) {
                                                                for (const candidate of copy.inputs) {
                                                                    if (candidate.encoderRole === role) candidate.encoderRole = GpioEncoderRole.None;
                                                                }
                                                            }
                                                            copy.inputs[index].encoderRole = role;
                                                            copy.encoderRolesConfigured = true;
                                                            setSettings(copy);
                                                        }}>
                                                        {[GpioEncoderRole.None, GpioEncoderRole.PresetBrowser, GpioEncoderRole.ParameterScroll,
                                                            GpioEncoderRole.Parameter1, GpioEncoderRole.Parameter2].map(role =>
                                                            <MenuItem key={role} value={role}>{encoderRoleName(role)}</MenuItem>)}
                                                    </TextField>
                                                    <FormControlLabel label="Reverse rotation (recommended for clockwise = increase)"
                                                        control={<Switch checked={input.encoderReversed}
                                                            onChange={event => updateInput(index, value => value.encoderReversed = event.target.checked)} />} />
                                                </>
                                            ) : input.inputType !== GpioInputType.Analog ? (
                                                <>
                                                    <Box sx={{ display: "grid", gridTemplateColumns: { xs: "1fr", sm: "1fr 1fr" }, gap: 2 }}>
                                                        <TextField select label="GPIO controller" size="small" value={input.chip}
                                                            onChange={event => updateInput(index, value => value.chip = event.target.value)}>
                                                            {(capabilities?.chips ?? []).map(item =>
                                                                <MenuItem key={item.path} value={item.path}>{item.path} — {item.label || item.name}</MenuItem>)}
                                                            {!capabilities?.chips.length && <MenuItem value={input.chip}>{input.chip}</MenuItem>}
                                                        </TextField>
                                                        {chip ? (
                                                            <TextField select label="GPIO line (BCM on Pi)" size="small" value={input.line}
                                                                onChange={event => updateInput(index, value => value.line = Number(event.target.value))}>
                                                                {chip.lines.map(line => {
                                                                    const busy = line.used || !!line.consumer;
                                                                    const pin = chip.path === "/dev/gpiochip0" ? headerPins[line.offset] : undefined;
                                                                    const details = [pin ? `header pin ${pin}` : "", line.name, busy ? `in use${line.consumer ? ` by ${line.consumer}` : ""}` : ""]
                                                                        .filter(Boolean).join(" — ");
                                                                    return <MenuItem key={line.offset} value={line.offset}
                                                                        disabled={busy && line.offset !== input.line}>
                                                                        GPIO {line.offset}{details ? ` — ${details}` : ""}
                                                                    </MenuItem>;
                                                                })}
                                                            </TextField>
                                                        ) : (
                                                            <TextField label="GPIO line (BCM on Pi)" type="number" size="small" value={input.line}
                                                                onChange={event => updateInput(index, value => value.line = Number(event.target.value))} />
                                                        )}
                                                    </Box>
                                                    <Box sx={{ display: "grid", gridTemplateColumns: { xs: "1fr", sm: "1fr 1fr" }, gap: 2 }}>
                                                        <TextField select label="Internal pull resistor" size="small" value={input.pull}
                                                            onChange={event => updateInput(index, value => value.pull = Number(event.target.value) as GpioPull)}>
                                                            <MenuItem value={GpioPull.None}>None / external resistor</MenuItem>
                                                            <MenuItem value={GpioPull.Up}>Pull up</MenuItem>
                                                            <MenuItem value={GpioPull.Down}>Pull down</MenuItem>
                                                        </TextField>
                                                        <TextField label="Debounce (ms)" type="number" size="small" value={input.debounceMs}
                                                            inputProps={{ min: 0, max: 2000 }}
                                                            onChange={event => updateInput(index, value => value.debounceMs = Number(event.target.value))} />
                                                    </Box>
                                                    <FormControlLabel label="Active when low (usual for a switch wired between GPIO and ground)"
                                                        control={<Switch checked={input.activeLow}
                                                            onChange={event => updateInput(index, value => value.activeLow = event.target.checked)} />} />
                                                </>
                                            ) : (
                                                <>
                                                    {!capabilities?.analogChannels.length && (
                                                        <Alert severity="info">
                                                            No IIO ADC channels are present. Configure an external ADC (for example MCP3008 via a device-tree overlay), then return here.
                                                        </Alert>
                                                    )}
                                                    <TextField select label="ADC channel" size="small" value={input.analogPath}
                                                        onChange={event => updateInput(index, value => value.analogPath = event.target.value)}>
                                                        {(capabilities?.analogChannels ?? []).map(channel =>
                                                            <MenuItem value={channel.path} key={channel.path}>
                                                                {channel.deviceName} — {channel.channelName}
                                                            </MenuItem>)}
                                                        {input.analogPath && !capabilities?.analogChannels.some(channel => channel.path === input.analogPath) &&
                                                            <MenuItem value={input.analogPath}>{input.analogPath} (not currently available)</MenuItem>}
                                                    </TextField>
                                                    <Box sx={{ display: "grid", gridTemplateColumns: { xs: "1fr 1fr", sm: "repeat(4, 1fr)" }, gap: 2 }}>
                                                        <TextField label="Raw minimum" type="number" size="small" value={input.analogMin}
                                                            onChange={event => updateInput(index, value => value.analogMin = Number(event.target.value))} />
                                                        <TextField label="Raw maximum" type="number" size="small" value={input.analogMax}
                                                            onChange={event => updateInput(index, value => value.analogMax = Number(event.target.value))} />
                                                        <TextField label="Smoothing (0–0.99)" type="number" size="small" value={input.smoothing}
                                                            inputProps={{ min: 0, max: 0.99, step: 0.01 }}
                                                            onChange={event => updateInput(index, value => value.smoothing = Number(event.target.value))} />
                                                        <TextField label="Deadband (0–1)" type="number" size="small" value={input.deadband}
                                                            inputProps={{ min: 0, max: 1, step: 0.001 }}
                                                            onChange={event => updateInput(index, value => value.deadband = Number(event.target.value))} />
                                                    </Box>
                                                </>
                                            )}

                                            {settings.enabled && input.enabled && status && (
                                                <>
                                                    <Divider />
                                                    <Typography variant="caption" color={status.error ? "error" : "text.secondary"}>
                                                        {status.error || (status.connected
                                                            ? `Live value: ${input.inputType === GpioInputType.Analog ? `${Math.round(status.value * 100)}%` : input.inputType === GpioInputType.Encoder ? (status.buttonPressed ? "button pressed" : "ready — turns are relative ±1 events") : (status.value >= 0.5 ? "On / pressed" : "Off / released")}`
                                                            : "Waiting for input…")}
                                                    </Typography>
                                                </>
                                            )}
                                        </Stack>
                                    </CardContent>
                                </Card>
                            );
                        })}

                        <Box sx={{ display: "flex", flexWrap: "wrap", gap: 1 }}>
                            <Button startIcon={<AddIcon />} variant="contained" onClick={addEncoderRig}>Set up four encoders + OLED</Button>
                            <Button startIcon={<AddIcon />} variant="outlined" onClick={addInput}>Add hardware input</Button>
                        </Box>
                        <Box sx={{ height: 16 }} />
                    </Stack>
                </Box>
            </Box>
        </DialogEx>
    );
}
