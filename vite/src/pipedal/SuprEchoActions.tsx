// Tap writes the same millisecond control as PiPedal MIDI Tap Tempo.
import { useEffect, useRef } from 'react';
import { PiPedalModelFactory } from './PiPedalModel';
import { isDarkMode } from './DarkMode';

export default function SuprEchoActions({ instanceId, division }: { instanceId: number; division: number }) {
    const model = PiPedalModelFactory.getInstance();
    const lastTap = useRef<number | null>(null);
    useEffect(() => { lastTap.current = null; }, [instanceId]);
    const dark = isDarkMode();
    const accent = dark ? "#e88f4d" : "#d2691e";
    const style = {
        height: 32, minWidth: 0, padding: 0,
        border: "1px solid " + (dark ? "#666" : "#999"),
        background: dark ? "#353535" : "#ddd", color: dark ? "#eee" : "#222",
        fontFamily: "inherit", fontSize: 10, cursor: "pointer"
    };
    return <div style={{ width: 80, display: "flex", flexDirection: "column", alignItems: "center", gap: 4 }}>
        <button type="button" style={{ ...style, width: 56, borderRadius: 4 }} aria-label="Tap quarter-note tempo"
            onClick={() => {
                const now = performance.now();
                if (lastTap.current !== null) {
                    const ms = now - lastTap.current;
                    if (ms >= 134 && ms <= 2000) model.setPedalboardControl(instanceId, "time", ms);
                }
                lastTap.current = now;
            }}>Tap</button>
        <div style={{ fontSize: 11 }}>Division</div>
        <div role="group" aria-label="Division" style={{ display: "flex", width: "100%" }}>
            {["1/4", "1/8", "1/8."].map((label, value) => (
                <button key={value} type="button" aria-pressed={division === value}
                    aria-label={`Division: ${["quarter", "eighth", "dotted eighth"][value]}`}
                    onClick={() => model.setPedalboardControl(instanceId, "division", value)}
                    style={{ ...style, flex: 1, borderLeftWidth: value === 0 ? 1 : 0,
                        borderRadius: value === 0 ? "4px 0 0 4px" : value === 2 ? "0 4px 4px 0" : 0,
                        color: division === value ? accent : style.color,
                        background: division === value ? (dark ? "rgba(232,143,77,0.24)" : "rgba(210,105,30,0.18)") : style.background,
                        boxShadow: division === value ? "inset 0 2px 4px rgba(0,0,0,0.3)" : undefined }}>
                    {label}
                </button>
            ))}
        </div>
    </div>;
}
