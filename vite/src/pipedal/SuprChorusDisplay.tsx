// SuprChorusDisplay: the modulation display for SuprChorus.
//
// One picture, no labels. A signal line runs left to right across a log
// frequency axis. Below the Low crossover it is a single still line; above it
// the line fans into one strand per voice, and the strands breathe with the
// plugin's own LFO. Everything you can set is visible in it:
//
//   where it pinches   Low          how far it opens   Depth
//   how many strands   Voices       how fast it moves  Rate
//   how bright it is   Mix          where it dims out  Tone
//
// The fan's envelope is the crossover's actual high-band magnitude and the
// dimming is the actual voice low-pass, both taken from the same expressions
// as src/ChorusDsp.h — so the pinch really is where the plugin stops
// modulating, not an artist's impression of it.
//
// Vertical position is each tap's delay, measured against the group's mean —
// the quantity that actually sets the comb spacing between voices. So the
// strands sit at their real base separation before any modulation, converge
// to the still line below the crossover because nothing down there is
// delayed at all, and cross over one another once Depth exceeds the spacing,
// which is exactly when the voices start trading places in the DSP.
//
// The strands move as a group rather than as a travelling wave, because that
// is what the DSP does: one LFO per voice displaces every frequency in the
// band at once. Motion is advanced locally from Rate and nudged toward the
// monitored lfo port, which arrives far too slowly to animate from directly.
//
// MIT license, (c) 2026 SuprDuprNatural.

import React, { Component } from 'react';
import { MonitorPortHandle, PiPedalModel, PiPedalModelFactory, State } from "./PiPedalModel";
import { isDarkMode } from './DarkMode';

interface SuprChorusDisplayProps {
    instanceId: number;
    // control values seeded from the pedalboard model (refreshed per render)
    controls: { [symbol: string]: number };
    // read by PluginControlView: opt out of the fixed-height control slot
    tallControl?: boolean;
}

interface SuprChorusDisplayState {
    live: { [symbol: string]: number };
    phase: number;
}

const PLOT_W = 248;
const PLOT_H = 116;
const FMIN = 20;
const FMAX = 12500;
const N_POINTS = 96;
const N_STOPS = 24;

// All four match src/ChorusDsp.h: kBaseMs is common to every tap and so
// cancels out of a centred view, kSpreadMs is the gap between them.
const SPREAD_MS = 2.9;
const MAX_DEPTH_MS = 8.0;
const MAX_DETUNE = 0.029;
// Scaled so the worst case — outer voice at full depth — just fits, with a
// margin at the screen edge.
const PX_PER_MS = 40 / (SPREAD_MS + MAX_DEPTH_MS);

const V_GRID = [100, 1000, 10000];
const MONITOR_SYMBOLS = ["lfo", "rate", "depth", "voices", "low", "tone", "mix"];

const DEFAULTS: { [symbol: string]: number } = {
    rate: 0.6, depth: 3.0, voices: 2, low: 120, tone: 6000, mix: 0.35,
};

// Linkwitz-Riley 4th order magnitudes — two cascaded Butterworth sections, so
// |LP| = 1/(1+r^4) and |HP| = r^4/(1+r^4), and the two sum to 1 everywhere.
function lr4(f: number, fc: number): { lp: number; hp: number } {
    const r4 = Math.pow(f / Math.max(fc, 1), 4);
    return { lp: 1 / (1 + r4), hp: r4 / (1 + r4) };
}

// Butterworth 2-pole low-pass magnitude (the per-voice Tone filter).
function lp2(f: number, fc: number): number {
    return 1 / Math.sqrt(1 + Math.pow(f / Math.max(fc, 1), 4));
}

