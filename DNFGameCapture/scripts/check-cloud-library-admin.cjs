const assert = require('node:assert/strict');
const { mkdtempSync, mkdirSync, rmSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');
const { pathToFileURL } = require('node:url');

const load = name => import(pathToFileURL(path.resolve(__dirname, '../cloud-match-server/dist/' + name + '.js')).href);
const entity = (entityId, names, gameIds = []) => ({ entityId, names, gameIds });
async function fixture() {
  const { openDatabase } = await load('db');
  const { createCloudMatchAdminApp } = await load('admin');
  const { initializeSyncRelationSchema } = await load('sync-relations');
  const { mutatePublicLibrary } = await load('library-admin-data');
  const directory = mkdtempSync(path.join(tmpdir(), 'dnf-library-admin-'));
  const db = openDatabase(path.join(directory, 'fixture.sqlite'));
  initializeSyncRelationSchema(db);
  const now = Math.floor(Date.now() / 1000);
  const entities = [entity('public-alpha', ['丁真', '抖音丁真'], ['江西2马搞#驱魔师', '抖音暗黑马区', '马区#驱魔师'])];
  for (let i = 1; i <= 32; i++) entities.push(entity('public-' + i, ['测试选手' + String(i).padStart(2, '0')], ['游戏角色' + i, '游戏别号' + i]));
  mutatePublicLibrary(db, 0, now, 'import', entities);
  for (const from of ['local-copy-a', 'local-copy-b', 'local-alias-a', 'local-alias-b']) {
    db.prepare('INSERT INTO player_entity_redirects VALUES(?,?)').run(from, 'public-alpha');
  }
  db.prepare('INSERT INTO player_entity_redirects VALUES(?,?)').run('local-unchanged-player', 'public-1');
  const pending = (items, device = 'dnf-test-broadcaster') => Number(db.prepare(`INSERT INTO player_library_submissions
    (device_id,payload_json,status,created_at) VALUES (?,?,'pending',?)`).run(device, JSON.stringify({ entities: items }), now).lastInsertRowid);
  const conflictId = pending([entity('local-alpha', ['新并列名', '丁真'], ['马区#驱魔师', '待审游戏ID'])]);
  const cleanId = pending([entity('clean-player', ['无冲突选手', '并列名称'], ['独立游戏ID'])]);
  const rejectId = pending([entity('reject-player', ['待驳回选手'], ['待驳回ID'])]);
  const alpha = entities[0];
  const unchangedId = pending([
    entity('local-copy-a', ['丁真'], alpha.gameIds),
    entity('local-copy-b', ['抖音丁真'], alpha.gameIds),
  ]);
  const reconciledId = pending([
    entity('local-alias-a', ['丁真'], alpha.gameIds),
    entity('local-alias-b', ['抖音丁真', '新关联别名'], alpha.gameIds),
    { ...entities[1], entityId: 'local-unchanged-player' },
  ]);
  for (let i = 1; i <= 18; i++) pending([entity('pending-' + i, ['投稿选手' + i], ['投稿角色' + i])], 'fixture-device-' + i);
  const app = createCloudMatchAdminApp({ db, now: () => now, csrfToken: 'library-preview-csrf', adminPassword: 'library-preview-only',
    socketController: { getActiveDeviceIds: () => new Set(), disconnectDevice: () => false, stopRealtimeViewer: () => false, notifyDirectoryChanged: () => {} } });
  const server = await new Promise((resolve, reject) => { const server = app.listen(0, '127.0.0.1', () => resolve(server)); server.once('error', reject); });
  const url = 'http://127.0.0.1:' + server.address().port;
  const close = async () => {
    await new Promise(resolve => server.close(resolve)); db.close();
    const resolved = path.resolve(directory), root = path.resolve(tmpdir()) + path.sep;
    assert(resolved.startsWith(root) && path.basename(resolved).startsWith('dnf-library-admin-'));
    rmSync(resolved, { recursive: true, force: true });
  };
  return { db, url, directory, close, conflictId, cleanId, rejectId, unchangedId, reconciledId, pending };
}

(async () => {
  const f = await fixture();
  if (process.argv.includes('--serve')) {
    console.log(JSON.stringify({ url: f.url + '/admin/library', username: 'admin', password: 'library-preview-only', database: f.directory, pid: process.pid }));
    const stop = () => f.close().then(() => process.exit());
    process.on('SIGINT', stop); process.on('SIGTERM', stop);
    return;
  }
  let browser;
  try {
    const { chromium } = require('playwright');
    browser = await chromium.launch({ headless: true, channel: 'msedge' });
    const context = await browser.newContext({ httpCredentials: { username: 'admin', password: 'library-preview-only' }, viewport: { width: 1600, height: 1000 } });
    const page = await context.newPage();
    const errors = []; page.on('pageerror', error => errors.push(error.message));
    let acceptDialog = true; page.on('dialog', dialog => acceptDialog ? dialog.accept() : dialog.dismiss());
    const idle = () => page.waitForFunction(() => !document.body.hasAttribute('aria-busy'));
    const click = async selector => { await page.locator(selector).click(); await idle(); };
    const refresh = () => click('#btn-refresh');
    const api = async (route, body, method = 'POST') => {
      const response = await fetch(f.url + '/admin/api/library' + route, { method: body ? method : 'GET',
        headers: { Authorization: 'Basic ' + Buffer.from('admin:library-preview-only').toString('base64'), 'Content-Type': 'application/json', 'x-dnf-admin-csrf': 'library-preview-csrf' },
        body: body ? JSON.stringify(body) : undefined });
      const data = await response.json(); assert(response.ok, JSON.stringify(data)); return data;
    };
    await page.goto(f.url + '/admin/library'); await idle();
    assert.equal(await page.title(), 'DNF 共享库管理台');
    assert.equal(await page.locator('#stat-pending').innerText(), '23');
    const raw = f.db.prepare('SELECT payload_json FROM player_library_submissions WHERE id=?').get(f.reconciledId).payload_json;
    const reconciled = (await api('/submissions/' + f.reconciledId)).submission;
    assert.equal(reconciled.rawEntityCount, 3); assert.equal(reconciled.entities.length, 2);
    assert.equal(reconciled.updatedEntityCount, 1); assert.equal(reconciled.unchangedCount, 1);
    assert.equal(reconciled.conflicts.length, 0);
    await click('[data-submission-id="' + f.reconciledId + '"]');
    assert.equal(await page.locator('[data-submission-id="' + f.reconciledId + '"] .pending-title').innerText(), '投稿 #' + f.reconciledId);
    assert.match(await page.locator('.submission-summary').innerText(), /原始 3.*归并后 2/);
    assert.match(await page.locator('.submission-summary').innerText(), /新增 0.*补充 1.*无变化 1/);
    assert.match(await page.locator('.entity-delta').first().innerText(), /新关联别名/);
    assert.doesNotMatch(await page.locator('.entity-delta').first().innerText(), /江西2马搞/);
    assert.equal(await page.locator('.unchanged-entities .review-entity').count(), 1);
    assert.equal(f.db.prepare('SELECT payload_json FROM player_library_submissions WHERE id=?').get(f.reconciledId).payload_json, raw);
    const reviewedNames = page.getByLabel('审核实体 1 并列名称 / 别名', { exact: true });
    await reviewedNames.fill((await reviewedNames.inputValue()) + '\n核对后别名');
    acceptDialog = false; await click('#btn-review-save');
    assert.equal(f.db.prepare('SELECT payload_json FROM player_library_submissions WHERE id=?').get(f.reconciledId).payload_json, raw);
    assert.match(await reviewedNames.inputValue(), /核对后别名/);
    const savedRequest = page.waitForRequest(request => request.method() === 'PUT' && request.url().endsWith('/submissions/' + f.reconciledId));
    acceptDialog = true; await click('#btn-review-save');
    assert.equal((await savedRequest).postDataJSON().confirmReconciliations, true);
    assert((await api('/submissions/' + f.reconciledId)).submission.entities.some(item => item.names.includes('核对后别名')));
    assert.equal((await api('/entities/public-alpha')).entity.names.includes('核对后别名'), false, 'draft confirmation must not publish');
    await click('[data-submission-id="' + f.unchangedId + '"]');
    assert.match(await page.locator('.submission-summary').innerText(), /新增 0.*补充 0.*无变化 1/);
    assert.equal(await page.locator('.review-entity').count(), 1, 'local aliases sharing public identifiers yield one review entity');
    await page.locator('#public-main-list').selectOption('public-alpha');
    await click('[data-submission-id="' + f.conflictId + '"]');
    assert.match(await page.locator('#detail-view').innerText(), /马区#驱魔师/);
    assert.match(await page.locator('#detail-view').innerText(), /public-alpha/);
    assert.equal(await page.locator('#btn-approve-removed').count(), 0);
    const output = path.resolve(__dirname, '../build/cloud-library-admin'); mkdirSync(output, { recursive: true });
    for (const [width, height] of [[2048, 990], [1366, 900], [900, 800], [390, 844]]) {
      await page.setViewportSize({ width, height });
      await page.screenshot({ path: path.join(output, 'library-' + width + '.png'), fullPage: true });
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), true, 'page must not overflow horizontally at ' + width);
      for (const selector of ['.pending-panel', '.detail-panel', '.public-panel']) {
        const bounds = await page.locator(selector).boundingBox();
        assert(bounds.x >= 0 && bounds.x + bounds.width <= width + 1, selector + ' outside viewport');
      }
      if (width >= 1366) {
        const boxes = await page.locator('.panel').evaluateAll(nodes => nodes.map(node => { const r = node.getBoundingClientRect(); return { x: r.x, y: r.y, right: r.right }; }));
        assert(boxes[0].right < boxes[1].x && boxes[1].right < boxes[2].x && boxes.every(box => box.y === boxes[0].y));
      }
    }
    await page.setViewportSize({ width: 1366, height: 900 });
    await page.locator('#pending-list').hover(); await page.mouse.wheel(0, 700);
    await page.waitForFunction(() => document.getElementById('pending-list').scrollTop > 0);
    await page.locator('#public-main-list').focus(); await page.keyboard.press('Home'); await page.keyboard.press('ArrowDown');
    assert.notEqual(await page.locator('#public-main-list').inputValue(), '');
    await page.locator('#pending-search').fill('新并列名');
    assert.equal(await page.locator('.pending-item').count(), 1);
    await page.locator('#pending-search').fill('');
    await page.locator('#conflict-filter').selectOption('conflict'); assert.equal(await page.locator('.pending-item').count(), 1);
    await page.locator('#conflict-filter').selectOption('all');
    await page.locator('#public-search').fill('江西2马搞'); assert.equal(await page.locator('#public-main-list option').count(), 1);
    await page.locator('#public-search').fill('');

    // Drafts and selections survive refresh, and an external write cannot silently rebase them.
    await page.locator('#public-main-list').selectOption('public-alpha');
    const originalNames = await page.locator('#public-names').inputValue();
    await page.locator('#public-names').fill(originalNames + '\n编辑中');
    await page.getByRole('checkbox', { name: '选择投稿 ' + f.conflictId, exact: true }).check();
    await page.getByLabel('审核实体 1 游戏ID', { exact: true }).fill('审核草稿游戏ID');
    await refresh();
    assert.match(await page.locator('#public-names').inputValue(), /编辑中/);
    assert.equal(await page.getByLabel('审核实体 1 游戏ID', { exact: true }).inputValue(), '审核草稿游戏ID');
    assert.equal(await page.locator('#public-main-list').inputValue(), 'public-alpha');
    assert.equal(await page.locator('#selected-count').innerText(), '1');
    const before = await api('/state');
    await api('/entities', { revision: before.revision, entity: entity('external', ['外部更新']) });
    await refresh(); await click('#btn-public-save');
    assert.match(await page.locator('#message').innerText(), /版本已变化/);
    assert.match(await page.locator('#public-names').inputValue(), /编辑中/);
    await click('#btn-public-reload'); assert.equal(await page.locator('#public-names').inputValue(), originalNames);
    await click('#btn-detail-reload');
    await page.getByLabel('关联公共实体 1', { exact: true }).selectOption('public-alpha');
    await click('#btn-review-save');
    assert.match(await page.locator('#detail-view').innerText(), /无名称归属冲突/);
    await click('#btn-approve');
    assert.match(await page.locator('#detail-meta').innerText(), /已通过/);
    assert((await api('/entities/public-alpha')).entity.names.includes('新并列名'));

    // Public create/save/delete plus cancellation and visible failure recovery.
    await click('#btn-public-new');
    await page.locator('#public-names').fill('浏览器新实体\n浏览器别名');
    await page.locator('#public-game-ids').fill('浏览器游戏ID');
    assert.equal(await page.locator('#public-adventure-ids').count(), 0);
    await click('#btn-public-add');
    const createdId = await page.locator('#public-main-list').inputValue(); assert(createdId);
    await page.locator('#public-game-ids').fill('修改后游戏ID'); await click('#btn-public-save');
    assert.deepEqual((await api('/entities/' + createdId)).entity.gameIds, ['修改后游戏ID']);
    acceptDialog = false; await click('#btn-public-delete'); assert.equal((await api('/entities/' + createdId)).entity.entityId, createdId);
    acceptDialog = true; await click('#btn-public-delete');
    assert.equal((await api('/state')).entities.some(entity => entity.entityId === createdId), false);
    await page.locator('#public-import-text').fill('浏览器导入=(导入角色一)(导入角色二)'); await click('#btn-public-import');
    assert.match(await page.locator('#import-result').innerText(), /已导入 1/);
    await page.locator('#public-import-text').fill(JSON.stringify({ entities: [entity('browser-v2', ['V2导入', 'V2并列名'], ['V2角色'])] }));
    await click('#btn-public-import'); assert.deepEqual((await api('/entities/browser-v2')).entity.gameIds, ['V2角色']);

    const clean = await api('/submissions/' + f.cleanId);
    await api('/submissions/' + f.cleanId, { revision: clean.revision, submissionRevision: clean.submission.submissionRevision,
      entities: [entity('clean-player', ['无冲突选手', '<img src=x onerror=alert(1)>'], ['独立游戏ID'])] }, 'PUT');
    await refresh(); await click('[data-submission-id="' + f.cleanId + '"]');
    assert.match(await page.getByLabel('审核实体 1 并列名称 / 别名', { exact: true }).inputValue(), /<img/);
    assert.equal(await page.locator('#detail-view img').count(), 0, 'submission text must not become HTML');
    await page.getByRole('checkbox', { name: '选择投稿 ' + f.cleanId, exact: true }).check();
    await page.getByRole('checkbox', { name: '选择投稿 ' + f.rejectId, exact: true }).check();
    await click('#btn-reject');
    assert.equal((await api('/submissions/' + f.cleanId)).submission.status, 'rejected');
    assert.equal((await api('/submissions/' + f.rejectId)).submission.status, 'rejected');

    await page.route('**/admin/api/library/state', async route => {
      await new Promise(resolve => setTimeout(resolve, 250));
      await route.fulfill({ status: 503, contentType: 'application/json', body: JSON.stringify({ ok: false, code: 'internal_error' }) });
    });
    await page.locator('#btn-refresh').click();
    assert.equal(await page.locator('#btn-refresh').isDisabled(), true);
    await idle(); assert.match(await page.locator('#message').innerText(), /服务器处理失败/);
    await page.unroute('**/admin/api/library/state'); await refresh();
    await click('#btn-approve-added');
    assert.deepEqual((await api('/state')).submissions.map(item => item.id), [f.unchangedId], 'additions batch leaves unchanged submissions pending');
    await click('[data-submission-id="' + f.unchangedId + '"]');
    await click('#btn-approve');
    assert.equal((await api('/state')).submissions.length, 0);
    await page.setViewportSize({ width: 390, height: 844 });
    await click('#btn-public-new');
    await page.locator('#public-names').fill('移动端实体'); await page.keyboard.press('Tab');
    assert.equal(await page.locator('#public-game-ids').evaluate(node => document.activeElement === node), true);
    await page.keyboard.type('mobile-id'); await click('#btn-public-add');
    assert((await api('/state')).entities.some(item => item.names.includes('移动端实体')));
    if (await page.locator('#public-editor').evaluate(node => node.scrollHeight > node.clientHeight)) {
      await page.locator('#public-editor').hover(); await page.mouse.wheel(0, 700);
      await page.waitForFunction(() => document.getElementById('public-editor').scrollTop > 0);
    }
    await page.setViewportSize({ width: 1366, height: 900 });
    // The workspace retains the viewed guard until explicitly reloaded.
    const staleId = f.pending([entity('review-viewed', ['Viewed payload A'], ['viewed-game'])]);
    await refresh(); await click('[data-submission-id="' + staleId + '"]');
    const viewed = await api('/submissions/' + staleId);
    await api('/submissions/' + staleId, { revision: viewed.revision, submissionRevision: viewed.submission.submissionRevision,
      entities: [entity('review-unseen', ['Unseen payload B'], ['unseen-game'])] }, 'PUT');
    await click('#btn-approve');
    assert.match(await page.locator('#message').innerText(), /投稿已被修改/);
    assert.equal((await api('/submissions/' + staleId)).submission.status, 'pending');
    assert.equal((await api('/state')).entities.some(item => item.entityId === 'review-unseen'), false);
    assert.equal(await page.getByLabel('审核实体 1 并列名称 / 别名', { exact: true }).inputValue(), 'Viewed payload A');
    await click('#btn-detail-reload');
    assert.equal(await page.getByLabel('审核实体 1 并列名称 / 别名', { exact: true }).inputValue(), 'Unseen payload B');
    await click('#btn-reject');
    assert.equal((await api('/submissions/' + staleId)).submission.status, 'rejected');
    const nameConflictId = f.pending([entity('name-conflict', ['丁真'], ['unrelated-game'])]);
    await refresh(); await click('[data-submission-id="' + nameConflictId + '"]');
    assert.match(await page.locator('.conflict-row').innerText(), /名称/);
    assert.equal(await page.locator('#btn-approve').isDisabled(), false, 'name conflicts can be explicitly skipped in partial review');
    await click('#btn-reject');
    assert.equal((await api('/submissions/' + nameConflictId)).submission.status, 'rejected');
    await page.screenshot({ path: path.join(output, 'library-verified.png'), fullPage: true });
    assert.deepEqual(errors, []);
    console.log('PASS: real Edge + SQLite CRUD/import/review, explicit conflict resolution, revision races, draft/selection refresh, auth, errors, keyboard/scroll, 4 responsive screenshots');
    console.log('Screenshots: ' + output);
  } finally { if (browser) await browser.close(); await f.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
