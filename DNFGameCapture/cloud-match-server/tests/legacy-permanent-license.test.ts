import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import request from 'supertest';
import { afterEach, expect, test, vi } from 'vitest';
import { createCloudMatchApp } from '../src/app.js';
import { generateLicenseKey, isNativeLicenseKey } from '../src/auth.js';
import { openDatabase } from '../src/db.js';
import { activateStoredLicense, createLicense, listLicenses, revealLicense } from '../src/license-store.js';
import { LICENSE_ADMIN_JS } from '../src/license-admin-page.js';

const resources: { directory: string; app: ReturnType<typeof createCloudMatchApp> }[] = [];
const now = 1_800_000_000, password = 'legacy-fixture-password', csrf = 'legacy-fixture-csrf';
const device = 'legacy-device-0001', other = 'legacy-device-0002';
const oldKey = 'CDK-FFFFFFFF-458E-A78EEAC3';
const otherKey = 'CDK-FFFFFFFF-1C8D-815E276D';

function fixture(enabled = true) {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-legacy-license-'));
  const databasePath = join(directory, 'test.sqlite');
  const app = createCloudMatchApp({ databasePath, now: () => now, adminPassword: password, adminCsrfToken: csrf,
    allowLegacyPermanentKeys: enabled, v2ServerUrl: 'http://127.0.0.1:28880' });
  resources.push({ directory, app });
  const activate = (key = oldKey, deviceId = device) => request(app.expressApp).post('/api/v2/auth/activate').send({ key, deviceId });
  const post = (path: string, body: object) => request(app.adminExpressApp).post('/admin/api/licenses' + path)
    .auth('admin', password).set('x-dnf-admin-csrf', csrf).send(body);
  return { app, databasePath, activate, post };
}

afterEach(async () => {
  vi.unstubAllEnvs();
  for (const { app, directory } of resources.splice(0)) {
    await app.close();
    rmSync(directory, { recursive: true, force: true });
  }
});

test('previously issued permanent card enrolls without OSS and returns a usable v2 session', async () => {
  const f = fixture();
  expect(isNativeLicenseKey(oldKey)).toBe(true);
  const activated = await f.activate().expect(200);
  expect(activated.body).toMatchObject({ ok: true, licenseExpiresAt: 0xFFFFFFFF, cloudServerUrl: 'http://127.0.0.1:28880' });
  await request(f.app.expressApp).get('/api/v2/player-library').set('Authorization', 'Bearer ' + activated.body.sessionToken)
    .set('x-dnf-device-id', device).expect(200);
  const cards = listLicenses(f.app.db);
  expect(cards).toHaveLength(1);
  expect(cards[0]).toMatchObject({ label: '旧永久卡自动登记', expiresAt: null, boundDeviceId: device, activatedAt: now, hasKey: true });
  expect((await f.post(`/${cards[0].id}/reveal`, {}).expect(200)).body.key).toBe(oldKey);
  const audit = f.app.db.prepare('SELECT action,details_json FROM license_audit ORDER BY id').all();
  expect(audit).toEqual(expect.arrayContaining([expect.objectContaining({ action: 'enroll_legacy' }), expect.objectContaining({ action: 'activate' })]));
  for (const value of [cards, audit, f.app.db.prepare('SELECT * FROM licenses').all()]) expect(JSON.stringify(value)).not.toContain(oldKey);
  expect(LICENSE_ADMIN_JS).toContain("enroll_legacy: '旧永久卡登记'");
});

test('all three supplied old cards pass their existing signature rules and can enroll', async () => {
  const f = fixture();
  for (const [index, key] of [oldKey, otherKey, 'CDK-FFFFFFFF-252D-2B70419A'].entries()) {
    await f.activate(key, 'legacy-device-' + index).expect(200);
  }
  expect(listLicenses(f.app.db)).toHaveLength(3);
});

test('repeated and normalized activation preserves one binding and one enrollment record', async () => {
  const f = fixture();
  await f.activate().expect(200);
  const before = listLicenses(f.app.db)[0];
  await f.activate('  ' + oldKey.toLowerCase() + '  ').expect(200);
  expect(listLicenses(f.app.db)).toEqual([before]);
  expect(f.app.db.prepare("SELECT id FROM license_audit WHERE action='enroll_legacy'").all()).toHaveLength(1);
  await f.activate(oldKey, other).expect(409, { ok: false, code: 'license_bound_to_other_device' });
});

