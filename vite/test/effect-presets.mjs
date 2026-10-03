// Run: node vite/test/effect-presets.mjs
// Exercise the production selector with a model double; never modify a live rig.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import ts from 'typescript';

function load(file, imports = {}) {
    const source = readFileSync(new URL('../src/pipedal/' + file, import.meta.url), 'utf8');
    const js = ts.transpileModule(source, { compilerOptions: {
        target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.ReactJSX,
    } }).outputText;
    const exports = {};
    new Function('exports', 'require', js)(exports, name => {
        if (name in imports) return imports[name];
        if (name.startsWith('@mui/') || name.endsWith('.svg?react')
            || ['./IconButtonEx', './PluginPresetsDialog', './RenameDialog', './OkCancelDialog'].includes(name))
            return { default: name };
        assert.fail('Unexpected dependency: ' + name);
    });
    return exports;
}
const { ObservableProperty } = load('ObservableProperty.tsx');
const presetTypes = load('PluginPreset.tsx');
const library = new presetTypes.PluginUiPresets().deserialize({
    pluginUri: 'effect:a', presets: [{ instanceId: 10, label: 'Warm' }],
});
const effect = (id, uri = 'effect:a') => ({
    instanceId: id, uri, title: '', pluginName: 'Effect ' + id,
    isStart: () => false, isEnd: () => false, isEmpty: () => false, isSplit: () => false,
    clone() { return effect(this.instanceId, this.uri); },
});
const effects = [effect(1), effect(2)];
const calls = { reads: [], loads: [], saves: [], pastes: [], alerts: [] };
const changed = new Set();
const model = {
    state: new ObservableProperty(1),
    presets: new ObservableProperty({ selectedInstanceId: 100 }),
    banks: new ObservableProperty({ selectedBank: 20 }),
    pedalboard: { get: () => ({ tryGetItem: id => effects.find(item => item.instanceId === id) }) },
    getUiPlugin: () => ({ name: 'Effect' }),
    uncachePluginPreset() {},
    getPluginPresets: async uri => { calls.reads.push(uri); return library; },
    loadPluginPreset: (...args) => calls.loads.push(args),
    saveCurrentPluginPresetAs: async (...args) => { calls.saves.push(args); return 11; },
    replacePedalboarditem: (...args) => calls.pastes.push(args),
    showAlert: message => calls.alerts.push(message),
    addPluginPresetsChangedListener: callback => { changed.add(callback); return callback; },
    removePluginPresetsChangedListener: callback => changed.delete(callback),
};
class Component {
    constructor(props) { this.props = props; }
    setState(update) { Object.assign(this.state, update); }
}
const jsx = (type, props, key) => ({ type, props, key });
const withStyles = Object.assign(component => component, { getClasses: () => ({}) });
const Selector = load('PluginPresetSelector.tsx', {
    react: { Component }, 'react/jsx-runtime': { jsx, jsxs: jsx },
    'tss-react/mui': { withStyles }, './WithStyles': { createStyles: value => value },
    './PiPedalModel': { PiPedalModelFactory: { getInstance: () => model }, State: { Ready: 1 } },
    './PluginPreset': presetTypes,
}).default;
const flush = () => new Promise(resolve => setImmediate(resolve));
const a = new Selector({ instanceId: 1, pedalboardItem: effects[0], compact: true });
const b = new Selector({ instanceId: 2, pedalboardItem: effects[1], compact: true });
a.componentDidMount(); b.componentDidMount(); await flush();
assert.equal(calls.reads.length, 2); // Loads immediately without first changing an effect.
assert.equal(b.state.presets.presets[0].label, 'Warm');

const previous = a.props;
a.props = { ...a.props, pedalboardItem: effect(1) };
a.componentDidUpdate(previous);
assert.equal(calls.reads.length, 2); // Knob edits must not fetch the library again.
b.handleLoadPluginPreset(10);
assert.deepEqual(calls.loads, [[2, 10]]); // Applies to this card, independent of toolbar selection.
a.handleCopy();
b.handleMenuOpen({ currentTarget: {} });
assert.equal(b.state.isPastePluginEnabled, true);
b.handlePaste();
assert.equal(calls.pastes[0][0], 2);
assert.equal(calls.pastes[0][1].instanceId, 1);
b.props = { ...b.props, enableStructureEditing: false };
assert(!b.buildMenuItems().some(item => ['copy', 'paste'].includes(item.key)));

a.handleSaveAs(); await a.savePreset('Warm');
assert.equal(calls.saves.length, 0);
assert.equal(a.state.overwriteName, 'Warm');
a.closeSaveDialog();
assert.equal(calls.saves.length, 0); // Cancelled collision never writes.
a.handleSaveAs(); await a.savePreset('Warm'); await a.savePreset('Warm', true);
assert.deepEqual(calls.saves, [[1, 'Warm']]);
assert.equal(a.state.renameDialogOpen, false);
a.handleSaveAs(); await a.savePreset('New');
assert.deepEqual(calls.saves.at(-1), [1, 'New']);

const save = model.saveCurrentPluginPresetAs;
model.saveCurrentPluginPresetAs = async () => { throw Error('disk full'); };
a.handleSaveAs(); await a.savePreset('New');
assert.equal(a.state.renameDialogOpen, true);
assert.match(calls.alerts.at(-1), /disk full/);
model.saveCurrentPluginPresetAs = save;

let complete;
const read = model.getPluginPresets;
model.getPluginPresets = () => new Promise(resolve => { complete = resolve; });
a.handleSaveAs();
const saving = a.savePreset('Another');
await a.savePreset('Another'); // A repeated Enter cannot create a second request.
const count = calls.saves.length;
model.presets.set({ selectedInstanceId: 101 });
assert.equal(a.state.renameDialogOpen, false);
assert.equal(a.state.saveAsName, '');
complete(library); await saving;
assert.equal(calls.saves.length, count); // Board IDs guard even identical effect IDs/URIs.

model.getPluginPresets = read;
model.state.set(0); assert.equal(a.state.ready, false);
const beforeReconnect = calls.reads.length;
model.state.set(1); await flush();
assert.equal(calls.reads.length, beforeReconnect + 2);
for (const listener of changed) listener('effect:a');
await flush(); assert.equal(calls.reads.length, beforeReconnect + 4);

model.getPluginPresets = () => new Promise(resolve => { complete = resolve; });
a.loadPresets(); a.componentWillUnmount();
const lastState = { ...a.state };
complete(new presetTypes.PluginUiPresets()); await flush();
assert.deepEqual(a.state, lastState); // A late response cannot update an unmounted selector.
b.componentWillUnmount();
assert.equal(changed.size, 0);
for (const property of [model.state, model.presets, model.banks])
    assert.equal(property._on_changed_handlers.length, 0);
console.log('Effect presets: initial load, card targeting, clipboard, lock, overwrite, errors, stale saves, reconnect and cleanup passed.');
