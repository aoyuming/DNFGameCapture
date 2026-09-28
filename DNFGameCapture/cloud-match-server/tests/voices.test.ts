import Database from 'better-sqlite3';
import express from 'express';
import request from 'supertest';
import { randomUUID } from 'node:crypto';
import { afterEach, describe, expect, test } from 'vitest';
import { VoiceStore, PHRASES, pcmToWav, validateWav } from '../src/voices.js';
import { createPublicVoiceApi, createVoiceAdminApi } from '../src/voice-routes.js';
import { createCloudMatchApp } from '../src/app.js';
import { VOICE_ADMIN_JS, buildVoiceAdminPage } from '../src/voice-admin-page.js';
const resources:Array<()=>Promise<void>>=[];
afterEach(async()=>{for(const close of resources.splice(0))await close();});
const wav=(sample=1000)=>pcmToWav(Buffer.from([sample&255,(sample>>8)&255,1,0]));
function fixture(fail=false){const db=new Database(':memory:');let calls=0;const store=new VoiceStore(db,async()=>{calls++;if(fail)throw Error('mock failure');return wav();});resources.push(async()=>{await store.close();db.close();});const app=express();app.use('/api/voice',createPublicVoiceApi(store));app.use('/admin',createVoiceAdminApi(store));return {store,db,app,calls:()=>calls};}
describe('cloud voice assets (all synthesis mocked, zero paid calls)',()=>{
 test('catalog has stable indexes and defaults; ETag/immutable assets update by content',async()=>{
  const {store,app,calls}=fixture();const before=await request(app).get('/api/voice/catalog');expect(before.status).toBe(200);expect(before.body.defaults.ink).toBe('lol-announcer');expect(before.body.defaults.normal).toBe('lol-announcer');expect(before.body.voices.map((v:{index:number})=>v.index)).toEqual([1,2,3,4,6]);
  expect((await request(app).get('/api/voice/catalog').set('If-None-Match',before.headers.etag)).status).toBe(304);
  const a=store.put('doubao-gufeng2','ink-double',wav());const after=await request(app).get('/api/voice/catalog').set('If-None-Match',before.headers.etag);expect(after.status).toBe(200);expect(after.body.revision).not.toBe(before.body.revision);
  const r=await request(app).get('/api/voice/audio/'+a.sha256+'.wav');expect(r.status).toBe(200);expect(r.headers['cache-control']).toContain('immutable');expect(Buffer.from(r.body)).toEqual(wav());
  const b=store.put('doubao-gufeng2','ink-double',wav(2000));expect(b.sha256).not.toBe(a.sha256);expect(store.asset(a.sha256)).toEqual(wav());expect(calls()).toBe(0);
 });
 test('custom voice creation, upload, disable/reenable never reuses an index',async()=>{
  const {store,app}=fixture();const input={id:'custom-a',label:'自定义',kind:'custom'};
  const v=(await request(app).post('/admin').send(input)).body.voice;expect(v.index).toBe(7);
  expect((await request(app).put('/admin/custom-a/clips/double').set('Content-Type','audio/wav').send(wav())).status).toBe(200);
  expect((await request(app).post('/admin/custom-a/generate').send({keys:['double'],requestId:randomUUID(),confirmPaid:true})).status).toBe(400);
  store.save({...input,enabled:false},true);expect(store.catalog().voices.some(v=>v.id==='custom-a')).toBe(false);
  const b=store.save({id:'custom-b',label:'B',kind:'custom'});expect(b.index).toBe(8);store.save({...input,enabled:true},true);expect(store.voice('custom-a').index).toBe(7);
 });
 test('paid generation is explicit, deduplicated, cached, and forced only deliberately',async()=>{
  const {store,calls}=fixture();const body={keys:['double'],requestId:randomUUID(),confirmPaid:true};
  expect(()=>store.generation('doubao-meihuo',{...body,confirmPaid:false})).toThrow();expect(calls()).toBe(0);
  store.generation('doubao-meihuo',body);await store.idle();expect(calls()).toBe(1);
  store.generation('doubao-meihuo',body);store.generation('doubao-meihuo',{...body,requestId:randomUUID()});await store.idle();expect(calls()).toBe(1);
  store.generation('doubao-meihuo',{...body,requestId:randomUUID(),force:true});await store.idle();expect(calls()).toBe(2);
 });
 test('failure leaves paid/custom assets intact and has no automatic retry',async()=>{
  const {store,calls}=fixture(true);const old=store.put('doubao-meihuo','double',wav());const body={keys:['double'],requestId:randomUUID(),confirmPaid:true,force:true};
  store.generation('doubao-meihuo',body);await store.idle();store.generation('doubao-meihuo',body);await store.idle();expect(calls()).toBe(1);expect(store.clips('doubao-meihuo')[0].sha256).toBe(old.sha256);expect(store.jobs()[0].state).toBe('failed');
 });
 test('concurrent jobs and uploads cannot overwrite an in-flight generation',async()=>{
  const db=new Database(':memory:');let release!:(value:Buffer)=>void;const store=new VoiceStore(db,()=>new Promise(resolve=>release=resolve));resources.push(async()=>{await store.close();db.close();});
  const body={keys:['double'],requestId:randomUUID(),confirmPaid:true};store.generation('doubao-meihuo',body);
  expect(()=>store.generation('doubao-meihuo',{...body,requestId:randomUUID()})).toThrow('voice_busy');expect(()=>store.put('doubao-meihuo','double',wav())).toThrow('voice_busy');
  release(wav());await store.idle();expect(store.jobs()[0].state).toBe('done');
 });
 test('reject invalid/custom non-WAV data and unsafe ids without changing catalog',async()=>{
  const {store,app,calls}=fixture();const revision=store.catalog().revision;
  expect(()=>validateWav(Buffer.from('not wav'))).toThrow();expect(()=>store.save({id:'../bad',kind:'custom',label:'bad'})).toThrow();
  expect((await request(app).put('/admin/doubao-meihuo/clips/double').set('Content-Type','audio/wav').send(Buffer.from('bad'))).status).toBe(400);
  expect((await request(app).get('/api/voice/audio/not-a-hash.wav')).status).toBe(404);expect(store.catalog().revision).toBe(revision);expect(calls()).toBe(0);
 });
 test('real admin mount requires authentication + CSRF; public API cannot generate',async()=>{
  const app=createCloudMatchApp({databasePath:':memory:',adminPassword:'voice-test',adminCsrfToken:'csrf-voice',voiceSynthesize:async()=>wav()});resources.push(()=>app.close());
  expect((await request(app.adminExpressApp).get('/admin/voices')).status).toBe(401);
  const html=await request(app.adminExpressApp).get('/admin/voices').auth('admin','voice-test');expect(html.status).toBe(200);expect(html.text).toContain('声音管理');
  expect((await request(app.adminExpressApp).post('/admin/api/voices').auth('admin','voice-test').send({id:'x',label:'X',kind:'custom'})).status).toBe(403);
  expect((await request(app.adminExpressApp).post('/admin/api/voices').auth('admin','voice-test').set('x-dnf-admin-csrf','csrf-voice').send({id:'x',label:'X',kind:'custom'})).status).toBe(201);
  expect((await request(app.expressApp).post('/api/voice/x/generate').send({})).status).toBe(404);
  expect((await request(app.expressApp).get('/admin/voices')).status).toBe(404);
 });
 test('LOL seed preserves preexisting custom index six and remains stable across restarts',async()=>{
  const db=new Database(':memory:');const initial=new VoiceStore(db,async()=>wav());await initial.close();
  db.prepare('DELETE FROM voice_definitions WHERE id=?').run('lol-announcer');
  db.prepare('INSERT INTO voice_definitions VALUES(?,?,?,?,?,?,?)').run(6,'preexisting','Existing custom','custom','','',1);
  const upgraded=new VoiceStore(db,async()=>wav());expect(upgraded.voice('preexisting').index).toBe(6);expect(upgraded.voice('lol-announcer').index).toBe(7);await upgraded.close();
  const restarted=new VoiceStore(db,async()=>wav());expect(restarted.voice('lol-announcer').index).toBe(7);expect(restarted.voices()).toHaveLength(6);await restarted.close();db.close();
 });
 test('seven victory phrases share the normal immutable store and LOL never synthesizes',()=>{
  const {store,calls}=fixture();const keys=Object.keys(PHRASES).filter(k=>k.includes('victory'));expect(Object.keys(PHRASES)).toHaveLength(28);expect(keys).toHaveLength(7);
  for(const key of keys)store.put('lol-announcer',key,wav());expect(new Set(store.clips('lol-announcer').map(c=>c.sha256)).size).toBe(1);
  expect(()=>store.generation('lol-announcer',{keys,requestId:randomUUID(),confirmPaid:true})).toThrow('custom_voice_upload_only');expect(calls()).toBe(0);
 });
 test('admin script parses and tokens are HTML escaped',()=>{expect(()=>new Function(VOICE_ADMIN_JS)).not.toThrow();expect(buildVoiceAdminPage('\"><script>')).not.toContain('content=""><script>');});
});
