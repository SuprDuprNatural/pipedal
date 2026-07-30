// Custom control views for Supr pedals: a big gain-reduction meter for
// SuprCompressor, a stereo needle VU for SuprVU, and live filter-response
// plots for SuprOctavePlus and SuprEnvelopeFilter.
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
import SuprResponsePlot from './SuprResponsePlot';
import SuprTunerDisplay from './SuprTunerDisplay';
import SuprTransientDisplay from './SuprTransientDisplay';
import SuprStepKnob from './SuprStepKnob';
import SuprChorusDisplay from './SuprChorusDisplay';
import { PanelColumn, SuprPanelUnit, mapControlNodes } from './SuprPanel';
import ToobSpectrumResponseView from './ToobSpectrumResponseView';

const SUPR_COMPRESSOR_URI = "https://suprduprnatural.github.io/supr-pedals/compressor";
const SUPR_VU_URI = "https://suprduprnatural.github.io/supr-pedals/vu-meter";
const SUPR_OCTAVE_URI = "https://suprduprnatural.github.io/supr-pedals/octave";
const SUPR_OCTAVE_PLUS_URI = "https://suprduprnatural.github.io/supr-pedals/octave-plus";
const SUPR_ENV_FILTER_URI = "https://suprduprnatural.github.io/supr-pedals/envelope-filter";
const SUPR_TUNER_URI = "https://suprduprnatural.github.io/supr-pedals/tuner";
const SUPR_TRANSIENT_URI = "https://suprduprnatural.github.io/supr-pedals/transient";
const SUPR_CHORUS_URI = "https://suprduprnatural.github.io/supr-pedals/chorus";
const SUPR_SANS_URI = "https://suprduprnatural.github.io/supr-pedals/sans";
const SUPR_FUZZ_URI = "https://suprduprnatural.github.io/supr-pedals/fuzz";
const SUPR_BAND_URI = "https://suprduprnatural.github.io/supr-pedals/multiband";

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

// Layout budget for sizing the plot. We measure the width-authoritative
// scroll frame (see findFrame) and give the plot everything left over after
// the grid's own padding, the plot frame's margins, and — when there's room —
// the controls column beside it.
const SPECTRUM_GRID_PAD = 80;      // PluginControlView grid paddingLeft(30)+Right(45), + slack
const SPECTRUM_PLOT_MARGIN = 16;   // ToobSpectrumResponseView frame marginLeft(8)+Right(8)
const SPECTRUM_CONTROLS_COL = SPECTRUM_CONTROLS_WIDTH + 16; // column width + its marginLeft/gap
const SPECTRUM_MIN_PLOT = 280;     // never narrower than this
const SPECTRUM_MIN_WIDE = 420;     // below this, drop the side column and let controls wrap under

interface SuprSpectrumControlProps {
    instanceId: number;
    controls: React.ReactNode[];
    tallControl?: boolean; // read by PluginControlView's node wrapper
}
interface SuprSpectrumControlState {
    plotWidth: number;
    controlsBeside: boolean;
}

class SuprSpectrumControl extends React.Component<SuprSpectrumControlProps, SuprSpectrumControlState> {
    private rootRef: React.RefObject<HTMLDivElement | null>;
    private resizeObserver?: ResizeObserver;
    private frameEl: HTMLElement | null = null;

    constructor(props: SuprSpectrumControlProps) {
        super(props);
        this.rootRef = React.createRef();
        this.state = { plotWidth: 480, controlsBeside: true };
    }

    // Walk up to the width-authoritative scroll frame. Our node lives inside
    // shrink-to-fit wrappers (controlPadding, and a fit-content landscape
    // grid), so measuring ourselves is circular and the plot never grows.
    // The scroll frame (frameScrollLandscape / frameScrollFitContent) is the
    // nearest block-level ancestor with a bounded width in both view modes;
    // identify it by display:block + a clipping/scrolling overflowX.
    private findFrame(): HTMLElement | null {
        let el: HTMLElement | null = this.rootRef.current?.parentElement ?? null;
        for (let i = 0; i < 12 && el; ++i, el = el.parentElement) {
            const cs = window.getComputedStyle(el);
            const ox = cs.overflowX;
            if (cs.display === "block" && (ox === "hidden" || ox === "auto" || ox === "scroll")) {
                return el;
            }
        }
        return null;
    }

