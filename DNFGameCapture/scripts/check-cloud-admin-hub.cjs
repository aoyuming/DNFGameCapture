const assert = require('node:assert/strict');
const { mkdtempSync, mkdirSync, rmSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { chromium } = require('playwright');
const load = name => import(pathToFileURL(path.resolve(__dirname, '../cloud-match-server/dist/' + name + '.js')).href);

(async () => {
  const { openDatabase } = await load('db');
  const { createCloudMatchAdminApp } = await load('admin');
  const { createBroadcasterAttributionService } = await load('broadcaster-attribution');
  const licenseStore = await load('license-store');
  const { initializeSyncRelationSchema } = await load('sync-relations');
  const { registerDevice } = await load('identity');
  const { joinUnifiedPool } = await load('unified');
  const directory = mkdtempSync(path.join(tmpdir(), 'dnf-admin-hub-'));
  const db = openDatabase(path.join(directory, 'hub.sqlite')); initializeSyncRelationSchema(db);
  const now = Math.floor(Date.now() / 1000), active = new Set(['hub-online-device']);
  for (const [id, name] of [['hub-online-device', '在线主播'], ['hub-offline-device', '离线主播']]) {
    registerDevice(db, id, now); joinUnifiedPool(db, id, name, now);
  }
  const attribution = createBroadcasterAttributionService(db, { resolveRegion: () => '中国 · 浙江 · 杭州' });
  attribution.connectBroadcaster({ deviceId: 'hub-online-device', broadcasterName: '在线主播', ipAddress: '47.1.2.3', observedAt: now });
  const app = createCloudMatchAdminApp({ db, now: () => now, csrfToken: 'hub-csrf', adminPassword: 'hub-fixture-password',
    socketController: { getActiveDeviceIds: () => new Set(active), disconnectDevice: id => active.delete(id), stopRealtimeViewer: () => false, notifyDirectoryChanged: () => {} }, attribution });
  const server = await new Promise(resolve => { const server = app.listen(0, '127.0.0.1', () => resolve(server)); });
  const url = 'http://127.0.0.1:' + server.address().port;
  let browser;
  try {
    for (const route of ['/admin', '/admin/licenses', '/admin/broadcasters', '/admin/library']) assert.equal((await fetch(url + route)).status, 401);
    browser = await chromium.launch({ headless: true, channel: 'msedge' });
    const context = await browser.newContext({ httpCredentials: { username: 'admin', password: 'hub-fixture-password' }, viewport: { width: 1366, height: 900 } });
    const page = await context.newPage(), errors = [], requests = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('request', request => { if (request.url().includes('/admin/api/')) requests.push(new URL(request.url()).pathname); });
    page.on('dialog', dialog => dialog.accept(dialog.type() === 'prompt' ? '确认' : undefined));
    const idle = () => page.waitForFunction(() => !document.body.hasAttribute('aria-busy'));
    const click = async selector => { await page.locator(selector).click(); await idle(); };
    const output = path.resolve(__dirname, '../build/cloud-admin-hub'); mkdirSync(output, { recursive: true });
    async function screenshots(name) {
      for (const [width, height] of [[1366, 900], [900, 800], [390, 844]]) {
        await page.setViewportSize({ width, height });
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), true, name + ' horizontal overflow at ' + width);
        await page.screenshot({ path: path.join(output, name + '-' + width + '.png'), fullPage: true });
      }
      await page.setViewportSize({ width: 1366, height: 900 });
    }
    await page.goto(url + '/admin');
    assert.deepEqual(await page.locator('main a').allTextContents(), ['管理密钥', '管理主播', '管理共享库']);
    await page.waitForTimeout(3200); assert.deepEqual(requests, [], 'hub must not poll management data');
    await screenshots('home');
    await page.getByRole('link', { name: '管理密钥', exact: true }).click(); await idle();
    assert.equal(await page.title(), 'DNF 密钥管理');
    assert.equal(await page.locator('#broadcaster-list').count(), 0);
    assert(requests.every(route => route === '/admin/api/licenses'));
    await page.locator('#license-label').fill('浏览器密钥 <script>bad()</script>');
    await click('#create-license');
    const key = await page.locator('#created-license-key').inputValue(); assert.match(key, /^CDK-/);
    assert.equal(await page.locator('#license-list script').count(), 0);
    assert.equal(await page.locator('#license-list tr').count(), 1);
    await click('#refresh-licenses'); assert.equal(await page.locator('#created-license-key').inputValue(), key);
    await context.grantPermissions(['clipboard-read', 'clipboard-write']);
    await page.evaluate(() => {
      window.fixtureReadClipboard = navigator.clipboard.readText.bind(navigator.clipboard);
      window.fixtureExecCommand = document.execCommand.bind(document);
      Object.defineProperty(navigator.clipboard, 'writeText', { configurable: true, value: async () => { throw new Error('Clipboard unavailable'); } });
      document.execCommand = () => false;
    });
    await click('#copy-license-key');
    assert.equal(await page.locator('#created-license-key').isEnabled(), true);
    assert.deepEqual(await page.locator('#copy-fallback').evaluate(node => ({ focused: document.activeElement === node,
      selected: node.value.slice(node.selectionStart, node.selectionEnd), readonly: node.readOnly })),
    { focused: true, selected: key, readonly: true }, 'manual fallback keeps the entire readonly key focused and selected');
    assert.match(await page.locator('#message').innerText(), /密钥已选中/);
    await page.evaluate(() => { document.execCommand = window.fixtureExecCommand; });
    await click('#copy-license-key');
    assert.equal(await page.evaluate(() => window.fixtureReadClipboard()), key, 'legacy copy works when modern clipboard.writeText fails');
    assert.equal(await page.locator('#message').innerText(), '密钥已复制');
    await page.locator('#license-list').getByRole('button', { name: '禁用', exact: true }).click();
    assert.equal(await page.locator('#license-dialog').isVisible(), true);
    await click('#dialog-submit');
    assert.match(await page.locator('#license-list').innerText(), /已禁用/);
    await page.locator('#license-list').getByRole('button', { name: '启用', exact: true }).click();
    await click('#dialog-submit');
    assert.match(await page.locator('#license-list').innerText(), /未激活/);
    await page.locator('#license-search').fill('absent'); assert.equal(await page.locator('#license-list tr').count(), 0);
    await page.locator('#license-search').fill('');
    await page.route('**/admin/api/licenses', async route => { await new Promise(resolve => setTimeout(resolve, 200)); await route.fulfill({ status: 503, contentType: 'application/json', body: JSON.stringify({ ok: false, code: 'fixture_failure' }) }); });
    await page.locator('#refresh-licenses').click(); assert.equal(await page.locator('#create-license').isDisabled(), true); await idle();
    assert.match(await page.locator('#refresh-status').innerText(), /读取失败/);
    assert.match(await page.locator('#message').innerText(), /请求失败/);
    await page.unroute('**/admin/api/licenses'); await click('#refresh-licenses');
    await screenshots('licenses');
    const activated = licenseStore.activateStoredLicense(db, key, 'hub-license-machine', now, 3600).license;
    attribution.observeLicense({ licenseId: activated.id, licenseDeviceId: 'hub-license-machine', ipAddress: '47.1.2.3', observedAt: now });
    await page.getByRole('link', { name: '管理首页', exact: true }).click(); requests.length = 0;
    await page.getByRole('link', { name: '管理主播', exact: true }).click();
    await page.waitForFunction(() => document.getElementById('broadcaster-count').textContent === '2');
    assert(requests.every(route => route === '/admin/api/broadcasters/state' || route === '/admin/api/licenses'));
    assert.equal(await page.locator('#license-list').count(), 0);
    await page.locator('.broadcaster-row').filter({ hasText: '在线主播' }).click();
    await page.waitForFunction(expected => document.getElementById('license-key-value').value === expected, key);
    assert.equal(await page.locator('#current-ip').innerText(), '47.1.2.3');
    assert.equal(await page.locator('#ip-region').innerText(), '中国 · 浙江 · 杭州');
    assert.equal(await page.locator('#license-key-value').inputValue(), key);
    const bound = page.waitForResponse(response => response.request().method() === 'PUT' && response.url().endsWith('/admin/api/broadcasters/hub-online-device/license'));
    await page.locator('#bind-license').click(); await bound;
    assert.equal(db.prepare("SELECT source FROM broadcaster_license_links WHERE broadcaster_device_id='hub-online-device'").get().source, 'manual');
    assert.equal(await page.locator('#delete-button').isDisabled(), true);
    assert.equal(await page.locator('#ocr-device-id').inputValue(), 'hub-online-device');
    await page.locator('#ocr-until').fill(String(now + 600));
    await page.locator('#set-ocr-policy').click();
    await page.waitForResponse(response => response.url().endsWith('/admin/api/broadcasters/state'));
    assert.equal(db.prepare('SELECT ocr_disabled_until AS value FROM broadcaster_policies WHERE device_id=?').get('hub-online-device').value, now + 600);
    await page.locator('#clear-ocr-policy').click();
    await page.waitForResponse(response => response.url().endsWith('/admin/api/broadcasters/state'));
    assert.equal(db.prepare('SELECT ocr_disabled_until AS value FROM broadcaster_policies WHERE device_id=?').get('hub-online-device').value, null);
    await screenshots('broadcasters');
    await page.locator('#disconnect-button').click();
    await page.waitForFunction(() => document.getElementById('disconnect-button').disabled);
    assert.equal(active.size, 0);
    await page.locator('.broadcaster-row').filter({ hasText: '离线主播' }).click();
    await page.locator('#delete-button').click();
    await page.waitForFunction(() => document.getElementById('broadcaster-count').textContent === '1');
    await page.route('**/admin/api/broadcasters/state*', route => route.fulfill({ status: 503, contentType: 'application/json', body: JSON.stringify({ ok: false, code: 'fixture_failure' }) }));
    await page.locator('#refresh-broadcasters').click({ timeout: 3000 });
    await page.waitForFunction(() => document.getElementById('refresh-status').textContent === '读取失败');
    await page.unroute('**/admin/api/broadcasters/state*');
    await page.locator('#refresh-broadcasters').click();
    await page.waitForFunction(() => document.getElementById('refresh-status').textContent.startsWith('已更新'));
    await page.getByRole('link', { name: '管理首页', exact: true }).click();
    await page.getByRole('link', { name: '管理共享库', exact: true }).click(); await idle();
    assert.equal(await page.locator('.panel').count(), 3);
    await page.getByRole('link', { name: '管理首页', exact: true }).click();
    assert.deepEqual(errors, []);
    console.log('PASS: real Edge hub auth/navigation, no home polling, isolated data requests, key create/disable/enable/search, OCR set/clear, disconnect/delete, refresh errors, 9 responsive screenshots');
  } finally {
    if (browser) await browser.close(); await new Promise(resolve => server.close(resolve)); db.close();
    const resolved = path.resolve(directory);
    assert(resolved.startsWith(path.resolve(tmpdir()) + path.sep) && path.basename(resolved).startsWith('dnf-admin-hub-'));
    rmSync(resolved, { recursive: true, force: true });
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
