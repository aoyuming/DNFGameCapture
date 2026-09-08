const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { once } = require('node:events');
const { mkdirSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');
const { chromium } = require('playwright');

const screenshots = process.env.SCREENSHOT_DIR || path.join(tmpdir(), 'dnf-player-detail-controls');
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

async function checkLayout(page, width, touch = false) {
    await page.setViewportSize({ width, height: 1000 });
    await page.locator('.identity-detail-header').scrollIntoViewIfNeeded();
    const layout = await page.locator('#identity-detail-pane').evaluate(detail => {
        const box = element => {
            const { x, y, width, height, right, bottom } = element.getBoundingClientRect();
            return { x, y, width, height, right, bottom };
        };
        const actions = detail.querySelector('.identity-detail-actions');
        return {
            children: [...detail.children].map(node => node.className),
            header: box(detail.querySelector('.identity-detail-header')),
            actions: box(actions), names: box(detail.querySelector('.identity-related-names')),
            buttons: [...actions.querySelectorAll('button')].map(box),
            chips: [...detail.querySelectorAll('.identity-editable-chip')].map(chip => ({
                chip: box(chip), label: box(chip.querySelector('.identity-chip-label')),
                remove: box(chip.querySelector('.identity-chip-delete'))
            })),
            paneWidth: detail.parentElement.clientWidth, paneScrollWidth: detail.parentElement.scrollWidth
        };
    });
    assert.deepEqual(layout.children.slice(0, 3), ['identity-detail-header', 'identity-detail-actions', 'identity-detail-section']);
    assert.ok(layout.actions.y >= layout.header.bottom, 'Toolbar must follow heading');
    assert.ok(layout.names.y >= layout.actions.bottom, 'Toolbar must precede associated names');
    assert.ok(layout.paneScrollWidth <= layout.paneWidth + 1, `Detail pane overflow at ${width}px`);
    for (const button of layout.buttons) {
        assert.ok(button.x >= layout.actions.x - 1 && button.right <= layout.actions.right + 1);
        assert.ok(button.y >= layout.actions.y - 1 && button.bottom <= layout.actions.bottom + 1);
    }
    assert.equal(await page.locator('.identity-detail-actions').evaluate(node => getComputedStyle(node).flexWrap), 'wrap');
    for (const { chip, label, remove } of layout.chips) {
        assert.ok(label.right <= remove.x, 'ID label must not overlap the delete region');
        assert.ok(remove.right <= chip.right && remove.bottom <= chip.bottom);
    }
    await page.screenshot({ path: path.join(screenshots, `${touch ? 'touch' : 'desktop'}-${width}.png`) });
    await page.locator('.identity-game-id-add').scrollIntoViewIfNeeded();
    await page.screenshot({ path: path.join(screenshots, `${touch ? 'touch' : 'desktop'}-${width}-chips.png`) });
}

(async () => {
    const fixture = await startFixture();
    let browser;
    try {
        browser = await chromium.launch({ headless: true, channel: process.env.BROWSER_CHANNEL || 'msedge' });
        const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
        page.setDefaultTimeout(10000);
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.goto(fixture.url);
        await page.waitForFunction(() => typeof canEditPlayerLibrary === 'function' && canEditPlayerLibrary());
        const initial = await page.evaluate(() => {
            window.identityTestCommands = [];
            const postMessage = window.chrome.webview.postMessage;
            window.chrome.webview.postMessage = command => {
                if (command.action.startsWith('cmd_identity_')) window.identityTestCommands.push(command);
                postMessage(command);
            };
            const group = playerIdentityState.groups[0];
            openPlayerIdentityPanel(group.names[0]);
            return { group, solo: playerIdentityState.entries.find(entry => !identityGroupsByName.has(entry.name)) };
        });
        const toolbar = page.locator('#identity-detail-pane > .identity-detail-actions');
        assert.equal(await toolbar.count(), 1);
        assert.equal(await toolbar.getAttribute('role'), 'group');
        assert.equal(await toolbar.locator('button').count(), 2);
        assert.equal(await toolbar.locator('.identity-edit-ids').count(), 0);
        await toolbar.locator('.identity-add-alias').click();
        assert.equal(await page.locator('#modal-input').isVisible(), true);
        await page.locator('#modal-cancel').click();
        await toolbar.locator('.identity-unmerge').click();
        assert.equal(await page.locator('#identity-split-owner').isVisible(), true);
        await page.locator('#modal-cancel').click();

        const chips = page.locator('.identity-game-id-chip');
        const edit = chips.first().locator('.identity-game-id-edit');
        const remove = chips.first().locator('.identity-game-id-delete');
        const commandCount = () => page.evaluate(() => window.identityTestCommands.length);
        const lastCommand = () => page.evaluate(() => window.identityTestCommands.at(-1));
        const settle = () => page.waitForFunction(() => !identityCommandPending);
        await edit.focus();
        assert.equal(await remove.evaluate(node => getComputedStyle(node).opacity), '1');
        assert.equal(await chips.first().locator('.identity-chip-hint').evaluate(node => getComputedStyle(node).opacity), '1');
        await page.screenshot({ path: path.join(screenshots, 'game-id-edit-hover.png') });
        await page.keyboard.press('Enter');
        assert.equal(await page.locator('#modal-input').inputValue(), initial.group.ids[0]);
        const beforeEditCancel = await commandCount();
        await page.locator('#modal-cancel').click();
        assert.equal(await commandCount(), beforeEditCancel, 'Cancelling edit must not save');
        await edit.click();
        await page.locator('#modal-input').fill(initial.group.ids[1]);
        await page.locator('#modal-ok').click();
        assert.match(await page.locator('.identity-rename-warning').textContent(), /\u8be5\u6e38\u620fID\u5df2\u5b58\u5728/);
        assert.equal(await page.locator('#modal-input').inputValue(), initial.group.ids[1]);
        assert.equal(await commandCount(), beforeEditCancel, 'Game-ID collision must not save');
        await page.locator('#modal-cancel').click();
        assert.equal(await commandCount(), beforeEditCancel);
        await edit.click();
        await page.locator('#modal-input').fill('renamed#profession');
        await page.locator('#modal-ok').click();
        await settle();
        assert.equal((await lastCommand()).action, 'cmd_identity_rename_id');
        assert.equal((await lastCommand()).name, initial.group.names[0]);
        assert.equal((await lastCommand()).oldId, initial.group.ids[0]);
        assert.equal((await lastCommand()).newId, 'renamed#profession');
        assert.equal(Object.hasOwn(await lastCommand(), 'ids'), false);
        assert.equal((await lastCommand()).revision, 1);
        assert.equal(await chips.first().locator('.identity-chip-label').textContent(), initial.group.ids[0], 'Read-only fixture must not mutate canonical IDs');
        await chips.first().hover();
        await remove.click();
        assert.equal(await page.locator('#modal-input').isVisible(), false, 'Delete must not also open edit');
        const count = await commandCount();
        await page.locator('#modal-cancel').click();
        assert.equal(await commandCount(), count);
        await remove.focus();
        await page.keyboard.press('Space');
        await page.locator('#modal-ok').click();
        await settle();
        assert.deepEqual((await lastCommand()).ids, initial.group.ids.slice(1));
        assert.equal((await lastCommand()).groupId, initial.group.groupId);
        const add = page.locator('.identity-game-id-add');
        assert.equal(await add.evaluate(node => node === node.parentElement.lastElementChild), true);
        await add.click();
        await page.locator('#modal-input').fill('new#profession');
        await page.locator('#modal-ok').click();
        await settle();
        assert.deepEqual((await lastCommand()).ids, [...initial.group.ids, 'new#profession']);
        assert.equal((await lastCommand()).groupId, initial.group.groupId);

        await page.evaluate(name => openPlayerIdentityPanel(name), initial.solo.name);
        assert.equal(await toolbar.locator('button').count(), 1);
        await edit.click();
        await page.locator('#modal-input').fill('solo-renamed#profession');
        await page.locator('#modal-ok').click();
        await settle();
        assert.equal((await lastCommand()).action, 'cmd_identity_rename_id');
        assert.equal((await lastCommand()).name, initial.solo.name);
        assert.equal((await lastCommand()).oldId, initial.solo.ids[0]);
        assert.equal((await lastCommand()).newId, 'solo-renamed#profession');
        const beforeLastIdDelete = await commandCount();
        await remove.focus();
        await page.keyboard.press('Enter');
        await page.locator('#modal-ok').click();
        await settle();
        assert.equal(await commandCount(), beforeLastIdDelete);
        assert.match(await page.locator('#modal-msg').textContent(), /至少保留一个有效游戏ID/);
        await page.locator('#modal-ok').click();

        for (const entity of [initial.group, initial.solo]) {
            const name = entity.names?.[0] || entity.name;
            await page.evaluate(name => openPlayerIdentityPanel(name), name);
            const nameEdit = page.locator(`.identity-name-edit[data-name="${name}"]`);
            await nameEdit.focus();
            await page.keyboard.press('Enter');
            assert.equal(await page.locator('#modal-input').inputValue(), name);
            const beforeName = await commandCount();
            await page.locator('#modal-cancel').click();
            assert.equal(await commandCount(), beforeName);
            await nameEdit.click();
            await page.locator('#modal-input').fill(name);
            await page.locator('#modal-ok').click();
            assert.equal(await commandCount(), beforeName, 'Unchanged rename is a no-op');
            await nameEdit.click();
            await page.locator('#modal-input').fill(entity.groupId ? initial.solo.name : initial.group.names[0]);
            await page.locator('#modal-ok').click();
            assert.equal(await page.locator('.identity-rename-warning').isVisible(), true);
            assert.equal(await page.locator('#modal-input').inputValue(), entity.groupId ? initial.solo.name : initial.group.names[0]);
            assert.equal(await page.locator('.identity-detail-header h3').textContent(), name);
            assert.equal(await commandCount(), beforeName, 'Duplicate rename must not auto-merge');
            await page.locator('#modal-input').fill('New Name');
            await page.locator('#modal-ok').click();
            await settle();
            assert.equal((await lastCommand()).action, 'cmd_identity_rename_name');
            assert.equal((await lastCommand()).name, name);
            assert.equal((await lastCommand()).newName, 'New Name');
            assert.equal(await page.locator('.identity-detail-header h3').textContent(), name, 'Failed rename retains selection');
            const deleteName = page.locator(`.identity-delete-alias[data-name="${name}"]`);
            await deleteName.focus();
            await page.keyboard.press('Enter');
            assert.equal(await page.locator('#modal-input').isVisible(), false);
            assert.match(await page.locator('#modal-msg').textContent(), entity.groupId ? /\u89e3\u9664\u5f52\u5e76/ : /\u5f53\u524d\u6bd4\u8d5b/);
            const beforeDeleteName = await commandCount();
            await page.locator('#modal-cancel').click();
            assert.equal(await commandCount(), beforeDeleteName);
            await deleteName.focus();
            await page.keyboard.press('Enter');
            await page.locator('#modal-ok').click();
            await settle();
            assert.equal((await lastCommand()).action, 'cmd_identity_delete_alias');
            assert.equal((await lastCommand()).groupId, entity.groupId || '');
            assert.equal((await lastCommand()).name, name);
        }

        await page.evaluate(name => openPlayerIdentityPanel(name), initial.group.names[0]);
        await page.locator('.identity-name-select').nth(1).click();
        assert.equal(await page.locator('.identity-detail-header h3').textContent(), initial.group.names[1]);
        assert.equal(await page.locator('.identity-name-select').nth(1).getAttribute('aria-pressed'), 'true');
        await page.locator('.identity-name-select').first().click();
        await page.evaluate(() => { playerLibraryState.busy = true; updatePlayerLibraryControls(); });
        const disabledCount = await commandCount();
        for (const button of [edit, remove, add, toolbar.locator('.identity-add-alias'), page.locator('.identity-name-edit').first(), page.locator('.identity-delete-alias').first()]) {
            assert.equal(await button.getAttribute('aria-disabled'), 'true');
            await button.focus();
            await page.keyboard.press('Enter');
            assert.equal(await page.locator('#custom-modal').evaluate(node => node.classList.contains('active')), false);
        }
        assert.equal(await commandCount(), disabledCount);
        await page.evaluate(() => { playerLibraryState.busy = false; updatePlayerLibraryControls(); });
        await toolbar.locator('.identity-add-alias').focus();
        assert.equal(await toolbar.locator('.identity-add-alias').evaluate(node => getComputedStyle(node).boxShadow), 'none');
        assert.notEqual(await toolbar.locator('.identity-add-alias').evaluate(node => getComputedStyle(node).outlineStyle), 'none');
        for (const width of [1440, 900, 390, 320]) await checkLayout(page, width);

        await page.setViewportSize({ width: 1440, height: 1000 });
        await page.locator('.identity-name-edit').first().focus();
        await page.screenshot({ path: path.join(screenshots, 'name-edit-focus.png') });
        await page.evaluate(() => {
            const postMessage = window.chrome.webview.postMessage;
            window.chrome.webview.postMessage = command => {
                if (command.action === 'cmd_identity_rename_name') window.identityHeldRename = command;
                else postMessage(command);
            };
        });
        await page.locator('.identity-name-edit').first().click();
        await page.locator('#modal-input').fill('Committed Name');
        await page.locator('#modal-ok').click();
        assert.equal(await page.locator('.identity-detail-header h3').textContent(), initial.group.names[0]);
        await page.evaluate(() => handlePlayerLibraryResult({ requestId: window.identityHeldRename.requestId, ok: true }));
        assert.equal(await page.locator('.identity-detail-header h3').textContent(), initial.group.names[0]);
        await page.evaluate(() => {
            const { name, newName } = window.identityHeldRename;
            const state = structuredClone(playerIdentityState);
            ++state.revision;
            state.groups[0].names = state.groups[0].names.map(member => member === name ? newName : member);
            state.entries.find(entry => entry.name === name).name = newName;
            receivePlayerLibraryState({ playerLibrary: { ...playerLibraryState, revision: state.revision }, playerIdentity: state });
        });
        await settle();
        assert.equal(await page.locator('.identity-detail-header h3').textContent(), 'Committed Name');
        assert.equal(await page.locator('.identity-name-select').first().getAttribute('aria-pressed'), 'true');
        assert.equal(await page.evaluate(() => identitySelectedNames.has('Committed Name')), true);

        const touch = await browser.newPage({ viewport: { width: 390, height: 1000 }, hasTouch: true, isMobile: true });
        touch.on('pageerror', error => errors.push(error.message));
        await touch.goto(fixture.url);
        await touch.waitForFunction(() => typeof canEditPlayerLibrary === 'function' && canEditPlayerLibrary());
        await touch.evaluate(() => openPlayerIdentityPanel(playerIdentityState.groups[0].names[0]));
        for (const selector of ['.identity-game-id-chip', '.identity-name-chip']) {
            const chip = touch.locator(selector).first();
            const touchRemove = chip.locator('.identity-chip-delete');
            assert.equal(await touchRemove.evaluate(node => getComputedStyle(node).opacity), '1');
            await touchRemove.focus();
            assert.equal(await chip.locator('.identity-chip-label').evaluate(node => getComputedStyle(node).opacity), '1', 'Touch focus must leave the raw value visible');
            await touchRemove.tap();
            assert.equal(await touch.locator('#modal-input').isVisible(), false);
            await touch.locator('#modal-cancel').tap();
        }
        await checkLayout(touch, 390, true);
        assert.deepEqual(errors, []);
        console.log(`Player detail browser checks passed: reduced toolbar, name/game tags, explicit native renames, duplicate/cancel/failure/committed selection, group/solo deletion, keyboard, disabled states, desktop/mobile/touch. Screenshots: ${screenshots}`);
    } finally {
        if (browser) await browser.close();
        const stopped = once(fixture.server, 'exit');
        fixture.server.kill();
        await stopped;
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
