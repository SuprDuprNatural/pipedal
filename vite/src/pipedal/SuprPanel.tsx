// SuprPanel: a "console face" layout for Supr pedal views, in the spirit of
// the TooB Parametric EQ — one outer bordered unit with the sections nestled
// together inside it, separated by walls of the same weight as the outer
// border, with faded inline section labels. Sections hold rows of the
// standard control nodes that PluginControlView already built (we only
// re-arrange them).
//
// Layout model:
//   unit    = wrapping flex row of COLUMNS (4px #888 border, like TooB)
//   header  = optional full-width row across the top, above every column
//   column  = vertical stack of SECTIONS (4px left wall)
//   section = label slot + rows of controls (4px top wall between stacked
//             sections)
//
// The unit hugs its content (no section stretching — that's what makes the
// TooB EQ read as one dense instrument face). When the window is narrower
// than the content, whole columns wrap and each wrapped row centers.
//
// MIT license, (c) 2026 SuprPedals contributors.

import React, { ReactNode } from 'react';
import Typography from '@mui/material/Typography';
import { ControlGroup } from './PluginControlView';
import { PiPedalModel } from './PiPedalModel';
import SuprControl from './SuprControl';
import { SuprKnobMarks } from './SuprKnob';

// A row entry of { compactSelect: "symbol" } renders that port's dropdown
// narrow and unlabelled; see compactSelect() below.
export interface CompactSelectItem {
    compactSelect: string;
}

// A row entry of { supr: "symbol" } asks the shared dispatcher for the Supr
// rendering of that port. Unsupported types retain their stock node.
export interface SuprControlItem {
    supr: string;
    marks?: SuprKnobMarks;
    markCount?: number;
    step?: number;
    showReadout?: boolean;
    showPointer?: boolean;
    compact?: boolean;
    wide?: boolean;
    hideLabel?: boolean;
    buttonText?: string;
}

// A small piece of nested panel geometry. This lets a face place, for
// example, a meter above four knobs beside a vertical stack of three knobs,
// while the whole arrangement still belongs to one section (and therefore
// has no artificial wall through the middle).
export interface PanelGroupItem {
    panelGroup: "row" | "column";
    items: PanelItem[];
    label?: string;
    gap?: number;
    justify?: "flex-start" | "center" | "flex-end" | "space-between"
        | "space-around" | "space-evenly";
    align?: "flex-start" | "center" | "flex-end" | "stretch";
    stretch?: boolean;
    width?: number;
}

export type PanelItem =
    string | CompactSelectItem | SuprControlItem | PanelGroupItem | ReactNode;

export interface PanelSection {
    label?: string;
    // rows of port symbols (resolved via mapControlNodes), ready nodes, or
    // a compact-select request
    rows: PanelItem[][];
    // put the label under the rows instead of above (TooB alternates these)
    labelBottom?: boolean;
    // drop the empty label slot entirely. Only for a unit whose sections are
    // ALL unlabelled — the slot exists to keep knob rows aligned across
    // neighbouring sections, so removing it from one of a labelled set would
    // step that section's knobs out of line with the rest.
    noLabelSlot?: boolean;
    // Give the section room to breathe. A pedal face wants its controls
    // packed; a synth face wants each section to read as its own panel, and
    // that is mostly a matter of the space around them.
    roomy?: boolean;
    // Centre a short stack within a neighbouring taller strip.
    centerRows?: boolean;
    // Give every row an equal share of the section's height.
    spreadRows?: boolean;
    // Alignment and spacing of the immediate rows. Nested PanelGroupItems
    // handle the more detailed geometry inside them.
    rowAlign?: "flex-start" | "center" | "flex-end" | "stretch";
    rowGap?: number;
}

export interface PanelColumn {
    sections: PanelSection[];
    // kept for callers; content-hugging layout ignores extra growth
    grow?: number;
}

const WALL = "4px solid #888";
const LABEL_SLOT = 20;

