// Persistent device-output meter. Its subscription is independent of selection
// and presets; the three-second loudness window lives in AudioHost.
// MIT license, (c) 2026 SuprPedals contributors.
import { useEffect, useState } from 'react';
import { PiPedalModelFactory, State, type VuSubscriptionHandle, type VuUpdateInfo } from './PiPedalModel';
import { Pedalboard } from './Pedalboard';
import './OutputMeter.css';

const FLOOR = -48;
const SILENCE = -120;
const EMPTY = { lufs: undefined as number | undefined, peaks: [SILENCE, SILENCE],
    holds: [SILENCE, SILENCE], stereo: false, clip: false, connected: false };
const db = (amplitude: number) => Number.isFinite(amplitude) && amplitude > 0
    ? Math.max(SILENCE, 20 * Math.log10(amplitude)) : SILENCE;
const position = (value: number) => Math.max(0, Math.min(1, (value - FLOOR) / -FLOOR));

// Small seven-segment numerals, drawn locally so the display never depends on
// a downloaded font. Geometry is deliberately upright and lightly spaced.
const SEGMENTS: Record<string, string> = {
    '0': 'abcdef', '1': 'bc', '2': 'abdeg', '3': 'abcdg', '4': 'bcfg',
    '5': 'acdfg', '6': 'acdefg', '7': 'abc', '8': 'abcdefg', '9': 'abcdfg', '-': 'g',
};
const BARS = [
    [3, 0, 8, 1.6], [11.5, 2, 1.6, 8], [11.5, 12, 1.6, 8],
    [3, 20.5, 8, 1.6], [1, 12, 1.6, 8], [1, 2, 1.6, 8], [3, 10.25, 8, 1.6],
];
function Digits({ value }: { value?: number }) {
    const text = value === undefined ? ' --.-' : value.toFixed(1).padStart(5, ' ');
    let x = 0;
    return <g fill="#e8a36e">{Array.from(text).map((character, index) => {
        const at = x; x += character === '.' ? 5 : 16;
        return <g key={index} transform={`translate(${at} 0)`}>
            {character === '.' ? <rect x="0" y="20.5" width="1.8" height="1.8" rx=".3" /> :
                BARS.map(([bx, by, width, height], segment) =>
                    <rect key={segment} x={bx} y={by} width={width} height={height} rx=".4"
                        opacity={character === ' ' ? 0 : SEGMENTS[character]?.includes('abcdefg'[segment]) ? 1 : .065} />)}
        </g>;
    })}</g>;
}

export default function OutputMeter() {
    const [reading, setReading] = useState(EMPTY);
    useEffect(() => {
        const model = PiPedalModelFactory.getInstance();
        let handle: VuSubscriptionHandle | undefined;
        let generation = 0;
        let lastUpdate = 0;
        let peaks = [SILENCE, SILENCE], holds = [SILENCE, SILENCE], holdUntil = [0, 0];
        let clipUntil = 0;
        const reset = () => {
            lastUpdate = 0; peaks = [SILENCE, SILENCE]; holds = [SILENCE, SILENCE];
            holdUntil = [0, 0]; clipUntil = 0; setReading(EMPTY);
        };
        const onState = (state: State) => {
            ++generation;
            if (handle) model.removeVuSubscription(handle);
            handle = undefined;
            reset();
            if (state !== State.Ready) return;
            const subscribedGeneration = generation;
            handle = model.addVuSubscription(Pedalboard.END_CONTROL_ID, (value: VuUpdateInfo) => {
                if (generation !== subscribedGeneration) return;
                const now = performance.now();
                const elapsed = lastUpdate ? Math.max(0, (now - lastUpdate) / 1000) : 0;
                lastUpdate = now;
                const levels = [db(value.outputMaxValueL), db(value.isStereoOutput ? value.outputMaxValueR : 0)];
                for (let c = 0; c < 2; ++c) {
                    peaks[c] = Math.max(levels[c], peaks[c] - 24 * elapsed);
                    if (levels[c] >= holds[c]) {
                        holds[c] = levels[c]; holdUntil[c] = now + 1200;
                    } else if (now > holdUntil[c]) {
                        holds[c] = Math.max(levels[c], holds[c] - 12 * elapsed);
                    }
                }
                if (levels.some(level => level >= 0)) clipUntil = now + 2000;
                // Missing field means an older host. Never invent LUFS from
                // peak values. -120 is the host's finite silence sentinel.
                const loudness = value.outputLufs;
                setReading({ lufs: typeof loudness === 'number' && Number.isFinite(loudness) && loudness > -100
                    ? Math.max(-99.9, Math.min(99.9, loudness)) : undefined,
                    peaks: [...peaks], holds: [...holds], stereo: value.isStereoOutput,
                    clip: now < clipUntil, connected: true });
            });
        };
        // ObservableProperty immediately delivers the current state.
        model.state.addOnChangedHandler(onState);
        const staleTimer = window.setInterval(() => {
            if (lastUpdate && performance.now() - lastUpdate > 1500) reset();
        }, 250);
        return () => {
            ++generation;
            window.clearInterval(staleTimer);
            model.state.removeOnChangedHandler(onState);
            if (handle) model.removeVuSubscription(handle);
        };
    }, []);

    const peak = Math.max(...reading.holds);
    const peakText = !reading.connected || peak <= SILENCE ? '−∞' : peak.toFixed(1).replace('-', '−');
    const description = !reading.connected ? 'Output meter unavailable' :
        `Output: ${reading.lufs === undefined ? 'silent' : reading.lufs.toFixed(1) + ' LUFS short-term'}, ` +
        `${peakText} dBFS sample peak${reading.clip ? ', clipping' : ''}`;
    return <div className="supr-output-meter" role="img" aria-label={description}
        title="Output · 3-second LUFS · sample peak dBFS">
        <svg viewBox="0 0 310 36" width="310" height="36" aria-hidden="true">
            <g transform="translate(8 6)"><Digits value={reading.lufs} /></g>
            <text x="80" y="24" className="supr-output-unit">LUFS</text>
            <path d="M110 7V29" stroke="white" strokeOpacity=".1" />
            {(reading.stereo ? [0, 1] : [0]).map(channel => {
                const y = reading.stereo ? 9 + channel * 7 : 12;
                return <g key={channel}>
                    {Array.from({ length: 48 }, (_, i) => <rect key={i}
                        x={120 + i * 3} y={y} width="2" height="4" rx=".4"
                        fill={i >= 45 ? '#e77867' : i >= 36 ? '#e8a36e' : '#8ba9a0'}
                        opacity={position(reading.peaks[channel]) * 48 > i ? 1 : .14} />)}
                    {reading.holds[channel] > FLOOR && <rect
                        x={120 + Math.min(47, Math.floor(position(reading.holds[channel]) * 48)) * 3}
                        y={y - 1} width="2" height="6" rx=".4" fill="#f0d3b9" />}
                </g>;
            })}
            {[-48, -24, -12, 0].map(tick => <text key={tick}
                x={120 + position(tick) * 143} y="31" textAnchor={tick === -48 ? 'start' : tick === 0 ? 'end' : 'middle'}
                className="supr-output-scale">{tick === 0 ? '0' : '−' + -tick}</text>)}
            <text x="302" y="17" textAnchor="end" className="supr-output-peak"
                fill={reading.clip ? '#ff8978' : '#e6d9cc'}>{peakText}</text>
            <text x="302" y="30" textAnchor="end" className="supr-output-unit">dBFS</text>
            <rect x="308" y="7" width="2" height="22" rx="1" fill="#ff7864" opacity={reading.clip ? 1 : 0} />
        </svg>
    </div>;
}
