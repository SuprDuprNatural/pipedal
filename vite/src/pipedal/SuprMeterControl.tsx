// SuprMeterControl: an analog needle meter for Supr pedals (gain-reduction
// and VU displays). Styled after the GxTuner meter face. Subscribes to LV2
// output control ports via PiPedalModel.monitorPort.
//
// MIT license, (c) 2026 SuprDuprNatural.

import React, { Component } from 'react';
import { Theme } from '@mui/material/styles';
import WithStyles from './WithStyles';
import { createStyles } from './WithStyles';
import { withStyles } from "tss-react/mui";
import { MonitorPortHandle, PiPedalModel, State, PiPedalModelFactory } from "./PiPedalModel";
import { isDarkMode } from './DarkMode';

const styles = (theme: Theme) => createStyles({});

export interface MeterNeedle {
    port: string;
    color: string;
}
export interface MeterTick {
    db: number;
    label?: string;
    red?: boolean;
}
export interface MeterLed {
    port: string;
    thresholdDb: number;
    color: string;
}

interface SuprMeterProps extends WithStyles<typeof styles> {
    instanceId: number;
    needles: MeterNeedle[];
    ticks: MeterTick[];
    minDb: number;
    maxDb: number;
    gamma: number; // > 1 spreads the top (right) end of the scale
    label: string;
    width?: number;
    height?: number;
    leds?: MeterLed[];
    // read by PluginControlView: opt out of the fixed-height control slot
    tallControl?: boolean;
}

interface SuprMeterState {
    values: number[];    // per needle, dB
    ledValues: number[]; // per led, dB
}

const SWEEP_RADIANS = 35 * Math.PI / 180;

