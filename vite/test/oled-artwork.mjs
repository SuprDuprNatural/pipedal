// Run: node vite/test/oled-artwork.mjs
import assert from 'node:assert/strict';
import { build } from 'esbuild';
async function module(file) {
    const result = await build({entryPoints:[new URL('../src/pipedal/'+file,import.meta.url).pathname],bundle:true,write:false,format:'esm',platform:'node'});
    return import('data:text/javascript;base64,'+Buffer.from(result.outputFiles[0].text).toString('base64'));
}
const {validateArtwork,presetNameLines,loadArtwork}=await module('OledArtwork.ts');
const {Pedalboard}=await module('Pedalboard.tsx');
const {GpioDisplaySettings}=await module('Gpio.tsx');
const art=Array.from({length:640},(_,i)=>i%256);
assert(validateArtwork(art));
for(const value of [[],art.slice(1),[...art,0],art.map((v,i)=>i? v:256),art.map((v,i)=>i?v:-1),art.map((v,i)=>i?v:0.5),art.map((v,i)=>i?v:'0')]) assert(!validateArtwork(value));
const board=new Pedalboard();board.oledArtwork=art;
const copy=board.clone();assert.deepEqual(copy.oledArtwork,art);copy.oledArtwork[0]=99;assert.equal(board.oledArtwork[0],0);
assert.deepEqual(new Pedalboard().deserialize(JSON.parse(JSON.stringify(board))).oledArtwork,art);
assert.equal(new Pedalboard().deserialize({items:[]}).oledArtwork,undefined);
assert.deepEqual(presetNameLines('é🎸B'),['??B','']);
assert.equal(presetNameLines('A'.repeat(100))[1],'A'.repeat(18)+'...');
assert.equal(presetNameLines('')[0],'UNTITLED');
const defaults=new GpioDisplaySettings().deserialize({});
assert(defaults.presetNameOnLoad); assert(!defaults.presetArtwork);assert(!defaults.swipeReveal);
let closed=0, decodes=0;
globalThis.createImageBitmap=async()=>{++decodes;return {width:2049,height:2,close(){++closed;}}};
await assert.rejects(loadArtwork({type:'image/png',size:2097153}),/2 MiB/); assert.equal(decodes,0);
await assert.rejects(loadArtwork({type:'image/svg+xml',size:1}),/PNG/); assert.equal(decodes,0);
await assert.rejects(loadArtwork({type:'image/png',size:1}),/2048/);assert.equal(closed,1);
globalThis.createImageBitmap=async()=>{throw Error('bad image')};
await assert.rejects(loadArtwork({type:'image/png',size:1}),/could not be read/);
console.log('OLED browser metadata, cloning, defaults, text and upload validation checks passed.');
