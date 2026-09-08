const assert = require('node:assert/strict');
const { mkdirSync } = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { chromium } = require('playwright');

(async () => {
  const load = file => import(pathToFileURL(path.resolve(__dirname, '../cloud-match-server/dist/' + file + '.js')).href);
  const { openDatabase } = await load('db');
  const { createCloudMatchAdminApp } = await load('admin');
  const { mutatePublicLibrary } = await load('library-admin-data');
  const { listPlayerLibrary } = await load('library-store');
  const db = openDatabase(':memory:');
  const entity = (entityId, names, gameIds = []) => ({ entityId, names, gameIds });
  const first = entity('zhuang', ['Zhuang', 'Lao'], ['one']);
  const second = entity('da', ['Da'], ['two']);
  mutatePublicLibrary(db, 0, 100, 'create', [first]);
  mutatePublicLibrary(db, 1, 100, 'create', [second]);
  mutatePublicLibrary(db, 2, 100, 'create', [entity('chou', ['Chou'], ['c-one'])]);
  mutatePublicLibrary(db, 3, 100, 'create', [entity('shuai', ['Shuai'], ['c-two'])]);
  const original = JSON.stringify({ entities: [entity('zhuang', ['Zhuang', 'Da', '<script>bad()</script>'], ['one', 'two', 'new']),
    entity('chou', ['Chou', 'Shuai'], ['c-one', 'c-two', 'c-new']), entity('safe', ['Safe'])] });
  db.prepare("INSERT INTO player_library_submissions(device_id,payload_json,status,created_at) VALUES('fixture',?,'pending',100)").run(original);
  const app = createCloudMatchAdminApp({ db, now: () => 101, csrfToken: 'test-csrf', adminPassword: 'fixture-only',
    socketController: { getActiveDeviceIds: () => new Set(), disconnectDevice: () => false,
      stopRealtimeViewer: () => false, notifyDirectoryChanged: () => {} } });
  const server = await new Promise(resolve => { const server = app.listen(0, '127.0.0.1', () => resolve(server)); });
  let browser;
  try {
    browser = await chromium.launch({ headless: true, channel: 'msedge' });
    const page = await browser.newPage({ viewport: { width: 1280, height: 800 }, httpCredentials: { username: 'admin', password: 'fixture-only' } });
    const errors = []; page.on('pageerror', error => errors.push(error.message));
    let accept = false; const confirmations = [];
    page.on('dialog', dialog => { confirmations.push(dialog.message()); return accept ? dialog.accept() : dialog.dismiss(); });
    const idle = () => page.waitForFunction(() => !document.body.hasAttribute('aria-busy'));
    const click = async selector => { await page.locator(selector).click(); await idle(); };
    await page.goto('http://127.0.0.1:' + server.address().port + '/admin/library'); await idle();
    await click('[data-submission-id="1"]');
    assert.equal(await page.locator('#btn-approve').isDisabled(), false, 'mixed conflicts allow explicit partial approval');
    await click('#btn-approve');
    assert.match(confirmations.at(-1), /整位选手跳过.*待审核/);
    assert.equal(listPlayerLibrary(db).revision, 4, 'cancel does not publish');
    accept = true; await click('#btn-approve');
    assert.match(await page.locator('#message').innerText(), /接受 1.*跳过 2.*待审核 1/);
    assert.equal(listPlayerLibrary(db).revision, 5);
    assert.equal(await page.locator('.review-entity').count(), 2);
    assert.equal(await page.locator('#detail-view script').count(), 0);
    await click('#btn-approve');
    assert.match(await page.locator('#message').innerText(), /接受 0.*跳过 2/);
    assert.equal(listPlayerLibrary(db).revision, 5);
    assert.equal(await page.locator('.conflict-merge').count(), 2, 'the two unrelated pairs need separate merge actions');
    await click('[data-merge-ids="chou,shuai"]');
    assert.deepEqual(await page.locator('#merge-options input:checked').evaluateAll(inputs => inputs.map(input => input.value).sort()), ['chou', 'shuai']);
    assert.doesNotMatch(await page.locator('#merge-preview').innerText(), /Zhuang|Da/);
    await click('#btn-merge-cancel');
    await click('[data-merge-ids="da,zhuang"]');
    assert.deepEqual(await page.locator('#merge-options input:checked').evaluateAll(inputs => inputs.map(input => input.value).sort()), ['da', 'zhuang']);
    assert.equal(await page.locator('#merge-dialog').isVisible(), true);
    await page.selectOption('#merge-target', 'zhuang');
    assert.match(await page.locator('#merge-preview').innerText(), /Zhuang.*Lao|Lao.*Zhuang/);
    assert.match(await page.locator('#merge-preview').innerText(), /one.*two/);
    assert.doesNotMatch(await page.locator('#merge-preview').innerText(), /冒险团/);
    const output = path.resolve(__dirname, '../build/cloud-library-review'); mkdirSync(output, { recursive: true });
    for (const [width, height] of [[1280, 800], [900, 800], [390, 844]]) {
      await page.setViewportSize({ width, height });
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), true);
      assert.equal(await page.locator('#merge-dialog').evaluate(el => el.scrollWidth <= el.clientWidth + 1), true);
      await page.screenshot({ path: path.join(output, 'partial-merge-' + width + '.png'), fullPage: true });
    }
    await page.setViewportSize({ width: 1280, height: 800 });
    accept = false; await click('#btn-merge-confirm');
    assert.equal(listPlayerLibrary(db).revision, 5);
    await click('#btn-merge-cancel');
    await page.selectOption('#public-main-list', 'zhuang');
    await click('#btn-public-merge');
    await page.getByLabel('合并 Da [da]', { exact: true }).check();
    assert.match(await page.locator('#merge-preview').innerText(), /one.*two/);
    // A stale preview cannot publish even after explicit confirmation.
    mutatePublicLibrary(db, 5, 102, 'create', [entity('later', ['Later'])]);
    accept = true; await click('#btn-merge-confirm');
    assert.match(await page.locator('#message').innerText(), /公共库版本已变化/);
    assert.equal(await page.locator('#merge-error').isVisible(), true, 'stale feedback must be visible above the modal backdrop');
    assert.equal(listPlayerLibrary(db).entityRedirects.length, 0);
    await click('#btn-merge-cancel'); await click('#btn-refresh');
    await click('[data-merge-ids="da,zhuang"]'); await page.selectOption('#merge-target', 'zhuang');
    await click('#btn-merge-confirm');
    assert.deepEqual(listPlayerLibrary(db).entityRedirects, [{ fromEntityId: 'da', toEntityId: 'zhuang' }]);
    await click('#btn-approve');
    assert.match(await page.locator('#message').innerText(), /接受 1.*跳过 1.*待审核 1/);
    assert.equal(listPlayerLibrary(db).entities.some(item => item.entityId === 'chou'), true, 'merging one pair must not absorb the other pair');
    await click('[data-merge-ids="chou,shuai"]'); await page.selectOption('#merge-target', 'shuai');
    await click('#btn-merge-confirm'); await click('#btn-approve');
    assert.match(await page.locator('#message').innerText(), /接受 1.*跳过 0.*待审核 0/);
    assert.equal(listPlayerLibrary(db).entities.find(item => item.entityId === 'zhuang').gameIds.includes('new'), true);
    assert.equal(db.prepare('SELECT payload_json FROM player_library_submission_originals').get().payload_json, original);
    const rev = () => listPlayerLibrary(db).revision;
    mutatePublicLibrary(db, rev(), 103, 'create', [entity('xia', ['Xia'], ['xia-only'])]);
    mutatePublicLibrary(db, rev(), 103, 'create', [entity('chong', ['Chong'], ['shared-element'])]);
    const sharedSubmission = Number(db.prepare("INSERT INTO player_library_submissions(device_id,payload_json,status,created_at) VALUES('fixture',?,'pending',104)")
      .run(JSON.stringify({ entities: [entity('xia', ['Xia'], ['xia-only', 'shared-element'])] })).lastInsertRowid);
    await click('#btn-refresh'); await click('[data-submission-id="' + sharedSubmission + '"]');
    assert.equal(await page.locator('.conflict-row').count(), 0, 'a shared ID is not an ownership error');
    assert.equal(await page.locator('.conflict-merge').count(), 0, 'one shared ID must not recommend merging different players');
    await click('#btn-approve');
    assert.match(await page.locator('#message').innerText(), /接受 1.*跳过 0/);
    assert.equal(listPlayerLibrary(db).entities.filter(row => row.gameIds.includes('shared-element')).length, 2);
    assert.equal(listPlayerLibrary(db).entityRedirects.some(row => ['xia', 'chong'].includes(row.fromEntityId)), false);
    await page.screenshot({ path: path.join(output, 'shared-identifiers-approved.png'), fullPage: true });
    const common = ['auto-1', 'auto-2', 'auto-3', 'auto-4'];
    mutatePublicLibrary(db, rev(), 105, 'create', [entity('auto-a', ['Automatic A'], [...common, 'a-only'])]);
    mutatePublicLibrary(db, rev(), 105, 'create', [entity('auto-b', ['Automatic B'], [...common, 'b-only'])]);
    const automaticSubmission = Number(db.prepare("INSERT INTO player_library_submissions(device_id,payload_json,status,created_at) VALUES('fixture',?,'pending',106)")
      .run(JSON.stringify({ entities: [entity('auto-a', ['Automatic A'], [...common, 'a-only', 'b-only'])] })).lastInsertRowid);
    await click('#btn-refresh'); await click('[data-submission-id="' + automaticSubmission + '"]');
    assert.match(await page.locator('.automatic-group-list').innerText(), /自动归并.*1 组/);
    assert.match(await page.locator('.automatic-group-list').innerText(), /Automatic A.*Automatic B/);
    assert.equal(listPlayerLibrary(db).entities.filter(row => row.entityId.startsWith('auto-')).length, 2, 'preview cannot merge');
    await click('#btn-approve');
    assert.equal(listPlayerLibrary(db).entities.filter(row => row.entityId.startsWith('auto-')).length, 1, 'five shared games qualify on approval');
    assert.equal(listPlayerLibrary(db).entityRedirects.some(row => row.fromEntityId === 'auto-b' && row.toEntityId === 'auto-a'), true);
    assert.deepEqual(errors, []);
    console.log('PASS: real isolated admin partial review, two independent conflict pairs, public/conflict merge union preview, cancel/all-conflict/retry, stale guard, redirects, original audit, 3 Edge viewports');
  } finally {
    await browser?.close();
    await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
    db.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
