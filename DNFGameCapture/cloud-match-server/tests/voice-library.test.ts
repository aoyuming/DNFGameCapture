import Database from 'better-sqlite3';
import express from 'express';
import request from 'supertest';
import { randomUUID } from 'node:crypto';
import { afterEach, describe, expect, test } from 'vitest';
import { pcmToWav, VoiceStore } from '../src/voices.js';
import { createPublicVoiceApi } from '../src/voice-routes.js';
import { TtsGenerator } from '../src/tts-generate.js';
import { createLibraryAdminApi, createPublicLibraryRoute, VoiceLibrary } from '../src/voice-library.js';

const resources: Array<() => Promise<void> | void> = [];
afterEach(async () => { for (const close of resources.splice(0)) await close(); });
const wav = (s = 1000) => pcmToWav(Buffer.from([s & 255, (s >> 8) & 255, 1, 0]));

function fixture(configured = true) {
  const db = new Database(':memory:');
  const calls: string[] = [];
  const voices = new VoiceStore(db, async () => { throw new Error('unused'); }, false);
  const tts = new TtsGenerator(db, voices, async () => wav(), () => 1_800_000_000, undefined, [], () => true);
  const library = new VoiceLibrary(db, voices, async (_v, text) => { calls.push(text); return wav(calls.length * 1000); }, () => 1_800_000_000, () => configured);
  const app = express();
  app.get('/api/voice/library', createPublicLibraryRoute(library));
  app.use('/api/voice', createPublicVoiceApi(voices));
  app.use('/admin/api/voice-library', createLibraryAdminApi(library));
  resources.push(async () => { library.close(); tts.close(); await voices.close(); db.close(); });
  return { app, db, calls, tts };
}

describe('server voice library (synthesis mocked, zero paid calls)', () => {
  test('admin generates once per request id; clients list and download; not counted in the TTS budget', async () => {
    const { app, calls, tts } = fixture();
    const requestId = randomUUID();
    const body = { voiceId: 'doubao-gufeng2', text: '欢迎来到今晚的 DNF 决斗赛！', title: '开场白', requestId };
    const created = await request(app).post('/admin/api/voice-library/generate').send(body).expect(201);
    await request(app).post('/admin/api/voice-library/generate').send(body).expect(201);
    expect(calls).toEqual(['欢迎来到今晚的 DNF 决斗赛!']);
    const list = await request(app).get('/api/voice/library').expect(200);
    expect(list.body.items).toHaveLength(1);
    expect(list.body.items[0]).toMatchObject({ title: '开场白', voiceId: 'doubao-gufeng2', voiceLabel: '古风雅韵 2.0' });
    expect((await request(app).get('/api/voice/library').set('If-None-Match', list.headers.etag)).status).toBe(304);
    const audio = await request(app).get(list.body.items[0].url).expect(200);
    expect(Buffer.from(audio.body)).toEqual(wav(1000));
    expect(tts.spendSummary()).toEqual([]);
    const admin = await request(app).get('/admin/api/voice-library').expect(200);
    expect(admin.body.stats).toMatchObject({ items: 1, published: 1, chars: 16 });
    expect(created.body.item.id).toBe(admin.body.items[0].id);
  });

  test('unpublish, rename, upload and delete', async () => {
    const { app, db } = fixture();
    const item = (await request(app).post('/admin/api/voice-library/generate')
      .send({ voiceId: 'doubao-meihuo', text: '双杀', requestId: randomUUID() }).expect(201)).body.item;
    await request(app).put('/admin/api/voice-library/' + item.id).send({ enabled: false }).expect(200);
    expect((await request(app).get('/api/voice/library')).body.items).toHaveLength(0);
    await request(app).put('/admin/api/voice-library/' + item.id).send({ title: '新名字', enabled: true }).expect(200);
    expect((await request(app).get('/api/voice/library')).body.items[0].title).toBe('新名字');
    await request(app).put('/admin/api/voice-library/' + item.id).send({ title: 'a/b' }).expect(400);
    const up = await request(app).put('/admin/api/voice-library/upload?title=' + encodeURIComponent('我的录音'))
      .set('Content-Type', 'audio/wav').send(wav(3000)).expect(201);
    expect(up.body.item).toMatchObject({ title: '我的录音', source: 'upload' });
    await request(app).put('/admin/api/voice-library/upload?title=x').set('Content-Type', 'audio/wav').send(Buffer.from('nope')).expect(400);
    await request(app).delete('/admin/api/voice-library/' + item.id).expect(200);
    expect(db.prepare('SELECT 1 FROM voice_assets WHERE sha256=?').get(item.sha256)).toBeUndefined();
    expect((await request(app).get('/api/voice/library')).body.items.map((i: { title: string }) => i.title)).toEqual(['我的录音']);
    await request(app).delete('/admin/api/voice-library/' + item.id).expect(404);
  });

  test('rejects non-TTS voices, bad text and missing keys before any paid call', async () => {
    const { app, calls } = fixture(false);
    const gen = (b: object) => request(app).post('/admin/api/voice-library/generate').send({ requestId: randomUUID(), ...b });
    expect((await gen({ voiceId: 'lol-announcer', text: '双杀' })).body.code).toBe('voice_not_tts');
    expect((await gen({ voiceId: 'doubao-meihuo', text: '字'.repeat(201) })).body.code).toBe('text_too_long');
    expect((await gen({ voiceId: 'doubao-meihuo', text: '双杀' })).status).toBe(503);
    expect(calls).toEqual([]);
  });
});
