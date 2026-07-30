// SuprClackDisplay: three reduction bars — Clack, Scrape, Gate.
//
// This pedal has three independent things that can be turning your signal
// down, and the only question you ever ask of it is WHICH ONE IS ACTING.
// A single summed meter cannot answer that, and it is the answer that
// tells you which knob to move: a bar pinned on Clack when you play
// cleanly means Sense is too strict; Scrape moving on every note means
// the sieve is finding fault with the instrument rather than with the
// noise; Gate chattering means Thresh is in the wrong place.
//
//   Clack   ◄────────
//   Scrape  ◄──
//   Gate    ◄─────
//
// All three fill leftward in the cool colour, because all three are cuts
// — the house language reserves supr orange for gain ADDED, and nothing
// here ever adds any. One shared 24 dB span so the three are directly
// comparable; the expander can exceed it, and a bar that is simply full
// is the correct reading of "closed".
//
// All three ports are peak-held in the DSP (120 ms decay) precisely so a
// display polling at 30 Hz cannot miss the few-millisecond clack events.
//
// MIT license, (c) 2026 SuprDuprNatural.

import { Component } from 'react';
import { Theme } from '@mui/material/styles';
import WithStyles from './WithStyles';
import { createStyles } from './WithStyles';
import { withStyles } from "tss-react/mui";
import { MonitorPortHandle, PiPedalModel, State, PiPedalModelFactory } from "./PiPedalModel";
import { isDarkMode } from './DarkMode';

const styles = (theme: Theme) => createStyles({});

const LABEL_W = 46;
// Wide enough to carry the header's share of a four-knob column, but
// never wider than the columns themselves — a header that overhangs
// leaves the columns' top walls drawing a rule that stops short of the
// unit's edges, which reads as a broken line.
const BAR_W = 244;
const BAR_H = 8;
const ROW_GAP = 5;
const SPAN_DB = 24; // full deflection, shared by all three

const RATE = 1.0 / 30;

const ROWS: { port: string; label: string }[] = [
    { port: "clackgr", label: "CLACK" },
    { port: "scrapegr", label: "SCRAPE" },
    { port: "expgr", label: "GATE" },
];

interface SuprClackDisplayProps extends WithStyles<typeof styles> {
    instanceId: number;
    // read by PluginControlView: opt out of the fixed-height control slot
    tallControl?: boolean;
}

interface SuprClackDisplayState {
    db: number[];
}

const SuprClackDisplay =
    withStyles(
        class extends Component<SuprClackDisplayProps, SuprClackDisplayState> {
            model: PiPedalModel;

            constructor(props: SuprClackDisplayProps) {
                super(props);
                this.model = PiPedalModelFactory.getInstance();
                this.state = { db: ROWS.map(() => 0) };
                this.onStateChanged = this.onStateChanged.bind(this);
            }

            monitorHandles: MonitorPortHandle[] = [];
            subscribedInstanceId: number = -1;

            addSubscriptions() {
                this.subscribedInstanceId = this.props.instanceId;
                ROWS.forEach((row, i) => {
                    this.monitorHandles.push(
                        this.model.monitorPort(this.props.instanceId, row.port, RATE,
                            (value: number) => {
                                this.setState((s) => {
                                    const db = s.db.slice();
                                    db[i] = value;
                                    return { db: db };
                                });
                            }));
                });
            }
            removeSubscriptions() {
                for (const h of this.monitorHandles)
                    this.model.unmonitorPort(h);
                this.monitorHandles = [];
                this.subscribedInstanceId = -1;
            }
            // Re-subscribe on EVERY arrival at Ready, unconditionally: the
            // model reaches Ready again after any websocket reconnect and
            // the server-side subscriptions do not survive it. Guarding on
            // "has the instanceId changed?" leaves the meters frozen for
            // the rest of the session while the knobs carry on working.
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

            render() {
                const dark = isDarkMode();
                const cut = dark ? "#6fa7d8" : "#3773aa";
                const track = dark ? "rgba(255,255,255,0.07)" : "rgba(0,0,0,0.07)";
                const tick = dark ? "rgba(255,255,255,0.16)" : "rgba(0,0,0,0.16)";
                const text = dark ? "rgba(255,255,255,0.66)" : "rgba(0,0,0,0.66)";

                return (
                    <div style={{
                        display: "flex", flexFlow: "column nowrap",
                        gap: ROW_GAP
                    }}>
                        {ROWS.map((row, i) => {
                            // The ports report reduction as <= 0 dB.
                            const amt = Math.min(-this.state.db[i], SPAN_DB);
                            const fill = Math.max(amt, 0) / SPAN_DB * BAR_W;
                            return (
                                <div key={row.port} style={{
                                    display: "flex", flexFlow: "row nowrap",
                                    alignItems: "center"
                                }}>
                                    <div style={{
                                        width: LABEL_W, flex: "0 0 auto",
                                        fontSize: "0.58rem", letterSpacing: 0.5,
                                        color: text, textAlign: "right",
                                        paddingRight: 6, userSelect: "none"
                                    }}>{row.label}</div>
                                    <div style={{
                                        position: "relative", width: BAR_W,
                                        height: BAR_H, borderRadius: 2,
                                        overflow: "hidden", background: track
                                    }}>
                                        {/* half-scale reference */}
                                        <div style={{
                                            position: "absolute", left: BAR_W / 2,
                                            top: 0, width: 1, height: BAR_H,
                                            background: tick
                                        }} />
                                        {/* fills leftward: reduction grows away
                                            from the zero at the right edge */}
                                        <div style={{
                                            position: "absolute", top: 0, right: 0,
                                            width: fill, height: BAR_H,
                                            background: cut,
                                            transition: "width 90ms linear"
                                        }} />
                                    </div>
                                </div>
                            );
                        })}
                    </div>
                );
            }
        },
        styles);

export default SuprClackDisplay;
