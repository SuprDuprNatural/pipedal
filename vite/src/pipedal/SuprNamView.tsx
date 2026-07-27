// SuprNamView: the control view for SuprNAM.
//
// SuprNAM has three model slots and seven routings, which is more state than a
// flat wall of knobs communicates. Two things fix that:
//
//  - A SIGNAL FLOW DIAGRAM at the top, drawn from the current Routing value.
//    "(A ∥ B) → C" is a perfectly good label once you already know what the
//    plugin does; a picture of two boxes summing into a third is better when
//    you do not. Slots the routing does not use are dimmed, and slots with no
//    model loaded are drawn as empty, so the diagram doubles as a status
//    display: what you see is what is actually running.
//
//  - PER-SLOT FILTERS BEHIND A TOGGLE. Four filters times three slots is
//    twelve controls that most people will never touch, and putting them on
//    screen next to Level and Blend would bury the controls that matter. They
//    collapse into a "Filters" button per slot.
//
// MIT license, (c) 2026 SuprDuprNatural.

import React, { ReactNode } from 'react';
import { Theme } from '@mui/material/styles';
import WithStyles from './WithStyles';
import { createStyles } from './WithStyles';
import { withStyles } from "tss-react/mui";
import Typography from '@mui/material/Typography';
import ButtonBase from '@mui/material/ButtonBase';
import IControlViewFactory from './IControlViewFactory';
import { PiPedalModel, PiPedalModelFactory } from "./PiPedalModel";
import { PedalboardItem } from './Pedalboard';
import PluginControlView, { ICustomizationHost, ControlGroup, ControlViewCustomization } from './PluginControlView';
import { PanelColumn, PanelSection, SuprPanelUnit, mapControlNodes, mapExtraNodes } from './SuprPanel';

const SUPR_NAM_URI = "https://suprduprnatural.github.io/supr-pedals/nam";

const MODEL_A_PROPERTY = SUPR_NAM_URI + "#modelA";
const MODEL_B_PROPERTY = SUPR_NAM_URI + "#modelB";
const MODEL_C_PROPERTY = SUPR_NAM_URI + "#modelC";
const MODEL_PROPERTIES = [MODEL_A_PROPERTY, MODEL_B_PROPERTY, MODEL_C_PROPERTY];

const MODEL_INFO_PROPERTY = SUPR_NAM_URI + "#modelInfo";

const SLOT_NAMES = ["A", "B", "C"];

// Layout of the modelInfo vector. Must match ModelInfoOffset / SlotInfoOffset
// in SuprPedals/src/nam/SuprNam.cpp.
const INFO_VERSION = 0;
const INFO_OVERLOAD = 1;
const INFO_SLOT_BASE = 3;
const INFO_FLOATS_PER_SLOT = 7;
const SLOT_LOADED = 0;
const SLOT_HAS_QUALITY = 1;
const SLOT_SAMPLE_RATE = 2;
const SLOT_LOUDNESS_DB = 3;
const SLOT_HAS_LOUDNESS = 5;
const SLOT_HAS_INPUT_LEVEL = 6;

interface SlotInfo {
    loaded: boolean;
    hasQuality: boolean;
    sampleRate: number;
    loudnessDb: number;
    hasLoudness: boolean;
    hasInputLevel: boolean;
}

function emptySlotInfo(): SlotInfo {
    return {
        loaded: false, hasQuality: false, sampleRate: 0,
        loudnessDb: 0, hasLoudness: false, hasInputLevel: false
    };
}

