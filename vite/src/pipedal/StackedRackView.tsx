// Copyright (c) Robin E.R. Davies
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

import React from 'react';
import Switch from '@mui/material/Switch';
import Typography from '@mui/material/Typography';
import { Theme } from '@mui/material/styles';

import { Pedalboard, PedalboardItem } from './Pedalboard';
import { PiPedalModelFactory } from './PiPedalModel';
import { GetControlView } from './ControlViewFactory';
import { FitContentContext } from './PluginControlView';
import { isDarkMode } from './DarkMode';

interface StackedRackViewProps {
    pedalboard: Pedalboard;
    selectedId: number;
    displayAuthor: boolean;
    theme: Theme;
    onSelectionChanged: (instanceId: number) => void;
}

function StackedRackView(props: StackedRackViewProps) {
    const model = PiPedalModelFactory.getInstance();

    function renderRackItem(item: PedalboardItem): React.ReactNode {
        const uiPlugin = item.isSplit() ? null : model.getUiPlugin(item.uri);
        let title: string;
        if (item.isSplit()) {
            title = "Split";
        } else {
            title = item.title || uiPlugin?.name || item.pluginName || "(empty)";
        }

        const missing = !item.isSplit() && !item.isEmpty() && !uiPlugin;
        const selected = item.instanceId === props.selectedId;
        const fitContent = !item.isSplit() && !missing && !item.isEmpty();
        const controlHeight = (missing || item.isEmpty()) ? 110 : 190;
        const borderColor = selected
            ? props.theme.palette.primary.main
            : (isDarkMode() ? "#444" : "#DDD");

        return (
            <div
                key={item.instanceId}
                style={{
                    border: "1px solid " + borderColor,
                    borderLeft: (selected ? "3px" : "1px") + " solid " + borderColor,
                    borderRadius: 8,
                    margin: "8px 12px",
                    overflow: "hidden"
                }}
            >
                <div
                    onClick={() => props.onSelectionChanged(item.instanceId)}
                    style={{
                        display: "flex",
                        flexFlow: "row nowrap",
                        alignItems: "center",
                        height: 40,
                        paddingLeft: 8,
                        paddingRight: 16,
                        cursor: "pointer",
                        background: isDarkMode()
                            ? "rgba(255,255,255,0.06)"
                            : "rgba(0,0,0,0.04)"
                    }}
                >
                    <div style={{ flex: "0 0 auto", width: 56 }}>
                        {uiPlugin && (
                            <Switch
                                color="secondary"
                                size="small"
                                checked={item.isEnabled}
                                inputProps={{ "aria-label": `${title} bypass` }}
                                onClick={(event) => event.stopPropagation()}
                                onChange={(event) => {
                                    model.setPedalboardItemEnabled(
                                        item.instanceId,
                                        event.target.checked
                                    );
                                }}
                            />
                        )}
                    </div>
                    <Typography
                        noWrap
                        style={{
                            flex: "0 1 auto",
                            minWidth: 0,
                            marginRight: 8,
                            fontSize: "1.1rem",
                            fontWeight: 700,
                            textOverflow: "ellipsis",
                            whiteSpace: "nowrap",
                            opacity: 0.75
                        }}
                    >
                        {title}
                    </Typography>
                    {props.displayAuthor && uiPlugin && item.title && (
                        <Typography
                            noWrap
                            style={{
                                flex: "0 1 auto",
                                minWidth: 0,
                                marginRight: 8,
                                fontSize: "0.8rem",
                                fontWeight: 500,
                                textOverflow: "ellipsis",
                                whiteSpace: "nowrap",
                                opacity: 0.75
                            }}
                        >
                            {uiPlugin.name}
                        </Typography>
                    )}
                </div>
                <div
                    style={{
                        position: "relative",
                        width: "100%",
                        height: fitContent ? undefined : controlHeight
                    }}
                >
                    {missing ? (
                        <div style={{ marginLeft: 40, marginTop: 20, marginRight: 20 }}>
                            <Typography variant="body1" color="error">
                                Plugin is not installed.
                            </Typography>
                            <Typography variant="body2" style={{ overflowWrap: "anywhere" }}>
                                {item.uri}
                            </Typography>
                        </div>
                    ) : (
                        <FitContentContext.Provider value={fitContent}>
                            {GetControlView(item, false, () => { })}
                        </FitContentContext.Provider>
                    )}
                </div>
            </div>
        );
    }

    const items: PedalboardItem[] = [];
    for (const item of props.pedalboard.itemsGenerator()) {
        if (!item.isStart() && !item.isEnd()) {
            items.push(item);
        }
    }

    return (
        <div
            style={{
                width: "100%",
                height: "100%",
                overflowY: "auto",
                overflowX: "hidden"
            }}
        >
            {items.map(item => renderRackItem(item))}
            <div style={{ height: 24 }} />
        </div>
    );
}

export default StackedRackView;
