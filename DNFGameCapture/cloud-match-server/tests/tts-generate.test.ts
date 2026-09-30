import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import express from 'express';
import request from 'supertest';
import { afterEach, describe, expect, test } from 'vitest';

import { hashLicenseKey } from '../src/auth.js';
import { openDatabase } from '../src/db.js';
import { createV2Api } from '../src/v2-api.js';
import { createTtsAdminApi } from '../src/tts-admin.js';
import { pcmToWav, VoiceError, VoiceStore } from '../src/voices.js';
import {
  HARD_DAILY_BUDGET_YUAN, normalizeTtsText, shanghaiPeriods, TtsGenerator, ttsLimitsFromEnv, type TtsLimits,
} from '../src/tts-generate.js';

const resources: Array<() => Promise<void> | void> = [];
afterEach(async () => { for (const close of resources.splice(0)) await close(); });

const wav = () => pcmToWav(Buffer.from([0xe8, 0x03, 1, 0]));

function fixture(overrides: Partial<TtsLimits> = {}, failWith?: string) {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-tts-'));
  const db = openDatabase(join(directory, 'test.sqlite'));
  let clock = 1_800_000_000;
  for (const key of ['CDK-TTS-ONE', 'CDK-TTS-TWO']) {
    db.prepare(`INSERT INTO licenses (key_hash, label, expires_at, disabled_at, bound_device_id, created_at, updated_at)
      VALUES (?, ?, ?, NULL, NULL, ?, ?)`).run(hashLicenseKey(key), key, clock + 86_400 * 60, clock, clock);
  }
  const calls: string[] = [];
  const voices = new VoiceStore(db, async () => { throw new Error('store synth must not be used'); }, false);
  const tts = new TtsGenerator(db, voices, async (_voice, text) => {
    calls.push(text);
    if (failWith) throw new VoiceError(502, failWith);
    return wav();
  }, () => clock, { ...ttsLimitsFromEnv({}), ...overrides }, ['自定义屏蔽'], () => true);
  const app = express();
  app.use('/admin/api/tts', createTtsAdminApi(tts));
  app.use('/api/v2', createV2Api({ db, now: () => clock, serverUrl: 'http://127.0.0.1:1', sessionTtlSeconds: 86_400 * 30, tts }));
  resources.push(async () => { tts.close(); await voices.close(); db.close(); rmSync(directory, { recursive: true, force: true }); });
  const login = async (key: string, deviceId: string) => {
    const r = await request(app).post('/api/v2/auth/activate').send({ key, deviceId }).expect(200);
    return (req: request.Test) => req.set('Authorization', `Bearer ${r.body.sessionToken}`).set('X-DNF-Device-Id', deviceId);
  };
  return { app, db, tts, calls, login, advance: (sec: number) => { clock += sec; } };
}

