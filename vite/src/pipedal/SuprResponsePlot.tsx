// SuprResponsePlot: a live frequency-response module for Supr pedals, in the
// spirit of the TooB Parametric EQ's graph — a compact "rack module" that
// flows inline with the port-group sections.
//
// The curves are computed from the same transfer functions as the plugin DSP
// (Simper ZDF SVF / RBJ biquad), so what you see is what the filter does.
// Live values (the envelope-swept cutoff, the tracked note) arrive over
// monitorPort from hidden output control ports; input controls are monitored
// too so the curve follows dial drags in real time. If the installed plugin
// predates the live ports, everything falls back to the static control
// values — the plot still draws, it just doesn't dance.
//
// MIT license, (c) 2026 SuprDuprNatural.

import React, { Component } from 'react';
import { Theme } from '@mui/material/styles';
import Typography from '@mui/material/Typography';
import WithStyles from './WithStyles';
import { createStyles } from './WithStyles';
import { withStyles } from "tss-react/mui";
import { MonitorPortHandle, PiPedalModel, State, PiPedalModelFactory } from "./PiPedalModel";
import { isDarkMode } from './DarkMode';

const styles = (theme: Theme) => createStyles({});

export type ResponseVariant = "envfilter" | "octaveplus";

interface SuprResponsePlotProps extends WithStyles<typeof styles> {
    instanceId: number;
    variant: ResponseVariant;
    // control values seeded from the pedalboard model (refreshed per render)
    controls: { [symbol: string]: number };
    // read by PluginControlView: opt out of the fixed-height control slot
    tallControl?: boolean;
}

interface SuprResponsePlotState {
    live: { [symbol: string]: number };
}

// ---------------------------------------------------------------------------
// plot geometry: a fixed-size module, sized to sit beside the knob sections
// ---------------------------------------------------------------------------
const FMIN = 20;
const FMAX = 12500;
const DB_MIN = -36;
const DB_MAX = 24;
const N_POINTS = 120;
const PLOT_W = 248;
const PLOT_H = 116;

const V_GRID = [50, 100, 200, 500, 1000, 2000, 5000, 10000];
const V_LABELS: { [f: number]: string } = { 100: "100", 1000: "1k", 10000: "10k" };

// live/monitored symbols per variant. Input controls are monitored so the
// curve tracks dial drags (previews go to the RT engine, not the model).
const MONITOR_SYMBOLS: { [v in ResponseVariant]: string[] } = {
    envfilter: ["fc", "mode", "dir", "cutoff", "range", "res", "blend", "level"],
    octaveplus: ["fc", "note", "samp", "tone", "cutoff", "res", "synthoct"],
};

// ---------------------------------------------------------------------------
// filter math (mirrors src/EnvFilterDsp.h and src/OctaverPlusDsp.h)
// ---------------------------------------------------------------------------
interface Cplx { re: number; im: number; }

function cDiv(a: Cplx, b: Cplx): Cplx {
    const d = b.re * b.re + b.im * b.im;
    return { re: (a.re * b.re + a.im * b.im) / d, im: (a.im * b.re - a.re * b.im) / d };
}

// Simper ZDF SVF response at frequency f (exact discrete response via the
// prewarped analog prototype). q as the DSP uses it: q = 0.5 + res * 9.5.
// Returns all three outputs as the DSP taps them (bp peaks at q, not unity).
function svfResponse(f: number, fc: number, q: number, fs: number):
    { lp: Cplx; bp: Cplx; hp: Cplx } {
    const g = Math.tan(Math.PI * Math.min(Math.max(fc, 20), 0.45 * fs) / fs);
    const k = 1.0 / Math.min(Math.max(q, 0.5), 12.0);
    const wn = Math.tan(Math.PI * Math.min(f, 0.499 * fs) / fs) / g;
    const den: Cplx = { re: 1 - wn * wn, im: k * wn };
    return {
        lp: cDiv({ re: 1, im: 0 }, den),
        bp: cDiv({ re: 0, im: wn }, den),
        hp: cDiv({ re: -wn * wn, im: 0 }, den),
    };
}

