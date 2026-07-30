// Copyright (c) Robin E.R. Davies
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
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

import React, { useRef, useState } from 'react';
import { Theme } from '@mui/material/styles';
import Switch from '@mui/material/Switch';
import Typography from '@mui/material/Typography';

import { Pedalboard, PedalboardItem } from './Pedalboard';
import { PiPedalModelFactory } from './PiPedalModel';
import { GetControlView } from './ControlViewFactory';
import { FitContentContext } from './PluginControlView';
import Draggable from './Draggable';
import { isDarkMode } from './DarkMode';

const COLLAPSED_RACK_ITEM_WIDTH = 52;
const RACK_ITEM_HEADER_HEIGHT = 40;

interface RackViewProps {
    pedalboard: Pedalboard;
    selectedId: number;
    displayAuthor: boolean;
    enableStructureEditing: boolean;
    collapsedItems: ReadonlySet<number>;
    theme: Theme;
    onSelectionChanged: (instanceId: number) => void;
    onToggleCollapsed: (instanceId: number) => void;
}

interface RackDropTarget {
    instanceId: number;
    before: boolean;
}

function pointInRect(clientX: number, clientY: number, rect: DOMRect): boolean {
    return clientX >= rect.left
        && clientX <= rect.right
        && clientY >= rect.top
        && clientY <= rect.bottom;
}

function distanceSquaredToRect(clientX: number, clientY: number, rect: DOMRect): number {
    const dx = Math.max(rect.left - clientX, 0, clientX - rect.right);
    const dy = Math.max(rect.top - clientY, 0, clientY - rect.bottom);
    return dx * dx + dy * dy;
}

function getDropTarget(
    rackElement: HTMLDivElement,
    pedalboard: Pedalboard,
    sourceInstanceId: number,
    clientX: number,
    clientY: number
): RackDropTarget | null {
    if (!pointInRect(clientX, clientY, rackElement.getBoundingClientRect())) {
        return null;
    }

    const sourceItem = pedalboard.maybeGetItem(sourceInstanceId);
    if (!sourceItem) {
        return null;
    }

    const rackItems = Array.from(
        rackElement.querySelectorAll<HTMLElement>("[data-rack-instance-id]")
    );
    const sourceElement = rackItems.find(
        element => Number(element.dataset.rackInstanceId) === sourceInstanceId
    );
    if (sourceElement && pointInRect(clientX, clientY, sourceElement.getBoundingClientRect())) {
        return null;
    }

    let nearestElement: HTMLElement | null = null;
    let nearestRect: DOMRect | null = null;
    let nearestDistance = Number.POSITIVE_INFINITY;
    for (const element of rackItems) {
        const targetInstanceId = Number(element.dataset.rackInstanceId);
        if (!Number.isFinite(targetInstanceId)
            || targetInstanceId === sourceInstanceId
            || sourceItem.isChild(targetInstanceId)) {
            continue;
        }
        const rect = element.getBoundingClientRect();
        const distance = distanceSquaredToRect(clientX, clientY, rect);
        if (distance < nearestDistance) {
            nearestElement = element;
            nearestRect = rect;
            nearestDistance = distance;
        }
    }

    if (!nearestElement || !nearestRect) {
        return null;
    }

    let before: boolean;
    if (clientY < nearestRect.top) {
        before = true;
    } else if (clientY > nearestRect.bottom) {
        before = false;
    } else {
        before = clientX < nearestRect.left + nearestRect.width / 2;
    }
    return {
        instanceId: Number(nearestElement.dataset.rackInstanceId),
        before
    };
}

