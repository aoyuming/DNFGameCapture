const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { once } = require('node:events');
const { mkdirSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');
const { chromium } = require('playwright');

const output = path.join(tmpdir(), 'dnf-shared-id-suggestions');
async function startFixture() {
    const server = spawn(process.execPath, [path.join(__dirname, 'player-library-preview.cjs')], {
        env: { ...process.env, PORT: '0' }, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe']
    });
    try {
        const url = await new Promise((resolve, reject) => {
            let text = '';
            const timeout = setTimeout(() => reject(new Error('Fixture timeout')), 10000);
            server.once('error', error => { clearTimeout(timeout); reject(error); });
            server.once('exit', code => { clearTimeout(timeout); reject(new Error(`Fixture exited: ${code}`)); });
            server.stdout.on('data', chunk => {
                text += chunk;
                const match = text.match(/http:\/\/127\.0\.0\.1:\d+\//);
                if (match) { clearTimeout(timeout); resolve(match[0]); }
            });
        });
        return { server, url };
    } catch (error) { server.kill(); throw error; }
}

(async () => {
    mkdirSync(output, { recursive: true });
    const fixture = await startFixture();
    let browser;
    try {
        browser = await chromium.launch({ headless: true, channel: 'msedge' });
        const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.goto(fixture.url);
        await page.waitForFunction(() => canEditPlayerLibrary());
        await page.evaluate(() => {
            window.sharedCommands = [];
            window.chrome.webview.postMessage = command => sharedCommands.push(command);
            const names = ['冲搞', '夏霖', '幕子', '白羽', '其他选手'];
            const common = [
                { leftName: names[0], rightName: names[1], commonIdCount: 1, commonGameIds: ['夏日白羽#剑魂'] },
                { leftName: names[0], rightName: names[2], commonIdCount: 1,
                    commonGameIds: ['白羽·青芒'] },
                { leftName: names[0], rightName: names[3], commonIdCount: 1, commonGameIds: ['滚动区域里的游戏ID'] },
                { leftName: names[3], rightName: names[4], commonIdCount: 1, commonGameIds: ['其他ID'] }
            ];
            window.sharedFixture = {
                playerLibrary: { loaded: true, ready: true, busy: false, storage: 'sqlite-v2', revision: 800 },
                playerIdentity: { revision: 800, groups: [], entries: names.map(name => ({ name, ids: ['示例游戏ID'] })),
                    overlapSuggestions: common, exactMatches: [] },
                fullAliasDB: Object.fromEntries(names.map(name => [name, '(示例游戏ID)']))
            };
            receivePlayerLibraryState(sharedFixture);
        });
        await page.waitForFunction(() => canEditPlayerLibrary() && playerIdentityState.revision === 800);
        await page.evaluate(() => openPlayerIdentityPanel('冲搞'));
        const list = page.locator('.identity-suggestion-list');
        const all = page.locator('.identity-suggestion-ignore-all');
        await page.getByText('夏日白羽#剑魂', { exact: false }).last().waitFor();
        assert.doesNotMatch(await list.textContent(), /冒险团/);
        assert.equal(await all.count(), 1);
        for (const viewport of [{ width: 1280, height: 900 }, { width: 900, height: 760 }, { width: 390, height: 844 }]) {
            await page.setViewportSize(viewport);
            await list.scrollIntoViewIfNeeded();
            await page.waitForFunction(() => {
                const list = document.querySelector('.identity-suggestion-list');
                const rows = list.querySelectorAll('.identity-suggestion-row');
                const gap = parseFloat(getComputedStyle(list).rowGap) || 0;
                return Math.abs(list.clientHeight - (rows[0].offsetHeight * 2 + gap)) < 3;
            });
            const layout = await list.evaluate(node => ({ height: node.clientHeight, scroll: node.scrollHeight,
                width: node.clientWidth, scrollWidth: node.scrollWidth,
                contained: [...node.querySelectorAll('.identity-suggestion-content, .identity-suggestion-actions')].every(child => {
                    const c = child.getBoundingClientRect(), p = child.parentElement.getBoundingClientRect();
                    return c.left >= p.left && c.right <= p.right + 1;
                }) }));
            assert.ok(layout.scroll > layout.height);
            await page.screenshot({ path: path.join(output, `shared-${viewport.width}.png`) });
            assert.ok(layout.scrollWidth <= layout.width + 1, `${viewport.width}: ${JSON.stringify(layout)}`);
            assert.ok(layout.contained);
            await list.evaluate(node => { node.scrollTop = 0; });
            await page.screenshot({ path: path.join(output, `shared-${viewport.width}.png`) });
            await list.hover();
            await page.mouse.wheel(0, 200);
            await page.waitForFunction(() => document.querySelector('.identity-suggestion-list').scrollTop > 0);
            await all.click();
            assert.match(await page.locator('#modal-msg').textContent(), /全部 4 条/);
            await page.locator('#modal-cancel').click();
            assert.equal(await page.evaluate(() => sharedCommands.filter(c => c.action === 'cmd_identity_ignore_overlaps').length), 0);
        }
        await page.setViewportSize({ width: 1280, height: 900 });
        await page.locator('#identity-search-input').fill('冲搞');
        assert.equal(await list.locator('.identity-suggestion-row').count(), 3);
        await all.click();
        assert.match(await page.locator('#modal-msg').textContent(), /全部 3 条/);
        await page.locator('#modal-ok').click();
        const command = await page.evaluate(() => sharedCommands.find(c => c.action === 'cmd_identity_ignore_overlaps'));
        assert.equal(command.revision, 800);
        assert.deepEqual(command.pairs, [
            { leftName: '冲搞', rightName: '夏霖' },
            { leftName: '冲搞', rightName: '幕子' },
            { leftName: '冲搞', rightName: '白羽' }
        ]);
        assert.equal(await list.locator('.identity-suggestion-row').count(), 3, 'No optimistic hiding');
        assert.equal(await all.getAttribute('aria-disabled'), 'true');
        await page.evaluate(command => {
            handlePlayerLibraryResult({ requestId: command.requestId, ok: false, error: '保存失败，原提示保留' });
        }, command);
        assert.equal(await list.locator('.identity-suggestion-row').count(), 3);
        await all.click(); await page.locator('#modal-ok').click();
        await page.evaluate(() => {
            const command = sharedCommands.filter(c => c.action === 'cmd_identity_ignore_overlaps').at(-1);
            handlePlayerLibraryResult({ requestId: command.requestId, ok: true });
            sharedFixture.playerLibrary.revision++;
            sharedFixture.playerIdentity.revision++;
            sharedFixture.playerIdentity.overlapSuggestions = sharedFixture.playerIdentity.overlapSuggestions.filter(row =>
                !command.pairs.some(pair => pair.leftName === row.leftName && pair.rightName === row.rightName));
            receivePlayerLibraryState(sharedFixture);
        });
        await page.waitForFunction(() => !identityCommandPending && canEditPlayerLibrary());
        assert.equal(await page.locator('#identity-suggestions').isVisible(), false);
        assert.equal(await page.evaluate(() => playerIdentityState.entries.length), 5);
        assert.equal(await page.evaluate(() => playerIdentityState.overlapSuggestions.length), 1);
        await page.locator('#identity-search-input').fill('');
        await page.setViewportSize({ width: 390, height: 844 });
        await page.evaluate(() => {
            sharedFixture.playerLibrary.revision++;
            sharedFixture.playerIdentity.revision++;
            const longNames = ['甲', '乙', '丙'].map(character => character.repeat(256));
            sharedFixture.playerIdentity.overlapSuggestions = longNames.map((name, index) => ({
                leftName: name, rightName: longNames[(index + 1) % longNames.length],
                commonIdCount: 2,
                commonGameIds: ['游戏'.repeat(128), '第二个游戏ID']
            }));
            receivePlayerLibraryState(sharedFixture);
        });
        await page.waitForFunction(() => canEditPlayerLibrary() && playerIdentityState.revision === 802);
        await page.waitForFunction(() => {
            const list = document.querySelector('.identity-suggestion-list');
            const row = list.querySelector('.identity-suggestion-row');
            const gap = parseFloat(getComputedStyle(list).rowGap) || 0;
            return Math.abs(list.clientHeight - (row.offsetHeight * 2 + gap)) < 3;
        });
        const bounded = await list.evaluate(node => {
            const bounds = node.getBoundingClientRect();
            return bounds.top >= 0 && bounds.bottom <= innerHeight && node.scrollWidth <= node.clientWidth + 1 &&
                [...node.querySelectorAll('.identity-suggestion-row')].slice(0, 2).every(row => {
                    const actions = row.querySelector('.identity-suggestion-actions').getBoundingClientRect();
                    return actions.top >= bounds.top && actions.bottom <= bounds.bottom + 1;
                });
        });
        assert.ok(bounded, 'Long names and IDs must not push the two rows or actions out of view');
        assert.equal(await page.locator('.identity-shared-names').first().getAttribute('title'), `${'甲'.repeat(256)} + ${'乙'.repeat(256)}`);
        await page.screenshot({ path: path.join(output, 'shared-long-390.png') });
        assert.deepEqual(errors, []);
        console.log(`Shared-ID UI passed: details, 2-row scrolling, 3 viewports, filtered bulk ignore, cancel, busy, failure and canonical success. Screenshots: ${output}`);
    } finally {
        if (browser) await browser.close();
        if (fixture.server.exitCode === null) { fixture.server.kill(); await once(fixture.server, 'exit'); }
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