// A dropdown whose label is redundant — the control beside it already says
// what it belongs to — rendered narrow and without its caption. The standard
// control is 160px wide with a 140px Select, sized for the longest label any
// plugin might use; ours are four waveform names, and "Triangle" is the
// longest. Done in CSS over the standard node rather than by rendering our
// own Select, so the value binding, theming and disabled states stay exactly
// the host's.
//
// Selected by STRUCTURE, not by class name: PluginControl's styles come from
// emotion, so the generated names are hashes like `css-1icnxvz` with nothing
// of the source key left in them. The frame's first child is the caption and
// its second is the control, and that is what these rules rely on.
//
// The caption is hidden with `visibility`, not `display`: it still has to
// occupy its 20px, or the dropdown rides up and stops lining up with the
// captioned knobs sitting beside it in the same row.
// Sized from the content rather than by eye: "Triangle" is the longest of the
// four waveform names and measures 50px at the control's 14px Roboto, and the
// dropdown arrow takes a further 24px. 76px fits both, which puts the column
// at the same 80-odd pixels as the knobs beside it so the dropdown stops
// stretching the section it sits in.
export const COMPACT_SELECT_CLASS = "supr-compact-select";
const compactSelectCss = `
.${COMPACT_SELECT_CLASS} > div { width: 84px !important; }
.${COMPACT_SELECT_CLASS} > div > div:first-of-type { visibility: hidden !important; }
.${COMPACT_SELECT_CLASS} .MuiInputBase-root { width: 76px !important; }
`;

export function compactSelect(node: ReactNode, key: string): ReactNode {
    return (
        <div key={key} className={COMPACT_SELECT_CLASS}>
            <style>{compactSelectCss}</style>
            {node}
        </div>
    );
}

// A file browser whose caption is redundant — the section it sits in
// already names it. Same structural selector as compactSelect (the frame's
// first child is its caption), but hidden with `display` rather than
// `visibility`: this one is the top row of its own column rather than a
// control sharing a row with captioned knobs, so there is no neighbour to
// stay level with and the reserved slot is just a gap. The label stays in
// the plugin's data, because it is what titles the file dialog.
export const CAPTIONLESS_FILE_CLASS = "supr-captionless-file";
const captionlessFileCss = `
.${CAPTIONLESS_FILE_CLASS} > div > div:first-of-type { display: none !important; }
`;

export function captionlessFile(node: ReactNode, key: string): ReactNode {
    return (
        <div key={key} className={CAPTIONLESS_FILE_CLASS}>
            <style>{captionlessFileCss}</style>
            {node}
        </div>
    );
}

// Build symbol -> control-node map from the (ReactNode | ControlGroup)[] that
// PluginControlView hands to modifyControls. Grouped controls carry their
// port indexes; ungrouped ones are matched positionally against the plugin's
// visible ungrouped controls in index order.
export function mapControlNodes(
    model: PiPedalModel,
    uri: string,
    controls: (ReactNode | ControlGroup)[]
): { [symbol: string]: ReactNode } {
    let result: { [symbol: string]: ReactNode } = {};
    const plugin = model.getUiPlugin(uri);
    if (!plugin)
        return result;

    let indexToSymbol: { [index: number]: string } = {};
    for (let control of plugin.controls) {
        indexToSymbol[control.index] = control.symbol;
    }
    let ungroupedSymbols: string[] = plugin.controls
        .filter((c) => c.is_input && !c.not_on_gui && c.port_group === "")
        .sort((a, b) => a.index - b.index)
        .map((c) => c.symbol);

    let ungroupedSeen = 0;
    for (let item of controls) {
        if (item instanceof ControlGroup) {
            for (let i = 0; i < item.controls.length; ++i) {
                const symbol = indexToSymbol[item.indexes[i]];
                if (symbol !== undefined) {
                    result[symbol] = item.controls[i];
                }
            }
        } else {
            if (ungroupedSeen < ungroupedSymbols.length) {
                result[ungroupedSymbols[ungroupedSeen++]] = item;
            }
        }
    }
    return result;
}

// The nodes in a ControlGroup that are not ports — file browsers and
// frequency plots, which PluginControlView splices into the group alongside
// the real controls. mapControlNodes drops them, because they have no port
// symbol to key on; this returns them in declaration order instead.
//
// It works by index: a file property's lv2:index is only used for ordering
// within its group, so declaring them above the port range (SuprNAM uses 100
// upwards) guarantees they never collide with a real port index. Without that
// a file property at index 0 would be mistaken for whichever port is at 0 and
// would quietly replace it.
export function mapExtraNodes(
    model: PiPedalModel,
    uri: string,
    controls: (ReactNode | ControlGroup)[]
): ReactNode[] {
    const plugin = model.getUiPlugin(uri);
    if (!plugin)
        return [];

    let portIndexes = new Set<number>();
    for (let control of plugin.controls) {
        portIndexes.add(control.index);
    }

    let extras: ReactNode[] = [];
    for (let item of controls) {
        if (item instanceof ControlGroup) {
            for (let i = 0; i < item.controls.length; ++i) {
                if (!portIndexes.has(item.indexes[i])) {
                    extras.push(item.controls[i]);
                }
            }
        }
    }
    return extras;
}

