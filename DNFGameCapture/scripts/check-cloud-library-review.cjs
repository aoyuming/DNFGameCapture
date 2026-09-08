const assert = require('node:assert/strict');
const { mkdirSync } = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { chromium } = require('playwright');

(async () => {
  const { buildLibraryAdminPage, LIBRARY_ADMIN_CSS, LIBRARY_ADMIN_JS } = await import(pathToFileURL(
    path.resolve(__dirname, '../cloud-match-server/dist/library-admin-page.js')).href);
  const browser = await chromium.launch({ headless: true, channel: 'msedge' });
  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
    const errors = []; page.on('pageerror', error => errors.push(error.message));
    let acceptDialog = true;
    const confirmations = [];
    page.on('dialog', dialog => { confirmations.push(dialog.message()); return acceptDialog ? dialog.accept() : dialog.dismiss(); });
    const entity = (entityId, names, gameIds = []) => ({ entityId, names, gameIds });
    const publicEntities = Array.from({ length: 80 }, (_, i) => entity('public-' + i, ['Unchanged player ' + i], ['game-' + i]));
    const submission = {
      id: 1, deviceId: 'test-device', createdAt: 1800000000, status: 'pending', valid: true,
      submissionRevision: 'a'.repeat(64), rawEntityCount: 82, unchangedCount: 79,
      addedEntityCount: 1, updatedEntityCount: 1, matchedEntityCount: 80,
      additions: { names: 2, gameIds: 2 },
      reconciliations: [{ submittedEntityId: 'local-alias', entityId: 'public-0', reason: 'entity_redirect' }],
      entities: [
        entity('public-0', ['Unchanged player 0', '<script>bad()</script>'], ['game-0', 'new-game']),
        ...publicEntities.slice(1), entity('new-player', ['New player'], ['new-player-game']),
      ], conflicts: [],
      entityChanges: [
        { entityId: 'public-0', status: 'updated', additions: { names: ['<script>bad()</script>'], gameIds: ['new-game'] } },
        ...publicEntities.slice(1).map(item => ({ entityId: item.entityId, status: 'unchanged', additions: { names: [], gameIds: [] } })),
        { entityId: 'new-player', status: 'added', additions: { names: ['New player'], gameIds: ['new-player-game'] } },
      ],
    };
    const noChange = { ...submission, id: 2, entities: [publicEntities[1]], rawEntityCount: 1, unchangedCount: 1,
      addedEntityCount: 0, updatedEntityCount: 0, matchedEntityCount: 1,
      additions: { names: 0, gameIds: 0 }, reconciliations: [],
      entityChanges: [{ entityId: 'public-1', status: 'unchanged', additions: { names: [], gameIds: [] } }] };
    const conflict = { ...noChange, id: 3, entities: [entity('unresolved', ['Conflicting player'], ['game-0'])],
      unchangedCount: 0, addedEntityCount: 1,
      conflicts: [{ kind: 'names', value: 'Unchanged player 0', entityIds: ['unresolved', 'public-0'] }] };
    let attempts = 0, processed = false, saved = 0;
    await page.route('http://review.test/**', async route => {
      const url = new URL(route.request().url());
      let body, contentType = 'application/json';
      if (url.pathname === '/admin/library') { body = buildLibraryAdminPage('test-csrf'); contentType = 'text/html'; }
      else if (url.pathname === '/admin/library/style.css') { body = LIBRARY_ADMIN_CSS; contentType = 'text/css'; }
      else if (url.pathname === '/admin/library/app.js') { body = LIBRARY_ADMIN_JS; contentType = 'application/javascript'; }
      else if (url.pathname === '/admin/api/library/state') {
        body = JSON.stringify({ ok: true, revision: 7, entities: publicEntities,
          submissions: processed ? [noChange, conflict] : [submission, noChange, conflict],
          stats: { pending: processed ? 2 : 3, conflicts: 1, pendingGameIds: 84, entities: 80, gameIds: 80 } });
      } else if (url.pathname === '/admin/api/library/submissions/1' && route.request().method() === 'PUT') {
        const payload = route.request().postDataJSON();
        assert.equal(payload.confirmReconciliations, true);
        assert.equal(payload.revision, 7);
        assert.equal(payload.submissionRevision, 'a'.repeat(64));
        assert.equal(payload.entities.length, 81, 'collapsed unchanged rows must remain in saved payload');
        assert.equal(payload.entities[1].entityId, 'public-1', 'review order stays stable');
        saved++; submission.entities = payload.entities; submission.submissionRevision = 'b'.repeat(64); submission.reconciliations = [];
        body = JSON.stringify({ ok: true, revision: 7, submission });
      } else if (/\/submissions\/[123]$/.test(url.pathname)) {
        await new Promise(resolve => setTimeout(resolve, 100));
        const item = [submission, noChange, conflict][Number(url.pathname.split('/').pop()) - 1];
        body = JSON.stringify({ ok: true, revision: 7, submission: item });
      } else if (url.pathname === '/admin/api/library/review') {
        assert.equal(route.request().headers()['x-dnf-admin-csrf'], 'test-csrf');
        assert.deepEqual(route.request().postDataJSON(), { revision: 7, action: 'approve',
          submissions: [{ id: 1, submissionRevision: submission.submissionRevision }], skipConflicts: true });
        if (++attempts === 1) {
          await route.fulfill({ status: 503, contentType, body: JSON.stringify({ ok: false, code: 'internal_error' }) }); return;
        }
        submission.status = 'approved'; processed = true; body = JSON.stringify({ ok: true, revision: 7,
          acceptedEntityCount: 81, skippedEntityCount: 0, pendingSubmissionCount: 0 });
      } else throw new Error('Unexpected request: ' + url.pathname);
      await route.fulfill({ contentType, body });
    });
    const idle = () => page.waitForFunction(() => !document.body.hasAttribute('aria-busy'));
    const click = async selector => { await page.locator(selector).click(); await idle(); };
    await page.goto('http://review.test/admin/library'); await idle();
    assert.equal(await page.locator('.pending-title').first().innerText(), '投稿 #1');
    assert.doesNotMatch(await page.locator('.pending-item').first().innerText(), /Unchanged player|<script>/);
    assert.match(await page.locator('.pending-item').first().innerText(), /新增 1.*补充 1.*无变化 79/);
    assert.match(await page.locator('.pending-item').first().innerText(), /名称 \+2.*游戏ID \+2/);
    assert.doesNotMatch(await page.locator('body').innerText(), /冒险团/);
    await click('[data-submission-id="1"]');
    assert.match(await page.locator('.submission-summary').innerText(), /原始 82.*归并后 81/);
    assert.match(await page.locator('.submission-summary').innerText(), /匹配公共实体 80/);
    assert.match(await page.locator('.entity-delta').first().innerText(), /new-game/);
    assert.doesNotMatch(await page.locator('.entity-delta').first().innerText(), /game-0/);
    assert.equal(await page.locator('.unchanged-entities .review-entity').count(), 79);
    assert.equal(await page.locator('.unchanged-entities').getAttribute('open'), null);
    assert.equal(await page.locator('#detail-view script').count(), 0);
    await page.locator('.reconciliation-list > summary').click();
    assert.match(await page.locator('.reconciliation-list').innerText(), /local-alias.*public-0/);
    const output = path.resolve(__dirname, '../build/cloud-library-review'); mkdirSync(output, { recursive: true });
    for (const [width, height] of [[1280, 800], [900, 800], [390, 844]]) {
      await page.setViewportSize({ width, height });
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), true);
      const title = await page.locator('.pending-title').first().boundingBox();
      assert(title.height < 45, 'submission title must remain compact');
      await page.screenshot({ path: path.join(output, 'review-' + width + '.png'), fullPage: true });
    }
    await page.setViewportSize({ width: 1280, height: 800 });
    // Hidden unchanged editors still participate in the payload and can be inspected.
    await page.locator('.unchanged-entities > summary').click();
    assert.equal(await page.getByLabel('审核实体 2 ID', { exact: true }).inputValue(), 'public-1');
    await page.getByLabel('审核实体 1 并列名称 / 别名', { exact: true }).fill('Unchanged player 0\nReviewed name');
    acceptDialog = false; await click('#btn-review-save');
    assert.equal(saved, 0, 'cancelling inferred association confirmation must not write');
    assert.match(await page.getByLabel('审核实体 1 并列名称 / 别名', { exact: true }).inputValue(), /Reviewed name/);
    assert.equal(confirmations.at(-1), '保存将确认当前显示的选手关联；原始投稿会保留，公共库仍需审核通过后更新。是否继续？');
    acceptDialog = true; await click('#btn-review-save'); assert.equal(saved, 1);
    await click('[data-submission-id="3"]');
    assert.equal(await page.locator('#btn-approve').isDisabled(), false);
    assert.equal(await page.locator('#btn-reject').isDisabled(), false);
    assert.match(await page.locator('.conflict-row').innerText(), /Unchanged player 0/);
    await click('[data-submission-id="1"]');
    await click('#btn-approve-added');
    assert.match(await page.locator('#message').innerText(), /服务器处理失败/);
    assert.equal(await page.locator('#btn-approve-added').isDisabled(), false);
    await click('#btn-approve-added');
    assert.equal(processed, true); assert.equal(attempts, 2);
    assert.equal(await page.locator('#btn-approve-added').isDisabled(), true, 'no-op and conflicted submissions are not additions');
    assert.deepEqual(errors, []);
    console.log('PASS: compact 82-row summary, actual additions, collapsed unchanged editors, reconciliation, conflict guard, guarded batch retry, escaping, 3 viewport screenshots');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
