// Custom control views for Supr pedals: a big gain-reduction meter for the
// Supr Compressor, and a stereo needle VU for Supr VU.
//
// MIT license, (c) 2026 SuprDuprNatural.

import React from 'react';
import { Theme } from '@mui/material/styles';
import WithStyles from './WithStyles';
import { createStyles } from './WithStyles';
import { withStyles } from "tss-react/mui";
import IControlViewFactory from './IControlViewFactory';
import { PiPedalModel, PiPedalModelFactory } from "./PiPedalModel";
import { PedalboardItem } from './Pedalboard';
import PluginControlView, { ICustomizationHost, ControlGroup, ControlViewCustomization } from './PluginControlView';
import SuprMeterControl, { MeterTick } from './SuprMeterControl';
import ToobSpectrumResponseView from './ToobSpectrumResponseView';

const SUPR_COMPRESSOR_URI = "https://suprduprnatural.github.io/supr-pedals/compressor";
const SUPR_VU_URI = "https://suprduprnatural.github.io/supr-pedals/vu-meter";

const styles = (theme: Theme) => createStyles({});

interface SuprViewProps extends WithStyles<typeof styles> {
    instanceId: number;
    item: PedalboardItem;
}
interface SuprViewState {
}

// 1176-style gain reduction scale: rest at 0 on the right, kicks left.
const GR_TICKS: MeterTick[] = [
    { db: -30, label: "30" },
    { db: -20, label: "20" },
    { db: -15 },
    { db: -10, label: "10" },
    { db: -7 },
    { db: -5, label: "5" },
    { db: -3, label: "3" },
    { db: -2 },
    { db: -1, label: "1" },
    { db: 0, label: "0" },
];

// Classic VU face: -20 to +3, red above 0.
const VU_TICKS: MeterTick[] = [
    { db: -20, label: "20" },
    { db: -10, label: "10" },
    { db: -7, label: "7" },
    { db: -5, label: "5" },
    { db: -3, label: "3" },
    { db: -2 },
    { db: -1, label: "1" },
    { db: 0, label: "0", red: true },
    { db: 2 },
    { db: 3, label: "+3", red: true },
];

// ---------------------------------------------------------------------------
// SuprSpectrum: the TooB Spectrum Analyzer display, suprfied — the plot fills
// the available width with the four controls tucked in on the right (they
// wrap below on narrow screens).
// ---------------------------------------------------------------------------

const SPECTRUM_CONTROLS_WIDTH = 200;
const SPECTRUM_PLOT_HEIGHT = 260;

interface SuprSpectrumControlProps {
    instanceId: number;
    controls: React.ReactNode[];
    tallControl?: boolean; // read by PluginControlView's node wrapper
}
interface SuprSpectrumControlState {
    plotWidth: number;
}

class SuprSpectrumControl extends React.Component<SuprSpectrumControlProps, SuprSpectrumControlState> {
    private rootRef: React.RefObject<HTMLDivElement | null>;
    private resizeObserver?: ResizeObserver;

    constructor(props: SuprSpectrumControlProps) {
        super(props);
        this.rootRef = React.createRef();
        this.state = { plotWidth: 480 };
    }

    updateWidth() {
        const el = this.rootRef.current;
        if (!el)
            return;
        const total = el.getBoundingClientRect().width;
        // room for the controls column unless we're so narrow they wrap below
        const forControls = total > 560 ? SPECTRUM_CONTROLS_WIDTH + 16 : 0;
        const plotWidth = Math.max(280, Math.floor(total - forControls - 20));
        if (Math.abs(plotWidth - this.state.plotWidth) > 2) {
            this.setState({ plotWidth: plotWidth });
        }
    }

    componentDidMount() {
        this.resizeObserver = new ResizeObserver(() => this.updateWidth());
        if (this.rootRef.current)
            this.resizeObserver.observe(this.rootRef.current);
        this.updateWidth();
    }
    componentWillUnmount() {
        this.resizeObserver?.disconnect();
    }

    render() {
        return (
            <div ref={this.rootRef} style={{
                width: "100%", display: "flex", flexFlow: "row wrap",
                alignItems: "flex-start", marginBottom: 12
            }}>
                <div style={{ flex: "0 0 auto" }}>
                    <ToobSpectrumResponseView
                        instanceId={this.props.instanceId}
                        width={this.state.plotWidth}
                        height={SPECTRUM_PLOT_HEIGHT} />
                </div>
                <div style={{
                    flex: "0 0 auto", width: SPECTRUM_CONTROLS_WIDTH,
                    display: "flex", flexFlow: "row wrap",
                    justifyContent: "flex-start", marginLeft: 8
                }}>
                    {this.props.controls}
                </div>
            </div>);
    }
}

