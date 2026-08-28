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
const defaultServerUrl = source.match(
    /const CLOUD_MATCH_DEFAULT_SERVER_URL = [^;]+;/)?.[0];
const displayStates = source.match(
    /const CLOUD_MATCH_DISPLAY_STATES = new Set\([^;]+;/s)?.[0];
requireCondition(roomNames && defaultServerUrl && displayStates,
    'Cloud match Web declarations are missing.');

const sandbox = {};
vm.createContext(sandbox);
vm.runInContext(
    `${roomNames}\n${defaultServerUrl}\n${displayStates}\n` +
    `${extractFunction(source, 'normalizeCloudSyncPanel')}\n` +
    `${extractFunction(source, 'normalizeCloudMatchState')}\n` +
    `${extractFunction(source, 'cloudOfflineRemaining')}\n` +
    `this.normalizeFixture = normalizeCloudMatchState;\n` +
    `this.offlineRemaining = cloudOfflineRemaining;\n`, sandbox);

const normalized = sandbox.normalizeFixture({
    joined: true,
    roomId: 'all-broadcasters',
    connected: true,
    unifiedPool: {
        joined: true,
        deviceId: 'device-self-0001',
        broadcasterName: 'Broadcaster A'
    },
    broadcasters: [{ deviceId: 'device-peer-0002', online: true }],
    syncHistory: { all: [{ id: 1 }] },
    realtimeRelations: [{ viewerDeviceId: 'device-self-0001' }]
});
requireCondition(normalized.joined === true, 'Unified pool must normalize as joined.');
requireCondition(normalized.roomId === 'all-broadcasters', 'Visible rooms must normalize to the unified pool.');
requireCondition(normalized.roomName === '', 'Unified Web state must not expose a room label.');
requireCondition(normalized.broadcasterName === 'Broadcaster A', 'Broadcaster identity was not normalized.');
requireCondition(normalized.broadcasters.length === 1, 'Broadcaster directory was lost.');
requireCondition(normalized.syncHistory.all.length === 1, 'Global sync history was lost.');
requireCondition(normalized.realtimeRelations.length === 1, 'Realtime relations were lost.');

const remaining = sandbox.offlineRemaining(Math.floor(Date.now() / 1000) + 3600);
requireCondition(typeof remaining === 'string' && remaining.length > 0,
    'Offline broadcaster retention countdown is missing.');

console.log('Unified broadcaster Web state tests passed.');
