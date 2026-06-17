const KILL_STATE_URL = 'http://127.0.0.1:18777/api/state';

const KILL_DISPLAY_LAYOUT_DEFAULTS = {
    bgAlpha: 0,
    panelAlpha: 49,
    rowAlpha: 0,
    canvasPadding: 0,
    panelPadding: 14,
    teamGap: 0,
    rowGap: 0,
    rowHeight: 48,
    panelRadius: 0,
    rowRadius: 0,
    boardBorder: 0,
    shadow: 0,
    pickColumnWidth: 54,
    statColumnWidth: 61,
    akColumnWidth: 24
};

const KILL_DISPLAY_TEXT_STYLE_TYPES = [
    {
        key: 'teamName',
        cssKey: 'team-name',
        allowTeamColor: true,
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 49, colorMode: 'team', color: '#ffffff', strokeColor: '#000000', strokeWidth: 4, glow: 0 }
    },
    {
        key: 'score',
        cssKey: 'score',
        allowTeamColor: true,
        defaults: { fontFamily: 'Arial Black', fontSize: 70, colorMode: 'team', color: '#ffffff', strokeColor: '#000000', strokeWidth: 3, glow: 2 }
    },
    {
        key: 'header',
        cssKey: 'header',
        allowTeamColor: false,
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 31, colorMode: 'custom', color: '#a9abb9', strokeColor: '#000000', strokeWidth: 2, glow: 0 }
    },
    {
        key: 'pickLabel',
        cssKey: 'pick-label',
        allowTeamColor: false,
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 27, colorMode: 'custom', color: '#a1a1a1', strokeColor: '#000000', strokeWidth: 2, glow: 0 }
    },
    {
        key: 'playerName',
        cssKey: 'player-name',
        allowTeamColor: false,
        defaults: { fontFamily: 'Arial Black', fontSize: 43, colorMode: 'custom', color: '#f7ca69', strokeColor: '#000000', strokeWidth: 5, glow: 2 }
    },
    {
        key: 'statNumber',
        cssKey: 'stat-number',
        allowTeamColor: false,
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 50, colorMode: 'custom', color: '#f7ca69', strokeColor: '#000000', strokeWidth: 4, glow: 0 }
    },
    {
        key: 'akMark',
        cssKey: 'ak-mark',
        allowTeamColor: false,
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 40, colorMode: 'custom', color: '#f7ca69', strokeColor: '#000000', strokeWidth: 1, glow: 0 }
    }
];

let lastStateSignature = '';
let lastStatusText = '';
let statusTimer = null;

function clampNumber(value, min, max, fallback) {
    const n = Number.parseInt(value, 10);
    if (!Number.isFinite(n)) return fallback;
    return Math.max(min, Math.min(max, n));
}

function normalizeHexColor(value, fallback = '#ffffff') {
    let raw = String(value || '').trim().toLowerCase();
    if (/^#[0-9a-f]{3}$/.test(raw)) {
        raw = `#${raw[1]}${raw[1]}${raw[2]}${raw[2]}${raw[3]}${raw[3]}`;
    }
    return /^#[0-9a-f]{6}$/.test(raw) ? raw : fallback;
}

function hexToRgbParts(hex) {
    const color = normalizeHexColor(hex, '#ffffff');
    return [
        Number.parseInt(color.slice(1, 3), 16),
        Number.parseInt(color.slice(3, 5), 16),
        Number.parseInt(color.slice(5, 7), 16)
    ].join(', ');
}

