// Copyright (c) 2026 Robin Davies
// SPDX-License-Identifier: MIT

import { useEffect, useMemo, useState } from 'react';
import Alert from '@mui/material/Alert';
import Box from '@mui/material/Box';
import Button from '@mui/material/Button';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Chip from '@mui/material/Chip';
import Divider from '@mui/material/Divider';
import FormControlLabel from '@mui/material/FormControlLabel';
import IconButton from '@mui/material/IconButton';
import LinearProgress from '@mui/material/LinearProgress';
import MenuItem from '@mui/material/MenuItem';
import Stack from '@mui/material/Stack';
import Switch from '@mui/material/Switch';
import TextField from '@mui/material/TextField';
import Typography from '@mui/material/Typography';
import AddIcon from '@mui/icons-material/Add';
import DeleteOutlineIcon from '@mui/icons-material/DeleteOutline';

import { PiPedalModelFactory } from './PiPedalModel';
import { Pedalboard, PedalboardItem } from './Pedalboard';
import { UiControl } from './Lv2Plugin';
import {
    GpioActionType,
    GpioBinding,
    GpioBindingEventType,
    GpioBindingMode,
    GpioEncoderRole,
    GpioInputConfiguration,
    GpioInputStatus,
    GpioInputType,
    GpioSettings
} from './Gpio';

interface TargetItem {
    instanceId: number;
    name: string;
    item?: PedalboardItem;
}

function actionName(action: GpioActionType): string {
    switch (action) {
        case GpioActionType.Control: return "Effect parameter";
        case GpioActionType.Bypass: return "Effect on / bypass";
        case GpioActionType.LoadPreset: return "Load a preset";
        case GpioActionType.NextPreset: return "Next preset";
        case GpioActionType.PreviousPreset: return "Previous preset";
        case GpioActionType.SelectSnapshot: return "Select snapshot";
        case GpioActionType.NextSnapshot: return "Next snapshot";
        case GpioActionType.PreviousSnapshot: return "Previous snapshot";
        case GpioActionType.NextBank: return "Next bank";
        case GpioActionType.PreviousBank: return "Previous bank";
    }
}

// Numeric fields commit on blur, not per keystroke: each edit is a websocket
// round-trip, and a half-typed "-" or an empty field would otherwise be sent as
// NaN and rejected by the server's mapping validation.
function finiteOr(text: string, fallback: number): number {
    const value = Number(text);
    return text.trim() !== "" && Number.isFinite(value) ? value : fallback;
}

function modeName(mode: GpioBindingMode): string {
    switch (mode) {
        case GpioBindingMode.Direct: return "Follow input position";
        case GpioBindingMode.Toggle: return "Toggle on each press";
        case GpioBindingMode.Trigger: return "Set maximum on press";
        case GpioBindingMode.Relative: return "Move by a step on each click";
    }
}

