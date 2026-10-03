// Run: node vite/test/keyboard-shortcuts.mjs
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import ts from 'typescript';
const file = new URL('../src/pipedal/MainPage.tsx', import.meta.url);
const source = ts.createSourceFile('MainPage.tsx', readFileSync(file, 'utf8'), ts.ScriptTarget.Latest, true, ts.ScriptKind.TSX);
let member;
function visit(node) {
    if (ts.isPropertyDeclaration(node) && node.name.getText(source) === 'onShortcut') member = node;
    ts.forEachChild(node, visit);
}
visit(source); assert(member);
const js = ts.transpileModule(`class Controller { ${member.getText(source)} }`, {
    compilerOptions: { target: ts.ScriptTarget.ES2022 },
}).outputText;
class Element { closest() { return this.editable; } }
const document = { modal: false, hidden: false, querySelectorAll() {
    return this.modal ? [{closest: () => this.hidden, getClientRects: () => [{}]}] : [];
} };
const page = new Function('Element', 'document', 'State', `${js}; return new Controller();`)(Element, document, {Ready: 1});
const calls = [];
const history = { canUndo: true, canRedo: true, busy: false };
page.model = { effectPresetHistory: { get: () => history }, state: {get: () => 1},
    presetChanged: {get: () => true}, saveCurrentPreset: () => calls.push('save'),
    undoEffectPreset: () => calls.push('undo'), redoEffectPreset: () => calls.push('redo') };
const key = (key, extra = {}) => {
    let prevented = false;
    page.onShortcut({key, ctrlKey: true, target: new Element(), preventDefault() {prevented = true;}, ...extra});
    return prevented;
};
assert(key('z')); assert(key('Z', {shiftKey: true})); assert(key('y'));
assert(key('z', {ctrlKey: false, metaKey: true})); assert(key('s'));
assert.deepEqual(calls, ['undo', 'redo', 'redo', 'undo', 'save']);
const editable = new Element(); editable.editable = true;
assert(!key('z', {target: editable})); assert(!key('s', {target: editable}));
document.modal = true; assert(!key('z')); assert(!key('s'));
document.hidden = true; assert(key('z')); calls.pop(); // MUI keeps closed menus mounted.
document.modal = false;
for (const flags of [{repeat: true}, {isComposing: true}, {altKey: true}, {defaultPrevented: true}, {ctrlKey: false}])
    assert(!key('z', flags));
history.busy = true; key('z'); history.busy = false;
history.canUndo = false; history.canRedo = false; key('z'); key('y');
assert.equal(calls.length, 5);
console.log('Undo, redo and save shortcuts pass; text editing, modal, repeat and busy guards preserved.');
