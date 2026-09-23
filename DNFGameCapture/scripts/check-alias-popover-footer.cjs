const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { once } = require('node:events');
const { mkdirSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');
const { chromium } = require('playwright');

const screenshots = process.env.SCREENSHOT_DIR || path.join(tmpdir(), 'dnf-alias-popover-footer');
mkdirSync(screenshots, { recursive: true });

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

async function openMenu(page, rowIndex = 4) {
    await page.evaluate(rowIndex => {
        const row = document.querySelectorAll('.player-row')[rowIndex];
        const input = row.querySelector('.name-input');
        const name = playerIdentityState.groups[0].names[0];
        input.value = name;
        playerDB[name] = Array.from({ length: 36 }, (_, i) => '\u6e38'.repeat(10) + String.fromCharCode(0x4e00 + i));
        openAliasPopover(input, name);
    }, rowIndex);
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    return page.locator('.alias-popover.active');
}

async function layout(page) {
    return page.locator('.alias-popover.active').evaluate(popover => {
        const box = element => {
            const { left, top, right, bottom, width, height } = element.getBoundingClientRect();
            return { left, top, right, bottom, width, height };
        };
        const list = popover.querySelector('.alias-id-list');
        const footer = popover.querySelector('.alias-add-actions');
        const names = popover.querySelector('.alias-name-list');
        return {
            names: box(names), namesScrollable: names.scrollHeight > names.clientHeight,
            popover: box(popover), list: box(list), footer: box(footer),
            buttons: [...popover.querySelectorAll('.alias-add-actions button, .alias-name-actions button')].map(button => ({
                ...box(button), fits: button.scrollWidth <= button.clientWidth + 1,
                hit: button.contains(document.elementFromPoint(
                    (button.getBoundingClientRect().left + button.getBoundingClientRect().right) / 2,
                    (button.getBoundingClientRect().top + button.getBoundingClientRect().bottom) / 2))
            })),
            scrollable: list.scrollHeight > list.clientHeight,
            scrollTop: list.scrollTop, viewport: { width: innerWidth, height: innerHeight }
        };
    });
}

(async () => {
    const fixture = await startFixture();
    let browser;
    try {
        browser = await chromium.launch({ headless: true, channel: process.env.BROWSER_CHANNEL || 'msedge' });
        const page = await browser.newPage({ viewport: { width: 1140, height: 580 } });
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.goto(fixture.url);
        await page.waitForTimeout(650); // Allow both initial fixture state broadcasts to finish.
        await page.waitForFunction(() => canEditPlayerLibrary());
        let menu = await openMenu(page);
        assert.equal(await menu.locator('.alias-name-item').count(), 2);
        assert.equal(await menu.locator('.add-name-alias-btn').count(), 1);
        assert.equal(await menu.locator('.edit-name-alias-btn').count(), 2);
        assert.equal(await menu.locator('.delete-name-alias-btn').count(), 2);
        assert.notEqual(await menu.locator('.alias-name-item .alias-name').first().evaluate(node => getComputedStyle(node).color),
            await menu.locator('.alias-id-list .alias-name').first().evaluate(node => getComputedStyle(node).color));
        const compact = await menu.evaluate(popover => ({
            width: popover.offsetWidth,
            idWidth: popover.querySelector('.alias-id-column').offsetWidth,
            nameWidth: popover.querySelector('.alias-name-column').offsetWidth,
        }));
        assert.ok(compact.width <= 406, `Compact popover: ${JSON.stringify(compact)}`);
        assert.ok(compact.idWidth <= 244 && compact.nameWidth <= 160,
            `Columns fit 11-character IDs and 6-character aliases: ${JSON.stringify(compact)}`);
        await page.evaluate(() => {
            window.originalAliasNames = [...playerIdentityState.groups[0].names];
            playerIdentityState.groups[0].names.push(...Array.from({ length: 24 }, (_, i) => '\u540d'.repeat(5) + String.fromCharCode(0x4e00 + i)));
            window.popoverCommands = [];
            window.chrome.webview.postMessage = command => window.popoverCommands.push(command);
        });
        assert.equal(await menu.locator('.alias-add-actions').count(), 1, 'Add actions need a fixed footer outside the scrolling IDs');
        assert.equal(await menu.locator('.alias-add-actions button').count(), 1);
        assert.equal(await menu.locator('.add-alias-btn').textContent(), '+ \u6dfb\u52a0\u6e38\u620fID');
        for (const [width, height] of [[1140, 580], [1440, 900], [820, 420], [390, 740]]) {
            await page.setViewportSize({ width, height });
            await page.waitForTimeout(150);
            for (const row of [0, 4, 7]) {
                menu = await openMenu(page, row);
                await menu.locator('.alias-id-list').evaluate(list => { list.scrollTop = 0; });
                const before = await layout(page);
                assert.ok(before.scrollable);
                assert.ok(before.namesScrollable, 'Many name aliases must scroll');
                assert.ok(before.names.left >= before.list.right, 'Name aliases belong to the right of game IDs');
                assert.ok(Math.abs(before.names.top - before.list.top) < 1, 'Both lists start at the same height');
                assert.ok(before.list.bottom <= before.footer.top + 1, 'IDs must not overlap footer');
                assert.ok(before.popover.left >= 7 && before.popover.right <= width - 7, `Horizontal fit: ${JSON.stringify(before)}`);
                assert.ok(before.popover.top >= 7 && before.popover.bottom <= height - 7, `Vertical fit: ${JSON.stringify(before)}`);
                for (const button of before.buttons) {
                    assert.ok(button.fits && button.hit, `Footer button must fit and be clickable: ${JSON.stringify(before)}`);
                }
                if (width >= 820) {
                    const textFits = await menu.locator('.alias-name').evaluateAll(items => items.every(item =>
                        item.scrollWidth <= item.clientWidth + 1 && getComputedStyle(item).fontSize === '12px'));
                    assert.ok(textFits, '11-character game IDs and 6-character aliases fit at normal font size');
                }
                const list = menu.locator('.alias-id-list');
                await list.hover();
                await page.mouse.wheel(0, 10000);
                await page.waitForFunction(() => document.querySelector('.alias-popover.active .alias-id-list').scrollTop > 0);
                const after = await layout(page);
                assert.ok(Math.abs(before.footer.top - after.footer.top) < 1, 'Footer must stay fixed on wheel scroll');
                if (row === 4) await page.screenshot({ path: path.join(screenshots, `${width}-${height}.png`) });
            }
        }

        await page.setViewportSize({ width: 1140, height: 580 });
        for (const scale of [0.8, 1.25]) {
            await page.evaluate(scale => { document.querySelector('.app-shell').style.transform = `scale(${scale})`; }, scale);
            menu = await openMenu(page, 7);
            const bounds = await layout(page);
            assert.ok(bounds.popover.left >= 7 && bounds.popover.right <= 1133);
            assert.ok(bounds.popover.top >= 7 && bounds.popover.bottom <= 573);
            await page.screenshot({ path: path.join(screenshots, `scaled-${scale}.png`) });
            assert.ok(bounds.buttons.every(button => button.fits && button.hit), `Scale ${scale}: ${JSON.stringify(bounds)}`);
        }
        await page.evaluate(() => { document.querySelector('.app-shell').style.transform = ''; });
        await page.evaluate(() => {
            playerIdentityState.groups[0].names = window.originalAliasNames;
            document.querySelectorAll('.name-input').forEach(input => { input.value = ''; });
        });
        menu = await openMenu(page, 0);
        const input = page.locator('.name-input').first();
        const selected = await menu.locator('.alias-name-item').first().getAttribute('data-name');
        await menu.locator('.alias-name-item').first().click();
        assert.equal(await input.inputValue(), selected, 'Mouse selection replaces the current name');
        await input.focus();
        await input.press('ArrowDown');
        const highlighted = await menu.locator('.alias-name-item.keyboard-focus').getAttribute('data-name');
        await page.evaluate(() => restorePendingAliasPopover());
        assert.equal(await menu.locator('.alias-name-item.keyboard-focus').getAttribute('data-name'), highlighted,
            'Scoreboard refresh must preserve the keyboard selection');
        await input.press('Enter');
        assert.equal(await input.inputValue(), highlighted, 'Arrow keys and Enter select a name alias');
        await menu.locator('.alias-name-item').first().focus();
        await page.keyboard.press('Enter');
        assert.equal(await input.inputValue(), selected, 'Focused name aliases support Enter');
        const finishFailedCommand = async action => {
            const command = await page.evaluate(action => popoverCommands.filter(c => c.action === action).at(-1), action);
            assert.ok(command, `Expected ${action}`);
            await page.evaluate(requestId => handlePlayerLibraryResult({ requestId, ok: false, message: 'Fixture failure' }), command.requestId);
            return command;
        };
        await menu.locator('.add-name-alias-btn').click();
        await page.locator('#modal-input').fill('NewNameAlias');
        await page.locator('#modal-ok').click();
        assert.equal((await finishFailedCommand('cmd_identity_add_alias')).newName, 'NewNameAlias');
        const editName = await menu.locator('.alias-name-item').first().getAttribute('data-name');
        await menu.locator('.edit-name-alias-btn').first().click();
        await page.locator('#modal-input').fill('RenamedAlias');
        await page.locator('#modal-ok').click();
        const renamed = await finishFailedCommand('cmd_identity_rename_name');
        assert.equal(renamed.name, editName);
        assert.equal(renamed.newName, 'RenamedAlias');
        await menu.locator('.delete-name-alias-btn').first().click();
        await page.locator('#modal-ok').click();
        assert.equal((await finishFailedCommand('cmd_identity_delete_alias')).name, editName);
        await page.evaluate(() => closeAliasPopovers());
        assert.equal(await page.evaluate(() => popoverCommands.some(c => c.action === 'cmd_set_alias_popover_expanded')), false,
            'Opening, selecting, editing, and closing aliases must not resize the host window');
        menu = await openMenu(page);
        await page.evaluate(() => {
            window.popoverCommands = [];
            window.chrome.webview.postMessage = command => window.popoverCommands.push(command);
        });
        await menu.locator('.add-alias-btn').click();
        assert.equal(await page.locator('#modal-input').isVisible(), true);
        await page.locator('#modal-cancel').click();
        assert.equal(await page.evaluate(() => popoverCommands.some(command => command.action.startsWith('cmd_identity_'))), false);
        const ids = await page.evaluate(() => playerIdentityState.groups[0].ids);
        await menu.locator('.add-alias-btn').focus();
        await page.keyboard.press('Enter');
        await page.locator('#modal-input').fill('NewGame#Profession');
        await page.locator('#modal-ok').click();
        const command = await page.evaluate(() => popoverCommands.find(command => command.action === 'cmd_identity_update_ids'));
        assert.equal(command.groupId, 'fixture-group');
        assert.deepEqual(command.ids, [...ids, 'NewGame#Profession']);
        assert.equal(Object.hasOwn(command, 'adventureGroupIds'), false);
        await page.evaluate(requestId => handlePlayerLibraryResult({ requestId, ok: false, message: 'Fixture failure' }), command.requestId);
        assert.equal(await menu.locator('.alias-row').count(), 36, 'Failed save must not alter the displayed game IDs');

        for (const busy of ['busy', 'loading']) {
            await page.evaluate(busy => {
                playerLibraryState.busy = busy === 'busy';
                playerLibraryState.ready = busy !== 'loading';
                updatePlayerLibraryControls();
            }, busy);
            const button = menu.locator('.add-alias-btn');
            assert.equal(await button.getAttribute('aria-disabled'), 'true');
            await button.evaluate(node => node.click());
            assert.equal(await page.locator('#custom-modal').evaluate(node => node.classList.contains('active')), false);
        }
        await page.evaluate(() => { playerLibraryState.busy = false; playerLibraryState.ready = true; updatePlayerLibraryControls(); });
        // Simulate the game-ID list from the next native match and library snapshot.
        await page.evaluate(() => {
            const state = structuredClone(playerIdentityState);
            ++state.revision;
            state.groups[0].ids.push('CommittedGame#Profession');
            playerDB[state.groups[0].names[0]] = state.groups[0].ids;
            receivePlayerLibraryState({ playerLibrary: { ...playerLibraryState, revision: state.revision }, playerIdentity: state });
        });
        await page.waitForFunction(() => canEditPlayerLibrary());
        assert.equal(await menu.locator('.alias-row').count(), ids.length + 1);
        assert.match(await menu.locator('.alias-id-list').textContent(), /CommittedGame#Profession/);

        await page.evaluate(() => {
            const solo = playerIdentityState.entries.find(entry => !identityGroupsByName.has(entry.name));
            solo.ids = [];
            playerDB[solo.name] = [];
            const input = document.querySelectorAll('.name-input')[4];
            input.value = solo.name;
            openAliasPopover(input, solo.name);
        });
        assert.equal(await menu.locator('.alias-row').count(), 0);
        assert.equal(await menu.locator('.alias-add-actions button').count(), 1);
        await menu.locator('.add-alias-btn').click();
        await page.locator('#modal-input').fill('SoloGame#Profession');
        await page.locator('#modal-ok').click();
        const soloCommand = await page.evaluate(() => popoverCommands.filter(command => command.action === 'cmd_identity_update_ids').at(-1));
        assert.equal(soloCommand.groupId, '');
        assert.deepEqual(soloCommand.ids, ['SoloGame#Profession']);
        assert.equal(await menu.locator('.alias-row').count(), 0, 'Adding the first game ID waits for acknowledgement');
        assert.doesNotMatch(await menu.textContent(), /冒险团/);
        assert.deepEqual(errors, []);
        console.log(`Alias popover checks passed: compact columns, 11/6-character text fit, no host resize, name selection by mouse/keyboard, name CRUD commands, scrolling, viewport fit, ID actions, busy/cancel/failure/refresh. Screenshots: ${screenshots}`);
    } finally {
        if (browser) await browser.close();
        const stopped = once(fixture.server, 'exit');
        fixture.server.kill();
        await stopped;
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
