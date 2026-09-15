// Run: node vite/test/knob-drag.mjs
// Exercise the production pointer handlers with lightweight React/model doubles.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import ts from 'typescript';

function knob(file, props) {
    const events = [];
    const model = {
        previewPedalboardValue: (...args) => events.push(['preview', ...args]),
        setPedalboardControl: (...args) => events.push(['commit', ...args]),
    };
    const hooks = {
        useRef: current => ({current}),
        useState: value => [value, () => {}],
        useCallback: callback => callback,
    };
    const jsx = (type, props) => ({type, props});
    const imports = {
        react: {default: hooks},
        'react/jsx-runtime': {jsx, jsxs: jsx},
        './DarkMode': {isDarkMode: () => true},
        './PiPedalModel': {PiPedalModelFactory: {getInstance: () => model}},
    };
    const source = readFileSync(new URL('../src/pipedal/'+file, import.meta.url), 'utf8');
    const js = ts.transpileModule(source, {compilerOptions: {
        target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS,
        jsx: ts.JsxEmit.ReactJSX,
    }}).outputText;
    const exports = {};
    new Function('exports', 'require', js)(exports, name => {
        assert(name in imports, 'Unexpected dependency: '+name);
        return imports[name];
    });
    function slider(node) {
        if (!node || typeof node !== 'object') return;
        if (node.props?.role === 'slider') return node.props;
        for (const child of [node.props?.children].flat(Infinity)) {
            const found = slider(child);
            if (found) return found;
        }
    }
    return {handlers: slider(exports.default(props)), events};
}
const event = (clientY, extra = {}) => ({pointerId: 1, pointerType: 'mouse', button: 0,
    clientY, currentTarget: {setPointerCapture() {}}, preventDefault() {}, stopPropagation() {}, ...extra});
function drag(file, props, distance) {
    const {handlers, events} = knob(file, props);
    handlers.onPointerDown(event(200));
    // One-pixel events must accumulate without losing movement to snapping.
    for (let i=1; i<=Math.abs(distance); ++i)
        handlers.onPointerMove(event(200-Math.sign(distance)*i));
    handlers.onPointerUp(event(200-distance));
    assert.equal(events.at(-1)[0], 'commit');
    return events.at(-1).at(-1);
}
for (const [min,max,step] of [[-24,24,1],[-12,12,3],[0,100,1],[0,4,1]]) {
    const props = {instanceId: 1, symbol: 'level', min, max, step, value: min};
    const continuous = {...props, uiControl: {symbol: 'level', name: 'Level',
        min_value: min, max_value: max, default_value: min,
        valueToRange: v => (v-min)/(max-min), rangeToValue: r => min+r*(max-min),
        formatDisplayValue: String, getDisplayUnits: () => '',}};
    for (const pixels of [60,120,240]) {
        const regular = drag('SuprKnob.tsx', continuous, pixels);
        const stepped = drag('SuprStepKnob.tsx', props, pixels);
        assert(Math.abs(stepped-regular) <= step/2+1e-9);
    }
    assert.equal(drag('SuprStepKnob.tsx', {...props,value:max}, -120), min);
    const {handlers, events} = knob('SuprStepKnob.tsx', props);
    handlers.onWheel(event(0,{deltaY:-1}));
    assert.equal(events.at(-1).at(-1), min+step);
    handlers.onKeyDown(event(0,{key:'ArrowUp'}));
    assert.equal(events.at(-1).at(-1), min+step);
}
console.log('Stepped and continuous knobs share a 120px sweep; snapping, small moves, bounds, wheel and keyboard steps passed.');
