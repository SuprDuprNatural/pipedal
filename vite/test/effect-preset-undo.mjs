// Run: node vite/test/effect-preset-undo.mjs
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import ts from 'typescript';
const file = new URL('../src/pipedal/PiPedalModel.tsx', import.meta.url);
const source = ts.createSourceFile('model.tsx', readFileSync(file, 'utf8'), ts.ScriptTarget.Latest, true, ts.ScriptKind.TSX);
const names = new Set(['effectPresetContext', 'effectSoundFingerprint', 'publishEffectPresetHistory',
    'clearEffectPresetHistory', 'invalidateEffectPresetEdit', 'validateEffectPresetHistory',
    'loadEffectPresetWithUndo', 'restoreEffectPreset', 'undoEffectPreset', 'redoEffectPreset']);
const methods = [];
function visit(node) {
    if (ts.isMethodDeclaration(node) && names.has(node.name.getText(source))) methods.push(node.getText(source));
    ts.forEachChild(node, visit);
}
visit(source);
assert.equal(methods.length, names.size);
const code = ts.transpileModule(`class Controller { ${methods.join('\n')} }`, {
    compilerOptions: { target: ts.ScriptTarget.ES2022 }
}).outputText;
class Item { deserialize(data) { return Object.assign(this, structuredClone(data)); } }
const Controller = new Function('PedalboardItem', 'State', `${code}; return Controller;`)(Item, { Ready: 1 });
const observable = value => ({ get() { return value; }, set(next) { value = next; } });
const before = { instanceId: 4, uri: 'effect', controlValues: [{key:'gain', value:0.2}],
    lv2State: [true, {path: 'before'}], pathProperties: {model:'before.nam'}, vstState:'', lilvPresetUri:'',
    isEnabled:false, sideChainInputId:12, title:'My effect', midiBindings:[] };
const after = {...structuredClone(before), controlValues:[{key:'gain', value:0.8}],
    lv2State:[true,{path:'after'}], pathProperties:{model:'after.nam'}};
let live = structuredClone(before), selected = 3;
const requests = [], alerts = [];
const model = new Controller();
Object.assign(model, { effectPresetEpoch:0, effectPresetHistory:observable({}),
    banks:{ get:() => ({selectedBank:2}) }, presets:{get:() => ({selectedInstanceId:selected})},
    pedalboard:{get:() => ({ selectedSnapshot:-1, itemsGenerator:() => [live], tryGetItem:() => live })},
    state:{get:() => 1}, presetCache:{}, getUiPlugin:() => ({name:'Test effect'}), showAlert:e => alerts.push(e),
    webSocket:{request: async (message,body) => {
        requests.push({message,body});
        if (message === 'loadPluginPresetWithUndo') { live = structuredClone(after); return [before,after]; }
        live = structuredClone(body.target); return live;
    }} });
await model.loadEffectPresetWithUndo(4,9);
assert(model.effectPresetHistory.get().canUndo);
assert.equal(model.effectPresetHistory.get().label, 'My effect');
model.presetCache.effect = {getItem: () => ({label: 'Warm drive'})};
await model.loadEffectPresetWithUndo(4,9);
assert.equal(model.effectPresetHistory.get().label, 'My effect · Warm drive');
assert.equal(requests[0].body.bankId,2);
await model.undoEffectPreset();
assert.deepEqual({...requests.at(-1).body.target}, before);
assert.deepEqual(live.pathProperties, before.pathProperties);
assert.equal(live.instanceId,4); assert.equal(live.sideChainInputId,12); assert.equal(live.isEnabled,false);
assert(model.effectPresetHistory.get().canRedo);
await model.redoEffectPreset();
assert.deepEqual({...requests.at(-1).body.target}, after);
assert(model.effectPresetHistory.get().canUndo);
model.invalidateEffectPresetEdit(99); assert(model.effectPresetHistory.get().canUndo);
model.invalidateEffectPresetEdit(4); assert(!model.effectPresetHistory.get().canUndo);
await model.loadEffectPresetWithUndo(4,9);
live.controlValues[0].value = 0.5;
model.validateEffectPresetHistory(); assert(!model.effectPresetHistory.get().canUndo);
await model.loadEffectPresetWithUndo(4,9);
selected = 10; model.validateEffectPresetHistory(); assert(!model.effectPresetHistory.get().canUndo);
let finish;
model.webSocket.request = () => new Promise(resolve => {finish=resolve;});
const pending = model.loadEffectPresetWithUndo(4,9);
assert(model.effectPresetHistory.get().busy);
selected = 11; model.validateEffectPresetHistory(); finish([before,after]); await pending;
assert(!model.effectPresetHistory.get().canUndo); assert(!model.effectPresetHistory.get().busy);
model.webSocket.request = async () => {throw Error('state capture failed');};
await model.loadEffectPresetWithUndo(4,9);
assert.match(alerts.at(-1),/state capture failed/);
assert(!model.effectPresetHistory.get().busy);
console.log('Effect preset undo/redo, complete state, edit/board conflict, pending capture and error checks passed.');

// Exercise the toolbar's actual JSX with lightweight hook/element substitutes.
const toolbarSource = ts.createSourceFile('toolbar.tsx', readFileSync(
    new URL('../src/pipedal/ToolbarInstruments.tsx', import.meta.url), 'utf8'),
    ts.ScriptTarget.Latest, true, ts.ScriptKind.TSX);
const toolbar = toolbarSource.statements.find(node => ts.isFunctionDeclaration(node)
    && node.name?.text === 'ToolbarInstruments');
const toolbarCode = ts.transpileModule(toolbar.getText(toolbarSource).replace('export default ', ''), {
    compilerOptions: {target: ts.ScriptTarget.ES2022, jsx: ts.JsxEmit.React}
}).outputText;
const React = {createElement: (type, props, ...children) => ({type, props, children}), Fragment: 'fragment'};
let toolbarHistory, wide = true;
const actions = [];
const toolbarModel = {effectPresetHistory: {get: () => toolbarHistory},
    undoEffectPreset: () => actions.push('undo'), redoEffectPreset: () => actions.push('redo')};
const renderToolbar = new Function('React', 'useState', 'useEffect', 'useMediaQuery', 'PiPedalModelFactory',
    'Box', 'IconButtonEx', 'UndoIcon', 'RedoIcon', 'TopTuner', 'OutputMeter',
    `${toolbarCode}; return ToolbarInstruments;`)(React, value => [value, () => {}], () => {}, () => wide,
    {getInstance: () => toolbarModel}, 'box', 'button', 'undo', 'redo', 'tuner', 'meter');
const elements = node => node && typeof node === 'object'
    ? [node, ...node.children.flatMap(elements)] : [];
for (const redo of [false, true]) {
    toolbarHistory = {canUndo: !redo, canRedo: redo, busy: false, label: 'My effect · Warm drive'};
    for (wide of [true, false]) {
        const buttons = elements(renderToolbar()).filter(node => node.type === 'button');
        assert.equal(buttons.length, 1);
        assert.equal(buttons[0].children[0].type, redo ? 'redo' : 'undo');
        assert.match(buttons[0].props['aria-label'], /My effect · Warm drive/);
        buttons[0].props.onClick();
        assert.equal(actions.at(-1), redo ? 'redo' : 'undo');
    }
}
toolbarHistory.busy = true;
assert(elements(renderToolbar()).find(node => node.type === 'button').props.disabled);
toolbarHistory = {canUndo: false, canRedo: false};
assert(!elements(renderToolbar()).some(node => node.type === 'button'));
console.log('Toolbar uses one undo/redo slot, names the effect, disables while busy, and remains available on narrow screens.');
