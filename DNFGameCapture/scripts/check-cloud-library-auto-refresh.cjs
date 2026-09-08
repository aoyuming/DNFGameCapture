const assert = require('node:assert/strict');
const { before, after, test } = require('node:test');
const { readFileSync, mkdirSync } = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { tmpdir } = require('node:os');
const vm = require('node:vm');
const { createRequire } = require('node:module');
const { chromium } = require('playwright');

const root = path.resolve(__dirname, '..');
const serverRequire = createRequire(path.join(root, 'cloud-match-server/package.json'));
const ts = serverRequire('typescript');
const compiled = ts.transpileModule(readFileSync(path.join(root, 'cloud-match-server/src/library-admin-page.ts'), 'utf8'), {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 }
});
const pageModule = { exports: {} };
vm.runInNewContext(compiled.outputText, pageModule);
const { buildLibraryAdminPage, LIBRARY_ADMIN_JS, LIBRARY_ADMIN_CSS } = pageModule.exports;
const screenshots = process.env.SCREENSHOT_DIR || path.join(tmpdir(), 'dnf-library-auto-refresh');
let browser;
before(async () => { browser = await chromium.launch({ headless: true, channel: 'msedge' }); });
after(async () => { await browser?.close(); });

const entity = i => ({ entityId: 'player-' + i, names: ['Player ' + i], gameIds: Array.from({ length: 25 }, (_, n) => 'Game-' + i + '-' + n) });
const submission = i => ({ id: i, deviceId: 'fixture-' + i, createdAt: 1788825600, status: 'pending',
  submissionRevision: String(i).padStart(64, '0'), valid: true, conflicts: [], entities: [entity('pending-' + i)] });

