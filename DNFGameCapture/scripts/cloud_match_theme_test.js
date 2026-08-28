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
const css = fs.readFileSync(path.join(webRoot, 'style.css'), 'utf8');
const declaration = source.match(/const CLOUD_MATCH_WEB_THEMES = new Set\([^;]+;/s)?.[0];
const functionStart = source.indexOf('function applyWebTheme(');
const functionEnd = source.indexOf('\nfunction ', functionStart + 1);
requireCondition(declaration && functionStart >= 0 && functionEnd > functionStart,
    'Theme whitelist runtime function is missing.');

const sandbox = {
    document: { documentElement: { dataset: {} } }
};
vm.createContext(sandbox);
vm.runInContext(`${declaration}\n${source.slice(functionStart, functionEnd)}\n` +
    'this.applyWebTheme = applyWebTheme;', sandbox);

for (const theme of ['dark-esports', 'frost-broadcast', 'black-gold']) {
    requireCondition(sandbox.applyWebTheme(theme) === theme,
        `Expected ${theme} to be accepted.`);
    requireCondition(sandbox.document.documentElement.dataset.theme === theme,
        `Expected ${theme} to be applied to documentElement.dataset.theme.`);
}
requireCondition(sandbox.applyWebTheme('untrusted-theme') === 'dark-esports',
    'Unknown themes must fall back to dark-esports.');
requireCondition(sandbox.document.documentElement.dataset.theme === 'dark-esports',
    'Fallback theme must be applied to the root element.');
requireCondition(source.includes('applyWebTheme(state.webTheme);'),
    'Server state updates must apply webTheme at runtime.');

for (const expected of [
    '.cloud-sync-preview-pane {\n    background: var(--cloud-sync-preview-bg);',
    '.cloud-sync-group {',
    'background: var(--cloud-sync-group-bg);',
    '.cloud-sync-preview-empty {',
    'color: var(--cloud-sync-empty);'
]) {
    requireCondition(css.includes(expected), `Missing themed cloud sync CSS: ${expected}`);
}

console.log('Cloud match theme runtime tests passed.');
