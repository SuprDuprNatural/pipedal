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
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import { SyntheticEvent, Component } from 'react';
import Button from '@mui/material/Button';
import Menu from '@mui/material/Menu';
import MenuItem from '@mui/material/MenuItem';
import ListSubheader from '@mui/material/ListSubheader';
import Divider from '@mui/material/Divider';
import Fade from '@mui/material/Fade';
import ExpandMoreIcon from '@mui/icons-material/ExpandMore';
import { Theme } from '@mui/material/styles';
import { withStyles } from "tss-react/mui";
import WithStyles, { createStyles } from './WithStyles';
import IconButtonEx from './IconButtonEx';
import { PiPedalModel, PiPedalModelFactory, PluginPresetsChangedHandle, State } from './PiPedalModel';
import PluginPresetsDialog from './PluginPresetsDialog';
import RenameDialog from './RenameDialog';
import OkCancelDialog from './OkCancelDialog';
import { PluginUiPresets } from './PluginPreset';
import PluginPresetsIcon from "./svg/ic_pluginpreset.svg?react";
import { PedalboardItem } from './Pedalboard';

interface PluginPresetSelectorProps extends WithStyles<typeof styles> {
    pedalboardItem: PedalboardItem | null;
    instanceId: number;
    compact?: boolean;
    enableStructureEditing?: boolean;
}

interface SaveTarget {
    instanceId: number;
    uri: string;
    presetId: number;
    bankId: number;
}

interface PluginPresetSelectorState {
    presets: PluginUiPresets;
    ready: boolean;
    loading: boolean;
    loadError: string;
    showPresetsDialog: boolean;
    menuAnchor: HTMLElement | null;
    renameDialogOpen: boolean;
    saveAsName: string;
    overwriteName?: string;
    isPastePluginEnabled: boolean;
}

const styles = (theme: Theme) => createStyles({
    pluginIcon: {
        width: 24, height: 24, opacity: 0.6, fill: theme.palette.text.primary
    }
});

let pluginClipboardContents: PedalboardItem | null = null;

