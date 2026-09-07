// Live low-band compression, gate state and output headroom. No DSP estimates.
import React from 'react';
import { MonitorPortHandle, PiPedalModelFactory, State } from './PiPedalModel';
import { isDarkMode } from './DarkMode';

export default function SuprForgeDisplay({ instanceId, variant = "levels" }: { instanceId: number; variant?: "levels" | "gate" }) {
    const model = PiPedalModelFactory.getInstance();
    const [values, setValues] = React.useState({ low_gr: 0, gate_gr: 0, peak: -120 });
    React.useEffect(() => {
        let handles: MonitorPortHandle[] = [];
        let mounted = true;
        const remove = () => {
            handles.forEach(handle => model.unmonitorPort(handle));
            handles = [];
        };
        const subscribe = () => {
            remove();
            const ports: ('low_gr' | 'gate_gr' | 'peak')[] = variant === 'gate' ? ['gate_gr'] : ['low_gr', 'peak'];
            for (const port of ports) {
                handles.push(model.monitorPort(instanceId, port, 1 / 30, value => {
                    if (mounted && Number.isFinite(value))
                        setValues(previous => ({ ...previous, [port]: value }));
                }));
            }
        };
        const stateChanged = (state: State) => { if (state === State.Ready) subscribe(); };
        model.state.addOnChangedHandler(stateChanged);
        subscribe();
        return () => {
            mounted = false;
            model.state.removeOnChangedHandler(stateChanged);
            remove();
        };
    }, [instanceId, model, variant]);
    const dark = isDarkMode();
    const orange = dark ? '#e88f4d' : '#d2691e';
    const blue = dark ? '#6fa7d8' : '#3773aa';
    const track = dark ? 'rgba(255,255,255,.10)' : 'rgba(0,0,0,.10)';
    const gateDb = Math.min(60, Math.max(0, -values.gate_gr));
    const gate = gateDb < 1 ? 'OPEN' : gateDb > 50 ? 'CLOSED' : 'CLOSING';
    // These are meters, not additional controls. Segment thresholds are in dB,
    // with reduction growing right-to-left and output growing left-to-right.
    const segments = (value: number, min: number, max: number, color: string,
        reverse = false) => (
        <div aria-hidden="true" style={{ display: 'flex', flexDirection: reverse ? 'row-reverse' : 'row', gap: 2, height: 6 }}>
            {Array.from({ length: 18 }, (_, i) => {
                const lit = value > min + i * (max - min) / 18;
                return <span key={i} style={{ flex: '1 1 0', borderRadius: 1,
                    background: lit ? color : track,
                    boxShadow: lit ? `0 0 3px ${color}44` : undefined }} />;
            })}
        </div>
    );
    if (variant === 'gate') return (
        <div data-supr-forge-gate role="meter" aria-label="Gate attenuation"
            aria-valuemin={0} aria-valuemax={60} aria-valuenow={gateDb}
            aria-valuetext={`${gate}, ${gateDb.toFixed(1)} dB reduction`}
            style={{ width: 80, height: 96, boxSizing: 'border-box', display: 'flex',
                flexDirection: 'column', justifyContent: 'center', gap: 9,
                fontVariantNumeric: 'tabular-nums' }}>
            <div style={{ borderRadius: 4, padding: '10px 3px', textAlign: 'center',
                background: dark ? 'rgba(0,0,0,.23)' : 'rgba(0,0,0,.045)',
                border: `1px solid ${track}`, boxShadow: 'inset 0 1px 3px rgba(0,0,0,.13)' }}>
                <div style={{ fontSize: 9, letterSpacing: '.09em', color: gateDb < 1 ? orange : blue }}>{gate}</div>
                <div style={{ marginTop: 9 }}>{segments(gateDb, 0, 60, blue, true)}</div>
            </div>
        </div>
    );
    const low = Math.min(60, Math.max(0, -values.low_gr));
    const peak = Math.max(-120, Math.min(36, values.peak));
    return (
        <div data-supr-forge-display style={{ width: 260, maxWidth: '100%', boxSizing: 'border-box',
            padding: '5px 0 7px', display: 'grid', gridTemplateColumns: '51px minmax(0, 1fr) 45px',
            alignItems: 'center', columnGap: 8, rowGap: 11, fontVariantNumeric: 'tabular-nums' }}>
            <div style={{ fontSize: 9, opacity: .6, letterSpacing: '.06em' }}>LOW GR</div>
            <div role="meter" aria-label="Low compression" aria-valuemin={0} aria-valuemax={60}
                aria-valuenow={low}>{segments(low, 0, 12, blue, true)}</div>
            <div style={{ fontSize: 10, textAlign: 'right', color: low > .1 ? blue : undefined }}>{low.toFixed(1)} dB</div>
            <div style={{ fontSize: 9, opacity: .6, letterSpacing: '.06em' }}>OUTPUT</div>
            <div role="meter" aria-label="Output peak" aria-valuemin={-120} aria-valuemax={36}
                aria-valuenow={peak}>{segments(peak, -36, 0, peak >= 0 ? '#d84432' : orange)}</div>
            <div style={{ fontSize: 10, textAlign: 'right', color: peak >= 0 ? '#d84432' : undefined }}>
                {peak < -99 ? '−∞' : peak.toFixed(1)} dB
            </div>
        </div>
    );
}