export default function GpioBindingsView() {
    const model = PiPedalModelFactory.getInstance();
    const [pedalboard, setPedalboard] = useState<Pedalboard>(model.pedalboard.get());
    const [settings, setSettings] = useState<GpioSettings>(model.gpioSettings.get());
    const [statuses, setStatuses] = useState<GpioInputStatus[]>(model.gpioInputStatuses.get());
    const [showAdvanced, setShowAdvanced] = useState(false);

    useEffect(() => {
        const pedalboardChanged = (value: Pedalboard) => setPedalboard(value);
        const settingsChanged = (value: GpioSettings) => setSettings(value);
        const statusesChanged = (value: GpioInputStatus[]) => setStatuses(value);
        model.pedalboard.addOnChangedHandler(pedalboardChanged);
        model.gpioSettings.addOnChangedHandler(settingsChanged);
        model.gpioInputStatuses.addOnChangedHandler(statusesChanged);
        return () => {
            model.pedalboard.removeOnChangedHandler(pedalboardChanged);
            model.gpioSettings.removeOnChangedHandler(settingsChanged);
            model.gpioInputStatuses.removeOnChangedHandler(statusesChanged);
        };
    }, []); // eslint-disable-line react-hooks/exhaustive-deps

    const enabledInputs = useMemo(
        () => settings.inputs.filter(input => input.enabled),
        [settings]);
    const presetEncoder = enabledInputs.find(input => input.encoderRole === GpioEncoderRole.PresetBrowser);
    const effectEncoder = enabledInputs.find(input => input.encoderRole === GpioEncoderRole.EffectSelector);
    const parameter1Encoder = enabledInputs.find(input => input.encoderRole === GpioEncoderRole.Parameter1);
    const parameter2Encoder = enabledInputs.find(input => input.encoderRole === GpioEncoderRole.Parameter2);
    const hasStandardWorkflow = !!effectEncoder && (!!parameter1Encoder || !!parameter2Encoder);

    const targets = useMemo(() => {
        const result: TargetItem[] = [
            { instanceId: Pedalboard.START_CONTROL_ID, name: "Input level" },
            { instanceId: Pedalboard.END_CONTROL_ID, name: "Output level" }
        ];
        for (const item of pedalboard.itemsGenerator()) {
            if (item.isEmpty() || item.isStart() || item.isEnd() || item.isSplit()) continue;
            const plugin = model.getUiPlugin(item.uri);
            result.push({
                instanceId: item.instanceId,
                name: item.title || plugin?.name || item.pluginName || `Effect ${item.instanceId}`,
                item
            });
        }
        return result;
    }, [pedalboard]); // eslint-disable-line react-hooks/exhaustive-deps

    const effectTargets = targets.filter(target => target.item);

    const controlsFor = (instanceId: number): UiControl[] => {
        if (instanceId === Pedalboard.START_CONTROL_ID || instanceId === Pedalboard.END_CONTROL_ID) return [];
        const item = pedalboard.maybeGetItem(instanceId);
        const plugin = item ? model.getUiPlugin(item.uri) : null;
        return plugin?.controls.filter(control => control.is_input && !control.is_bypass) ?? [];
    };

    const applyControlDefaults = (binding: GpioBinding) => {
        if (binding.instanceId === Pedalboard.START_CONTROL_ID || binding.instanceId === Pedalboard.END_CONTROL_ID) {
            binding.symbol = "volume_db";
            binding.minValue = -60;
            binding.maxValue = 12;
            if (binding.mode === GpioBindingMode.Relative) binding.stepValue = 1;
            return;
        }
        const controls = controlsFor(binding.instanceId);
        const control = controls.find(value => value.symbol === binding.symbol) ?? controls[0];
        if (control) {
            binding.symbol = control.symbol;
            binding.minValue = control.min_value;
            binding.maxValue = control.max_value;
            if (binding.mode === GpioBindingMode.Relative) {
                binding.stepValue = control.integer_property || control.enumeration_property
                    ? 1
                    : control.range_steps > 1
                        ? Math.abs(control.max_value - control.min_value) / (control.range_steps - 1)
                        : Math.max(Math.abs(control.max_value - control.min_value) / 100, 0.001);
            }
        } else {
            binding.symbol = "";
            binding.minValue = 0;
            binding.maxValue = 1;
        }
    };

    const saveBindings = (bindings: GpioBinding[]) => model.setGpioBindings(bindings);
    const updateBinding = (index: number, update: (binding: GpioBinding) => void) => {
        const bindings = pedalboard.gpioBindings.map(binding => binding.clone());
        update(bindings[index]);
        saveBindings(bindings);
    };

    const addBinding = () => {
        if (!enabledInputs.length) return;
        const binding = new GpioBinding();
        binding.inputId = enabledInputs[0].id;
        const firstEffect = effectTargets[0];
        if (enabledInputs[0].inputType === GpioInputType.Encoder && firstEffect) {
            binding.actionType = GpioActionType.Control;
            binding.instanceId = firstEffect.instanceId;
            binding.mode = GpioBindingMode.Relative;
            binding.eventType = GpioBindingEventType.EncoderTurn;
            applyControlDefaults(binding);
        } else if (firstEffect) {
            binding.actionType = GpioActionType.Bypass;
            binding.instanceId = firstEffect.instanceId;
            binding.mode = enabledInputs[0].inputType === GpioInputType.Momentary
                ? GpioBindingMode.Toggle : GpioBindingMode.Direct;
        } else {
            binding.actionType = GpioActionType.NextPreset;
            binding.mode = GpioBindingMode.Trigger;
        }
        saveBindings([...pedalboard.gpioBindings.map(value => value.clone()), binding]);
    };

    const standardBindingFor = (instanceId: number, slot: number): GpioBinding | undefined =>
        pedalboard.gpioBindings.find(binding => binding.parameterSlot === slot && binding.instanceId === instanceId);

    const setStandardParameter = (instanceId: number, slot: number, symbol: string) => {
        const roleInput = slot === 1 ? parameter1Encoder : parameter2Encoder;
        if (!roleInput) return;
        const bindings = pedalboard.gpioBindings.map(value => value.clone());
        let index = bindings.findIndex(binding => binding.parameterSlot === slot && binding.instanceId === instanceId);
        if (!symbol) {
            if (index !== -1) bindings.splice(index, 1);
            saveBindings(bindings);
            return;
        }
        if (index === -1) {
            const binding = new GpioBinding();
            binding.parameterSlot = slot;
            binding.instanceId = instanceId;
            bindings.push(binding);
            index = bindings.length - 1;
        }
        const binding = bindings[index];
        binding.enabled = true;
        binding.inputId = roleInput.id;
        binding.actionType = GpioActionType.Control;
        binding.mode = GpioBindingMode.Relative;
        binding.eventType = GpioBindingEventType.EncoderTurn;
        binding.selectorInputId = effectEncoder?.id ?? "";
        binding.symbol = symbol;
        applyControlDefaults(binding);
        saveBindings(bindings);
    };

    const setStandardStep = (instanceId: number, slot: number, stepValue: number) => {
        const bindings = pedalboard.gpioBindings.map(value => value.clone());
        const binding = bindings.find(value => value.parameterSlot === slot && value.instanceId === instanceId);
        if (!binding || !Number.isFinite(stepValue) || stepValue <= 0) return;
        binding.stepValue = stepValue;
        saveBindings(bindings);
    };

    const inputFor = (id: string): GpioInputConfiguration | undefined => settings.inputs.find(input => input.id === id);
    const statusFor = (id: string): GpioInputStatus | undefined => statuses.find(status => status.inputId === id);

    const renderInputStatus = (input: GpioInputConfiguration) => {
        const status = statusFor(input.id);
        const valueText = !settings.enabled ? "GPIO disabled" : status?.error ? "Error" : !status?.connected ? "Waiting" :
            input.inputType === GpioInputType.Analog ? `${Math.round(status.value * 100)}%` :
            input.inputType === GpioInputType.Encoder ? (status.buttonPressed ? "Pressed" : "Ready") :
            status.value >= 0.5 ? "On" : "Off";
        return (
            <Card variant="outlined" key={input.id} sx={{ minWidth: 160, flex: "1 1 180px" }}>
                <CardContent sx={{ pb: "12px !important" }}>
                    <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
                        <Box sx={{ width: 9, height: 9, borderRadius: "50%", bgcolor: status?.error ? "error.main" : status?.connected ? "success.main" : "text.disabled" }} />
                        <Typography variant="subtitle2" noWrap sx={{ flex: 1 }}>{input.name || "Unnamed input"}</Typography>
                        <Chip size="small" label={valueText} />
                    </Box>
                    {input.inputType === GpioInputType.Analog && (
                        <LinearProgress variant="determinate" value={(status?.value ?? 0) * 100} sx={{ mt: 1 }} />
                    )}
                    {status?.error && <Typography variant="caption" color="error" sx={{ display: "block", mt: 1 }}>{status.error}</Typography>}
                </CardContent>
            </Card>
        );
    };

    if (!settings.enabled) {
        return (
            <Box sx={{ height: "100%", overflow: "auto", p: 3 }}>
                <Alert severity="info">GPIO hardware inputs are disabled. Enable and configure them under Settings → Hardware.</Alert>
            </Box>
        );
    }

    return (
        <Box sx={{ height: "100%", overflow: "auto", p: { xs: 1.5, sm: 2.5 } }}>
            <Stack spacing={2} sx={{ maxWidth: 1100, mx: "auto" }}>
                <Box>
                    <Typography variant="h6">Hardware controls for this preset</Typography>
                    <Typography variant="body2" color="text.secondary">
                        Choose the two parameters you want available for each effect. These choices are saved with this preset.
                    </Typography>
                </Box>

                <Box sx={{ display: "flex", flexWrap: "wrap", gap: 1 }}>
                    {enabledInputs.map(renderInputStatus)}
                </Box>

                {!enabledInputs.length && <Alert severity="warning">No hardware inputs are enabled in Settings.</Alert>}

                {hasStandardWorkflow && (
                    <>
                        <Card variant="outlined">
                            <CardContent>
                                <Typography variant="subtitle1">Standard encoder workflow</Typography>
                                <Typography variant="body2" color="text.secondary" sx={{ mb: 1.5 }}>
                                    The effect selector chooses which row is shown on the OLED. Its push button cycles controls, waveform, and tuner views. Each parameter encoder turn is one relative ±1 event.
                                </Typography>
                                <Box sx={{ display: "flex", flexWrap: "wrap", gap: 1 }}>
                                    {presetEncoder && <Chip label={`${presetEncoder.name}: presets`} />}
                                    {effectEncoder && <Chip label={`${effectEncoder.name}: effects / OLED view`} />}
                                    {parameter1Encoder && <Chip label={`${parameter1Encoder.name}: parameter 1`} />}
                                    {parameter2Encoder && <Chip label={`${parameter2Encoder.name}: parameter 2`} />}
                                </Box>
                            </CardContent>
                        </Card>

                        {effectTargets.filter(target => controlsFor(target.instanceId).length !== 0).map(target => {
                            const controls = controlsFor(target.instanceId);
                            const slot1 = standardBindingFor(target.instanceId, 1);
                            const slot2 = standardBindingFor(target.instanceId, 2);
                            const renderSlot = (slot: number, encoder: GpioInputConfiguration | undefined, binding: GpioBinding | undefined) => {
                                if (!encoder) return null;
                                return (
                                    <Box key={slot} sx={{ display: "grid", gridTemplateColumns: { xs: "1fr", sm: "minmax(220px, 1fr) 150px" }, gap: 1.5 }}>
                                        <TextField select size="small" label={`${encoder.name} parameter`} value={binding?.symbol ?? ""}
                                            onChange={event => setStandardParameter(target.instanceId, slot, event.target.value)}>
                                            <MenuItem value="">Not assigned</MenuItem>
                                            {controls.map(control => <MenuItem key={control.symbol} value={control.symbol}>{control.name}</MenuItem>)}
                                        </TextField>
                                        <TextField size="small" type="number" label="Parameter step / click"
                                            disabled={!binding}
                                            key={`${target.instanceId}-${slot}-${binding?.symbol ?? "none"}`}
                                            defaultValue={binding?.stepValue ?? ""}
                                            inputProps={{ min: 0.000001, max: 1000000, step: "any" }}
                                            onBlur={event => setStandardStep(target.instanceId, slot, Number(event.target.value))} />
                                    </Box>
                                );
                            };
                            return (
                                <Card variant="outlined" key={target.instanceId}>
                                    <CardContent>
                                        <Stack spacing={1.5}>
                                            <Typography variant="subtitle1">{target.name}</Typography>
                                            {renderSlot(1, parameter1Encoder, slot1)}
                                            {renderSlot(2, parameter2Encoder, slot2)}
                                        </Stack>
                                    </CardContent>
                                </Card>
                            );
                        })}
                    </>
                )}

                <Divider />
                {hasStandardWorkflow && (
                    <Button variant="text" onClick={() => setShowAdvanced(value => !value)}>
                        {showAdvanced ? "Hide advanced mappings" : "Show advanced mappings"}
                    </Button>
                )}

                {(showAdvanced || !hasStandardWorkflow) && pedalboard.gpioBindings.map((binding, index) => {
                    if (binding.parameterSlot !== 0) return null;
                    const selectedInput = inputFor(binding.inputId);
                    const isCommand = binding.actionType >= GpioActionType.LoadPreset;
                    const parameterControls = controlsFor(binding.instanceId);
                    return (
                        <Card variant="outlined" key={index} sx={{ opacity: binding.enabled ? 1 : 0.65 }}>
                            <CardContent>
                                <Stack spacing={2}>
                                    <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
                                        <FormControlLabel label="Enabled" control={<Switch checked={binding.enabled}
                                            onChange={event => updateBinding(index, value => value.enabled = event.target.checked)} />} />
                                        <Box sx={{ flex: 1 }} />
                                        <IconButton aria-label="Remove mapping" onClick={() => {
                                            const bindings = pedalboard.gpioBindings.map(value => value.clone());
                                            bindings.splice(index, 1);
                                            saveBindings(bindings);
                                        }}><DeleteOutlineIcon /></IconButton>
                                    </Box>

                                    <Box sx={{ display: "grid", gridTemplateColumns: { xs: "1fr", sm: "1fr 1fr" }, gap: 2 }}>
                                        <TextField select size="small" label="Hardware input" value={binding.inputId}
                                            onChange={event => updateBinding(index, value => {
                                                value.inputId = event.target.value;
                                                const input = inputFor(value.inputId);
                                                if (input?.inputType === GpioInputType.Analog) {
                                                    value.mode = GpioBindingMode.Direct;
                                                    value.eventType = GpioBindingEventType.Value;
                                                } else if (input?.inputType === GpioInputType.Encoder) {
                                                    value.mode = GpioBindingMode.Relative;
                                                    value.eventType = GpioBindingEventType.EncoderTurn;
                                                } else {
                                                    value.eventType = GpioBindingEventType.Value;
                                                }
                                                value.selectorInputId = "";
                                            })}>
                                            {settings.inputs.map(input => <MenuItem key={input.id} value={input.id} disabled={!input.enabled}>
                                                {input.name || "Unnamed input"}{!input.enabled ? " (disabled)" : ""}
                                            </MenuItem>)}
                                        </TextField>
                                        {selectedInput?.inputType === GpioInputType.Encoder && (
                                            <TextField select size="small" label="Encoder control" value={binding.eventType}
                                                onChange={event => updateBinding(index, value => {
                                                    value.eventType = Number(event.target.value) as GpioBindingEventType;
                                                    value.selectorInputId = "";
                                                    value.mode = value.eventType === GpioBindingEventType.EncoderTurn
                                                        ? GpioBindingMode.Relative
                                                        : value.actionType >= GpioActionType.LoadPreset ? GpioBindingMode.Trigger : GpioBindingMode.Toggle;
                                                })}>
                                                <MenuItem value={GpioBindingEventType.EncoderTurn}>Turn</MenuItem>
                                                <MenuItem value={GpioBindingEventType.EncoderButton}>Push button</MenuItem>
                                            </TextField>
                                        )}
                                        <TextField select size="small" label="Action" value={binding.actionType}
                                            onChange={event => updateBinding(index, value => {
                                                value.actionType = Number(event.target.value) as GpioActionType;
                                                if (value.actionType >= GpioActionType.LoadPreset) {
                                                    value.mode = GpioBindingMode.Trigger;
                                                    if (selectedInput?.inputType === GpioInputType.Encoder) {
                                                        value.eventType = GpioBindingEventType.EncoderButton;
                                                        value.selectorInputId = "";
                                                    }
                                                    if (value.actionType === GpioActionType.LoadPreset) value.targetId = model.presets.get().selectedInstanceId;
                                                    if (value.actionType === GpioActionType.SelectSnapshot) value.targetId = 0;
                                                } else if (value.actionType === GpioActionType.Bypass) {
                                                    value.instanceId = effectTargets[0]?.instanceId ?? -1;
                                                    value.minValue = 0;
                                                    value.maxValue = 1;
                                                } else {
                                                    value.instanceId = targets[0]?.instanceId ?? -1;
                                                    applyControlDefaults(value);
                                                }
                                            })}>
                                            {Object.values(GpioActionType).filter(value => typeof value === "number").map(value =>
                                                <MenuItem key={value as number} value={value as number}>{actionName(value as GpioActionType)}</MenuItem>)}
                                        </TextField>
                                    </Box>

                                    {(binding.actionType === GpioActionType.Control || binding.actionType === GpioActionType.Bypass) && (
                                        <Box sx={{ display: "grid", gridTemplateColumns: { xs: "1fr", sm: "1fr 1fr" }, gap: 2 }}>
                                            <TextField select size="small" label={binding.actionType === GpioActionType.Bypass ? "Effect" : "Target"}
                                                value={binding.instanceId}
                                                onChange={event => updateBinding(index, value => {
                                                    value.instanceId = Number(event.target.value);
                                                    if (value.actionType === GpioActionType.Control) applyControlDefaults(value);
                                                })}>
                                                {(binding.actionType === GpioActionType.Bypass ? effectTargets : targets).map(target =>
                                                    <MenuItem key={target.instanceId} value={target.instanceId}>{target.name}</MenuItem>)}
                                            </TextField>
                                            {binding.actionType === GpioActionType.Control && (
                                                binding.instanceId === Pedalboard.START_CONTROL_ID || binding.instanceId === Pedalboard.END_CONTROL_ID ?
                                                    <TextField size="small" label="Parameter" value="Level (dB)" disabled /> :
                                                    <TextField select size="small" label="Parameter" value={binding.symbol}
                                                        onChange={event => updateBinding(index, value => {
                                                            value.symbol = event.target.value;
                                                            applyControlDefaults(value);
                                                        })}>
                                                        {parameterControls.map(control => <MenuItem key={control.symbol} value={control.symbol}>{control.name}</MenuItem>)}
                                                    </TextField>
                                            )}
                                        </Box>
                                    )}

                                    {binding.actionType === GpioActionType.LoadPreset && (
                                        <TextField select size="small" label="Target preset" value={binding.targetId}
                                            onChange={event => updateBinding(index, value => value.targetId = Number(event.target.value))}>
                                            {model.presets.get().presets.map(preset =>
                                                <MenuItem key={preset.instanceId} value={preset.instanceId}>{preset.name}</MenuItem>)}
                                        </TextField>
                                    )}

                                    {binding.actionType === GpioActionType.SelectSnapshot && (
                                        <TextField select size="small" label="Snapshot" value={binding.targetId}
                                            onChange={event => updateBinding(index, value => value.targetId = Number(event.target.value))}>
                                            {pedalboard.snapshots.map((snapshot, snapshotIndex) => snapshot &&
                                                <MenuItem key={snapshotIndex} value={snapshotIndex}>{snapshot.name || `Snapshot ${snapshotIndex + 1}`}</MenuItem>)}
                                        </TextField>
                                    )}

                                    {!isCommand && (
                                        <TextField select size="small" label="Input behavior" value={binding.mode}
                                            onChange={event => updateBinding(index, value => value.mode = Number(event.target.value) as GpioBindingMode)}>
                                            {[GpioBindingMode.Direct, GpioBindingMode.Toggle, GpioBindingMode.Trigger, GpioBindingMode.Relative].map(mode =>
                                                <MenuItem key={mode} value={mode} disabled={
                                                    (selectedInput?.inputType === GpioInputType.Analog && mode !== GpioBindingMode.Direct) ||
                                                    (binding.eventType === GpioBindingEventType.EncoderTurn && mode !== GpioBindingMode.Relative) ||
                                                    (binding.eventType !== GpioBindingEventType.EncoderTurn && mode === GpioBindingMode.Relative)}>
                                                    {modeName(mode)}
                                                </MenuItem>)}
                                        </TextField>
                                    )}

                                    {selectedInput?.inputType === GpioInputType.Encoder &&
                                        binding.eventType === GpioBindingEventType.EncoderTurn &&
                                        binding.actionType === GpioActionType.Control && (
                                        <TextField select size="small" label="Selected by" value={binding.selectorInputId}
                                            helperText="Mappings with the same encoder and selector become a selectable list."
                                            onChange={event => updateBinding(index, value => value.selectorInputId = event.target.value)}>
                                            <MenuItem value="">Always active (move with other mappings)</MenuItem>
                                            {enabledInputs.filter(input => input.inputType === GpioInputType.Encoder && input.id !== binding.inputId)
                                                .map(input => <MenuItem key={input.id} value={input.id}>{input.name || "Unnamed encoder"}</MenuItem>)}
                                        </TextField>
                                    )}

                                    {binding.actionType === GpioActionType.Control && (
                                        <>
                                            <Box sx={{ display: "grid", gridTemplateColumns: { xs: "1fr 1fr", sm: "1fr 1fr 1fr" }, gap: 2 }}>
                                                <TextField size="small" type="number" label="Value at low / off"
                                                    key={`${index}-${binding.symbol}-min`} defaultValue={binding.minValue}
                                                    onBlur={event => updateBinding(index, value =>
                                                        value.minValue = finiteOr(event.target.value, binding.minValue))} />
                                                <TextField size="small" type="number" label="Value at high / on"
                                                    key={`${index}-${binding.symbol}-max`} defaultValue={binding.maxValue}
                                                    onBlur={event => updateBinding(index, value =>
                                                        value.maxValue = finiteOr(event.target.value, binding.maxValue))} />
                                                {binding.mode === GpioBindingMode.Relative ?
                                                    <TextField size="small" type="number" label="Value per encoder click"
                                                        key={`${index}-${binding.symbol}-step`} defaultValue={binding.stepValue}
                                                        inputProps={{ min: 0.000001, max: 1000000, step: "any" }}
                                                        onBlur={event => updateBinding(index, value => {
                                                            const step = finiteOr(event.target.value, binding.stepValue);
                                                            value.stepValue = step > 0 ? step : binding.stepValue;
                                                        })} /> :
                                                    <TextField size="small" type="number" label="Response curve"
                                                        key={`${index}-${binding.symbol}-curve`} defaultValue={binding.curve}
                                                        inputProps={{ min: 0.05, max: 20, step: 0.05 }}
                                                        disabled={binding.mode !== GpioBindingMode.Direct}
                                                        onBlur={event => updateBinding(index, value =>
                                                            value.curve = Math.min(20, Math.max(0.05,
                                                                finiteOr(event.target.value, binding.curve))))} />}
                                            </Box>
                                            <Typography variant="caption" color="text.secondary">
                                                Swap the low and high values to reverse direction. Relative mode clamps every encoder click to this range.
                                            </Typography>
                                        </>
                                    )}

                                    {binding.actionType === GpioActionType.Bypass && binding.mode === GpioBindingMode.Direct && (
                                        <TextField select size="small" label="Direction" value={binding.minValue <= binding.maxValue ? 0 : 1}
                                            onChange={event => updateBinding(index, value => {
                                                const reversed = Number(event.target.value) === 1;
                                                value.minValue = reversed ? 1 : 0;
                                                value.maxValue = reversed ? 0 : 1;
                                            })}>
                                            <MenuItem value={0}>High / on enables the effect</MenuItem>
                                            <MenuItem value={1}>Low / off enables the effect</MenuItem>
                                        </TextField>
                                    )}
                                </Stack>
                            </CardContent>
                        </Card>
                    );
                })}

                {(showAdvanced || !hasStandardWorkflow) && (
                    <>
                        <Divider />
                        <Button variant="outlined" startIcon={<AddIcon />} disabled={!enabledInputs.length} onClick={addBinding}>
                            Add advanced mapping
                        </Button>
                    </>
                )}
                <Box sx={{ height: 16 }} />
            </Stack>
        </Box>
    );
}
