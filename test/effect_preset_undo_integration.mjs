// Node 22+. Isolated host only; optional argument seeds a copied board JSON.
// PIPEDAL_TEST_URL=http://127.0.0.1:18088 node test/effect_preset_undo_integration.mjs [board.json]
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const base = process.env.PIPEDAL_TEST_URL ?? 'http://127.0.0.1:18088';
const socket = new WebSocket(base.replace(/^http/, 'ws') + '/pipedal');
const pending = new Map(); let nextId = 0;
socket.onmessage = event => {
    const [header, body] = JSON.parse(event.data);
    if (header.reply !== undefined) {
        const request = pending.get(header.reply);
        if (request) { pending.delete(header.reply); clearTimeout(request.timer);
            header.message === 'error' ? request.reject(Error(String(body))) : request.resolve(body); }
    } else if (header.replyTo !== undefined) socket.send(JSON.stringify([{reply:header.replyTo,message:header.message},true]));
};
await new Promise((resolve,reject) => {socket.onopen=resolve;socket.onerror=reject;});
const send = (message,body) => socket.send(JSON.stringify([{message},body]));
const request = (message,body) => new Promise((resolve,reject) => {
    const replyTo = ++nextId;
    const timer = setTimeout(() => {pending.delete(replyTo);reject(Error('Timeout '+message));},20000);
    pending.set(replyTo,{resolve,reject,timer}); socket.send(JSON.stringify([{message,replyTo},body]));
});
function items(board) {
    return board.items.flatMap(item => [item, ...items({items:item.topChain ?? []}), ...items({items:item.bottomChain ?? []})]);
}
const sound = item => ({controls:item.controlValues,state:item.lv2State,vst:item.vstState,
    paths:item.pathProperties,lilv:item.lilvPresetUri});
const metadata = item => ({id:item.instanceId,uri:item.uri,enabled:item.isEnabled,title:item.title,
    color:item.iconColor,sidechain:item.sideChainInputId,midi:item.midiBindings,channel:item.midiChannelBinding,useModUi:item.useModUi});
try {
    const audio = await request('getJackServerSettings');
    assert.equal(audio.alsaInputDevice,'null','Refusing a host with real audio input');
    assert.equal(audio.alsaOutputDevice,'null','Refusing a host with real audio output');
    assert.equal((await request('getGpioSettings')).enabled,false,'Refusing active GPIO hardware');
    if (process.argv[2]) {
        const fixture = JSON.parse(readFileSync(process.argv[2],'utf8'));
        send('updateCurrentPedalboard',{clientId:-1,pedalboard:fixture.pedalboard ?? fixture});
    }
    const board = await request('currentPedalboard'), plugins = await request('plugins');
    const candidates = items(board).filter(item => item.controlValues?.length && plugins.some(p=>p.uri===item.uri));
    const effect = candidates.find(item => /supr.*fuzz/i.test(item.uri)) ?? candidates[0];
    assert(effect,'Provide a copied board containing a working effect');
    const plugin = plugins.find(p => p.uri === effect.uri);
    const port = plugin.controls.find(c => c.is_input !== false && !c.is_bypass && !c.trigger_property
        && !c.toggled_property && !c.integer_property && c.max_value > c.min_value
        && effect.controlValues.some(v => v.key===c.symbol));
    assert(port,'Need one continuous input control');
    const bankId=(await request('getBankIndex')).selectedBank, presetId=(await request('getPresets')).selectedInstanceId;
    const pluginPresetId = await request('savePluginPresetAs',{instanceId:effect.instanceId,name:'Undo regression '+Date.now()});
    const original = effect.controlValues.find(c=>c.key===port.symbol).value;
    const changed = original === port.min_value ? port.max_value : port.min_value;
    send('setControl',{clientId:-1,instanceId:effect.instanceId,symbol:port.symbol,value:changed});
    await request('currentPedalboard');
    const [before,after] = await request('loadPluginPresetWithUndo',{bankId,presetId,instanceId:effect.instanceId,uri:effect.uri,pluginPresetId});
    assert.equal(before.controlValues.find(c=>c.key===port.symbol).value,changed);
    assert.equal(after.controlValues.find(c=>c.key===port.symbol).value,original);
    assert.deepEqual(metadata(after),metadata(before));
    const undo = await request('restorePluginPresetState',{bankId,presetId,expected:after,
        target:{...before,title:'must be ignored',isEnabled:!before.isEnabled,sideChainInputId:-900}});
    assert.deepEqual(sound(undo),sound(before)); assert.deepEqual(metadata(undo),metadata(after));
    const redo = await request('restorePluginPresetState',{bankId,presetId,expected:undo,target:after});
    assert.deepEqual(sound(redo),sound(after)); assert.deepEqual(metadata(redo),metadata(after));
    const latest = await request('currentPedalboard');
    for (const other of items(board).filter(item=>item.instanceId!==effect.instanceId)) {
        assert.deepEqual(metadata(items(latest).find(item=>item.instanceId===other.instanceId)),metadata(other));
        assert.deepEqual(items(latest).find(item=>item.instanceId===other.instanceId).controlValues,other.controlValues);
    }
    send('setControl',{clientId:-1,instanceId:effect.instanceId,symbol:port.symbol,value:changed});
    await request('currentPedalboard');
    await assert.rejects(request('restorePluginPresetState',{bankId,presetId,expected:redo,target:before}),/settings changed/);
    const protectedBoard=await request('currentPedalboard');
    assert.equal(items(protectedBoard).find(item=>item.instanceId===effect.instanceId).controlValues.find(c=>c.key===port.symbol).value,changed);
    await assert.rejects(request('loadPluginPresetWithUndo',{bankId:bankId+10000,presetId,instanceId:effect.instanceId,uri:effect.uri,pluginPresetId}),/pedalboard changed/);
    console.log('Atomic live capture/load, undo/redo, metadata/bypass/routing preservation and conflict refusal passed for '+plugin.name+'.');
} finally { socket.close(); }