function RackView(props: RackViewProps) {
    const model = PiPedalModelFactory.getInstance();
    const rackRef = useRef<HTMLDivElement | null>(null);
    const [draggingId, setDraggingId] = useState<number | null>(null);
    const [dropTarget, setDropTarget] = useState<RackDropTarget | null>(null);
    const suprOrange = isDarkMode() ? "#e88f4d" : "#d2691e";

    const items: PedalboardItem[] = [];
    for (const item of props.pedalboard.itemsGenerator()) {
        if (!item.isStart() && !item.isEnd()) {
            items.push(item);
        }
    }

    function findDropTarget(
        instanceId: number,
        clientX: number,
        clientY: number
    ): RackDropTarget | null {
        if (!props.enableStructureEditing || !rackRef.current) {
            return null;
        }
        return getDropTarget(
            rackRef.current,
            props.pedalboard,
            instanceId,
            clientX,
            clientY
        );
    }

    function onDragMove(instanceId: number, clientX: number, clientY: number) {
        const target = findDropTarget(instanceId, clientX, clientY);
        setDropTarget(current => {
            if (current?.instanceId === target?.instanceId
                && current?.before === target?.before) {
                return current;
            }
            return target;
        });
    }

    function clearDragFeedback() {
        setDraggingId(null);
        setDropTarget(null);
    }

    function onDragEnd(instanceId: number, clientX: number, clientY: number) {
        const target = findDropTarget(instanceId, clientX, clientY);
        clearDragFeedback();
        if (!target) {
            return;
        }
        if (target.before) {
            model.movePedalboardItemBefore(instanceId, target.instanceId);
        } else {
            model.movePedalboardItemAfter(instanceId, target.instanceId);
        }
        props.onSelectionChanged(instanceId);
    }

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
        const collapsed = props.collapsedItems.has(item.instanceId) && !item.isEmpty();
        const draggable = props.enableStructureEditing && !item.isEmpty();
        const dragging = draggingId === item.instanceId;
        const fitContent = !item.isSplit() && !missing && !item.isEmpty();
        const controlHeight = (missing || item.isEmpty()) ? 110 : 190;
        const borderColor = selected
            ? props.theme.palette.primary.main
            : (isDarkMode() ? "#444" : "#DDD");
        const headerBackground = isDarkMode()
            ? "rgba(255,255,255,0.06)"
            : "rgba(0,0,0,0.04)";

        const header = (
            <div
                title={draggable
                    ? `Drag to reorder ${title}. Double-click to ${collapsed ? "expand" : "collapse"}.`
                    : `Double-click to ${collapsed ? "expand" : "collapse"} ${title}.`}
                onClick={() => props.onSelectionChanged(item.instanceId)}
                onDoubleClick={(event) => {
                    event.preventDefault();
                    event.stopPropagation();
                    if (!item.isEmpty()) {
                        props.onToggleCollapsed(item.instanceId);
                    }
                }}
                style={{
                    display: "flex",
                    flexFlow: collapsed ? "column-reverse nowrap" : "row nowrap",
                    alignItems: "center",
                    width: "100%",
                    height: "100%",
                    minWidth: 0,
                    paddingLeft: collapsed ? 0 : 8,
                    paddingRight: collapsed ? 0 : 16,
                    paddingBottom: collapsed ? 8 : 0,
                    cursor: dragging ? "grabbing" : (draggable ? "grab" : "pointer"),
                    userSelect: "none",
                    background: headerBackground
                }}
            >
                <div
                    onPointerDown={(event) => event.stopPropagation()}
                    onClick={(event) => event.stopPropagation()}
                    onDoubleClick={(event) => event.stopPropagation()}
                    style={{
                        display: "flex",
                        flex: "0 0 auto",
                        alignItems: "center",
                        justifyContent: "center",
                        width: collapsed ? "100%" : 56,
                        height: collapsed ? 56 : "100%"
                    }}
                >
                    {uiPlugin && (
                        <div
                            style={{
                                display: "flex",
                                alignItems: "center",
                                justifyContent: "center",
                                transform: collapsed ? "rotate(-90deg)" : undefined
                            }}
                        >
                            <Switch
                                color="secondary"
                                size="small"
                                checked={item.isEnabled}
                                inputProps={{ "aria-label": `${title} bypass` }}
                                onChange={(event) => {
                                    model.setPedalboardItemEnabled(item.instanceId, event.target.checked);
                                }}
                            />
                        </div>
                    )}
                </div>
                <Typography
                    noWrap
                    style={{
                        flex: collapsed ? "1 1 auto" : "0 1 auto",
                        minWidth: 0,
                        maxHeight: collapsed ? "calc(100% - 56px)" : undefined,
                        fontSize: "1.1rem",
                        fontWeight: 700,
                        textOverflow: "ellipsis",
                        whiteSpace: "nowrap",
                        opacity: 0.75,
                        writingMode: collapsed ? "vertical-rl" : undefined,
                        transform: collapsed ? "rotate(180deg)" : undefined
                    }}
                >
                    {title}
                </Typography>
                {!collapsed && props.displayAuthor && uiPlugin && item.title && (
                    <Typography
                        noWrap
                        style={{
                            flex: "0 1 auto",
                            minWidth: 0,
                            marginLeft: 8,
                            fontWeight: 500,
                            fontSize: "0.8rem",
                            textOverflow: "ellipsis",
                            whiteSpace: "nowrap",
                            opacity: 0.75
                        }}
                    >
                        {uiPlugin.name}
                    </Typography>
                )}
            </div>
        );

        return (
            <div
                key={item.instanceId}
                data-rack-instance-id={item.instanceId}
                style={{
                    display: "flex",
                    flexDirection: "column",
                    boxSizing: "border-box",
                    flex: collapsed
                        ? `0 0 ${COLLAPSED_RACK_ITEM_WIDTH}px`
                        : "0 1 auto",
                    width: collapsed
                        ? COLLAPSED_RACK_ITEM_WIDTH
                        : "fit-content",
                    minWidth: collapsed
                        ? COLLAPSED_RACK_ITEM_WIDTH
                        : 0,
                    maxWidth: collapsed
                        ? COLLAPSED_RACK_ITEM_WIDTH
                        : "100%",
                    minHeight: collapsed ? 180 : undefined,
                    border: "1px solid " + borderColor,
                    borderLeft: (selected ? "3px" : "1px") + " solid " + borderColor,
                    borderRadius: 8,
                    overflow: "hidden",
                    outline: dragging ? `2px solid ${suprOrange}` : undefined,
                    outlineOffset: dragging ? -2 : undefined,
                    boxShadow: selected
                        ? `0 0 0 1px ${props.theme.palette.primary.main}55`
                        : undefined
                }}
            >
                <Draggable
                    draggable={draggable}
                    moveElement={false}
                    getScrollContainer={() => rackRef.current}
                    onDragStart={() => {
                        setDraggingId(item.instanceId);
                        setDropTarget(null);
                    }}
                    onDragMove={(clientX, clientY) => {
                        onDragMove(item.instanceId, clientX, clientY);
                    }}
                    onDragEnd={(clientX, clientY) => onDragEnd(item.instanceId, clientX, clientY)}
                    onDragCancel={() => clearDragFeedback()}
                    style={{
                        flex: collapsed ? "1 1 auto" : `0 0 ${RACK_ITEM_HEADER_HEIGHT}px`,
                        width: "100%",
                        height: collapsed ? "100%" : RACK_ITEM_HEADER_HEIGHT
                    }}
                >
                    {header}
                </Draggable>
                {!collapsed && (
                    <div
                        style={{
                            position: "relative",
                            flex: "1 1 auto",
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
                )}
            </div>
        );
    }

    function renderDropMarker(key: string): React.ReactNode {
        return (
            <div
                key={key}
                aria-hidden={true}
                style={{
                    flex: "0 0 4px",
                    minHeight: RACK_ITEM_HEADER_HEIGHT,
                    alignSelf: "stretch",
                    borderRadius: 2,
                    background: suprOrange
                }}
            />
        );
    }

    const rackChildren: React.ReactNode[] = [];
    for (const item of items) {
        if (dropTarget?.instanceId === item.instanceId && dropTarget.before) {
            rackChildren.push(renderDropMarker(`drop-before-${item.instanceId}`));
        }
        rackChildren.push(renderRackItem(item));
        if (dropTarget?.instanceId === item.instanceId && !dropTarget.before) {
            rackChildren.push(renderDropMarker(`drop-after-${item.instanceId}`));
        }
    }

    return (
        <div
            ref={rackRef}
            style={{
                width: "100%",
                height: "100%",
                boxSizing: "border-box",
                overflowY: "auto",
                overflowX: "hidden",
                padding: 12
            }}
        >
            <div
                style={{
                    display: "flex",
                    flexFlow: "row wrap",
                    alignItems: "stretch",
                    alignContent: "flex-start",
                    gap: 12,
                    width: "100%"
                }}
            >
                {rackChildren}
            </div>
            <div style={{ height: 12 }} />
        </div>
    );
}

export default RackView;
