// SuprKnob: the standard continuous control for Supr pedal faces.
//
// The drawing shares SuprStepKnob's body, rim and 270-degree sweep, but a
// continuous control always carries a pointer and normally has no detent
// ring. Optional endpoint marks frame a sweep without acting as indicators;
// the fill ring is for controls where increasing amount is itself meaningful.
//
// Control semantics stay with PiPedal's UiControl. Its valueToRange and
// rangeToValue methods provide the host's log taper and quantisation, and
// formatDisplayValue preserves scale-point labels and unit formatting.
//
// MIT license, (c) 2026 SuprDuprNatural.

import React from 'react';
import { isDarkMode } from './DarkMode';
import { UiControl } from './Lv2Plugin';
import { PiPedalModel, PiPedalModelFactory } from './PiPedalModel';

export interface SuprKnobProps {
    instanceId: number;
    uiControl: UiControl;
    /** Current port value, kept authoritative outside an active drag. */
    value: number;
    /** Static endpoints, a default/home mark, or a progressive fill ring. */
    marks?: SuprKnobMarks;
    /** Total dots in a fill ring, including the two endpoints. */
    markCount?: number;
    /** Diameter of the knob body in px. */
    size?: number;
}

export type SuprKnobMarks = "none" | "endpoints" | "home" | "fill";

const PX_PER_RANGE = 120;
const FINE_MULTIPLIER = 10;
const ULTRA_FINE_MULTIPLIER = 50;
const CONTROL_WIDTH = 80;

interface DragState {
    pointerId: number;
    lastY: number;
    rawRange: number;
    value: number;
}

function clampRange(range: number): number {
    return Math.max(0, Math.min(1, range));
}