async function fixture(t) {
  const f = { revision: 1, entities: Array.from({ length: 36 }, (_, i) => entity(i + 1)),
    submissions: Array.from({ length: 30 }, (_, i) => submission(i + 1)), calls: [], active: 0, maxActive: 0,
    holdState: false, holdMutation: false, stateStatus: 200, responseState: null, completed: new Map() };
  f.snapshot = () => ({ ok: true, revision: f.revision, entities: structuredClone(f.entities), submissions: structuredClone(f.submissions),
    stats: { pending: f.submissions.length, conflicts: 0, pendingGameIds: f.submissions.length * 25,
      entities: f.entities.length, gameIds: f.entities.reduce((sum, item) => sum + item.gameIds.length, 0) } });
  const server = http.createServer(async (request, response) => {
    const url = new URL(request.url, 'http://127.0.0.1');
    const send = (body, type = 'application/json', status = 200) => {
      if (response.destroyed) return;
      response.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store' });
      response.end(type === 'application/json' ? JSON.stringify(body) : body);
    };
    if (url.pathname === '/admin/library') return send(buildLibraryAdminPage('fixture'), 'text/html; charset=utf-8');
    if (url.pathname === '/admin/library/app.js') return send(LIBRARY_ADMIN_JS, 'application/javascript');
    if (url.pathname === '/admin/library/style.css') return send(LIBRARY_ADMIN_CSS, 'text/css');
    if (url.pathname === '/favicon.ico') { response.writeHead(204); return response.end(); }
    let raw = ''; for await (const chunk of request) raw += chunk;
    const body = raw ? JSON.parse(raw) : null;
    f.calls.push({ path: url.pathname, method: request.method, body });
    ++f.active; f.maxActive = Math.max(f.maxActive, f.active);
    response.once('close', () => { --f.active; });
    if (url.pathname.endsWith('/state')) {
      const data = structuredClone(f.responseState || f.snapshot()), status = f.stateStatus;
      if (f.holdState) { f.holdState = false; await new Promise(resolve => { f.releaseState = resolve; response.once('close', resolve); }); }
      return send(status === 200 ? data : { ok: false, code: 'internal_error' }, 'application/json', status);
    }
    if (request.method === 'GET' && url.pathname.includes('/submissions/')) {
      const id = Number(url.pathname.split('/').pop());
      const found = f.submissions.find(item => item.id === id) || f.completed.get(id);
      return send(found ? { ok: true, revision: f.revision, submission: found } : { ok: false, code: 'submission_not_found' }, 'application/json', found ? 200 : 404);
    }
    if (f.holdMutation) { f.holdMutation = false; await new Promise(resolve => { f.releaseMutation = resolve; response.once('close', resolve); }); }
    if (body?.revision !== f.revision) return send({ ok: false, code: 'stale_revision' }, 'application/json', 409);
    if (url.pathname.endsWith('/import/preview')) return send({ ok: true, revision: f.revision, entities: [entity('import')], conflicts: [] });
    if (url.pathname.endsWith('/entities') && request.method === 'POST') {
      const created = { ...body.entity, entityId: 'created-player' }; f.entities.push(created); ++f.revision;
      return send({ ok: true, revision: f.revision, entity: created });
    }
    return send({ ok: false, code: 'stale_submission' }, 'application/json', 409);
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } });
  const page = await context.newPage(); page.setDefaultTimeout(3000);
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  page.on('dialog', dialog => f.onDialog ? f.onDialog(dialog) : dialog.accept());
  await page.clock.install({ time: new Date('2026-09-08T00:00:00Z') });
  await page.clock.pauseAt(new Date('2026-09-08T00:00:01Z'));
  await page.addInitScript(() => {
    window.apiPending = 0; window.stateResponses = 0; window.fixtureHidden = false;
    Object.defineProperty(document, 'hidden', { configurable: true, get: () => window.fixtureHidden });
    const fetch = window.fetch;
    window.fetch = async (...args) => {
      ++window.apiPending;
      try { return await fetch(...args); }
      finally { --window.apiPending; if (String(args[0]).endsWith('/state')) ++window.stateResponses; }
    };
  });
  t.after(async () => {
    f.releaseState?.(); f.releaseMutation?.();
    await context.close(); server.closeAllConnections(); await new Promise(resolve => server.close(resolve));
    assert.deepEqual(errors, [], 'No uncaught page errors');
  });
  f.page = page;
  f.idle = () => page.waitForFunction(() => window.apiPending === 0 && !document.body.hasAttribute('aria-busy'));
  f.stateCalls = () => f.calls.filter(call => call.path.endsWith('/state')).length;
  f.poll = async (ms = 5001) => {
    const before = await page.evaluate(() => window.stateResponses);
    await page.clock.runFor(ms);
    await page.waitForFunction(before => window.stateResponses > before, before);
    await f.idle();
  };
  f.focus = () => page.evaluate(() => window.dispatchEvent(new Event('focus')));
  await page.goto('http://127.0.0.1:' + server.address().port + '/admin/library');
  await page.waitForFunction(() => document.getElementById('stat-pending').textContent === '30'); await f.idle();
  f.open = async () => { await page.locator('[data-submission-id="1"]').click(); await f.idle(); await page.locator('#public-main-list').selectOption('player-1'); };
  return f;
}

test('polls silently every five seconds and refreshes same-revision submissions', async t => {
  const f = await fixture(t), page = f.page; await f.open();
  await page.locator('#public-alias-list').selectOption({ index: 4 });
  const alias = await page.locator('#public-alias-list').inputValue();
  await page.locator('[data-field="gameIds"]').focus();
  await page.evaluate(() => {
    window.originalInput = document.activeElement; originalInput.setSelectionRange(3, 8, 'backward');
    window.originalPending = document.getElementById('pending-list').firstElementChild;
    window.originalOption = document.getElementById('public-main-list').firstElementChild;
  });
  await f.poll();
  assert.equal(await page.evaluate(() => document.activeElement === window.originalInput), true, 'Unchanged polling must not rebuild the editor');
  assert.equal(await page.evaluate(() => originalPending.isConnected && originalOption.isConnected), true, 'Unchanged polling must not rebuild lists');
  assert.equal(await page.locator('#message').isVisible(), false);
  f.submissions.push(submission(31));
  f.submissions[0].entities[0].gameIds.push('External game'); f.submissions[0].submissionRevision = 'a'.repeat(64);
  await f.poll();
  assert.equal(await page.locator('#stat-pending').textContent(), '31');
  assert.match(await page.locator('[data-field="gameIds"]').inputValue(), /External game/);
  assert.deepEqual(await page.locator('[data-field="gameIds"]').evaluate(el => [document.activeElement === el, el.selectionStart, el.selectionEnd, el.selectionDirection]), [true, 3, 8, 'backward']);
  assert.equal(await page.locator('#public-alias-list').inputValue(), alias);
  assert.equal(f.maxActive, 1);
});