function decodeModelInfo(values: number[]): SlotInfo[] {
    let slots = [emptySlotInfo(), emptySlotInfo(), emptySlotInfo()];
    if (!values || values.length < INFO_SLOT_BASE || values[INFO_VERSION] !== 1)
        return slots;
    for (let i = 0; i < 3; ++i) {
        const base = INFO_SLOT_BASE + i * INFO_FLOATS_PER_SLOT;
        if (base + INFO_FLOATS_PER_SLOT > values.length)
            break;
        slots[i] = {
            loaded: values[base + SLOT_LOADED] !== 0,
            hasQuality: values[base + SLOT_HAS_QUALITY] !== 0,
            sampleRate: values[base + SLOT_SAMPLE_RATE],
            loudnessDb: values[base + SLOT_LOUDNESS_DB],
            hasLoudness: values[base + SLOT_HAS_LOUDNESS] !== 0,
            hasInputLevel: values[base + SLOT_HAS_INPUT_LEVEL] !== 0,
        };
    }
    return slots;
}

// Must match enum class Routing in SuprPedals/src/NamDsp.h.
enum Routing {
    Single = 0,
    Parallel2 = 1,
    Series2 = 2,
    Parallel3 = 3,
    SeriesParallel = 4,
    ParallelSeries = 5,
    Series3 = 6,
}

// Stages, matching planFor() in NamDsp.h. Each entry is a list of stages, and
// each stage is the slots that run in parallel within it.
const ROUTING_PLANS: number[][][] = [
    [[0]],              // A
    [[0, 1]],           // A ∥ B
    [[0], [1]],         // A → B
    [[0, 1, 2]],        // A ∥ B ∥ C
    [[0], [1, 2]],      // A → (B ∥ C)
    [[0, 1], [2]],      // (A ∥ B) → C
    [[0], [1], [2]],    // A → B → C
];

function planFor(routing: number): number[][] {
    return ROUTING_PLANS[Math.max(0, Math.min(ROUTING_PLANS.length - 1, Math.round(routing)))];
}

function slotsUsedBy(routing: number): boolean[] {
    let used = [false, false, false];
    for (let stage of planFor(routing)) {
        for (let slot of stage) {
            used[slot] = true;
        }
    }
    return used;
}

// The pair Blend crossfades: the first two-wide stage. Matches
// blendStageIndex() in NamDsp.h, which relies on there being at most one.
function blendPair(routing: number): number[] | null {
    for (let stage of planFor(routing)) {
        if (stage.length === 2)
            return stage;
    }
    return null;
}

// ---------------------------------------------------------------------------
// Signal flow diagram
// ---------------------------------------------------------------------------

const BOX_W = 42;
const BOX_H = 26;
const STAGE_GAP = 34;
const ROW_GAP = 8;
const FLOW_PAD = 10;

interface SuprNamFlowProps {
    routing: number;
    loaded: boolean[];
    threaded: boolean;
}

