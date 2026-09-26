const KILL_STATE_URL = 'http://127.0.0.1:18777/api/state';
const KILL_SETTINGS_URL = 'http://127.0.0.1:18777/api/kill-display-settings';

// 展示页风格（皮肤）：序号写入 layout.skin 并保存到 ini，顺序只能追加，不能调整。
// 皮肤只改变背景、边框和装饰；recommended 是「套用配色」时写入的推荐文字颜色（不改字体、字号、描边宽度）。
const KILL_DISPLAY_SKINS = [
    {
        id: 'dnf', label: 'DNF 金边',
        recommended: { header: '#c9a86a', pickLabel: '#6fc8b9', playerName: '#f7ca69', killNumber: '#f7ca69', deathNumber: '#ab986d', akMark: '#f7d67e', akCountBadge: '#f7d67e' }
    },
    {
        id: 'classic', label: '经典简洁',
        recommended: { header: '#9aa0b4', pickLabel: '#a6b7bf', playerName: '#ffffff', killNumber: '#ffffff', deathNumber: '#b8bcc8', akMark: '#ffd36a', akCountBadge: '#ffd36a' }
    },
    {
        id: 'neon', label: '电竞霓虹',
        recommended: { header: '#7f9cc8', pickLabel: '#7cf7d4', playerName: '#eafcff', killNumber: '#00e5ff', deathNumber: '#ff5fae', akMark: '#ffe45c', akCountBadge: '#ffe45c' },
        glow: { killNumber: 8, akMark: 6 }
    },
    {
        id: 'ink', label: '水墨国风',
        recommended: { header: '#b9ab8f', pickLabel: '#c8a86a', playerName: '#f2ead8', killNumber: '#f2ead8', deathNumber: '#9c9486', akMark: '#e0473a', akCountBadge: '#e0473a' }
    },
    {
        id: 'glass', label: '玻璃极简',
        recommended: { header: '#d7e3f5', pickLabel: '#bfe3ff', playerName: '#ffffff', killNumber: '#ffffff', deathNumber: '#c6d0e0', akMark: '#ffd76a', akCountBadge: '#ffd76a' }
    },
    {
        id: 'pixel', label: '像素街机',
        recommended: { header: '#fce0a8', pickLabel: '#80d010', playerName: '#f8f8f8', killNumber: '#fce0a8', deathNumber: '#a4a4a4', akMark: '#ff7a3c', akCountBadge: '#ff7a3c' }
    },
    {
        id: 'inferno', label: '烈焰熔岩',
        recommended: { header: '#d9a066', pickLabel: '#ff8a47', playerName: '#ffe7b8', killNumber: '#ffb347', deathNumber: '#b98a6a', akMark: '#ffd23f', akCountBadge: '#ffd23f' },
        glow: { killNumber: 10, akMark: 8 }
    },
    {
        id: 'broadcast', label: '赛事转播',
        recommended: { header: '#8fa0b8', pickLabel: '#9fb4d0', playerName: '#ffffff', killNumber: '#ffffff', deathNumber: '#9aa6b8', akMark: '#ffc62b', akCountBadge: '#ffc62b' }
    },
    {
        id: 'frost', label: '寒冰水晶',
        recommended: { header: '#9cc4dc', pickLabel: '#9fe8ff', playerName: '#eaf8ff', killNumber: '#bff0ff', deathNumber: '#8fb3c8', akMark: '#d6f6ff', akCountBadge: '#d6f6ff' },
        glow: { killNumber: 6, akMark: 8 }
    }
];
const KILL_DISPLAY_SKIN_MAX = KILL_DISPLAY_SKINS.length - 1;

function getKillDisplaySkin(index) {
    return KILL_DISPLAY_SKINS[clampNumber(index, 0, KILL_DISPLAY_SKIN_MAX, 0)] || KILL_DISPLAY_SKINS[0];
}

const KILL_DISPLAY_LAYOUT_DEFAULTS = {
    skin: 0,
    bgImageRev: 0,
    bgImageScale: 100,
    bgImageX: 0,
    bgImageY: 0,
    bgImageOpacity: 100,
    fxEnabled: 1,
    showDeathNumber: 0,
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
    akColumnWidth: 24,
    pageScale: 100,
    teamNameOffsetX: -9,
    teamNameOffsetY: -9,
    pickLabelOffsetX: -7,
    pickLabelOffsetY: 0,
    playerNameOffsetX: 0,
    playerNameOffsetY: 0,
    killNumberOffsetX: -7,
    killNumberOffsetY: 0,
    deathNumberOffsetX: -11,
    deathNumberOffsetY: 0,
    akMarkOffsetX: 2,
    akMarkOffsetY: 0,
    akCountBadgeOffsetX: 12,
    akCountBadgeOffsetY: -26
};

const KILL_DISPLAY_LAYOUT_LIMITS = {
    skin: [0, KILL_DISPLAY_SKIN_MAX],
    bgImageRev: [0, 2147483647],
    bgImageScale: [10, 400],
    bgImageX: [-3000, 3000],
    bgImageY: [-3000, 3000],
    bgImageOpacity: [0, 100],
    fxEnabled: [0, 1],
    showDeathNumber: [0, 1],
    bgAlpha: [0, 100],
    panelAlpha: [0, 100],
    rowAlpha: [0, 100],
    canvasPadding: [0, 40],
    panelPadding: [0, 40],
    teamGap: [0, 40],
    rowGap: [0, 20],
    rowHeight: [32, 90],
    panelRadius: [0, 28],
    rowRadius: [0, 22],
    boardBorder: [0, 6],
    shadow: [0, 48],
    pickColumnWidth: [36, 120],
    statColumnWidth: [28, 110],
    akColumnWidth: [24, 90],
    pageScale: [60, 180],
    teamNameOffsetX: [-180, 180],
    teamNameOffsetY: [-120, 120],
    pickLabelOffsetX: [-180, 180],
    pickLabelOffsetY: [-120, 120],
    playerNameOffsetX: [-180, 180],
    playerNameOffsetY: [-120, 120],
    killNumberOffsetX: [-180, 180],
    killNumberOffsetY: [-120, 120],
    deathNumberOffsetX: [-180, 180],
    deathNumberOffsetY: [-120, 120],
    akMarkOffsetX: [-180, 180],
    akMarkOffsetY: [-120, 120],
    akCountBadgeOffsetX: [-80, 80],
    akCountBadgeOffsetY: [-80, 80]
};