describe('user TTS generation (synthesis mocked, zero paid calls)', () => {
  test('requires a v2 session', async () => {
    const { app } = fixture();
    await request(app).post('/api/v2/tts/generate').send({ voice: 'doubao-meihuo', text: '你好' }).expect(401);
    await request(app).get('/api/v2/tts/status').expect(401);
  });

  test('generates once, then serves every license from the global cache for free', async () => {
    const { app, calls, login } = fixture();
    const a = await login('CDK-TTS-ONE', 'device-tts-0001');
    const b = await login('CDK-TTS-TWO', 'device-tts-0002');
    const first = await a(request(app).post('/api/v2/tts/generate')).send({ voice: 'doubao-meihuo', text: '九零老王残血反杀！' }).expect(200);
    expect(first.body.cached).toBe(false);
    expect(Buffer.from(first.body.audio, 'base64')).toEqual(wav());
    expect(first.body.usage.dayUsed).toBe(9);
    const again = await b(request(app).post('/api/v2/tts/generate')).send({ voice: 'doubao-meihuo', text: ' 九零老王残血反杀！ ' }).expect(200);
    expect(again.body.cached).toBe(true);
    expect(again.body.usage.dayUsed).toBe(0);
    expect(calls).toEqual(['九零老王残血反杀!']);
    const status = await a(request(app).get('/api/v2/tts/status')).expect(200);
    expect(status.body.voices.map((v: { id: string }) => v.id)).toEqual(['doubao-meihuo', 'doubao-gufeng2', 'doubao-gufeng1', 'doubao-wuzetian']);
    expect(status.body.maxTextChars).toBe(30);
  });

  test('rejects long, invalid, blocked text and non-TTS voices before any paid call', async () => {
    const { app, calls, login } = fixture();
    const a = await login('CDK-TTS-ONE', 'device-tts-0001');
    const gen = (body: object) => a(request(app).post('/api/v2/tts/generate')).send(body);
    expect((await gen({ voice: 'doubao-meihuo', text: '字'.repeat(31) })).body.code).toBe('text_too_long');
    expect((await gen({ voice: 'doubao-meihuo', text: '双杀😀' })).body.code).toBe('text_invalid_chars');
    expect((await gen({ voice: 'doubao-meihuo', text: '你个 傻 逼' })).body.code).toBe('text_blocked');
    expect((await gen({ voice: 'doubao-meihuo', text: '自定义屏蔽词' })).body.code).toBe('text_blocked');
    expect((await gen({ voice: 'lol-announcer', text: '双杀' })).body.code).toBe('voice_not_tts');
    expect((await gen({ voice: 'nope', text: '双杀' })).status).toBe(404);
    expect(calls).toEqual([]);
  });

  test('per-license daily quota and per-minute rate limit', async () => {
    const { app, login, advance } = fixture({ licenseDailyChars: 10, licenseGeneratePerMinute: 2 });
    const a = await login('CDK-TTS-ONE', 'device-tts-0001');
    const gen = (text: string) => a(request(app).post('/api/v2/tts/generate')).send({ voice: 'doubao-meihuo', text });
    await gen('一二三').expect(200);
    await gen('四五六').expect(200);
    expect((await gen('七八九')).body.code).toBe('rate_limited');
    advance(61);
    await gen('七八九').expect(200);
    const over = await gen('十十');
    expect(over.status).toBe(429);
    expect(over.body.code).toBe('quota_daily');
    await gen('一二三').expect(200); // 缓存命中不受额度限制
  });

  test('global daily budget circuit breaker never exceeds the hard cap and resets next day', async () => {
    expect(ttsLimitsFromEnv({ TTS_DAILY_BUDGET_YUAN: '50' }).dailyBudgetYuan).toBe(HARD_DAILY_BUDGET_YUAN);
    // 0.0009 元预算、3 元/万字符 → 最多 3 个字
    const { app, login, advance, tts } = fixture({ dailyBudgetYuan: 0.0009, licenseGeneratePerMinute: 100 });
    const a = await login('CDK-TTS-ONE', 'device-tts-0001');
    const gen = (text: string) => a(request(app).post('/api/v2/tts/generate')).send({ voice: 'doubao-meihuo', text });
    await gen('一二').expect(200);
    await gen('三').expect(200);
    expect((await gen('四')).body.code).toBe('budget_exhausted');
    expect((await a(request(app).get('/api/v2/tts/status'))).body.budgetExhausted).toBe(true);
    expect(tts.spendSummary()[0].costYuan).toBeCloseTo(0.0009);
    advance(86_400);
    await gen('四').expect(200);
  });

  test('failed synthesis refunds the license quota', async () => {
    const { app, login } = fixture({}, 'tts_request_failed');
    const a = await login('CDK-TTS-ONE', 'device-tts-0001');
    expect((await a(request(app).post('/api/v2/tts/generate')).send({ voice: 'doubao-meihuo', text: '双杀' })).status).toBe(502);
    expect((await a(request(app).get('/api/v2/tts/status'))).body.usage.dayUsed).toBe(0);
  });

  test('text normalisation and Beijing-time periods', () => {
    expect(normalizeTtsText('  你好\u200b，世界！ ', 30, [])).toBe('你好,世界!');
    expect(shanghaiPeriods(Date.UTC(2026, 8, 30, 16, 30) / 1000)).toEqual({ day: '2026-10-01', month: '2026-10' });
  });
});

