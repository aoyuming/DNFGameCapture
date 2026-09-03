const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function requireCondition(condition, message) {
    if (!condition) throw new Error(message);
}

const root = path.resolve(__dirname, '..');
const webRoot = fs.readdirSync(root, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => path.join(root, entry.name))
    .find((candidate) => fs.existsSync(path.join(candidate, 'main.js')) &&
        fs.existsSync(path.join(candidate, 'style.css')));
requireCondition(webRoot, 'Unable to locate Web scoreboard files.');

const source = fs.readFileSync(path.join(webRoot, 'main.js'), 'utf8');
const css = fs.readFileSync(path.join(webRoot, 'style.css'), 'utf8')
    .replace(/\r\n/g, '\n');
const html = fs.readFileSync(path.join(webRoot, 'index.html'), 'utf8');
const hostSource = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.cpp'), 'utf8');
const hostHeader = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.h'), 'utf8');
const declaration = source.match(/const CLOUD_MATCH_WEB_THEMES = new Set\([^;]+;/s)?.[0];
const functionStart = source.indexOf('function applyWebTheme(');
const functionEnd = source.indexOf('\nfunction ', functionStart + 1);
const requestStart = source.indexOf('function requestWebTheme(');
const requestEnd = source.indexOf('\nfunction ', requestStart + 1);
requireCondition(declaration && functionStart >= 0 && functionEnd > functionStart &&
    requestStart >= 0 && requestEnd > requestStart,
    'Theme whitelist runtime function is missing.');

const themeSelect = { value: '' };
const postedMessages = [];
const sandbox = {
    document: {
        documentElement: { dataset: {} },
        getElementById(id) { return id === 'web-theme-select' ? themeSelect : null; }
    },
    window: {
        chrome: { webview: { postMessage(message) { postedMessages.push(message); } } }
    }
};
vm.createContext(sandbox);
vm.runInContext(`${declaration}\n${source.slice(functionStart, functionEnd)}\n` +
    `${source.slice(requestStart, requestEnd)}\n` +
    'this.applyWebTheme = applyWebTheme; this.requestWebTheme = requestWebTheme;', sandbox);

for (const theme of ['dark-esports', 'frost-broadcast', 'black-gold']) {
    requireCondition(sandbox.applyWebTheme(theme) === theme,
        `Expected ${theme} to be accepted.`);
    requireCondition(sandbox.document.documentElement.dataset.theme === theme,
        `Expected ${theme} to be applied to documentElement.dataset.theme.`);
    requireCondition(themeSelect.value === theme,
        `Expected ${theme} to update the appearance theme selector.`);
}
for (const theme of ['dark-esports', 'frost-broadcast', 'black-gold']) {
    sandbox.requestWebTheme(theme);
    requireCondition(postedMessages.at(-1)?.action === 'cmd_set_web_theme' &&
        postedMessages.at(-1)?.theme === theme,
    `Expected ${theme} selection to post cmd_set_web_theme.`);
}
requireCondition(sandbox.applyWebTheme('untrusted-theme') === 'dark-esports',
    'Unknown themes must fall back to dark-esports.');
requireCondition(sandbox.document.documentElement.dataset.theme === 'dark-esports',
    'Fallback theme must be applied to the root element.');
requireCondition(source.includes('applyWebTheme(state.webTheme);'),
    'Server state updates must apply webTheme at runtime.');
requireCondition(source.includes("action: 'cmd_set_web_theme'"),
    'Theme selector must send cmd_set_web_theme to the host.');
requireCondition(source.includes("getElementById('web-theme-select')?.addEventListener('change'"),
    'Theme selector change handler is missing.');

for (const expected of [
    'id="web-theme-select"',
    '<option value="dark-esports">暗黑电竞</option>',
    '<option value="frost-broadcast">霜白直播台</option>',
    '<option value="black-gold">黑金赛事</option>'
]) {
    requireCondition(html.includes(expected), `Missing Web theme control markup: ${expected}`);
}

for (const expected of [
    'CString m_webTheme = L"dark-esports";',
    'GetPrivateProfileString(L"Settings", L"WebTheme", L"dark-esports"',
    'data["webTheme"] = DnfJsonUtf8(m_webTheme);',
    'action == "cmd_set_web_theme"',
    'WritePrivateProfileString(L"Settings", L"WebTheme", m_webTheme, m_iniPath)'
]) {
    requireCondition(hostHeader.includes(expected) || hostSource.includes(expected),
        `Missing host WebTheme contract: ${expected}`);
}
for (const theme of ['dark-esports', 'frost-broadcast', 'black-gold']) {
    requireCondition(hostSource.includes(`value == L"${theme}"`),
        `Host whitelist must include ${theme}.`);
}

for (const expected of [
    '.cloud-sync-preview-pane {\n    background: var(--cloud-sync-preview-bg);',
    '.cloud-sync-group {',
    'background: var(--cloud-sync-group-bg);',
    '.cloud-sync-preview-empty {',
    'color: var(--cloud-sync-empty);'
]) {
    requireCondition(css.includes(expected), `Missing themed cloud sync CSS: ${expected}`);
}

for (const expected of [
    '.appearance-panel {',
    'background: var(--cloud-sync-surface',
    '.web-theme-field',
    'color: var(--cloud-sync-text'
]) {
    requireCondition(css.includes(expected), `Missing themed appearance CSS: ${expected}`);
}

console.log('Cloud match theme runtime tests passed.');
