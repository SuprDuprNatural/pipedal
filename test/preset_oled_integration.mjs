// Node 22+. Only use the isolated test host described in PresetIssuesAndOledPlan.md.
// Example tunnel: ssh -N -L 127.0.0.1:18088:127.0.0.1:8088 user@pipedal-device.local
// Run: node test/preset_oled_integration.mjs
import assert from 'node:assert/strict';
import {writeFileSync,readFileSync} from 'node:fs';
const base='http://127.0.0.1:18088';
async function connect(){
 const socket=new WebSocket(base.replace('http','ws')+'/pipedal');let id=0;const pending=new Map();
 socket.onmessage=e=>{const[h,b]=JSON.parse(e.data);if(h.reply!==undefined){const p=pending.get(h.reply);if(p){pending.delete(h.reply);clearTimeout(p.timer);h.message==='error'?p.reject(Error(String(b))):p.resolve(b);}}else if(h.replyTo!==undefined)socket.send(JSON.stringify([{reply:h.replyTo,message:h.message},true]));};
 await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
 return {close:()=>socket.close(),send:(message,body)=>socket.send(JSON.stringify([{message},body])),request:(message,body)=>new Promise((resolve,reject)=>{const replyTo=++id;const timer=setTimeout(()=>{pending.delete(replyTo);reject(Error('Timeout '+message));},10000);pending.set(replyTo,{resolve,reject,timer});socket.send(JSON.stringify([{message,replyTo},body]));})};
}
const a=await connect(),b=await connect();
const req=a.request;
async function load(id){a.send('loadPreset',id);return await req('currentPedalboard');}
async function save(bank,name,overwrite=-1){return await req('saveCurrentPresetAs',{clientId:-1,bankInstanceId:bank,name,saveAfterInstanceId:-1,overwritePresetId:overwrite});}
try {
 const banks=await req('getBankIndex'), settings=await req('getGpioSettings');
 assert.equal(banks.entries.find(x=>x.instanceId===1)?.name,'Test','Refusing non-test bank');
 assert.equal(banks.entries.find(x=>x.instanceId===2)?.name,'Other','Refusing non-test bank');
 assert.equal(settings.enabled,false,'Hardware must be disabled');
 if(process.argv.includes('--failed-write')){
  const index=await req('getPresets'), bank=(await req('getBankIndex')).selectedBank;
  await req('setOledArtwork',[bank,index.selectedInstanceId,Array(640).fill(17)]);
  const before=await req('currentPedalboard');
  await assert.rejects(save(bank,'Must not save '+Date.now()),/denied|Permission|filesystem/);
  assert.deepEqual(await req('currentPedalboard'),before);
  const after=await req('getPresets');assert.equal(after.selectedInstanceId,index.selectedInstanceId);assert(after.presetChanged);
  console.log('Real read-only-directory save failure retains artwork, live name, selection and dirty state.');
 } else if(process.argv.includes('--verify-restart')){
  const expected=JSON.parse(readFileSync('/tmp/pipedal-oled-integration-expected.json','utf8'));
  await req('openBank',expected.bank); const board=await load(expected.id);
  assert.deepEqual(board.oledArtwork,expected.artwork);assert.equal(board.name,expected.name);
  console.log('Saved artwork and preset name survived host restart.');
 } else {
  await req('openBank',1);const initial=await req('currentPedalboard');assert.equal(initial.items.length,0,'Test board must be empty');
  let id=(await req('getPresets')).selectedInstanceId;
  const art=Array.from({length:640},(_,i)=>i%256), changed=art.map(v=>255-v),stamp=Date.now();
  await req('setOledArtwork',[1,id,art]);assert.deepEqual((await b.request('currentPedalboard')).oledArtwork,art);
  assert((await req('getPresets')).presetChanged);
  for(const malformed of [[],[...art,0],art.map((v,i)=>i?v:256),art.map((v,i)=>i?v:-1),art.map((v,i)=>i?v:1.5),art.map((v,i)=>i?v:'1')])
   await assert.rejects(req('setOledArtwork',[1,id,malformed]),/640 integers/);
  assert.deepEqual((await req('currentPedalboard')).oledArtwork,art);
  const name='zebra '+stamp;id=await save(1,name);
  assert.equal((await req('currentPedalboard')).name,name);assert.equal((await req('getPresets')).presetChanged,false);
  assert.deepEqual((await load(id)).oledArtwork,art);
  const clone=await req('copyPreset',{clientId:-1,fromId:id,toId:-1});assert.deepEqual((await load(clone)).oledArtwork,art);
  await load(id);await req('setOledArtwork',[1,id,changed]);
  assert.equal(await save(1,name,id),id);assert.deepEqual((await load(id)).oledArtwork,changed);
  await assert.rejects(save(1,name),/already exists/);
  const renamed='Alpha '+stamp;await b.request('renamePresetItem',{clientId:-1,instanceId:id,name:renamed});
  await assert.rejects(save(1,name,id),/changed/); // stale confirmation from another client
  await req('setOledArtwork',[1,id,art]);
  await assert.rejects(req('setOledArtwork',[2,id,changed]),/preset changed/);
  const before=await req('currentPedalboard');
  assert.equal(await save(2,'Cross '+stamp),-1);
  assert.deepEqual(await req('currentPedalboard'),before);assert((await req('getPresets')).presetChanged);
  const cross=(await req('requestBankPresets',{bankInstanceId:2})).find(x=>x.name==='Cross '+stamp).instanceId;
  await req('openBank',2);assert.deepEqual((await load(cross)).oledArtwork,art);
  await req('copyPresetsToBank',{bankInstanceId:1,presets:[cross]});
  await req('openBank',1);const copied=(await req('getPresets')).presets.find(x=>x.name==='Cross '+stamp).instanceId;
  assert.deepEqual((await load(copied)).oledArtwork,art);
  await req('openBank',2);await req('importPresetsFromBank',{bankInstanceId:1,presets:[clone]});
  const imported=(await req('getPresets')).presets.find(x=>x.name.includes(name));assert(imported);assert.deepEqual((await load(imported.instanceId)).oledArtwork,art);
  // Real ZIP export/import paths; include arbitrary binary bytes, not just zeros.
  const preset=await fetch(base+'/var/downloadPreset?id='+cross);assert(preset.ok);
  const presetZip=new Uint8Array(await preset.arrayBuffer());assert.equal(presetZip[0],80);assert.equal(presetZip[1],75);writeFileSync('/tmp/pipedal-oled-test.piPreset',presetZip);
  const response=await fetch(base+'/var/uploadPreset',{method:'POST',headers:{'Content-Type':'application/json'},body:presetZip});assert(response.ok, await response.clone().text());const uploaded=await response.json();
  assert.deepEqual((await load(uploaded)).oledArtwork,art);
  const bank=await fetch(base+'/var/downloadBank?id=2');assert(bank.ok);const bankZip=new Uint8Array(await bank.arrayBuffer());
  const bankUpload=await fetch(base+'/var/uploadBank',{method:'POST',headers:{'Content-Type':'application/json'},body:bankZip});assert(bankUpload.ok, await bankUpload.clone().text());const newBank=await bankUpload.json();
  await req('openBank',newBank);const entries=(await req('getPresets')).presets;
  const exported=entries.find(x=>x.name==='Cross '+stamp);assert(exported);assert.deepEqual((await load(exported.instanceId)).oledArtwork,art);
  for(const sortName of ['éclair','beta','Delta']) await save(newBank,sortName+' '+stamp);
  const ordered=(await req('getPresets')).presets;
  const collator=new Intl.Collator('en-GB',{sensitivity:'base'});
  assert.deepEqual(ordered.map(x=>x.name),ordered.map(x=>x.name).sort(collator.compare));
  await load(ordered[0].instanceId);a.send('nextPreset');
  assert.equal((await req('getPresets')).selectedInstanceId,ordered[1].instanceId);
  a.send('previousPreset');assert.equal((await req('getPresets')).selectedInstanceId,ordered[0].instanceId);
  await load(exported.instanceId);
  const finalName='Persist '+stamp;await save(newBank,finalName);
  const finalId=(await req('getPresets')).selectedInstanceId;
  writeFileSync('/tmp/pipedal-oled-integration-expected.json',JSON.stringify({bank:newBank,id:finalId,name:finalName,artwork:art}));
  await req('setOledArtwork',[newBank,finalId,null]);assert.equal((await req('currentPedalboard')).oledArtwork,undefined);
  assert.deepEqual((await load(finalId)).oledArtwork,art); // Remove remains unsaved until Save.
  console.log('Two-client artwork, validation, Save As, overwrite, stale confirmation, cross-bank save, copy/import, ZIP preset/bank round-trips and Remove passed.');
 }
}finally{a.close();b.close();}
