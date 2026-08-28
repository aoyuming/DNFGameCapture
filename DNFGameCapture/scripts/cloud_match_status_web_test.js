const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function requireCondition(condition, message) {
    if (!condition) throw new Error(message);
}

function extractFunction(source, name) {
    const start = source.indexOf(`function ${name}(`);
    const end = source.indexOf('\nfunction ', start + 1);
    requireCondition(start >= 0 && end > start, `Missing Web function: ${name}`);
    return source.slice(start, end);
}

const root = path.resolve(__dirname, '..');
const webRoot = fs.readdirSync(root, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => path.join(root, entry.name))
    .find((candidate) => fs.existsSync(path.join(candidate, 'main.js')) &&
        fs.existsSync(path.join(candidate, 'index.html')));
requireCondition(webRoot, 'Unable to locate Web scoreboard files.');

const source = fs.readFileSync(path.join(webRoot, 'main.js'), 'utf8');
const roomNames = source.match(
    /const CLOUD_ROOM_NAMES = Object\.freeze\(\{.*?\}\);/s)?.[0];
const displayStates = source.match(
    /const CLOUD_MATCH_DISPLAY_STATES = new Set\([^;]+;/s)?.[0];
requireCondition(roomNames && displayStates,
    'Cloud match Web display declarations are missing.');

const statusElement = { dataset: {}, textContent: '', title: '' };
const sandbox = {
    statusElement,
    document: {
        getElementById(id) { return id === 'cloud-room-status' ? statusElement : null; }
    }
};
vm.createContext(sandbox);
vm.runInContext(
    `let cloudMatchState = null;\n${roomNames}\n${displayStates}\n` +
    `${extractFunction(source, 'normalizeCloudSyncPanel')}\n` +
    `${extractFunction(source, 'normalizeCloudMatchState')}\n` +
    `${extractFunction(source, 'renderCloudRoomStatus')}\n` +
    `this.renderFixture = (fixture) => {\n` +
    `  cloudMatchState = normalizeCloudMatchState(fixture);\n` +
    `  renderCloudRoomStatus();\n` +
    `  return { state: statusElement.dataset.state, text: statusElement.textContent, title: statusElement.title };\n` +
    `};`, sandbox);

for (const fixture of [
    { displayState: 'online', displayText: '李永房 · 主播甲' },
    { displayState: 'reconnecting', displayText: '李永房 · 重连中' },
    { displayState: 'offline', displayText: '李永房 · 离线' },
    { displayState: 'not-joined', displayText: '未加入云端房间' }
]) {
    const rendered = sandbox.renderFixture(fixture);
    requireCondition(rendered.state === fixture.displayState,
        `Expected ${fixture.displayState}, got ${rendered.state}.`);
    requireCondition(rendered.text === fixture.displayText,
        `Expected visible text ${fixture.displayText}.`);
    requireCondition(rendered.title === fixture.displayText,
        `Expected full tooltip ${fixture.displayText}.`);
}

console.log('Cloud match status Web runtime tests passed.');