test('keeps dirty public/review/import drafts, selection, caret, scroll and stale guards', async t => {
  const f = await fixture(t), page = f.page; await f.open();
  await page.locator('#public-alias-list').selectOption({ index: 24 });
  await page.locator('.pending-item input').first().check();
  await page.locator('#public-names').fill('Public draft\nStill editing');
  await page.locator('#public-import-text').fill('Import draft=(Game)');
  await page.locator('[data-field="gameIds"]').fill(Array.from({ length: 60 }, (_, i) => 'Review draft ' + i).join('\n'));
  const positions = await page.evaluate(() => {
    const el = document.activeElement; window.dirtyInput = el; el.setSelectionRange(6, 11, 'backward'); el.scrollTop = 120;
    for (const id of ['pending-list', 'detail-view', 'public-main-list', 'public-alias-list', 'public-editor']) document.getElementById(id).scrollTop = 65;
    return ['pending-list', 'detail-view', 'public-main-list', 'public-alias-list', 'public-editor'].map(id => document.getElementById(id).scrollTop);
  });
  ++f.revision; f.submissions[0].submissionRevision = 'b'.repeat(64); f.entities[0].names = ['External name'];
  await f.poll();
  assert.equal(await page.locator('#public-names').inputValue(), 'Public draft\nStill editing');
  assert.equal(await page.locator('#public-import-text').inputValue(), 'Import draft=(Game)');
  assert.match(await page.locator('[data-field="gameIds"]').inputValue(), /Review draft 59/);
  assert.equal(await page.locator('#selected-count').textContent(), '1');
  assert.deepEqual(await page.evaluate(() => [document.activeElement === window.dirtyInput, dirtyInput.selectionStart, dirtyInput.selectionEnd, dirtyInput.selectionDirection, dirtyInput.scrollTop]), [true, 6, 11, 'backward', 120]);
  assert.deepEqual(await page.evaluate(() => ['pending-list', 'detail-view', 'public-main-list', 'public-alias-list', 'public-editor'].map(id => document.getElementById(id).scrollTop)), positions);
  await page.locator('#btn-public-save').click(); await f.idle();
  assert.equal(f.calls.find(call => call.method === 'PUT' && call.path.endsWith('/entities/player-1')).body.revision, 1);
  await page.locator('#btn-review-save').click(); await f.idle();
  const review = f.calls.find(call => call.method === 'PUT' && call.path.endsWith('/submissions/1'));
  assert.equal(review.body.revision, 1); assert.equal(review.body.submissionRevision, String(1).padStart(64, '0'));
  assert.match(await page.locator('[data-field="gameIds"]').inputValue(), /Review draft 59/);
  await page.locator('#btn-reject').click(); await f.idle();
  assert.equal(f.calls.find(call => call.path.endsWith('/review')).body.submissions[0].submissionRevision, String(1).padStart(64, '0'), 'Selected review tokens must not be rebased');
  mkdirSync(screenshots, { recursive: true });
  await page.screenshot({ path: path.join(screenshots, 'drafts-1440.png'), fullPage: true });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.screenshot({ path: path.join(screenshots, 'drafts-390.png'), fullPage: true });
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), true);
});

