// SuprTunerDisplay: the compact display-only face for the Supr bass tuner.
//
// Its five scalar inputs are the display contract for the plugin-backed web
// face. PiPedal's passive OLED tuner has its own main-input analyser.
//
// MIT license, (c) 2026 SuprPedals contributors.

import React, { Component } from 'react';
import { MonitorPortHandle, PiPedalModel, PiPedalModelFactory, State } from "./PiPedalModel";

interface SuprTunerDisplayProps {
    instanceId: number;
    tallControl?: boolean;
}

interface SuprTunerDisplayState {
    frequency: number;
    note: number;
    cents: number;
    confidence: number;
    phase: number;
}

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"];

const PIXEL_GLYPHS: Record<string, string[]> = {
    A: ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    B: ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    C: ["01111", "10000", "10000", "10000", "10000", "10000", "01111"],
    D: ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    E: ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    F: ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    G: ["01111", "10000", "10000", "10111", "10001", "10001", "01111"],
};

function clamp(value: number, minimum: number, maximum: number): number {
    return Math.max(minimum, Math.min(maximum, value));
}

export default class SuprTunerDisplay
    extends Component<SuprTunerDisplayProps, SuprTunerDisplayState> {
    private model: PiPedalModel;
    private monitorHandles: MonitorPortHandle[] = [];
    private subscribedInstanceId = -1;
    private lastWrappedPhase?: number;
    private unwrappedPhase = 0;

    constructor(props: SuprTunerDisplayProps) {
        super(props);
        this.model = PiPedalModelFactory.getInstance();
        this.state = {
            frequency: 0,
            note: -1,
            cents: 0,
            confidence: 0,
            phase: 0,
        };
        this.onModelStateChanged = this.onModelStateChanged.bind(this);
    }

    private subscribe() {
        this.subscribedInstanceId = this.props.instanceId;
        const monitor = (port: string, interval: number,
            update: (value: number) => void) => {
            this.monitorHandles.push(this.model.monitorPort(
                this.props.instanceId, port, interval, update));
        };
        monitor("frequency", 1 / 30,
            (frequency) => this.setState({ frequency }));
        monitor("note", 1 / 20,
            (note) => this.setState({ note: Math.round(note) }));
        monitor("cents", 1 / 30,
            (cents) => this.setState({ cents }));
        monitor("confidence", 1 / 15,
            (confidence) => this.setState({ confidence }));
        monitor("strobe", 1 / 30, (wrapped) => {
            if (this.lastWrappedPhase === undefined) {
                this.lastWrappedPhase = wrapped;
                this.unwrappedPhase = wrapped;
            } else {
                let delta = wrapped - this.lastWrappedPhase;
                if (delta > 0.5) delta -= 1;
                if (delta < -0.5) delta += 1;
                this.unwrappedPhase += delta;
                this.lastWrappedPhase = wrapped;
            }
            this.setState({ phase: this.unwrappedPhase });
        });
    }

    private unsubscribe() {
        this.subscribedInstanceId = -1;
        for (const handle of this.monitorHandles)
            this.model.unmonitorPort(handle);
        this.monitorHandles = [];
        this.lastWrappedPhase = undefined;
        this.unwrappedPhase = 0;
    }

    private onModelStateChanged(state: State) {
        if (state === State.Ready) {
            this.unsubscribe();
            this.subscribe();
        }
    }

    componentDidMount() {
        this.model.state.addOnChangedHandler(this.onModelStateChanged);
        this.subscribe();
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
    }

    private renderGlyph(glyph: string, x: number, y: number,
        pitch: number, color: string, prefix: string): React.ReactNode {
        const rows = PIXEL_GLYPHS[glyph];
        if (!rows) return null;
        const pixels: React.ReactNode[] = [];
        rows.forEach((row, rowIndex) => {
            Array.from(row).forEach((lit, columnIndex) => {
                if (lit === "1") {
                    pixels.push(
                        <circle key={`${prefix}-${rowIndex}-${columnIndex}`}
                            cx={x + columnIndex * pitch}
                            cy={y + rowIndex * pitch}
                            r={pitch * 0.33} fill={color} />);
                }
            });
        });
        return pixels;
    }

    private renderSharp(x: number, y: number, color: string): React.ReactNode {
        return (
            <path
                d={`M ${x + 5.5} ${y} L ${x + 4} ${y + 28}
                    M ${x + 13} ${y} L ${x + 11.5} ${y + 28}
                    M ${x} ${y + 9} L ${x + 17} ${y + 9}
                    M ${x - 0.5} ${y + 19} L ${x + 16.5} ${y + 19}`}
                fill="none" stroke={color} strokeWidth="2.4"
                strokeLinecap="square" strokeLinejoin="miter" />
        );
    }

    render() {
        const locked = this.state.note >= 0 && this.state.confidence > 0.55;
        const cents = clamp(this.state.cents, -50, 50);
        const inTune = locked && Math.abs(cents) <= 0.8;
        const width = 232;
        const height = 132;
        const laneStart = 12;
        const laneEnd = width - 12;
        const laneCenter = width / 2;
        const noteName = locked
            ? NOTE_NAMES[(this.state.note % 12 + 12) % 12]
            : "";
        const noteLetter = noteName.charAt(0);
        const sharp = noteName.length > 1;
        const noteX = laneCenter - (sharp ? 26 : 14);
        const sharpX = noteX + 39;
        const noteColor = "#ff3155";
        const green = "#55f65c";
        const red = "#ff3155";
        const needleX = inTune
            ? laneCenter
            : laneCenter + cents / 50 * (laneEnd - laneCenter);
        const needleColor = inTune ? green : red;
        const svgId = `suprTuner${this.props.instanceId}`;

        const laneDots: React.ReactNode[] = [];
        const dotCount = 41;
        const dotSpacing = (laneEnd - laneStart) / (dotCount - 1);
        // A low bass note produces long, broad bands; each octave up halves
        // their spatial wavelength. Motion still comes solely from strobe phase.
        const wavePeriodDots = locked
            ? clamp(10 * 55 / clamp(this.state.frequency, 27.5, 220), 2.8, 15)
            : 10;
        const phaseDots = this.state.phase * wavePeriodDots * 2;
        for (let i = 0; i < dotCount; ++i) {
            const wave = 0.5 + 0.5 * Math.cos(
                2 * Math.PI * (i - phaseDots) / wavePeriodDots);
            const opacity = locked ? 0.14 + 0.86 * Math.pow(wave, 3) : 1;
            laneDots.push(
                <circle key={i} cx={laneStart + i * dotSpacing} cy="25"
                    r="1.9" fill={locked ? green : "#172018"}
                    opacity={opacity} />);
        }

        return (
            <div style={{
                width, height, position: "relative",
                marginBottom: 10,
                borderRadius: 6, overflow: "hidden",
                boxShadow: "5px 5px 6px rgba(0,0,0,0.8) inset",
                background: "#050705"
            }}>
                <svg viewBox={`0 0 ${width} ${height}`} width={width} height={height}
                    role="img"
                    aria-label={locked
                        ? `${noteName}, ${cents >= 0 ? "plus " : "minus "}${Math.abs(cents).toFixed(1)} cents`
                        : "Tuner waiting for a note"}
                    style={{
                        display: "block",
                        fontFamily: "arial,roboto,helvetica,sans"
                    }}>
                    <defs>
                        <filter id={`${svgId}GreenGlow`} x="-30%" y="-80%" width="160%" height="260%">
                            <feGaussianBlur stdDeviation="1.8" result="blur" />
                            <feMerge>
                                <feMergeNode in="blur" />
                                <feMergeNode in="SourceGraphic" />
                            </feMerge>
                        </filter>
                        <filter id={`${svgId}RedGlow`} x="-30%" y="-30%" width="160%" height="160%">
                            <feGaussianBlur stdDeviation="1.5" result="blur" />
                            <feMerge>
                                <feMergeNode in="blur" />
                                <feMergeNode in="SourceGraphic" />
                            </feMerge>
                        </filter>
                    </defs>

                    <g filter={locked ? `url(#${svgId}GreenGlow)` : undefined}>
                        {laneDots}
                    </g>

                    <g fill="#314331" opacity=".55">
                        <circle cx={laneCenter} cy="15" r="2.2" />
                        <circle cx={laneCenter} cy="25" r="2.2" />
                        <circle cx={laneCenter} cy="35" r="2.2" />
                    </g>

                    {locked &&
                        <g fill={needleColor}
                            filter={`url(#${inTune
                                ? `${svgId}GreenGlow`
                                : `${svgId}RedGlow`})`}>
                            <circle cx={needleX} cy="15" r="2.5" />
                            <circle cx={needleX} cy="25" r="2.5" />
                            <circle cx={needleX} cy="35" r="2.5" />
                        </g>}

                    {locked &&
                        <g filter={`url(#${svgId}RedGlow)`}>
                            {this.renderGlyph(noteLetter, noteX, 59, 7,
                                noteColor, "note")}
                            {sharp && this.renderSharp(sharpX, 65, noteColor)}
                        </g>}

                    <text x={laneStart} y="122" textAnchor="start"
                        fill={locked ? "#bdc5bd" : "#303630"}
                        fontSize="11" fontWeight="400"
                        style={{ fontVariantNumeric: "tabular-nums" }}>
                        {locked ? `${this.state.frequency.toFixed(2)} Hz` : "—"}
                    </text>

                    <text x={laneEnd} y="122" textAnchor="end"
                        fill={locked ? "#bdc5bd" : "#303630"}
                        fontSize="11" fontWeight="400"
                        style={{ fontVariantNumeric: "tabular-nums" }}>
                        {locked
                            ? `${cents >= 0 ? "+" : "-"}${(Math.abs(cents) / 100).toFixed(2)}`
                            : "—"}
                    </text>
                </svg>
            </div>
        );
    }
}
