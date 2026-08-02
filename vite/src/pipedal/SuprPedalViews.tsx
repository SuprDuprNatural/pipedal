// Custom control views for Supr pedals: a big gain-reduction meter for
// SuprCompressor, a stereo needle VU for SuprVU, and live filter-response
// plots for SuprOctavePlus and SuprEnvelope.
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
import SuprChorusDisplay from './SuprChorusDisplay';
import SuprClackDisplay from './SuprClackDisplay';
import SuprDotMeter from './SuprDotMeter';
import { PanelColumn, SuprPanelUnit, mapControlNodes } from './SuprPanel';

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
const SUPR_CLACK_URI = "https://suprduprnatural.github.io/supr-pedals/clack";

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

// The compressor face is one uninterrupted console section: the four timing
// controls sit directly under the GR meter, while the three setup/output
// controls form a balanced stack at its right.
const SuprCompressorView = makePanelView((ctx) => [
    {
        sections: [{
            noLabelSlot: true,
            rowAlign: "stretch",
            rowGap: 8,
            rows: [[
                {
                    panelGroup: "column",
                    items: [
                        (
                            <SuprMeterControl key="supr_gr_meter"
                                instanceId={ctx.instanceId}
                                needles={[{ port: "gr", color: "#b03030" }]}
                                ticks={GR_TICKS}
                                minDb={-30} maxDb={0} gamma={2.4}
                                label="GAIN REDUCTION"
                                width={360} height={160} />
                        ),
                        {
                            panelGroup: "row",
                            items: ["threshold", "ratio", "attack", "release"]
                        }
                    ]
                },
                {
                    panelGroup: "column",
                    items: [
                        { supr: "makeup", step: 3, showReadout: true, showPointer: true },
                        { supr: "blend", marks: "home" },
                        { supr: "schpf", marks: "home" }
                    ],
                    justify: "space-evenly",
                    stretch: true
                }
            ]]
        }]
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
    { sections: [{ rows: [[{ supr: "calibration", marks: "home" }]] }] },
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
                const controlValues = this.currentControlValues();
                const built = builder({
                    instanceId: this.props.instanceId,
                    controlValues: controlValues
                });
                const spec: PanelSpec =
                    Array.isArray(built) ? { columns: built } : built;
                return [(
                    <SuprPanelUnit key="supr_panel" columns={spec.columns}
                        header={spec.header} nodes={nodes}
                        instanceId={this.props.instanceId}
                        uri={this.props.item.uri}
                        controlValues={controlValues}
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
    {
        sections: [{
            noLabelSlot: true,
            rows: [
                [{ supr: "direct", marks: "home" }, { supr: "oct1", marks: "home" }],
                [{ supr: "tone", marks: "home" }, { supr: "gate", marks: "home" }]
            ]
        }]
    }
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
            {
                label: "Mixer", roomy: true,
                rows: [
                    [{ supr: "direct", marks: "home" }, { supr: "gate", marks: "home" }],
                    [{ supr: "oct1", marks: "home" }, { supr: "tone", marks: "home" }]
                ]
            },
        ]
    },
    {
        // One row per oscillator — level, octave, waveform — so the two read
        // as a pair rather than as six unrelated controls, with the shared
        // pitch controls under them. The waveform dropdowns drop their
        // captions: the knob to their left already names the oscillator.
        sections: [{
            label: "Synth", roomy: true,
            rows: [
                [
                    { supr: "osc1level", marks: "home" },
                    { supr: "osc1oct", step: 1, showReadout: true },
                    { supr: "osc1wave", compact: true }
                ],
                [
                    { supr: "osc2level", marks: "home" },
                    { supr: "osc2oct", step: 1, showReadout: true },
                    { supr: "osc2wave", compact: true }
                ],
                [{ supr: "detune", marks: "home" }, "glide"]
            ]
        }]
    },
    {
        sections: [{
            label: "Filter", roomy: true,
            rows: [
                [{ supr: "cutoff", marks: "home" }, "res"],
                [{ supr: "envmod", marks: "home" }, { supr: "keytrack", marks: "home" }],
                ["fattack", "fdecay"]
            ]
        }]
    },
]);

// Drive and the tone stack down the left, the two voicing switches and the
// output trim down the right. The tone stack is the only section big enough
// to want two lines.
const SuprSansView = makePanelView(() => [
    {
        sections: [
            {
                noLabelSlot: true,
                rows: [["drive", { supr: "blend", marks: "home" }]]
            },
            {
                noLabelSlot: true,
                rows: [
                    [{ supr: "bass", marks: "home" }, { supr: "mid", marks: "home" }],
                    [{ supr: "midfreq", marks: "home" }, { supr: "treble", marks: "home" }]
                ]
            },
        ]
    },
    {
        sections: [{
            noLabelSlot: true,
            spreadRows: true,
            rows: [
                [{ supr: "air", hideLabel: true, buttonText: "AIR\nLIFT" }],
                [{ supr: "rumble", hideLabel: true, buttonText: "RUMBLE\nCUT" }],
                [{ supr: "level", step: 3, showReadout: true, showPointer: true }]
            ]
        }]
    },
]);

const SuprFuzzView = makePanelView(() => [
    {
        sections: [{
            noLabelSlot: true,
            rows: [
                [
                    { supr: "sustain", marks: "fill", markCount: 11 },
                    { supr: "tone", marks: "endpoints" }
                ],
                [{ supr: "gate" }, { supr: "blend", marks: "endpoints" }],
                [{ supr: "level", step: 3, showReadout: true, showPointer: true }]
            ]
        }]
    }
]);

// Three identical band strips side by side, each read top to bottom, so the
// same knob on each band is always at the same height and the three can be
// compared at a glance. That is the whole reason this face is worth having,
// so the strips keep their columns even though everything else here got
// wider. Signal order across: the splits that make the bands, the bands, and
// the blend and level that end them.
// Each band's Comp and Drive knob gets a three-dot meter tucked in on its
// right — blue down for reduction, orange up for the level the drive is
// adding, 3 dB a dot. Every strip gains the same width, so the three
// bands stay in line with each other, which is the whole reason this face
// keeps its columns. Level needs no meter: it is not program dependent.
//
// The dots are BALANCED by an empty spacer of the same width on the left.
// Without it the row centres knob-plus-dots as one object, which slides
// the knob half the meter's width to the left and breaks its alignment
// with the uncluttered Level knob below it — the columns would still line
// up with each other, and every knob would still be off its own axis.
const DOT_METER_W = 11; // SuprDotMeter: 7px dot + 3 left + 1 right
const dotSpacer = (key: string) => (
    <div key={key} style={{ width: DOT_METER_W, flex: "0 0 auto" }} />
);

const meteredKnob = (symbol: string, port: string, instanceId: number,
    direction: "up" | "down", tone: "cut" | "boost") => ({
        panelGroup: "row" as const, align: "center" as const, items: [
            dotSpacer(port + "_pad"),
            { supr: symbol, marks: "home" as const },
            (<SuprDotMeter key={port} instanceId={instanceId}
                port={port} direction={direction} tone={tone} />)
        ]
    });

const bandStrip = (name: string, comp: string, drive: string,
    level: string, grPort: string, drvPort: string,
    ctx: { instanceId: number }): PanelColumn => ({
        sections: [{
            label: name,
            rows: [
                [meteredKnob(comp, grPort, ctx.instanceId, "down", "cut")],
                [meteredKnob(drive, drvPort, ctx.instanceId, "up", "boost")],
                [{ supr: level, step: 3, showReadout: true, showPointer: true }]
            ]
        }]
    });

const SuprBandView = makePanelView((ctx) => [
    {
        sections: [{
            noLabelSlot: true,
            centerRows: true,
            rows: [[{ supr: "split1", marks: "home" }], [{ supr: "split2", marks: "home" }]]
        }]
    },
    bandStrip("Low", "lowComp", "lowDrive", "lowLevel", "grLow", "drvLow", ctx),
    bandStrip("Mid", "midComp", "midDrive", "midLevel", "grMid", "drvMid", ctx),
    bandStrip("High", "highComp", "highDrive", "highLevel", "grHigh", "drvHigh", ctx),
    {
        sections: [{
            noLabelSlot: true,
            centerRows: true,
            rows: [
                [{ supr: "blend", marks: "home" }],
                [{ supr: "level", step: 3, showReadout: true, showPointer: true }]
            ]
        }]
    },
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
    {
        sections: [{
            label: "Envelope",
            rows: [[{ supr: "sens", marks: "home" }], ["attack", "release"]]
        }]
    },
    {
        sections: [{
            label: "Filter",
            rows: [[{ supr: "cutoff", marks: "home" }, "range"], ["res"]]
        }]
    },
    {
        sections: [{
            label: "Output",
            rows: [[{ supr: "blend", marks: "home" }], [{ supr: "level", marks: "home" }]]
        }]
    },
]);

const SuprTunerView = makePanelView((ctx) => [
    {
        grow: 0, sections: [{
            noLabelSlot: true,
            rows: [
                [(
                    <SuprTunerDisplay key="supr_tuner_display"
                        instanceId={ctx.instanceId} />
                )],
                [{ supr: "mute", hideLabel: true }]
            ]
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
                rows: [
                    [{ supr: "attack", marks: "home" }],
                    [{ supr: "sustain", marks: "home" }],
                    [{ supr: "hr", step: 3 }]
                ]
            }]
        },
        {
            sections: [{
                noLabelSlot: true,
                rows: [
                    [{ supr: "schpf", marks: "home" }],
                    [{ supr: "focus", marks: "home" }],
                    [{ supr: "level", step: 3, showReadout: true, showPointer: true }]
                ]
            }]
        },
    ]
}));