const KILL_DISPLAY_TEXT_STYLE_TYPES = [
    {
        key: 'teamName',
        cssKey: 'team-name',
        label: '队伍名',
        allowTeamColor: true,
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 54, colorMode: 'team', color: '#ffffff', strokeColor: '#000000', strokeWidth: 4, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'score',
        cssKey: 'score',
        label: '比分',
        allowTeamColor: true,
        defaults: { fontFamily: 'Arial Black', fontSize: 70, colorMode: 'team', color: '#ffffff', strokeColor: '#000000', strokeWidth: 3, glow: 2, letterSpacing: 0 }
    },
    {
        key: 'header',
        cssKey: 'header',
        label: '表头',
        allowTeamColor: false,
        defaults: { fontFamily: 'FZXS24', fontSize: 31, colorMode: 'custom', color: '#c9a86a', strokeColor: '#000000', strokeWidth: 2, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'pickLabel',
        cssKey: 'pick-label',
        label: '选人顺序',
        allowTeamColor: false,
        defaults: { fontFamily: 'FZXS24', fontSize: 27, colorMode: 'custom', color: '#6fc8b9', strokeColor: '#000000', strokeWidth: 3, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'playerName',
        cssKey: 'player-name',
        label: '选手',
        allowTeamColor: false,
        defaults: { fontFamily: 'Arial', fontSize: 43, colorMode: 'custom', color: '#f7ca69', strokeColor: '#000000', strokeWidth: 5, glow: 2, letterSpacing: 0 }
    },
    {
        key: 'killNumber',
        cssKey: 'kill-number',
        label: '杀',
        allowTeamColor: false,
        defaults: { fontFamily: 'FZXS24', fontSize: 50, colorMode: 'custom', color: '#f7ca69', strokeColor: '#000000', strokeWidth: 4, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'deathNumber',
        cssKey: 'death-number',
        label: '死',
        allowTeamColor: false,
        defaults: { fontFamily: 'FZXS24', fontSize: 50, colorMode: 'custom', color: '#ab986d', strokeColor: '#000000', strokeWidth: 4, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'akMark',
        cssKey: 'ak-mark',
        label: 'AK',
        allowTeamColor: false,
        defaults: { fontFamily: 'FZXS24', fontSize: 40, colorMode: 'custom', color: '#f7d67e', strokeColor: '#000000', strokeWidth: 3, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'akCountBadge',
        cssKey: 'ak-count',
        label: 'AK次数',
        allowTeamColor: false,
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 30, colorMode: 'custom', color: '#f7d67e', strokeColor: '#000000', strokeWidth: 1, glow: 0, letterSpacing: 0 }
    }
];

const EDITABLE_STYLE_KEYS = ['teamName', 'pickLabel', 'playerName', 'killNumber', 'deathNumber', 'akMark', 'akCountBadge'];
const PAGE_EDIT_KEY = 'page';
const KILL_STYLE_OFFSET_KEYS = {
    teamName: ['teamNameOffsetX', 'teamNameOffsetY'],
    pickLabel: ['pickLabelOffsetX', 'pickLabelOffsetY'],
    playerName: ['playerNameOffsetX', 'playerNameOffsetY'],
    killNumber: ['killNumberOffsetX', 'killNumberOffsetY'],
    deathNumber: ['deathNumberOffsetX', 'deathNumberOffsetY'],
    akMark: ['akMarkOffsetX', 'akMarkOffsetY'],
    akCountBadge: ['akCountBadgeOffsetX', 'akCountBadgeOffsetY']
};
const KILL_HORIZONTAL_ONLY_OFFSET_Y_KEYS = [
    'pickLabelOffsetY',
    'playerNameOffsetY',
    'killNumberOffsetY',
    'deathNumberOffsetY',
    'akMarkOffsetY'
];

let lastStateSignature = '';
let lastStatusText = '';
let statusTimer = null;
let saveSettingsTimer = null;
let killDisplaySettings = getDefaultKillDisplaySettings();
let systemFonts = [];
let killEditMode = false;
let selectedKillStyleKey = 'playerName';
let dragLayoutState = null;
let suppressClickAfterDrag = false;
let suppressRemoteKillSettingsUntil = 0;
let isSavingKillSettings = false;

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

function getKillStyleType(key) {
    return KILL_DISPLAY_TEXT_STYLE_TYPES.find(t => t.key === key) || KILL_DISPLAY_TEXT_STYLE_TYPES[0];
}

function normalizeSystemFonts(fonts = []) {
    const seen = new Set();
    const result = [];
    [...(Array.isArray(fonts) ? fonts : []), 'Microsoft YaHei', 'SimHei', 'Arial Black', 'Arial'].forEach(name => {
        const cleaned = cleanFontFamilyName(name, '');
        const key = cleaned.toLowerCase();
        if (!cleaned || seen.has(key)) return;
        seen.add(key);
        result.push(cleaned);
    });
    return result;
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
        glow: clampNumber(style.glow, 0, 36, defaults.glow),
        letterSpacing: clampNumber(style.letterSpacing, -4, 16, defaults.letterSpacing || 0)
    };
}

function normalizeKillDisplayTextStyles(styles = {}) {
    const normalized = {};
    KILL_DISPLAY_TEXT_STYLE_TYPES.forEach(type => {
        const legacyStatStyle = (type.key === 'killNumber' || type.key === 'deathNumber') ? styles?.statNumber : null;
        normalized[type.key] = normalizeKillDisplayTextStyle(type.key, styles?.[type.key] || legacyStatStyle);
    });
    return normalized;
}

function normalizeKillDisplayLayout(layout = {}) {
    const normalized = {};
    Object.entries(KILL_DISPLAY_LAYOUT_DEFAULTS).forEach(([key, fallback]) => {
        const [min, max] = KILL_DISPLAY_LAYOUT_LIMITS[key];
        normalized[key] = key === 'showDeathNumber'
            ? (clampNumber(layout?.[key], min, max, fallback) ? 1 : 0)
            : KILL_HORIZONTAL_ONLY_OFFSET_Y_KEYS.includes(key)
            ? 0
            : clampNumber(layout?.[key], min, max, fallback);
    });
    return normalized;
}

function getDefaultKillDisplaySettings() {
    return {
        obsUrl: 'http://127.0.0.1:18777/kill.html',
        layout: { ...KILL_DISPLAY_LAYOUT_DEFAULTS },
        textStyles: KILL_DISPLAY_TEXT_STYLE_TYPES.reduce((acc, type) => {
            acc[type.key] = { ...type.defaults };
            return acc;
        }, {})
    };
}

function getStyleOffsetKeys(styleKey) {
    return KILL_STYLE_OFFSET_KEYS[styleKey] || null;
}

function getStyleOffsetForKey(styleKey) {
    const keys = getStyleOffsetKeys(styleKey);
    if (!keys) return { x: 0, y: 0 };
    return {
        x: killDisplaySettings.layout?.[keys[0]] || 0,
        y: killDisplaySettings.layout?.[keys[1]] || 0
    };
}

function getHiddenDeathStyleOffsetDelta(styleKey) {
    if (killDisplaySettings.layout?.showDeathNumber === 1) return { x: 0, y: 0 };
    if (styleKey === 'killNumber') return { x: 14, y: 0 };
    if (styleKey === 'akMark') return { x: -8, y: 0 };
    return { x: 0, y: 0 };
}

function isVerticalLayoutDragAllowed(styleKey) {
    return styleKey === 'teamName' || styleKey === 'akCountBadge';
}

function setOffsetVars(styleKey) {
    const type = getKillStyleType(styleKey);
    const keys = getStyleOffsetKeys(styleKey);
    if (!keys) return;
    const root = document.documentElement;
    root.style.setProperty(`--kill-${type.cssKey}-offset-x`, `${killDisplaySettings.layout[keys[0]] || 0}px`);
    root.style.setProperty(`--kill-${type.cssKey}-offset-y`, `${killDisplaySettings.layout[keys[1]] || 0}px`);
}

function setStyleVars(type, style) {
    const root = document.documentElement;
    const base = `--kill-${type.cssKey}`;
    root.style.setProperty(`${base}-font-family`, cssFontFamily(style.fontFamily));
    root.style.setProperty(`${base}-font-size`, `${style.fontSize}px`);
    root.style.setProperty(`${base}-stroke-color`, style.strokeColor);
    root.style.setProperty(`${base}-stroke-width`, `${style.strokeWidth}px`);
    root.style.setProperty(`${base}-glow`, `${style.glow}px`);
    root.style.setProperty(`${base}-letter-spacing`, `${style.letterSpacing}px`);

    if (type.allowTeamColor && style.colorMode === 'team') {
        root.style.setProperty(`${base}-red-color`, '#ff4646');
        root.style.setProperty(`${base}-blue-color`, '#38b6ff');
        root.style.setProperty(`${base}-red-glow-rgb`, '255, 70, 70');
        root.style.setProperty(`${base}-blue-glow-rgb`, '56, 182, 255');
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
    const previousShowDeathNumber = killDisplaySettings?.layout?.showDeathNumber;
    const normalized = {
        obsUrl: String(settings?.obsUrl || 'http://127.0.0.1:18777/kill.html'),
        layout: normalizeKillDisplayLayout(settings?.layout || {}),
        textStyles: normalizeKillDisplayTextStyles(settings?.textStyles || {})
    };
    const shouldRelockBoard = killEditMode &&
        previousShowDeathNumber !== undefined &&
        previousShowDeathNumber !== normalized.layout.showDeathNumber;
    if (shouldRelockBoard) unlockKillBoardAfterEditMode();
    killDisplaySettings = normalized;

    const root = document.documentElement;
    const layout = normalized.layout;
    const displayRoot = document.getElementById('kill-display-root');
    displayRoot?.classList.toggle('hide-death-number', layout.showDeathNumber !== 1);
    if (displayRoot) displayRoot.dataset.skin = getKillDisplaySkin(layout.skin).id;
    applyKillCustomBackground(layout);
    syncKillSkinPanel();
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
    root.style.setProperty('--kill-page-scale', String(layout.pageScale / 100));
    EDITABLE_STYLE_KEYS.forEach(setOffsetVars);

    KILL_DISPLAY_TEXT_STYLE_TYPES.forEach(type => {
        setStyleVars(type, normalized.textStyles[type.key]);
    });
    syncKillEditToolbar();
    scheduleFitKillTextElements();
    if (shouldRelockBoard) relockKillBoardForEditMode();
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

function formatAkMark(akCount) {
    const value = clampNumber(akCount, 0, 999, 0);
    if (value === 0) return '-';
    return 'A';
}

function renderAkMark(container, akCount) {
    if (!container) return;
    const value = clampNumber(akCount, 0, 999, 0);
    container.classList.toggle('has-ak-count', value > 1);
    container.innerHTML = '';

    const mark = document.createElement('span');
    mark.className = 'kill-ak-symbol';
    mark.dataset.styleKey = 'akMark';
    mark.textContent = formatAkMark(value);
    container.appendChild(mark);

    if (value > 1) {
        const badge = document.createElement('span');
        badge.className = 'kill-ak-count';
        badge.dataset.styleKey = 'akCountBadge';
        badge.textContent = String(value);
        container.appendChild(badge);
    }
}

function renderRows(container, players) {
    if (!container) return;
    container.innerHTML = '';
    players.forEach(player => {
        const row = document.createElement('div');
        row.className = 'kill-row';
        row.innerHTML = `
            <span class="kill-pick-label" data-style-key="pickLabel" data-fit-text></span>
            <span class="kill-player-name" data-style-key="playerName" data-fit-text></span>
            <span class="kill-kill-number" data-style-key="killNumber" data-fit-text></span>
            <span class="kill-death-number" data-style-key="deathNumber" data-fit-text></span>
            <span class="kill-ak-mark" data-style-key="akMark" data-fit-text></span>
        `;
        const cells = row.children;
        cells[0].textContent = player.seatLabel;
        cells[1].textContent = player.name;
        cells[2].textContent = String(player.kills);
        cells[3].textContent = String(player.deaths);
        cells[3].classList.toggle('compact-death', player.deaths >= 10);
        renderAkMark(cells[4], player.akCount);
        container.appendChild(row);
    });
    refreshSelectedStyleMarker();
}

function setPanelIdentity(section, team) {
    if (!section) return;
    section.classList.toggle('kill-team-red', team === 'red');
    section.classList.toggle('kill-team-blue', team === 'blue');
    section.dataset.teamColor = team;
    const name = section.querySelector('.kill-team-name');
    if (name) {
        name.textContent = team === 'red' ? '红队' : '蓝队';
        name.dataset.styleKey = 'teamName';
        name.dataset.fitText = '';
    }
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
    if (firstScore) {
        firstScore.textContent = String(firstTeam === 'red' ? (data.redScore || 0) : (data.blueScore || 0));
        firstScore.dataset.styleKey = 'teamName';
        firstScore.dataset.fitText = '';
    }
    if (secondScore) {
        secondScore.textContent = String(secondTeam === 'red' ? (data.redScore || 0) : (data.blueScore || 0));
        secondScore.dataset.styleKey = 'teamName';
        secondScore.dataset.fitText = '';
    }

    document.querySelectorAll('.kill-stat-header span').forEach(item => {
        item.dataset.fitText = '';
    });

    renderRows(first?.querySelector('.kill-rows'), firstTeam === 'red' ? redPlayers : bluePlayers);
    renderRows(second?.querySelector('.kill-rows'), secondTeam === 'red' ? redPlayers : bluePlayers);
    scheduleFitKillTextElements();
}

function fitKillTextElements() {
    document.querySelectorAll('[data-fit-text]').forEach(item => {
        const available = item.clientWidth;
        const needed = getFitTextNeededWidth(item);
        let scale = 1;
        if (available > 2 && needed > available + 1) {
            scale = Math.max(0.46, Math.min(1, (available - 1) / needed));
        }
        applyFitTextTransform(item, scale);
    });
}

function getFitTextNeededWidth(item) {
    if (item?.classList?.contains('kill-ak-mark')) {
        const symbol = item.querySelector('.kill-ak-symbol');
        if (symbol) return symbol.scrollWidth || symbol.getBoundingClientRect().width || item.scrollWidth;
    }
    return item?.scrollWidth || 0;
}

function applyFitTextTransform(item, scale = 1) {
    const styleKey = item?.dataset?.styleKey || '';
    const offset = getStyleOffsetForKey(styleKey);
    const hiddenDeathDelta = getHiddenDeathStyleOffsetDelta(styleKey);
    const x = offset.x + hiddenDeathDelta.x;
    const y = offset.y + hiddenDeathDelta.y;
    const parts = [];
    if (x !== 0 || y !== 0) {
        parts.push(`translate(${x}px, ${y}px)`);
    }
    if (scale < 1) {
        parts.push(`scaleX(${scale})`);
    }
    item.style.transform = parts.join(' ');
}

function scheduleFitKillTextElements() {
    if (window.requestAnimationFrame) window.requestAnimationFrame(fitKillTextElements);
    else setTimeout(fitKillTextElements, 0);
}

function getLayoutControlForStyle(styleKey) {
    if (styleKey === PAGE_EDIT_KEY) return { key: 'pageScale', label: '整体缩放' };
    if (styleKey === 'pickLabel') return { key: 'pickColumnWidth', label: '选人列宽' };
    if (styleKey === 'killNumber' || styleKey === 'deathNumber') return { key: 'statColumnWidth', label: '战绩列宽' };
    if (styleKey === 'akMark') return { key: 'akColumnWidth', label: 'AK列宽' };
    if (styleKey === 'akCountBadge') return { key: 'akCountBadgeOffsetX', label: '次数横移' };
    if (styleKey === 'teamName') return { key: 'teamGap', label: '队伍间距' };
    return { key: 'rowGap', label: '行距' };
}

function selectedStyle() {
    if (!EDITABLE_STYLE_KEYS.includes(selectedKillStyleKey)) return null;
    const style = killDisplaySettings.textStyles?.[selectedKillStyleKey];
    return normalizeKillDisplayTextStyle(selectedKillStyleKey, style);
}

function setSelectedKillStyleKey(styleKey) {
    if (styleKey !== PAGE_EDIT_KEY && !EDITABLE_STYLE_KEYS.includes(styleKey)) return;
    selectedKillStyleKey = styleKey;
    syncKillEditToolbar();
    refreshSelectedStyleMarker();
}

function refreshSelectedStyleMarker() {
    document.querySelectorAll('.style-selected').forEach(item => item.classList.remove('style-selected'));
    if (!killEditMode) return;
    if (!EDITABLE_STYLE_KEYS.includes(selectedKillStyleKey)) return;
    document.querySelectorAll(`[data-style-key="${selectedKillStyleKey}"]`).forEach(item => {
        item.classList.add('style-selected');
    });
}

function populateFontList(selectedFont = '') {
    const select = document.getElementById('kill-edit-font');
    if (!select) return;

    const currentStyle = selectedStyle();
    const cleaned = cleanFontFamilyName(selectedFont || currentStyle?.fontFamily || '', 'Microsoft YaHei');
    const fonts = normalizeSystemFonts([...systemFonts, cleaned]);
    select.innerHTML = '';
    fonts.forEach(font => {
        const option = document.createElement('option');
        option.value = font;
        option.textContent = font;
        option.style.fontFamily = cssFontFamily(font);
        select.appendChild(option);
    });
    select.value = cleaned;
}

function syncKillEditToolbar() {
    const toolbar = document.getElementById('kill-edit-toolbar');
    if (!toolbar) return;

    const style = selectedStyle();
    populateFontList(style?.fontFamily || '');
    const isPageTarget = selectedKillStyleKey === PAGE_EDIT_KEY;
    const target = document.getElementById('kill-edit-target');
    const font = document.getElementById('kill-edit-font');
    const size = document.getElementById('kill-edit-size');
    const color = document.getElementById('kill-edit-color');
    const stroke = document.getElementById('kill-edit-stroke');
    const letter = document.getElementById('kill-edit-letter');
    const glow = document.getElementById('kill-edit-glow');
    const width = document.getElementById('kill-edit-width');
    const widthLabel = document.getElementById('kill-edit-width-label');
    const showDeath = document.getElementById('kill-edit-show-death');
    const skin = document.getElementById('kill-edit-skin');
    const layoutControl = getLayoutControlForStyle(selectedKillStyleKey);
    const [layoutMin, layoutMax] = KILL_DISPLAY_LAYOUT_LIMITS[layoutControl.key];

    if (target) target.value = selectedKillStyleKey;
    [font, size, color, stroke, letter, glow].forEach(control => {
        if (control) control.disabled = isPageTarget;
    });
    if (font) font.value = style?.fontFamily || '';
    if (size) size.value = style?.fontSize || '';
    if (color) color.value = style?.color || '#ffffff';
    if (stroke) stroke.value = style?.strokeWidth ?? '';
    if (letter) letter.value = style?.letterSpacing ?? '';
    if (glow) glow.value = style?.glow ?? '';
    if (width) {
        width.min = String(layoutMin);
        width.max = String(layoutMax);
        width.value = killDisplaySettings.layout[layoutControl.key];
    }
    if (widthLabel) widthLabel.textContent = layoutControl.label;
    if (showDeath) showDeath.checked = killDisplaySettings.layout.showDeathNumber === 1;
    if (skin) {
        if (skin.options.length !== KILL_DISPLAY_SKINS.length) populateKillSkinList();
        skin.value = String(clampNumber(killDisplaySettings.layout.skin, 0, KILL_DISPLAY_SKIN_MAX, 0));
    }
}

function populateKillSkinList() {
    const select = document.getElementById('kill-edit-skin');
    if (!select) return;
    select.innerHTML = '';
    KILL_DISPLAY_SKINS.forEach((skin, index) => {
        const option = document.createElement('option');
        option.value = String(index);
        option.textContent = skin.label;
        select.appendChild(option);
    });
}

function setKillDisplaySkin(index, save = true) {
    killDisplaySettings.layout.skin = clampNumber(index, 0, KILL_DISPLAY_SKIN_MAX, 0);
    applyKillDisplaySettings(killDisplaySettings);
    showStatus(`风格：${getKillDisplaySkin(killDisplaySettings.layout.skin).label}`, false);
    if (save) queueKillDisplaySettingsSave();
}

// 把当前风格的推荐文字颜色写入各文字对象；只改颜色（和少量发光），保留字体、字号、描边宽度、字距。
function applyKillSkinRecommendedColors(save = true) {
    const skin = getKillDisplaySkin(killDisplaySettings.layout.skin);
    Object.entries(skin.recommended || {}).forEach(([key, color]) => {
        const current = killDisplaySettings.textStyles?.[key];
        if (!current) return;
        const patch = { ...current, color, colorMode: 'custom' };
        patch.glow = skin.glow?.[key] ?? 0;
        killDisplaySettings.textStyles[key] = normalizeKillDisplayTextStyle(key, patch);
    });
    applyKillDisplaySettings(killDisplaySettings);
    showStatus(`已套用「${skin.label}」推荐配色，可继续单独微调`, false);
    if (save) queueKillDisplaySettingsSave();
}

function updateSelectedStyle(patch, save = true) {
    if (!EDITABLE_STYLE_KEYS.includes(selectedKillStyleKey)) return;
    const current = selectedStyle();
    const next = normalizeKillDisplayTextStyle(selectedKillStyleKey, { ...current, ...patch });
    killDisplaySettings.textStyles[selectedKillStyleKey] = next;
    applyKillDisplaySettings(killDisplaySettings);
    if (save) queueKillDisplaySettingsSave();
}

function updateSelectedLayout(value, save = true) {
    const layoutControl = getLayoutControlForStyle(selectedKillStyleKey);
    const [min, max] = KILL_DISPLAY_LAYOUT_LIMITS[layoutControl.key];
    killDisplaySettings.layout[layoutControl.key] = clampNumber(value, min, max, killDisplaySettings.layout[layoutControl.key]);
    applyKillDisplaySettings(killDisplaySettings);
    if (save) queueKillDisplaySettingsSave();
}

function updateKillLayoutOffset(styleKey, x, y, save = true) {
    const keys = getStyleOffsetKeys(styleKey);
    if (!keys) return;
    const [xMin, xMax] = KILL_DISPLAY_LAYOUT_LIMITS[keys[0]];
    const [yMin, yMax] = KILL_DISPLAY_LAYOUT_LIMITS[keys[1]];
    killDisplaySettings.layout[keys[0]] = clampNumber(x, xMin, xMax, killDisplaySettings.layout[keys[0]]);
    killDisplaySettings.layout[keys[1]] = clampNumber(y, yMin, yMax, killDisplaySettings.layout[keys[1]]);
    applyKillDisplaySettings(killDisplaySettings);
    if (save) queueKillDisplaySettingsSave();
}

function setShowDeathNumber(enabled, save = true) {
    killDisplaySettings.layout.showDeathNumber = enabled ? 1 : 0;
    if (killEditMode) unlockKillBoardAfterEditMode();
    applyKillDisplaySettings(killDisplaySettings);
    if (killEditMode) relockKillBoardForEditMode();
    if (save) queueKillDisplaySettingsSave();
}

function bindKillEditToolbar() {
    const target = document.getElementById('kill-edit-target');
    const font = document.getElementById('kill-edit-font');
    const size = document.getElementById('kill-edit-size');
    const color = document.getElementById('kill-edit-color');
    const stroke = document.getElementById('kill-edit-stroke');
    const letter = document.getElementById('kill-edit-letter');
    const glow = document.getElementById('kill-edit-glow');
    const width = document.getElementById('kill-edit-width');
    const showDeath = document.getElementById('kill-edit-show-death');
    const skin = document.getElementById('kill-edit-skin');
    const skinColors = document.getElementById('kill-edit-skin-colors');
    const exit = document.getElementById('kill-edit-exit');

    target?.addEventListener('change', () => setSelectedKillStyleKey(target.value));
    font?.addEventListener('change', () => updateSelectedStyle({ fontFamily: font.value }));
    size?.addEventListener('input', () => updateSelectedStyle({ fontSize: size.value }));
    color?.addEventListener('input', () => updateSelectedStyle({ color: color.value, colorMode: 'custom' }));
    stroke?.addEventListener('input', () => updateSelectedStyle({ strokeWidth: stroke.value }));
    letter?.addEventListener('input', () => updateSelectedStyle({ letterSpacing: letter.value }));
    glow?.addEventListener('input', () => updateSelectedStyle({ glow: glow.value }));
    width?.addEventListener('input', () => updateSelectedLayout(width.value));
    showDeath?.addEventListener('change', () => setShowDeathNumber(showDeath.checked));
    populateKillSkinList();
    skin?.addEventListener('change', () => setKillDisplaySkin(skin.value));
    skinColors?.addEventListener('click', () => applyKillSkinRecommendedColors());
    exit?.addEventListener('click', () => toggleKillEditMode(false));
}

function queueKillDisplaySettingsSave() {
    suppressRemoteKillSettingsUntil = Date.now() + 2000;
    clearTimeout(saveSettingsTimer);
    saveSettingsTimer = setTimeout(saveKillDisplaySettings, 180);
}

async function saveKillDisplaySettings() {
    isSavingKillSettings = true;
    suppressRemoteKillSettingsUntil = Date.now() + 2000;
    try {
        const response = await fetch(KILL_SETTINGS_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ settings: killDisplaySettings })
        });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const payload = await response.json();
        if (payload?.settings) {
            applyKillDisplaySettings(payload.settings);
        }
        showStatus('外观已保存', false);
    } catch (err) {
        showStatus('外观保存失败，请确认主程序仍在运行', true);
    } finally {
        isSavingKillSettings = false;
        suppressRemoteKillSettingsUntil = Date.now() + 500;
    }
}

function lockKillBoardForEditMode() {
    const root = document.getElementById('kill-display-root');
    const board = document.getElementById('kill-board');
    if (!root || !board) return;
    const rect = board.getBoundingClientRect();
    root.style.setProperty('--kill-edit-board-left', `${Math.max(0, Math.round(rect.left))}px`);
    root.style.setProperty('--kill-edit-board-top', `${Math.max(0, Math.round(rect.top))}px`);
    root.style.setProperty('--kill-edit-board-width', `${Math.max(1, Math.round(board.offsetWidth || rect.width))}px`);
    root.style.setProperty('--kill-edit-board-height', `${Math.max(1, Math.round(board.offsetHeight || rect.height))}px`);
    root.style.setProperty('--kill-edit-toolbar-top', `${Math.max(0, Math.round(rect.bottom + 6))}px`);
}

function unlockKillBoardAfterEditMode() {
    const root = document.getElementById('kill-display-root');
    if (!root) return;
    root.style.removeProperty('--kill-edit-board-left');
    root.style.removeProperty('--kill-edit-board-top');
    root.style.removeProperty('--kill-edit-board-width');
    root.style.removeProperty('--kill-edit-board-height');
    root.style.removeProperty('--kill-edit-toolbar-top');
}

function relockKillBoardForEditMode() {
    if (!killEditMode) return;
    unlockKillBoardAfterEditMode();
    requestAnimationFrame(() => {
        if (killEditMode) lockKillBoardForEditMode();
    });
}

function toggleKillEditMode(force) {
    const nextMode = typeof force === 'boolean' ? force : !killEditMode;
    if (nextMode && !killEditMode) lockKillBoardForEditMode();
    killEditMode = nextMode;
    const root = document.getElementById('kill-display-root');
    const toolbar = document.getElementById('kill-edit-toolbar');
    root?.classList.toggle('edit-mode', killEditMode);
    if (toolbar) toolbar.hidden = !killEditMode;
    syncKillEditWindowHeight();
    refreshSelectedStyleMarker();
    if (killEditMode) syncKillEditToolbar();
    else unlockKillBoardAfterEditMode();
}

function beginKillLayoutDrag(event) {
    if (!killEditMode || event.button !== 0) return;
    const target = event.target?.closest?.('[data-style-key]');
    const styleKey = target?.dataset?.styleKey;
    if (!EDITABLE_STYLE_KEYS.includes(styleKey)) return;

    const offset = getStyleOffsetForKey(styleKey);
    dragLayoutState = {
        pointerId: event.pointerId,
        styleKey,
        startX: event.clientX,
        startY: event.clientY,
        offsetX: offset.x,
        offsetY: offset.y,
        moved: false
    };
    setSelectedKillStyleKey(styleKey);
    event.preventDefault();
    event.stopPropagation();
    target.setPointerCapture?.(event.pointerId);
    document.getElementById('kill-display-root')?.classList.add('layout-dragging');
    document.addEventListener('pointermove', handleKillLayoutDrag, { passive: false });
    document.addEventListener('pointerup', finishKillLayoutDrag, { passive: false });
    document.addEventListener('pointercancel', finishKillLayoutDrag, { passive: false });
}

function handleKillLayoutDrag(event) {
    if (!dragLayoutState) return;
    event.preventDefault();
    const scale = Math.max(0.6, (killDisplaySettings.layout.pageScale || 100) / 100);
    const dx = Math.round((event.clientX - dragLayoutState.startX) / scale);
    const dy = isVerticalLayoutDragAllowed(dragLayoutState.styleKey)
        ? Math.round((event.clientY - dragLayoutState.startY) / scale)
        : 0;
    if (Math.abs(dx) > 2 || Math.abs(dy) > 2) {
        dragLayoutState.moved = true;
    }
    updateKillLayoutOffset(
        dragLayoutState.styleKey,
        dragLayoutState.offsetX + dx,
        dragLayoutState.offsetY + dy
    );
}

function finishKillLayoutDrag(event) {
    if (!dragLayoutState) return;
    event?.preventDefault?.();
    suppressClickAfterDrag = dragLayoutState.moved;
    dragLayoutState = null;
    document.getElementById('kill-display-root')?.classList.remove('layout-dragging');
    document.removeEventListener('pointermove', handleKillLayoutDrag);
    document.removeEventListener('pointerup', finishKillLayoutDrag);
    document.removeEventListener('pointercancel', finishKillLayoutDrag);
    if (suppressClickAfterDrag) {
        setTimeout(() => {
            suppressClickAfterDrag = false;
        }, 80);
    }
}

function adjustSelectedByWheel(event) {
    if (!killEditMode) return;
    if (event.target?.closest?.('.kill-edit-toolbar')) return;
    const isBlankWheel = !event.target?.closest?.('[data-style-key]');
    if (isBlankWheel) {
        setSelectedKillStyleKey(PAGE_EDIT_KEY);
    }
    const delta = event.deltaY < 0 ? 1 : -1;
    event.preventDefault();
    if (selectedKillStyleKey === PAGE_EDIT_KEY) {
        updateSelectedLayout(killDisplaySettings.layout.pageScale + delta * 2);
        return;
    }
    if (event.shiftKey) {
        const control = getLayoutControlForStyle(selectedKillStyleKey);
        updateSelectedLayout(killDisplaySettings.layout[control.key] + delta);
        return;
    }
    if (event.ctrlKey) {
        updateSelectedStyle({ strokeWidth: selectedStyle().strokeWidth + delta });
        return;
    }
    updateSelectedStyle({ fontSize: selectedStyle().fontSize + delta });
}

function postKillHostCommand(action, extra = {}) {
    if (!window.chrome?.webview) return;
    window.chrome.webview.postMessage({ action, ...extra });
}

function syncKillEditWindowHeight() {
    postKillHostCommand('cmd_kill_window_edit_mode', { enabled: killEditMode });
}

function isKillShellWindow() {
    return !!window.chrome?.webview;
}

function initKillWindowControls() {
    const root = document.getElementById('kill-display-root');
    const closeBtn = document.getElementById('kill-window-close');
    const settingsBtn = document.getElementById('kill-window-settings');
    root?.classList.toggle('shell-window', isKillShellWindow());
    closeBtn?.addEventListener('mousedown', (event) => {
        event.preventDefault();
        event.stopPropagation();
    });
    closeBtn?.addEventListener('click', (event) => {
        event.preventDefault();
        event.stopPropagation();
        postKillHostCommand('cmd_kill_window_close');
    });
    settingsBtn?.addEventListener('mousedown', (event) => {
        event.preventDefault();
        event.stopPropagation();
    });
    settingsBtn?.addEventListener('click', (event) => {
        event.preventDefault();
        event.stopPropagation();
        toggleKillEditMode();
    });
}

function bindWindowControls() {
    const root = document.getElementById('kill-display-root');
    const grip = document.getElementById('kill-resize-grip');
    if (root) {
        root.addEventListener('pointerdown', beginKillLayoutDrag);
        root.addEventListener('mousedown', (event) => {
            if (event.button !== 0) return;
            if (killEditMode) return;
            if (event.target?.closest?.('.kill-resize-grip, .kill-edit-toolbar, .kill-window-close, .kill-window-settings, .kill-window-skin, .kill-skin-panel')) return;
            if (isKillBgAdjusting()) return;
            event.preventDefault();
            postKillHostCommand('cmd_kill_window_drag');
        });
        root.addEventListener('dblclick', (event) => {
            if (event.target?.closest?.('.kill-edit-toolbar, .kill-window-close, .kill-window-settings, .kill-window-skin, .kill-skin-panel')) return;
            if (isKillBgAdjusting()) return;
            if (event.target?.closest?.('[data-style-key]')) return;
            toggleKillEditMode();
        });
        root.addEventListener('click', (event) => {
            if (!killEditMode) return;
            if (suppressClickAfterDrag) return;
            if (event.target?.closest?.('.kill-edit-toolbar, .kill-resize-grip, .kill-window-close, .kill-window-settings, .kill-window-skin, .kill-skin-panel')) return;
            const target = event.target?.closest?.('[data-style-key]');
            if (target?.dataset?.styleKey) setSelectedKillStyleKey(target.dataset.styleKey);
            else setSelectedKillStyleKey(PAGE_EDIT_KEY);
        });
        root.addEventListener('wheel', adjustSelectedByWheel, { passive: false });
    }
    if (grip) {
        grip.addEventListener('mousedown', (event) => {
            if (event.button !== 0) return;
            event.preventDefault();
            event.stopPropagation();
            postKillHostCommand('cmd_kill_window_resize');
        });
    }
    document.addEventListener('keydown', (event) => {
        if (event.key === 'F2') {
            event.preventDefault();
            toggleKillEditMode();
        }
    });
}

// ==========================================
// 右上角「界面风格」面板：一键切换 9 种风格 + 自定义背景图（缩放 / 拖动 / 透明度）
// 背景图由主程序保存在配置目录（kill_background.img），layout.bgImageRev 作为版本号。
// ==========================================
const KILL_BG_URL = 'http://127.0.0.1:18777/kill-bg';
const KILL_BG_UPLOAD_URL = 'http://127.0.0.1:18777/api/kill-bg-image';
const KILL_BG_CLEAR_URL = 'http://127.0.0.1:18777/api/kill-bg-image-clear';
const KILL_BG_MAX_SIDE = 2560;
const KILL_BG_MAX_BYTES = 12 * 1024 * 1024;
const KILL_SKIN_SWATCHES = {
    dnf: 'linear-gradient(135deg, #2a1d10 0%, #6b4a1f 45%, #e7c27a 100%)',
    classic: 'linear-gradient(135deg, #2b2d36 0%, #4a4d5a 100%)',
    neon: 'linear-gradient(135deg, #07101f 0%, #00e5ff 55%, #ff3fa4 100%)',
    ink: 'linear-gradient(135deg, #1d1b18 0%, #5b554a 55%, #c8a86a 100%)',
    glass: 'linear-gradient(135deg, rgba(255,255,255,0.55) 0%, rgba(160,190,230,0.35) 100%)',
    pixel: 'repeating-linear-gradient(90deg, #1a1030 0 6px, #3c2a6a 6px 12px)',
    inferno: 'linear-gradient(135deg, #1a0703 0%, #b3300c 55%, #ffb347 100%)',
    broadcast: 'linear-gradient(135deg, #0b1528 0%, #1e3a6e 60%, #ffc62b 100%)',
    frost: 'linear-gradient(135deg, #0d2233 0%, #3f86ad 55%, #d6f6ff 100%)'
};
let killSkinPanelOpen = false;
let killBgLoadedRev = -1;
let killBgNatural = { w: 0, h: 0 };
let killBgDrag = null;
let killBgBusy = false;

function isKillBgAdjusting() {
    return killSkinPanelOpen && (killDisplaySettings?.layout?.bgImageRev || 0) > 0;
}

function ensureKillCustomBgLayer() {
    const board = document.getElementById('kill-board');
    if (!board) return null;
    let layer = board.querySelector(':scope > .kill-custom-bg');
    if (!layer) {
        layer = document.createElement('div');
        layer.className = 'kill-custom-bg';
        layer.setAttribute('aria-hidden', 'true');
        layer.hidden = true;
        const img = document.createElement('img');
        img.alt = '';
        img.draggable = false;
        img.decoding = 'async';
        img.addEventListener('load', () => {
            killBgNatural = { w: img.naturalWidth, h: img.naturalHeight };
            layoutKillCustomBg();
        });
        img.addEventListener('error', () => {
            killBgNatural = { w: 0, h: 0 };
            layer.classList.remove('ready');
        });
        layer.appendChild(img);
        board.prepend(layer);
        if (window.ResizeObserver) new ResizeObserver(() => layoutKillCustomBg()).observe(board);
    }
    return layer;
}

function applyKillCustomBackground(layout) {
    const board = document.getElementById('kill-board');
    const layer = ensureKillCustomBgLayer();
    if (!board || !layer) return;
    const img = layer.firstElementChild;
    const rev = layout.bgImageRev || 0;
    board.classList.toggle('has-custom-bg', rev > 0);
    layer.hidden = rev <= 0;
    if (rev > 0 && killBgLoadedRev !== rev) {
        killBgLoadedRev = rev;
        killBgNatural = { w: 0, h: 0 };
        layer.classList.remove('ready');
        img.src = `${KILL_BG_URL}?v=${rev}`;
    } else if (rev <= 0 && killBgLoadedRev !== 0) {
        killBgLoadedRev = 0;
        killBgNatural = { w: 0, h: 0 };
        layer.classList.remove('ready');
        img.removeAttribute('src');
    }
    layer.style.opacity = String(layout.bgImageOpacity / 100);
    layoutKillCustomBg();
}

// 缩放 100% = 刚好铺满整个展示面板（cover），X/Y 为相对面板中心的偏移（未缩放的面板像素）。
function layoutKillCustomBg() {
    const board = document.getElementById('kill-board');
    const layer = board?.querySelector(':scope > .kill-custom-bg');
    const img = layer?.firstElementChild;
    if (!img || !killBgNatural.w || !killBgNatural.h) return;
    const bw = board.clientWidth;
    const bh = board.clientHeight;
    if (!bw || !bh) return;
    const layout = killDisplaySettings.layout;
    const cover = Math.max(bw / killBgNatural.w, bh / killBgNatural.h);
    const k = cover * layout.bgImageScale / 100;
    img.style.width = `${Math.round(killBgNatural.w * k)}px`;
    img.style.height = `${Math.round(killBgNatural.h * k)}px`;
    img.style.transform = `translate(-50%, -50%) translate(${layout.bgImageX}px, ${layout.bgImageY}px)`;
    layer.classList.add('ready');
}

function setKillBgLayout(patch, { save = true, full = false } = {}) {
    const layout = killDisplaySettings.layout;
    Object.entries(patch).forEach(([key, value]) => {
        const [min, max] = KILL_DISPLAY_LAYOUT_LIMITS[key];
        layout[key] = clampNumber(value, min, max, layout[key]);
    });
    suppressRemoteKillSettingsUntil = Date.now() + 2000;
    if (full) applyKillDisplaySettings(killDisplaySettings);
    else {
        const layer = document.querySelector('#kill-board > .kill-custom-bg');
        if (layer) layer.style.opacity = String(layout.bgImageOpacity / 100);
        layoutKillCustomBg();
        syncKillSkinPanel();
    }
    if (save) queueKillDisplaySettingsSave();
}

function loadKillImageElement(file) {
    return new Promise((resolve, reject) => {
        const url = URL.createObjectURL(file);
        const img = new Image();
        img.onload = () => { URL.revokeObjectURL(url); resolve(img); };
        img.onerror = () => { URL.revokeObjectURL(url); reject(new Error('无法读取这张图片')); };
        img.src = url;
    });
}

// 大图先在本地缩到最长边 2560 并转成 webp，避免把几十 MB 的原图写进配置目录。
async function prepareKillBackgroundBlob(file) {
    const type = String(file.type || '').toLowerCase();
    if (type === 'image/gif') {
        if (file.size > KILL_BG_MAX_BYTES) throw new Error('GIF 超过 12MB');
        return file;
    }
    const img = await loadKillImageElement(file);
    const w = img.naturalWidth;
    const h = img.naturalHeight;
    if (!w || !h) throw new Error('图片尺寸无效');
    const ratio = Math.min(1, KILL_BG_MAX_SIDE / Math.max(w, h));
    if (ratio === 1 && file.size <= 4 * 1024 * 1024 && ['image/png', 'image/jpeg', 'image/webp'].includes(type)) return file;
    const canvas = document.createElement('canvas');
    canvas.width = Math.max(1, Math.round(w * ratio));
    canvas.height = Math.max(1, Math.round(h * ratio));
    const ctx = canvas.getContext('2d');
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
    const toBlob = (mime, quality) => new Promise(resolve => canvas.toBlob(resolve, mime, quality));
    let blob = await toBlob('image/webp', 0.92);
    if (!blob || blob.type !== 'image/webp') blob = await toBlob('image/png');
    if (!blob) throw new Error('图片转换失败');
    if (blob.size > KILL_BG_MAX_BYTES) throw new Error('图片处理后仍超过 12MB');
    return blob;
}

async function uploadKillBackgroundFile(file) {
    if (!file || killBgBusy) return;
    if (!/^image\//i.test(file.type || '')) {
        showStatus('请选择图片文件（PNG / JPG / WEBP / GIF）', false);
        return;
    }
    killBgBusy = true;
    syncKillSkinPanel();
    showStatus('正在处理背景图...', true);
    try {
        const blob = await prepareKillBackgroundBlob(file);
        const response = await fetch(KILL_BG_UPLOAD_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/octet-stream' },
            body: blob
        });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok || payload?.ok !== true) throw new Error(payload?.error || `HTTP ${response.status}`);
        const rev = clampNumber(payload.rev, 1, 2147483647, Math.floor(Date.now() / 1000) % 2147483647);
        setKillBgLayout({ bgImageRev: rev, bgImageScale: 100, bgImageX: 0, bgImageY: 0 }, { full: true });
        showStatus('背景图已设置：面板打开时拖动画面移动、滚轮缩放', false);
    } catch (err) {
        showStatus(`背景图设置失败：${err?.message || err}`, false);
    } finally {
        killBgBusy = false;
        syncKillSkinPanel();
    }
}

