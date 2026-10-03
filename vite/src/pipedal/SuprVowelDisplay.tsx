// Actual vowel motion and formant centres from the DSP; no browser detector.
import React from 'react';
import { MonitorPortHandle, PiPedalModelFactory, State } from './PiPedalModel';
import { isDarkMode } from './DarkMode';

const vowels = ['OO', 'OH', 'AH', 'EH', 'EE'];
type Values = { morph: number; f1: number; f2: number; f3: number };

export default function SuprVowelDisplay({ instanceId, from, to, width = 348 }: {
    instanceId: number; from: number; to: number; width?: number;
}) {
    const model = PiPedalModelFactory.getInstance();
    const [values, setValues] = React.useState<Partial<Values>>({});
    React.useEffect(() => {
        let handles: MonitorPortHandle[] = [];
        let generation = 0;
        const remove = () => {
            ++generation;
            handles.forEach(handle => model.unmonitorPort(handle));
            handles = [];
        };
        const subscribe = () => {
            remove();
            setValues({});
            if (model.state.get() !== State.Ready) return;
            const current = generation;
            for (const port of ['morph', 'f1', 'f2', 'f3'] as const) {
                handles.push(model.monitorPort(instanceId, port, 1 / 30, value => {
                    if (generation === current && Number.isFinite(value))
                        setValues(previous => ({ ...previous, [port]: value }));
                }));
            }
        };
        model.state.addOnChangedHandler(subscribe);
        subscribe();
        return () => {
            remove();
            model.state.removeOnChangedHandler(subscribe);
        };
    }, [instanceId, model]);
    const dark = isDarkMode();
    const accent = dark ? '#e88f4d' : '#d2691e';
    const secondary = dark ? '#6fa7d8' : '#3773aa';
    const track = dark ? 'rgba(255,255,255,.15)' : 'rgba(0,0,0,.15)';
    const name = (value: number) => vowels[Math.max(0, Math.min(4, Math.round(value)))] ?? '—';
    const position = values.morph === undefined ? undefined : Math.max(0, Math.min(1, values.morph));
    const x = (hz: number) => 18 + 284 * Math.log(Math.max(80, Math.min(6000, hz)) / 80) / Math.log(6000 / 80);
    return (
        <div data-supr-vowel-display style={{ width, maxWidth: '100%', padding: '0 12px',
            boxSizing: 'border-box', fontVariantNumeric: 'tabular-nums' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 12, fontSize: 12, letterSpacing: '.08em' }}>
                <span>{name(from)}</span>
                <div role="meter" aria-label="Vowel position" aria-valuemin={0} aria-valuemax={100}
                    aria-valuenow={position === undefined ? undefined : Math.round(position * 100)}
                    aria-valuetext={position === undefined ? 'Waiting for audio' : `${Math.round(position * 100)} percent from ${name(from)} to ${name(to)}`}
                    style={{ flex: 1, height: 5, borderRadius: 3, background: track, position: 'relative' }}>
                    {position !== undefined && <span style={{ position: 'absolute', left: `${position * 100}%`,
                        top: -2, width: 9, height: 9, transform: 'translateX(-50%)',
                        borderRadius: '50%', background: accent }} />}
                </div>
                <span>{name(to)}</span>
            </div>
            <svg viewBox="0 0 320 58" aria-hidden="true" style={{ display: 'block', width: '100%', marginTop: 8 }}>
                <line x1="18" x2="302" y1="34" y2="34" stroke={track} />
                {[100, 300, 1000, 3000].map(hz => <g key={hz}>
                    <line x1={x(hz)} x2={x(hz)} y1="31" y2="38" stroke={track} />
                    <text x={x(hz)} y="51" textAnchor="middle" fill="currentColor" opacity=".5" fontSize="9">
                        {hz >= 1000 ? `${hz / 1000}k` : hz}
                    </text>
                </g>)}
                {(['f1', 'f2', 'f3'] as const).map((port, i) => values[port] === undefined ? null :
                    <g key={port}>
                        <line x1={x(values[port]!)} x2={x(values[port]!)} y1={8 + i * 4} y2="34"
                            stroke={i === 2 ? secondary : accent} strokeWidth="3" strokeLinecap="round" />
                        <text x={x(values[port]!)} y={6 + i * 4} textAnchor="middle" fill="currentColor" fontSize="8">{i + 1}</text>
                    </g>)}
            </svg>
            <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 10, opacity: .7 }}>
                {(['f1', 'f2', 'f3'] as const).map((port, i) => <span key={port}>
                    F{i + 1} {values[port] === undefined ? '—' : Math.round(values[port]!)} Hz
                </span>)}
            </div>
        </div>
    );
}