test('two devices racing to enroll one unknown old card cannot both claim it', async () => {
  const f = fixture();
  const responses = await Promise.all([f.activate(oldKey, device), f.activate(oldKey, other)]);
  expect(responses.map(response => response.status).sort()).toEqual([200, 409]);
  expect(listLicenses(f.app.db)).toHaveLength(1);
  expect(f.app.db.prepare('SELECT * FROM auth_sessions').all()).toHaveLength(1);
});

test('finite, malformed, tampered and alternate-width cards never auto-enroll', async () => {
  const f = fixture();
  const invalid = [generateLicenseKey(3600), generateLicenseKey(), 'CDK-FFFFFFFF-458E-A78EEAC2', 'CDK-FFFFFFFF-458E-',
    oldKey + '-EXTRA', oldKey.replace('FFFFFFFF', '0FFFFFFFF'), oldKey.replace('A78EEAC3', '0A78EEAC3'),
    oldKey.replace('458E', '458F'), 'DNF-FFFFFFFF-458E-A78EEAC3', oldKey.replace('458E', '458E\n')];
  for (const key of invalid) await f.activate(key).expect(401, { ok: false, code: 'invalid_license' });
  expect(listLicenses(f.app.db)).toEqual([]);
  expect(f.app.db.prepare('SELECT * FROM auth_sessions').all()).toEqual([]);
  expect(f.app.db.prepare('SELECT * FROM license_audit').all()).toEqual([]);
});

test('old generator zero-padded signatures are accepted at their original width only', async () => {
  const f = fixture();
  let key = '';
  for (let nonce = 0; nonce < 0xFFFF; nonce++) {
    const nonceText = nonce.toString(16).toUpperCase().padStart(4, '0');
    let hash = 5381;
    for (const char of `FFFFFFFF-${nonceText}-MySuperSecretKey2026`) hash = (hash * 33 + char.charCodeAt(0)) >>> 0;
    const signature = hash.toString(16).toUpperCase().padStart(8, '0');
    if (signature.startsWith('0')) { key = `CDK-FFFFFFFF-${nonceText}-${signature}`; break; }
  }
  expect(key).not.toBe('');
  await f.activate(key).expect(200);
  const short = key.split('-'); short[3] = short[3].replace(/^0+/, '');
  await f.activate(short.join('-'), other).expect(401);
  expect(listLicenses(f.app.db)).toHaveLength(1);
});

test('existing disabled and expired cards cannot be revived by the legacy fallback', async () => {
  const f = fixture();
  createLicense(f.app.db, { key: oldKey, nowSec: now });
  f.app.db.prepare('UPDATE licenses SET disabled_at=?').run(now);
  const before = listLicenses(f.app.db);
  await f.activate().expect(403, { ok: false, code: 'license_disabled' });
  await f.activate(oldKey.replace('FFFFFFFF', '0FFFFFFFF')).expect(401);
  expect(listLicenses(f.app.db)).toEqual(before);
  f.app.db.prepare('UPDATE licenses SET disabled_at=NULL,expires_at=?').run(now - 1);
  await f.activate().expect(403, { ok: false, code: 'license_expired' });
  expect(listLicenses(f.app.db)[0].expiresAt).toBe(now - 1);
  expect(f.app.db.prepare("SELECT id FROM license_audit WHERE action='enroll_legacy'").all()).toEqual([]);
});

test('an existing finite admin record wins even when its printed key says permanent', async () => {
  const f = fixture();
  createLicense(f.app.db, { key: oldKey, nowSec: now, expiresAt: now + 3600 });
  expect((await f.activate().expect(200)).body.licenseExpiresAt).toBe(now + 3600);
  expect(listLicenses(f.app.db)[0].expiresAt).toBe(now + 3600);
});

test.each(['disabled', 'expired', 'bound'])('canonical enrollment cannot bypass an existing %s padded spelling', async state => {
  const f = fixture();
  const padded = oldKey.replace('FFFFFFFF', '000FFFFFFFF').replace('A78EEAC3', '000A78EEAC3');
  expect(isNativeLicenseKey(padded)).toBe(true);
  createLicense(f.app.db, { key: padded, nowSec: now });
  f.app.db.prepare('UPDATE licenses SET disabled_at=?,expires_at=?,bound_device_id=?,key_ciphertext=NULL')
    .run(state === 'disabled' ? now : null, state === 'expired' ? now - 1 : null, state === 'bound' ? other : null);
  const before = listLicenses(f.app.db);
  await f.activate().expect(409, { ok: false, code: 'license_already_exists' });
  expect(listLicenses(f.app.db)).toEqual(before);
  expect(f.app.db.prepare('SELECT * FROM auth_sessions').all()).toEqual([]);
  await f.activate(padded).expect(state === 'bound' ? 409 : 403);
});