    updateWidth() {
        const root = this.rootRef.current;
        if (!root)
            return;
        if (!this.frameEl || !this.frameEl.isConnected) {
            this.frameEl = this.findFrame();
        }
        let avail: number;
        if (this.frameEl) {
            const cs = window.getComputedStyle(this.frameEl);
            const padX = (parseFloat(cs.paddingLeft) || 0) + (parseFloat(cs.paddingRight) || 0);
            avail = this.frameEl.clientWidth - padX;
        } else {
            avail = root.getBoundingClientRect().width; // fallback: measure self
        }
        const usable = avail - SPECTRUM_GRID_PAD - SPECTRUM_PLOT_MARGIN;
        let controlsBeside = usable - SPECTRUM_CONTROLS_COL >= SPECTRUM_MIN_WIDE;
        let plotWidth = controlsBeside ? usable - SPECTRUM_CONTROLS_COL : usable;
        plotWidth = Math.max(SPECTRUM_MIN_PLOT, Math.floor(plotWidth));
        if (Math.abs(plotWidth - this.state.plotWidth) > 2 || controlsBeside !== this.state.controlsBeside) {
            this.setState({ plotWidth: plotWidth, controlsBeside: controlsBeside });
        }
    }

    componentDidMount() {
        this.frameEl = this.findFrame();
        this.resizeObserver = new ResizeObserver(() => this.updateWidth());
        // Observe the frame so the plot follows window resizes; fall back to
        // observing ourselves if the frame couldn't be located.
        const observed = this.frameEl ?? this.rootRef.current;
        if (observed)
            this.resizeObserver.observe(observed);
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
                    flex: this.state.controlsBeside ? "0 0 auto" : "1 1 100%",
                    width: this.state.controlsBeside ? SPECTRUM_CONTROLS_WIDTH : "auto",
                    display: "flex", flexFlow: "row wrap",
                    justifyContent: "flex-start", marginLeft: this.state.controlsBeside ? 8 : 0
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

// The compressor face: big GR meter on the left, the Compression section on
// the right with the Output section nestled directly beneath it — like the
// front panel of a real studio compressor. (The gr port is pprops:notOnGUI;
// the meter displays it instead.)
const SuprCompressorView = makePanelView((ctx) => [
    {
        grow: 0, sections: [{
            rows: [[(
                <SuprMeterControl key="supr_gr_meter"
                    instanceId={ctx.instanceId}
                    needles={[{ port: "gr", color: "#b03030" }]}
                    ticks={GR_TICKS}
                    minDb={-30} maxDb={0} gamma={2.4}
                    label="GAIN REDUCTION"
                    width={360} height={160} />
            )]]
        }]
    },
    {
        sections: [
            { label: "Compression", rows: [["threshold", "ratio", "attack", "release", "schpf"]] },
            { label: "Output", rows: [["makeup", "blend"]] },
        ]
    },
]);

// A classic stereo pair: one VU face per channel, Calibration at the end.
// (The four dB readout ports are pprops:notOnGUI; the needles show them.)
const SuprVuView = makePanelView((ctx) => [
    {
        grow: 0, sections: [{
            rows: [[(
                <SuprMeterControl key="supr_vu_meter_l"
                    instanceId={ctx.instanceId}
                    needles={[{ port: "vu_l", color: "#b03030" }]}
                    leds={[{ port: "peak_l", thresholdDb: -1, color: "#e33" }]}
                    ticks={VU_TICKS}
                    minDb={-20} maxDb={3} gamma={2.0}
                    label="LEFT"
                    width={300} height={140} />
            )]]
        }]
    },
    {
        grow: 0, sections: [{
            rows: [[(
                <SuprMeterControl key="supr_vu_meter_r"
                    instanceId={ctx.instanceId}
                    needles={[{ port: "vu_r", color: "#b03030" }]}
                    leds={[{ port: "peak_r", thresholdDb: -1, color: "#e33" }]}
                    ticks={VU_TICKS}
                    minDb={-20} maxDb={3} gamma={2.0}
                    label="RIGHT"
                    width={300} height={140} />
            )]]
        }]
    },
    { sections: [{ rows: [["calibration"]] }] },
]);

// ---------------------------------------------------------------------------
// Panel-based views: one TooB-EQ-style console unit per pedal, built by
// re-arranging the standard control nodes into nestled sections (SuprPanel).
// ---------------------------------------------------------------------------

interface PanelContext {
    instanceId: number;
    // current control values, for the response plots
    controlValues: { [symbol: string]: number };
}
// A builder returns the unit's columns, or — when the pedal has a readout
// belonging to the whole unit rather than to one section — a header node and
// the columns that sit under it.
interface PanelSpec {
    header?: React.ReactNode;
    columns: PanelColumn[];
}
type PanelBuilder = (ctx: PanelContext) => PanelColumn[] | PanelSpec;

function makePanelView(builder: PanelBuilder) {
    return withStyles(
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

            currentControlValues(): { [symbol: string]: number } {
                let values: { [symbol: string]: number } = {};
                try {
                    const item = this.model.pedalboard.get().getItem(this.props.instanceId);
                    for (let cv of item.controlValues) {
                        values[cv.key] = cv.value;
                    }
                } catch (e) {
                    // start/end/missing items: plots fall back to defaults
                }
                return values;
            }

            modifyControls(host: ICustomizationHost,
                controls: (React.ReactNode | ControlGroup)[]): (React.ReactNode | ControlGroup)[] {
                const nodes = mapControlNodes(this.model, this.props.item.uri, controls);
                const built = builder({
                    instanceId: this.props.instanceId,
                    controlValues: this.currentControlValues()
                });
                const spec: PanelSpec =
                    Array.isArray(built) ? { columns: built } : built;
                return [(
                    <SuprPanelUnit key="supr_panel" columns={spec.columns}
                        header={spec.header} nodes={nodes}
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
}

// The faces below are stacked rather than laid out in one long row: sections
// become columns, and a section wider than a pair of controls wraps onto a
// second line instead of running off to the right. The target is a face that
// is roughly as tall as it is wide. Pure one-per-row columns were tried and
// are worse — anything with more than four controls turns into a ladder.
const SuprOctaveView = makePanelView(() => [
    { sections: [{ label: "Mix", rows: [["direct", "oct1"], ["oct2"]] }] },
    { sections: [{ label: "Sub", rows: [["tone"], ["gate"]] }] },
]);

// This one is not a pedal and should not look like one. Twenty-one controls
// across four genuinely different jobs is a SYNTHESISER, so it gets a synth's
// face: the display tucked into the top left with the octave voices beneath
// it, and Synth, Filter and Envelope each given a full-height room of its own
// down the rest of the panel. Every section is roomy, and the three on the
// right run the whole height of the unit rather than being packed to their
// contents — which is what makes them read as panels instead of as rows.
const SuprOctavePlusView = makePanelView((ctx) => [
    {
        sections: [
            {
                noLabelSlot: true, roomy: true, rows: [[(
                    <SuprResponsePlot key="plot" instanceId={ctx.instanceId}
                        variant="octaveplus" controls={ctx.controlValues} frameless />
                )]]
            },
            { label: "Mixer", roomy: true, rows: [["direct", "gate"], ["oct1", "tone"]] },
        ]
    },
    {
        // One row per oscillator — level, octave, waveform — so the two read
        // as a pair rather than as six unrelated controls, with the shared
        // pitch controls under them. The waveform dropdowns drop their
        // captions: the knob to their left already names the oscillator.
        sections: [{
            label: "Synth", roomy: true,
            rows: [["osc1level", "osc1oct", { compactSelect: "osc1wave" }],
            ["osc2level", "osc2oct", { compactSelect: "osc2wave" }],
            ["detune", "glide"]]
        }]
    },
    { sections: [{ label: "Filter", roomy: true, rows: [["cutoff", "res"], ["envmod", "keytrack"], ["fattack", "fdecay"]] }] },
]);

// Drive and the tone stack down the left, the two voicing switches and the
// output trim down the right. The tone stack is the only section big enough
// to want two lines.
const SuprSansView = makePanelView(() => [
    {
        sections: [
            { label: "Drive", rows: [["drive", "blend"]] },
            { label: "Tone", rows: [["bass", "mid"], ["midfreq", "treble"]] },
        ]
    },
    {
        sections: [
            { label: "Filters", rows: [["air"], ["rumble"]] },
            { label: "Output", rows: [["level"]] },
        ]
    },
]);

const SuprFuzzView = makePanelView(() => [
    { sections: [{ label: "Fuzz", rows: [["sustain", "tone"], ["octave"]] }] },
    { sections: [{ label: "Output", rows: [["gate", "blend"], ["level"]] }] },
]);

// Three identical band strips side by side, each read top to bottom, so the
// same knob on each band is always at the same height and the three can be
// compared at a glance. That is the whole reason this face is worth having,
// so the strips keep their columns even though everything else here got
// wider. Signal order across: the splits that make the bands, the bands, and
// the blend and level that end them.
const SuprBandView = makePanelView(() => [
    { sections: [{ label: "Crossover", rows: [["split1"], ["split2"]] }] },
    { sections: [{ label: "Low", rows: [["lowComp"], ["lowDrive"], ["lowLevel"]] }] },
    { sections: [{ label: "Mid", rows: [["midComp"], ["midDrive"], ["midLevel"]] }] },
    { sections: [{ label: "High", rows: [["highComp"], ["highDrive"], ["highLevel"]] }] },
    { sections: [{ label: "Output", rows: [["blend"], ["level"]] }] },
]);

const SuprEnvFilterView = makePanelView((ctx) => [
    {
        grow: 0, sections: [{
            rows: [
                [(
                    <SuprResponsePlot key="plot" instanceId={ctx.instanceId}
                        variant="envfilter" controls={ctx.controlValues} frameless />
                )],
                ["mode", "dir"]
            ]
        }]
    },
    { sections: [{ label: "Envelope", rows: [["sens"], ["attack", "release"]] }] },
    { sections: [{ label: "Filter", rows: [["cutoff", "range"], ["res"]] }] },
    { sections: [{ label: "Output", rows: [["blend"], ["level"]] }] },
]);

const SuprTunerView = makePanelView((ctx) => [
    {
        grow: 0, sections: [{
            rows: [[(
                <SuprTunerDisplay key="supr_tuner_display"
                    instanceId={ctx.instanceId} />
            )]]
        }]
    },
    {
        sections: [{
            rows: [["mute"]]
        }]
    },
]);

// The transient face: the −/+ gain meter across the top, then the two
// shaping knobs in their own column with the detector and output setup
// beside them. Narrow and tall rather than wide — the meter is the only
// thing that wants width, and it wants less of it than five knobs in a row.
// No section labels: with five controls that all say what they are, they
// would be decoration.
const SuprTransientView = makePanelView((ctx) => ({
    header: (
        <SuprTransientDisplay key="supr_transient_display"
            instanceId={ctx.instanceId} />
    ),
    columns: [
        // HR sits under the two shaping knobs: it is what calibrates them to
        // your rig, so it belongs with them rather than off in the metering
        // area. It also evens the two columns at three rows each, and being
        // visibly a different kind of knob is what says "set this once".
        {
            sections: [{
                noLabelSlot: true,
                rows: [["attack"], ["sustain"], [(
                    <SuprStepKnob key="supr_transient_hr"
                        instanceId={ctx.instanceId} symbol="hr"
                        value={ctx.controlValues["hr"] ?? 0}
                        label="HR" step={3} min={-12} max={12} />
                )]]
            }]
        },
        { sections: [{ noLabelSlot: true, rows: [["schpf"], ["focus"], ["level"]] }] },
    ]
}));

// The display carries the whole state of the pedal, so the knobs sit under it
// as one unlabelled 3x2 block. Modulation on the top row, band on the bottom.
const SuprChorusView = makePanelView((ctx) => [
    {
        grow: 0, sections: [{
            rows: [[(
                <SuprChorusDisplay key="supr_chorus_display"
                    instanceId={ctx.instanceId} controls={ctx.controlValues} />
            )]]
        }]
    },
    {
        sections: [{
            rows: [["rate", "depth", "voices"], ["low", "tone", "mix"]]
        }]
    },
]);

export class SuprChorusViewFactory implements IControlViewFactory {
    uri: string = SUPR_CHORUS_URI;
    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprChorusView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
    }
}

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

export class SuprOctaveViewFactory implements IControlViewFactory {
    uri: string = SUPR_OCTAVE_URI;
    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprOctaveView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
    }
}

export class SuprOctavePlusViewFactory implements IControlViewFactory {
    uri: string = SUPR_OCTAVE_PLUS_URI;
    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprOctavePlusView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
    }
}

export class SuprSansViewFactory implements IControlViewFactory {
    uri: string = SUPR_SANS_URI;
    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprSansView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
    }
}

export class SuprFuzzViewFactory implements IControlViewFactory {
    uri: string = SUPR_FUZZ_URI;
    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprFuzzView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
    }
}

export class SuprBandViewFactory implements IControlViewFactory {
    uri: string = SUPR_BAND_URI;
    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprBandView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
    }
}

export class SuprEnvFilterViewFactory implements IControlViewFactory {
    uri: string = SUPR_ENV_FILTER_URI;
    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprEnvFilterView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
    }
}

export class SuprTunerViewFactory implements IControlViewFactory {
    uri: string = SUPR_TUNER_URI;
    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprTunerView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
    }
}

export class SuprTransientViewFactory implements IControlViewFactory {
    uri: string = SUPR_TRANSIENT_URI;
    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprTransientView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
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
