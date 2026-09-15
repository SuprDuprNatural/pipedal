// Run with: node vite/test/preset-save.mjs
// Exercise the actual controller methods without mounting the hardware-backed app.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import ts from 'typescript';

function controller(file, method) {
    const source = ts.createSourceFile(file,
        readFileSync(new URL(`../src/pipedal/${file}`, import.meta.url), 'utf8'),
        ts.ScriptTarget.Latest, true, ts.ScriptKind.TSX);
    let member;
    function visit(node) {
        if (ts.isMethodDeclaration(node) && node.name.getText(source) === method) member = node;
        ts.forEachChild(node, visit);
    }
    visit(source);
    assert.ok(member, `${file}.${method} exists`);
    const js = ts.transpileModule(`class Controller { ${member.getText(source)} }`,
        { compilerOptions: { target: ts.ScriptTarget.ES2022 } }).outputText;
    return new Function('nullCast', `${js}; return new Controller();`)(value => value);
}

const model = controller('PiPedalModel.tsx', 'saveCurrentPresetAs');
const requests = [];
model.clientId = 5;
model.presets = { get: () => ({ selectedInstanceId: 42 }) };
model.loadPreset = () => assert.fail('Save As must not request a reload');
model.webSocket = { request: async (message, body) => { requests.push({ message, body }); return 7; } };
assert.equal(await model.saveCurrentPresetAs(1, 'New'), 7);
assert.deepEqual(requests[0], { message: 'saveCurrentPresetAs', body: {
    clientId: 5, bankInstanceId: 1, name: 'New', saveAfterInstanceId: 42, overwritePresetId: -1,
} });
await model.saveCurrentPresetAs(2, 'Existing', -1, 9);
assert.equal(requests[1].body.overwritePresetId, 9);
model.webSocket.request = async () => { throw Error('disk full'); };
await assert.rejects(model.saveCurrentPresetAs(1, 'New'), /disk full/);

const dialog = controller('SavePresetAsDialog.tsx', 'save');
dialog.mounted = true;
dialog.state = { saving: false };
dialog.setState = update => Object.assign(dialog.state, update);
const saves = [], alerts = [];
dialog.props = { onOk: async (...args) => { saves.push(args); } };
dialog.model = { requestBankPresets: async () => [{ name: 'Existing', instanceId: 9 }],
    showAlert: text => alerts.push(text) };
await dialog.save(2, 'Existing');
assert.equal(saves.length, 0); // A collision only opens confirmation.
assert.deepEqual(dialog.state.overwrite, { bankInstanceId: 2, name: 'Existing', instanceId: 9 });
assert.equal(dialog.state.saving, false);
dialog.setState({ overwrite: undefined }); // Cancel leaves the source untouched.
assert.equal(saves.length, 0);
await dialog.save(2, 'New');
assert.deepEqual(saves.pop(), [2, 'New', -1]);
await dialog.save(2, 'Existing', 9);
assert.deepEqual(saves.pop(), [2, 'Existing', 9]);
dialog.props.onOk = async () => { throw Error('disk full'); };
await dialog.save(2, 'Existing', 9);
assert.match(alerts.pop(), /disk full/);
assert.equal(dialog.state.overwrite, undefined);
assert.equal(dialog.state.saving, false);
// Repeated Enter/click while awaiting the server sends only one save.
let finish;
dialog.props.onOk = () => new Promise(resolve => { finish = resolve; });
const saving = dialog.save(2, 'Existing', 9);
assert.equal(dialog.state.saving, true);
await dialog.save(2, 'Existing', 9);
finish();
await saving;
assert.equal(dialog.submitting, false);
console.log('Preset Save As request/confirmation/error/busy checks passed.');