// Three sections stacked down one column, in the order you set them up:
// what the pedal takes out while you play (Noise), what it does in the
// gaps (Expander), and what comes out (Output). Sections rather than one
// flat block of eight knobs, because the three groups answer different
// questions and share nothing but the signal.
//
// The meters go in the header, above all of it. With three independent
// things that can be turning the signal down, "which one is acting" is
// the only question this face has to answer, and the answer is what tells
// you which knob to reach for.
//
// Clack and Scrape lead: they are the two amounts, and that pair is the
// pedal. Sense and Focus are shared detector settings for both the click
// and the squeak duck, so they sit under them rather than in a section of
// their own.
//
// Delta gets an unlabelled section of its own beside the expander rather
// than a row underneath it. It belongs to no section — it is a monitor,
// and it changes what you hear rather than what the pedal does — so
// filing it under any of the three would be a small lie about what it
// is. Unlabelled and tucked into the space the expander's three knobs
// leave, it costs no height at all.
//
// There is no output trim. This pedal only ever takes away, and by
// amounts it decides for itself; a make-up knob would be a second
// opinion about a level the pedal never set.
const SuprClackView = makePanelView((ctx) => ({
    header: (
        <SuprClackDisplay key="supr_clack_display"
            instanceId={ctx.instanceId} />
    ),
    columns: [
        {
            sections: [
                {
                    label: "Noise",
                    rows: [
                        [{ supr: "clack", marks: "home" },
                        { supr: "scrape", marks: "home" },
                        { supr: "sense", marks: "home" },
                        { supr: "focus", marks: "home" }]
                    ]
                },
                {
                    label: "Expander",
                    rows: [
                        [
                            { supr: "thresh", marks: "home" },
                            { supr: "range", marks: "home" },
                            { supr: "release", marks: "home" },
                            // Delta's own compartment, walled off with the
                            // panel's own rule. It cannot be a COLUMN — a
                            // column runs the full height of the unit, which
                            // would put it beside the Noise row as well, and
                            // Delta has nothing to do with those four knobs.
                            // Inside this row it lands exactly under Noise's
                            // fourth knob, so the face reads as a clean 4x2.
                            (<div key="delta_wall" style={{
                                alignSelf: "stretch", width: 4,
                                background: "#888", marginLeft: 6,
                                marginRight: 6
                            }} />),
                            {
                                supr: "delta", hideLabel: true,
                                buttonText: "DELTA"
                            }
                        ]
                    ]
                },
            ]
        }
    ]
}));