test('coalesces focus refreshes and preserves edits made while a poll is in flight', async t => {
  const f = await fixture(t), page = f.page; await f.open();
  f.holdState = true;
  await page.clock.runFor(5001); await page.waitForFunction(() => window.apiPending === 1);
  const calls = f.stateCalls();
  assert.equal(await page.locator('#public-names').isDisabled(), false);
  await page.locator('#public-names').fill('Typed during request');
  await f.focus(); await f.focus(); await page.clock.runFor(6000);
  assert.equal(f.stateCalls(), calls); assert.equal(f.maxActive, 1);
  f.releaseState(); await f.idle();
  assert.equal(await page.locator('#public-names').inputValue(), 'Typed during request');
  assert.equal(await page.locator('#public-names').evaluate(el => document.activeElement === el), true);
});

test('pauses hidden pages, resumes on visibility/focus and silently recovers from transient failures', async t => {
  const f = await fixture(t), page = f.page;
  await page.evaluate(() => { window.fixtureHidden = true; document.dispatchEvent(new Event('visibilitychange')); });
  const calls = f.stateCalls(); await page.clock.runFor(20000); assert.equal(f.stateCalls(), calls);
  f.submissions.push(submission(31));
  await page.evaluate(() => { window.fixtureHidden = false; document.dispatchEvent(new Event('visibilitychange')); });
  await page.waitForFunction(() => document.getElementById('stat-pending').textContent === '31'); await f.idle();
  f.submissions.push(submission(32)); await f.focus();
  await page.waitForFunction(() => document.getElementById('stat-pending').textContent === '32'); await f.idle();
  f.stateStatus = 503; await f.poll();
  assert.equal(await page.locator('#message').isVisible(), false);
  assert.equal(await page.locator('#stat-pending').textContent(), '32');
  f.stateStatus = 200; ++f.revision; await f.poll();
  assert.match(await page.locator('#public-meta').textContent(), /2/);
  const latest = await page.locator('#status-text').textContent();
  f.responseState = { ...f.snapshot(), revision: 1, submissions: [], stats: { pending: 0 } }; await f.poll();
  assert.equal(await page.locator('#stat-pending').textContent(), '32');
  assert.equal(await page.locator('#status-text').textContent(), latest);
});

test('serializes mutations behind polling and defers polling while a mutation is busy', async t => {
  const f = await fixture(t), page = f.page;
  await page.locator('#public-names').fill('New player draft');
  f.holdState = true; await page.clock.runFor(5001); await page.waitForFunction(() => window.apiPending === 1);
  f.holdMutation = true; await page.locator('#btn-public-add').click();
  await f.focus();
  assert.equal(f.calls.some(call => call.method === 'POST'), false, 'Mutation waits for the active read');
  f.releaseState(); await page.waitForFunction(() => window.apiPending === 1 && document.body.hasAttribute('aria-busy'));
  await new Promise(resolve => setImmediate(resolve));
  const calls = f.stateCalls(); await page.clock.runFor(10000); assert.equal(f.stateCalls(), calls);
  f.releaseMutation(); await f.idle();
  assert.equal(f.maxActive, 1); assert.equal(f.entities.at(-1).names[0], 'New player draft');
  assert.equal(await page.locator('#public-main-list').inputValue(), 'created-player');
  f.submissions.push(submission(31)); await f.poll();
  assert.equal(await page.locator('#stat-pending').textContent(), '31');
});

test('discards slow snapshots after public selection or a new draft', async t => {
  const f = await fixture(t), page = f.page; await f.open();
  f.responseState = f.snapshot(); f.responseState.entities[1].names = ['Obsolete response'];
  f.holdState = true; await page.clock.runFor(5001); await page.waitForFunction(() => window.apiPending === 1);
  await page.locator('#public-main-list').selectOption('player-2');
  f.releaseState(); await f.idle();
  assert.equal(await page.locator('#public-main-list').inputValue(), 'player-2');
  assert.equal(await page.locator('#public-names').inputValue(), 'Player 2');
  f.holdState = true; await page.clock.runFor(5001); await page.waitForFunction(() => window.apiPending === 1);
  await page.locator('#btn-public-new').click();
  f.releaseState(); await f.idle();
  assert.equal(await page.locator('#public-main-list').inputValue(), '');
  assert.equal(await page.locator('#public-names').inputValue(), '');
  assert.equal(await page.locator('#public-names').evaluate(el => document.activeElement === el), true);
});