async function clearKillBackground() {
    if (killBgBusy) return;
    killBgBusy = true;
    try {
        await fetch(KILL_BG_CLEAR_URL, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{}' });
    } catch (err) {
        // 文件删除失败也要让展示页恢复为无背景
    } finally {
        killBgBusy = false;
    }
    setKillBgLayout({ bgImageRev: 0, bgImageScale: 100, bgImageX: 0, bgImageY: 0 }, { full: true });
    showStatus('已移除自定义背景', false);
}

function selectKillSkinFromPanel(index) {
    const applyColors = document.getElementById('kill-skin-apply-colors')?.checked !== false;
    killDisplaySettings.layout.skin = clampNumber(index, 0, KILL_DISPLAY_SKIN_MAX, 0);
    if (applyColors) {
        applyKillSkinRecommendedColors(false);
        showStatus(`风格：${getKillDisplaySkin(killDisplaySettings.layout.skin).label}（已套用推荐配色）`, false);
        queueKillDisplaySettingsSave();
    } else {
        setKillDisplaySkin(index);
    }
}

function buildKillSkinGrid() {
    const grid = document.getElementById('kill-skin-grid');
    if (!grid || grid.childElementCount === KILL_DISPLAY_SKINS.length) return;
    grid.innerHTML = '';
    KILL_DISPLAY_SKINS.forEach((skin, index) => {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'kill-skin-option';
        button.dataset.skinIndex = String(index);
        button.title = skin.label;
        const swatch = document.createElement('i');
        swatch.style.background = KILL_SKIN_SWATCHES[skin.id] || '#333';
        const label = document.createElement('span');
        label.textContent = skin.label;
        button.append(swatch, label);
        button.addEventListener('click', () => selectKillSkinFromPanel(index));
        grid.appendChild(button);
    });
}

function syncKillSkinPanel() {
    const panel = document.getElementById('kill-skin-panel');
    if (!panel || !killDisplaySettings?.layout) return;
    const layout = killDisplaySettings.layout;
    buildKillSkinGrid();
    panel.querySelectorAll('.kill-skin-option').forEach(button => {
        button.classList.toggle('active', Number(button.dataset.skinIndex) === layout.skin);
    });
    const hasBg = layout.bgImageRev > 0;
    const stateText = document.getElementById('kill-bg-state');
    if (stateText) stateText.textContent = killBgBusy ? '处理中...' : (hasBg ? '已设置' : '未设置');
    const setRange = (id, value, text) => {
        const input = document.getElementById(id);
        const out = document.getElementById(`${id}-val`);
        if (input && document.activeElement !== input) input.value = String(value);
        if (out) out.textContent = text;
    };
    setRange('kill-bg-scale', layout.bgImageScale, `${layout.bgImageScale}%`);
    setRange('kill-bg-opacity', layout.bgImageOpacity, `${layout.bgImageOpacity}%`);
    setRange('kill-bg-panel', layout.panelAlpha, `${layout.panelAlpha}%`);
    ['kill-bg-scale', 'kill-bg-opacity', 'kill-bg-reset', 'kill-bg-clear'].forEach(id => {
        const el = document.getElementById(id);
        if (el) el.disabled = !hasBg || killBgBusy;
    });
    const pick = document.getElementById('kill-bg-pick');
    if (pick) {
        pick.disabled = killBgBusy;
        pick.textContent = hasBg ? '更换图片' : '选择图片';
    }
    panel.classList.toggle('has-bg', hasBg);
}

function setKillSkinPanelOpen(open) {
    killSkinPanelOpen = !!open;
    const panel = document.getElementById('kill-skin-panel');
    if (panel) panel.hidden = !killSkinPanelOpen;
    document.getElementById('kill-display-root')?.classList.toggle('skin-panel-open', killSkinPanelOpen);
    document.getElementById('kill-window-skin')?.classList.toggle('active', killSkinPanelOpen);
    if (!killSkinPanelOpen) killBgDrag = null;
    syncKillSkinPanel();
}

function initKillSkinPanel() {
    const root = document.getElementById('kill-display-root');
    const button = document.getElementById('kill-window-skin');
    const panel = document.getElementById('kill-skin-panel');
    if (!root || !button || !panel) return;
    const applyColors = document.getElementById('kill-skin-apply-colors');
    try {
        if (applyColors) applyColors.checked = localStorage.getItem('killSkinApplyColors') !== '0';
    } catch (err) { /* localStorage 不可用时保持默认勾选 */ }
    applyColors?.addEventListener('change', () => {
        try { localStorage.setItem('killSkinApplyColors', applyColors.checked ? '1' : '0'); } catch (err) { /* ignore */ }
    });
    button.addEventListener('mousedown', (event) => {
        event.preventDefault();
        event.stopPropagation();
    });
    button.addEventListener('click', (event) => {
        event.preventDefault();
        event.stopPropagation();
        setKillSkinPanelOpen(!killSkinPanelOpen);
    });
    ['mousedown', 'pointerdown', 'dblclick', 'wheel'].forEach(type => {
        panel.addEventListener(type, event => event.stopPropagation(), type === 'wheel' ? { passive: true } : undefined);
    });
    document.getElementById('kill-skin-panel-close')?.addEventListener('click', () => setKillSkinPanelOpen(false));
    const file = document.getElementById('kill-bg-file');
    document.getElementById('kill-bg-pick')?.addEventListener('click', () => file?.click());
    file?.addEventListener('change', () => {
        const picked = file.files?.[0];
        file.value = '';
        uploadKillBackgroundFile(picked);
    });
    document.getElementById('kill-bg-clear')?.addEventListener('click', () => clearKillBackground());
    document.getElementById('kill-bg-reset')?.addEventListener('click', () => {
        setKillBgLayout({ bgImageScale: 100, bgImageX: 0, bgImageY: 0 });
        showStatus('背景已复位为铺满', false);
    });
    const bindRange = (id, key, full = false) => {
        const input = document.getElementById(id);
        input?.addEventListener('input', () => setKillBgLayout({ [key]: input.value }, { full }));
    };
    bindRange('kill-bg-scale', 'bgImageScale');
    bindRange('kill-bg-opacity', 'bgImageOpacity');
    bindRange('kill-bg-panel', 'panelAlpha', true);

    // 面板打开且有背景时：在画面上拖动 = 移动背景，滚轮 = 缩放背景
    root.addEventListener('pointerdown', (event) => {
        if (!isKillBgAdjusting() || event.button !== 0) return;
        if (event.target?.closest?.('.kill-skin-panel, .kill-window-skin, .kill-window-close, .kill-window-settings, .kill-edit-toolbar, .kill-resize-grip')) return;
        event.preventDefault();
        event.stopImmediatePropagation();
        const layout = killDisplaySettings.layout;
        killBgDrag = {
            pointerId: event.pointerId,
            x: event.clientX,
            y: event.clientY,
            startX: layout.bgImageX,
            startY: layout.bgImageY,
            scale: Math.max(0.1, layout.pageScale / 100),
            moved: false
        };
        try { root.setPointerCapture(event.pointerId); } catch (err) { /* ignore */ }
        root.classList.add('bg-dragging');
    }, true);
    root.addEventListener('pointermove', (event) => {
        if (!killBgDrag || event.pointerId !== killBgDrag.pointerId) return;
        const dx = (event.clientX - killBgDrag.x) / killBgDrag.scale;
        const dy = (event.clientY - killBgDrag.y) / killBgDrag.scale;
        killBgDrag.moved = true;
        setKillBgLayout({ bgImageX: Math.round(killBgDrag.startX + dx), bgImageY: Math.round(killBgDrag.startY + dy) }, { save: false });
    });
    const endDrag = (event) => {
        if (!killBgDrag || (event && event.pointerId !== killBgDrag.pointerId)) return;
        const moved = killBgDrag.moved;
        killBgDrag = null;
        root.classList.remove('bg-dragging');
        if (moved) queueKillDisplaySettingsSave();
    };
    root.addEventListener('pointerup', endDrag);
    root.addEventListener('pointercancel', endDrag);
    root.addEventListener('wheel', (event) => {
        if (!isKillBgAdjusting()) return;
        if (event.target?.closest?.('.kill-skin-panel')) return;
        event.preventDefault();
        event.stopImmediatePropagation();
        const current = killDisplaySettings.layout.bgImageScale;
        let next = Math.round(current * (event.deltaY < 0 ? 1.06 : 1 / 1.06));
        if (next === current) next += event.deltaY < 0 ? 1 : -1;
        setKillBgLayout({ bgImageScale: next });
    }, { passive: false, capture: true });
    document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape' && killSkinPanelOpen) setKillSkinPanelOpen(false);
    });
    root.addEventListener('dragover', event => { if (killSkinPanelOpen) event.preventDefault(); });
    root.addEventListener('drop', (event) => {
        if (!killSkinPanelOpen) return;
        event.preventDefault();
        const dropped = event.dataTransfer?.files?.[0];
        if (dropped) uploadKillBackgroundFile(dropped);
    });
    syncKillSkinPanel();
}