function SuprNamFlow(props: SuprNamFlowProps) {
    const plan = planFor(props.routing);
    const rows = Math.max(...plan.map((stage) => stage.length));

    const width = FLOW_PAD * 2 + plan.length * BOX_W + (plan.length + 1) * STAGE_GAP;
    const height = FLOW_PAD * 2 + rows * BOX_H + (rows - 1) * ROW_GAP;
    const midY = height / 2;

    const active = "#9ecbff";
    const idle = "#666";
    const wire = "#888";

    let elements: ReactNode[] = [];
    let key = 0;

    // Left and right stubs, so the diagram reads as an insert rather than
    // three boxes floating on their own.
    elements.push(<line key={key++} x1={2} y1={midY} x2={FLOW_PAD + STAGE_GAP} y2={midY}
        stroke={wire} strokeWidth={1.5} />);
    elements.push(<line key={key++} x1={width - FLOW_PAD - STAGE_GAP} y1={midY}
        x2={width - 2} y2={midY} stroke={wire} strokeWidth={1.5} />);

    plan.forEach((stage, stageIndex) => {
        const x = FLOW_PAD + STAGE_GAP + stageIndex * (BOX_W + STAGE_GAP);
        const stageHeight = stage.length * BOX_H + (stage.length - 1) * ROW_GAP;
        const top = midY - stageHeight / 2;

        // Split and sum nodes for a parallel stage: the fan-out on the left,
        // the fan-in on the right.
        if (stage.length > 1) {
            const firstY = top + BOX_H / 2;
            const lastY = top + stageHeight - BOX_H / 2;
            elements.push(<line key={key++} x1={x - STAGE_GAP / 2} y1={firstY}
                x2={x - STAGE_GAP / 2} y2={lastY} stroke={wire} strokeWidth={1.5} />);
            elements.push(<line key={key++} x1={x + BOX_W + STAGE_GAP / 2} y1={firstY}
                x2={x + BOX_W + STAGE_GAP / 2} y2={lastY} stroke={wire} strokeWidth={1.5} />);
        }

        stage.forEach((slot, row) => {
            const y = top + row * (BOX_H + ROW_GAP);
            const cy = y + BOX_H / 2;
            const on = props.loaded[slot];

            elements.push(<line key={key++} x1={x - STAGE_GAP / 2} y1={cy} x2={x} y2={cy}
                stroke={wire} strokeWidth={1.5} />);
            elements.push(<line key={key++} x1={x + BOX_W} y1={cy}
                x2={x + BOX_W + STAGE_GAP / 2} y2={cy} stroke={wire} strokeWidth={1.5} />);

            elements.push(
                <rect key={key++} x={x} y={y} width={BOX_W} height={BOX_H} rx={4}
                    fill={on ? "rgba(158,203,255,0.14)" : "none"}
                    stroke={on ? active : idle}
                    strokeWidth={1.5}
                    strokeDasharray={on ? undefined : "3 3"} />);
            elements.push(
                <text key={key++} x={x + BOX_W / 2} y={cy + 4} textAnchor="middle"
                    fill={on ? active : idle} fontSize={13} fontWeight={600}
                    fontFamily="inherit">
                    {SLOT_NAMES[slot]}
                </text>);
        });
    });

    return (
        <div style={{ display: "flex", flexFlow: "column nowrap", alignItems: "center" }}>
            <svg width={width} height={height} style={{ display: "block" }}>
                {elements}
            </svg>
            <Typography variant="caption" style={{ opacity: 0.6, marginTop: 2 }}>
                {props.threaded ? "threaded · 1 block latency" : "inline · no latency"}
            </Typography>
        </div>
    );
}

// ---------------------------------------------------------------------------
// The view
// ---------------------------------------------------------------------------

const styles = (theme: Theme) => createStyles({});

interface SuprNamViewProps extends WithStyles<typeof styles> {
    instanceId: number;
    item: PedalboardItem;
}

interface SuprNamViewState {
    // One expandable filter section per slot.
    filtersOpen: boolean[];
    slotInfo: SlotInfo[];
    overload: boolean;
    revision: number;
}

