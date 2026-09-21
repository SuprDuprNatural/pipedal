// Run from vite: node test/vowel-display.mjs
// Exercise the actual display's monitor lifecycle, including stale callbacks.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import ts from 'typescript';
const source = readFileSync(new URL('../src/pipedal/SuprVowelDisplay.tsx', import.meta.url), 'utf8');
const js = ts.transpileModule(source, { compilerOptions: {
    target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.ReactJSX,
}}).outputText;
let connected = true, listener, cleanup, values = {}, serial = 0;
const active = new Map(), callbacks = new Map();
const model = {
    state: {
        get: () => connected ? 1 : 0,
        addOnChangedHandler: fn => { assert(!listener); listener = fn; },
        removeOnChangedHandler: fn => { assert.equal(fn, listener); listener = undefined; },
    },
    monitorPort: (id, port, rate, callback) => {
        assert.equal(id, 42); assert.equal(rate, 1 / 30);
        const handle = ++serial; active.set(handle, port); callbacks.set(handle, callback); return handle;
    },
    unmonitorPort: handle => { assert(active.delete(handle)); },
};
const hooks = {
    useState: () => [values, update => { values = typeof update === 'function' ? update(values) : update; }],
    useEffect: callback => { cleanup = callback(); },
};
const jsx = (type, props) => ({ type, props });
const imports = {
    react: { default: hooks }, 'react/jsx-runtime': { jsx, jsxs: jsx },
    './PiPedalModel': { PiPedalModelFactory: { getInstance: () => model }, State: { Ready: 1 } },
    './DarkMode': { isDarkMode: () => true },
};
const exports = {};
new Function('exports', 'require', js)(exports, name => { assert(name in imports); return imports[name]; });
exports.default({ instanceId: 42, from: 0, to: 2 });
assert.deepEqual([...active.values()], ['morph', 'f1', 'f2', 'f3']);
callbacks.get(1)(.4); callbacks.get(2)(440); callbacks.get(3)(NaN);
assert.deepEqual(values, { morph: .4, f1: 440 });
listener(); // A second Ready event with the same instance must resubscribe.
assert.equal(active.size, 4);assert.equal(serial, 8);assert.deepEqual(values, {});
callbacks.get(1)(.9);assert.deepEqual(values, {}, 'old subscription must not update the display');
callbacks.get(5)(.6);assert.equal(values.morph, .6);
connected = false;listener();assert.equal(active.size, 0);assert.deepEqual(values, {});
connected = true;listener();assert.equal(active.size, 4);assert.equal(serial, 12);
cleanup();assert.equal(active.size, 0);assert.equal(listener, undefined);
callbacks.get(9)(.7);assert.deepEqual(values, {});
const views = readFileSync(new URL('../src/pipedal/SuprPedalViews.tsx', import.meta.url), 'utf8');
const panel = views.slice(views.indexOf('const SuprVowelView ='));
for (const symbol of 'vowel_a vowel_b mode position depth rate sensitivity release throat focus mix level'.split(' '))
    assert.equal((panel.match(new RegExp(`"${symbol}"`, 'g')) || []).length, 1, symbol+' appears once');
const factory = readFileSync(new URL('../src/pipedal/ControlViewFactory.tsx', import.meta.url), 'utf8');
assert.equal((factory.match(/new SuprVowelViewFactory\(\)/g) || []).length, 1);
console.log('PASS Vowel display: 12 controls, registration, four monitors, reconnect, disconnect, stale callbacks and unmount.');
