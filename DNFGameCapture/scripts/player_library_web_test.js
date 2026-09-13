const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

const root = path.resolve(__dirname, '..');
const main = fs.readFileSync(path.join(root, 'web\u524d\u7aef', 'main.js'), 'utf8');
const workerSource = fs.readFileSync(path.join(root, 'web\u524d\u7aef', 'autocomplete-worker.js'), 'utf8');

function functionSource(name) {
    const start = main.indexOf(`function ${name}(`);
    assert.notEqual(start, -1, `Missing function ${name}`);
    const end = main.indexOf('\n}', start);
    return main.slice(start, end + 2);
}

function harness() {
    const messages = [];
    const timers = [];
    const nodes = new Map();
    const alerts = [];
    let elapsed = 0;
    const context = vm.createContext({
        console, performance: { now: () => ++elapsed }, messages,
        setTimeout: callback => { timers.push(callback); return timers.length; },
        clearTimeout() {},
        requestAnimationFrame: callback => timers.push(callback),
        window: { chrome: { webview: { postMessage: value => messages.push(value) } } },
        document: {
            getElementById: id => nodes.get(id) || null,
            querySelector: () => null,
            querySelectorAll: () => [],
            activeElement: null
        },
        escapeHtml: text => String(text).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('"', '&quot;'),
        showAlert: value => alerts.push(value),
        scheduleLayoutFit() {}
    });
    vm.runInContext(main.slice(0, main.indexOf("applyWebTheme('dark-esports');")), context);
    for (const name of ['normalizeAliasTextForCompare', 'getAliasDuplicateId', 'sameAliasId',
        'getAliasJobKey', 'aliasHasDeclaredJob', 'sameAliasStorageEntry', 'mergeAliasIntoArray',
        'uniqueAliasArray', 'removeAliasFromArray', 'getAliasesForPlayerName',
        'formatAliasArrayForCpp', 'buildFormattedAliasDB', 'pushStateToServer', 'rememberIdentityEditorDraft',
        'getCleanAliases', 'bindAliasToPlayer', 'updateAliasForPlayer']) {
        vm.runInContext(functionSource(name), context);
    }
    const identityStart = main.indexOf('function normalizePlayerIdentityState(');
    const identityEnd = main.indexOf("document.getElementById('identity-member-list')?.addEventListener", identityStart);
    vm.runInContext(main.slice(identityStart, identityEnd), context);
    return {
        context, messages, nodes, alerts,
        run: source => vm.runInContext(source, context),
        step: () => timers.shift()?.(),
        flush() {
            let count = 0;
            while (timers.length) {
                assert.ok(++count < 10000, 'Timers must terminate');
                timers.shift()();
            }
        }
    };
}

function element() {
    return {
        writes: 0, _html: '', textContent: '', dataset: {}, listeners: {}, attributes: {},
        get innerHTML() { return this._html; },
        set innerHTML(value) { ++this.writes; this._html = value; },
        setAttribute(key, value) { this.attributes[key] = value; },
        getAttribute(key) { return this.attributes[key]; },
        classList: { contains: () => true },
        querySelector: () => null,
        querySelectorAll: () => [],
        addEventListener(type, listener) { this.listeners[type] = listener; }
    };
}

function snapshot(revision = 1) {
    return {
        playerLibrary: { loaded: true, ready: true, busy: false, storage: 'sqlite-v2', revision, error: '' },
        playerIdentity: {
            revision,
            groups: [{ groupId: 'g1', source: 'manual', names: ['Beta', 'Alpha'], ids: ['game#job'] }],
            entries: [{ name: 'Beta', ids: ['game#job'] }, { name: 'Alpha', ids: ['game#job'] }, { name: 'Solo', ids: ['solo'] }, { name: 'NameOnly', ids: [] }],
            exactMatches: [], overlapSuggestions: []
        },
        fullAliasDB: { Beta: '(game#job)', Alpha: '(game#job)', Solo: '(solo)', NameOnly: '' }
    };
}

// Read-only fixture for browser screenshots; no native host or HTTP server required.
if (process.argv.includes('--fixture')) {
    process.stdout.write(JSON.stringify(snapshot(), null, 2) + '\n');
    process.exit(0);
}

test('legacy identity fields are discarded without losing names or inventing game IDs', () => {
    const h = harness();
    h.context.state = snapshot();
    h.context.state.playerIdentity.groups[0].adventureGroupIds = ['legacy-group'];
    h.context.state.playerIdentity.entries.find(entry => entry.name === 'NameOnly').adventureGroupIds = ['legacy-only'];
    h.context.state.playerIdentity.groups.push({ groupId: 'empty-group', names: ['RetainedAlias'], ids: [], adventureGroupIds: ['legacy-group'] });
    h.context.state.playerIdentity.exactMatches = [{ groupId: 'g1', names: ['Alpha', 'Beta'], adventureGroupIds: ['legacy-group'] }];
    h.context.state.playerIdentity.overlapSuggestions = [
        { leftName: 'Alpha', rightName: 'Solo', commonIdCount: 1, commonGameIds: ['game#job'], commonAdventureIds: ['legacy-group'] },
        { leftName: 'Solo', rightName: 'NameOnly', commonIdCount: 0, commonAdventureIdCount: 1 },
        { leftName: '', rightName: 'Solo', commonIdCount: 1 }
    ];
    h.run('receivePlayerLibraryState(state)'); h.flush();
    assert.doesNotMatch(h.run('JSON.stringify(playerIdentityState)'), /adventure/i);
    assert.doesNotMatch(h.run('JSON.stringify(identityMembers)'), /adventureGroupIds/);
    assert.equal(h.run('findIdentityEntryByName("NameOnly").name'), 'NameOnly');
    assert.deepEqual(Array.from(h.run('identityIdsForName("NameOnly")')), []);
    assert.deepEqual(Array.from(h.run('savedDB.NameOnly')), []);
    assert.equal(h.run('getIdentityGroupForName("RetainedAlias").names[0]'), 'RetainedAlias');
    assert.deepEqual(Array.from(h.run('identityIdsForName("RetainedAlias")')), []);
    assert.equal(h.run('playerIdentityState.overlapSuggestions.length'), 1);
    assert.equal(h.run('visibleIdentityOverlaps().length'), 1);
    assert.equal(h.run('playerIdentityState.exactMatches[0].groupId'), 'g1');
});

test('name-only players require a game ID before monitoring', () => {
    const h = harness();
    h.context.state = snapshot();
    h.context.state.playerIdentity.entries.find(entry => entry.name === 'NameOnly').adventureGroupIds = ['legacy-only'];
    h.run('receivePlayerLibraryState(state)'); h.flush();
    h.run(functionSource('getActiveNoAliasViolations'));
    h.run(functionSource('getNoAliasGuardMessage'));
    const input = { value: 'NameOnly', classList: { contains: () => false } };
    h.context.document.querySelectorAll = selector => selector === '.player-row' ? [{ querySelector: () => input }] : [];
    assert.equal(h.run('getActiveNoAliasViolations().length'), 1);
    assert.match(h.run('getNoAliasGuardMessage(getActiveNoAliasViolations())'), /请绑定游戏ID/);
    input.value = 'Solo';
    assert.equal(h.run('getActiveNoAliasViolations().length'), 0);
});

