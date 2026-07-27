// SuprPanel: a "console face" layout for Supr pedal views, in the spirit of
// the TooB Parametric EQ — one outer bordered unit with the sections nestled
// together inside it, separated by walls of the same weight as the outer
// border, with faded inline section labels. Sections hold rows of the
// standard control nodes that PluginControlView already built (we only
// re-arrange them).
//
// Layout model:
//   unit    = wrapping flex row of COLUMNS (4px #888 border, like TooB)
//   column  = vertical stack of SECTIONS (4px left wall)
//   section = label slot + rows of controls (4px top wall between stacked
//             sections)
//
// The unit hugs its content (no section stretching — that's what makes the
// TooB EQ read as one dense instrument face). When the window is narrower
// than the content, whole columns wrap and each wrapped row centers.
//
// MIT license, (c) 2026 SuprDuprNatural.

import React, { ReactNode } from 'react';
import Typography from '@mui/material/Typography';
import { ControlGroup } from './PluginControlView';
import { PiPedalModel } from './PiPedalModel';

export interface PanelSection {
    label?: string;
    // rows of port symbols (resolved via mapControlNodes) or ready nodes
    rows: (string | ReactNode)[][];
    // put the label under the rows instead of above (TooB alternates these)
    labelBottom?: boolean;
}

export interface PanelColumn {
    sections: PanelSection[];
    // kept for callers; content-hugging layout ignores extra growth
    grow?: number;
}

const WALL = "4px solid #888";
const LABEL_SLOT = 20;

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

function renderSection(section: PanelSection,
    nodes: { [symbol: string]: ReactNode }, key: number, first: boolean): ReactNode {
    return (
        <div key={key} style={{
            flex: "1 0 auto",
            display: "flex", flexFlow: "column nowrap",
            justifyContent: "center", alignItems: "center",
            borderTop: first ? undefined : WALL,
            paddingLeft: 6, paddingRight: 6,
            paddingTop: 2, paddingBottom: 4
        }}>
            {!section.labelBottom && sectionLabel(section)}
            {section.rows.map((row, ri) => (
                <div key={ri} style={{
                    display: "flex", flexFlow: "row nowrap",
                    justifyContent: "center", alignItems: "flex-start"
                }}>
                    {row.map((item, ci) => (
                        <div key={ci} style={{ flex: "0 0 auto" }}>
                            {typeof item === "string" ? nodes[item] : item}
                        </div>
                    ))}
                </div>
            ))}
            {section.labelBottom && sectionLabel(section)}
        </div>
    );
}

interface SuprPanelUnitProps {
    columns: PanelColumn[];
    nodes: { [symbol: string]: ReactNode };
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
        return (
            <div style={{
                display: "flex", flexFlow: "row nowrap",
                justifyContent: "center", width: "100%", maxWidth: maxWidth
            }}>
                <div style={{
                    flex: "0 1 auto", maxWidth: maxWidth,
                    display: "flex", flexFlow: "row wrap",
                    alignItems: "stretch", justifyContent: "center",
                    border: "4px #888 solid", borderRadius: 8,
                    overflow: "hidden", marginBottom: 8
                }}>
                    {this.props.columns.map((column, i) => (
                        <div key={i} style={{
                            // content-hugging: the unit is exactly as wide as
                            // its sections, like the TooB EQ face
                            flex: "0 0 auto",
                            display: "flex", flexFlow: "column nowrap",
                            alignItems: "stretch",
                            borderLeft: i === 0 ? undefined : WALL,
                            // merges into the outer border on the first row;
                            // separates wrapped rows below it
                            marginTop: -4, borderTop: WALL
                        }}>
                            {column.sections.map((section, si) =>
                                renderSection(section, this.props.nodes, si, si === 0))}
                        </div>
                    ))}
                </div>
            </div>
        );
    }
}