export default class SuprChorusDisplay
    extends Component<SuprChorusDisplayProps, SuprChorusDisplayState> {
    private model: PiPedalModel;
    private monitorHandles: MonitorPortHandle[] = [];
    private subscribedInstanceId = -1;
    private raf = 0;
    private lastFrame = 0;
    private phase = 0;
    private reportedPhase?: number;

    constructor(props: SuprChorusDisplayProps) {
        super(props);
        this.model = PiPedalModelFactory.getInstance();
        this.state = { live: {}, phase: 0 };
        this.onModelStateChanged = this.onModelStateChanged.bind(this);
        this.tick = this.tick.bind(this);
    }

    private subscribe() {
        this.subscribedInstanceId = this.props.instanceId;
        for (const symbol of MONITOR_SYMBOLS) {
            this.monitorHandles.push(this.model.monitorPort(
                this.props.instanceId, symbol, 1 / 15, (value: number) => {
                    if (symbol === "lfo") {
                        this.reportedPhase = value;
                    } else {
                        this.setState((s) => ({
                            live: { ...s.live, [symbol]: value }
                        }));
                    }
                }));
        }
    }

    private unsubscribe() {
        this.subscribedInstanceId = -1;
        for (const handle of this.monitorHandles)
            this.model.unmonitorPort(handle);
        this.monitorHandles = [];
        this.reportedPhase = undefined;
    }

    private onModelStateChanged(state: State) {
        if (state === State.Ready) {
            this.unsubscribe();
            this.subscribe();
        }
    }

    // ~30 fps is plenty for an LFO that tops out at 8 Hz, and the browser on
    // a Pi has better things to do than repaint this 60 times a second.
    private tick(now: number) {
        this.raf = window.requestAnimationFrame(this.tick);
        const dt = (now - this.lastFrame) / 1000;
        if (dt < 1 / 30)
            return;
        this.lastFrame = now;

        this.phase += this.value("rate") * Math.min(dt, 0.2);
        this.phase -= Math.floor(this.phase);

        // Pull gently toward what the plugin actually reports. The port
        // arrives at 15 Hz, so it can only ever be a correction, never the
        // clock — a hard set would stutter at any interesting rate.
        if (this.reportedPhase !== undefined) {
            let d = this.reportedPhase - this.phase;
            if (d > 0.5) d -= 1;
            if (d < -0.5) d += 1;
            this.phase += d * 0.1;
            this.phase -= Math.floor(this.phase);
        }
        this.setState({ phase: this.phase });
    }

    componentDidMount() {
        this.model.state.addOnChangedHandler(this.onModelStateChanged);
        this.subscribe();
        this.lastFrame = performance.now();
        this.raf = window.requestAnimationFrame(this.tick);
    }

    componentDidUpdate() {
        if (this.subscribedInstanceId !== this.props.instanceId) {
            this.unsubscribe();
            this.subscribe();
        }
    }

    componentWillUnmount() {
        this.model.state.removeOnChangedHandler(this.onModelStateChanged);
        this.unsubscribe();
        window.cancelAnimationFrame(this.raf);
    }

    private value(symbol: string): number {
        const live = this.state.live[symbol];
        if (live !== undefined)
            return live;
        const seeded = this.props.controls[symbol];
        return seeded !== undefined ? seeded : DEFAULTS[symbol];
    }

    private xOf(f: number): number {
        return PLOT_W * Math.log(f / FMIN) / Math.log(FMAX / FMIN);
    }
    private fOf(t: number): number {
        return FMIN * Math.pow(FMAX / FMIN, t);
    }

    // Mirrors ChorusDsp::effectiveDepthMs — depth and rate multiply into
    // pitch, so the swing is capped. The display shows what you get, not what
    // the knob says.
    private effectiveDepth(): number {
        const rate = Math.max(this.value("rate"), 0.02);
        return Math.min(this.value("depth"),
            1000 * MAX_DETUNE / (2 * Math.PI * rate));
    }

    render() {
        const dark = isDarkMode();
        const grid = dark ? "#3a3a3a" : "#e2e2e2";
        const accent = dark ? "#e88f4d" : "#d2691e";  // supr orange
        const second = dark ? "#6fa7d8" : "#3773aa";  // sub blue

        const low = this.value("low");
        const tone = this.value("tone");
        const mix = this.value("mix");
        const voices = Math.max(1, Math.min(3, Math.round(this.value("voices"))));
        const depth = this.effectiveDepth();
        const mid = PLOT_H / 2;
        const id = `suprChorus${this.props.instanceId}`;

        // One strand per voice, at that tap's delay relative to the group
        // mean, scaled by how much of the band the crossover has let through.
        const strands: React.ReactNode[] = [];
        const centre = SPREAD_MS * (voices - 1) / 2;
        for (let v = 0; v < voices; ++v) {
            const delayMs = SPREAD_MS * v - centre
                + depth * Math.sin(2 * Math.PI * (this.state.phase + v / voices));
            const px = delayMs * PX_PER_MS;
            let d = "";
            for (let i = 0; i <= N_POINTS; ++i) {
                const t = i / N_POINTS;
                const y = mid + lr4(this.fOf(t), low).hp * px;
                d += (i === 0 ? "M" : "L")
                    + (t * PLOT_W).toFixed(1) + " " + y.toFixed(2);
            }
            strands.push(
                <path key={v} d={d} fill="none" stroke={`url(#${id}Fan)`}
                    strokeWidth={1.8} strokeLinecap="round" />);
        }

        // Both strokes fade along the axis rather than at a hard edge: the
        // fan appears as the crossover hands over and dims where Tone rolls
        // the voices off, and the still line fades out underneath it.
        const fanStops: React.ReactNode[] = [];
        const lineStops: React.ReactNode[] = [];
        for (let i = 0; i <= N_STOPS; ++i) {
            const t = i / N_STOPS;
            const f = this.fOf(t);
            const b = lr4(f, low);
            fanStops.push(
                <stop key={i} offset={t}
                    stopColor={accent}
                    stopOpacity={b.hp * lp2(f, tone) * (0.15 + 0.85 * mix)} />);
            lineStops.push(
                <stop key={i} offset={t}
                    stopColor={second} stopOpacity={b.lp} />);
        }

        let lineD = "";
        for (let i = 0; i <= N_POINTS; ++i) {
            const t = i / N_POINTS;
            lineD += (i === 0 ? "M" : "L")
                + (t * PLOT_W).toFixed(1) + " " + mid.toFixed(2);
        }

        return (
            <div style={{
                width: PLOT_W, height: PLOT_H, position: "relative",
                borderRadius: 4, overflow: "hidden",
                // Top-left, matching SuprTunerDisplay. The display was
                // rotated from the orientation the light theme's original
                // 1px/5px offset was drawn for, which left its shadow
                // sitting on the top edge alone.
                boxShadow: dark
                    ? "5px 5px 6px rgba(0,0,0,0.8) inset"
                    : "5px 5px 6px #888 inset",
                background: dark ? "rgba(255,255,255,0.05)" : "rgba(0,0,0,0.02)"
            }}>
                <svg viewBox={`0 0 ${PLOT_W} ${PLOT_H}`}
                    width={PLOT_W} height={PLOT_H} role="img"
                    aria-label={`Chorus: ${voices} voice${voices === 1 ? "" : "s"}`
                        + ` above ${Math.round(low)} hertz`}
                    style={{ position: "absolute" }}>
                    <defs>
                        <linearGradient id={`${id}Fan`} gradientUnits="userSpaceOnUse"
                            x1={0} y1={0} x2={PLOT_W} y2={0}>
                            {fanStops}
                        </linearGradient>
                        <linearGradient id={`${id}Line`} gradientUnits="userSpaceOnUse"
                            x1={0} y1={0} x2={PLOT_W} y2={0}>
                            {lineStops}
                        </linearGradient>
                    </defs>

                    {V_GRID.map((f) => (
                        <line key={f} x1={this.xOf(f)} y1={0}
                            x2={this.xOf(f)} y2={PLOT_H}
                            stroke={grid} strokeWidth={1} />
                    ))}
                    <line x1={this.xOf(low)} y1={8} x2={this.xOf(low)} y2={PLOT_H - 8}
                        stroke={second} strokeWidth={1} strokeDasharray="2 4"
                        opacity={0.45} />

                    <path d={lineD} fill="none" stroke={`url(#${id}Line)`}
                        strokeWidth={2} strokeLinecap="round" />
                    {strands}
                </svg>
            </div>
        );
    }
}