function showStatus(text, sticky = false) {
    const status = document.getElementById('kill-status');
    if (!status) return;
    if (!text) {
        lastStatusText = '';
        status.classList.remove('active');
        status.textContent = '';
        return;
    }
    if (text === lastStatusText) return;
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
            systemFonts = normalizeSystemFonts(data.systemFonts);
            if (!isSavingKillSettings && Date.now() >= suppressRemoteKillSettingsUntil) {
                applyKillDisplaySettings(data.killDisplaySettings || getDefaultKillDisplaySettings());
            } else {
                populateFontList(selectedStyle()?.fontFamily || '');
            }
            renderKillDisplay(data);
        }
        if (lastStatusText && lastStatusText.includes('等待')) showStatus('', false);
    } catch (err) {
        showStatus('等待主程序数据...', true);
    }
}

window.addEventListener('resize', scheduleFitKillTextElements);
document.fonts?.ready?.then(scheduleFitKillTextElements).catch(() => {});

applyKillDisplaySettings(getDefaultKillDisplaySettings());
renderKillDisplay({});
initKillWindowControls();
initKillSkinPanel();
bindKillEditToolbar();
bindWindowControls();
fetchKillDisplayState();
setInterval(fetchKillDisplayState, 250);


/* ================= 击杀特效（多杀 / AK / 一血 / 终结 / 复仇） ================= */
/* ================= 击杀特效引擎（按 9 种界面风格自动换装） =================
 * KillFx.play({ team: 'red'|'blue', killerRow, victimRow, level, name, victim })
 *   level: 1 | 2 | 3 | 'ak' | 'first' (一血) | 'shutdown' (终结) | 'revenge' (复仇)
 * 风格取自 #kill-display-root 的 data-skin（dnf/classic/neon/ink/glass/pixel/inferno/broadcast/frost）
 * 事件由 KillFxTracker 依据 /api/state 的击杀 / 死亡 / AK 变化自动识别。
 */
