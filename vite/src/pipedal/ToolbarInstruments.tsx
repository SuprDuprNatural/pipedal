// The built-in tuner follows the main input, regardless of the current pedalboard.
// MIT license, (c) 2026 SuprPedals contributors.
import { useEffect, useState } from 'react';
import Box from '@mui/material/Box';
import UndoIcon from '@mui/icons-material/Undo';
import RedoIcon from '@mui/icons-material/Redo';
import IconButtonEx from './IconButtonEx';
import useMediaQuery from '@mui/material/useMediaQuery';
import { PiPedalModelFactory, State, type TunerFrame } from './PiPedalModel';
import OutputMeter from './OutputMeter';
import './OutputMeter.css';

const EMPTY: TunerFrame = { frequency: 0, note: -1, cents: 0, confidence: 0, strobePhase: 0 };
const NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const GLYPHS: Record<string, string[]> = {
    A: ['01110', '10001', '10001', '11111', '10001', '10001', '10001'],
    B: ['11110', '10001', '10001', '11110', '10001', '10001', '11110'],
    C: ['01111', '10000', '10000', '10000', '10000', '10000', '01111'],
    D: ['11110', '10001', '10001', '10001', '10001', '10001', '11110'],
    E: ['11111', '10000', '10000', '11110', '10000', '10000', '11111'],
    F: ['11111', '10000', '10000', '11110', '10000', '10000', '10000'],
    G: ['01111', '10000', '10000', '10111', '10001', '10001', '01111'],
};
const clamp = (n: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, n));

function TopTuner() {
    const [frame, setFrame] = useState<TunerFrame>(EMPTY);
    useEffect(() => {
        const model = PiPedalModelFactory.getInstance();
        let generation = 0;
        let timer: number | undefined;
        const onState = (state: State) => {
            const current = ++generation;
            window.clearTimeout(timer);
            setFrame(EMPTY);
            if (state !== State.Ready) return;
            const poll = async () => {
                try {
                    const next = await model.getTunerFrame();
                    if (generation !== current) return;
                    setFrame(next);
                    timer = window.setTimeout(poll, 33);
                } catch {
                    // A reconnect will re-enter Ready and start a new poll loop.
                    if (generation === current) setFrame(EMPTY);
                }
            };
            poll();
        };
        model.state.addOnChangedHandler(onState);
        return () => {
            ++generation;
            window.clearTimeout(timer);
            model.state.removeOnChangedHandler(onState);
        };
    }, []);

    const locked = frame.note >= 0 && frame.confidence > .55;
    const name = locked ? NOTES[((frame.note % 12) + 12) % 12] : '';
    const cents = clamp(frame.cents, -50, 50);
    const inTune = locked && Math.abs(cents) <= .8;
    const wavePeriod = locked ? clamp(550 / clamp(frame.frequency, 27.5, 220), 2.8, 15) : 10;
    const phaseDots = frame.strobePhase * wavePeriod * 2;
    const needleX = 134 + (inTune ? 0 : cents / 50 * 86);
    const description = locked
        ? `${name}, ${cents >= 0 ? 'plus' : 'minus'} ${Math.abs(cents).toFixed(1)} cents`
        : 'Tuner waiting for a note';
    return <div className="supr-top-tuner" role="img" aria-label={description} title={description}>
        <svg viewBox="0 0 232 36" width="232" height="36" aria-hidden="true">
            <defs>
                <filter id="toolbarTunerGlow" x="-30%" y="-80%" width="160%" height="260%">
                    <feGaussianBlur in="SourceGraphic" stdDeviation=".8" result="blur" />
                    <feComponentTransfer in="blur" result="halo"><feFuncA type="linear" slope=".65" /></feComponentTransfer>
                    <feMerge><feMergeNode in="halo" /><feMergeNode in="SourceGraphic" /></feMerge>
                </filter>
            </defs>
            <path d="M38 6V30" stroke="white" strokeOpacity=".1" />
            {locked ? <g filter="url(#toolbarTunerGlow)" fill="#ff3155">
                {GLYPHS[name[0]].flatMap((row, y) => Array.from(row).map((pixel, x) =>
                    pixel === '1' ? <circle key={`${y}-${x}`} cx={(name.length > 1 ? 8 : 13) + x * 3} cy={9 + y * 3}
                        r="1" /> : null))}
                {name.length > 1 && <path d="M27 10L25 26M32 10L30 26M23 16H34M22 21H33"
                    fill="none" stroke="#ff3155" strokeWidth="1" />}
            </g> : <text x="18" y="24" textAnchor="middle" fill="#41664a" fontSize="16">—</text>}
            <g fill="#34493a" opacity=".8">
                {[8, 18, 28].map(y => <circle key={y} cx="134" cy={y} r="1.5" />)}
            </g>
            <g filter={locked ? 'url(#toolbarTunerGlow)' : undefined}>
                {Array.from({ length: 41 }, (_, i) => {
                    const wave = .5 + .5 * Math.cos(2 * Math.PI * (i - phaseDots) / wavePeriod);
                    return <circle key={i} cx={48 + i * 4.3} cy="18" r="1.9"
                        fill={locked ? '#55f65c' : '#345b3a'}
                        opacity={locked ? .07 + .93 * Math.pow(wave, 3) : .8} />;
                })}
            </g>
            {locked && <g filter="url(#toolbarTunerGlow)" fill={inTune ? '#55f65c' : '#ff3155'}>
                {[8, 18, 28].map(y => <circle key={y} cx={needleX} cy={y} r="2" />)}
            </g>}
        </svg>
    </div>;
}

export default function ToolbarInstruments() {
    const showMeter = useMediaQuery('(min-width: 901px)');
    const showTuner = useMediaQuery('(min-width: 1121px)');
    const model = PiPedalModelFactory.getInstance();
    const [history, setHistory] = useState(model.effectPresetHistory.get());
    useEffect(() => {
        model.effectPresetHistory.addOnChangedHandler(setHistory);
        return () => model.effectPresetHistory.removeOnChangedHandler(setHistory);
    }, [model]);
    const action = history.canRedo ? 'Redo' : 'Undo';
    return <>
        {(history.canUndo || history.canRedo) && <Box sx={{
            display: 'flex', alignItems: 'center', flex: '0 1 auto', minWidth: 36,
            maxWidth: { xs: 140, sm: 220 }, height: 36, ml: 1, mr: showMeter ? 0 : 1,
            bgcolor: '#25282b', color: '#e0e2e4', border: '1px solid #ffffff1f', borderRadius: '5px',
        }}>
            <Box component="span" role="status" title={history.label} sx={{
                pl: 1.25, minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis',
                whiteSpace: 'nowrap', fontSize: 12,
            }}>{history.label}</Box>
            <IconButtonEx color="inherit" size="small" sx={{ flex: '0 0 34px', height: 34 }}
                aria-label={`${action} ${history.label}`}
                tooltip={`${action} ${history.label} (Ctrl/⌘ ${history.canRedo ? 'Shift Z' : 'Z'})`}
                disabled={history.busy}
                onClick={() => { void (history.canRedo ? model.redoEffectPreset() : model.undoEffectPreset()); }}>
                {history.canRedo ? <RedoIcon fontSize="small" /> : <UndoIcon fontSize="small" />}
            </IconButtonEx>
        </Box>}
        {showMeter && <div className="supr-toolbar-instruments">
            {showTuner && <TopTuner />}
            <OutputMeter />
        </div>}
    </>;
}
