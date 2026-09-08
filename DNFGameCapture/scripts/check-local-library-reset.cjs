const assert = require('node:assert/strict');
const { spawn, execFileSync } = require('node:child_process');
const { once } = require('node:events');
const { mkdirSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');
const { chromium } = require('playwright');

const screenshots = process.env.SCREENSHOT_DIR || path.join(tmpdir(), 'dnf-local-library-reset');
const snapshot = JSON.parse(execFileSync(process.execPath,
    [path.join(__dirname, 'player_library_web_test.js'), '--fixture'], { encoding: 'utf8', windowsHide: true }));

function installFixture(state) {
    const listeners = [];
    Object.assign(state, {
        blueScore: 3, redScore: 2, startupReady: true, isAuthValid: true, isMonitoring: true,
        aliasDbAutoSync: { enabled: true }, cloudMatch: { shouldPrompt: false, broadcasterName: 'Fixture' },
        recentEvents: [{ time: '12:00:00', text: 'Preserved match history' }],
        players: Array.from({ length: 8 }, (_, i) => ({ team: i < 4 ? 0 : 1,
            name: i === 0 ? 'Alpha' : `Fielded${i + 1}`, aliases: [`match-only-${i + 1}`], kills: i + 1, deaths: i, akCount: i + 1 }))
    });
    const emit = message => listeners.forEach(listener => listener({ data: message }));
    window.resetFixture = { state, emit, commands: [], sync: () => emit({ action: 'sync_state', data: state }) };
    window.chrome = window.chrome || {};
    window.chrome.webview = {
        addEventListener(type, listener) { if (type === 'message') listeners.push(listener); },
        postMessage(command) {
            window.resetFixture.commands.push(command);
            if (command.action === 'page_ready' || command.action === 'cmd_identity_refresh') {
                setTimeout(window.resetFixture.sync, 0);
            }
        }
    };
}

async function startFixture() {
    const server = spawn(process.execPath, [path.join(__dirname, 'player-library-preview.cjs')], {
        env: { ...process.env, PORT: '0' }, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe']
    });
    try {
        const url = await new Promise((resolve, reject) => {
            let output = '';
            const timeout = setTimeout(() => reject(new Error('Fixture startup timed out')), 10000);
            server.once('error', error => { clearTimeout(timeout); reject(error); });
            server.once('exit', code => { clearTimeout(timeout); reject(new Error(`Fixture exited: ${code}`)); });
            server.stdout.on('data', chunk => {
                output += chunk;
                const match = output.match(/http:\/\/127\.0\.0\.1:\d+\//);
                if (match) { clearTimeout(timeout); resolve(match[0]); }
            });
            server.stderr.on('data', chunk => process.stderr.write(chunk));
        });
        return { server, url };
    } catch (error) { server.kill(); throw error; }
}

async function assertContained(locator, viewport) {
    const bounds = await locator.evaluate(node => {
        const { x, y, right, bottom } = node.getBoundingClientRect();
        return { x, y, right, bottom, width: node.clientWidth, scrollWidth: node.scrollWidth };
    });
    assert.ok(bounds.x >= 0 && bounds.right <= viewport.width + 1, 'Control exceeds viewport width');
    assert.ok(bounds.y >= 0 && bounds.bottom <= viewport.height + 1, 'Control exceeds viewport height');
    assert.ok(bounds.scrollWidth <= bounds.width + 1, 'Text overflows control');
}

(async () => {
    mkdirSync(screenshots, { recursive: true });
    const fixture = await startFixture();
    let browser;
    try {
        browser = await chromium.launch({ headless: true, channel: process.env.BROWSER_CHANNEL || 'msedge' });
        const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
        page.setDefaultTimeout(10000);
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
        await page.route('**/fixture.js', route => route.fulfill({ contentType: 'text/javascript',
            body: `(${installFixture.toString()})(${JSON.stringify(snapshot)});` }));
        await page.goto(fixture.url);
        await page.waitForFunction(() => canEditPlayerLibrary());
        const reset = page.locator('#btn-reset-local-library');
        const commandCount = () => page.evaluate(() => resetFixture.commands.filter(command => command.action === 'cmd_identity_reset_local').length);
        const openReset = async () => {
            await page.locator('#btn-more-controls').click();
            await reset.click();
            assert.equal(await page.locator('#modal-input').isVisible(), false);
        };
        for (const viewport of [{ width: 1280, height: 900 }, { width: 900, height: 600 }, { width: 390, height: 844 }]) {
            await page.setViewportSize(viewport);
            await page.locator('#btn-more-controls').click();
            await reset.scrollIntoViewIfNeeded();
            await assertContained(reset, viewport);
            await page.screenshot({ path: path.join(screenshots, `menu-${viewport.width}.png`) });
            await reset.click();
            const text = await page.locator('#modal-msg').textContent();
            for (const phrase of ['\u5168\u90e8\u672c\u5730\u9009\u624b', '\u505c\u6b62\u76d1\u63a7', '\u573a\u4e0a8\u4e2a\u4f4d\u7f6e\u7684\u9009\u624b\u540d\u79f0\u3001ID\u548c\u4e2a\u4eba\u6218\u7ee9', 'players_config.txt', '\u5907\u4efd', '\u961f\u4f0d\u603b\u5206', '\u6700\u8fd1\u6218\u7ee9\u8bb0\u5f55', '\u4e91\u7aef\u5e93\u4fdd\u6301\u4e0d\u53d8', '\u81ea\u52a8\u540c\u6b65', '\u624b\u52a8']) assert.ok(text.includes(phrase), `Missing confirmation scope: ${phrase}`);
            await assertContained(page.locator('#custom-modal .modal-box'), viewport);
            for (const button of [page.locator('#modal-cancel'), page.locator('#modal-ok')]) await assertContained(button, viewport);
            await page.screenshot({ path: path.join(screenshots, `confirm-${viewport.width}.png`) });
            await page.locator('#modal-cancel').click();
            assert.equal(await commandCount(), 0);
        }
        await page.setViewportSize({ width: 1280, height: 900 });
        for (const busy of ['library', 'manual', 'auto', 'following']) {
            await page.evaluate(busy => {
                const state = resetFixture.state;
                state.playerLibrary.busy = busy === 'library';
                state.aliasDbAutoSync = { enabled: true, manualInFlight: busy === 'manual', inFlight: busy === 'auto' };
                state.cloudMatch.realtimeFollowing = busy === 'following';
                resetFixture.sync();
            }, busy);
            assert.equal(await reset.isDisabled(), true, `${busy} must disable reset without a library revision change`);
        }
        await page.evaluate(() => {
            resetFixture.state.cloudMatch.realtimeFollowing = false;
            resetFixture.sync();
        });
        assert.equal(await reset.isEnabled(), true);
        await openReset();
        await page.evaluate(() => { resetFixture.state.aliasDbAutoSync.manualInFlight = true; resetFixture.sync(); });
        await page.locator('#modal-ok').click();
        assert.equal(await commandCount(), 0, 'Busy state must be rechecked after confirmation opens');
        await page.locator('#modal-ok').click();
        await page.evaluate(() => { resetFixture.state.aliasDbAutoSync.manualInFlight = false; resetFixture.sync(); });
        await openReset();
        await page.evaluate(() => {
            resetFixture.state.playerLibrary.revision = 2;
            resetFixture.state.playerIdentity.revision = 2;
            resetFixture.sync();
        });
        await page.waitForFunction(() => canEditPlayerLibrary());
        await page.locator('#modal-ok').click();
        const command = await page.evaluate(() => resetFixture.commands.find(command => command.action === 'cmd_identity_reset_local'));
        assert.equal(command.revision, 1);
        assert.equal(command.confirmed, true);
        assert.ok(command.requestId);
        assert.equal(await reset.isDisabled(), true);
        assert.equal(await page.evaluate(() => Object.keys(savedDB).length), 4);
        await page.evaluate(requestId => resetFixture.emit({ action: 'identity_error', requestId, message: 'Stale library revision' }), command.requestId);
        assert.equal(await page.locator('#modal-msg').textContent(), 'Stale library revision');
        await page.locator('#modal-ok').click();
        assert.equal(await reset.isEnabled(), true);
        const preserved = await page.evaluate(() => ({
            scores: [document.querySelector('#team-blue .team-score-input').value, document.querySelector('#team-red .team-score-input').value],
            history: recentEvents, cloud: resetFixture.state.cloudMatch
        }));
        await openReset();
        await page.locator('#modal-ok').click();
        assert.equal(await page.locator('.name-input').first().inputValue(), 'Alpha', 'Confirmation must not clear fielded identities optimistically');
        await page.evaluate(() => {
            const command = resetFixture.commands.filter(command => command.action === 'cmd_identity_reset_local').at(-1);
            resetFixture.emit({ action: 'alias_sync_result', message: 'Reset saved; backup: local.db' });
            resetFixture.emit({ action: 'identity_result', requestId: command.requestId, ok: true });
        });
        assert.equal(await page.evaluate(() => Object.keys(savedDB).length), 4, 'Success notice must not optimistically delete');
        assert.equal(await page.locator('.name-input').first().inputValue(), 'Alpha', 'Acknowledgement alone must not clear fielded identities');
        assert.equal(await page.locator('.stat-kill').first().inputValue(), '1', 'Acknowledgement alone must not clear slot stats');
        assert.equal(await page.evaluate(() => isMonitoring), true, 'Monitoring follows native state');
        await page.evaluate(() => {
            const state = resetFixture.state;
            state.playerLibrary.revision = 3;
            state.playerIdentity = { revision: 3, groups: [], entries: [], exactMatches: [], overlapSuggestions: [] };
            state.fullAliasDB = {};
            state.players = state.players.map(player => ({ ...player, name: '', aliases: [], kills: 0, deaths: 0, akCount: 0 }));
            state.isMonitoring = false;
            state.aliasDbAutoSync.enabled = false;
            resetFixture.sync();
        });
        await page.waitForFunction(() => !identityCommandPending && identityMembers.length === 0);
        assert.equal(await page.evaluate(() => Object.keys(savedDB).length), 0);
        assert.deepEqual(await page.evaluate(() => playerDB), {});
        assert.deepEqual(await page.locator('.name-input').evaluateAll(nodes => nodes.map(node => node.value)), Array(8).fill(''));
        assert.deepEqual(await page.locator('.player-row').evaluateAll(rows => rows.map(row =>
            ['.stat-kill', '.stat-death', '.stat-ak'].map(selector => row.querySelector(selector).value))), Array.from({ length: 8 }, () => ['0', '0', '-']));
        assert.equal(await page.evaluate(() => isMonitoring), false);
        assert.deepEqual(await page.evaluate(() => ({
            scores: [document.querySelector('#team-blue .team-score-input').value, document.querySelector('#team-red .team-score-input').value],
            history: recentEvents, cloud: resetFixture.state.cloudMatch
        })), preserved);
        const status = await page.locator('#identity-library-status').textContent();
        for (const phrase of ['\u5df2\u5220\u9664\u5168\u90e8\u672c\u5730\u9009\u624b', '\u5df2\u505c\u6b62\u76d1\u63a7', '\u573a\u4e0a8\u4e2a\u4f4d\u7f6e', '\u540d\u79f0\u3001ID\u548c\u4e2a\u4eba\u6218\u7ee9', '\u961f\u4f0d\u603b\u5206', '\u6700\u8fd1\u6218\u7ee9\u8bb0\u5f55', '\u4e91\u7aef\u5e93\u4fdd\u6301\u4e0d\u53d8']) assert.ok(status.includes(phrase), `Missing success scope: ${phrase}`);
        assert.equal(await page.locator('#alias-auto-sync-enabled').isChecked(), false);
        assert.equal(await page.locator('#modal-msg').textContent(), 'Reset saved; backup: local.db');
        await page.locator('#modal-ok').click();
        await page.locator('#btn-more-controls').click();
        await page.locator('#btn-identity-groups').click();
        for (const viewport of [{ width: 1280, height: 900 }, { width: 900, height: 600 }, { width: 390, height: 844 }]) {
            await page.setViewportSize(viewport);
            await assertContained(page.locator('#identity-library-status'), viewport);
            await page.screenshot({ path: path.join(screenshots, `success-${viewport.width}.png`) });
        }
        assert.deepEqual(errors, []);
        console.log(`Local-library reset browser checks passed (3 viewports, guards, cancel, stale error, canonical success). Screenshots: ${screenshots}`);
    } finally {
        if (browser) await browser.close();
        const closed = once(fixture.server, 'exit');
        fixture.server.kill();
        await closed;
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
