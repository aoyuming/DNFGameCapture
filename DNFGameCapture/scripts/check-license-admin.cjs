// Uses source modules; no build, production DB, installed dependencies or live server is changed.
// Run: NODE_PATH=<existing Playwright modules> node scripts/check-license-admin.cjs
const assert = require('node:assert/strict');
const { createRequire } = require('node:module');
const { mkdtempSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');
const serverRequire = createRequire(path.resolve(__dirname, '../cloud-match-server/package.json'));
serverRequire('tsx/cjs');
const { chromium } = require('playwright');
const express = serverRequire('express');
const request = serverRequire('supertest');
const load = name => serverRequire('./src/' + name + '.ts');
const { buildLicenseAdminPage, LICENSE_ADMIN_JS, LICENSE_ADMIN_CSS } = load('license-admin-page');
const { generateLicenseKey } = load('auth');

const csrf = 'fixture-<"&-csrf';
const password = 'isolated-license-smoke';
const authorization = 'Basic ' + Buffer.from('admin:' + password).toString('base64');
const now = Math.floor(Date.now() / 1000);
const presets = [
  ['hour', '小时卡', 3600], ['day', '天卡', 86400], ['week', '周卡', 604800],
  ['ten_days', '十天卡', 864000], ['quarter', '季度卡', 7776000],
  ['half_year', '半年卡', 15552000], ['year', '年卡', 31536000], ['permanent', '永久卡', null],
].map(([id, label, durationSeconds]) => ({ id, label, durationSeconds }));
const devices = [{ deviceId: 'device-known', name: '已知设备',
  broadcasterName: '激活主播 · 已知设备 <img src=x onerror=alert(1)>', broadcasterDeviceId: 'fixture-broadcaster' }];

function fixture() {
  const keys = new Map(), events = new Map(), replay = new Map(), writes = [];
  const licenses = [
    { id: 1, label: '<script>window.injected=1</script>', activationMode: 'first_use', activatedAt: null, durationSeconds: 3600, expiresAt: null },
    { id: 2, label: '旧卡', activationMode: 'fixed', activatedAt: null, durationSeconds: null, expiresAt: now - 3600, hasKey: false },
    { id: 3, label: '永久卡', activationMode: 'first_use', activatedAt: null, durationSeconds: null, expiresAt: null },
    { id: 4, label: '已激活', activationMode: 'first_use', activatedAt: now - 60, durationSeconds: 86400, expiresAt: now + 86400 },
    { id: 5, label: '旧永久卡', activationMode: 'fixed', activatedAt: null, durationSeconds: null, expiresAt: null },
  ].map(item => ({ disabledAt: null, boundDeviceId: null, revision: 1, hasKey: true, createdAt: now, updatedAt: now, ...item }));
  licenses.forEach(item => keys.set(item.id, generateLicenseKey()));
  const app = express();
  app.use((req, res, next) => {
    res.set('Cache-Control', 'no-store');
    res.set('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; form-action 'none'");
    if (req.get('authorization') !== authorization) {
      res.set('WWW-Authenticate', 'Basic realm="Fixture"'); return res.sendStatus(401);
    }
    if (req.method !== 'GET' && req.get('x-dnf-admin-csrf') !== csrf) return res.status(403).json({ ok: false, code: 'invalid_csrf' });
    next();
  });
  app.use(express.json());
  app.get('/admin/licenses', (_req, res) => res.type('html').send(buildLicenseAdminPage(csrf)));
  app.get('/admin/licenses/app.js', (_req, res) => res.type('js').send(LICENSE_ADMIN_JS));
  app.get('/admin/licenses/style.css', (_req, res) => res.type('css').send(LICENSE_ADMIN_CSS || ''));
  app.get('/admin/api/licenses', (_req, res) => res.json({ ok: true, licenses, presets, devices, maxBatch: 200 }));
  app.get('/admin/api/licenses/:id/audit', (req, res) => res.json({ ok: true, events: events.get(Number(req.params.id)) || [
    { id: 1, action: 'create', createdAt: now, details: { label: '<img src=x onerror=alert(1)>', preset: 'hour' } },
  ] }));
  app.post('/admin/api/licenses/:id/reveal', (req, res) => {
    const item = licenses.find(item => item.id === Number(req.params.id));
    if (!item?.hasKey) return res.status(409).json({ ok: false, code: 'key_unavailable' });
    res.json({ ok: true, key: keys.get(item.id) });
  });
  app.post(['/admin/api/licenses', '/admin/api/licenses/:id/:action'], (req, res) => {
    const body = req.body, id = Number(req.params.id), action = req.params.action || 'create';
    writes.push({ action, id, body: structuredClone(body) });
    assert.equal(typeof body.requestId, 'string'); assert(body.requestId.length >= 16);
    const signature = JSON.stringify([req.path, body]);
    if (replay.has(body.requestId)) {
      const old = replay.get(body.requestId);
      if (old.signature !== signature) return res.status(409).json({ ok: false, code: 'request_id_conflict' });
      return res.json({ ...old.result, replayed: true });
    }
    const preset = presets.find(item => item.id === body.preset);
    let result;
    if (action === 'create') {
      if (!preset || !Number.isInteger(body.count) || body.count < 1 || body.count > 200 || (body.key && body.count !== 1)) {
        return res.status(400).json({ ok: false, code: 'invalid_request' });
      }
      const batch = [];
      for (let i = 0; i < body.count; i++) {
        const item = { id: licenses.length + 1, label: body.label, activationMode: 'first_use', activatedAt: null,
          durationSeconds: preset.durationSeconds, expiresAt: null, disabledAt: null, boundDeviceId: null, revision: 1, hasKey: true };
        keys.set(item.id, body.key || generateLicenseKey()); licenses.push(item); batch.push({ ...item, key: keys.get(item.id) });
      }
      result = { ok: true, licenses: batch, replayed: false };
    } else {
      const item = licenses.find(item => item.id === id);
      if (body.revision !== item.revision) return res.status(409).json({ ok: false, code: 'stale_license' });
      if (action === 'extend') {
        if (!preset) return res.status(400).json({ ok: false, code: 'invalid_preset' });
        if (item.activationMode === 'first_use' && item.activatedAt === null) {
          item.durationSeconds = item.durationSeconds === null || preset.durationSeconds === null ? null : item.durationSeconds + preset.durationSeconds;
        } else if (item.expiresAt !== null) {
          item.expiresAt = preset.durationSeconds === null ? null : Math.max(now, item.expiresAt) + preset.durationSeconds;
        }
      } else if (action === 'rebind') {
        if (body.deviceId !== null && !devices.some(device => device.deviceId === body.deviceId)) return res.status(400).json({ ok: false, code: 'unknown_device' });
        item.boundDeviceId = body.deviceId;
      } else if (action === 'disable') item.disabledAt = body.disabled ? now : null;
      else if (action === 'key') {
        if (body.key !== keys.get(id)) return res.status(400).json({ ok: false, code: 'key_mismatch' });
        item.hasKey = true;
      } else return res.sendStatus(404);
      item.revision++;
      events.set(id, [{ id: 2, action, createdAt: now, details: { revision: item.revision } }]);
      result = { ok: true, license: { ...item } };
    }
    replay.set(body.requestId, { signature, result: structuredClone(result) });
    res.json(result);
  });
  return { app, licenses, keys, writes };
}

async function realSecurity() {
  const { openDatabase } = load('db');
  const { createCloudMatchAdminApp } = load('admin');
  const db = openDatabase(':memory:');
  try {
    const app = createCloudMatchAdminApp({ db, now: () => now, csrfToken: csrf, adminPassword: password,
      socketController: { getActiveDeviceIds: () => new Set(), disconnectDevice: () => false, stopRealtimeViewer: () => false, notifyDirectoryChanged() {} } });
    for (const route of ['/admin/licenses', '/admin/licenses/style.css', '/admin/licenses/app.js', '/admin/api/licenses', '/admin/api/licenses/1/audit']) {
      await request(app).get(route).expect(401);
    }
    for (const route of ['', '/1/reveal', '/1/key', '/1/extend', '/1/rebind', '/1/disable']) {
      await request(app).post('/admin/api/licenses' + route).expect(401);
      await request(app).post('/admin/api/licenses' + route).auth('admin', password).send({}).expect(403);
      await request(app).post('/admin/api/licenses' + route).auth('admin', password).set('x-dnf-admin-csrf', 'wrong').send({}).expect(403);
    }
    const page = await request(app).get('/admin/licenses').auth('admin', password).expect(200);
    assert(page.headers['content-security-policy'].includes("script-src 'self'"));
    console.log('PASS: real isolated admin app authentication, CSRF and CSP');
  } finally { db.close(); }
}

async function realBrowser(browser, output) {
  const { openDatabase } = load('db');
  const { createCloudMatchAdminApp } = load('admin');
    const store = load('license-store');
    const { createBroadcasterAttributionService } = load('broadcaster-attribution');
    const { registerDevice } = load('identity');
    const { joinUnifiedPool } = load('unified');
  const db = openDatabase(':memory:');
  let server, context;
  try {
    registerDevice(db, 'known-device-a', now); registerDevice(db, 'known-device-b', now);
    const originalKey = generateLicenseKey();
    const legacy = store.createLicense(db, { key: originalKey, label: '旧卡 <script>bad()</script>', expiresAt: now - 60, nowSec: now });
    db.prepare('UPDATE licenses SET key_ciphertext=NULL WHERE id=?').run(legacy.id);
    const permanent = store.createLicense(db, { key: generateLicenseKey(), label: '永久', expiresAt: null, nowSec: now });
    store.activateStoredLicense(db, store.revealLicense(db, permanent.id), 'known-device-b', now, 3600);
    registerDevice(db, 'real-broadcaster', now); joinUnifiedPool(db, 'real-broadcaster', '真实激活主播', now);
    const attribution = createBroadcasterAttributionService(db);
    attribution.manualLink('real-broadcaster', permanent.id, now);
    const disconnected = [];
    const app = createCloudMatchAdminApp({ db, now: () => now, csrfToken: csrf, adminPassword: password,
      socketController: { getActiveDeviceIds: () => new Set(), disconnectDevice: id => { disconnected.push(id); return true; }, stopRealtimeViewer: () => false, notifyDirectoryChanged() {} }, attribution });
    server = await new Promise(resolve => { const s = app.listen(0, '127.0.0.1', () => resolve(s)); });
    const url = 'http://127.0.0.1:' + server.address().port;
    context = await browser.newContext({ httpCredentials: { username: 'admin', password }, viewport: { width: 1366, height: 900 } });
    const page = await context.newPage(), errors = [], writes = [];
    page.setDefaultTimeout(5000);
    page.on('pageerror', error => errors.push(error.message));
    page.on('request', req => { if (req.method() === 'POST') writes.push({ url: req.url(), body: req.postDataJSON() }); });
    await page.route('**/*', route => new URL(route.request().url()).origin === url ? route.continue() : route.abort());
    const idle = () => page.waitForFunction(() => !document.body.hasAttribute('aria-busy'));
    const click = async selector => { await page.locator(selector).click(); await idle(); };
    const row = id => page.locator('#license-list tr[data-license-id="' + id + '"]');
    const action = async (id, name) => { await row(id).getByRole('button', { name, exact: true }).click(); await idle(); };
    async function lostWrite(pathname, submitSelector, closeBeforeRetry = false) {
      const routeUrl = url + '/admin/api/licenses' + pathname;
      let first = true, rejected;
      await page.route(routeUrl, async route => {
        if (first) {
          first = false; const response = await route.fetch();
          if (!response.ok()) { rejected = await response.text(); await route.fulfill({ response }); }
          else await route.abort('failed');
        }
        else await route.continue();
      });
      const before = writes.length;
      await page.locator(submitSelector).evaluate(button => { button.click(); button.click(); }); await idle();
      assert.equal(rejected, undefined, 'real request rejected: ' + pathname);
      assert.equal(writes.length, before + 1, 'one real write on double-click');
      if (closeBeforeRetry) await click('#dialog-cancel');
      const retry = await page.locator('#license-dialog').isVisible() ? '#dialog-retry' : '#retry-mutation';
      await click(retry); assert.equal(writes.length, before + 2);
      assert.deepEqual(writes[before], writes[before + 1], 'real replay reuses complete mutation');
      await page.unroute(routeUrl);
    }
    await page.goto(url + '/admin/licenses'); await idle();
    const cssResponse = await request(app).get('/admin/licenses/style.css').auth('admin', password).expect(200);
    assert.equal(cssResponse.text, LICENSE_ADMIN_CSS, 'parent mounts composed license stylesheet');
    assert.equal(await page.locator('#license-preset option').count(), 8);
    assert.match(await row(legacy.id).innerText(), /已过期/);
    assert.match(await row(permanent.id).innerText(), /真实激活主播/);
    await page.locator('#license-quantity').fill('2'); await page.locator('#license-preset').selectOption('hour');
    await page.locator('#license-label').fill('真实批次 <img src=x onerror=alert(1)>');
    await lostWrite('', '#create-license');
    assert.equal(store.listLicenses(db).length, 4);
    const generated = store.listLicenses(db).filter(item => item.activationMode === 'first_use').sort((a, b) => a.id - b.id);
    const issued = await page.locator('#issued-keys input[readonly]').evaluateAll(nodes => nodes.map(node => node.value));
    assert.deepEqual(issued, generated.map(item => store.revealLicense(db, item.id)));
    const id = generated[0].id;
    const listing = await request(app).get('/admin/api/licenses').auth('admin', password).expect(200);
    for (const key of issued) assert(!JSON.stringify(listing.body).includes(key));
    assert(listing.body.licenses.every(item => !('key' in item) && typeof item.hasKey === 'boolean'));
    await action(id, '延长');
    store.activateStoredLicense(db, issued[0], 'known-device-a', now, 3600);
    await click('#dialog-submit');
    assert.match(await page.locator('#dialog-error').innerText(), /其他操作更新/);
    assert.equal(await page.locator('#dialog-submit').isDisabled(), true);
    assert.equal(store.getManagedLicense(db, id).expiresAt, now + 3600);
    await click('#dialog-cancel');
    await action(id, '延长'); await page.locator('#dialog-preset').selectOption('day');
    await lostWrite('/' + id + '/extend', '#dialog-submit', true);
    assert.equal(store.getManagedLicense(db, id).expiresAt, now + 90000);
    assert.match(await row(id).innerText(), /可用/);
    await page.setViewportSize({ width: 390, height: 844 });
    await action(id, '换绑'); await page.locator('#dialog-device').selectOption('known-device-b');
    assert.equal(await page.locator('#dialog-device option').count(), 3, 'only known devices and null unbind');
    assert(await page.locator('#license-dialog').evaluate(node => node.scrollWidth <= node.clientWidth));
    await page.screenshot({ path: path.join(output, 'real-rebind-390.png') });
    await lostWrite('/' + id + '/rebind', '#dialog-submit');
    assert.equal(store.getManagedLicense(db, id).boundDeviceId, 'known-device-b');
    assert.equal(store.getManagedLicense(db, id).expiresAt, now + 90000);
    assert.equal(db.prepare('SELECT COUNT(*) AS n FROM auth_sessions WHERE license_id=?').get(id).n, 0);
    assert.equal(disconnected.length, 0, 'authorization machine IDs are not lobby connection IDs');
    await action(id, '换绑'); await page.locator('#dialog-device').selectOption(''); await click('#dialog-submit');
    assert.equal(store.getManagedLicense(db, id).boundDeviceId, null);
    await action(id, '禁用'); await lostWrite('/' + id + '/disable', '#dialog-submit');
    assert.match(await row(id).innerText(), /已禁用/);
    await action(id, '启用'); await click('#dialog-submit');
    await action(legacy.id, '补录密钥'); await page.locator('#dialog-key').fill(generateLicenseKey()); await click('#dialog-submit');
    assert.match(await page.locator('#dialog-error').innerText(), /不匹配/);
    await page.locator('#dialog-key').fill(originalKey); await lostWrite('/' + legacy.id + '/key', '#dialog-submit');
    assert.equal(store.revealLicense(db, legacy.id), originalKey);
    assert.equal(store.getManagedLicense(db, legacy.id).expiresAt, now - 60, 'supplementation preserves old expiry');
    await action(legacy.id, '显示密钥'); assert.equal(await row(legacy.id).locator('input[readonly]').inputValue(), originalKey);
    await action(legacy.id, '延长'); await page.locator('#dialog-preset').selectOption('hour'); await click('#dialog-submit');
    assert.equal(store.getManagedLicense(db, legacy.id).expiresAt, now + 3600, 'expired fixed card extends from now');
    await action(legacy.id, '延长'); await page.locator('#dialog-preset').selectOption('permanent'); await click('#dialog-submit');
    assert.equal(store.getManagedLicense(db, legacy.id).expiresAt, null);
    const revision = store.getManagedLicense(db, permanent.id).revision;
    await action(permanent.id, '延长'); await click('#dialog-submit');
    assert.equal(store.getManagedLicense(db, permanent.id).revision, revision, 'permanent extension is a no-op');
    await action(id, '记录');
    assert.match(await page.locator('#audit-list').innerText(), /延长/);
    assert.match(await page.locator('#audit-list').innerText(), /换绑/);
    assert.doesNotMatch(await page.locator('#audit-list').innerText(), /附加信息|其他操作/);
    const audit = store.licenseAudit(db, id);
    assert.equal(audit.filter(event => event.action === 'extend').length, 1, 'replayed writes are not audited twice');
    for (const key of [...issued, originalKey]) assert(!JSON.stringify(audit).includes(key));
    await page.screenshot({ path: path.join(output, 'real-audit-390.png') }); await click('#dialog-cancel');
    await page.setViewportSize({ width: 1366, height: 900 });
    await page.screenshot({ path: path.join(output, 'real-list-1366.png'), fullPage: true });
    assert.equal(await page.locator('#license-list script, #license-list img').count(), 0);
    assert.deepEqual(errors, []);
    console.log('PASS: real memory SQLite + vault browser, parent CSS mount, atomic batch replay, activation/revision conflict, all mutation retries, unbind/session revocation, legacy recovery, fixed expiry and permanent no-op, audit');
  } finally {
    if (context) await context.close();
    if (server) await new Promise(resolve => server.close(resolve));
    db.close();
  }
}

(async () => {
  await realSecurity();
  const f = fixture();
  const server = await new Promise(resolve => { const s = f.app.listen(0, '127.0.0.1', () => resolve(s)); });
  const url = 'http://127.0.0.1:' + server.address().port;
  let browser;
  try {
    browser = await chromium.launch({ headless: true, channel: process.env.LICENSE_SMOKE_BROWSER || 'msedge' });
    const context = await browser.newContext({ httpCredentials: { username: 'admin', password }, viewport: { width: 1366, height: 900 } });
    const page = await context.newPage(), errors = [];
    page.setDefaultTimeout(5000);
    page.on('pageerror', error => errors.push(error.message));
    await page.route('**/*', route => new URL(route.request().url()).origin === url ? route.continue() : route.abort());
    const idle = () => page.waitForFunction(() => !document.body.hasAttribute('aria-busy'));
    const click = async selector => { await page.locator(selector).click(); await idle(); };
    const row = id => page.locator('#license-list tr[data-license-id="' + id + '"]');
    const action = async (id, label) => { await row(id).getByRole('button', { name: label, exact: true }).click(); await idle(); };
    const output = mkdtempSync(path.join(tmpdir(), 'dnf-license-admin-shots-'));
    async function screenshot(name) {
      for (const width of [1366, 390]) {
        await page.setViewportSize({ width, height: 900 });
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), name + ' overflow ' + width);
        await page.screenshot({ path: path.join(output, name + '-' + width + '.png'), fullPage: true });
      }
      await page.setViewportSize({ width: 1366, height: 900 });
    }
    await page.goto(url + '/admin/licenses'); await idle();
    assert.equal(await page.locator('#license-preset option').count(), 8, 'all approved presets');
    assert.equal(await page.locator('#license-quantity').getAttribute('max'), '200');
    assert.match(await row(1).innerText(), /未激活/); assert.match(await row(1).innerText(), /1小时时长/);
    assert.match(await row(2).innerText(), /已过期/); assert(!/未激活/.test(await row(2).innerText()));
    assert.match(await row(3).innerText(), /未激活/); assert.match(await row(3).innerText(), /永久/);
    assert.match(await row(4).innerText(), /可用/); assert.match(await row(5).innerText(), /永久/);
    assert.equal(await page.locator('#license-list script, #license-list img').count(), 0);
    assert.equal(await page.locator('#license-list').innerText().then(text => text.includes(f.keys.get(1))), false);
    await screenshot('list');

    await page.locator('#license-quantity').fill('2');
    assert.equal(await page.locator('#license-key').isDisabled(), true);
    await page.locator('#license-label').fill('批次 <img src=x onerror=alert(1)>');
    await page.locator('#license-preset').selectOption('day');
    let lostResponse = true;
    await page.route('**/admin/api/licenses', async route => {
      if (route.request().method() === 'POST' && lostResponse) {
        lostResponse = false; await route.fetch(); await route.abort('failed');
      } else await route.continue();
    });
    await page.locator('#create-license').evaluate(button => { button.click(); button.click(); }); await idle();
    assert.equal(f.writes.length, 1, 'double click sends one write');
    assert.equal(await page.locator('#retry-mutation').isVisible(), true);
    assert.equal(await page.locator('#create-license').isDisabled(), true, 'uncertain write cannot be replaced');
    await click('#retry-mutation');
    assert.equal(f.writes.length, 2); assert.deepEqual(f.writes[0], f.writes[1], 'retry retains entire request including requestId');
    assert.equal(f.licenses.length, 7, 'batch replay does not duplicate');
    assert.equal(await page.locator('#issued-keys input[readonly]').count(), 2);
    const batchKeys = await page.locator('#issued-keys input[readonly]').evaluateAll(nodes => nodes.map(node => node.value));
    assert.deepEqual(batchKeys, [f.keys.get(6), f.keys.get(7)]);
    await click('#refresh-licenses');
    assert.deepEqual(await page.locator('#issued-keys input[readonly]').evaluateAll(nodes => nodes.map(node => node.value)), batchKeys);
    assert.equal(await page.locator('#license-list tr').count(), 7, 'success rerenders the list');
    await page.unroute('**/admin/api/licenses');

    await context.grantPermissions(['clipboard-read', 'clipboard-write']);
    await page.evaluate(() => {
      window.readClipboard = navigator.clipboard.readText.bind(navigator.clipboard);
      window.execCopy = document.execCommand.bind(document);
      Object.defineProperty(navigator, 'clipboard', { configurable: true, value: undefined });
    });
    await click('#copy-all-keys');
    assert.equal(await page.evaluate(() => window.readClipboard()), batchKeys.join('\r\n'));
    await page.evaluate(() => { document.execCommand = () => false; });
    await click('#copy-all-keys');
    assert.equal(await page.locator('#copy-fallback').inputValue(), batchKeys.join('\n'));
    assert.equal(await page.locator('#copy-fallback').evaluate(node => node.value.slice(node.selectionStart, node.selectionEnd)), batchKeys.join('\n'));
    await page.evaluate(() => { document.execCommand = window.execCopy; });
    const downloadPromise = page.waitForEvent('download'); await click('#export-keys');
    const download = await downloadPromise;
    assert.match(download.suggestedFilename(), /\.txt$/);
    const stream = await download.createReadStream(), chunks = [];
    for await (const chunk of stream) chunks.push(chunk);
    assert.equal(Buffer.concat(chunks).toString('utf8').replace(/^\uFEFF/, ''), batchKeys.join('\r\n'));
    await action(1, '显示密钥'); assert.equal(await row(1).locator('input[readonly]').inputValue(), f.keys.get(1));
    await action(1, '复制密钥'); assert.equal(await page.evaluate(() => window.readClipboard()), f.keys.get(1));
    await action(1, '隐藏密钥'); assert.equal(await row(1).locator('input[readonly]').count(), 0);

    await action(1, '延长'); await page.locator('#dialog-preset').selectOption('day');
    assert.match(await page.locator('#dialog-preview').innerText(), /25小时时长/);
    await screenshot('extend');
    const beforeCancel = f.writes.length;
    await click('#dialog-cancel'); assert.equal(f.writes.length, beforeCancel);
    await action(1, '延长'); await page.locator('#dialog-preset').selectOption('day'); await click('#dialog-submit');
    assert.equal(f.licenses[0].durationSeconds, 90000); assert.match(await row(1).innerText(), /25小时时长/);
    await action(2, '延长'); await page.locator('#dialog-preset').selectOption('hour');
    assert.match(await page.locator('#dialog-preview').innerText(), /当前时间/); await click('#dialog-cancel');
    await action(4, '延长'); await page.locator('#dialog-preset').selectOption('day');
    assert.match(await page.locator('#dialog-preview').innerText(), /原到期时间/); await click('#dialog-cancel');
    await action(3, '延长'); assert.match(await page.locator('#dialog-preview').innerText(), /永久.*不变/); await click('#dialog-cancel');
    await action(5, '延长'); assert.match(await page.locator('#dialog-preview').innerText(), /永久.*不变/); await click('#dialog-cancel');
    await action(1, '延长'); await page.locator('#dialog-preset').selectOption('permanent');
    assert.match(await page.locator('#dialog-preview').innerText(), /永久/); await click('#dialog-cancel');

    await action(1, '换绑');
    assert.equal(await page.locator('#dialog-device').evaluate(node => node.tagName), 'SELECT');
    await page.locator('#dialog-device').selectOption('device-known');
    assert.match(await page.locator('#dialog-preview').innerText(), /离线/);
    await screenshot('rebind'); await click('#dialog-submit');
    assert.equal(f.licenses[0].boundDeviceId, 'device-known'); assert.match(await row(1).innerText(), /激活主播.*已知设备/);
    await action(1, '换绑'); await page.locator('#dialog-device').selectOption(''); await click('#dialog-cancel');
    assert.equal(f.licenses[0].boundDeviceId, 'device-known');
    await action(1, '换绑'); await page.locator('#dialog-device').selectOption(''); await click('#dialog-submit');
    assert.equal(f.writes.at(-1).body.deviceId, null); assert.equal(f.licenses[0].boundDeviceId, null);
    await action(1, '禁用'); await page.keyboard.press('Escape'); assert.equal(f.licenses[0].disabledAt, null);
    await action(1, '禁用'); await click('#dialog-submit'); assert.match(await row(1).innerText(), /已禁用/);
    await action(1, '启用'); await click('#dialog-submit'); assert.match(await row(1).innerText(), /未激活/);

    await action(2, '补录密钥'); await page.locator('#dialog-key').fill('wrong'); await click('#dialog-submit');
    assert.match(await page.locator('#dialog-error').innerText(), /不匹配/); assert.equal(await page.locator('#license-dialog').isVisible(), true);
    await page.locator('#dialog-key').fill(f.keys.get(2)); await click('#dialog-submit');
    assert.equal(await row(2).getByRole('button', { name: '补录密钥', exact: true }).count(), 0);
    await action(2, '显示密钥'); assert.equal(await row(2).locator('input[readonly]').inputValue(), f.keys.get(2));
    await action(3, '记录'); assert.match(await page.locator('#audit-list').innerText(), /<img/);
    assert.equal(await page.locator('#audit-list img').count(), 0); await screenshot('audit'); await click('#dialog-cancel');

    let extendAttempt = 0;
    await page.route('**/admin/api/licenses/1/extend', async route => {
      extendAttempt++;
      if (extendAttempt === 1) { await route.fetch(); await route.abort('failed'); }
      else if (extendAttempt === 2) await route.fulfill({ status: 401, body: '' });
      else if (extendAttempt === 3) await route.fulfill({ status: 403, contentType: 'application/json', body: JSON.stringify({ ok: false, code: 'invalid_csrf' }) });
      else await route.continue();
    });
    await action(1, '延长'); await click('#dialog-submit');
    assert.equal(await page.locator('#dialog-preset').isDisabled(), true);
    await click('#dialog-retry');
    assert.equal(await page.locator('#dialog-retry').isVisible(), true, 'auth failure cannot discard a previously uncertain write');
    await click('#dialog-retry');
    assert.equal(await page.locator('#dialog-retry').isVisible(), true, 'CSRF failure cannot discard a previously uncertain write');
    await click('#dialog-retry');
    assert.deepEqual(f.writes.at(-1), f.writes.at(-2), 'modal retries preserve body and revision');
    await page.unroute('**/admin/api/licenses/1/extend');
    await action(1, '禁用');
    const beforeRefreshFailure = f.writes.length;
    await page.route('**/admin/api/licenses', route => route.fulfill({ status: 503, contentType: 'application/json', body: JSON.stringify({ ok: false, code: 'fixture_failure' }) }));
    await click('#dialog-submit');
    assert.equal(f.writes.length, beforeRefreshFailure + 1);
    assert.match(await row(1).innerText(), /已禁用/);
    assert.equal(await page.locator('#pending-notice').isVisible(), false, 'failed GET after success never retries the write');
    assert.match(await page.locator('#message').innerText(), /密钥已更新.*列表刷新失败/);
    await page.unroute('**/admin/api/licenses'); await click('#refresh-licenses');

    await page.clock.install();
    await page.evaluate(() => {
      const originalFetch = window.fetch.bind(window);
      let stalled = false;
      window.fetch = (url, options) => {
        if (!stalled && url.endsWith('/1/disable')) {
          stalled = true; window.timedBody = options.body;
          return Promise.resolve({ ok: true, status: 200, json: () => new Promise((_resolve, reject) => {
            options.signal?.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')), { once: true });
          }) });
        }
        return originalFetch(url, options);
      };
    });
    await action(1, '启用'); await page.locator('#dialog-submit').click();
    await page.clock.fastForward(30001); await idle();
    assert.match(await page.locator('#dialog-error').innerText(), /超时/);
    assert.equal(await page.locator('#dialog-retry').isVisible(), true);
    await click('#dialog-retry');
    assert.deepEqual(f.writes.at(-1).body, await page.evaluate(() => JSON.parse(window.timedBody)), 'body-read timeout retains the request');
    await page.clock.resume();

    await page.locator('#license-quantity').fill('1'); await page.locator('#license-key').fill(generateLicenseKey());
    await click('#create-license'); assert.equal(f.writes.at(-1).body.count, 1); assert(f.writes.at(-1).body.key);
    for (const value of ['0', '201', '1.5']) {
      await page.locator('#license-quantity').fill(value); const count = f.writes.length;
      await click('#create-license'); assert.equal(f.writes.length, count);
    }
    await page.locator('#license-quantity').fill('200'); await click('#create-license');
    assert.equal(f.writes.at(-1).body.count, 200);
    assert.equal(await page.locator('#issued-keys input[readonly]').count(), 200);
    await page.locator('#license-search').fill('absent'); assert.equal(await page.locator('#license-empty').isVisible(), true);
    await page.locator('#license-search').fill('');

    for (const [status, code, message] of [[403, 'invalid_csrf', /安全令牌/], [401, '', /登录/], [503, 'vault_unavailable', /密钥库/]]) {
      await page.route('**/admin/api/licenses', route => route.fulfill({ status, contentType: 'application/json', body: JSON.stringify({ ok: false, code }) }));
      await click('#refresh-licenses'); assert.match(await page.locator('#message').innerText(), message);
      await page.unroute('**/admin/api/licenses');
    }
    await click('#refresh-licenses');
    assert.deepEqual(errors, []);
    console.log('PASS: contract browser desktop/mobile, presets, batch replay, key copy/export/HTTP fallback, confirmations, preview semantics, recovery, audit escaping, rerender and errors');
    await context.close();
    await realBrowser(browser, output);
    console.log('Screenshots: ' + output);
  } finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