test('equivalent hash-only spellings are checked to the existing 256-character API limit', async () => {
  const f = fixture();
  const padded = oldKey.replace('FFFFFFFF', '0'.repeat(256 - oldKey.length) + 'FFFFFFFF');
  expect(padded).toHaveLength(256);
  createLicense(f.app.db, { key: padded, nowSec: now });
  f.app.db.prepare('UPDATE licenses SET key_ciphertext=NULL').run();
  await f.activate().expect(409);
  expect(listLicenses(f.app.db)).toHaveLength(1);
  await f.activate(padded).expect(200);
});

test('enrolled cards support normal unbinding and disable without re-enrollment', async () => {
  const f = fixture();
  const first = await f.activate().expect(200);
  let card = listLicenses(f.app.db)[0];
  card = (await f.post(`/${card.id}/rebind`, { deviceId: null, revision: card.revision, requestId: 'legacy-unbind-01' }).expect(200)).body.license;
  await request(f.app.expressApp).post('/api/v2/auth/validate').send({ deviceId: device, sessionToken: first.body.sessionToken }).expect(401);
  await f.activate(oldKey, other).expect(200);
  await f.activate(oldKey, device).expect(409);
  card = listLicenses(f.app.db)[0];
  await f.post(`/${card.id}/disable`, { disabled: true, revision: card.revision, requestId: 'legacy-disable-01' }).expect(200);
  await f.activate(oldKey, other).expect(403);
  expect(listLicenses(f.app.db)).toHaveLength(1);
});

test('turning compatibility off rejects unknown cards but keeps registered cards usable', async () => {
  const f = fixture(false);
  await f.activate().expect(401);
  createLicense(f.app.db, { key: oldKey, nowSec: now });
  await f.activate().expect(200);
});

test.each(['enroll_legacy', 'activate'])('failed %s audit rolls back enrollment, binding and session together', async action => {
  const f = fixture();
  f.app.db.exec(`CREATE TRIGGER reject_legacy_audit BEFORE INSERT ON license_audit WHEN NEW.action='${action}'
    BEGIN SELECT RAISE(ABORT,'fixture audit failure'); END`);
  await f.activate().expect(500);
  expect(listLicenses(f.app.db)).toEqual([]);
  expect(f.app.db.prepare('SELECT * FROM license_audit').all()).toEqual([]);
  expect(f.app.db.prepare('SELECT * FROM auth_sessions').all()).toEqual([]);
  f.app.db.exec('DROP TRIGGER reject_legacy_audit');
  await f.activate().expect(200);
});

test('wrong vault key prevents unknown enrollment without breaking existing card authentication', async () => {
  const f = fixture();
  createLicense(f.app.db, { key: otherKey, nowSec: now });
  const backup = readFileSync(f.databasePath + '.license-key');
  await f.app.close();
  writeFileSync(f.databasePath + '.license-key', Buffer.alloc(32, 8));
  const app = createCloudMatchApp({ databasePath: f.databasePath, now: () => now, allowLegacyPermanentKeys: true });
  resources[resources.length - 1].app = app;
  await request(app.expressApp).post('/api/v2/auth/activate').send({ key: oldKey, deviceId: device }).expect(500);
  expect(listLicenses(app.db)).toHaveLength(1);
  await request(app.expressApp).post('/api/v2/auth/activate').send({ key: otherKey, deviceId: device }).expect(200);
  writeFileSync(f.databasePath + '.license-key', backup);
  await request(app.expressApp).post('/api/v2/auth/activate').send({ key: oldKey, deviceId: other }).expect(200);
});

test('enrolled identity, key and binding survive restart even without the compatibility flag', async () => {
  const f = fixture();
  await f.activate().expect(200);
  const before = listLicenses(f.app.db)[0];
  await f.app.close();
  const db = openDatabase(f.databasePath);
  try {
    expect(listLicenses(db)).toEqual([before]);
    expect(revealLicense(db, before.id)).toBe(oldKey);
    expect(activateStoredLicense(db, oldKey, device, now + 100, 3600).license.expiresAt).toBeNull();
    expect(() => activateStoredLicense(db, oldKey, other, now + 100, 3600)).toThrow('license_bound_to_other_device');
  } finally { db.close(); }
});

test.each([undefined, '', 'false', '0', 'yes', 'true', '1', ' TRUE '])('configuration only explicitly enables legacy enrollment: %s', async value => {
  vi.resetModules();
  vi.stubEnv('ALLOW_LEGACY_PERMANENT_KEYS', value);
  const { serverConfig } = await import('../src/config.js');
  expect(serverConfig.allowLegacyPermanentKeys).toBe(['true', '1', ' TRUE '].includes(value ?? ''));
});