const KillFx = (() => {
    const root = document.getElementById('kill-display-root');
    const layer = document.createElement('div');
    layer.className = 'fx-layer';
    const canvas = document.createElement('canvas');
    layer.appendChild(canvas);
    root.appendChild(layer);
    const ctx = canvas.getContext('2d');
    let particles = [];
    let running = false;

    const TEAM_RGB = { red: '255, 70, 70', blue: '56, 182, 255' };
    const TEAM_HUE = { red: 0, blue: 200 };

    /* 每种风格：粒子类型、死亡标记、AK 文案/装饰 */
    const THEMES = {
        dnf:       { p: 'ember',    mark: 'crack', akText: 'AK',  akSub: '一人团灭', akEn: 'ALL KILL', deco: 'magic', bands: true },
        classic:   { p: 'spark',    mark: 'crack', akText: 'ACE', akSub: '一人团灭', akEn: 'ALL KILL', deco: 'bars' },
        neon:      { p: 'neon',     mark: 'crack', akText: 'AK',  akSub: '一人团灭', akEn: 'ALL // KILL', deco: 'rings' },
        ink:       { p: 'ink',      mark: 'ink',   akText: '全歼', akSub: '一人破阵', akEn: '', deco: 'enso' },
        glass:     { p: 'bubble',   mark: 'crack', akText: 'AK',  akSub: '一人团灭', akEn: 'ALL KILL', deco: 'glass' },
        pixel:     { p: 'pixel',    mark: 'pixel', akText: 'AK!', akSub: 'PERFECT', akEn: 'INSERT COIN', deco: 'pixel' },
        inferno:   { p: 'flame',    mark: 'crack', akText: 'AK',  akSub: '焚尽一切', akEn: 'ALL KILL', deco: 'fire', bands: true },
        broadcast: { p: 'confetti', mark: 'crack', akText: 'ACE', akSub: '一人团灭', akEn: 'ALL KILL', deco: 'stinger' },
        frost:     { p: 'snow',     mark: 'ice',   akText: 'AK',  akSub: '冰封全场', akEn: 'ALL KILL', deco: 'flake', bands: true }
    };
    let skin = 'dnf';
    let theme = THEMES.dnf;
    function syncSkin() {
        skin = THEMES[root.dataset.skin] ? root.dataset.skin : 'dnf';
        theme = THEMES[skin];
        layer.dataset.skin = skin;
    }

    function resize() {
        const r = layer.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        canvas.width = Math.max(1, r.width * dpr);
        canvas.height = Math.max(1, r.height * dpr);
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    }
    window.addEventListener('resize', resize);
    resize();
    if (window.ResizeObserver) { try { new ResizeObserver(resize).observe(layer); } catch (err) { /* ignore */ } }

    function localRect(el) {
        const a = el.getBoundingClientRect();
        const b = layer.getBoundingClientRect();
        return { x: a.left - b.left, y: a.top - b.top, w: a.width, h: a.height };
    }
    function center() { const r = layer.getBoundingClientRect(); return { cx: r.width / 2, cy: r.height / 2, w: r.width, h: r.height }; }
    function panelFor(team) { return root.querySelector(`.kill-team[data-team-color="${team}"]`); }
    function rowOf(team, idx) { return panelFor(team)?.querySelectorAll('.kill-row')[idx] || null; }
    function add(el, ms) { layer.appendChild(el); setTimeout(() => el.remove(), ms); return el; }
    function div(cls, style = {}) { const d = document.createElement('div'); d.className = cls; Object.assign(d.style, style); return d; }
    function esc(s) { return String(s ?? '').replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }

    /* ---------- 粒子：9 种形态 ---------- */
    const rnd = (a, b) => a + Math.random() * (b - a);
    const PRESET = { // 每种粒子的默认物理参数
        ember:    { g: .05,  drag: .985, add: true },
        spark:    { g: .08,  drag: .96,  add: true },
        neon:     { g: 0,    drag: .94,  add: true },
        ink:      { g: .03,  drag: .9,   add: false },
        bubble:   { g: -.03, drag: .97,  add: false },
        pixel:    { g: .12,  drag: .98,  add: false },
        flame:    { g: -.06, drag: .96,  add: true },
        confetti: { g: .07,  drag: .96,  add: false },
        snow:     { g: .015, drag: .97,  add: true }
    };
    const CONFETTI = ['#ffc62b', '#ffffff', '#ff4646', '#38b6ff', '#7cf7d4'];
    const PIXEL = ['#ffe45c', '#ff7a3c', '#fce0a8', '#80d010', '#ffffff'];
    function burst(x, y, n, opts = {}) {
        const type = opts.type || theme.p;
        const pre = PRESET[type] || PRESET.ember;
        for (let i = 0; i < n; i++) {
            const a = (opts.dir ?? 0) + (Math.random() - 0.5) * (opts.spread ?? Math.PI * 2);
            const v = (opts.speed ?? 3) * (0.35 + Math.random());
            particles.push({
                type, x, y, vx: Math.cos(a) * v, vy: Math.sin(a) * v - (opts.lift ?? 0.6),
                life: 0, max: (opts.life ?? 50) * (0.6 + Math.random() * 0.8),
                size: (opts.size ?? 2.4) * (0.5 + Math.random()),
                hue: opts.hue ?? (type === 'neon' ? (Math.random() < .5 ? 186 : 312) : type === 'flame' ? rnd(8, 40) : type === 'snow' ? 195 : rnd(32, 54)),
                g: opts.gravity ?? pre.g, drag: pre.drag, add: pre.add,
                shard: opts.shard && Math.random() < 0.5, rot: Math.random() * 6, vr: (Math.random() - .5) * .4,
                color: type === 'confetti' ? CONFETTI[i % 5] : type === 'pixel' ? PIXEL[i % 5] : null,
                wob: Math.random() * 6
            });
        }
        if (!running) { running = true; requestAnimationFrame(tick); }
    }
    function drawP(p, t) {
        switch (p.type) {
            case 'spark': { // 细长白色火花
                ctx.strokeStyle = `rgba(255,255,255,${t})`; ctx.lineWidth = Math.max(1, p.size * .5);
                ctx.beginPath(); ctx.moveTo(p.x, p.y); ctx.lineTo(p.x - p.vx * 3, p.y - p.vy * 3); ctx.stroke(); break;
            }
            case 'neon': { // 霓虹拖尾
                ctx.strokeStyle = `hsla(${p.hue},100%,65%,${t})`; ctx.lineWidth = p.size * .8; ctx.shadowBlur = 8; ctx.shadowColor = `hsl(${p.hue},100%,60%)`;
                ctx.beginPath(); ctx.moveTo(p.x, p.y); ctx.lineTo(p.x - p.vx * 5, p.y - p.vy * 5); ctx.stroke(); ctx.shadowBlur = 0; break;
            }
            case 'ink': { // 墨点，越到后面越大越淡
                const r = p.size * (1.2 + (1 - t) * 1.6);
                ctx.fillStyle = `rgba(8,6,4,${t * .9})`;
                ctx.beginPath(); ctx.ellipse(p.x, p.y, r * 1.2, r, p.rot, 0, Math.PI * 2); ctx.fill(); break;
            }
            case 'bubble': { // 玻璃气泡
                const r = p.size * 2.2; p.x += Math.sin(p.life * .1 + p.wob) * .3;
                ctx.strokeStyle = `rgba(255,255,255,${t * .8})`; ctx.lineWidth = 1;
                ctx.fillStyle = `rgba(200,225,255,${t * .15})`;
                ctx.beginPath(); ctx.arc(p.x, p.y, r, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
                ctx.fillStyle = `rgba(255,255,255,${t * .8})`; ctx.beginPath(); ctx.arc(p.x - r * .35, p.y - r * .35, r * .22, 0, Math.PI * 2); ctx.fill(); break;
            }
            case 'pixel': { // 方块像素，坐标取整
                const s = Math.max(3, Math.round(p.size * 1.6));
                ctx.fillStyle = p.color; ctx.globalAlpha = t > .3 ? 1 : 0;
                ctx.fillRect(Math.round(p.x / 3) * 3, Math.round(p.y / 3) * 3, s, s); break;
            }
            case 'flame': { // 上升火苗：黄→橙→红
                const r = p.size * (1 + t * 2.2);
                const g = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, r * 2);
                g.addColorStop(0, `hsla(${p.hue + t * 20},100%,${55 + t * 30}%,${t})`); g.addColorStop(1, `hsla(${p.hue},100%,40%,0)`);
                ctx.fillStyle = g; ctx.beginPath(); ctx.arc(p.x, p.y, r * 2, 0, Math.PI * 2); ctx.fill(); break;
            }
            case 'confetti': { // 彩带纸片翻转
                ctx.save(); ctx.translate(p.x, p.y); ctx.rotate(p.rot); ctx.scale(1, Math.cos(p.life * .2 + p.wob));
                ctx.fillStyle = p.color; ctx.fillRect(-p.size * 1.4, -p.size * .7, p.size * 2.8, p.size * 1.4); ctx.restore(); break;
            }
            case 'snow': { // 六角雪花
                const r = p.size * 1.8; p.x += Math.sin(p.life * .08 + p.wob) * .4;
                ctx.save(); ctx.translate(p.x, p.y); ctx.rotate(p.rot);
                ctx.strokeStyle = `rgba(225,248,255,${t})`; ctx.lineWidth = 1.1; ctx.shadowBlur = 5; ctx.shadowColor = '#9fe8ff';
                ctx.beginPath();
                for (let k = 0; k < 3; k++) { const a = k * Math.PI / 3; ctx.moveTo(Math.cos(a) * r, Math.sin(a) * r); ctx.lineTo(-Math.cos(a) * r, -Math.sin(a) * r); }
                ctx.stroke(); ctx.restore(); break;
            }
            default: { // ember：金色余烬 / 碎片
                if (p.shard) {
                    ctx.save(); ctx.translate(p.x, p.y); ctx.rotate(p.rot);
                    ctx.fillStyle = `hsl(${p.hue}, 100%, ${60 + t * 30}%)`;
                    ctx.beginPath(); ctx.moveTo(0, -p.size * 2.2); ctx.lineTo(p.size, 0); ctx.lineTo(0, p.size * 2.2); ctx.lineTo(-p.size, 0); ctx.closePath(); ctx.fill();
                    ctx.restore();
                } else {
                    const g = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, p.size * 3);
                    g.addColorStop(0, `hsla(${p.hue}, 100%, 85%, 1)`); g.addColorStop(0.35, `hsla(${p.hue}, 100%, 60%, .8)`); g.addColorStop(1, `hsla(${p.hue}, 100%, 50%, 0)`);
                    ctx.fillStyle = g; ctx.beginPath(); ctx.arc(p.x, p.y, p.size * 3, 0, Math.PI * 2); ctx.fill();
                }
            }
        }
    }
    function tick() {
        const r = layer.getBoundingClientRect();
        ctx.clearRect(0, 0, r.width, r.height);
        particles = particles.filter(p => p.life < p.max);
        for (const p of particles) {
            p.life++; p.x += p.vx; p.y += p.vy; p.vy += p.g; p.vx *= p.drag; p.vy *= (p.g < 0 ? p.drag : 1); p.rot += p.vr;
            const t = 1 - p.life / p.max;
            ctx.globalCompositeOperation = p.add ? 'lighter' : 'source-over';
            ctx.globalAlpha = Math.min(1, t * 1.6);
            drawP(p, t);
        }
        ctx.globalAlpha = 1; ctx.globalCompositeOperation = 'source-over';
        if (particles.length) requestAnimationFrame(tick); else { running = false; ctx.clearRect(0, 0, r.width, r.height); }
    }
    /* 持续飘洒（AK 后的余烬 / 雪 / 火 / 彩带…） */
    function drizzle(count, every) {
        const { w, h } = center();
        let n = 0;
        const iv = setInterval(() => {
            const t = theme.p;
            if (t === 'flame' || t === 'bubble') burst(Math.random() * w, h + 6, 3, { dir: -Math.PI / 2, spread: .5, speed: 1.4, life: 90, size: 2.2, lift: 0 });
            else if (t === 'confetti' || t === 'pixel') burst(Math.random() * w, -6, 3, { dir: Math.PI / 2, spread: .8, speed: 1.5, life: 140, size: 3, lift: 0, gravity: .02 });
            else if (t === 'neon') burst(Math.random() * w, Math.random() * h, 2, { speed: 3, life: 25, size: 2 });
            else if (t === 'ink') { if (n % 3 === 0) burst(Math.random() * w, Math.random() * h, 1, { speed: .2, life: 60, size: 4, lift: 0, gravity: 0 }); }
            else burst(Math.random() * w, -6, 3, { dir: Math.PI / 2, spread: 0.6, speed: 1.2, life: 120, size: t === 'snow' ? 2.4 : 1.8, lift: 0, gravity: 0.01 });
            if (++n > count) clearInterval(iv);
        }, every);
    }

    /* ---------- 基础：击杀者行 + 阵亡行 ---------- */
    function rowHit(team, idx, slashes, dur) {
        const row = rowOf(team, idx);
        if (!row) return null;
        const r = localRect(row);
        const glow = add(div('fx-rowglow', { left: r.x + 'px', top: r.y + 'px', width: r.w + 'px', height: r.h + 'px' }), dur);
        glow.style.setProperty('--fx-dur', dur + 'ms');

        const num = row.querySelector('.kill-kill-number');
        if (num) {
            num.style.setProperty('--fx-pop', getComputedStyle(layer).getPropertyValue('--fx-accent'));
            num.classList.remove('fx-pop'); void num.offsetWidth; num.classList.add('fx-pop');
            const n = localRect(num);
            add(div('fx-plus', { left: n.x + n.w / 2 + 'px', top: n.y - 4 + 'px' }), 1200).textContent = '+1';
            burst(n.x + n.w / 2, n.y + n.h / 2, 14 + slashes * 8, { speed: 2.4, life: 38, size: 2 });
        }
        if (slashes > 0) { // 刀光：单杀 1 道，双杀交叉 2 道，三杀 3 道
            const angles = [[-4], [-7, 7], [-8, 0, 8]][Math.min(slashes, 3) - 1];
            angles.forEach((deg, i) => {
                const len = r.w * 0.62;
                add(div('fx-slash', {
                    left: r.x + (r.w - len) / 2 + 'px', top: r.y + r.h / 2 - 2 + 'px', width: len + 'px',
                    transformOrigin: 'center center', transform: `rotate(${deg}deg)`, animationDelay: i * 90 + 'ms'
                }), 900);
            });
        }
        return r;
    }
    function rowDead(team, idx) {
        const row = rowOf(team, idx);
        if (!row) return;
        const r = localRect(row);
        add(div('fx-deadflash', { left: r.x + 'px', top: r.y + 'px', width: r.w + 'px', height: r.h + 'px' }), 1300);
        const name = row.querySelector('.kill-player-name');
        const nr = name ? localRect(name) : r;
        add(div('fx-mark ' + theme.mark, { left: nr.x + Math.min(nr.w, 90) * 0.5 + 'px', top: nr.y + nr.h / 2 + 'px' }), 1000);
        row.classList.remove('fx-dead'); void row.offsetWidth; row.classList.add('fx-dead');
        setTimeout(() => row.classList.remove('fx-dead'), 1500);
        const deadOpt = { ember: { hue: 0 }, flame: { hue: 0 }, spark: { hue: 0, type: 'ember' } }[theme.p] || {};
        burst(nr.x + 30, nr.y + nr.h / 2, 12, { speed: 2, life: 30, size: 1.8, ...deadOpt });
    }

    /* ---------- 横幅 ---------- */
    function banner(team, title, sub, name, dur, extra = '', mini = false) {
        const b = div('fx-banner' + (mini ? ' mini' : ''));
        b.style.setProperty('--fx-dur', dur + 'ms');
        const tag = skin === 'ink' ? (team === 'red' ? '朱' : '青') : (team === 'red' ? '红队' : '蓝队');
        b.innerHTML = `<i class="fx-blade l"></i><i class="fx-blade r"></i>
            <div class="fx-banner-title" data-text="${esc(title)}">${esc(title)}</div>
            <div class="fx-banner-sub">${esc(sub)}</div>
            <div class="fx-banner-name"><b>${tag}</b>${esc(name)}${extra ? `<em>${extra}</em>` : ''}</div>`;
        add(b, dur + 50);
        return b;
    }
    function shake() {
        const board = root.querySelector('.kill-board');
        if (!board) return;
        board.classList.remove('fx-shake'); void board.offsetWidth; board.classList.add('fx-shake');
        setTimeout(() => board.classList.remove('fx-shake'), 600);
    }

    /* ---------- AK：各风格装饰 ---------- */
    const MAGIC_SVG = `<svg viewBox="0 0 300 300"><defs>
        <path id="fxrun" d="M150 150 m-118 0 a118 118 0 1 1 236 0 a118 118 0 1 1 -236 0"/></defs>
        <g class="c1" fill="none" stroke="#f5dc9c">
            <circle cx="150" cy="150" r="140" stroke-width="2"/>
            <circle cx="150" cy="150" r="128" stroke-width="1" stroke-dasharray="3 6"/>
            <text font-size="15" font-family="serif" fill="#ffe39a" stroke="none" letter-spacing="7"><textPath href="#fxrun">ᚨᚲ ✦ ALL KILL ✦ ᛟᚱ ✦ 一人团灭 ✦ ᛞᚾᚠ ✦ ALL KILL ✦ ᚹᛁ ✦</textPath></text>
        </g>
        <g class="c2" fill="none" stroke="#ffb84a" stroke-width="2">
            <polygon points="150,48 238,201 62,201"/><polygon points="150,252 62,99 238,99"/>
            <circle cx="150" cy="150" r="102" stroke-width="1.5"/>
        </g></svg>`;
    const ENSO_SVG = `<svg class="fx-ak-enso" viewBox="0 0 290 290"><path d="M168 30 C 90 18, 28 80, 34 150 C 40 222, 108 266, 170 256 C 236 246, 270 190, 258 128 C 248 76, 206 44, 150 40"/></svg>`;
    const FLAKE_SVG = `<svg viewBox="-150 -150 300 300"><g fill="none" stroke="#e6fbff" stroke-width="3" stroke-linecap="round">
        ${[0, 60, 120, 180, 240, 300].map(a => `<g transform="rotate(${a})"><path d="M0 0 V-132 M0 -48 l-22 -22 M0 -48 l22 -22 M0 -88 l-18 -18 M0 -88 l18 -18"/><path d="M0 -132 l-6 8 M0 -132 l6 8" stroke-width="2"/></g>`).join('')}
        <polygon points="0,-40 34.6,-20 34.6,20 0,40 -34.6,20 -34.6,-20" stroke-width="2"/></g></svg>`;
    function akDeco(team) {
        switch (theme.deco) {
            case 'magic': return `<div class="fx-ak-rays"></div><div class="fx-ak-circle">${MAGIC_SVG}</div>`;
            case 'bars': return `<div class="fx-ak-bars"><i></i><i></i></div>`;
            case 'rings': return `<div class="fx-ak-rings"><i></i><i></i><i></i></div>`;
            case 'enso': return ENSO_SVG;
            case 'glass': return `<div class="fx-ak-glass"></div>`;
            case 'pixel': return `<div class="fx-ak-pixgrid"></div>`;
            case 'fire': return `<div class="fx-ak-fire"></div><div class="fx-ak-rays" style="--fx-accent:255,120,30"></div>`;
            case 'stinger': return `<div class="fx-ak-stinger"><i></i><i></i><i></i></div>`;
            case 'flake': return `<div class="fx-ak-frost"></div><div class="fx-ak-flake">${FLAKE_SVG}</div>`;
        }
        return '';
    }
    function ak(team, idx, name) {
        const wrap = div('fx-ak' + (theme.bands ? ' bands' : ''));
        const flash = ['ink', 'glass', 'broadcast'].includes(skin) ? '' : '<div class="fx-flash"></div>';
        wrap.innerHTML = `<div class="fx-ak-dim"></div><div class="fx-ak-band t"></div><div class="fx-ak-band b"></div>
            ${akDeco(team)}
            <div class="fx-ak-core"><div class="fx-ak-text" data-text="${esc(theme.akText)}">${esc(theme.akText)}</div>
            <div class="fx-ak-sub"><b>${esc(name)}</b> ${esc(theme.akSub)}</div>${theme.akEn ? `<div class="fx-ak-en">${esc(theme.akEn)}</div>` : ''}</div>
            ${skin === 'ink' ? '<div class="fx-ak-seal">团灭</div>' : ''}${flash}`;
        add(wrap, 4300);
        const { cx, cy } = center();
        const big = { ember: { shard: true }, flame: { lift: 2 }, snow: { speed: 4 }, ink: { speed: 3, size: 3 }, bubble: { speed: 2.5, size: 3 }, pixel: { size: 3 }, confetti: { size: 3.2, lift: 2 } }[theme.p] || {};
        setTimeout(() => { if (!['glass', 'ink'].includes(skin)) shake(); burst(cx, cy, 90, { speed: 6, life: 70, size: 2.6, gravity: undefined, ...big }); }, 350);
        setTimeout(() => burst(cx, cy, 40, { speed: 3.5, life: 90, size: 2.2, lift: 1.2 }), 700);
        drizzle(40, 70);
        setTimeout(() => rowOf(team, idx)?.querySelector('.kill-ak-mark')?.classList.add('fx-ak-glow'), 3800);
    }

    const LEVEL_TEXT = {
        2: ['双杀', 'DOUBLE KILL'], 3: ['三杀', 'TRIPLE KILL'],
        first: ['一血', 'FIRST BLOOD'], shutdown: ['终结', 'SHUT DOWN'], revenge: ['复仇', 'REVENGE']
    };
    const INK_TEXT = { 2: '双斩', 3: '三斩', first: '首胜', shutdown: '断其锋', revenge: '雪耻' };
    const PIXEL_TEXT = { 2: 'DOUBLE!', 3: 'TRIPLE!!', first: '1ST BLOOD', shutdown: 'STOPPED!', revenge: 'REVENGE!' };

    let enabled = true;
    function setEnabled(on) { enabled = !!on; if (!enabled) { particles.length = 0; layer.innerHTML = ''; layer.appendChild(canvas); } }

    function play({ team = 'red', killerRow = 0, victimRow = 0, level = 1, name = '', victim = '' }) {
        if (!enabled) return;
        syncSkin();
        layer.style.setProperty('--fx-team', TEAM_RGB[team]);
        const enemy = team === 'red' ? 'blue' : 'red';
        const { cx, cy } = center();
        if (level === 'ak') {
            rowHit(team, killerRow, 3, 1800);
            rowDead(enemy, victimRow);
            setTimeout(() => ak(team, killerRow, name), 250);
            return;
        }
        const special = typeof level === 'string';
        rowHit(team, killerRow, special ? 1 : level, level === 1 ? 1500 : 2000);
        rowDead(enemy, victimRow);
        if (level === 1) return;

        let [title, sub] = LEVEL_TEXT[level];
        if (skin === 'ink') title = INK_TEXT[level];
        if (skin === 'pixel') { title = PIXEL_TEXT[level]; }
        const extra = level === 'shutdown' && victim ? `终结了 ${esc(victim)} 的连杀`
            : level === 'revenge' && victim ? `向 ${esc(victim)} 复仇` : '';
        if (level === 2) {
            banner(team, title, sub, name, 2200);
            setTimeout(() => burst(cx, cy, 40, { speed: 4, life: 50 }), 200);
        } else if (level === 3) {
            add(div('fx-edge'), 2500);
            add(div('fx-ring'), 900);
            setTimeout(() => add(div('fx-ring'), 900), 160);
            banner(team, title, sub, name, 2600);
            shake();
            setTimeout(() => burst(cx, cy, 80, { speed: 5.5, life: 60, size: 2.6, shard: true }), 180);
        } else {
            // 一血 / 终结 / 复仇：迷你横幅
            banner(team, title, sub, name, 1900, extra, true);
            if (level === 'first') setTimeout(() => burst(cx, cy, 36, { speed: 3.5, life: 45, hue: theme.p === 'ember' ? 0 : undefined }), 150);
            if (level === 'shutdown') { add(div('fx-ring'), 900); setTimeout(() => burst(cx, cy, 44, { speed: 4.5, life: 50, shard: true }), 150); }
            if (level === 'revenge') setTimeout(() => burst(cx, cy, 30, { speed: 3, life: 55, hue: TEAM_HUE[team] }), 150);
        }
    }
    return { play, burst, setEnabled, THEMES };
})();