// RBJ biquad low-pass response (the OctaverPlus Tone filter, Q = 0.7071).
function biquadLpDb(f: number, fc: number, q: number, fs: number): number {
    const w0 = 2 * Math.PI * Math.min(Math.max(fc, 20), 0.45 * fs) / fs;
    const cw = Math.cos(w0);
    const alpha = Math.sin(w0) / (2 * q);
    const a0 = 1 + alpha;
    const b0 = ((1 - cw) * 0.5) / a0, b1 = (1 - cw) / a0, b2 = b0;
    const a1 = (-2 * cw) / a0, a2 = (1 - alpha) / a0;
    const w = 2 * Math.PI * Math.min(f, 0.499 * fs) / fs;
    const c1 = Math.cos(-w), s1 = Math.sin(-w);
    const c2 = Math.cos(-2 * w), s2 = Math.sin(-2 * w);
    const num: Cplx = { re: b0 + b1 * c1 + b2 * c2, im: b1 * s1 + b2 * s2 };
    const den: Cplx = { re: 1 + a1 * c1 + a2 * c2, im: a1 * s1 + a2 * s2 };
    const h = cDiv(num, den);
    return 20 * Math.log10(Math.max(Math.hypot(h.re, h.im), 1e-6));
}

const SuprResponsePlot =
    withStyles(
        class extends Component<SuprResponsePlotProps, SuprResponsePlotState> {
            model: PiPedalModel;

            constructor(props: SuprResponsePlotProps) {
                super(props);
                this.model = PiPedalModelFactory.getInstance();
                this.state = { live: {} };
                this.onStateChanged = this.onStateChanged.bind(this);
            }

            // -- monitor subscriptions (same pattern as SuprMeterControl) ----
            monitorHandles: MonitorPortHandle[] = [];
            subscribedInstanceId: number = -1;

            addSubscriptions() {
                this.subscribedInstanceId = this.props.instanceId;
                for (let symbol of MONITOR_SYMBOLS[this.props.variant]) {
                    this.monitorHandles.push(
                        this.model.monitorPort(this.props.instanceId, symbol, 1.0 / 30,
                            (value: number) => {
                                this.setState((s) => ({
                                    live: { ...s.live, [symbol]: value }
                                }));
                            }));
                }
            }
            removeSubscriptions() {
                this.subscribedInstanceId = -1;
                for (let handle of this.monitorHandles) {
                    this.model.unmonitorPort(handle);
                }
                this.monitorHandles = [];
            }
            onStateChanged(state: State) {
                if (state === State.Ready) {
                    this.removeSubscriptions();
                    this.addSubscriptions();
                }
            }
            componentDidMount() {
                this.model.state.addOnChangedHandler(this.onStateChanged);
                this.addSubscriptions();
            }
            componentDidUpdate() {
                if (this.subscribedInstanceId !== this.props.instanceId) {
                    this.removeSubscriptions();
                    this.addSubscriptions();
                }
            }
            componentWillUnmount() {
                this.model.state.removeOnChangedHandler(this.onStateChanged);
                this.removeSubscriptions();
            }

            // -- values ------------------------------------------------------
            value(symbol: string, fallback: number): number {
                const live = this.state.live[symbol];
                if (live !== undefined)
                    return live;
                const seeded = this.props.controls[symbol];
                return seeded !== undefined ? seeded : fallback;
            }
            sampleRate(): number {
                const sr = this.model.jackConfiguration.get().sampleRate;
                return sr && sr > 0 ? sr : 48000;
            }

            xOf(f: number): number {
                return PLOT_W * Math.log(f / FMIN) / Math.log(FMAX / FMIN);
            }
            yOf(db: number): number {
                const t = (Math.min(Math.max(db, DB_MIN), DB_MAX) - DB_MIN) / (DB_MAX - DB_MIN);
                return PLOT_H - t * PLOT_H;
            }
            pathOf(dbAt: (f: number) => number): string {
                let d = "";
                for (let i = 0; i <= N_POINTS; ++i) {
                    const f = FMIN * Math.pow(FMAX / FMIN, i / N_POINTS);
                    const x = this.xOf(f);
                    const y = this.yOf(dbAt(f));
                    d += (i === 0 ? "M" : "L") + x.toFixed(1) + " " + y.toFixed(1);
                }
                return d;
            }

            // full EnvFilter response: level * (blend * wet + (1-blend) * dry),
            // dry and wet summed as complex values, like the DSP does.
            envFilterDb(f: number, fc: number): number {
                const fs = this.sampleRate();
                const mode = Math.round(this.value("mode", 0));
                const res = this.value("res", 0.55);
                const blend = this.value("blend", 0.8);
                const level = this.value("level", 1.0);
                const r = svfResponse(f, fc, 0.5 + res * 9.5, fs);
                const wet = mode === 0 ? r.lp : (mode === 1 ? r.bp : r.hp);
                const re = blend * wet.re + (1 - blend);
                const im = blend * wet.im;
                return 20 * Math.log10(Math.max(level * Math.hypot(re, im), 1e-6));
            }

            envFilterRestingFc(): number {
                const cutoff = this.value("cutoff", 120);
                const range = this.value("range", 3);
                const dir = Math.round(this.value("dir", 0));
                // Up rests at the base cutoff; Down rests at the top of the sweep
                const fc = dir === 1 ? cutoff * Math.pow(2, range) : cutoff;
                return Math.min(Math.max(fc, 30), 8000);
            }

            renderEnvFilter(elements: React.ReactNode[], key: number,
                accent: string, faint: string): number {
                const cutoff = this.value("cutoff", 120);
                const range = this.value("range", 3);
                const liveFcRaw = this.state.live["fc"];
                const fcLive = (liveFcRaw !== undefined && liveFcRaw > 0)
                    ? liveFcRaw : this.envFilterRestingFc();

                // sweep band: where the cutoff can travel
                const bandX0 = this.xOf(Math.min(Math.max(cutoff, FMIN), FMAX));
                const bandX1 = this.xOf(Math.min(Math.max(cutoff * Math.pow(2, range), FMIN), FMAX));
                elements.push(
                    <rect key={key++} x={bandX0} y={0} width={Math.max(bandX1 - bandX0, 1)} height={PLOT_H}
                        fill={accent} opacity={0.07} />);

                // resting-position reference curve
                const fcRest = this.envFilterRestingFc();
                elements.push(
                    <path key={key++} d={this.pathOf((f) => this.envFilterDb(f, fcRest))}
                        stroke={faint} strokeWidth="1" strokeDasharray="4 3" fill="none" />);

                // the live curve
                elements.push(
                    <path key={key++} d={this.pathOf((f) => this.envFilterDb(f, fcLive))}
                        stroke={accent} strokeWidth="2" fill="none" strokeLinejoin="round" />);
                return key;
            }

            renderOctavePlus(elements: React.ReactNode[], key: number,
                accent: string, second: string, textColor: string): number {
                const fs = this.sampleRate();
                const res = this.value("res", 0.3);
                const cutoff = this.value("cutoff", 900);
                const tone = this.value("tone", 550);
                const synthoct = Math.round(this.value("synthoct", 0));
                const liveFcRaw = this.state.live["fc"];
                const fcLive = (liveFcRaw !== undefined && liveFcRaw > 0) ? liveFcRaw : cutoff;
                const note = Math.max(this.value("note", 55), 10);
                const samp = Math.min(Math.max(this.value("samp", 0), 0), 1);

                // note markers: the played note, the sub voices, the synth pitch
                const markers: { f: number; label: string; opacity: number; color: string }[] = [
                    { f: note, label: "note", opacity: 0.5, color: textColor },
                    { f: note / 2, label: "-1", opacity: 0.5, color: second },
                    { f: note / 4, label: "-2", opacity: 0.5, color: second },
                    { f: note * Math.pow(2, synthoct), label: "syn", opacity: 0.25 + 0.75 * samp, color: accent },
                ];
                for (let m of markers) {
                    if (m.f < FMIN || m.f > FMAX)
                        continue;
                    const x = this.xOf(m.f);
                    elements.push(
                        <line key={key++} x1={x} y1={10} x2={x} y2={PLOT_H}
                            stroke={m.color} strokeWidth="1" strokeDasharray="2 3"
                            opacity={m.opacity} />);
                    elements.push(
                        <text key={key++} x={x} y={8} fontSize="7" fill={m.color}
                            opacity={m.opacity} textAnchor="middle">{m.label}</text>);
                }

                // Tone: the sub voices' low-pass (RBJ biquad, Q 0.7071)
                elements.push(
                    <path key={key++} d={this.pathOf((f) => biquadLpDb(f, tone, 0.7071, fs))}
                        stroke={second} strokeWidth="1.5" fill="none" strokeLinejoin="round" />);

                // Synth filter: resonant SVF low-pass at the live (envelope-
                // and keytrack-modulated) cutoff
                elements.push(
                    <path key={key++} d={this.pathOf((f) => {
                        const r = svfResponse(f, fcLive, 0.5 + res * 9.5, fs);
                        return 20 * Math.log10(Math.max(Math.hypot(r.lp.re, r.lp.im), 1e-6));
                    })}
                        stroke={accent} strokeWidth="2" fill="none" strokeLinejoin="round" />);

                // legend
                elements.push(
                    <text key={key++} x={PLOT_W - 4} y={12} fontSize="8" fill={accent}
                        textAnchor="end" fontWeight={700}>SYNTH</text>);
                elements.push(
                    <text key={key++} x={PLOT_W - 4} y={22} fontSize="8" fill={second}
                        textAnchor="end" fontWeight={700}>SUB</text>);
                return key;
            }

            render() {
                const dark = isDarkMode();
                const gridColor = dark ? "#3a3a3a" : "#ddd";
                const gridMajor = dark ? "#4a4a4a" : "#ccc";
                const textColor = dark ? "#999" : "#666";
                const accent = dark ? "#e88f4d" : "#d2691e";  // supr orange
                const second = dark ? "#6fa7d8" : "#3773aa";  // sub blue
                const faint = dark ? "#666" : "#aaa";

                let elements: React.ReactNode[] = [];
                let key = 0;

                // horizontal grid (dB)
                for (let db = DB_MIN + 12; db < DB_MAX; db += 12) {
                    const y = this.yOf(db);
                    elements.push(
                        <line key={key++} x1={0} y1={y} x2={PLOT_W} y2={y}
                            stroke={db === 0 ? gridMajor : gridColor}
                            strokeWidth={db === 0 ? 1.5 : 1} />);
                    if (db === 0 || db === -24 || db === 12) {
                        elements.push(
                            <text key={key++} x={2} y={y - 2} fontSize="7"
                                fill={textColor}>{db > 0 ? "+" + db : db}</text>);
                    }
                }
                // vertical grid (frequency decades)
                for (let f of V_GRID) {
                    const x = this.xOf(f);
                    const label = V_LABELS[f];
                    elements.push(
                        <line key={key++} x1={x} y1={0} x2={x} y2={PLOT_H}
                            stroke={label ? gridMajor : gridColor} strokeWidth="1" />);
                    if (label) {
                        elements.push(
                            <text key={key++} x={x + 2} y={PLOT_H - 2} fontSize="7"
                                fill={textColor}>{label}</text>);
                    }
                }

                if (this.props.variant === "envfilter") {
                    key = this.renderEnvFilter(elements, key, accent, faint);
                } else {
                    key = this.renderOctavePlus(elements, key, accent, second, textColor);
                }

                // A module frame styled to match PluginControlView's portGroup
                // boxes (border, radius, floating title chip), so the plot
                // reads as one more section in the rack.
                return (
                    <div style={{
                        position: "relative",
                        // -12 cancels the portgroupControlPadding wrapper's
                        // marginTop so the module tops align with the groups
                        marginLeft: 8, marginRight: 8, marginBottom: 12, marginTop: -12,
                        paddingLeft: 8, paddingRight: 8, paddingTop: 10, paddingBottom: 8,
                        border: "2pt #AAA solid", borderRadius: 8,
                        flex: "0 0 auto"
                    }}>
                        <div style={{
                            position: "absolute", top: -15, marginLeft: 12,
                            paddingLeft: 8, paddingRight: 8,
                            background: dark ? "#222" : "#FFFFFF"
                        }}>
                            <Typography noWrap variant="caption">Response</Typography>
                        </div>
                        <div style={{
                            width: PLOT_W, height: PLOT_H, position: "relative",
                            borderRadius: 4, overflow: "hidden",
                            boxShadow: dark ?
                                "5px 5px 6px rgba(0,0,0,0.8) inset" :
                                "1px 5px 6px #888 inset",
                            background: dark ? "rgba(255,255,255,0.05)" : "rgba(0,0,0,0.02)"
                        }}>
                            <svg viewBox={`0 0 ${PLOT_W} ${PLOT_H}`} width={PLOT_W} height={PLOT_H}
                                style={{ position: "absolute" }}>
                                {elements}
                            </svg>
                        </div>
                    </div>);
            }
        },
        styles
    );

export default SuprResponsePlot;
