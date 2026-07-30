// SuprSelect: a compact, panel-native selector for categorical ports.

import { isDarkMode } from './DarkMode';
import { UiControl } from './Lv2Plugin';
import { PiPedalModelFactory } from './PiPedalModel';

export interface SuprSelectProps {
    instanceId: number;
    uiControl: UiControl;
    value: number;
    compact?: boolean;
    wide?: boolean;
}

export default function SuprSelect(props: SuprSelectProps) {
    const { instanceId, uiControl, value, compact = false, wide = false } = props;
    const model = PiPedalModelFactory.getInstance();
    const dark = isDarkMode();
    const current = uiControl.clampSelectValue(value);
    const points = [...uiControl.scale_points].sort((a, b) => a.value - b.value);

    const text = dark ? "rgba(255,255,255,0.78)" : "rgba(0,0,0,0.72)";
    const edge = dark ? "#555" : "#aaa";
    const face = dark ? "#303030" : "#d6d6d6";
    const width = compact ? 84 : (wide ? 188 : 124);
    const selectWidth = compact ? 76 : (wide ? 180 : 116);

    return (
        <div style={{
            width, display: "flex", flexFlow: "column nowrap",
            alignItems: "center", userSelect: "none"
        }}>
            <div style={{
                height: 20, display: "flex", alignItems: "center",
                fontSize: 11, color: text,
                visibility: compact ? "hidden" : "visible"
            }}>
                {uiControl.name}
            </div>
            <div style={{ paddingTop: 7, paddingBottom: 7 }}>
                <style>{`
                    .supr-hardware-select:focus:not(:focus-visible) { outline: none; }
                    .supr-hardware-select:focus-visible {
                        outline: 2px solid ${dark ? "#e88f4d" : "#d2691e"};
                        outline-offset: 2px;
                    }
                `}</style>
                <select
                    className="supr-hardware-select"
                    aria-label={uiControl.name}
                    value={current}
                    onChange={(e) => model.setPedalboardControl(
                        instanceId, uiControl.symbol, Number(e.target.value)
                    )}
                    style={{
                        width: selectWidth, height: 32,
                        border: `1px solid ${edge}`, borderRadius: 4,
                        background: face, color: text,
                        boxShadow: dark
                            ? "inset 0 1px 3px rgba(0,0,0,0.55)"
                            : "inset 0 1px 3px rgba(0,0,0,0.18)",
                        colorScheme: dark ? "dark" : "light",
                        fontFamily: "inherit", fontSize: 12,
                        paddingLeft: 8, cursor: "pointer"
                    }}>
                    {points.map((point) => (
                        <option key={point.value} value={point.value}>
                            {point.label}
                        </option>
                    ))}
                </select>
            </div>
            <div style={{ height: 22 }} />
        </div>
    );
}
