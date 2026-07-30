// SuprButton: hardware-style toggle and A/B controls for Supr pedal faces.
//
// State belongs to the cap itself: an engaged control sits lower and picks up
// the face's restrained accent colour. There is deliberately no separate LED
// duplicating the same information.

import React from 'react';
import { isDarkMode } from './DarkMode';
import { UiControl } from './Lv2Plugin';
import { PiPedalModelFactory } from './PiPedalModel';

export interface SuprButtonProps {
    instanceId: number;
    uiControl: UiControl;
    value: number;
    compact?: boolean;
    hideLabel?: boolean;
    buttonText?: string;
}

function stateText(symbol: string, on: boolean): string {
    switch (symbol.toLowerCase()) {
        case "mute": return on ? "MUTED" : "LIVE";
        case "polarity":
        case "polaritya":
        case "polarityb":
        case "polarityc":
            return on ? "FLIP" : "NORM";
        case "air": return on ? "LIFT" : "FLAT";
        case "rumble": return on ? "CUT" : "FLAT";
        default: return on ? "ON" : "OFF";
    }
}

export default function SuprButton(props: SuprButtonProps) {
    const {
        instanceId, uiControl, value, compact = false,
        hideLabel = false, buttonText
    } = props;
    const model = PiPedalModelFactory.getInstance();
    const dark = isDarkMode();
    const isPair = uiControl.scale_points.length === 2;
    const current = isPair
        ? uiControl.clampSelectValue(value)
        : (value === 0 ? 0 : 1);

    const text = dark ? "rgba(255,255,255,0.76)" : "rgba(0,0,0,0.70)";
    const cap = dark ? "#353535" : "#d1d1d1";
    const capEdge = dark ? "#525252" : "#a7a7a7";
    const isCut = ["mute", "rumble"].includes(uiControl.symbol.toLowerCase());
    const accent = isCut
        ? (dark ? "#5d8fa8" : "#397996")
        : (dark ? "#e88f4d" : "#d2691e");
    const outerWidth = compact ? 84 : (isPair ? 120 : 80);

    const setValue = (next: number) => {
        model.setPedalboardControl(instanceId, uiControl.symbol, next);
    };

    const multiline = buttonText?.includes("\n") ?? false;
    const capStyle = (active: boolean): React.CSSProperties => ({
        flex: "1 1 0", minWidth: 0, height: multiline ? 40 : 32,
        border: `1px solid ${active ? accent : capEdge}`,
        borderRadius: 4,
        background: active
            ? (isCut
                ? (dark ? "rgba(93,143,168,0.24)" : "rgba(57,121,150,0.18)")
                : (dark ? "rgba(232,143,77,0.24)" : "rgba(210,105,30,0.18)"))
            : cap,
        color: active ? accent : text,
        boxShadow: active
            ? "inset 0 2px 4px rgba(0,0,0,0.42)"
            : (dark
                ? "0 2px 3px rgba(0,0,0,0.45), inset 0 1px rgba(255,255,255,0.10)"
                : "0 2px 3px rgba(0,0,0,0.24), inset 0 1px rgba(255,255,255,0.70)"),
        fontFamily: "inherit", fontSize: 10, fontWeight: 700,
        lineHeight: 1.15, whiteSpace: "pre-line",
        letterSpacing: "0.035em", padding: "0 5px",
        cursor: "pointer", transform: active ? "translateY(1px)" : undefined
    });

    const points = [...uiControl.scale_points].sort((a, b) => a.value - b.value);
    const reading = uiControl.formatDisplayValue(current);

    return (
        <div style={{
            width: outerWidth, display: "flex", flexFlow: "column nowrap",
            alignItems: "center", userSelect: "none"
        }}>
            {!hideLabel && (
                <div style={{
                    height: 20, display: "flex", alignItems: "center",
                    fontSize: 11, color: text,
                    visibility: compact ? "hidden" : "visible"
                }}>
                    {uiControl.name}
                </div>
            )}
            <div style={{
                width: compact ? 76 : (isPair ? 112 : 56),
                display: "flex", gap: 4,
                paddingTop: multiline ? 3 : 7,
                paddingBottom: multiline ? 3 : 7
            }}>
                <style>{`
                    .supr-hardware-button:focus:not(:focus-visible) { outline: none; }
                    .supr-hardware-button:focus-visible {
                        outline: 2px solid ${accent};
                        outline-offset: 2px;
                    }
                `}</style>
                {isPair ? points.map((point) => {
                    const active = point.value === current;
                    return (
                        <button key={point.value}
                            className="supr-hardware-button"
                            type="button"
                            aria-label={`${uiControl.name}: ${point.label}`}
                            aria-pressed={active}
                            onClick={() => setValue(point.value)}
                            style={capStyle(active)}>
                            {point.label}
                        </button>
                    );
                }) : (
                    <button
                        className="supr-hardware-button"
                        type="button"
                        aria-label={uiControl.name}
                        aria-pressed={current !== 0}
                        onClick={() => setValue(current === 0 ? 1 : 0)}
                        style={capStyle(current !== 0)}>
                        {buttonText ?? stateText(uiControl.symbol, current !== 0)}
                    </button>
                )}
            </div>
            <div style={{
                height: 22, display: "flex", alignItems: "flex-start",
                fontSize: 11, color: text, visibility: "hidden"
            }}>
                {reading}
            </div>
        </div>
    );
}