/* ================= 击杀事件识别 =================
 * 依据 /api/state 的 kills / deaths / akCount / currentStreak 变化推断：
 *   单杀 / 双杀 / 三杀 / AK / 一血 / 终结 / 复仇
 * 连杀直接使用主程序的 currentStreak（任何人击杀会清零其他人，每局结束全部清零）。
 * 只做推断，不改动任何战绩数据。
 * ================================================= */
const KillFxTracker = (() => {
    const QUEUE_STEP = 240;        // 同一次刷新里多次击杀的播放间隔
    const MAX_QUEUE = 12;          // 队列上限，防止异常数据堆积

    const keyOf = p => `${Number(p?.team) === 1 ? 1 : 0}|${p?.name || ''}`;
    // 当前局序号 = 红蓝大比分之和（第 1 局为 0）
    const roundOf = d => (Number(d?.redScore) || 0) + (Number(d?.blueScore) || 0);

    let prev = null;            // Map<key, {kills, deaths, ak, streak, team, name}>
    let prevRound = 0;          // prev 对应的局序号
    let killsByRound = new Map(); // 局序号 -> Set<"凶手key>死者key">，用于复仇判定
    let firstBlood = false;
    let queue = [];
    let timer = null;
    let enabled = true;

    function snapshot(players) {
        const map = new Map();
        (players || []).forEach(p => {
            if (!p || !p.name) return;
            const team = Number(p.team) === 1 ? 1 : 0;
            map.set(keyOf(p), {
                kills: Number(p.kills) || 0,
                deaths: Number(p.deaths) || 0,
                ak: Number(p.akCount) || 0,
                streak: Number(p.currentStreak) || 0,
                team,
                name: p.name
            });
        });
        return map;
    }

    function resetMatch() {
        killsByRound = new Map();
        queue = [];
        firstBlood = false;
        if (timer) { clearTimeout(timer); timer = null; }
    }

    function recordKill(round, killerKey, victimKey) {
        let set = killsByRound.get(round);
        if (!set) { set = new Set(); killsByRound.set(round, set); }
        set.add(`${killerKey}>${victimKey}`);
        // 复仇只看上一局，更早的局无需保留
        killsByRound.forEach((_, r) => { if (r < round - 1) killsByRound.delete(r); });
    }

    // 复仇：本次死者在「上一局」击杀过本次凶手
    function isRevenge(round, killerKey, victimKey) {
        if (round <= 0) return false;
        const set = killsByRound.get(round - 1);
        return !!set && set.has(`${victimKey}>${killerKey}`);
    }

    function rowIndexOf(teamColor, name) {
        const rows = document.querySelectorAll(`.kill-team[data-team-color="${teamColor}"] .kill-rows .kill-row`);
        for (let i = 0; i < rows.length; i++) {
            const cell = rows[i].querySelector('.kill-player-name');
            if (cell && cell.textContent === name) return i;
        }
        return -1;
    }

    function drain() {
        timer = null;
        const evt = queue.shift();
        if (!evt) return;
        try { KillFx.play(evt); } catch (err) { /* 特效异常不影响记分板 */ }
        if (queue.length) timer = setTimeout(drain, QUEUE_STEP);
    }

    function enqueue(evt) {
        if (queue.length >= MAX_QUEUE) queue.length = 0;
        queue.push(evt);
        if (!timer) timer = setTimeout(drain, 10);
    }

    function setEnabled(on) {
        enabled = !!on;
        if (typeof KillFx !== 'undefined') KillFx.setEnabled(enabled);
        if (!enabled) resetMatch();
    }

    function ingest(data) {
        if (!enabled || document.hidden) return;
        // 本次新增的击杀归属上一次状态所在的局：决胜击杀与大比分 +1 会在同一次刷新里到达。
        const killRound = prevRound;
        prevRound = roundOf(data);
        const players = Array.isArray(data?.players) ? data.players : [];
        if (!players.length) { prev = null; return; }

        const cur = snapshot(players);
        if (!cur.size) { prev = null; return; }
        if (!prev) { prev = cur; return; }

        // 新的一局：战绩全部归零，或选手名单发生变化
        let total = 0;
        cur.forEach(p => { total += p.kills + p.deaths; });
        const roster = [...cur.keys()].sort().join('|');
        const prevRoster = [...prev.keys()].sort().join('|');
        if (total === 0 || roster !== prevRoster) {
            prev = cur;
            if (total === 0) resetMatch();
            return;
        }

        const killers = [];
        const deathsByTeam = { 0: [], 1: [] };
        cur.forEach((c, key) => {
            const p = prev.get(key);
            if (!p) return;
            const dk = c.kills - p.kills;
            const dd = c.deaths - p.deaths;
            const da = c.ak - p.ak;
            if (dk > 0) killers.push({ key, c, dk, da });
            // prevStreak：死者阵亡前的连杀数，用于「终结」判定
            for (let i = 0; i < dd; i++) deathsByTeam[c.team].push({ key, c, prevStreak: p.streak });
        });
        const prevOf = prev;
        prev = cur;
        if (!killers.length) return;

        killers.sort((a, b) => a.c.team - b.c.team);

        killers.forEach(k => {
            const enemyTeam = k.c.team === 1 ? 0 : 1;
            const pool = deathsByTeam[enemyTeam];
            let cursor = 0;
            // 本次最后一杀的连杀序号：
            //   产生 AK → 第 4 杀；
            //   currentStreak > 0 → 直接采用主程序数据；
            //   否则（决胜击杀后整局清零）→ 用上一帧连杀数 + 本次击杀数。
            const before = prevOf.get(k.key)?.streak || 0;
            const lastN = k.da > 0 ? 4 : (k.c.streak > 0 ? k.c.streak : before + k.dk);

            for (let i = 0; i < k.dk; i++) {
                const v = pool[cursor] || pool[pool.length - 1];
                if (v) cursor++;
                const n = Math.max(1, lastN - (k.dk - 1 - i));
                const isAk = n >= 4 || (k.da > 0 && i === k.dk - 1);
                const vCount = v ? (v.prevStreak || 0) : 0;
                const revenge = !!v && isRevenge(killRound, k.key, v.key);

                // 优先级：AK > 一血 > 终结 > 复仇 > 连杀数
                let level;
                if (isAk) level = 'ak';
                else if (!firstBlood) level = 'first';
                else if (vCount >= 2) level = 'shutdown';
                else if (revenge) level = 'revenge';
                else level = n;
                if (level === 'ak' || level === 'first') firstBlood = true;

                if (v) recordKill(killRound, k.key, v.key);

                enqueue({
                    team: k.c.team === 1 ? 'blue' : 'red',
                    killerRow: rowIndexOf(k.c.team === 1 ? 'blue' : 'red', k.c.name),
                    victimRow: v ? rowIndexOf(enemyTeam === 1 ? 'blue' : 'red', v.c.name) : -1,
                    level,
                    name: k.c.name,
                    victim: v ? v.c.name : ''
                });
                if (isAk) break;
            }
        });
    }

    return { ingest, setEnabled, resetMatch };
})();