const SuprSpectrumView =
    withStyles(
        class extends React.Component<SuprViewProps, SuprViewState>
            implements ControlViewCustomization {
            model: PiPedalModel;
            customizationId: number = 1;

            fullScreen() {
                return false;
            }

            constructor(props: SuprViewProps) {
                super(props);
                this.model = PiPedalModelFactory.getInstance();
                this.state = {};
            }

            modifyControls(host: ICustomizationHost,
                controls: (React.ReactNode | ControlGroup)[]): (React.ReactNode | ControlGroup)[] {
                return [(
                    <SuprSpectrumControl key="supr_spectrum"
                        instanceId={this.props.instanceId}
                        controls={controls as React.ReactNode[]}
                        tallControl={true} />
                )];
            }

            render() {
                return (<PluginControlView
                    instanceId={this.props.instanceId}
                    item={this.props.item}
                    customization={this}
                    customizationId={this.customizationId}
                    showModGui={false}
                    onSetShowModGui={(instanceId: number, showModGui: boolean) => { }}
                />);
            }
        },
        styles
    );

const SuprCompressorView =
    withStyles(
        class extends React.Component<SuprViewProps, SuprViewState>
            implements ControlViewCustomization {
            model: PiPedalModel;
            customizationId: number = 1;

            fullScreen() {
                return false;
            }

            constructor(props: SuprViewProps) {
                super(props);
                this.model = PiPedalModelFactory.getInstance();
                this.state = {};
            }

            modifyControls(host: ICustomizationHost,
                controls: (React.ReactNode | ControlGroup)[]): (React.ReactNode | ControlGroup)[] {
                // the gr port is pprops:notOnGUI; the meter displays it instead
                const meter = (
                    <SuprMeterControl key="supr_gr_meter"
                        instanceId={this.props.instanceId}
                        needles={[{ port: "gr", color: "#b03030" }]}
                        ticks={GR_TICKS}
                        minDb={-30} maxDb={0} gamma={2.4}
                        label="GAIN REDUCTION"
                        width={360} height={160} tallControl={true} />);
                return [meter, ...controls];
            }

            render() {
                return (<PluginControlView
                    instanceId={this.props.instanceId}
                    item={this.props.item}
                    customization={this}
                    customizationId={this.customizationId}
                    showModGui={false}
                    onSetShowModGui={(instanceId: number, showModGui: boolean) => { }}
                />);
            }
        },
        styles
    );

const SuprVuView =
    withStyles(
        class extends React.Component<SuprViewProps, SuprViewState>
            implements ControlViewCustomization {
            model: PiPedalModel;
            customizationId: number = 1;

            fullScreen() {
                return false;
            }

            constructor(props: SuprViewProps) {
                super(props);
                this.model = PiPedalModelFactory.getInstance();
                this.state = {};
            }

            modifyControls(host: ICustomizationHost,
                controls: (React.ReactNode | ControlGroup)[]): (React.ReactNode | ControlGroup)[] {
                // a classic stereo pair: one VU face per channel
                const meterL = (
                    <SuprMeterControl key="supr_vu_meter_l"
                        instanceId={this.props.instanceId}
                        needles={[{ port: "vu_l", color: "#b03030" }]}
                        leds={[{ port: "peak_l", thresholdDb: -1, color: "#e33" }]}
                        ticks={VU_TICKS}
                        minDb={-20} maxDb={3} gamma={2.0}
                        label="LEFT"
                        width={300} height={140} tallControl={true} />);
                const meterR = (
                    <SuprMeterControl key="supr_vu_meter_r"
                        instanceId={this.props.instanceId}
                        needles={[{ port: "vu_r", color: "#b03030" }]}
                        leds={[{ port: "peak_r", thresholdDb: -1, color: "#e33" }]}
                        ticks={VU_TICKS}
                        minDb={-20} maxDb={3} gamma={2.0}
                        label="RIGHT"
                        width={300} height={140} tallControl={true} />);
                // The four dB output readouts are pprops:notOnGUI, so `controls`
                // holds only the visible input controls (the Calibration knob);
                // show the meters, then that knob below them.
                return [meterL, meterR, ...controls];
            }

            render() {
                return (<PluginControlView
                    instanceId={this.props.instanceId}
                    item={this.props.item}
                    customization={this}
                    customizationId={this.customizationId}
                    showModGui={false}
                    onSetShowModGui={(instanceId: number, showModGui: boolean) => { }}
                />);
            }
        },
        styles
    );

export class SuprCompressorViewFactory implements IControlViewFactory {
    uri: string = SUPR_COMPRESSOR_URI;
    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprCompressorView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
    }
}

export class SuprVuViewFactory implements IControlViewFactory {
    uri: string = SUPR_VU_URI;
    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprVuView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
    }
}

// Takes over the display of the TooB Spectrum Analyzer (registered ahead of
// the stock factory in ControlViewFactory).
export class SuprSpectrumViewFactory implements IControlViewFactory {
    uri: string = "http://two-play.com/plugins/toob-spectrum";
    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprSpectrumView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
    }
}
