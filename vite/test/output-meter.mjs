// node test/output-meter.mjs — exercise the production component's lifecycle.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import ts from 'typescript';
const source = readFileSync(new URL('../src/pipedal/OutputMeter.tsx', import.meta.url), 'utf8');
const js = ts.transpileModule(source, { compilerOptions: {
    target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.ReactJSX,
}}).outputText;
let listener, cleanup, timer, values, now = 100, serial = 0;
let connected = true, mounted = false;
const active = new Map(), callbacks = new Map();
const model = {
    state: {
        addOnChangedHandler: fn => { assert(!listener); listener = fn; fn(connected ? 1 : 0); },
        removeOnChangedHandler: fn => { assert.equal(fn, listener); listener = undefined; },
    },
    addVuSubscription: (id, callback) => {
        assert.equal(id, -3);
        const handle = { id: ++serial }; active.set(handle, callback); callbacks.set(serial, callback); return handle;
    },
    removeVuSubscription: handle => { assert(active.delete(handle)); },
};
const hooks = {
    useState: initial => { values ??= initial; return [values, update => { values = update; }]; },
    useEffect: callback => { if (!mounted) { mounted = true; cleanup = callback(); } },
};
const jsx = (type, props) => ({ type, props });
const imports = {
    react: hooks, 'react/jsx-runtime': { jsx, jsxs: jsx }, './OutputMeter.css': {},
    './PiPedalModel': { PiPedalModelFactory: { getInstance: () => model }, State: { Ready: 1 } },
    './Pedalboard': { Pedalboard: { END_CONTROL_ID: -3 } },
};
const exports = {};
new Function('exports', 'require', 'performance', 'window', js)(exports,
    name => { assert(name in imports); return imports[name]; }, { now: () => now },
    { setInterval: callback => { timer = callback; return 1; }, clearInterval: id => { assert.equal(id, 1); timer = undefined; } });
const render = exports.default;
render();
assert.equal(active.size, 1, 'mount subscribes immediately to current Ready state');
const update = (peak = .1, lufs = -23, stereo = false) => ({
    outputMaxValueL: peak, outputMaxValueR: .2, outputLufs: lufs, isStereoOutput: stereo,
});
callbacks.get(1)(update());
assert.equal(values.lufs, -23); assert(Math.abs(values.holds[0] + 20) < 1e-6);
assert.match(render().props['aria-label'], /-23.0 LUFS short-term/);
// A new preset's output arrives on the same master subscription.
now += 33; callbacks.get(1)(update(.05, -24));
assert.equal(serial, 1); assert.equal(values.lufs, -24);
assert(Math.abs(values.holds[0] + 20) < 1e-6, 'peak hold survives a preset change');
callbacks.get(1)(update(.1, -23, true));
assert(values.stereo); assert(Math.abs(values.holds[1] - 20 * Math.log10(.2)) < 1e-6);
now += 33; callbacks.get(1)(update(1.1)); assert(values.clip);
now += 2100; callbacks.get(1)(update(.1)); assert(!values.clip);
callbacks.get(1)({ outputMaxValueL: .1, outputMaxValueR: 0, isStereoOutput: false });
assert.equal(values.lufs, undefined, 'older host must not be presented as a LUFS meter');
callbacks.get(1)(update(.1, NaN)); assert.equal(values.lufs, undefined);
callbacks.get(1)(update(.1, -120)); assert.equal(values.lufs, undefined);
callbacks.get(1)(update()); now += 1600; timer();
assert.equal(values.connected, false, 'missing packets cannot leave a stale meter visible');
callbacks.get(1)(update()); assert(values.connected);
connected = false; listener(0); assert.equal(active.size, 0); assert(!values.connected);
callbacks.get(1)(update(.5, -10)); assert(!values.connected, 'ignore stale callback after disconnect');
connected = true; listener(1); assert.equal(active.size, 1); assert.equal(serial, 2);
callbacks.get(2)(update()); assert.equal(values.lufs, -23);
listener(1); assert.equal(active.size, 1); assert.equal(serial, 3, 'Ready rebinds without duplicate listeners');
callbacks.get(2)(update(.5, -10)); assert(!values.connected);
callbacks.get(3)(update()); cleanup();
assert.equal(active.size, 0); assert.equal(listener, undefined); assert.equal(timer, undefined);
const final = values; callbacks.get(3)(update(.5, -10)); assert.equal(values, final);
console.log('PASS output meter: master subscription, preset continuity, peak hold, stereo, clip release, old hosts, stale packets, reconnect and unmount.');