test('does not render or poll during IME composition and resumes afterward', async t => {
  const f = await fixture(t), page = f.page; await f.open();
  f.holdState = true; await page.clock.runFor(5001); await page.waitForFunction(() => window.apiPending === 1);
  await page.locator('[data-field="names"]').focus();
  await page.evaluate(() => {
    const input = document.activeElement; window.compositionInput = input;
    input.dispatchEvent(new CompositionEvent('compositionstart', { bubbles: true }));
    input.value = 'IME draft'; input.dispatchEvent(new InputEvent('input', { bubbles: true, isComposing: true, data: 'draft' }));
    input.setSelectionRange(3, 6);
  });
  f.releaseState(); await f.idle();
  const calls = f.stateCalls(); await page.clock.runFor(20000); assert.equal(f.stateCalls(), calls);
  assert.deepEqual(await page.evaluate(() => [document.activeElement === compositionInput, compositionInput.value, compositionInput.selectionStart, compositionInput.selectionEnd]), [true, 'IME draft', 3, 6]);
  f.submissions.push(submission(31));
  await page.evaluate(() => compositionInput.dispatchEvent(new CompositionEvent('compositionend', { bubbles: true })));
  await page.waitForFunction(() => document.getElementById('stat-pending').textContent === '31'); await f.idle();
  assert.equal(await page.locator('[data-field="names"]').inputValue(), 'IME draft');
});

test('stops automatic requests after auth expiry without losing drafts or repeating notices', async t => {
  const f = await fixture(t), page = f.page; await f.open();
  await page.locator('#public-names').fill('Draft after auth expiry');
  f.stateStatus = 401; await f.poll();
  assert.equal(await page.locator('#message').isVisible(), true);
  const message = await page.locator('#message').textContent(), calls = f.stateCalls();
  await page.locator('#message').click(); await f.focus(); await page.clock.runFor(30000);
  assert.equal(f.stateCalls(), calls); assert.equal(await page.locator('#message').isVisible(), false);
  assert.equal(await page.locator('#message').textContent(), message);
  assert.equal(await page.locator('#public-names').inputValue(), 'Draft after auth expiry');
});

test('updates externally rejected submissions without a public revision change', async t => {
  const f = await fixture(t), page = f.page; await f.open();
  f.completed.set(1, { ...f.submissions.shift(), status: 'rejected', submissionRevision: 'c'.repeat(64) });
  await f.poll();
  assert.equal(await page.locator('#stat-pending').textContent(), '29');
  assert.equal(await page.locator('[data-submission-id="1"]').count(), 0);
  assert.equal(await page.locator('[data-field="names"]').isDisabled(), true);
  assert.equal(await page.locator('#btn-review-save').count(), 0);
  assert.equal(await page.locator('#message').isVisible(), false);
});

test('preserves merge selections and the captured merge revision while the dialog is open', async t => {
  const f = await fixture(t), page = f.page;
  await page.locator('#btn-public-merge').click(); await f.idle();
  await page.locator('#merge-options input').nth(0).check(); await page.locator('#merge-options input').nth(1).check();
  await page.locator('#merge-target').selectOption('player-2');
  const calls = f.stateCalls(); ++f.revision; await f.focus(); await page.clock.runFor(10000);
  assert.equal(f.stateCalls(), calls); assert.equal(await page.locator('#merge-target').inputValue(), 'player-2');
  await page.locator('#btn-merge-confirm').click(); await f.idle();
  const request = f.calls.find(call => call.path.endsWith('/entities/merge'));
  assert.equal(request.body.revision, 1); assert.deepEqual(request.body.entityIds, ['player-1', 'player-2']);
  assert.equal(request.body.targetEntityId, 'player-2');
  assert.equal(await page.locator('#merge-options input:checked').count(), 2);
  await page.locator('#btn-merge-cancel').click();
  await page.waitForFunction(() => document.getElementById('public-meta').textContent.includes('2')); await f.idle();
});

