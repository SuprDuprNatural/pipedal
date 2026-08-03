// SuprTransientDisplay: the SuprTransient face — one bidirectional gain
// meter, and nothing else.
//
// A transient shaper is invisible on a level meter: it does not change the
// average level of anything (by construction — see src/TransientDsp.h), so a
// needle sits still while the pedal works. What is worth showing is the gain
// it is applying right now, read out from the middle:
//
//   ◄──── cut (cool)  │  boost (warm) ────►
//
// Two tracks either side of a small centre gap, each filling outward from
// it, so the pair reads as a single −/+ dB meter with zero at the middle.
// No frame and no inlay: it sits directly on the panel above the controls,
// where a bordered box would fight the panel's own walls.
//
// The DSP's gain port is peak-held (120 ms decay) precisely so a display
// polling at 30 Hz cannot miss the few-millisecond attack events; a short
// CSS transition smooths the steps between polls.
//
// MIT license, (c) 2026 SuprPedals contributors.

import { Component } from 'react';
import { Theme } from '@mui/material/styles';
import WithStyles from './WithStyles';
import { createStyles } from './WithStyles';
import { withStyles } from "tss-react/mui";
import { MonitorPortHandle, PiPedalModel, State, PiPedalModelFactory } from "./PiPedalModel";
import { isDarkMode } from './DarkMode';

const styles = (theme: Theme) => createStyles({});

const BAR_W = 81;                              // each half
const BAR_H = 8;                               // thin: it is a readout, not a plot
const BAR_GAP = 6;                             // the zero point sits in here
const METER_W = BAR_W * 2 + BAR_GAP;

const RATE = 1.0 / 30;
const GAIN_SPAN = 12; // dB, full deflection on each side

interface SuprTransientDisplayProps extends WithStyles<typeof styles> {
    instanceId: number;
    // read by PluginControlView: opt out of the fixed-height control slot
    tallControl?: boolean;
}

interface SuprTransientDisplayState {
    gainDb: number;
}

const SuprTransientDisplay =
    withStyles(
        class extends Component<SuprTransientDisplayProps, SuprTransientDisplayState> {
            model: PiPedalModel;

            constructor(props: SuprTransientDisplayProps) {
                super(props);
                this.model = PiPedalModelFactory.getInstance();
                this.state = { gainDb: 0 };
                this.onStateChanged = this.onStateChanged.bind(this);
            }

            monitorHandles: MonitorPortHandle[] = [];
            subscribedInstanceId: number = -1;

            addSubscriptions() {
                this.subscribedInstanceId = this.props.instanceId;
                this.monitorHandles.push(
                    this.model.monitorPort(this.props.instanceId, "gain", RATE,
                        (value: number) => { this.setState({ gainDb: value }); }));
            }
            removeSubscriptions() {
                for (const h of this.monitorHandles)
                    this.model.unmonitorPort(h);
                this.monitorHandles = [];
                this.subscribedInstanceId = -1;
            }
            // Re-subscribe on EVERY arrival at Ready, unconditionally. The
            // model reaches Ready again after a websocket reconnect — which
            // happens on a server restart, a Pi reboot, or a dropped
            // connection — and the server-side port subscriptions do not
            // survive it. Guarding this on "has the instanceId changed?"
            // looks like an optimisation and is a bug: the id has not
            // changed, so nothing re-subscribes, and the meters sit frozen
            // for the rest of the session while the knobs carry on working
            // because control values travel a different path entirely.
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

            // One half of the meter. fromRight puts the fill's anchor at the
            // centre gap, so both halves grow away from zero.
            bar(fillPx: number, color: string, fromRight: boolean, dark: boolean) {
                const track = dark ? "rgba(255,255,255,0.07)" : "rgba(0,0,0,0.07)";
                const tick = dark ? "rgba(255,255,255,0.16)" : "rgba(0,0,0,0.16)";
                const anchor = fromRight ? { right: 0 } : { left: 0 };
                return (
                    <div style={{
                        position: "relative", width: BAR_W, height: BAR_H,
                        borderRadius: 2, overflow: "hidden", background: track
                    }}>
                        {/* half-scale reference, the only marking there is */}
                        <div style={{
                            position: "absolute", left: BAR_W / 2, top: 0,
                            width: 1, height: BAR_H, background: tick
                        }} />
                        <div style={{
                            position: "absolute", top: 0, ...anchor,
                            width: fillPx, height: BAR_H, background: color,
                            transition: "width 90ms linear"
                        }} />
                    </div>
                );
            }

            render() {
                const dark = isDarkMode();
                const up = dark ? "#e88f4d" : "#d2691e";   // supr orange
                const down = dark ? "#6fa7d8" : "#3773aa"; // cool, for cuts

                const g = this.state.gainDb;
                const boostPx = Math.min(Math.max(g, 0) / GAIN_SPAN, 1) * BAR_W;
                const cutPx = Math.min(Math.max(-g, 0) / GAIN_SPAN, 1) * BAR_W;

                return (
                    <div style={{
                        width: METER_W, display: "flex", flexFlow: "row nowrap",
                        alignItems: "center"
                    }}>
                        {this.bar(cutPx, down, true, dark)}
                        <div style={{ width: BAR_GAP, flex: "0 0 auto" }} />
                        {this.bar(boostPx, up, false, dark)}
                    </div>
                );
            }
        },
        styles);

export default SuprTransientDisplay;