function sectionLabel(section: PanelSection): ReactNode {
    // A fixed-height slot whether or not there's text, so knob rows align
    // across neighbouring sections.
    if (section.noLabelSlot && !section.label)
        return null;
    return (
        <div style={{ height: LABEL_SLOT, display: "flex", alignItems: "center" }}>
            {section.label && (
                <Typography variant="body1" noWrap style={{
                    fontSize: "0.85em", fontWeight: 700,
                    opacity: 0.4, textAlign: "center"
                }}>{section.label}</Typography>
            )}
        </div>
    );
}

function groupLabel(label: string): ReactNode {
    return (
        <div style={{
            height: LABEL_SLOT, display: "flex", alignItems: "center",
            justifyContent: "center", flex: "0 0 auto"
        }}>
            <Typography variant="body1" noWrap style={{
                fontSize: "0.85em", fontWeight: 700,
                opacity: 0.4, textAlign: "center"
            }}>{label}</Typography>
        </div>
    );
}

interface SuprControlContext {
    instanceId: number;
    uri: string;
    controlValues: { [symbol: string]: number };
}

function renderPanelItem(item: PanelItem,
    nodes: { [symbol: string]: ReactNode }, key: string,
    suprContext?: SuprControlContext): ReactNode {
    if (item && typeof item === "object"
        && "panelGroup" in (item as object)) {
        const group = item as PanelGroupItem;
        return (
            <div key={key} style={{
                display: "flex",
                flexFlow: group.panelGroup === "row"
                    ? "row nowrap" : "column nowrap",
                justifyContent: group.justify ?? "center",
                alignItems: group.align ?? "center",
                alignSelf: group.stretch ? "stretch" : undefined,
                gap: group.gap,
                width: group.width,
                flex: "0 0 auto"
            }}>
                {group.label && groupLabel(group.label)}
                {group.items.map((child, index) =>
                    renderPanelItem(child, nodes, `${key}_${index}`, suprContext)
                )}
            </div>
        );
    }

    let content: ReactNode;
    if (typeof item === "string") {
        content = suprContext ? (
            <SuprControl
                instanceId={suprContext.instanceId}
                uri={suprContext.uri}
                symbol={item}
                value={suprContext.controlValues[item]}
                fallback={nodes[item]} />
        ) : nodes[item];
    } else if (item && typeof item === "object"
        && "compactSelect" in (item as object)) {
        const sym = (item as CompactSelectItem).compactSelect;
        content = suprContext ? (
            <SuprControl
                instanceId={suprContext.instanceId}
                uri={suprContext.uri}
                symbol={sym}
                value={suprContext.controlValues[sym]}
                fallback={compactSelect(nodes[sym], "cs_" + sym)}
                compact />
        ) : compactSelect(nodes[sym], "cs_" + sym);
    } else if (item && typeof item === "object"
        && "supr" in (item as object)) {
        const request = item as SuprControlItem;
        content = suprContext ? (
            <SuprControl
                instanceId={suprContext.instanceId}
                uri={suprContext.uri}
                symbol={request.supr}
                value={suprContext.controlValues[request.supr]}
                fallback={nodes[request.supr]}
                marks={request.marks}
                markCount={request.markCount}
                step={request.step}
                showReadout={request.showReadout}
                showPointer={request.showPointer}
                compact={request.compact}
                wide={request.wide}
                hideLabel={request.hideLabel}
                buttonText={request.buttonText} />
        ) : nodes[request.supr];
    } else {
        content = item as ReactNode;
    }
    return (
        <div key={key} style={{ flex: "0 0 auto" }}>
            {content}
        </div>
    );
}

function renderSection(section: PanelSection,
    nodes: { [symbol: string]: ReactNode }, key: number, first: boolean,
    suprContext?: SuprControlContext): ReactNode {
    return (
        <div key={key} style={{
            flex: "1 0 auto",
            display: "flex", flexFlow: "column nowrap",
            // Top-aligned, not centred: in a stacked face the columns hold
            // different numbers of controls, and centring floats each
            // section's label to a different height, which reads as broken
            // even though the grouping is right. Where the columns are the
            // same height this is identical to centring.
            justifyContent: section.centerRows ? "center" : "flex-start",
            alignItems: "center",
            borderTop: first ? undefined : WALL,
            paddingLeft: section.roomy ? 14 : 6,
            paddingRight: section.roomy ? 14 : 6,
            paddingTop: section.roomy ? 8 : 2,
            paddingBottom: section.roomy ? 14 : 4
        }}>
            {!section.labelBottom && sectionLabel(section)}
            {section.rows.map((row, ri) => (
                <div key={ri} style={{
                    display: "flex", flexFlow: "row nowrap",
                    justifyContent: "center",
                    alignItems: section.rowAlign
                        ?? (section.spreadRows ? "center" : "flex-start"),
                    flex: section.spreadRows ? "1 1 0" : undefined,
                    gap: section.rowGap
                }}>
                    {row.map((item, ci) =>
                        renderPanelItem(item, nodes, `${key}_${ri}_${ci}`, suprContext)
                    )}
                </div>
            ))}
            {section.labelBottom && sectionLabel(section)}
        </div>
    );
}