test('game-only bulk editor renders defaults and rejects empty lists for all players', () => {
    const h = harness(); h.context.state = snapshot();
    h.run('receivePlayerLibraryState(state)'); h.flush();
    assert.match(h.run('renderIdentityIdEditor(playerIdentityState.groups[0])'), /game#job/);
    assert.match(h.run('renderIdentityIdEditor(null, "NameOnly")'), /暂无游戏ID/);
    assert.match(h.run('renderIdentityIdChips(null, "NameOnly", [])'), /identity-game-id-add/);
    const detail = element(); h.nodes.set('identity-detail-pane', detail);
    const start = main.indexOf("document.getElementById('identity-detail-pane')?.addEventListener('input'");
    const end = main.indexOf("document.getElementById('identity-suggestions')?.addEventListener", start);
    h.run(main.slice(start, end));
    const editor = { dataset: { editorKey: 'group:g1' }, querySelectorAll: () => [] };
    const button = { dataset: { groupId: 'g1' }, getAttribute: () => null, classList: { contains: name => name === 'identity-id-save' }, closest: () => editor };
    for (const name of ['', 'Solo', 'NameOnly']) {
        button.dataset = { groupId: name ? '' : 'g1', name };
        detail.listeners.click({ target: { closest: () => button } });
    }
    assert.equal(h.messages.length, 0);
    assert.deepEqual(h.alerts, Array(3).fill('至少保留一个有效游戏ID。'));
});

test('removed frontend features have no controls, commands, styles or script', () => {
    const html = fs.readFileSync(path.join(root, 'web前端', 'index.html'), 'utf8');
    const css = fs.readFileSync(path.join(root, 'web前端', 'style.css'), 'utf8');
    for (const source of [main, html, css]) assert.doesNotMatch(source, /adventure|冒险团|scene.diagnostics/i);
    assert.equal(fs.existsSync(path.join(root, 'web前端', 'scene-diagnostics.js')), false);
});

test('library snapshots are deferred, revision gated, and shared IDs are not copied per name', () => {
    const h = harness();
    h.context.state = snapshot();
    h.run('receivePlayerLibraryState(state)');
    assert.equal(h.run('playerIdentityState'), null);
    assert.equal(h.run('canEditPlayerLibrary()'), false);
    h.flush();
    assert.equal(h.run('canEditPlayerLibrary()'), true);
    assert.equal(h.run('identityIdsForName("Alpha") === identityIdsForName("Beta")'), true);
    assert.equal(h.run('savedDB.Alpha === savedDB.Beta'), true);
    assert.equal(h.run('Object.keys(playerDB).length'), 0);
    h.run('globalThis.previousIdentity = playerIdentityState; globalThis.previousSaved = savedDB');
    h.context.state.playerIdentity.entries = new Proxy([], { get() { throw new Error('Repeated revision parsed'); } });
    h.context.state.fullAliasDB = new Proxy({}, { ownKeys() { throw new Error('Repeated map parsed'); } });
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    assert.equal(h.run('playerIdentityState === previousIdentity && savedDB === previousSaved'), true);
    h.run('receivePlayerLibraryState({players: []})');
    h.flush();
    assert.equal(h.run('savedDB === previousSaved'), true);
});

test('shared-ID suggestions escape game evidence and ignore legacy evidence', () => {
    const h = harness();
    const row = { leftName: 'Alpha', rightName: 'Solo', commonIdCount: 2, commonAdventureIdCount: 1,
        commonGameIds: ['same#job', '<img src=x onerror=1>'], commonAdventureIds: ['Team&One'] };
    h.context.row = row;
    const html = h.run('renderIdentityOverlapRow(row, "Alpha")');
    assert.match(html, /same#job/);
    assert.match(html, /&lt;img src=x onerror=1>/);
    assert.doesNotMatch(html, /<img/);
    assert.doesNotMatch(html, /Team&amp;One/);
    assert.match(html, /游戏ID/);
    assert.doesNotMatch(html, /冒险团ID/);
    assert.match(html, /identity-focused-suggestion/);
    assert.match(html, /确认同一人/);
    assert.match(html, /忽略/);
});

test('bulk ignore captures all filtered suggestions including scroll rows and confirmation revision', () => {
    const h = harness();
    h.context.state = snapshot(19);
    h.context.state.playerIdentity.overlapSuggestions = [
        { leftName: 'Alpha', rightName: 'Solo', commonIdCount: 1, commonGameIds: ['shared-a'] },
        { leftName: 'Alpha', rightName: 'Beta', commonIdCount: 1, commonGameIds: ['shared-b'] },
        { leftName: 'Alpha', rightName: 'NameOnly', commonIdCount: 1, commonGameIds: ['shared-c'] },
        { leftName: 'Solo', rightName: 'Beta', commonIdCount: 1, commonGameIds: ['filtered-out'] }
    ];
    h.run('receivePlayerLibraryState(state)'); h.flush();
    h.run('identitySearchQuery = "Alpha"; globalThis.showConfirm = (message, callback) => { globalThis.confirmText = message; globalThis.confirm = callback; }');
    const before = h.run('JSON.stringify(playerIdentityState)');
    h.run('ignoreAllIdentityOverlaps()');
    assert.match(h.run('confirmText'), /3/);
    h.run('confirm(false)');
    assert.equal(h.messages.length, 0);
    h.run('ignoreAllIdentityOverlaps(); playerIdentityState.revision = 20; playerLibraryState.revision = 20; identitySearchQuery = "Solo"; confirm(true)');
    const command = h.messages.at(-1);
    assert.equal(command.action, 'cmd_identity_ignore_overlaps');
    assert.equal(command.revision, 19);
    assert.equal(command.pairs.length, 3);
    assert.ok(command.pairs.every(pair => pair.leftName === 'Alpha'));
    assert.equal(h.run('playerIdentityState.overlapSuggestions.length'), 4);
    h.context.failure = { requestId: command.requestId, ok: false, error: 'stale revision' };
    h.run('handlePlayerLibraryResult(failure); playerIdentityState.revision = 19');
    assert.equal(h.run('JSON.stringify(playerIdentityState)'), before);
});

test('bulk ignore obeys busy state and has no action for an empty list', () => {
    const h = harness();
    h.context.state = snapshot();
    h.run('receivePlayerLibraryState(state)'); h.flush();
    h.run('globalThis.showConfirm = () => { throw new Error("Unexpected confirmation"); }; ignoreAllIdentityOverlaps()');
    h.run('playerIdentityState.overlapSuggestions = [{leftName:"Alpha", rightName:"Solo", commonIdCount:1}]; playerLibraryState.busy = true; ignoreAllIdentityOverlaps()');
    assert.equal(h.messages.length, 0);
});

test('mutations wait for readiness, send revisions, and leave canonical state untouched', () => {
    const h = harness();
    assert.equal(h.run('sendPlayerIdentityCommand("cmd_identity_merge", {names: ["Alpha", "Beta"]})'), false);
    h.context.state = snapshot(7);
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    assert.equal(h.run('sendPlayerIdentityCommand("cmd_identity_update_ids", {groupId: "g1", name: "", ids: []})'), true);
    const command = h.messages.at(-1);
    assert.equal(command.revision, 7);
    assert.ok(command.requestId);
    assert.deepEqual(Array.from(command.ids), []);
    assert.equal(h.run('playerIdentityState.groups[0].ids.length'), 1);
    assert.equal(h.run('sendPlayerIdentityCommand("cmd_identity_merge", {names: ["Alpha", "Beta"]})'), false);
    h.context.result = { requestId: command.requestId, ok: false, error: 'stale revision' };
    h.run('handlePlayerLibraryResult(result)');
    assert.equal(h.run('identityCommandPending'), null);
    assert.equal(h.run('playerIdentityState.groups[0].ids.length'), 1);
});

test('autocomplete worker ignores repeated and older name revisions', () => {
    const sent = [];
    const context = vm.createContext({ self: { postMessage: value => sent.push(value) } });
    vm.runInContext(workerSource, context);
    context.self.onmessage({ data: { type: 'set-names', revision: 2, names: ['Alpha', 'Beta'] } });
    context.self.onmessage({ data: { type: 'set-names', revision: 2, names: ['Wrong'] } });
    context.self.onmessage({ data: { type: 'set-names', revision: 1, names: ['Old'] } });
    context.self.onmessage({ data: { type: 'query', requestId: 9, query: 'a', activeNames: ['Beta'] } });
    assert.deepEqual(Array.from(sent[0].matches), ['Alpha']);
});

test('busy scoreboard ticks preserve editor DOM, drafts, focus, and caret', () => {
    const h = harness();
    h.context.state = snapshot();
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    const detail = element();
    h.nodes.set('identity-detail-pane', detail);
    h.nodes.set('identity-member-list', element());
    h.nodes.set('identity-overlay', element());
    h.run('identityFocusedName = "Alpha"; identityExpandedIdGroups.add("group:g1"); renderPlayerIdentityPanel()');
    const editor = { dataset: { editorKey: 'group:g1' }, querySelectorAll: () => [input] };
    const input = { value: 'unfinished', selectionStart: 3, selectionEnd: 5, closest: () => editor };
    h.context.document.activeElement = input;
    h.context.editor = editor;
    h.run('rememberIdentityEditorDraft(editor)');
    const writes = detail.writes;
    for (let i = 0; i < 30; ++i) {
        h.run('receivePlayerLibraryState({ playerLibrary: {ready: true, busy: true, storage: "sqlite-v2", revision: 1} })');
    }
    h.flush();
    assert.equal(detail.writes, writes);
    assert.equal(h.context.document.activeElement, input);
    assert.equal(input.value, 'unfinished');
    assert.equal(input.selectionStart, 3);
    assert.equal(input.selectionEnd, 5);
    assert.equal(h.run('identityFocusedName'), 'Alpha');
    assert.equal(h.run('identityEditorDrafts.get("group:g1").values[0]'), 'unfinished');
});

test('success toast waits for canonical snapshot and clears only the acknowledged draft', () => {
    const h = harness();
    h.context.state = snapshot();
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    h.run('identityEditorDrafts.set("group:g1", {values: ["submitted"], revision: 1}); sendPlayerIdentityCommand("cmd_identity_update_ids", {groupId: "g1", ids: ["submitted"], draftKey: "group:g1"})');
    h.run('handlePlayerLibraryResult({ok: true})');
    assert.equal(h.run('identityEditorDrafts.has("group:g1")'), true);
    assert.equal(h.run('playerIdentityState.groups[0].ids[0]'), 'game#job');
    assert.equal(h.messages.at(-1).action, 'cmd_identity_refresh');
    h.context.state = snapshot(2);
    h.context.state.playerIdentity.groups[0].ids = ['submitted'];
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    assert.equal(h.run('identityCommandPending'), null);
    assert.equal(h.run('identityEditorDrafts.has("group:g1")'), false);
    assert.equal(h.run('playerIdentityState.groups[0].ids[0]'), 'submitted');
});

test('failed commits and later edits never discard unsaved input', () => {
    const h = harness();
    h.context.state = snapshot();
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    h.run('identityEditorDrafts.set("group:g1", {values: ["draft"], revision: 1}); sendPlayerIdentityCommand("cmd_identity_update_ids", {groupId: "g1", ids: ["draft"], draftKey: "group:g1"}); handlePlayerLibraryResult({ok: false, error: "disk full"})');
    assert.equal(h.run('identityEditorDrafts.get("group:g1").values[0]'), 'draft');
    assert.equal(h.run('identityLibraryNotice'), 'disk full');
    h.run('sendPlayerIdentityCommand("cmd_identity_update_ids", {groupId: "g1", ids: ["draft"], draftKey: "group:g1"}); identityEditorDrafts.get("group:g1").values = ["typed during save"]; handlePlayerLibraryResult({ok: true})');
    h.context.state = snapshot(2);
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    assert.equal(h.run('identityEditorDrafts.get("group:g1").values[0]'), 'typed during save');
    assert.equal(h.run('identityEditorDrafts.get("group:g1").revision'), 2);
});

test('newer snapshots cancel unfinished parsing and missing alias maps retain latest compatibility data', () => {
    const h = harness();
    h.context.state = snapshot(1);
    for (let i = 0; i < 1000; ++i) h.context.state.playerIdentity.entries.push({ name: `N${i}`, ids: ['id'] });
    h.run('receivePlayerLibraryState(state)');
    h.step();
    assert.equal(h.run('playerIdentityState'), null);
    h.context.state = snapshot(2);
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    assert.equal(h.run('playerIdentityState.revision'), 2);
    assert.equal(h.run('identityEntriesByName.has("N999")'), false);
    h.run('globalThis.previousSaved = savedDB');
    h.context.state = snapshot(3);
    delete h.context.state.fullAliasDB;
    h.context.state.playerIdentity.groups[0].ids = ['latest'];
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    assert.equal(h.run('savedDB === previousSaved'), true);
    assert.equal(h.run('getAliasesForPlayerName("Alpha")[0]'), 'latest');
});

test('actual bridge renders scoreboard before library processing and match writes omit full library', () => {
    const h = harness();
    const order = [];
    let listener;
    h.context.window.chrome.webview.addEventListener = (type, callback) => { listener = callback; };
    h.context.setInterval = () => 1;
    h.context.clearInterval = () => {};
    h.context.applyStateFromServer = data => order.push(data.blueScore);
    h.context.restorePendingAliasPopover = () => true;
    const start = main.indexOf("if (window.chrome && window.chrome.webview) {");
    const end = main.indexOf('\nfunction buildFormattedAliasDB()', start);
    h.run(main.slice(start, end));
    listener({ data: { action: 'sync_state', data: { ...snapshot(1), blueScore: 12, players: [{ name: 'OnField', aliases: ['active'] }] } } });
    assert.deepEqual(order, [12]);
    assert.equal(h.run('playerIdentityState'), null);
    assert.equal(h.run('playerDB.OnField[0]'), 'active');
    h.context.document.querySelector = () => ({ value: '12' });
    h.context.document.querySelectorAll = () => [];
    h.run('pushStateToServer()');
    assert.equal(h.messages.at(-1).action, 'update_state');
    assert.equal(Object.hasOwn(h.messages.at(-1).data, 'fullAliasDB'), false);
    h.flush();
    h.run('pushStateToServer({ NewPlayer: "(new-id)" })');
    assert.equal(h.messages.at(-1).data.playerLibraryRevision, 1);
    assert.equal(h.messages.at(-1).data.fullAliasDB.NewPlayer, '(new-id)');
    h.run('pushStateToServer()');
    assert.equal(Object.hasOwn(h.messages.at(-1).data, 'fullAliasDB'), false);
});

test('grouped-first order does not promote clicked names and ignored suggestions stay hidden', () => {
    const h = harness();
    h.context.state = snapshot();
    h.context.state.playerIdentity.overlapSuggestions = [{ leftName: 'Alpha', rightName: 'Solo', commonIdCount: 1, ignored: true }];
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    h.nodes.set('identity-member-list', element());
    h.nodes.set('identity-suggestions', element());
    h.run('identityFocusedName = "Solo"; renderPlayerIdentityPanel()');
    const html = h.nodes.get('identity-member-list').innerHTML;
    assert.ok(html.indexOf('data-name="Beta"') < html.indexOf('data-name="Alpha"'));
    assert.ok(html.indexOf('data-name="Alpha"') < html.indexOf('data-name="Solo"'));
    assert.equal(h.nodes.get('identity-suggestions').hidden, true);

    const properties = new Map();
    const rows = [{ offsetHeight: 44 }, { offsetHeight: 68 }, { offsetHeight: 52 }];
    h.context.list = {
        style: { setProperty: (key, value) => properties.set(key, value), removeProperty: key => properties.delete(key) },
        querySelectorAll: () => rows
    };
    h.context.window.getComputedStyle = () => ({ rowGap: '6px' });
    h.run('sizeIdentitySuggestionViewport(list)');
    assert.equal(properties.get('--identity-suggestion-row-height'), '68px');
    assert.equal(properties.get('--identity-suggestion-list-height'), '142px');
    rows.forEach(row => { row.offsetHeight = 44; });
    h.run('sizeIdentitySuggestionViewport(list)');
    assert.equal(properties.get('--identity-suggestion-list-height'), '94px');
    rows.splice(1);
    h.run('sizeIdentitySuggestionViewport(list)');
    assert.equal(properties.get('--identity-suggestion-list-height'), '44px');
});

test('shared IDs remain independent-player information and ignore legacy-only overlaps', () => {
    const h = harness();
    h.context.state = snapshot();
    h.context.state.playerIdentity.overlapSuggestions = [
        { leftName: 'Alpha', rightName: 'Solo', commonIdCount: 1 },
        { leftName: 'Beta', rightName: 'Solo', commonIdCount: 0, commonAdventureIdCount: 2 }
    ];
    h.run('receivePlayerLibraryState(state)'); h.flush();
    h.nodes.set('identity-member-list', element());
    h.nodes.set('identity-suggestions', element());
    h.run('renderPlayerIdentityPanel()');
    const html = h.nodes.get('identity-suggestions').innerHTML;
    assert.match(html, /独立选手/);
    assert.doesNotMatch(html, /可能是同一选手/);
    assert.doesNotMatch(html, /冒险团ID/);
    assert.equal((html.match(/class="identity-suggestion-row/g) || []).length, 1);
});

test('nickname and popover edits send canonical commands without ID copies across aliases', () => {
    const h = harness();
    h.context.state = snapshot(4);
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    h.run('bindAliasToPlayer("Alpha", "new-game")');
    assert.equal(h.messages.at(-1).action, 'cmd_identity_update_ids');
    assert.equal(h.messages.at(-1).revision, 4);
    assert.equal(h.run('Object.keys(playerDB).length'), 0);
    assert.equal(h.run('savedDB.Alpha.length'), 1);
    h.run('handlePlayerLibraryResult({ok: false, error: "test"})');
    h.context.showPrompt = (message, callback) => callback('NewNickname');
    h.run('promptIdentityAlias("g1")');
    assert.equal(h.messages.at(-1).action, 'cmd_identity_add_alias');
    assert.equal(h.messages.at(-1).newName, 'NewNickname');
    assert.equal(Object.hasOwn(h.messages.at(-1), 'ids'), false);
    assert.equal(h.run('identityEntriesByName.has("NewNickname")'), false);
});

test('structured results match requests and repeated scoreboard results are harmless', () => {
    const h = harness();
    h.context.state = snapshot();
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    h.run('sendPlayerIdentityCommand("cmd_identity_update_ids", {name: "Solo", ids: ["solo"]})');
    const requestId = h.messages.at(-1).requestId;
    h.context.state = { ...snapshot(2), libraryResult: { requestId: 'unrelated', ok: false, error: 'unrelated failure' } };
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    assert.notEqual(h.run('identityCommandPending'), null);
    h.context.state.libraryResult = { requestId, ok: true, error: '' };
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    assert.equal(h.run('identityCommandPending'), null);
    h.run('sendPlayerIdentityCommand("cmd_identity_update_ids", {name: "Solo", ids: ["next"]}); receivePlayerLibraryState(state)');
    assert.notEqual(h.run('identityCommandPending'), null);
});

test('unmerge sends the selected owner and the confirmation view revision', () => {
    const h = harness();
    h.context.state = snapshot(9);
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    let confirm;
    let markup;
    h.context.showConfirm = (html, callback) => { markup = html; confirm = callback; };
    h.run('confirmIdentityUnmerge("g1")');
    assert.match(markup, /<select id="identity-split-owner">/);
    h.nodes.set('identity-split-owner', { value: 'Beta' });
    h.context.state = snapshot(10);
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    confirm(true);
    assert.equal(h.messages.at(-1).action, 'cmd_identity_unmerge');
    assert.equal(h.messages.at(-1).keepNewIdsWith, 'Beta');
    assert.equal(h.messages.at(-1).revision, 9);
});

test('loaded legacy fallback remains searchable but never writable', () => {
    const h = harness();
    h.context.state = snapshot(1);
    h.context.state.playerLibrary = { loaded: true, ready: false, busy: false, storage: 'legacy', revision: 1, error: 'migration failed' };
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    assert.equal(h.run('playerIdentityState.revision'), 1);
    assert.equal(h.run('getLibraryMainNamesForAutocomplete().includes("Alpha")'), true);
    assert.equal(h.run('identityIdsForName("Alpha")[0]'), 'game#job');
    assert.equal(h.run('canEditPlayerLibrary()'), false);
    assert.equal(h.run('sendPlayerIdentityCommand("cmd_identity_merge", {names: ["Alpha", "Beta"]})'), false);
    assert.equal(h.run('bindAliasToPlayer("Alpha", "new-id")'), false);
    const status = element();
    h.nodes.set('identity-library-status', status);
    h.run('updatePlayerLibraryControls()');
    assert.equal(status.textContent, 'migration failed');
});

test('old hosts without loaded keep the ready gate on failed startup', () => {
    const h = harness();
    h.context.state = snapshot(1);
    h.context.state.playerLibrary = { ready: false, busy: false, storage: 'legacy', revision: 1, error: 'load failed' };
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    assert.equal(h.run('playerIdentityState'), null);
    assert.equal(h.run('librarySnapshotJob'), null);
    h.nodes.set('identity-member-list', element());
    h.run('renderPlayerIdentityPanel()');
    assert.doesNotMatch(h.nodes.get('identity-member-list').innerHTML, /\u52a0\u8f7d\u4e2d/);
});

test('split payloads at one revision coalesce without losing identity or compatibility maps', () => {
    const h = harness();
    const state = snapshot();
    h.context.aliasState = { playerLibrary: state.playerLibrary, fullAliasDB: state.fullAliasDB };
    h.context.identityState = { playerLibrary: state.playerLibrary, playerIdentity: state.playerIdentity };
    h.run('receivePlayerLibraryState(aliasState); receivePlayerLibraryState(identityState)');
    h.flush();
    assert.equal(h.run('playerIdentityState.revision'), 1);
    assert.equal(h.run('savedDB.Alpha[0]'), 'game#job');
    assert.equal(h.run('canEditPlayerLibrary()'), true);
});

test('identity editor includes padding and borders in its bounded width', () => {
    const css = fs.readFileSync(path.join(root, 'web\u524d\u7aef', 'style.css'), 'utf8');
    const editorRule = css.match(/\.identity-id-editor\s*\{([^}]+)\}/)?.[1];
    assert.ok(editorRule, 'Editor sizing rule must exist');
    assert.match(editorRule, /width:\s*min\(100%,\s*620px\);/);
    assert.match(editorRule, /box-sizing:\s*border-box;/);
    assert.match(editorRule, /min-width:\s*0;/);
    assert.match(css, /\.identity-id-editor-row\s*\{[^}]*min-width:\s*0;/);
    assert.match(css, /\.identity-id-input\s*\{[^}]*min-width:\s*0;/);
});

function detailHarness(name = 'Alpha', state = snapshot(7)) {
    const h = harness();
    h.context.state = state;
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    const detail = element();
    h.nodes.set('identity-detail-pane', detail);
    h.context.focusedName = name;
    h.run('identityFocusedName = focusedName; renderIdentityDetailPane()');
    const start = main.indexOf("document.getElementById('identity-detail-pane')?.addEventListener('input'");
    const end = main.indexOf("document.getElementById('identity-suggestions')?.addEventListener", start);
    h.run(main.slice(start, end));
    h.click = (className, index = 0) => {
        const markup = [...detail.innerHTML.matchAll(/<button\b[^>]*>/g)]
            .map(match => match[0]).filter(tag => tag.match(/class="([^"]+)"/)?.[1].split(' ').includes(className))[index];
        assert.ok(markup, `Missing rendered ${className} button ${index}`);
        const attrs = Object.fromEntries([...markup.matchAll(/([\w-]+)="([^"]*)"/g)].map(match => [match[1], match[2]]));
        const button = {
            dataset: Object.fromEntries(Object.entries(attrs).filter(([key]) => key.startsWith('data-'))
                .map(([key, value]) => [key.slice(5).replace(/-([a-z])/g, (_, letter) => letter.toUpperCase()), value])),
            getAttribute: key => attrs[key],
            classList: { contains: value => attrs.class.split(' ').includes(value) },
            closest: () => null
        };
        detail.listeners.click({ target: { closest: () => button }, preventDefault() {}, stopPropagation() {} });
    };
    return h;
}

for (const name of ['Alpha', 'Solo', 'NameOnly']) {
    test(`detail toolbar precedes associated names for ${name}`, () => {
        const h = detailHarness(name);
        const html = h.nodes.get('identity-detail-pane').innerHTML;
        const toolbar = html.match(/<div class="identity-detail-actions"[^>]*>([\s\S]*?)<\/div>/);
        assert.ok(toolbar, 'Selected player actions must exist');
        assert.ok(html.indexOf('</h3>') < toolbar.index);
        assert.ok(toolbar.index < html.indexOf('class="identity-related-names"'), 'Actions belong above associated names');
        assert.equal((html.match(/class="identity-detail-actions"/g) || []).length, 1);
        assert.match(toolbar[0], /role="group" aria-label="[^"]+"/);
        assert.match(toolbar[1], /identity-add-alias/);
        assert.doesNotMatch(toolbar[1], /identity-edit-ids/);
        assert.equal((toolbar[1].match(/<button\b/g) || []).length, name === 'Alpha' ? 2 : 1);
        assert.equal(toolbar[1].includes('identity-unmerge'), name === 'Alpha');
        if (name !== 'Alpha') assert.ok(toolbar[1].includes(`data-source-name="${name}"`));
        assert.doesNotMatch(html, /identity-adventure/);
        assert.match(html, /identity-name-edit/);
        assert.match(html, /identity-name-select/);
    });
}

for (const name of ['Alpha', 'Solo']) {
    test(`game chip body uses native whole-identity rename with profession text for ${name}`, () => {
        const state = snapshot(7);
        const entity = name === 'Alpha' ? state.playerIdentity.groups[0] : state.playerIdentity.entries[2];
        entity.ids = ['first#profession', 'second#other'];
        const h = detailHarness(name, state);
        let prompt;
        h.context.showPrompt = (message, callback, options) => { prompt = { callback, options }; };
        h.click('identity-game-id-edit');
        assert.equal(prompt.options.value, 'first#profession');
        prompt.callback('renamed#profession');
        const command = h.messages.at(-1);
        assert.equal(command.action, 'cmd_identity_rename_id');
        assert.equal(command.name, name === 'Alpha' ? 'Beta' : 'Solo');
        assert.equal(command.oldId, 'first#profession');
        assert.equal(command.newId, 'renamed#profession');
        assert.equal(command.revision, 7);
        assert.equal(Object.hasOwn(command, 'ids'), false, 'Rename must not remove/re-add IDs and drop per-ID statistics');
        assert.equal(h.run(`identityIdsForName(${JSON.stringify(name)})[0]`), 'first#profession');
    });

    test(`game chip deletion requires confirmation for ${name}`, () => {
        const h = detailHarness(name);
        let confirm;
        h.context.showPrompt = () => assert.fail('Delete must never open the edit prompt');
        h.context.showConfirm = (message, callback) => { confirm = callback; };
        h.click('identity-game-id-delete');
        assert.equal(typeof confirm, 'function');
        const count = h.messages.length;
        confirm(false);
        assert.equal(h.messages.length, count);
        h.click('identity-game-id-delete');
        confirm(true);
        assert.equal(h.messages.length, count, 'Last game ID must remain protected');
        assert.equal(h.alerts.length, 1);
    });

    test(`game rename rejects other-index storage collisions but permits self and distinct professions for ${name}`, () => {
        const state = snapshot(7);
        const entity = name === 'Alpha' ? state.playerIdentity.groups[0] : state.playerIdentity.entries[2];
        entity.ids = ['first#profession', 'second#other'];
        const h = detailHarness(name, state);
        let prompt;
        h.context.showPrompt = (message, callback, options) => { prompt = { message, callback, options }; };
        h.click('identity-game-id-edit');
        for (const value of ['second#other', ' second # other ']) {
            prompt.callback(value);
            assert.equal(h.messages.length, 0, 'Collision must not issue a native command');
            assert.match(prompt.message, /\u8be5\u6e38\u620fID\u5df2\u5b58\u5728/);
            assert.equal(prompt.options.value, value.trim(), 'Keep attempted value in the edit prompt');
            assert.equal(h.run('identityFocusedName'), name);
        }
        prompt.callback(null);
        assert.equal(h.messages.length, 0);
        h.click('identity-game-id-edit');
        prompt.callback('first#profession');
        assert.equal(h.messages.length, 0, 'Unchanged raw value is a no-op');
        prompt.callback('first # profession');
        assert.equal(h.messages.at(-1).action, 'cmd_identity_rename_id', 'The edited index is excluded from collision checks');
        assert.equal(h.messages.at(-1).oldId, 'first#profession');
        h.run('handlePlayerLibraryResult({ok: false, error: "fixture"})');
        h.click('identity-game-id-edit');
        prompt.callback('second#different');
        assert.equal(h.messages.at(-1).newId, 'second#different', 'Different professions are distinct storage entries');
        assert.equal(h.messages.at(-1).revision, 7);
        assert.deepEqual(Array.from(h.run('identityIdsForName(focusedName)')), ['first#profession', 'second#other']);
    });
}

test('trailing game-ID add supports empty standalone lists and retains prompt revision', () => {
    const h = detailHarness('NameOnly');
    let submit;
    h.context.showPrompt = (message, callback) => { submit = callback; };
    h.click('identity-game-id-add');
    h.context.state = snapshot(8);
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    submit('new#profession');
    assert.equal(h.messages.at(-1).name, 'NameOnly');
    assert.equal(h.messages.at(-1).revision, 7);
    assert.deepEqual(Array.from(h.messages.at(-1).ids), ['new#profession']);
});

test('chip add appends to the group list and empty or cancelled edits do not save', () => {
    const h = detailHarness();
    let submit;
    h.context.showPrompt = (message, callback) => { submit = callback; };
    h.click('identity-game-id-edit');
    const count = h.messages.length;
    submit(null);
    submit('   ');
    assert.equal(h.messages.length, count);
    h.click('identity-game-id-add');
    submit('new#profession');
    assert.equal(h.messages.at(-1).groupId, 'g1');
    assert.deepEqual(Array.from(h.messages.at(-1).ids), ['game#job', 'new#profession']);
});

test('chip actions obey runtime readiness both before opening and before saving', () => {
    const h = detailHarness();
    let submit;
    h.context.showPrompt = (message, callback) => { submit = callback; };
    h.click('identity-game-id-edit');
    h.run('playerLibraryState.busy = true');
    const count = h.messages.length;
    submit('new#profession');
    assert.equal(h.messages.length, count);
    h.context.showPrompt = h.context.showConfirm = () => assert.fail('Disabled action opened a dialog');
    for (const action of ['edit', 'delete', 'add']) h.click(`identity-game-id-${action}`);
    h.run('playerLibraryState.busy = false; playerLibraryState.ready = false');
    for (const action of ['edit', 'delete', 'add']) h.click(`identity-game-id-${action}`);
    assert.equal(h.messages.length, count);
});

test('toolbar and game chips have bounded wrapping and explicit focus/touch controls', () => {
    const css = fs.readFileSync(path.join(root, 'web\u524d\u7aef', 'style.css'), 'utf8');
    assert.match(css, /\.identity-detail-actions\s*\{[^}]*flex-wrap:\s*wrap;/);
    assert.match(css, /\.identity-detail-actions \.identity-card-action:focus-visible/);
    assert.match(css, /\.identity-editable-chip\s*\{[^}]*position:\s*relative;/);
    assert.match(css, /\.identity-chip-label\s*\{[^}]*overflow-wrap:\s*anywhere;/);
    assert.match(css, /\.identity-editable-chip:focus-within/);
    assert.match(css, /@media\s*\(hover:\s*none\)/);
});

test('HTML, scripts, styles and autocomplete cache use one frontend version', () => {
    const html = fs.readFileSync(path.join(root, 'web\u524d\u7aef', 'index.html'), 'utf8');
    const version = main.match(/const WEB_LAYOUT_VERSION = '([^']+)'/)?.[1];
    assert.ok(version);
    assert.equal(version, '20260912-5.2.2-production');
    assert.ok(html.includes(`name="dnf-web-layout-version" content="${version}"`));
    assert.ok(html.includes(`style.css?v=${version}`));
    assert.ok(html.includes(`main.js?v=${version}`));
    assert.ok(main.includes('autocomplete-worker.js?v=${WEB_LAYOUT_VERSION}'));
});

for (const name of ['Alpha', 'Solo']) {
    test(`name tag rename uses native revision-gated command for ${name}`, () => {
        const h = detailHarness(name);
        let prompt;
        h.context.showPrompt = (message, callback, options) => { prompt = { callback, options }; };
        h.click('identity-name-edit', name === 'Alpha' ? 1 : 0);
        assert.equal(prompt.options.value, name);
        const count = h.messages.length;
        prompt.callback(null);
        prompt.callback(' ');
        prompt.callback(name);
        assert.equal(h.messages.length, count, 'Cancel, empty and unchanged names must not save');
        prompt.callback('Renamed');
        const command = h.messages.at(-1);
        assert.equal(command.action, 'cmd_identity_rename_name');
        assert.equal(command.name, name);
        assert.equal(command.newName, 'Renamed');
        assert.equal(command.revision, 7);
        assert.equal(Object.hasOwn(command, 'selectionChange'), false, 'UI selection metadata stays local');
        assert.equal(h.run('identityFocusedName'), name, 'Wait for canonical rename before changing heading');
        h.run('handlePlayerLibraryResult({ok: true}); finishPlayerIdentityCommand()');
        assert.notEqual(h.run('identityCommandPending'), null, 'Old canonical revision cannot finish a rename');
        h.run('identitySelectedNames.add(identityFocusedName)');
        h.context.state = snapshot(8);
        h.context.state.playerIdentity.entries.find(entry => entry.name === name).name = 'Renamed';
        if (name === 'Alpha') h.context.state.playerIdentity.groups[0].names[1] = 'Renamed';
        h.run('receivePlayerLibraryState(state)');
        h.flush();
        assert.equal(h.run('identityFocusedName'), 'Renamed');
        assert.equal(h.run('identitySelectedNames.has("Renamed")'), true);
        assert.equal(h.run('identitySelectedNames.has(focusedName)'), false);
    });

    test(`name tag delete is confirmed and preserves native semantics for ${name}`, () => {
        const h = detailHarness(name);
        let confirm;
        h.context.showPrompt = () => assert.fail('Delete must not edit or select a name');
        h.context.showConfirm = (message, callback) => { confirm = callback; };
        h.click('identity-delete-alias', name === 'Alpha' ? 1 : 0);
        assert.equal(typeof confirm, 'function');
        const count = h.messages.length;
        confirm(false);
        assert.equal(h.messages.length, count);
        confirm(true);
        const command = h.messages.at(-1);
        assert.equal(command.action, 'cmd_identity_delete_alias');
        assert.equal(command.groupId, name === 'Alpha' ? 'g1' : '');
        assert.equal(command.name, name);
        assert.equal(command.revision, 7);
        h.run('identitySelectedNames.add(identityFocusedName); handlePlayerLibraryResult({ok: true})');
        h.context.state = snapshot(8);
        h.context.state.playerIdentity.entries = h.context.state.playerIdentity.entries.filter(entry => entry.name !== name);
        h.context.state.playerIdentity.groups[0].names = h.context.state.playerIdentity.groups[0].names.filter(member => member !== name);
        h.run('receivePlayerLibraryState(state)');
        h.flush();
        assert.notEqual(h.run('identityFocusedName'), name);
        assert.equal(h.run('!!findIdentityEntryByName(identityFocusedName)'), true);
        assert.equal(h.run('identitySelectedNames.has(focusedName)'), false);
    });

}

test('name selection is explicit and never edits match data or opens a prompt', () => {
    const h = detailHarness('Alpha');
    h.context.showPrompt = () => assert.fail('Selecting a name must not edit it');
    h.click('identity-name-select');
    assert.equal(h.run('identityFocusedName'), 'Beta');
    assert.equal(h.messages.length, 0);
});

test('confirmed rename finalizes when a coalesced snapshot contains both names', () => {
    for (const ackFirst of [true, false]) {
        const h = detailHarness('Alpha');
        h.run('identitySelectedNames.add("Alpha")');
        h.context.showPrompt = (message, callback) => callback('Renamed');
        h.click('identity-name-edit', 1);
        if (ackFirst) h.run('handlePlayerLibraryResult({ok: true}); finishPlayerIdentityCommand()');
        assert.notEqual(h.run('identityCommandPending'), null);
        assert.equal(h.run('identityFocusedName'), 'Alpha');
        assert.deepEqual(Array.from(h.run('identitySelectedNames')), ['Alpha']);
        h.context.state = snapshot(9);
        h.context.state.playerIdentity.entries.push({ name: 'Renamed', ids: ['renamed'] });
        h.run('receivePlayerLibraryState(state)');
        h.flush();
        if (!ackFirst) {
            assert.notEqual(h.run('identityCommandPending'), null, 'Snapshot alone cannot finalize an unacknowledged rename');
            assert.equal(h.run('identityFocusedName'), 'Alpha');
            assert.deepEqual(Array.from(h.run('identitySelectedNames')), ['Alpha']);
            h.run('handlePlayerLibraryResult({ok: true}); receivePlayerLibraryState({players: []})');
        }
        assert.equal(h.run('identityCommandPending'), null, 'Old name reintroduced by import must not block completion');
        assert.equal(h.run('canEditPlayerLibrary()'), true);
        assert.equal(h.run('identityFocusedName'), 'Renamed');
        assert.deepEqual(Array.from(h.run('identitySelectedNames')), ['Renamed']);
        assert.equal(h.run('!!findIdentityEntryByName("Alpha")'), true, 'Canonical old name remains untouched');
    }
});

test('confirmed rename finalizes without a replacement and keeps focus valid', () => {
    for (const scenario of [
        { oldName: true, focus: 'Alpha', expectedFocus: 'Alpha', selected: ['Alpha'] },
        { focus: 'Solo', expectedFocus: 'Solo', selected: ['Solo'] },
        { focus: 'Alpha', expectedFocus: 'Beta', selected: [] },
        { empty: true, focus: 'Alpha', expectedFocus: '', selected: [] }
    ]) {
        const h = detailHarness('Alpha');
        h.context.showPrompt = (message, callback) => callback('Renamed');
        h.click('identity-name-edit', 1);
        h.context.currentFocus = scenario.focus;
        h.run('identityFocusedName = currentFocus; identitySelectedNames.add("Alpha"); identitySelectedNames.add(currentFocus); handlePlayerLibraryResult({ok: true}); finishPlayerIdentityCommand()');
        assert.notEqual(h.run('identityCommandPending'), null, 'Acknowledgement still waits for a newer revision');
        assert.equal(h.run('identityFocusedName'), scenario.focus);
        h.context.state = snapshot(9);
        if (!scenario.oldName) {
            h.context.state.playerIdentity.groups[0].names = ['Beta'];
            h.context.state.playerIdentity.entries = h.context.state.playerIdentity.entries.filter(entry => entry.name !== 'Alpha');
        }
        if (scenario.empty) {
            h.context.state.playerIdentity.groups = [];
            h.context.state.playerIdentity.entries = [];
            h.context.state.fullAliasDB = {};
        }
        h.run('receivePlayerLibraryState(state)');
        h.flush();
        assert.equal(h.run('identityCommandPending'), null, 'Missing replacement must not hold the UI busy forever');
        assert.equal(h.run('canEditPlayerLibrary()'), true);
        assert.equal(h.run('identityFocusedName'), scenario.expectedFocus);
        assert.deepEqual(Array.from(h.run('identitySelectedNames')), scenario.selected);
        assert.equal(h.run('identitySelectedNames.has("Renamed")'), false);
        assert.equal(h.run('identityFocusedName === "" || !!findIdentityEntryByName(identityFocusedName)'), true);
    }
});

test('name rename conflict, failed commit and revision changes do not optimistically rename', () => {
    const h = detailHarness('Alpha');
    let submit;
    let prompt;
    h.context.showPrompt = (message, callback, options) => { submit = callback; prompt = { message, options }; };
    h.click('identity-name-edit', 1);
    submit('Solo');
    assert.equal(h.messages.length, 0);
    assert.match(prompt.message, /\u540d\u79f0\u5df2\u5b58\u5728/);
    assert.equal(prompt.options.value, 'Solo');
    assert.equal(h.run('identityFocusedName'), 'Alpha');
    h.context.state = snapshot(8);
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    submit('Renamed');
    assert.equal(h.messages.at(-1).revision, 7);
    h.run('handlePlayerLibraryResult({ok: false, error: "stale"})');
    assert.equal(h.run('identityFocusedName'), 'Alpha');
});

test('name and game actions cannot open or save while runtime is disabled', () => {
    const h = detailHarness();
    let submit;
    h.context.showPrompt = (message, callback) => { submit = callback; };
    h.click('identity-name-edit');
    h.run('playerLibraryState.busy = true');
    submit('Renamed');
    assert.equal(h.messages.length, 0);
    h.context.showPrompt = h.context.showConfirm = () => assert.fail('Disabled action opened a modal');
    for (const action of ['identity-name-edit', 'identity-delete-alias', 'identity-game-id-edit', 'identity-game-id-delete', 'identity-game-id-add']) h.click(action);
    h.run('playerLibraryState.busy = false; playerLibraryState.ready = false');
    for (const action of ['identity-name-edit', 'identity-delete-alias', 'identity-game-id-edit', 'identity-game-id-delete', 'identity-game-id-add']) h.click(action);
    assert.equal(h.messages.length, 0);
});

function resetHarness() {
    const h = harness();
    for (const name of ['normalizeAliasDbAutoSyncState', 'renderAliasDbAutoSync']) h.run(functionSource(name));
    h.context.state = snapshot(7);
    h.run('receivePlayerLibraryState(state)');
    h.flush();
    h.nodes.set('btn-reset-local-library', element());
    h.context.showConfirm = (message, callback, options) => { h.confirm = { message, callback, options }; };
    const start = main.indexOf("document.getElementById('btn-reset-local-library')?.addEventListener");
    assert.notEqual(start, -1, 'Reset button needs a click handler');
    const end = main.indexOf('\ndocument.getElementById(', start + 1);
    h.run(main.slice(start, end));
    h.click = () => h.nodes.get('btn-reset-local-library').listeners.click();
    return h;
}

test('local reset is a disabled-by-default action in More > Data', () => {
    const html = fs.readFileSync(path.join(root, 'web\u524d\u7aef', 'index.html'), 'utf8');
    const section = html.match(/<div class="more-controls-section">\s*<div class="more-controls-title">数据<\/div>([\s\S]*?)<div class="more-controls-section">/)?.[1];
    assert.ok(section);
    assert.match(section, /<button[^>]*id="btn-reset-local-library"[^>]*disabled[^>]*>删除本地选手库<\/button>/);
});

test('local reset confirms local scope, backup, sync policy, and captured revision without optimistic changes', () => {
    const h = resetHarness();
    h.run('playerDB = {Alpha: ["match-only"]}; aliasDbAutoSyncState = {enabled: true}');
    h.click();
    for (const phrase of ['全部本地选手', '停止监控', '场上8个位置的选手名称、ID和个人战绩', '游戏ID', '身份归并关系', '选手库与players_config.txt一并备份并清空', '备份失败则不删除', '队伍总分', '最近战绩记录', '云端库保持不变', '自动同步', '关闭', '手动', '同步云端库']) {
        assert.ok(h.confirm.message.includes(phrase), `Missing reset warning: ${phrase}`);
    }
    assert.doesNotMatch(h.confirm.message, /当前比赛、场上选手、比分、战绩和云端库保持不变/);
    assert.equal(h.confirm.options.okText, '删除本地选手库');
    h.confirm.callback(false);
    assert.equal(h.messages.length, 0);
    h.click();
    h.context.state = snapshot(8);
    h.run('receivePlayerLibraryState(state)'); h.flush();
    h.confirm.callback(true);
    const command = h.messages.at(-1);
    assert.equal(command.action, 'cmd_identity_reset_local');
    assert.equal(command.confirmed, true);
    assert.equal(command.revision, 7, 'Use the revision that was shown at confirmation time');
    assert.ok(command.requestId);
    assert.equal(h.run('playerIdentityState.entries.length'), 4);
    assert.equal(h.run('Object.keys(savedDB).length'), 4);
    assert.equal(h.run('playerDB.Alpha[0]'), 'match-only');
    assert.equal(h.run('aliasDbAutoSyncState.enabled'), true);
    h.confirm.callback(true);
    assert.equal(h.messages.length, 1, 'Pending reset must not submit twice');
});

test('local reset tooltip and pending status describe all local and fielded identities', () => {
    const h = resetHarness();
    h.nodes.set('identity-library-status', element());
    h.run('updatePlayerLibraryControls()');
    const title = h.nodes.get('btn-reset-local-library').title;
    for (const phrase of ['全部本地选手', '停止监控', '场上8个位置', '名称、ID和个人战绩', '队伍总分', '最近战绩记录', '云端库保持不变']) {
        assert.ok(title.includes(phrase), `Missing reset tooltip scope: ${phrase}`);
    }
    h.click(); h.confirm.callback(true);
    const status = h.nodes.get('identity-library-status').textContent;
    for (const phrase of ['正在删除全部本地选手', '停止监控', '场上8个位置', '名称、ID和个人战绩', '队伍总分', '最近战绩记录', '云端库保持不变']) {
        assert.ok(status.includes(phrase), `Missing pending reset scope: ${phrase}`);
    }
    assert.doesNotMatch(status, /已删除/);
});

for (const blocked of [
    'playerLibraryState.ready = false',
    'playerLibraryState.busy = true',
    'librarySnapshotJob = {}',
    'identityCommandPending = {requestId: "other"}',
    'aliasDbAutoSyncState = {inFlight: true}',
    'aliasDbAutoSyncState = {manualInFlight: true}',
    'cloudMatchState = {realtimeFollowing: true}'
]) {
    test(`local reset blocks opening, confirmation, and direct submission when ${blocked}`, () => {
        const h = resetHarness();
        h.click();
        const confirm = h.confirm.callback;
        h.confirm = null;
        h.run(blocked + '; updatePlayerLibraryControls()');
        const button = h.nodes.get('btn-reset-local-library');
        assert.equal(button.disabled, true);
        assert.equal(button.getAttribute('aria-disabled'), 'true');
        assert.ok(button.title);
        h.click();
        assert.equal(h.confirm, null);
        confirm(true);
        assert.equal(h.run('sendPlayerIdentityCommand("cmd_identity_reset_local", {confirmed: true, revision: 7})'), false);
        assert.equal(h.messages.length, 0);
    });
}

test('local reset requires explicit confirmation and tracks sync state without a new library revision', () => {
    const h = resetHarness();
    assert.equal(h.run('sendPlayerIdentityCommand("cmd_identity_reset_local")'), false);
    assert.equal(h.messages.length, 0);
    const button = h.nodes.get('btn-reset-local-library');
    h.run('renderAliasDbAutoSync({manualInFlight: true})');
    assert.equal(button.disabled, true);
    h.run('renderAliasDbAutoSync({inFlight: true})');
    assert.equal(button.disabled, true);
    h.run('renderAliasDbAutoSync({enabled: true})');
    assert.equal(button.disabled, false, 'Enabled but idle auto sync does not block reset');
    assert.equal(button.getAttribute('aria-disabled'), 'false');
});

test('failed local reset preserves library and drafts and surfaces the native error outside the editor', () => {
    const h = resetHarness();
    h.run('identityFocusedName = "Alpha"; identityEditorDrafts.set("draft", {values: ["unsaved"]})');
    h.click(); h.confirm.callback(true);
    h.context.result = { requestId: h.messages.at(-1).requestId, ok: false, error: 'backup <failed>' };
    h.run('handlePlayerLibraryResult(result)');
    assert.equal(h.run('identityCommandPending'), null);
    assert.equal(h.run('playerIdentityState.entries.length'), 4);
    assert.equal(h.run('Object.keys(savedDB).length'), 4);
    assert.equal(h.run('identityFocusedName'), 'Alpha');
    assert.equal(h.run('identityEditorDrafts.size'), 1);
    assert.deepEqual(h.alerts, ['backup &lt;failed>']);
    assert.equal(h.nodes.get('btn-reset-local-library').disabled, false);
    h.run('handlePlayerLibraryResult(result)');
    assert.equal(h.alerts.length, 1, 'Duplicate native errors do not reopen the alert');
});

for (const ackFirst of [true, false]) {
    test(`local reset clears editor state only after acknowledgement and newer canonical snapshot (ackFirst=${ackFirst})`, () => {
        const h = resetHarness();
        h.nodes.set('identity-library-status', element());
        h.run('playerDB = {Alpha: ["match-only"]}; identityFocusedName = "Alpha"; identitySelectedNames.add("Alpha"); identityExpandedIdGroups.add("group:g1"); identityEditorDrafts.set("draft", {values: ["unsaved"]})');
        h.click(); h.confirm.callback(true);
        h.context.result = { requestId: h.messages.at(-1).requestId, ok: true };
        if (ackFirst) {
            h.run('handlePlayerLibraryResult(result); receivePlayerLibraryState({players: []})');
            assert.notEqual(h.run('identityCommandPending'), null, 'Acknowledgement alone cannot finish against the old snapshot');
            assert.equal(h.run('Object.keys(savedDB).length'), 4);
            assert.equal(h.run('playerDB.Alpha[0]'), 'match-only');
            assert.doesNotMatch(h.nodes.get('identity-library-status').textContent, /已删除/);
        }
        h.context.state = snapshot(8);
        h.context.state.playerIdentity.groups = [];
        h.context.state.playerIdentity.entries = [];
        h.context.state.fullAliasDB = {};
        h.run('receivePlayerLibraryState(state)'); h.flush();
        if (!ackFirst) {
            assert.equal(h.run('identityEditorDrafts.size'), 1);
            assert.doesNotMatch(h.nodes.get('identity-library-status').textContent, /已删除/);
            assert.equal(h.run('playerDB.Alpha[0]'), 'match-only');
            h.run('autocompleteMainNamesCacheDirty = false');
            h.run('handlePlayerLibraryResult(result); receivePlayerLibraryState({players: []})');
        }
        assert.equal(h.run('identityCommandPending'), null);
        assert.equal(h.run('Object.keys(savedDB).length'), 0);
        assert.equal(h.run('identityMembers.length'), 0);
        assert.equal(h.run('identityEditorDrafts.size'), 0);
        assert.equal(h.run('identitySelectedNames.size'), 0);
        assert.equal(h.run('identityExpandedIdGroups.size'), 0);
        assert.equal(h.run('identityFocusedName'), '');
        assert.equal(h.run('Object.keys(playerDB).length'), 0, 'Confirmed reset must discard cached fielded names and IDs');
        assert.equal(h.run('autocompleteMainNamesCacheDirty'), true);
        const status = h.nodes.get('identity-library-status').textContent;
        for (const phrase of ['已删除全部本地选手', '已停止监控', '场上8个位置', '名称、ID和个人战绩', '队伍总分', '最近战绩记录', '云端库保持不变']) {
            assert.ok(status.includes(phrase), `Missing successful reset scope: ${phrase}`);
        }
        assert.equal(h.alerts.length, 0, 'Native alias_sync_result owns the success notification');
    });
}

test('local reset bridge consumes identity replies and native alias sync notification', () => {
    const h = resetHarness();
    let listener;
    h.context.window.chrome.webview.addEventListener = (type, callback) => { listener = callback; };
    h.context.setInterval = () => 1;
    h.context.clearInterval = () => {};
    const start = main.indexOf('if (window.chrome && window.chrome.webview) {');
    h.run(main.slice(start, main.indexOf('\nfunction buildFormattedAliasDB()', start)));
    h.click(); h.confirm.callback(true);
    const requestId = h.messages.at(-1).requestId;
    listener({ data: { action: 'identity_error', requestId: 'unrelated', message: 'ignore' } });
    assert.equal(h.alerts.length, 0);
    assert.equal(h.run('identityCommandPending.requestId'), requestId);
    listener({ data: { action: 'alias_sync_result', message: 'Reset saved; backup: local.db' } });
    assert.deepEqual(h.alerts, ['Reset saved; backup: local.db']);
    assert.equal(h.run('Object.keys(savedDB).length'), 4);
    listener({ data: { action: 'identity_result', requestId, ok: true } });
    assert.equal(h.messages.at(-1).action, 'cmd_identity_refresh');
    assert.equal(h.run('identityCommandPending.confirmed'), true);
});
