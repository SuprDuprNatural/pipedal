// SuprDotMeter: a three-dot column that tucks in beside a knob.
//
// SuprBand's three band strips are the reason this exists. The whole point
// of that face is that the same knob on each band sits at the same height
// so the three can be compared at a glance — but a knob position tells you
// what you ASKED for, not what the band is doing, and on a compressor
// inside a band those are very different things. A band whose compressor
// is catching hard needs its Level put back; one that never catches has a
// threshold set too high to matter. Neither is visible without a meter.
//
// A full meter per knob would wreck the layout this face is built around,
// so this is deliberately the smallest thing that answers the question:
// three dots, 3 dB each, no label, no readout, no frame. It reads as an
// indicator beside a control rather than as a display of its own.
//
// Direction carries the meaning, matching the house colour language:
//
//   comp   ● 3 dB      blue, filling DOWNWARD  — gain being taken away
//          ● 6 dB
//          ● 9 dB
//
//   drive  ● 9 dB      orange, filling UPWARD  — level being added
//          ● 6 dB
//          ● 3 dB
//
// Both ports are read as magnitudes: the compressor publishes reduction as
// a negative dB and the drive publishes its action as a positive one, so
// the sign is normalised here and the dots only ever mean "how much".
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

const DOT = 7;
const GAP = 4;
const COUNT = 3;
const STEP_DB = 3;
const RATE = 1.0 / 30;

interface SuprDotMeterProps extends WithStyles<typeof styles> {
    instanceId: number;
    port: string;
    // "down" lights from the top (cuts), "up" from the bottom (additions)
    direction: "up" | "down";
    tone: "cut" | "boost";
}

interface SuprDotMeterState {
    db: number;
}

const SuprDotMeter =
    withStyles(
        class extends Component<SuprDotMeterProps, SuprDotMeterState> {
            model: PiPedalModel;

            constructor(props: SuprDotMeterProps) {
                super(props);
                this.model = PiPedalModelFactory.getInstance();
                this.state = { db: 0 };
                this.onStateChanged = this.onStateChanged.bind(this);
            }

            handle: MonitorPortHandle | null = null;
            subscribedInstanceId: number = -1;
            subscribedPort: string = "";

            addSubscriptions() {
                this.subscribedInstanceId = this.props.instanceId;
                this.subscribedPort = this.props.port;
                this.handle = this.model.monitorPort(
                    this.props.instanceId, this.props.port, RATE,
                    (value: number) => { this.setState({ db: value }); });
            }
            removeSubscriptions() {
                if (this.handle) {
                    this.model.unmonitorPort(this.handle);
                    this.handle = null;
                }
                this.subscribedInstanceId = -1;
            }
            // Re-subscribe on EVERY arrival at Ready: the model reaches Ready
            // again after any websocket reconnect and the server-side
            // subscriptions do not survive it. Guarding on "has the
            // instanceId changed?" leaves the dots frozen for the session.
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
                if (this.subscribedInstanceId !== this.props.instanceId
                    || this.subscribedPort !== this.props.port) {
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
                const lit = this.props.tone === "cut"
                    ? (dark ? "#6fa7d8" : "#3773aa")   // cool: taken away
                    : (dark ? "#e88f4d" : "#d2691e");  // supr orange: added
                const off = dark ? "rgba(255,255,255,0.10)"
                    : "rgba(0,0,0,0.10)";

                const amount = Math.abs(this.state.db);

                // Index 0 is the TOP dot. Filling downward lights the top
                // first; filling upward lights the bottom first.
                let dots = [];
                for (let i = 0; i < COUNT; ++i) {
                    const rank = this.props.direction === "down"
                        ? i + 1
                        : COUNT - i;
                    const on = amount >= rank * STEP_DB;
                    dots.push(
                        <div key={i} style={{
                            width: DOT, height: DOT, borderRadius: DOT / 2,
                            background: on ? lit : off,
                            marginTop: i === 0 ? 0 : GAP,
                            transition: "background 90ms linear"
                        }} />
                    );
                }

                return (
                    <div style={{
                        display: "flex", flexFlow: "column nowrap",
                        alignItems: "center", justifyContent: "center",
                        width: DOT, flex: "0 0 auto",
                        marginLeft: 3, marginRight: 1
                    }}>
                        {dots}
                    </div>
                );
            }
        },
        styles);

export default SuprDotMeter;