/* ---------- 与记分板 / 设置面板联动 ---------- */
(function () {
    const origRender = renderKillDisplay;
    renderKillDisplay = function (data) {
        origRender(data);
        try { KillFxTracker.ingest(data); } catch (err) { /* ignore */ }
    };

    const origSyncSkinPanel = syncKillSkinPanel;
    syncKillSkinPanel = function () {
        origSyncSkinPanel();
        const box = document.getElementById('kill-fx-enabled');
        if (box) box.checked = killDisplaySettings?.layout?.fxEnabled !== 0;
    };

    // 设置从服务端同步下来时，同步特效开关
    const origApplySettings = applyKillDisplaySettings;
    applyKillDisplaySettings = function (settings) {
        origApplySettings(settings);
        const on = killDisplaySettings?.layout?.fxEnabled !== 0;
        KillFxTracker.setEnabled(on);
        const box = document.getElementById('kill-fx-enabled');
        if (box) box.checked = on;
    };

    const fxBox = document.getElementById('kill-fx-enabled');
    fxBox?.addEventListener('change', () => {
        killDisplaySettings.layout.fxEnabled = fxBox.checked ? 1 : 0;
        KillFxTracker.setEnabled(fxBox.checked);
        queueKillDisplaySettingsSave();
        showStatus(fxBox.checked ? '击杀特效：开' : '击杀特效：关', false);
    });
    KillFxTracker.setEnabled(killDisplaySettings?.layout?.fxEnabled !== 0);

    window.KillFx = KillFx;
    window.KillFxTracker = KillFxTracker;
})();
