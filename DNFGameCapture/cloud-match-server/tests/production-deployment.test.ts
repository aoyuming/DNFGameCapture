import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import request from 'supertest';
import { afterEach, expect, test } from 'vitest';
import { createCloudMatchApp } from '../src/app.js';
import { createBroadcasterAttributionService } from '../src/broadcaster-attribution.js';
import { registerDevice } from '../src/identity.js';
import { mutatePublicLibrary } from '../src/library-admin-data.js';
import { listPlayerLibrary } from '../src/library-store.js';
import { createLicense, listLicenses, revealLicense } from '../src/license-store.js';
import { joinUnifiedPool } from '../src/unified.js';

const require = createRequire(import.meta.url);
const deploy = new URL('../deploy/', import.meta.url);
const productionUrl = 'http://47.109.149.111:18880';
type Prepared = { text: string; databasePath: string; allowLegacyPermanentKeys: boolean };
function prepare(text: string): Prepared {
  const path = new URL('production-env.cjs', deploy);
  expect(existsSync(path), 'production environment preparation helper').toBe(true);
  return require(fileURLToPath(path)).prepareProductionEnvironment(text);
}

test('existing installations cannot silently choose a database when DATABASE_PATH was never configured', () => {
  const helper = require(fileURLToPath(new URL('production-env.cjs', deploy)));
  expect(() => helper.prepareProductionEnvironment('ADMIN_PASSWORD=keep\n', undefined, true)).toThrow(/database/i);
});

test.each([{ ok: true, protocolVersion: 1 }, { ok: true, protocolVersion: 2, cloudServerUrl: 'http://localhost:28880', allowLegacyPermanentKeys: true },
  { ok: true, protocolVersion: 2, cloudServerUrl: productionUrl },
  { ok: true, protocolVersion: 2, cloudServerUrl: productionUrl, allowLegacyPermanentKeys: false }])('preflight rejects incomplete or mismatched runtime metadata: %j', body => {
  const helper = require(fileURLToPath(new URL('production-env.cjs', deploy)));
  expect(() => helper.checkHealth(body, true)).toThrow(/mismatch/i);
});

test('production environment explicitly advertises production routing and missing legacy policy defaults on', () => {
  const prepared = prepare('ADMIN_PASSWORD=keep-this-password\nCUSTOM_OPTION=preserved\n');
  for (const line of ['NODE_ENV=production', 'PORT=18880', 'ADMIN_PORT=18881', `PUBLIC_URL=${productionUrl}`,
    'ALLOW_LEGACY_PERMANENT_KEYS=true', 'ADMIN_PASSWORD=keep-this-password', 'CUSTOM_OPTION=preserved']) {
    expect(prepared.text.split('\n')).toContain(line);
  }
  expect(prepared.databasePath).toBe('/var/lib/dnf-cloud-match/cloud-match.sqlite');
  expect(prepared.allowLegacyPermanentKeys).toBe(true);
  expect(prepare(prepared.text)).toEqual(prepared);
});

test.each(['false', '0', '"false"', "'false'", ''])('explicit legacy policy %s is never enabled implicitly', value => {
  const prepared = prepare(`ADMIN_PASSWORD=keep\nALLOW_LEGACY_PERMANENT_KEYS=${value}\n`);
  expect(prepared.text).toContain(`ALLOW_LEGACY_PERMANENT_KEYS=${value}\n`);
  expect(prepared.allowLegacyPermanentKeys).toBe(false);
});

test('existing credentials, comments, custom production database and administrator binding survive preparation', () => {
  const source = '# private configuration\nADMIN_PASSWORD="password with # spaces"\nADMIN_HOST=127.0.0.1\n' +
    'DATABASE_PATH=/var/lib/dnf-cloud-match/custom.sqlite\nPRIVATE_OPTION=untouched\nPORT=28880\nPUBLIC_URL=http://127.0.0.1:28880\n';
  const prepared = prepare(source);
  for (const line of source.split('\n').filter(line => line && !/^(PORT|PUBLIC_URL)=/.test(line))) expect(prepared.text).toContain(line + '\n');
  expect(prepared.databasePath).toBe('/var/lib/dnf-cloud-match/custom.sqlite');
  expect(prepared.text).not.toContain('28880');
});

test('an empty admin password is generated once and is not a known test credential', () => {
  const result = prepare('ADMIN_PASSWORD=\n');
  expect(result.text).toMatch(/ADMIN_PASSWORD=[a-f0-9]{48}\n/);
  expect(prepare(result.text)).toEqual(result);
});

test.each(['/var/lib/dnf-cloud-match-test/cloud-match.sqlite', ':memory:', '../test.sqlite',
  '/var/lib/dnf-cloud-match/../test.sqlite', '/var/lib/dnf-cloud-match/link/../../test.sqlite'])('unsafe database %s is rejected before deployment', path => {
  expect(() => prepare(`DATABASE_PATH=${path}\n`)).toThrow(/database/i);
});