test('keeps import confirmation revision and text when an external write races the preview', async t => {
  const f = await fixture(t), page = f.page;
  await page.locator('#public-import-text').fill('Import player=(Import game)');
  f.holdMutation = true;
  f.onDialog = async dialog => { ++f.revision; await dialog.accept(); };
  await page.locator('#btn-public-import').click(); await page.waitForFunction(() => window.apiPending === 1);
  const calls = f.stateCalls(); await f.focus(); await page.clock.runFor(10000); assert.equal(f.stateCalls(), calls);
  f.releaseMutation(); await f.idle();
  const request = f.calls.find(call => call.path.endsWith('/import'));
  assert.equal(request.body.revision, 1); assert.equal(request.body.text, 'Import player=(Import game)');
  assert.equal(await page.locator('#public-import-text').inputValue(), 'Import player=(Import game)');
  assert.equal(f.maxActive, 1);
});

test('times out a stuck poll and resumes without blocking editing', async t => {
  const f = await fixture(t), page = f.page;
  f.holdState = true; await page.clock.runFor(5001); await page.waitForFunction(() => window.apiPending === 1);
  await page.clock.runFor(15001); await f.idle();
  assert.equal(await page.locator('#message').isVisible(), false);
  assert.equal(await page.locator('#public-names').isDisabled(), false);
  f.submissions.push(submission(31)); await f.poll();
  assert.equal(await page.locator('#stat-pending').textContent(), '31');
  assert.equal(f.maxActive, 1);
});

test('selected approvals retain the public revision when a fifth shared ID changes automatic grouping', async t => {
  const f = await fixture(t), page = f.page;
  const shared = ['Shared-1', 'Shared-2', 'Shared-3', 'Shared-4'];
  f.entities[0].gameIds = [...shared];
  f.submissions[0].entities[0].gameIds = [...shared, 'Shared-5'];
  f.submissions[0].automaticGroups = [];
  await f.poll(); await f.open();
  await page.locator('.pending-item input').first().check();
  const token = f.submissions[0].submissionRevision;
  assert.equal(await page.locator('.automatic-group-list').count(), 0);
  f.entities[0].gameIds.push('Shared-5'); ++f.revision;
  f.submissions[0].automaticGroups = [[f.entities[0].entityId, f.submissions[0].entities[0].entityId]];
  await f.poll();
  assert.equal(await page.locator('#selected-count').textContent(), '1');
  assert.equal(await page.locator('.pending-item input').first().isChecked(), true);
  assert.equal(await page.locator('.automatic-group-list').count(), 1);
  assert.match(await page.locator('.pending-item').first().textContent(), /已选版本过期，请重新选择/);
  assert.equal(f.submissions[0].submissionRevision, token);
  await page.locator('#btn-approve').click(); await f.idle();
  const first = f.calls.filter(call => call.path.endsWith('/review')).at(-1);
  assert.equal(first.body.revision, 1, 'A retained checkbox must never approve against a silently advanced public revision');
  assert.deepEqual(first.body.submissions, [{ id: 1, submissionRevision: token }]);
  assert.match(await page.locator('#message').textContent(), /公共库版本已变化/);
  assert.equal(await page.locator('#selected-count').textContent(), '1');
  await page.locator('.pending-item input').nth(1).check();
  await page.locator('#btn-approve').click(); await f.idle();
  assert.equal(f.calls.filter(call => call.path.endsWith('/review')).at(-1).body.revision, 1, 'A new selection cannot rebase older selections');
  await page.locator('.pending-item input').first().uncheck();
  await page.locator('.pending-item input').first().check();
  await page.locator('#btn-approve').click(); await f.idle();
  assert.equal(f.calls.filter(call => call.path.endsWith('/review')).at(-1).body.revision, 2, 'Explicit reselection captures the newly viewed grouping');
});