const SuprMeterControl =
    withStyles(
        class extends Component<SuprMeterProps, SuprMeterState> {
            model: PiPedalModel;

            constructor(props: SuprMeterProps) {
                super(props);
                this.model = PiPedalModelFactory.getInstance();
                this.state = {
                    values: props.needles.map(() => props.minDb),
                    ledValues: (props.leds ?? []).map(() => -96),
                };
                this.onStateChanged = this.onStateChanged.bind(this);
            }

            monitorHandles: MonitorPortHandle[] = [];
            subscribedInstanceId: number = -1;

            addSubscriptions() {
                this.subscribedInstanceId = this.props.instanceId;
                this.props.needles.forEach((needle, i) => {
                    this.monitorHandles.push(
                        this.model.monitorPort(this.props.instanceId, needle.port, 1.0 / 30,
                            (value: number) => {
                                let values = [...this.state.values];
                                values[i] = value;
                                this.setState({ values: values });
                            }));
                });
                (this.props.leds ?? []).forEach((led, i) => {
                    this.monitorHandles.push(
                        this.model.monitorPort(this.props.instanceId, led.port, 1.0 / 15,
                            (value: number) => {
                                let ledValues = [...this.state.ledValues];
                                ledValues[i] = value;
                                this.setState({ ledValues: ledValues });
                            }));
                });
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

            // dB -> angle (radians, 0 = straight up, negative = left)
            dbToAngle(db: number): number {
                const { minDb, maxDb, gamma } = this.props;
                let x = (db - minDb) / (maxDb - minDb);
                if (x < 0) x = 0;
                if (x > 1) x = 1;
                const t = Math.pow(x, gamma);
                return (t - 0.5) * 2 * SWEEP_RADIANS;
            }

            polar(cx: number, cy: number, r: number, angle: number): [number, number] {
                return [cx + r * Math.sin(angle), cy - r * Math.cos(angle)];
            }

            render() {
                const width = this.props.width ?? 340;
                const height = this.props.height ?? 150;
                const cx = width / 2;
                const cy = height * 1.9;       // pivot below the face
                const rNeedle = cy - height * 0.28;
                const rTickOuter = rNeedle * 1.02;
                const rTickInner = rNeedle * 0.94;
                const rLabel = rNeedle * 1.1;
                const dark = isDarkMode();
                const tickColor = dark ? "#777" : "#555";
                const textColor = dark ? "#999" : "#444";

                let elements: React.ReactNode[] = [];
                let key = 0;

                // red zone arc
                const redTicks = this.props.ticks.filter(t => t.red);
                if (redTicks.length >= 1) {
                    const a0 = this.dbToAngle(redTicks[0].db);
                    const a1 = SWEEP_RADIANS;
                    const [x0, y0] = this.polar(cx, cy, rTickOuter, a0);
                    const [x1, y1] = this.polar(cx, cy, rTickOuter, a1);
                    elements.push(
                        <path key={key++}
                            d={`M ${x0} ${y0} A ${rTickOuter} ${rTickOuter} 0 0 1 ${x1} ${y1}`}
                            stroke="#b33" strokeWidth="3" fill="none" />);
                }

                // ticks + labels
                for (let tick of this.props.ticks) {
                    const a = this.dbToAngle(tick.db);
                    const [x0, y0] = this.polar(cx, cy, rTickOuter, a);
                    const [x1, y1] = this.polar(cx, cy, rTickInner, a);
                    elements.push(
                        <line key={key++} x1={x0} y1={y0} x2={x1} y2={y1}
                            stroke={tick.red ? "#b33" : tickColor}
                            strokeWidth={tick.label !== undefined ? 2 : 1} />);
                    if (tick.label !== undefined) {
                        const [tx, ty] = this.polar(cx, cy, rLabel, a);
                        elements.push(
                            <text key={key++} x={tx} y={ty} fontSize="10"
                                fill={tick.red ? "#b33" : textColor}
                                textAnchor="middle">{tick.label}</text>);
                    }
                }

                // needles
                this.props.needles.forEach((needle, i) => {
                    const a = this.dbToAngle(this.state.values[i]);
                    const [x0, y0] = this.polar(cx, cy, rNeedle * 0.35, a);
                    const [x1, y1] = this.polar(cx, cy, rNeedle, a);
                    elements.push(
                        <line key={key++} x1={x0} y1={y0} x2={x1} y2={y1}
                            stroke={needle.color} strokeWidth="2.5"
                            strokeLinecap="round" />);
                });

                // peak LEDs
                const leds = this.props.leds ?? [];
                leds.forEach((led, i) => {
                    const lit = this.state.ledValues[i] > led.thresholdDb;
                    elements.push(
                        <circle key={key++} cx={width - 18 - i * 22} cy={16} r={5}
                            fill={lit ? led.color : (dark ? "#333" : "#ccc")}
                            stroke={dark ? "#555" : "#999"} strokeWidth="1" />);
                });

                // legend for multiple needles
                if (this.props.needles.length > 1) {
                    this.props.needles.forEach((needle, i) => {
                        elements.push(
                            <text key={key++} x={14} y={16 + i * 14} fontSize="10"
                                fill={needle.color} fontWeight={700}>
                                {i === 0 ? "L" : "R"}</text>);
                    });
                }

                return (
                    <div style={{
                        width: width, height: height, position: "relative",
                        marginBottom: 20, marginRight: 12,
                        borderRadius: 6,
                        boxShadow: dark ?
                            "5px 5px 6px rgba(0,0,0,0.8) inset" :
                            "1px 5px 6px #888 inset",
                        background: dark ? "rgba(255,255,255,0.07)" : "",
                        fontFamily: "arial,roboto,helvetica,sans"
                    }}>
                        <svg viewBox={`0 0 ${width} ${height}`} width={width}
                            height={height} style={{ position: "absolute" }}>
                            {elements}
                        </svg>
                        <div style={{
                            position: "absolute", bottom: 6, width: "100%",
                            textAlign: "center", color: textColor,
                            fontSize: 13, fontWeight: 700, letterSpacing: 2
                        }}>
                            {this.props.label}
                        </div>
                    </div>);
            }
        },
        styles
    );

export default SuprMeterControl;
