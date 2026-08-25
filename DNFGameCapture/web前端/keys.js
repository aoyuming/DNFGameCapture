const KEY_MAPPING_STATE_URL = 'http://127.0.0.1:18777/api/key-mapping-state';
const SLOT_COUNT = 14;

const grid = document.getElementById('key-grid');
const stage = document.getElementById('key-stage');
const shellControls = document.getElementById('shell-controls');
const resizeButton = document.getElementById('key-window-resize');
const isShell = new URLSearchParams(window.location.search).get('shell') === '1';
const cells = [];
let polling = false;

function createGrid() {
    const fragment = document.createDocumentFragment();
    for (let index = 0; index < SLOT_COUNT; index += 1) {
        const cell = document.createElement('div');
        cell.className = 'key-highlight';
        cell.dataset.index = String(index);
        fragment.appendChild(cell);
        cells.push(cell);
    }
    grid.replaceChildren(fragment);
}

function normalizeColor(value) {
    return /^#[0-9a-f]{6}$/i.test(String(value || '')) ? String(value) : '#00e5ff';
}

function renderState(state = {}) {
    const enabled = state.enabled === true;
    const activeMask = enabled ? Number(state.activeMask || 0) : 0;
    const slots = Array.isArray(state.slots) ? state.slots : [];
    cells.forEach((cell, index) => {
        const slot = slots[index] || {};
        const active = (activeMask & (1 << index)) !== 0;
        cell.style.backgroundColor = normalizeColor(slot.color);
        cell.style.opacity = active ? String(Math.max(0, Math.min(100, Number(slot.opacity ?? 42))) / 100) : '0';
    });
}

async function pollState() {
    if (polling) return;
    polling = true;
    try {
        const response = await fetch(`${KEY_MAPPING_STATE_URL}?t=${Date.now()}`, { cache: 'no-store' });
        if (response.ok) renderState(await response.json());
    } catch (_) {
        renderState({ enabled: false, activeMask: 0 });
    } finally {
        polling = false;
    }
}

function postShellCommand(action) {
    if (!isShell || !window.chrome?.webview) return;
    window.chrome.webview.postMessage({ action });
}

function setupShellControls() {
    if (!isShell) return;
    document.body.classList.add('shell-mode');
    shellControls.hidden = false;
    resizeButton.hidden = false;
    document.getElementById('key-window-close').addEventListener('click', () => postShellCommand('cmd_key_window_close'));
    document.getElementById('key-window-drag').addEventListener('pointerdown', (event) => {
        event.preventDefault();
        postShellCommand('cmd_key_window_drag');
    });
    resizeButton.addEventListener('pointerdown', (event) => {
        event.preventDefault();
        postShellCommand('cmd_key_window_resize');
    });
    stage.addEventListener('dblclick', (event) => {
        if (event.target === stage || event.target === grid) postShellCommand('cmd_key_window_drag');
    });
}

createGrid();
setupShellControls();
renderState();
pollState();
setInterval(pollState, 33);