test.each(['PORT=18880\nPORT=28880\n', 'export DATABASE_PATH=/var/lib/dnf-cloud-match/a.sqlite\n',
  'ADMIN_PASSWORD="multi\nline"\n'])('ambiguous environment fails closed: %s', source => {
  expect(() => prepare(source)).toThrow(/environment/i);
});

const apps: ReturnType<typeof createCloudMatchApp>[] = [];
const directories: string[] = [];
afterEach(async () => {
  for (const app of apps.splice(0)) await app.close();
  for (const path of directories.splice(0)) rmSync(path, { recursive: true, force: true });
});

test.each([true, false])('v2 deployment probe reports effective compatibility %s without creating keys or sessions', async enabled => {
  const app = createCloudMatchApp({ databasePath: ':memory:', v2ServerUrl: productionUrl, allowLegacyPermanentKeys: enabled });
  apps.push(app);
  const before = app.db.prepare('SELECT total_changes() AS changes').get();
  await request(app.expressApp).get('/health').expect(200, { ok: true });
  await request(app.expressApp).get('/api/v2/health').expect(200, {
    ok: true, protocolVersion: 2, cloudServerUrl: productionUrl, allowLegacyPermanentKeys: enabled,
  });
  await request(app.expressApp).get('/api/v2/player-library').expect(401);
  expect(app.db.prepare('SELECT total_changes() AS changes').get()).toEqual(before);
});

test('production restart preserves library, session, registered license and vault while real old Keygen cards enroll', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-production-preserve-'));
  directories.push(directory);
  const databasePath = join(directory, 'cloud-match.sqlite');
  const options = { databasePath, now: () => 1_800_000_000, v2ServerUrl: productionUrl, allowLegacyPermanentKeys: true };
  const old = createCloudMatchApp(options);
  apps.push(old);
  const registered = createLicense(old.db, { key: 'CDK-FFFFFFFF-458E-A78EEAC3', nowSec: options.now() });
  mutatePublicLibrary(old.db, 0, options.now(), 'create', [{ entityId: 'preserved', names: ['Old player'], gameIds: ['Game ID'] }]);
  const activate = (app: typeof old, key: string, deviceId: string) => request(app.expressApp).post('/api/v2/auth/activate').send({ key, deviceId });
  const first = await activate(old, 'CDK-FFFFFFFF-458E-A78EEAC3', 'production-existing-device').expect(200);
  expect(registerDevice(old.db, 'production-broadcaster', options.now())).not.toBeNull();
  expect(joinUnifiedPool(old.db, 'production-broadcaster', '生产主播', options.now())).not.toBeNull();
  const attribution = createBroadcasterAttributionService(old.db, { resolveRegion: () => '中国 · 浙江 · 杭州' });
  attribution.connectBroadcaster({ deviceId: 'production-broadcaster', broadcasterName: '生产主播',
    ipAddress: '47.1.2.3', observedAt: options.now() });
  attribution.manualLink('production-broadcaster', registered.id, options.now());
  const library = listPlayerLibrary(old.db), cards = listLicenses(old.db);
  const vault = readFileSync(databasePath + '.license-key');
  await old.close(); apps.pop();
  const next = createCloudMatchApp(options); apps.push(next);
  const restoredAttribution = createBroadcasterAttributionService(next.db);
  expect(listPlayerLibrary(next.db)).toEqual(library);
  expect(listLicenses(next.db)).toEqual(cards);
  expect(revealLicense(next.db, registered.id)).toBe('CDK-FFFFFFFF-458E-A78EEAC3');
  expect(restoredAttribution.getBroadcasterAttribution('production-broadcaster')).toMatchObject({
    licenseId: registered.id, broadcasterName: '生产主播', source: 'manual',
  });
  expect(restoredAttribution.getBroadcasterNetwork('production-broadcaster')).toMatchObject({
    currentIp: null, lastIp: '47.1.2.3', region: '中国 · 浙江 · 杭州',
  });
  await request(next.expressApp).get('/api/v2/player-library').set('Authorization', 'Bearer ' + first.body.sessionToken)
    .set('x-dnf-device-id', 'production-existing-device').expect(200);
  // Previously supplied output from the original native Keygen, not newly generated server keys.
  for (const key of ['CDK-FFFFFFFF-1C8D-815E276D', 'CDK-FFFFFFFF-252D-2B70419A']) {
    const response = await activate(next, key, 'production-old-card-device').expect(200);
    expect(response.body).toMatchObject({ cloudServerUrl: productionUrl, licenseExpiresAt: 0xFFFFFFFF });
    await activate(next, key, 'another-device').expect(409);
  }
  expect(readFileSync(databasePath + '.license-key')).toEqual(vault);
  expect(listPlayerLibrary(next.db)).toEqual(library);
});
