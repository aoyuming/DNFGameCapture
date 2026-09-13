const assert = require('node:assert/strict');
const { mkdirSync, mkdtempSync, rmSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');
const { pathToFileURL } = require('node:url');

const load = name => import(pathToFileURL(path.resolve(__dirname, '../cloud-match-server/dist/' + name + '.js')).href);
const entity = (entityId, names, gameIds = []) => ({ entityId, names, gameIds });

function removeFixtureDirectory(directory) {
  const resolved = path.resolve(directory);
  const temporaryRoot = path.resolve(tmpdir()) + path.sep;
  assert(resolved.startsWith(temporaryRoot), 'fixture cleanup must stay inside the temporary directory');
  assert(path.basename(resolved).startsWith('dnf-library-conflict-batch-'), 'fixture cleanup must use the expected prefix');
  rmSync(resolved, { recursive: true, force: true });
}

async function fixture() {
  const { createCloudMatchAdminApp } = await load('admin');
  const { openDatabase } = await load('db');
  const { mutatePublicLibrary } = await load('library-admin-data');
  const { listPlayerLibrary } = await load('library-store');
  const { initializeSyncRelationSchema } = await load('sync-relations');
  const directory = mkdtempSync(path.join(tmpdir(), 'dnf-library-conflict-batch-'));
  let db;
  let server;

  try {
    db = openDatabase(path.join(directory, 'fixture.sqlite'));
    initializeSyncRelationSchema(db);
    const now = 1789185600;
    mutatePublicLibrary(db, 0, now, 'import', [
      entity('public-name-a', ['同名甲', '甲公开名', '<img src=x onerror=alert(1)>'], ['甲公开游戏ID']),
      entity('public-name-b', ['同名乙', '乙公开名'], ['乙公开游戏ID']),
      entity('public-merge-left', ['公共冲突甲'], ['公共甲游戏ID']),
      entity('public-merge-right', ['公共冲突乙'], ['公共乙游戏ID']),
    ]);

    const insertPending = (deviceId, entities) => Number(db.prepare(`INSERT INTO player_library_submissions
      (device_id,payload_json,status,created_at) VALUES (?,?,'pending',?)`)
      .run(deviceId, JSON.stringify({ entities }), now).lastInsertRowid);
    const uniqueAId = insertPending('name-device-a', [
      entity('local-name-a', ['同名甲', '甲投稿别名', '<img src=x onerror=alert(1)>'], ['甲投稿游戏ID']),
    ]);
    const uniqueA2Id = insertPending('name-device-a-2', [
      entity('local-name-a-2', ['同名甲', '甲第二投稿别名'], ['甲第二投稿游戏ID']),
    ]);
    const uniqueBId = insertPending('name-device-b', [
      entity('local-name-b', ['同名乙', '乙投稿别名'], ['乙投稿游戏ID']),
    ]);
    const publicMergeId = insertPending('public-merge-device', [
      entity('public-merge-left', ['公共冲突甲', '公共冲突乙', '公共桥接别名'], ['公共桥接游戏ID']),
    ]);
    const ambiguousAId = insertPending('ambiguous-device-a', [
      entity('local-ambiguous-a', ['无归属同名', '待人工甲'], ['待人工游戏ID甲']),
    ]);
    const ambiguousBId = insertPending('ambiguous-device-b', [
      entity('local-ambiguous-b', ['无归属同名', '待人工乙'], ['待人工游戏ID乙']),
    ]);

    const app = createCloudMatchAdminApp({
      db,
      now: () => now + 60,
      csrfToken: 'conflict-batch-csrf',
      adminPassword: 'conflict-batch-only',
      socketController: {
        getActiveDeviceIds: () => new Set(),
        disconnectDevice: () => false,
        stopRealtimeViewer: () => false,
        notifyDirectoryChanged: () => {},
      },
    });
    server = await new Promise((resolve, reject) => {
      const listening = app.listen(0, '127.0.0.1', () => resolve(listening));
      listening.once('error', reject);
    });
    const url = 'http://127.0.0.1:' + server.address().port;
    let closed = false;
    const close = async () => {
      if (closed) return;
      closed = true;
      try {
        server.closeAllConnections();
        await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
      } finally {
        try { db.close(); }
        finally { removeFixtureDirectory(directory); }
      }
    };
    const mutationSnapshot = () => ({
      library: listPlayerLibrary(db),
      submissions: db.prepare(`SELECT id,device_id,payload_json,status,created_at,reviewed_at,review_reason
        FROM player_library_submissions ORDER BY id`).all(),
      originals: db.prepare('SELECT * FROM player_library_submission_originals ORDER BY submission_id').all(),
      audits: db.prepare('SELECT * FROM player_library_conflict_resolution_audit ORDER BY id').all(),
    });
    return {
      db,
      url,
      close,
      mutationSnapshot,
      uniqueAId,
      uniqueA2Id,
      uniqueBId,
      publicMergeId,
      ambiguousAId,
      ambiguousBId,
    };
  } catch (error) {
    try {
      if (server) {
        server.closeAllConnections();
        await new Promise(resolve => server.close(resolve));
      }
    } finally {
      try { db?.close(); }
      finally { removeFixtureDirectory(directory); }
    }
    throw error;
  }
}

function assertExactValues(actual, expected, label) {
  assert.equal(actual.length, expected.length, label + ' count');
  assert.deepEqual([...actual].sort(), [...expected].sort(), label);
}

(async () => {
  let browser;
  let context;
  let f;
  try {
    f = await fixture();
    const { chromium } = require('playwright');
    browser = await chromium.launch({ headless: true, channel: 'msedge' });
    context = await browser.newContext({
      httpCredentials: { username: 'admin', password: 'conflict-batch-only' },
      viewport: { width: 1366, height: 900 },
      locale: 'zh-CN',
      timezoneId: 'America/New_York',
    });
    const page = await context.newPage();
    page.setDefaultTimeout(5000);
    await page.clock.install({ time: new Date('2026-09-12T16:00:00Z') });
    await page.clock.pauseAt(new Date('2026-09-12T16:00:01Z'));
    await page.route('**/favicon.ico', route => route.fulfill({ status: 204, body: '' }));
    await page.addInitScript(() => {
      window.__batchApiPending = 0;
      const originalFetch = window.fetch;
      window.fetch = async (...args) => {
        ++window.__batchApiPending;
        try { return await originalFetch(...args); }
        finally { --window.__batchApiPending; }
      };
    });

    const pageErrors = [];
    const consoleErrors = [];
    const resolveRequests = [];
    const stateRequests = [];
    const nativeConfirmations = [];
    let nextDialogAction = null;
    page.on('pageerror', error => pageErrors.push(error.message));
    page.on('console', message => { if (message.type() === 'error') consoleErrors.push(message.text()); });
    page.on('request', request => {
      if (request.url().endsWith('/admin/api/library/conflicts/resolve') && request.method() === 'POST') resolveRequests.push(request);
      if (request.url().endsWith('/admin/api/library/state')) stateRequests.push(request);
    });
    page.on('dialog', async dialog => {
      nativeConfirmations.push(dialog.message());
      const action = nextDialogAction;
      nextDialogAction = null;
      if (action === 'accept') await dialog.accept();
      else await dialog.dismiss();
      if (!action) pageErrors.push('Unexpected native dialog: ' + dialog.message());
    });

    const idle = () => page.waitForFunction(() => window.__batchApiPending === 0 && !document.body.hasAttribute('aria-busy'));
    const click = async selector => { await page.locator(selector).click(); await idle(); };
    const headers = {
      Authorization: 'Basic ' + Buffer.from('admin:conflict-batch-only').toString('base64'),
      'Content-Type': 'application/json',
      'x-dnf-admin-csrf': 'conflict-batch-csrf',
    };
    const api = async (route, body, method = 'POST') => {
      const response = await fetch(f.url + '/admin/api/library' + route, {
        method: body === undefined ? 'GET' : method,
        headers,
        body: body === undefined ? undefined : JSON.stringify(body),
      });
      const data = await response.json();
      assert(response.ok, JSON.stringify(data));
      return data;
    };

    await page.goto(f.url + '/admin/library');
    await idle();
    const initial = await api('/state');
    assert.deepEqual(initial.conflictResolutionStats, { uniqueNameTargets: 2, publicMerges: 1, ambiguous: 1 });
    assert.equal(initial.conflictResolutionGroups.length, 4);

    const batchAction = page.locator('#btn-resolve-by-name');
    assert.equal(await batchAction.count(), 1, '#btn-resolve-by-name must exist');
    assert.equal(await batchAction.isEnabled(), true, '#btn-resolve-by-name must be enabled');
    await click('#btn-resolve-by-name');
    const dialog = page.locator('#conflict-batch-dialog');
    assert.equal(await dialog.evaluate(element => element.open), true, 'the real conflict batch dialog must open');
    assert.equal(await page.getByRole('group', { name: '冲突组选择操作', exact: true }).count(), 1);
    assert.equal(await page.getByRole('list', { name: '冲突处理组', exact: true }).count(), 1);
    assert.equal(await dialog.getByRole('listitem').count(), 4);
    assert.match(await page.locator('#conflict-batch-summary').innerText(), /按名称可合并 2/);
    assert.match(await page.locator('#conflict-batch-summary').innerText(), /已跳过 2/);

    const uniqueRows = page.locator('.conflict-batch-row[data-kind="unique_name_target"]');
    const publicMergeRow = page.locator('.conflict-batch-row[data-kind="public_merge"]');
    const ambiguousRow = page.locator('.conflict-batch-row[data-kind="ambiguous"]');
    assert.equal(await uniqueRows.count(), 2);
    assert.equal(await uniqueRows.locator('input[type="checkbox"]:checked').count(), 2);
    assert.equal(await page.locator('#conflict-batch-list input[type="checkbox"]:checked').count(), 2);
    assert.equal(await publicMergeRow.count(), 1);
    assert.equal(await publicMergeRow.locator('input[type="checkbox"]').isChecked(), false);
    assert.equal(await publicMergeRow.locator('input[type="checkbox"]').isDisabled(), true);
    assert.equal(await ambiguousRow.count(), 1);
    assert.equal(await ambiguousRow.locator('input[type="checkbox"]').isDisabled(), true);
    assert.match(await ambiguousRow.innerText(), /无法自动处理|需要人工/);
    assert.match(await dialog.innerText(), /<img src=x onerror=alert\(1\)>/);
    assert.equal(await dialog.locator('img').count(), 0, 'untrusted conflict names must remain text');

    const publicTarget = publicMergeRow.locator('select');
    assert.deepEqual(await publicTarget.locator('option').evaluateAll(options => options.map(option => option.value)), [
      '',
      'public-merge-left',
      'public-merge-right',
    ]);
    await publicTarget.selectOption('public-merge-right');
    assert.equal(await publicMergeRow.locator('input[type="checkbox"]').isEnabled(), true);
    await publicMergeRow.locator('input[type="checkbox"]').check();
    assert.match(await page.locator('#conflict-batch-summary').innerText(), /当前选择 3 组/);
    await click('#btn-conflict-batch-clear');
    assert.equal(await page.locator('#conflict-batch-list input[type="checkbox"]:checked').count(), 0);
    assert.equal(await page.locator('#btn-conflict-batch-confirm').isDisabled(), true);
    await publicTarget.selectOption('');
    await click('#btn-conflict-batch-safe-all');
    assert.equal(await uniqueRows.locator('input[type="checkbox"]:checked').count(), 2);
    assert.equal(await publicMergeRow.locator('input[type="checkbox"]').isChecked(), false);
    assert.match(await page.locator('#conflict-batch-summary').innerText(), /当前选择 2 组/);
    assert.match(await page.locator('#conflict-batch-summary').innerText(), /待审投稿 3 条 \/ 实体 3 个/);
    assert.match(await page.locator('#conflict-batch-summary').innerText(), /合并公共实体 0 个/);
    assert.match(await page.locator('#conflict-batch-summary').innerText(), /重定向 3 条/);

    const output = path.resolve(__dirname, '../build/cloud-library-conflict-batch');
    mkdirSync(output, { recursive: true });
    const assertOpenDialogLayout = async (width, height) => {
      await page.setViewportSize({ width, height });
      await page.locator('#conflict-batch-list').evaluate(list => { list.scrollTop = 0; });
      await page.locator('#conflict-batch-dialog').evaluate(modal => { modal.scrollTop = 0; });
      const metrics = await page.evaluate(() => {
        const modal = document.getElementById('conflict-batch-dialog');
        const list = document.getElementById('conflict-batch-list');
        const bounds = modal.getBoundingClientRect();
        return {
          pageWidth: document.documentElement.scrollWidth,
          dialog: { left: bounds.left, top: bounds.top, right: bounds.right, bottom: bounds.bottom },
          dialogScrollWidth: modal.scrollWidth,
          dialogClientWidth: modal.clientWidth,
          listScrollWidth: list.scrollWidth,
          listClientWidth: list.clientWidth,
        };
      });
      assert(metrics.pageWidth <= width + 1, 'page must not overflow horizontally at ' + width);
      assert(metrics.dialog.left >= -1 && metrics.dialog.right <= width + 1, 'dialog must fit horizontally at ' + width);
      assert(metrics.dialog.top >= -1 && metrics.dialog.bottom <= height + 1, 'dialog must fit vertically at ' + width);
      assert(metrics.dialogScrollWidth <= metrics.dialogClientWidth + 1, 'dialog content must not overflow horizontally at ' + width);
      assert(metrics.listScrollWidth <= metrics.listClientWidth + 1, 'dialog list must not overflow horizontally at ' + width);
      for (const selector of [
        '#conflict-batch-heading',
        '#conflict-batch-summary',
        '#btn-conflict-batch-safe-all',
        '#btn-conflict-batch-clear',
        '#conflict-batch-list',
        '#btn-conflict-batch-cancel',
        '#btn-conflict-batch-confirm',
      ]) {
        assert.equal(await page.locator(selector).isVisible(), true, selector + ' must be visible at ' + width);
        const bounds = await page.locator(selector).boundingBox();
        assert(bounds && bounds.x >= -1 && bounds.x + bounds.width <= width + 1 && bounds.y >= -1 && bounds.y + bounds.height <= height + 1,
          selector + ' must fit inside the viewport at ' + width);
      }
      assert.equal(await page.locator('#btn-conflict-batch-safe-all').isEnabled(), true);
      assert.equal(await page.locator('#btn-conflict-batch-clear').isEnabled(), true);
      assert.equal(await page.locator('#btn-conflict-batch-cancel').isEnabled(), true);
      assert.equal(await page.locator('#btn-conflict-batch-confirm').isEnabled(), true);
      await page.screenshot({ path: path.join(output, 'dialog-' + width + 'x' + height + '.png') });
      for (const control of [uniqueRows.first().locator('input[type="checkbox"]'), publicTarget, ambiguousRow.locator('input[type="checkbox"]')]) {
        await control.scrollIntoViewIfNeeded();
        assert.equal(await control.isVisible(), true, 'each conflict control must be visible and reachable at ' + width);
        const bounds = await control.boundingBox();
        assert(bounds && bounds.x >= -1 && bounds.x + bounds.width <= width + 1 && bounds.y >= -1 && bounds.y + bounds.height <= height + 1,
          'each conflict control must be inside the viewport at ' + width);
      }
    };
    await assertOpenDialogLayout(1366, 900);
    await assertOpenDialogLayout(390, 844);
    await page.setViewportSize({ width: 1366, height: 900 });

    const stateCallsWhileOpen = stateRequests.length;
    await page.evaluate(() => window.dispatchEvent(new Event('focus')));
    await page.waitForTimeout(200);
    assert.equal(stateRequests.length, stateCallsWhileOpen, 'silent refresh must pause while the conflict dialog is open');
    const resumedRefresh = page.waitForRequest(request => request.url().endsWith('/admin/api/library/state'));
    await page.locator('#btn-conflict-batch-cancel').click();
    await resumedRefresh;
    await idle();
    assert.equal(await dialog.evaluate(element => element.open), false);
    await click('#btn-resolve-by-name');

    const beforeCancel = f.mutationSnapshot();
    const requestsBeforeCancel = resolveRequests.length;
    nextDialogAction = 'dismiss';
    await click('#btn-conflict-batch-confirm');
    assert.equal(resolveRequests.length, requestsBeforeCancel, 'dismissed confirmation must not POST');
    assert.deepEqual(f.mutationSnapshot(), beforeCancel, 'dismissed confirmation must not change library, submissions, redirects, or audit state');
    assert.equal(await dialog.evaluate(element => element.open), true, 'dismissed confirmation keeps selections in the dialog');
    assert.equal(await uniqueRows.locator('input[type="checkbox"]:checked').count(), 2);
    assert.match(nativeConfirmations.at(-1), /2 组/);
    assert.match(nativeConfirmations.at(-1), /待审投稿 3 条 \/ 实体 3 个/);
    assert.match(nativeConfirmations.at(-1), /合并公共实体 0 个/);
    assert.match(nativeConfirmations.at(-1), /重定向 3 条/);

    const expectedGroups = initial.conflictResolutionGroups
      .filter(group => group.kind === 'unique_name_target')
      .map(group => ({ token: group.token, targetEntityId: group.suggestedTargetEntityId }));
    const posted = page.waitForRequest(request => request.url().endsWith('/admin/api/library/conflicts/resolve') && request.method() === 'POST');
    nextDialogAction = 'accept';
    await click('#btn-conflict-batch-confirm');
    assert.deepEqual((await posted).postDataJSON(), { revision: initial.revision, groups: expectedGroups, confirm: true });
    assert.equal(await dialog.evaluate(element => element.open), false, 'successful resolution closes the dialog');

    const resolved = await api('/state');
    assert.equal(resolved.revision, initial.revision + 1);
    assert.equal(resolved.stats.pending, 3);
    assert.deepEqual(resolved.conflictResolutionStats, { uniqueNameTargets: 0, publicMerges: 1, ambiguous: 1 });
    assert.deepEqual(resolved.submissions.map(item => item.id), [f.publicMergeId, f.ambiguousAId, f.ambiguousBId]);
    const targetA = resolved.entities.find(item => item.entityId === 'public-name-a');
    const targetB = resolved.entities.find(item => item.entityId === 'public-name-b');
    assertExactValues(targetA.names, ['同名甲', '甲公开名', '甲投稿别名', '<img src=x onerror=alert(1)>', '甲第二投稿别名'], 'first target names');
    assertExactValues(targetA.gameIds, ['甲公开游戏ID', '甲投稿游戏ID', '甲第二投稿游戏ID'], 'first target game IDs');
    assertExactValues(targetB.names, ['同名乙', '乙公开名', '乙投稿别名'], 'second target names');
    assertExactValues(targetB.gameIds, ['乙公开游戏ID', '乙投稿游戏ID'], 'second target game IDs');
    assert.deepEqual(resolved.entityRedirects, [
      { fromEntityId: 'local-name-a', toEntityId: 'public-name-a' },
      { fromEntityId: 'local-name-a-2', toEntityId: 'public-name-a' },
      { fromEntityId: 'local-name-b', toEntityId: 'public-name-b' },
    ]);
    assert(resolved.entities.some(item => item.entityId === 'public-merge-left'));
    assert(resolved.entities.some(item => item.entityId === 'public-merge-right'));
    assert.equal(f.db.prepare('SELECT status FROM player_library_submissions WHERE id=?').get(f.uniqueAId).status, 'approved');
    assert.equal(f.db.prepare('SELECT status FROM player_library_submissions WHERE id=?').get(f.uniqueA2Id).status, 'approved');
    assert.equal(f.db.prepare('SELECT status FROM player_library_submissions WHERE id=?').get(f.uniqueBId).status, 'approved');
    assert.equal(await page.locator('#stat-pending').innerText(), '3');
    assert.match(await page.locator('#status-text').innerText(), new RegExp('公共库版本 ' + resolved.revision));
    assert.equal(await page.locator('#message').innerText(),
      '冲突合并完成：已处理 2 组冲突，已接纳 3 个待审实体，剩余待审核 3 条，公共库版本 ' + resolved.revision + '。');

    await click('#btn-resolve-by-name');
    const refreshedPublicMergeRow = page.locator('.conflict-batch-row[data-kind="public_merge"]');
    const refreshedPublicTarget = refreshedPublicMergeRow.locator('select');
    const refreshedPublicCheck = refreshedPublicMergeRow.locator('input[type="checkbox"]');
    await refreshedPublicTarget.selectOption('public-merge-right');
    await refreshedPublicCheck.check();
    const logicalErrorRoute = '**/admin/api/library/conflicts/resolve';
    await page.route(logicalErrorRoute, route => route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ ok: false, code: 'internal_error' }),
    }), { times: 1 });
    nextDialogAction = 'accept';
    await click('#btn-conflict-batch-confirm');
    assert.equal(await dialog.evaluate(element => element.open), true, 'an API error keeps the dialog open');
    assert.equal(await refreshedPublicCheck.isChecked(), true, 'an API error keeps the selected group');
    assert.equal(await refreshedPublicTarget.inputValue(), 'public-merge-right', 'an API error keeps the retained target');
    assert.match(await page.locator('#conflict-batch-error').innerText(), /服务器处理失败/);
    await page.unroute(logicalErrorRoute);

    const publicMergeGroup = resolved.conflictResolutionGroups.find(group => group.kind === 'public_merge');
    assert(publicMergeGroup, 'the public-merge group must remain available for the revision race');
    const raced = await api('/entities', {
      revision: resolved.revision,
      entity: entity('external-revision-race', ['外部版本竞态'], ['外部版本游戏ID']),
    });
    assert.equal(raced.revision, resolved.revision + 1, 'the external write must advance the public revision');
    const beforeStaleRevisionSubmit = f.mutationSnapshot();
    let staleRevisionUpstreamStatus;
    let staleRevisionUpstreamCode;
    await page.route(logicalErrorRoute, async route => {
      const upstream = await route.fetch();
      staleRevisionUpstreamStatus = upstream.status();
      staleRevisionUpstreamCode = (await upstream.json()).code;
      await route.fulfill({ response: upstream, status: 200 });
    }, { times: 1 });
    const staleRevisionRequestCount = resolveRequests.length;
    const staleRevisionStateRequestCount = stateRequests.length;
    nextDialogAction = 'accept';
    await click('#btn-conflict-batch-confirm');
    assert.equal(staleRevisionUpstreamStatus, 409, 'the real server must reject the stale public revision with HTTP 409');
    assert.equal(staleRevisionUpstreamCode, 'stale_revision');
    await page.unroute(logicalErrorRoute);
    assert.equal(resolveRequests.length, staleRevisionRequestCount + 1, 'a stale revision must produce one request only');
    assert.deepEqual(resolveRequests.at(-1).postDataJSON(), {
      revision: resolved.revision,
      groups: [{ token: publicMergeGroup.token, targetEntityId: 'public-merge-right' }],
      confirm: true,
    }, 'the rejected request must contain the dialog revision and explicit retained target');
    assert.deepEqual(f.mutationSnapshot(), beforeStaleRevisionSubmit,
      'the rejected stale decision must not change library, submissions, redirects, or audit state');
    assert.equal(await dialog.evaluate(element => element.open), true, 'a stale revision keeps the refreshed dialog open');
    assert.equal(await page.locator('#conflict-batch-list input[type="checkbox"]:checked').count(), 0,
      'a stale revision must clear every old selection for explicit re-checking');
    assert.equal(await refreshedPublicTarget.inputValue(), '', 'a stale revision must clear the old retained target');
    assert.equal(await page.locator('#btn-conflict-batch-confirm').isDisabled(), true);
    assert.equal(await page.locator('#conflict-batch-error').innerText(),
      '公共库版本已变化。草稿已保留，请重新载入后核对再提交。');
    assert(stateRequests.length > staleRevisionStateRequestCount, 'a stale revision must refresh authoritative state');
    assert.match(await page.locator('#status-text').innerText(), new RegExp('公共库版本 ' + raced.revision));
    const afterStaleRevision = await api('/state');
    assert.equal(afterStaleRevision.revision, raced.revision);
    assert(afterStaleRevision.entities.some(item => item.entityId === 'external-revision-race'));
    assert(afterStaleRevision.entities.some(item => item.entityId === 'public-merge-left'));
    assert(afterStaleRevision.entities.some(item => item.entityId === 'public-merge-right'));
    assert.equal(afterStaleRevision.entityRedirects.some(redirect =>
      redirect.fromEntityId === 'public-merge-left' || redirect.fromEntityId === 'public-merge-right'), false,
    'the stale public merge must not create a redirect');
    await page.waitForTimeout(250);
    assert.equal(resolveRequests.length, staleRevisionRequestCount + 1, 'stale revision choices must not be resubmitted');

    await refreshedPublicTarget.selectOption('public-merge-right');
    await refreshedPublicCheck.check();
    const stalePayloadRow = f.db.prepare('SELECT payload_json FROM player_library_submissions WHERE id=?').get(f.publicMergeId);
    const stalePayload = JSON.parse(stalePayloadRow.payload_json);
    stalePayload.entities[0].names.push('公共冲突数据已变化');
    f.db.prepare('UPDATE player_library_submissions SET payload_json=? WHERE id=?')
      .run(JSON.stringify(stalePayload), f.publicMergeId);
    let staleUpstreamStatus;
    await page.route(logicalErrorRoute, async route => {
      const upstream = await route.fetch();
      staleUpstreamStatus = upstream.status();
      await route.fulfill({ response: upstream, status: 200 });
    }, { times: 1 });
    const staleRequestCount = resolveRequests.length;
    nextDialogAction = 'accept';
    await click('#btn-conflict-batch-confirm');
    assert.equal(staleUpstreamStatus, 409, 'the real server must reject the stale token with HTTP 409');
    await page.unroute(logicalErrorRoute);
    assert.equal(resolveRequests.length, staleRequestCount + 1, 'stale data must produce one request only');
    assert.equal(await dialog.evaluate(element => element.open), true, 'stale data keeps the refreshed dialog open');
    assert.match(await page.locator('#conflict-batch-error').innerText(), /冲突数据已变化，请刷新后重新确认。/);
    assert.equal(await page.locator('#conflict-batch-list input[type="checkbox"]:checked').count(), 0,
      'authoritative stale refresh requires explicit re-checking');
    assert.equal(await refreshedPublicTarget.inputValue(), '', 'stale conflict data must clear the old retained target');
    assert.equal(await page.locator('#btn-conflict-batch-confirm').isDisabled(), true);
    assert.equal((await api('/state')).revision, raced.revision, 'stale resolution must not publish');
    await page.waitForTimeout(250);
    assert.equal(resolveRequests.length, staleRequestCount + 1, 'stale selections must not be resubmitted');
    await click('#btn-conflict-batch-cancel');

    assert.deepEqual(pageErrors, [], 'no page errors');
    assert.deepEqual(consoleErrors, [], 'no console errors');
    console.log('PASS: real Edge batch conflict preview, distinct success counts, cancellation atomicity, exact two-group POST, unions, redirects, stale revision/group recovery, and responsive dialog');
    console.log('Screenshots: ' + output);
  } finally {
    try { await context?.close(); }
    finally {
      try { await browser?.close(); }
      finally { await f?.close(); }
    }
  }
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