function cleanFontFamilyName(value, fallback = 'Microsoft YaHei') {
    const cleaned = String(value || '')
        .replace(/["'\\\r\n]/g, '')
        .trim()
        .slice(0, 80);
    return cleaned || fallback;
}

function cssFontFamily(value) {
    const font = cleanFontFamilyName(value);
    return `"${font}", "Microsoft YaHei", sans-serif`;
}

function postKillHostCommand(action) {
    if (!window.chrome?.webview) return;
    window.chrome.webview.postMessage({ action });
}

function bindWindowControls() {
    const root = document.getElementById('kill-display-root');
    const grip = document.getElementById('kill-resize-grip');
    if (root) {
        root.addEventListener('mousedown', (event) => {
            if (event.button !== 0) return;
            if (event.target?.closest?.('.kill-resize-grip')) return;
            event.preventDefault();
            postKillHostCommand('cmd_kill_window_drag');
        });
    }
    if (grip) {
        grip.addEventListener('mousedown', (event) => {
            if (event.button !== 0) return;
            event.preventDefault();
            event.stopPropagation();
            postKillHostCommand('cmd_kill_window_resize');
        });
    }
}

function getKillStyleType(key) {
    return KILL_DISPLAY_TEXT_STYLE_TYPES.find(t => t.key === key) || KILL_DISPLAY_TEXT_STYLE_TYPES[0];
}

function normalizeKillDisplayTextStyle(key, value = {}) {
    const type = getKillStyleType(key);
    const defaults = type.defaults;
    const style = value && typeof value === 'object' ? value : {};
    const requestedColorMode = String(style.colorMode || defaults.colorMode);
    return {
        fontFamily: cleanFontFamilyName(style.fontFamily, defaults.fontFamily),
        fontSize: clampNumber(style.fontSize, 10, 76, defaults.fontSize),
        colorMode: type.allowTeamColor ? (requestedColorMode === 'custom' ? 'custom' : 'team') : 'custom',
        color: normalizeHexColor(style.color, defaults.color),
        strokeColor: normalizeHexColor(style.strokeColor, defaults.strokeColor),
        strokeWidth: clampNumber(style.strokeWidth, 0, 8, defaults.strokeWidth),
        glow: clampNumber(style.glow, 0, 36, defaults.glow)
    };
}

function normalizeKillDisplayTextStyles(styles = {}) {
    const normalized = {};
    KILL_DISPLAY_TEXT_STYLE_TYPES.forEach(type => {
        normalized[type.key] = normalizeKillDisplayTextStyle(type.key, styles?.[type.key]);
    });
    return normalized;
}

function normalizeKillDisplayLayout(layout = {}) {
    return {
        bgAlpha: clampNumber(layout.bgAlpha, 0, 100, KILL_DISPLAY_LAYOUT_DEFAULTS.bgAlpha),
        panelAlpha: clampNumber(layout.panelAlpha, 0, 100, KILL_DISPLAY_LAYOUT_DEFAULTS.panelAlpha),
        rowAlpha: clampNumber(layout.rowAlpha, 0, 100, KILL_DISPLAY_LAYOUT_DEFAULTS.rowAlpha),
        canvasPadding: clampNumber(layout.canvasPadding, 0, 40, KILL_DISPLAY_LAYOUT_DEFAULTS.canvasPadding),
        panelPadding: clampNumber(layout.panelPadding, 0, 40, KILL_DISPLAY_LAYOUT_DEFAULTS.panelPadding),
        teamGap: clampNumber(layout.teamGap, 0, 40, KILL_DISPLAY_LAYOUT_DEFAULTS.teamGap),
        rowGap: clampNumber(layout.rowGap, 0, 20, KILL_DISPLAY_LAYOUT_DEFAULTS.rowGap),
        rowHeight: clampNumber(layout.rowHeight, 32, 90, KILL_DISPLAY_LAYOUT_DEFAULTS.rowHeight),
        panelRadius: clampNumber(layout.panelRadius, 0, 28, KILL_DISPLAY_LAYOUT_DEFAULTS.panelRadius),
        rowRadius: clampNumber(layout.rowRadius, 0, 22, KILL_DISPLAY_LAYOUT_DEFAULTS.rowRadius),
        boardBorder: clampNumber(layout.boardBorder, 0, 6, KILL_DISPLAY_LAYOUT_DEFAULTS.boardBorder),
        shadow: clampNumber(layout.shadow, 0, 48, KILL_DISPLAY_LAYOUT_DEFAULTS.shadow),
        pickColumnWidth: clampNumber(layout.pickColumnWidth, 36, 110, KILL_DISPLAY_LAYOUT_DEFAULTS.pickColumnWidth),
        statColumnWidth: clampNumber(layout.statColumnWidth, 28, 90, KILL_DISPLAY_LAYOUT_DEFAULTS.statColumnWidth),
        akColumnWidth: clampNumber(layout.akColumnWidth, 24, 80, KILL_DISPLAY_LAYOUT_DEFAULTS.akColumnWidth)
    };
}

function getDefaultKillDisplaySettings() {
    return {
        layout: { ...KILL_DISPLAY_LAYOUT_DEFAULTS },
        textStyles: KILL_DISPLAY_TEXT_STYLE_TYPES.reduce((acc, type) => {
            acc[type.key] = { ...type.defaults };
            return acc;
        }, {})
    };
}

function setStyleVars(type, style) {
    const root = document.documentElement;
    const base = `--kill-${type.cssKey}`;
    root.style.setProperty(`${base}-font-family`, cssFontFamily(style.fontFamily));
    root.style.setProperty(`${base}-font-size`, `${style.fontSize}px`);
    root.style.setProperty(`${base}-stroke-color`, style.strokeColor);
    root.style.setProperty(`${base}-stroke-width`, `${style.strokeWidth}px`);
    root.style.setProperty(`${base}-glow`, `${style.glow}px`);

    if (type.allowTeamColor && style.colorMode === 'team') {
        root.style.setProperty(`${base}-red-color`, '#ff1264');
        root.style.setProperty(`${base}-blue-color`, '#00e6ff');
        root.style.setProperty(`${base}-red-glow-rgb`, '255, 18, 100');
        root.style.setProperty(`${base}-blue-glow-rgb`, '0, 230, 255');
    } else if (type.allowTeamColor) {
        root.style.setProperty(`${base}-red-color`, style.color);
        root.style.setProperty(`${base}-blue-color`, style.color);
        root.style.setProperty(`${base}-red-glow-rgb`, hexToRgbParts(style.color));
        root.style.setProperty(`${base}-blue-glow-rgb`, hexToRgbParts(style.color));
    } else {
        root.style.setProperty(`${base}-color`, style.color);
        root.style.setProperty(`${base}-glow-rgb`, hexToRgbParts(style.color));
    }
}

function applyKillDisplaySettings(settings = getDefaultKillDisplaySettings()) {
    const normalized = {
        layout: normalizeKillDisplayLayout(settings.layout),
        textStyles: normalizeKillDisplayTextStyles(settings.textStyles)
    };
    const root = document.documentElement;
    const layout = normalized.layout;
    root.style.setProperty('--kill-bg-alpha', String(layout.bgAlpha / 100));
    root.style.setProperty('--kill-panel-alpha', String(layout.panelAlpha / 100));
    root.style.setProperty('--kill-row-alpha', String(layout.rowAlpha / 100));
    root.style.setProperty('--kill-canvas-padding', `${layout.canvasPadding}px`);
    root.style.setProperty('--kill-panel-padding', `${layout.panelPadding}px`);
    root.style.setProperty('--kill-team-gap', `${layout.teamGap}px`);
    root.style.setProperty('--kill-row-gap', `${layout.rowGap}px`);
    root.style.setProperty('--kill-row-height', `${layout.rowHeight}px`);
    root.style.setProperty('--kill-panel-radius', `${layout.panelRadius}px`);
    root.style.setProperty('--kill-row-radius', `${layout.rowRadius}px`);
    root.style.setProperty('--kill-border-width', `${layout.boardBorder}px`);
    root.style.setProperty('--kill-shadow', `${layout.shadow}px`);
    root.style.setProperty('--kill-pick-width', `${layout.pickColumnWidth}px`);
    root.style.setProperty('--kill-stat-width', `${layout.statColumnWidth}px`);
    root.style.setProperty('--kill-ak-width', `${layout.akColumnWidth}px`);

    KILL_DISPLAY_TEXT_STYLE_TYPES.forEach(type => {
        setStyleVars(type, normalized.textStyles[type.key]);
    });
}

function displaySeatLabel(label) {
    const clean = String(label || '').trim();
    if (clean === 'x选') return '先选';
    if (clean === 'h选') return '后选';
    return clean || '-';
}

function normalizePlayer(p, fallbackTeam, index) {
    return {
        team: Number.isFinite(Number(p?.team)) ? Number(p.team) : fallbackTeam,
        name: String(p?.name || '').trim() || `空位${index + 1}`,
        seatLabel: displaySeatLabel(p?.seatLabel || ''),
        kills: clampNumber(p?.kills, 0, 999, 0),
        deaths: clampNumber(p?.deaths, 0, 999, 0),
        akCount: clampNumber(p?.akCount, 0, 999, 0)
    };
}

function getTeamPlayers(players, team) {
    const filtered = (Array.isArray(players) ? players : [])
        .filter(p => Number(p?.team) === team)
        .map((p, index) => normalizePlayer(p, team, index));
    while (filtered.length < 4) {
        filtered.push(normalizePlayer({}, team, filtered.length));
    }
    return filtered.slice(0, 4);
}

function renderRows(container, players) {
    if (!container) return;
    container.innerHTML = '';
    players.forEach(player => {
        const row = document.createElement('div');
        row.className = 'kill-row';
        row.innerHTML = `
            <span class="kill-pick-label"></span>
            <span class="kill-player-name"></span>
            <span class="kill-stat-number"></span>
            <span class="kill-stat-number"></span>
            <span class="kill-ak-mark"></span>
        `;
        const cells = row.children;
        cells[0].textContent = player.seatLabel;
        cells[1].textContent = player.name;
        cells[2].textContent = String(player.kills);
        cells[3].textContent = String(player.deaths);
        cells[4].textContent = player.akCount > 0 ? String(player.akCount) : '-';
        container.appendChild(row);
    });
}

function setPanelIdentity(section, team) {
    if (!section) return;
    section.classList.toggle('kill-team-red', team === 'red');
    section.classList.toggle('kill-team-blue', team === 'blue');
    section.dataset.teamColor = team;
    const name = section.querySelector('.kill-team-name');
    if (name) name.textContent = team === 'red' ? '红队' : '蓝队';
}

function renderKillDisplay(data = {}) {
    const players = Array.isArray(data.players) ? data.players : [];
    const redPlayers = getTeamPlayers(players, 0);
    const bluePlayers = getTeamPlayers(players, 1);
    const flipped = data.isFlipped === true;
    const sections = document.querySelectorAll('.kill-team');
    const first = sections[0];
    const second = sections[1];
    const firstTeam = flipped ? 'blue' : 'red';
    const secondTeam = flipped ? 'red' : 'blue';

    setPanelIdentity(first, firstTeam);
    setPanelIdentity(second, secondTeam);

    const firstScore = first?.querySelector('.kill-team-score');
    const secondScore = second?.querySelector('.kill-team-score');
    if (firstScore) firstScore.textContent = String(firstTeam === 'red' ? (data.redScore || 0) : (data.blueScore || 0));
    if (secondScore) secondScore.textContent = String(secondTeam === 'red' ? (data.redScore || 0) : (data.blueScore || 0));

    renderRows(first?.querySelector('.kill-rows'), firstTeam === 'red' ? redPlayers : bluePlayers);
    renderRows(second?.querySelector('.kill-rows'), secondTeam === 'red' ? redPlayers : bluePlayers);
}

function showStatus(text, sticky = false) {
    const status = document.getElementById('kill-status');
    if (!status || text === lastStatusText) return;
    lastStatusText = text;
    status.textContent = text;
    status.classList.add('active');
    clearTimeout(statusTimer);
    if (!sticky) {
        statusTimer = setTimeout(() => status.classList.remove('active'), 1600);
    }
}

async function fetchKillDisplayState() {
    try {
        const response = await fetch(KILL_STATE_URL, { cache: 'no-store' });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const payload = await response.json();
        const data = payload?.data || payload;
        const signature = JSON.stringify(data);
        if (signature !== lastStateSignature) {
            lastStateSignature = signature;
            applyKillDisplaySettings(data.killDisplaySettings || getDefaultKillDisplaySettings());
            renderKillDisplay(data);
        }
        if (lastStatusText) showStatus('', false);
    } catch (err) {
        showStatus('等待主程序数据...', true);
    }
}

applyKillDisplaySettings(getDefaultKillDisplaySettings());
renderKillDisplay({});
bindWindowControls();
fetchKillDisplayState();
setInterval(fetchKillDisplayState, 250);
