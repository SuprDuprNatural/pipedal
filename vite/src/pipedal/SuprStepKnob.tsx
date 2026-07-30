// SuprStepKnob: a small knob that snaps, in the spirit of the calibration
// trims on console gear — the API 2500's HR being the obvious one.
//
// WHY IT EXISTS. The standard control is the wrong shape for a calibration.
// A continuous dial with a numeric readout says "sweep me and listen"; a trim
// is something you set once, in fixed steps, against a mark. So this is
// deliberately small, has a detent per step, and shows which step it is on
// rather than a number to three decimal places.
//
// TWO VARIANTS, both worth keeping:
//
//   "full"     label + detent ring + pointer + dB readout.
//              The explicit one. Use where the value matters numerically or
//              where the control is unfamiliar and wants spelling out.
//
//   "minimal"  label + detent ring only. No pointer, no readout: the lit
//              detent IS the indicator. Reads as a piece of hardware rather
//              than a widget, and at this size the pointer and the number
//              were both saying what the lit dot already said. This is the
//              default for a reason — it is the nicer of the two — but the
//              value is still on the hover title, so nothing is lost.
//
// It writes the port directly rather than going through PluginControl, which
// is what buys the different behaviour — and means it must do the two things
// PluginControl would otherwise do for it: clamp to the port's range, and
// tell the server on release rather than on every pixel of the drag.
//
// Reusable: point it at any port with any step, range and size. See
// docs/DESIGN_LANGUAGE.md in the SuprPedals repo for where it fits.
//
// MIT license, (c) 2026 SuprDuprNatural.

import React from 'react';
import { PiPedalModel, PiPedalModelFactory } from "./PiPedalModel";
import { isDarkMode } from './DarkMode';

export type SuprStepKnobVariant = "full" | "minimal";

export interface SuprStepKnobProps {
    instanceId: number;
    symbol: string;
    /** Current port value, e.g. from the panel builder's controlValues. */
    value: number;
    label?: string;
    /** Detent size in the port's own units. */
    step?: number;
    min?: number;
    max?: number;
    /** Diameter of the knob body in px; the detent ring sits just outside. */
    size?: number;
    /** Suffix on the readout and the hover title. */
    unit?: string;
    variant?: SuprStepKnobVariant;
}

// Vertical travel for one detent. Loose enough that a step is deliberate,
// tight enough that the whole range is one comfortable drag.
const PX_PER_STEP = 14;

export default function SuprStepKnob(props: SuprStepKnobProps) {
    const {
        instanceId, symbol, value,
        label, step = 3, min = -12, max = 12, size = 44,
        unit = "dB", variant = "minimal"
    } = props;

    const model: PiPedalModel = PiPedalModelFactory.getInstance();
    const dark = isDarkMode();

    const snap = React.useCallback((v: number) => {
        const s = Math.round(v / step) * step;
        return Math.max(min, Math.min(max, s));
    }, [step, min, max]);

    // The drag's own origin, so a drag is measured from where it started
    // rather than accumulating rounding as it goes.
    const drag = React.useRef<{ y: number; from: number } | null>(null);
    const [live, setLive] = React.useState<number | null>(null);

    const shown = snap(live !== null ? live : value);

    const commit = (v: number, final: boolean) => {
        const s = snap(v);
        if (final) {
            model.setPedalboardControl(instanceId, symbol, s);
            setLive(null);
        } else {
            setLive(s);
            model.previewPedalboardValue(instanceId, symbol, s);
        }
    };

    const onPointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
        (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
        drag.current = { y: e.clientY, from: shown };
        e.preventDefault();
    };
    const onPointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
        if (!drag.current)
            return;
        const dy = drag.current.y - e.clientY;   // up = more
        commit(drag.current.from + (dy / PX_PER_STEP) * step, false);
    };
    const onPointerUp = () => {
        if (!drag.current)
            return;
        drag.current = null;
        // The drag already snapped every move; release just makes the value
        // the server's rather than a preview.
        commit(shown, true);
    };
    const onWheel = (e: React.WheelEvent<HTMLDivElement>) => {
        e.preventDefault();
        commit(shown + (e.deltaY < 0 ? step : -step), true);
    };
    const onDoubleClick = () => commit(0, true);

    // Geometry: a 270-degree sweep, zero at the top, which is how a trim with
    // a centre detent reads at a glance.
    const steps = Math.round((max - min) / step);
    const frac = (shown - min) / (max - min);
    const angle = -135 + frac * 270;
    const r = size / 2;
    const pointer = 0.30 * size;

    const body = dark ? "#2b2b2b" : "#d9d9d9";
    const rim = dark ? "#4a4a4a" : "#b3b3b3";
    const mark = dark ? "#e8e8e8" : "#333333";
    const tick = dark ? "rgba(255,255,255,0.30)" : "rgba(0,0,0,0.30)";
    const tickOn = dark ? "#e88f4d" : "#d2691e"; // supr orange, for the detent
    const text = dark ? "rgba(255,255,255,0.72)" : "rgba(0,0,0,0.66)";

    const dots = [];
    for (let i = 0; i <= steps; ++i) {
        const a = (-135 + (i / steps) * 270) * Math.PI / 180;
        const rr = r + 5;
        const on = Math.abs(min + i * step - shown) < step / 2;
        dots.push(
            <circle key={i}
                cx={r + rr * Math.sin(a)} cy={r - rr * Math.cos(a)}
                r={on ? 1.9 : 1.2} fill={on ? tickOn : tick} />
        );
    }

    const sign = shown > 0 ? "+" : "";
    const reading = `${sign}${shown}${unit ? " " + unit : ""}`;
    return (
        <div style={{
            width: size + 18, display: "flex", flexFlow: "column nowrap",
            alignItems: "center", userSelect: "none", touchAction: "none"
        }}>
            {label && (
                <div style={{ fontSize: 11, color: text, marginBottom: 2 }}>
                    {label}
                </div>
            )}
            <div
                onPointerDown={onPointerDown}
                onPointerMove={onPointerMove}
                onPointerUp={onPointerUp}
                onPointerCancel={onPointerUp}
                onWheel={onWheel}
                onDoubleClick={onDoubleClick}
                style={{ cursor: "ns-resize", lineHeight: 0 }}
                title={`${label ?? symbol} ${reading}`}
            >
                <svg width={size + 14} height={size + 14}
                    viewBox={`${-7} ${-7} ${size + 14} ${size + 14}`}>
                    {dots}
                    <circle cx={r} cy={r} r={r - 1} fill={body}
                        stroke={rim} strokeWidth="2" />
                    {variant === "full" && (
                        <line
                            x1={r} y1={r}
                            x2={r + pointer * Math.sin(angle * Math.PI / 180)}
                            y2={r - pointer * Math.cos(angle * Math.PI / 180)}
                            stroke={mark} strokeWidth="2.5"
                            strokeLinecap="round" />
                    )}
                </svg>
            </div>
            {variant === "full" && (
                <div style={{ fontSize: 11, color: text, marginTop: 1 }}>
                    {reading}
                </div>
            )}
        </div>
    );
}
