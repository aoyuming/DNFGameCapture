import { mkdtempSync, readFileSync, rmSync, existsSync, writeFileSync, unlinkSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import request from 'supertest';
import { afterEach, expect, test } from 'vitest';
import { createCloudMatchApp } from '../src/app.js';
import { createLicense } from '../src/v2-api.js';
import { generateLicenseKey, hashLicenseKey, isNativeLicenseKey } from '../src/auth.js';
import Database from 'better-sqlite3';
import { openDatabase } from '../src/db.js';
import { activateStoredLicense, generateLicenseBatch, listLicenses, revealLicense } from '../src/license-store.js';
import { decryptLicenseKey } from '../src/license-vault.js';

const resources: { app: ReturnType<typeof createCloudMatchApp>; directory: string }[] = [];
const password = 'license-fixture-password', csrf = 'license-fixture-csrf';
const device = 'license-device-0001', other = 'license-device-0002';
async function fixture() {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-license-'));
  const databasePath = join(directory, 'fixture.sqlite');
  const clock = { now: 1_700_000_000 };
  const app = createCloudMatchApp({ databasePath, now: () => clock.now, adminPassword: password, adminCsrfToken: csrf });
  resources.push({ app, directory });
  const get = (path = '') => request(app.adminExpressApp).get('/admin/api/licenses' + path).auth('admin', password);
  const post = (path: string, body: object) => request(app.adminExpressApp).post('/admin/api/licenses' + path)
    .auth('admin', password).set('x-dnf-admin-csrf', csrf).send(body);
  const activate = (key: string, deviceId = device) => request(app.expressApp).post('/api/v2/auth/activate').send({ key, deviceId });
  return { app, directory, databasePath, clock, get, post, activate };
}
afterEach(async () => { for (const { app, directory } of resources.splice(0)) { await app.close(); rmSync(directory, { recursive: true, force: true }); } });

test('preset batches are delayed until first use, atomic and retry-idempotent', async () => {
  const f = await fixture();
  const state = await f.get().expect(200);
  expect(state.body.presets.map((p: { durationSeconds: number | null }) => p.durationSeconds))
    .toEqual([3600, 86400, 604800, 864000, 7776000, 15552000, 31536000, null]);
  const body = { preset: 'hour', count: 3, label: 'fixture', requestId: 'create-batch-0001' };
  const first = await f.post('', body).expect(201);
  expect(first.body.licenses).toHaveLength(3);
  expect(new Set(first.body.licenses.map((r: { key: string }) => r.key)).size).toBe(3);
  for (const card of first.body.licenses) {
    expect(isNativeLicenseKey(card.key)).toBe(true);
    expect(card).toMatchObject({ activationMode: 'first_use', activatedAt: null, expiresAt: null, durationSeconds: 3600 });
  }
  const again = await f.post('', body).expect(201);
  expect(again.body.licenses.map((r: { key: string }) => r.key)).toEqual(first.body.licenses.map((r: { key: string }) => r.key));
  expect(again.body.replayed).toBe(true);
  await f.post('', { ...body, count: 4 }).expect(409);
  expect((await f.get()).body.licenses).toHaveLength(3);
  f.clock.now += 86400 * 20;
  const expires = f.clock.now + 3600;
  expect((await f.activate(first.body.licenses[0].key).expect(200)).body.licenseExpiresAt).toBe(expires);
  f.clock.now += 500;
  expect((await f.activate(first.body.licenses[0].key).expect(200)).body.licenseExpiresAt).toBe(expires);
  await f.activate(first.body.licenses[0].key, other).expect(409);
  f.clock.now = expires;
  await f.activate(first.body.licenses[0].key).expect(403);
});

test('key recovery is encrypted, admin-only and never in ordinary lists or audits', async () => {
  const f = await fixture();
  const card = (await f.post('', { preset: 'permanent', count: 1, requestId: 'create-secret-0001' }).expect(201)).body.licenses[0];
  await request(f.app.adminExpressApp).post(`/admin/api/licenses/${card.id}/reveal`).expect(401);
  await request(f.app.adminExpressApp).post(`/admin/api/licenses/${card.id}/reveal`).auth('admin', password).expect(403);
  expect((await f.post(`/${card.id}/reveal`, {}).expect(200)).body.key).toBe(card.key);
  expect(JSON.stringify((await f.get()).body)).not.toContain(card.key);
  expect(JSON.stringify((await f.get(`/${card.id}/audit`)).body)).not.toContain(card.key);
  expect(JSON.stringify(f.app.db.prepare('SELECT * FROM licenses').all())).not.toContain(card.key);
  expect(JSON.stringify(f.app.db.prepare('SELECT * FROM license_admin_operations').all())).not.toContain(card.key);
  expect(existsSync(f.databasePath + '.license-key')).toBe(true);
  expect(readFileSync(f.databasePath + '.license-key')).toHaveLength(32);
});

test('extend pending, active and expired cards without resetting activation; permanent remains permanent', async () => {
  const f = await fixture();
  let card = (await f.post('', { preset: 'day', count: 1, requestId: 'extend-create-0001' }).expect(201)).body.licenses[0];
  card = (await f.post(`/${card.id}/extend`, { preset: 'week', revision: card.revision, requestId: 'extend-pending-0001' }).expect(200)).body.license;
  expect(card).toMatchObject({ durationSeconds: 86400 * 8, activatedAt: null, expiresAt: null });
  const key = (await f.post(`/${card.id}/reveal`, {})).body.key;
  const started = f.clock.now;
  await f.activate(key).expect(200);
  card = (await f.get()).body.licenses[0];
  const old = card.expiresAt;
  const body = { preset: 'hour', revision: card.revision, requestId: 'extend-active-0001' };
  card = (await f.post(`/${card.id}/extend`, body).expect(200)).body.license;
  expect(card.expiresAt).toBe(old + 3600);
  await f.post(`/${card.id}/extend`, body).expect(200);
  await f.post(`/${card.id}/extend`, { ...body, requestId: 'extend-stale-0001' }).expect(409);
  f.clock.now = card.expiresAt + 100;
  card = (await f.post(`/${card.id}/extend`, { preset: 'day', revision: card.revision, requestId: 'extend-expired-0001' }).expect(200)).body.license;
  expect(card.expiresAt).toBe(f.clock.now + 86400);
  expect(card.activatedAt).toBe(started);
  card = (await f.post(`/${card.id}/extend`, { preset: 'permanent', revision: card.revision, requestId: 'extend-forever-0001' }).expect(200)).body.license;
  expect(card.expiresAt).toBeNull();
  card = (await f.post(`/${card.id}/extend`, { preset: 'hour', revision: card.revision, requestId: 'extend-permanent-0001' }).expect(200)).body.license;
  expect(card.expiresAt).toBeNull();
});

test('rebind invalidates old sessions and does not reset expiry or activation', async () => {
  const f = await fixture();
  const key = generateLicenseKey();
  createLicense(f.app.db, { key, expiresAt: f.clock.now + 10000, nowSec: f.clock.now });
  const first = (await f.activate(key).expect(200)).body;
  let card = (await f.get()).body.licenses[0];
  await f.post(`/${card.id}/rebind`, { deviceId: 'unknown-device-0003', revision: card.revision, requestId: 'rebind-unknown-0001' }).expect(400);
  card = (await f.post(`/${card.id}/rebind`, { deviceId: null, revision: card.revision, requestId: 'rebind-clear-0001' }).expect(200)).body.license;
  expect(card.boundDeviceId).toBeNull();
  expect(card.expiresAt).toBe(first.licenseExpiresAt);
  await request(f.app.expressApp).post('/api/v2/auth/validate').send({ deviceId: device, sessionToken: first.sessionToken }).expect(401);
  expect((await f.activate(key, other).expect(200)).body.licenseExpiresAt).toBe(first.licenseExpiresAt);
  await f.activate(key, device).expect(409);
});

test('legacy fixed dates survive and hash-only keys can be backfilled after successful activation', async () => {
  const f = await fixture(); const key = generateLicenseKey();
  f.app.db.prepare('INSERT INTO licenses(key_hash,label,expires_at,created_at,updated_at) VALUES(?,?,?,?,?)')
    .run(hashLicenseKey(key), 'legacy', f.clock.now + 600, f.clock.now, f.clock.now);
  let card = (await f.get()).body.licenses[0];
  expect(card).toMatchObject({ activationMode: 'fixed', hasKey: false });
  await f.post(`/${card.id}/reveal`, {}).expect(404);
  await f.post(`/${card.id}/key`, { key: generateLicenseKey(), revision: card.revision, requestId: 'wrong-key-0001' }).expect(400);
  expect((await f.activate(key).expect(200)).body.licenseExpiresAt).toBe(f.clock.now + 600);
  card = (await f.get()).body.licenses[0];
  expect(card.hasKey).toBe(true);
  expect((await f.post(`/${card.id}/reveal`, {})).body.key).toBe(key);
});

test('batch validation rejects unsafe input and late audit failure rolls back all keys', async () => {
  const f = await fixture();
  for (const count of [0, -1, 201, 1.2]) await f.post('', { preset: 'day', count, requestId: 'invalid-batch-0001' }).expect(400);
  await f.post('', { preset: 'invalid', count: 1, requestId: 'invalid-kind-0001' }).expect(400);
  f.app.db.exec("CREATE TRIGGER fail_license_audit BEFORE INSERT ON license_audit BEGIN SELECT RAISE(ABORT,'audit failure'); END");
  await f.post('', { preset: 'day', count: 3, requestId: 'rollback-batch-0001' }).expect(500);
  expect((await f.get()).body.licenses).toEqual([]);
});

test.each(['hour', 'day', 'week', 'ten_days', 'quarter', 'half_year', 'year', 'permanent'])(
  'preset %s activates with the exact promised duration', async preset => {
    const f = await fixture();
    const card = (await f.post('', { preset, count: 1, requestId: 'each-preset-0001' }).expect(201)).body.licenses[0];
    const response = await f.activate(card.key).expect(200);
    expect(response.body.licenseExpiresAt).toBe(card.durationSeconds === null ? 0xFFFFFFFF : f.clock.now + card.durationSeconds);
  });

test('racing devices cannot both first-activate one card', async () => {
  const f = await fixture();
  const card = (await f.post('', { preset: 'day', count: 1, requestId: 'race-create-0001' })).body.licenses[0];
  const responses = await Promise.all([f.activate(card.key, device), f.activate(card.key, other)]);
  expect(responses.map(r => r.status).sort()).toEqual([200, 409]);
  expect(f.app.db.prepare('SELECT * FROM auth_sessions').all()).toHaveLength(1);
  expect((await f.get(`/${card.id}/audit`)).body.events.filter((e: { action: string }) => e.action === 'activate')).toHaveLength(1);
});

test('known-device rebind keeps the first-use date and invalidates repeat old tokens', async () => {
  const f = await fixture();
  const card = (await f.post('', { preset: 'week', count: 2, requestId: 'known-create-0001' })).body.licenses;
  const first = await f.activate(card[0].key).expect(200);
  await f.activate(card[1].key, other).expect(200);
  const before = (await f.get()).body.licenses.find((r: { id: number }) => r.id === card[0].id);
  const body = { deviceId: other, revision: before.revision, requestId: 'known-rebind-0001' };
  const result = await f.post(`/${before.id}/rebind`, body).expect(200);
  expect(result.body.license).toMatchObject({ boundDeviceId: other, activatedAt: before.activatedAt, expiresAt: before.expiresAt });
  await f.post(`/${before.id}/rebind`, body).expect(200);
  await f.activate(card[0].key, device).expect(409);
  await request(f.app.expressApp).get('/api/v2/player-library').set('Authorization', 'Bearer ' + first.body.sessionToken)
    .set('x-dnf-device-id', device).expect(401);
  await f.activate(card[0].key, other).expect(200);
});

test('mutation validation, stale guards and disabled-card behavior protect pending validity', async () => {
  const f = await fixture();
  let card = (await f.post('', { preset: 'day', count: 1, requestId: 'guard-create-0001' })).body.licenses[0];
  await f.post(`/${card.id}suffix/extend`, { preset: 'hour', revision: card.revision, requestId: 'invalid-path-0001' }).expect(400);
  await f.post(`/${card.id}/extend`, { preset: 'day', requestId: 'no-revision-0001' }).expect(400);
  await f.post(`/${card.id}/extend`, { preset: 'day', revision: card.revision }).expect(400);
  await f.post(`/${card.id}/rebind`, { revision: card.revision, requestId: 'no-device-0001' }).expect(400);
  const key = card.key;
  card = (await f.post(`/${card.id}/disable`, { disabled: true, revision: card.revision, requestId: 'disable-test-0001' }).expect(200)).body.license;
  f.clock.now += 100;
  await f.activate(key).expect(403);
  expect(card.activatedAt).toBeNull();
  card = (await f.post(`/${card.id}/disable`, { disabled: false, revision: card.revision, requestId: 'enable-test-0001' }).expect(200)).body.license;
  await f.activate(key).expect(200);
  expect((await f.get()).body.licenses[0].expiresAt).toBe(f.clock.now + 86400);
});

test('failed extension and rebind audits roll back expiry, binding, sessions and retry records', async () => {
  const f = await fixture();
  const created = (await f.post('', { preset: 'day', count: 1, requestId: 'rollback-create-0001' })).body.licenses[0];
  await f.activate(created.key).expect(200);
  const before = (await f.get()).body.licenses[0];
  const sessions = f.app.db.prepare('SELECT * FROM auth_sessions').all();
  f.app.db.exec("CREATE TRIGGER fail_license_write BEFORE INSERT ON license_audit BEGIN SELECT RAISE(ABORT,'audit failure'); END");
  for (const [action, fields] of [['extend', { preset: 'day' }], ['rebind', { deviceId: null }]] as const) {
    await f.post(`/${before.id}/${action}`, { ...fields, revision: before.revision, requestId: 'failed-' + action + '-0001' }).expect(500);
    expect((await f.get()).body.licenses[0]).toEqual(before);
    expect(f.app.db.prepare('SELECT * FROM auth_sessions').all()).toEqual(sessions);
  }
  expect(f.app.db.prepare("SELECT * FROM license_admin_operations WHERE request_id LIKE 'failed-%'").all()).toEqual([]);
});

test('vault survives restart, detects wrong keys and never recreates a lost key for encrypted records', () => {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-license-vault-')), path = join(directory, 'db.sqlite');
  let db = openDatabase(path);
  try {
    const card = generateLicenseBatch(db, { preset: 'hour', count: 1, label: '', requestId: 'vault-create-0001' }, 1700000000).licenses[0];
    const master = readFileSync(path + '.license-key');
    db.close(); db = openDatabase(path);
    expect(revealLicense(db, card.id)).toBe(card.key);
    const row = db.prepare('SELECT key_ciphertext,key_hash FROM licenses').get() as { key_ciphertext: string; key_hash: string };
    expect(() => decryptLicenseKey(db, row.key_ciphertext, hashLicenseKey('different'))).toThrow('vault_unavailable');
    db.close(); unlinkSync(path + '.license-key'); db = openDatabase(path);
    expect(() => revealLicense(db, card.id)).toThrow('vault_unavailable');
    expect(existsSync(path + '.license-key')).toBe(false);
    expect(activateStoredLicense(db, card.key, device, 1700000200, 3600).license.expiresAt).toBe(1700003800);
    writeFileSync(path + '.license-key', Buffer.alloc(32, 5));
    expect(() => revealLicense(db, card.id)).toThrow('vault_unavailable');
    expect(() => generateLicenseBatch(db, { preset: 'day', count: 1, label: '', requestId: 'wrong-vault-0001' }, 1700000200)).toThrow('vault_unavailable');
    expect(listLicenses(db)).toHaveLength(1);
    db.close(); writeFileSync(path + '.license-key', master); db = openDatabase(path);
    expect(revealLicense(db, card.id)).toBe(card.key);
    expect(generateLicenseBatch(db, { preset: 'hour', count: 1, label: '', requestId: 'vault-create-0001' }, 1700000200).replayed).toBe(true);
  } finally { if (db.open) db.close(); rmSync(directory, { recursive: true, force: true }); }
});

test('rebind options use authorization hardware IDs, not unrelated broadcaster identities', async () => {
  const f = await fixture();
  f.app.db.prepare('INSERT INTO devices(id,token_hash,created_at,last_seen_at) VALUES(?,?,?,?)').run('socket-broadcaster-01', 'unused', f.clock.now, f.clock.now);
  const created = (await f.post('', { preset: 'day', count: 1, requestId: 'identity-kinds-0001' })).body.licenses[0];
  await f.activate(created.key, device).expect(200);
  const state = (await f.get()).body;
  expect(state.devices.map((d: { deviceId: string }) => d.deviceId)).toContain(device);
  expect(state.devices.map((d: { deviceId: string }) => d.deviceId)).not.toContain('socket-broadcaster-01');
  await f.post(`/${created.id}/rebind`, { deviceId: 'socket-broadcaster-01', revision: state.licenses[0].revision, requestId: 'wrong-kind-0001' }).expect(400);
});

test('old license schema migrates without touching fixed expiry, disabled state or binding', () => {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-old-license-')), path = join(directory, 'old.sqlite');
  const key = generateLicenseKey(); let db = new Database(path);
  try {
    db.exec(`CREATE TABLE licenses(id INTEGER PRIMARY KEY AUTOINCREMENT,key_hash TEXT NOT NULL UNIQUE,label TEXT NOT NULL DEFAULT '',
      expires_at INTEGER,disabled_at INTEGER,bound_device_id TEXT,created_at INTEGER NOT NULL,updated_at INTEGER NOT NULL);`);
    db.prepare('INSERT INTO licenses VALUES(1,?,?,?,?,?,?,?)').run(hashLicenseKey(key), 'old', 1700100000, 1700000100, device, 1700000000, 1700000100);
    db.close(); db = openDatabase(path);
    expect(listLicenses(db)[0]).toMatchObject({ id: 1, expiresAt: 1700100000, disabledAt: 1700000100, boundDeviceId: device,
      createdAt: 1700000000, updatedAt: 1700000100, revision: 0, activationMode: 'fixed', hasKey: false });
    db.close(); db = openDatabase(path);
    expect(listLicenses(db)).toHaveLength(1);
  } finally { if (db.open) db.close(); rmSync(directory, { recursive: true, force: true }); }
});