const SuprNamView =
    withStyles(
        class extends React.Component<SuprNamViewProps, SuprNamViewState>
            implements ControlViewCustomization {
            model: PiPedalModel;
            customizationId: number = 1;

            constructor(props: SuprNamViewProps) {
                super(props);
                this.model = PiPedalModelFactory.getInstance();
                this.state = {
                    filtersOpen: [false, false, false],
                    slotInfo: [emptySlotInfo(), emptySlotInfo(), emptySlotInfo()],
                    overload: false,
                    revision: 0
                };
                this.onPedalboardChanged = this.onPedalboardChanged.bind(this);
                this.handleModelInfo = this.handleModelInfo.bind(this);
            }

            fullScreen() { return false; }

            // The plugin publishes what it found in each model file: whether it
            // carries a loudness figure, whether it says what input level it
            // was trained at, whether it is slimmable. Most models in the wild
            // record neither level, which means the Calibration controls are
            // quietly doing nothing — the UI has to say so, or a setting that
            // is inert looks exactly like one that is working.
            private listenHandle: any = null;
            private subscribedId: number = -1;

            handleModelInfo(atomData: any) {
                if (atomData && atomData.otype_ === "Vector" && atomData.value) {
                    const values = atomData.value as number[];
                    this.setState({
                        slotInfo: decodeModelInfo(values),
                        overload: values.length > INFO_OVERLOAD && values[INFO_OVERLOAD] !== 0
                    });
                }
            }

            private subscribeToModelInfo() {
                this.subscribedId = this.props.instanceId;
                this.listenHandle = this.model.monitorPatchProperty(
                    this.props.instanceId, MODEL_INFO_PROPERTY,
                    (instanceId, propertyUri, atomData) => { this.handleModelInfo(atomData); });
                this.model.getPatchProperty(this.props.instanceId, MODEL_INFO_PROPERTY)
                    .then((atomData) => { this.handleModelInfo(atomData); })
                    .catch(() => { /* not loaded yet; the notification will arrive */ });
            }

            private unsubscribeFromModelInfo() {
                if (this.listenHandle) {
                    this.model.cancelMonitorPatchProperty(this.listenHandle);
                    this.listenHandle = null;
                }
                this.subscribedId = -1;
            }

            // The diagram is derived from Routing and from which slots have a
            // model, both of which live in the pedalboard rather than in this
            // component, so it has to redraw when the pedalboard does.
            onPedalboardChanged() {
                this.setState((s) => ({ revision: s.revision + 1 }));
            }

            componentDidMount() {
                this.model.pedalboard.addOnChangedHandler(this.onPedalboardChanged);
                this.subscribeToModelInfo();
            }
            componentWillUnmount() {
                this.model.pedalboard.removeOnChangedHandler(this.onPedalboardChanged);
                this.unsubscribeFromModelInfo();
            }
            componentDidUpdate() {
                if (this.props.instanceId !== this.subscribedId) {
                    this.unsubscribeFromModelInfo();
                    this.subscribeToModelInfo();
                }
            }

            private controlValues(): { [symbol: string]: number } {
                let values: { [symbol: string]: number } = {};
                try {
                    const item = this.model.pedalboard.get().getItem(this.props.instanceId);
                    for (let cv of item.controlValues) {
                        values[cv.key] = cv.value;
                    }
                } catch (e) {
                    // start/end/missing items: fall back to defaults
                }
                return values;
            }

            // A slot counts as loaded when its patch property holds a real
            // path. PiPedal stores "null" for an empty one.
            private loadedSlots(): boolean[] {
                let loaded = [false, false, false];
                try {
                    const item = this.model.pedalboard.get().getItem(this.props.instanceId);
                    for (let i = 0; i < MODEL_PROPERTIES.length; ++i) {
                        const path = item.pathProperties[MODEL_PROPERTIES[i]];
                        loaded[i] = !!path && path !== "null" && path !== "";
                    }
                } catch (e) {
                    // leave them all empty
                }
                return loaded;
            }

            private filtersButton(slot: number): ReactNode {
                const open = this.state.filtersOpen[slot];
                return (
                    <ButtonBase key={"filters" + slot}
                        onClick={() => {
                            this.setState((s) => {
                                let next = [...s.filtersOpen];
                                next[slot] = !next[slot];
                                return { filtersOpen: next };
                            });
                        }}
                        style={{
                            marginTop: 4, marginBottom: 2, padding: "2px 8px", borderRadius: 4,
                            border: "1px solid #666", opacity: open ? 1.0 : 0.65
                        }}>
                        <Typography variant="caption" noWrap style={{ fontSize: "0.75em" }}>
                            {(open ? "▾ " : "▸ ") + "Filters"}
                        </Typography>
                    </ButtonBase>
                );
            }

            private slotColumn(slot: number, modelNode: ReactNode, used: boolean): PanelColumn {
                const name = SLOT_NAMES[slot];
                let sections: PanelSection[] = [];

                const info = this.state.slotInfo[slot];

                let rows: (string | ReactNode)[][] = [];
                if (modelNode)
                    rows.push([modelNode]);
                // Drive first: on a plugin running models side by side it is
                // the control you reach for before any of the others.
                rows.push(["drive" + name, "level" + name]);
                rows.push(["slim" + name, "polarity" + name, "delay" + name]);

                if (info.loaded && (!info.hasInputLevel || !info.hasLoudness)) {
                    const missing = !info.hasInputLevel && !info.hasLoudness
                        ? "no level metadata"
                        : (!info.hasInputLevel ? "no input level" : "no loudness");
                    rows.push([(
                        <Typography key={"uncal" + slot} variant="caption" noWrap
                            style={{ opacity: 0.55, fontSize: "0.7em", display: "block" }}
                            title={"This model does not record the level it was trained at, so "
                                + "the matching Calibration control is inert for it. Set Drive "
                                + name + " by ear instead."}>
                            {"⚠ " + missing + " — set Drive by ear"}
                        </Typography>
                    )]);
                }

                rows.push([this.filtersButton(slot)]);

                sections.push({
                    // A used slot gets its letter; an unused one says so, which
                    // is less confusing than a column that simply does nothing.
                    label: used ? name : name + " (unused)",
                    rows: rows
                });

                if (this.state.filtersOpen[slot]) {
                    sections.push({
                        label: "Filters",
                        rows: [
                            ["inHp" + name, "inLp" + name],
                            ["outHp" + name, "outLp" + name],
                        ]
                    });
                }
                return { sections: sections };
            }

            modifyControls(host: ICustomizationHost,
                controls: (React.ReactNode | ControlGroup)[]): (React.ReactNode | ControlGroup)[] {
                const nodes = mapControlNodes(this.model, this.props.item.uri, controls);
                // The three model browsers, in slot order. They are not ports,
                // so mapControlNodes cannot see them.
                const modelNodes = mapExtraNodes(this.model, this.props.item.uri, controls);

                const values = this.controlValues();
                const routing = values["routing"] ?? 0;
                const threaded = (values["threaded"] ?? 0) !== 0;
                const loaded = this.loadedSlots();
                const used = slotsUsedBy(routing);
                const pair = blendPair(routing);

                let columns: PanelColumn[] = [];

                // Routing, with the flow diagram above it. Blend is only
                // meaningful when there is a pair to crossfade, so it is
                // labelled with the actual letters and dropped when there is
                // not — a dead knob is worse than no knob.
                let routingRows: (string | ReactNode)[][] = [
                    [(<SuprNamFlow key="flow" routing={routing} loaded={loaded}
                        threaded={threaded} />)],
                    ["routing"],
                ];
                if (pair) {
                    routingRows.push(["blend", "mixLaw"]);
                } else {
                    routingRows.push([(
                        <Typography key="noblend" variant="caption"
                            style={{ opacity: 0.55, maxWidth: 190, display: "block" }}>
                            {planFor(routing).some((s) => s.length > 2)
                                ? "Three-up: balance with the Level trims."
                                : "No parallel pair to blend."}
                        </Typography>
                    )]);
                }
                columns.push({
                    sections: [{
                        label: pair
                            ? "Routing  ·  Blend " + SLOT_NAMES[pair[0]] + "/" + SLOT_NAMES[pair[1]]
                            : "Routing",
                        rows: routingRows
                    }]
                });

                for (let slot = 0; slot < 3; ++slot) {
                    columns.push(this.slotColumn(slot, modelNodes[slot], used[slot]));
                }

                columns.push({
                    sections: [
                        { label: "Amp", rows: [["inputGain", "outputGain"], ["gate", "threaded"]] },
                        {
                            label: "Calibration",
                            rows: [["inputCalibrationMode", "outputCalibration"], ["calibration"]]
                        },
                    ]
                });

                return [(
                    <SuprPanelUnit key="supr_nam_panel" columns={columns} nodes={nodes}
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

export class SuprNamViewFactory implements IControlViewFactory {
    uri: string = SUPR_NAM_URI;

    Create(model: PiPedalModel, pedalboardItem: PedalboardItem): React.ReactNode {
        return (<SuprNamView instanceId={pedalboardItem.instanceId} item={pedalboardItem} />);
    }
}

export default SuprNamViewFactory;