interface SuprPanelUnitProps {
    columns: PanelColumn[];
    nodes: { [symbol: string]: ReactNode };
    instanceId?: number;
    uri?: string;
    controlValues?: { [symbol: string]: number };
    // Full-width row across the top of the unit, above the columns — for a
    // readout that belongs to the whole pedal rather than to one section.
    // The columns' own top walls become the rule beneath it.
    header?: ReactNode;
    // read by PluginControlView: opt out of the fixed-height control slot
    tallControl?: boolean;
}

// The landscape control grid is width:fit-content, so percentage max-widths
// can't stop the unit from running off screen — cap it against the window
// width instead (the TooB Parametric EQ view does the same), leaving room
// for the view's side VU bars and paddings. Columns then wrap into
// centered rows.
const PANEL_WINDOW_MARGIN = 150;

export class SuprPanelUnit extends React.Component<SuprPanelUnitProps, { windowWidth: number }> {
    constructor(props: SuprPanelUnitProps) {
        super(props);
        this.state = { windowWidth: document.documentElement.clientWidth };
        this.handleResize = this.handleResize.bind(this);
    }
    handleResize() {
        this.setState({ windowWidth: document.documentElement.clientWidth });
    }
    componentDidMount() {
        window.addEventListener('resize', this.handleResize);
    }
    componentWillUnmount() {
        window.removeEventListener('resize', this.handleResize);
    }

    render() {
        const maxWidth = Math.max(this.state.windowWidth - PANEL_WINDOW_MARGIN, 300);
        const suprContext: SuprControlContext | undefined =
            this.props.instanceId !== undefined
                && this.props.uri !== undefined
                && this.props.controlValues !== undefined
                ? {
                    instanceId: this.props.instanceId,
                    uri: this.props.uri,
                    controlValues: this.props.controlValues
                }
                : undefined;
        return (
            <div style={{
                display: "flex", flexFlow: "row nowrap",
                justifyContent: "center", width: "100%", maxWidth: maxWidth
            }}>
                <div style={{
                    // A COLUMN: the optional header stacks above the row of
                    // panel columns. Deliberately not one row-wrap container
                    // with a `flex: 0 0 100%` header — a percentage basis
                    // resolves against a container that is itself sizing to
                    // its content, and the circularity blows the unit out
                    // far wider than its columns.
                    flex: "0 1 auto", maxWidth: maxWidth,
                    display: "flex", flexFlow: "column nowrap",
                    alignItems: "stretch",
                    border: "4px #888 solid", borderRadius: 8,
                    overflow: "hidden", marginBottom: 8
                }}>
                    {this.props.header && (
                        <div style={{
                            display: "flex", justifyContent: "center",
                            alignItems: "center",
                            // the columns below pull up 4px onto their own
                            // top wall, which is what draws the rule here
                            paddingTop: 8, paddingBottom: 10
                        }}>
                            {this.props.header}
                        </div>
                    )}
                    <div style={{
                        display: "flex", flexFlow: "row wrap",
                        alignItems: "stretch", justifyContent: "center"
                    }}>
                        {this.props.columns.map((column, i) => (
                            <div key={i} style={{
                                // content-hugging: the unit is exactly as wide
                                // as its sections, like the TooB EQ face
                                flex: "0 0 auto",
                                display: "flex", flexFlow: "column nowrap",
                                alignItems: "stretch",
                                borderLeft: i === 0 ? undefined : WALL,
                                // merges into the outer border (or the header's
                                // bottom padding) on the first row; separates
                                // wrapped rows below it
                                marginTop: -4, borderTop: WALL
                            }}>
                                {column.sections.map((section, si) =>
                                    renderSection(
                                        section, this.props.nodes, si, si === 0,
                                        suprContext
                                    ))}
                            </div>
                        ))}
                    </div>
                </div>
            </div>
        );
    }
}