describe('admin quota management', () => {
  test('per-license override, bonus credits after the daily quota, disable and reset', async () => {
    const { app, login, db } = fixture({ licenseDailyChars: 4, licenseGeneratePerMinute: 100 });
    const a = await login('CDK-TTS-ONE', 'device-tts-0001');
    const id = (db.prepare("SELECT id FROM licenses WHERE label='CDK-TTS-ONE'").get() as { id: number }).id;
    const gen = (text: string) => a(request(app).post('/api/v2/tts/generate')).send({ voice: 'doubao-meihuo', text });
    await gen('一二三').expect(200);
    expect((await gen('四五')).body.code).toBe('quota_daily');
    // 加额外额度：日额度不够时整句改扣额外额度
    await request(app).post(`/admin/api/tts/licenses/${id}/bonus`).send({ delta: 5, note: '赞助' }).expect(200);
    const viaBonus = await gen('四五').expect(200);
    expect(viaBonus.body.usage).toMatchObject({ dayUsed: 3, bonusLeft: 3 });
    // 单独放宽日额度
    await request(app).put(`/admin/api/tts/licenses/${id}`).send({ dailyLimit: 100, monthlyLimit: null, disabled: false, note: '大主播' }).expect(200);
    const normal = await gen('六七八').expect(200);
    expect(normal.body.usage).toMatchObject({ dayUsed: 6, dayLimit: 100, bonusLeft: 3 });
    // 清零今日
    await request(app).post(`/admin/api/tts/licenses/${id}/reset`).send({ scope: 'day' }).expect(200);
    expect((await a(request(app).get('/api/v2/tts/status'))).body.usage.dayUsed).toBe(0);
    // 禁止生成：连缓存都不能用
    await request(app).put(`/admin/api/tts/licenses/${id}`).send({ dailyLimit: null, monthlyLimit: null, disabled: true, note: '' }).expect(200);
    expect((await gen('一二三')).body.code).toBe('tts_disabled');
    expect((await a(request(app).get('/api/v2/tts/status'))).body.enabled).toBe(false);
    const state = (await request(app).get('/admin/api/tts').expect(200)).body;
    const row = state.licenses.find((l: { id: number }) => l.id === id);
    expect(row).toMatchObject({ ttsDisabled: true, bonusChars: 3, dayBonusUsed: 2 });
    const audit = (await request(app).get(`/admin/api/tts/licenses/${id}/audit`).expect(200)).body.events;
    expect(audit.map((e: { action: string }) => e.action)).toEqual(['quota', 'reset', 'quota', 'bonus']);
    await request(app).post(`/admin/api/tts/licenses/99999/bonus`).send({ delta: 5 }).expect(404);
  });

  test('bonus credits never bypass the global budget; settings can only lower the hard cap', async () => {
    const { app, login, db } = fixture({ licenseDailyChars: 0, licenseGeneratePerMinute: 100 });
    const a = await login('CDK-TTS-ONE', 'device-tts-0001');
    const id = (db.prepare("SELECT id FROM licenses WHERE label='CDK-TTS-ONE'").get() as { id: number }).id;
    await request(app).put('/admin/api/tts/settings').send({ dailyBudgetYuan: 6 }).expect(400);
    await request(app).put('/admin/api/tts/settings').send({ dailyBudgetYuan: 0.0006 }).expect(200);
    await request(app).post(`/admin/api/tts/licenses/${id}/bonus`).send({ delta: 1000 }).expect(200);
    const gen = (text: string) => a(request(app).post('/api/v2/tts/generate')).send({ voice: 'doubao-meihuo', text });
    await gen('一二').expect(200);
    expect((await gen('三')).body.code).toBe('budget_exhausted');
    await request(app).put('/admin/api/tts/settings').send({ enabled: false, dailyBudgetYuan: 5 }).expect(200);
    expect((await gen('三')).body.code).toBe('tts_generation_disabled');
    expect((await gen('一二')).status).toBe(200); // 已缓存的仍可免费使用
    const state = (await request(app).get('/admin/api/tts').expect(200)).body;
    expect(state.settings).toMatchObject({ enabled: false, dailyBudgetYuan: 5 });
  });
});