const PluginPresetSelector = withStyles(
    class extends Component<PluginPresetSelectorProps, PluginPresetSelectorState> {
        model: PiPedalModel = PiPedalModelFactory.getInstance();
        private mounted = false;
        private presetRequest = 0;
        private submitting = false;
        private selectedBoard = "";
        private saveTarget?: SaveTarget;
        private presetsChangedHandle?: PluginPresetsChangedHandle;

        constructor(props: PluginPresetSelectorProps) {
            super(props);
            this.selectedBoard = this.boardIdentity();
            this.state = {
                presets: new PluginUiPresets(),
                ready: this.model.state.get() === State.Ready,
                loading: false,
                loadError: "",
                showPresetsDialog: false,
                menuAnchor: null,
                renameDialogOpen: false,
                saveAsName: "",
                isPastePluginEnabled: pluginClipboardContents !== null
            };
        }

        hasPresets(): boolean {
            const item = this.props.pedalboardItem;
            return !!item?.uri && !item.isStart() && !item.isEnd()
                && !item.isEmpty() && !item.isSplit();
        }

        effectName(): string {
            const item = this.props.pedalboardItem;
            return item?.title || (item && this.model.getUiPlugin(item.uri)?.name)
                || item?.pluginName || (item?.isSplit() ? "Split" : "Empty slot");
        }

        private boardIdentity(): string {
            return `${this.model.banks.get().selectedBank}:${this.model.presets.get().selectedInstanceId}`;
        }

        private resetEffectContext() {
            this.saveTarget = undefined;
            this.setState({ menuAnchor: null, showPresetsDialog: false,
                renameDialogOpen: false, overwriteName: undefined, saveAsName: "" });
        }

        private onPresetSelectionChanged = () => {
            const identity = this.boardIdentity();
            if (identity !== this.selectedBoard) {
                this.selectedBoard = identity;
                this.resetEffectContext();
            }
        };

        private onStateChanged = (state: State) => {
            this.setState({ ready: state === State.Ready });
            if (state === State.Ready) {
                const uri = this.props.pedalboardItem?.uri;
                if (uri) this.model.uncachePluginPreset(uri);
                this.loadPresets();
            } else {
                ++this.presetRequest;
                this.saveTarget = undefined;
                this.setState({ menuAnchor: null, renameDialogOpen: false,
                    overwriteName: undefined, showPresetsDialog: false, loading: false });
            }
        };

        componentDidMount() {
            this.mounted = true;
            this.presetsChangedHandle = this.model.addPluginPresetsChangedListener(uri => {
                if (uri === this.props.pedalboardItem?.uri) this.loadPresets();
            });
            this.model.presets.addOnChangedHandler(this.onPresetSelectionChanged);
            this.model.banks.addOnChangedHandler(this.onPresetSelectionChanged);
            // ObservableProperty invokes the handler immediately, including the first load.
            this.model.state.addOnChangedHandler(this.onStateChanged);
        }

        componentWillUnmount() {
            this.mounted = false;
            ++this.presetRequest;
            this.saveTarget = undefined;
            this.model.state.removeOnChangedHandler(this.onStateChanged);
            this.model.presets.removeOnChangedHandler(this.onPresetSelectionChanged);
            this.model.banks.removeOnChangedHandler(this.onPresetSelectionChanged);
            if (this.presetsChangedHandle) {
                this.model.removePluginPresetsChangedListener(this.presetsChangedHandle);
            }
        }

        componentDidUpdate(prevProps: PluginPresetSelectorProps) {
            const uriChanged = this.props.pedalboardItem?.uri !== prevProps.pedalboardItem?.uri;
            if (uriChanged || this.props.instanceId !== prevProps.instanceId) {
                this.resetEffectContext();
            }
            if (uriChanged) {
                ++this.presetRequest;
                this.setState({ presets: new PluginUiPresets(), loadError: "", loading: false });
                this.loadPresets();
            }
        }

        loadPresets(): void {
            if (!this.hasPresets() || this.model.state.get() !== State.Ready) return;
            const uri = this.props.pedalboardItem!.uri;
            const request = ++this.presetRequest;
            this.setState({ loading: true, loadError: "" });
            this.model.getPluginPresets(uri).then(presets => {
                if (this.mounted && request === this.presetRequest) {
                    this.setState({ presets, loading: false });
                }
            }).catch(error => {
                if (this.mounted && request === this.presetRequest) {
                    this.setState({ presets: new PluginUiPresets(), loading: false,
                        loadError: String(error) });
                }
            });
        }

        handleMenuOpen(event: SyntheticEvent) {
            this.setState({ menuAnchor: event.currentTarget as HTMLElement,
                isPastePluginEnabled: pluginClipboardContents !== null });
        }

        handleLoadPluginPreset(presetId: number) {
            const name = this.state.presets.getItem(presetId)?.label ?? "";
            this.setState({ menuAnchor: null, saveAsName: name });
            this.model.loadPluginPreset(this.props.instanceId, presetId);
        }

        handleSaveAs() {
            const item = this.props.pedalboardItem;
            if (!item || !this.hasPresets()) return;
            this.saveTarget = {
                instanceId: this.props.instanceId, uri: item.uri,
                presetId: this.model.presets.get().selectedInstanceId,
                bankId: this.model.banks.get().selectedBank
            };
            this.setState({ menuAnchor: null, renameDialogOpen: true, overwriteName: undefined });
        }

        private isSaveTargetCurrent(target: SaveTarget): boolean {
            return this.mounted && this.saveTarget === target
                && this.model.state.get() === State.Ready
                && this.props.instanceId === target.instanceId
                && this.props.pedalboardItem?.uri === target.uri
                && this.model.pedalboard.get().tryGetItem(target.instanceId)?.uri === target.uri
                && this.model.presets.get().selectedInstanceId === target.presetId
                && this.model.banks.get().selectedBank === target.bankId;
        }

        private async savePreset(name: string, overwrite = false) {
            if (this.submitting) return;
            const target = this.saveTarget;
            if (!target || !this.isSaveTargetCurrent(target)) {
                this.closeSaveDialog();
                this.model.showAlert("The effect has changed. Open Save effect preset again.");
                return;
            }
            this.submitting = true;
            try {
                if (!overwrite) {
                    // Saving by name replaces an existing preset, so check the current library.
                    this.model.uncachePluginPreset(target.uri);
                    const presets = await this.model.getPluginPresets(target.uri);
                    if (!this.isSaveTargetCurrent(target)) return;
                    if (presets.presets.some(preset => preset.label === name)) {
                        this.setState({ presets, overwriteName: name });
                        return;
                    }
                }
                if (!this.isSaveTargetCurrent(target)) return;
                await this.model.saveCurrentPluginPresetAs(target.instanceId, name);
                if (this.isSaveTargetCurrent(target)) {
                    this.saveTarget = undefined;
                    this.setState({ renameDialogOpen: false, overwriteName: undefined, saveAsName: name });
                }
            } catch (error) {
                if (this.isSaveTargetCurrent(target)) {
                    this.setState({ overwriteName: undefined });
                    this.model.showAlert("Could not save effect preset: " + String(error));
                }
            } finally {
                this.submitting = false;
            }
        }

        closeSaveDialog() {
            if (this.submitting) return;
            this.saveTarget = undefined;
            this.setState({ renameDialogOpen: false, overwriteName: undefined });
        }

        handleCopy() {
            const item = this.model.pedalboard.get().tryGetItem(this.props.instanceId);
            pluginClipboardContents = item?.clone() ?? null;
            this.setState({ menuAnchor: null, isPastePluginEnabled: pluginClipboardContents !== null });
        }

        handlePaste() {
            this.setState({ menuAnchor: null });
            if (pluginClipboardContents) {
                this.model.replacePedalboarditem(this.props.instanceId, pluginClipboardContents.clone());
            }
        }

        buildMenuItems() {
            const items: React.ReactNode[] = [
                <ListSubheader key="heading" component="div" disableSticky
                    style={{ lineHeight: "20px", paddingTop: 8, paddingBottom: 8,
                        whiteSpace: "normal", overflowWrap: "anywhere", maxWidth: 320 }}>
                    {this.effectName()}
                </ListSubheader>
            ];
            if (this.hasPresets()) {
                if (this.state.loading) {
                    items.push(<MenuItem key="loading" disabled>Loading presets…</MenuItem>);
                } else if (this.state.loadError) {
                    items.push(<MenuItem key="retry" onClick={() => this.loadPresets()}
                        title={this.state.loadError}>Could not load presets. Retry</MenuItem>);
                } else if (!this.state.presets.presets.length) {
                    items.push(<MenuItem key="empty" disabled>No saved presets yet</MenuItem>);
                } else {
                    for (const preset of this.state.presets.presets) {
                        items.push(<MenuItem key={preset.instanceId}
                            style={{ whiteSpace: "normal", overflowWrap: "anywhere" }}
                            onClick={() => this.handleLoadPluginPreset(preset.instanceId)}>
                            {preset.label}
                        </MenuItem>);
                    }
                }
                items.push(<Divider key="presets-divider" />);
                items.push(<MenuItem key="save" onClick={() => this.handleSaveAs()}>Save effect preset…</MenuItem>);
                items.push(<MenuItem key="manage" disabled={this.state.loading || !!this.state.loadError}
                    onClick={() => this.setState({ menuAnchor: null, showPresetsDialog: true })}>
                    Manage presets…
                </MenuItem>);
            }
            if (this.props.enableStructureEditing !== false) {
                if (this.hasPresets()) items.push(<Divider key="clipboard-divider" />);
                items.push(<MenuItem key="copy" disabled={!this.props.pedalboardItem || this.props.pedalboardItem.isEmpty()}
                    onClick={() => this.handleCopy()}>Copy effect</MenuItem>);
                items.push(<MenuItem key="paste" disabled={!this.state.isPastePluginEnabled}
                    onClick={() => this.handlePaste()}>Paste effect (replace)</MenuItem>);
            }
            return items;
        }

        render() {
            const item = this.props.pedalboardItem;
            if (!item?.uri || item.isStart() || item.isEnd()
                || (!this.hasPresets() && (this.props.compact || this.props.enableStructureEditing === false))) {
                return null;
            }
            const classes = withStyles.getClasses(this.props);
            const menuId = `effect-presets-${this.props.instanceId}-${this.props.compact ? "inline" : "toolbar"}`;
            const buttonId = menuId + "-button";
            const open = Boolean(this.state.menuAnchor);
            const buttonProps = {
                id: buttonId,
                "aria-label": `Presets for ${this.effectName()}`,
                "aria-haspopup": "menu" as const,
                "aria-expanded": open,
                "aria-controls": open ? menuId : undefined,
                disabled: !this.state.ready,
                onClick: (event: SyntheticEvent) => this.handleMenuOpen(event)
            };
            return (
                <div style={{ flex: "0 0 auto" }}
                    onPointerDown={event => event.stopPropagation()}
                    onClick={event => event.stopPropagation()}
                    onDoubleClick={event => event.stopPropagation()}>
                    {this.props.compact ? (
                        <Button {...buttonProps} size="small" color="inherit" endIcon={<ExpandMoreIcon />}
                            style={{ minHeight: 32, minWidth: 0, padding: "4px 8px", fontSize: "0.75rem",
                                fontWeight: 500, textTransform: "none", opacity: 0.8 }}>
                            Presets
                        </Button>
                    ) : (
                        <IconButtonEx {...buttonProps} tooltip="Effect presets" size="large">
                            <PluginPresetsIcon className={classes.pluginIcon} />
                        </IconButtonEx>
                    )}
                    <Menu id={menuId} anchorEl={this.state.menuAnchor} open={open}
                        onClose={() => this.setState({ menuAnchor: null })} TransitionComponent={Fade}
                        MenuListProps={{ "aria-labelledby": buttonId, style: { minWidth: 220 } }}>
                        {this.buildMenuItems()}
                    </Menu>
                    {this.hasPresets() && this.state.showPresetsDialog && (
                        <PluginPresetsDialog instanceId={this.props.instanceId} presets={this.state.presets}
                            show isEditDialog onDialogClose={() => this.setState({ showPresetsDialog: false })} />
                    )}
                    <RenameDialog open={this.state.renameDialogOpen} title={`Save ${this.effectName()} preset`}
                        label="Preset name" defaultName={this.state.saveAsName} acceptActionName="Save"
                        useSafeFilenames={false} onClose={() => this.closeSaveDialog()}
                        onOk={name => { void this.savePreset(name); }} />
                    <OkCancelDialog open={this.state.overwriteName !== undefined}
                        text={`Replace effect preset "${this.state.overwriteName ?? ""}" with the current settings?`}
                        okButtonText="Overwrite"
                        onClose={() => { if (!this.submitting) this.setState({ overwriteName: undefined }); }}
                        onOk={() => {
                            if (this.state.overwriteName !== undefined) {
                                void this.savePreset(this.state.overwriteName, true);
                            }
                        }} />
                </div>
            );
        }
    }, styles);

export default PluginPresetSelector;