export default function SuprKnob(props: SuprKnobProps) {
    const {
        instanceId, uiControl, value,
        marks = "none", markCount = 11, size = 44
    } = props;

    const model: PiPedalModel = PiPedalModelFactory.getInstance();
    const dark = isDarkMode();
    const drag = React.useRef<DragState | null>(null);
    const [live, setLive] = React.useState<number | null>(null);

    const normalise = React.useCallback((v: number) => {
        return uiControl.rangeToValue(uiControl.valueToRange(v));
    }, [uiControl]);

    const shown = normalise(live !== null ? live : value);
    const shownRange = uiControl.valueToRange(shown);

    const previewRange = (range: number): number => {
        const next = uiControl.rangeToValue(clampRange(range));
        setLive(next);
        model.previewPedalboardValue(instanceId, uiControl.symbol, next);
        return next;
    };

    const commitValue = (next: number) => {
        const normalised = normalise(next);
        model.setPedalboardControl(instanceId, uiControl.symbol, normalised);
        setLive(null);
    };

    const rangeIncrement = React.useCallback(() => {
        if (uiControl.range_steps > 1)
            return 1 / (uiControl.range_steps - 1);
        if (uiControl.integer_property) {
            const integerSteps = Math.round(
                Math.abs(uiControl.max_value - uiControl.min_value)
            );
            if (integerSteps > 0)
                return 1 / integerSteps;
        }
        return 0.01;
    }, [uiControl]);

    const onPointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
        if (e.pointerType === "mouse" && e.button !== 0)
            return;
        e.currentTarget.setPointerCapture?.(e.pointerId);
        drag.current = {
            pointerId: e.pointerId,
            lastY: e.clientY,
            rawRange: shownRange,
            value: shown
        };
        e.preventDefault();
        e.stopPropagation();
    };

    const onPointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
        const current = drag.current;
        if (!current || current.pointerId !== e.pointerId)
            return;

        let scale = PX_PER_RANGE;
        if (e.ctrlKey)
            scale *= ULTRA_FINE_MULTIPLIER;
        else if (e.shiftKey)
            scale *= FINE_MULTIPLIER;

        current.rawRange = clampRange(
            current.rawRange + (current.lastY - e.clientY) / scale
        );
        current.lastY = e.clientY;
        current.value = previewRange(current.rawRange);
        e.preventDefault();
        e.stopPropagation();
    };

    const finishPointer = (e: React.PointerEvent<HTMLDivElement>) => {
        const current = drag.current;
        if (!current || current.pointerId !== e.pointerId)
            return;
        drag.current = null;
        commitValue(current.value);
        e.preventDefault();
        e.stopPropagation();
    };

    const onWheel = (e: React.WheelEvent<HTMLDivElement>) => {
        const direction = e.deltaY < 0 ? 1 : -1;
        const fine = e.shiftKey ? 0.1 : 1;
        const range = shownRange + direction * rangeIncrement() * fine;
        commitValue(uiControl.rangeToValue(clampRange(range)));
        e.preventDefault();
        e.stopPropagation();
    };

    const onDoubleClick = (e: React.MouseEvent<HTMLDivElement>) => {
        commitValue(uiControl.default_value);
        e.preventDefault();
        e.stopPropagation();
    };

    const onKeyDown = (e: React.KeyboardEvent<HTMLDivElement>) => {
        let delta = 0;
        switch (e.key) {
            case "ArrowUp":
            case "ArrowRight":
                delta = rangeIncrement();
                break;
            case "ArrowDown":
            case "ArrowLeft":
                delta = -rangeIncrement();
                break;
            case "PageUp":
                delta = 0.1;
                break;
            case "PageDown":
                delta = -0.1;
                break;
            case "Home":
                commitValue(uiControl.min_value);
                break;
            case "End":
                commitValue(uiControl.max_value);
                break;
            default:
                return;
        }
        if (delta !== 0) {
            if (e.shiftKey)
                delta *= 0.1;
            commitValue(uiControl.rangeToValue(clampRange(shownRange + delta)));
        }
        e.preventDefault();
        e.stopPropagation();
    };

    const angle = -135 + shownRange * 270;
    const r = size / 2;
    const pointer = 0.30 * size;

    const body = dark ? "#2b2b2b" : "#d9d9d9";
    const rim = dark ? "#4a4a4a" : "#b3b3b3";
    const pointerColor = dark ? "#e8e8e8" : "#333333";
    const tick = dark ? "rgba(255,255,255,0.24)" : "rgba(0,0,0,0.24)";
    const tickOn = dark ? "#e88f4d" : "#d2691e";
    const text = dark ? "rgba(255,255,255,0.72)" : "rgba(0,0,0,0.66)";

    const dotCount = marks === "fill" ? Math.max(2, Math.round(markCount)) : 2;
    // A continuous value almost never lands bit-exactly on an intermediate
    // dot, especially after clamping at an endpoint. Quantise only the
    // drawing to the nearest dot: the port itself remains fully continuous.
    const litThrough = Math.round(shownRange * (dotCount - 1));
    const defaultRange = uiControl.valueToRange(uiControl.default_value);
    const markRanges = marks === "fill"
        ? Array.from({ length: dotCount }, (_, i) => i / (dotCount - 1))
        : marks === "home"
            ? [0, defaultRange, 1].filter((range, index, values) =>
                values.findIndex((other) => Math.abs(other - range) < 0.0001) === index
            )
            : [0, 1];

    const reading = uiControl.formatDisplayValue(shown);

    return (
        <div style={{
            width: CONTROL_WIDTH, display: "flex", flexFlow: "column nowrap",
            alignItems: "center", userSelect: "none", touchAction: "none"
        }}>
            <style>{`
                .supr-knob-input:focus { outline: none; }
                .supr-knob-input:focus-visible {
                    outline: 2px solid ${tickOn};
                    outline-offset: 2px;
                    border-radius: 3px;
                }
            `}</style>
            <div style={{
                height: 20, display: "flex", alignItems: "center",
                fontSize: 11, color: text
            }}>
                {uiControl.name}
            </div>
            <div
                className="supr-knob-input"
                role="slider"
                tabIndex={0}
                aria-label={uiControl.name}
                aria-valuemin={uiControl.min_value}
                aria-valuemax={uiControl.max_value}
                aria-valuenow={shown}
                aria-valuetext={reading}
                onPointerDown={onPointerDown}
                onPointerMove={onPointerMove}
                onPointerUp={finishPointer}
                onPointerCancel={finishPointer}
                onWheel={onWheel}
                onDoubleClick={onDoubleClick}
                onKeyDown={onKeyDown}
                style={{
                    cursor: "ns-resize", lineHeight: 0, outlineOffset: 2
                }}
                title={`${uiControl.name} ${reading}`}
            >
                <svg width={size + 14} height={size + 14}
                    viewBox={`${-7} ${-7} ${size + 14} ${size + 14}`}>
                    {marks !== "none" && markRanges.map((range, i) => {
                        const a = (-135 + range * 270) * Math.PI / 180;
                        const rr = r + 5;
                        const endpoint = range < 0.0001 || range > 0.9999;
                        const on = (marks === "fill" && i <= litThrough)
                            || (marks === "home"
                                && Math.abs(range - defaultRange) < 0.0001
                                && Math.abs(shownRange - defaultRange) <= 0.008);
                        return (
                            <circle key={i}
                                cx={r + rr * Math.sin(a)}
                                cy={r - rr * Math.cos(a)}
                                r={endpoint ? 1.9 : (on ? 1.55 : 1.15)}
                                fill={on ? tickOn : tick} />
                        );
                    })}
                    <circle cx={r} cy={r} r={r - 1} fill={body}
                        stroke={rim} strokeWidth="2" />
                    <line
                        x1={r} y1={r}
                        x2={r + pointer * Math.sin(angle * Math.PI / 180)}
                        y2={r - pointer * Math.cos(angle * Math.PI / 180)}
                        stroke={pointerColor} strokeWidth="2.5"
                        strokeLinecap="round" />
                </svg>
            </div>
            <div style={{
                height: 22, fontSize: 11, color: text,
                display: "flex", alignItems: "flex-start"
            }}>
                {reading}
            </div>
        </div>
    );
}