// The display and controls are one section. Turning the display onto its side
// gives it the same stature as the three two-knob rows beside it.
const SuprChorusView = makePanelView((ctx) => [
    {
        sections: [{
            noLabelSlot: true,
            rowAlign: "center",
            rowGap: 8,
            rows: [[
                (
                    <div key="supr_chorus_display_rotated" style={{
                        position: "relative", width: 116, height: 248
                    }}>
                        <div style={{
                            position: "absolute", left: "50%", top: "50%",
                            transform: "translate(-50%, -50%) rotate(-90deg)"
                        }}>
                            <SuprChorusDisplay
                                instanceId={ctx.instanceId}
                                controls={ctx.controlValues} />
                        </div>
                    </div>
                ),
                {
                    panelGroup: "column",
                    items: [
                        {
                            panelGroup: "row",
                            items: ["rate", { supr: "depth", marks: "home" }]
                        },
                        {
                            panelGroup: "row",
                            items: [
                                { supr: "low", marks: "home" },
                                { supr: "tone", marks: "home" }
                            ]
                        },
                        {
                            panelGroup: "row",
                            items: [
                                { supr: "voices", step: 1, showReadout: true },
                                { supr: "mix", marks: "home" }
                            ]
                        }
                    ]
                }
            ]]
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

export class SuprClackViewFactory implements IControlViewFactory {
    uri: string = SUPR_CLACK_URI;
    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprClackView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
    }
}
