const KILL_STATE_URL = 'http://127.0.0.1:18777/api/state';
const KILL_SETTINGS_URL = 'http://127.0.0.1:18777/api/kill-display-settings';

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
    akColumnWidth: 24,
    pageScale: 100,
    teamNameOffsetX: -9,
    teamNameOffsetY: -9,
    pickLabelOffsetX: -7,
    pickLabelOffsetY: 0,
    playerNameOffsetX: 0,
    playerNameOffsetY: 0,
    killNumberOffsetX: -21,
    killNumberOffsetY: 0,
    deathNumberOffsetX: -11,
    deathNumberOffsetY: 0,
    akMarkOffsetX: 2,
    akMarkOffsetY: 0,
    akCountBadgeOffsetX: 8,
    akCountBadgeOffsetY: -25
};

const KILL_DISPLAY_LAYOUT_LIMITS = {
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
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 31, colorMode: 'custom', color: '#a9abb9', strokeColor: '#000000', strokeWidth: 2, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'pickLabel',
        cssKey: 'pick-label',
        label: '选人顺序',
        allowTeamColor: false,
        defaults: { fontFamily: 'Arial Black', fontSize: 27, colorMode: 'custom', color: '#6fc8b9', strokeColor: '#000000', strokeWidth: 3, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'playerName',
        cssKey: 'player-name',
        label: '主号',
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
        normalized[key] = KILL_HORIZONTAL_ONLY_OFFSET_Y_KEYS.includes(key)
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
        obsUrl: String(settings?.obsUrl || 'http://127.0.0.1:18777/kill.html'),
        layout: normalizeKillDisplayLayout(settings?.layout || {}),
        textStyles: normalizeKillDisplayTextStyles(settings?.textStyles || {})
    };
    killDisplaySettings = normalized;

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
    root.style.setProperty('--kill-page-scale', String(layout.pageScale / 100));
    EDITABLE_STYLE_KEYS.forEach(setOffsetVars);

    KILL_DISPLAY_TEXT_STYLE_TYPES.forEach(type => {
        setStyleVars(type, normalized.textStyles[type.key]);
    });
    syncKillEditToolbar();
    scheduleFitKillTextElements();
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
    const parts = [];
    if (offset.x !== 0 || offset.y !== 0) {
        parts.push(`translate(${offset.x}px, ${offset.y}px)`);
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

function bindKillEditToolbar() {
    const target = document.getElementById('kill-edit-target');
    const font = document.getElementById('kill-edit-font');
    const size = document.getElementById('kill-edit-size');
    const color = document.getElementById('kill-edit-color');
    const stroke = document.getElementById('kill-edit-stroke');
    const letter = document.getElementById('kill-edit-letter');
    const glow = document.getElementById('kill-edit-glow');
    const width = document.getElementById('kill-edit-width');

    target?.addEventListener('change', () => setSelectedKillStyleKey(target.value));
    font?.addEventListener('change', () => updateSelectedStyle({ fontFamily: font.value }));
    size?.addEventListener('input', () => updateSelectedStyle({ fontSize: size.value }));
    color?.addEventListener('input', () => updateSelectedStyle({ color: color.value, colorMode: 'custom' }));
    stroke?.addEventListener('input', () => updateSelectedStyle({ strokeWidth: stroke.value }));
    letter?.addEventListener('input', () => updateSelectedStyle({ letterSpacing: letter.value }));
    glow?.addEventListener('input', () => updateSelectedStyle({ glow: glow.value }));
    width?.addEventListener('input', () => updateSelectedLayout(width.value));
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

function toggleKillEditMode(force) {
    killEditMode = typeof force === 'boolean' ? force : !killEditMode;
    const root = document.getElementById('kill-display-root');
    const toolbar = document.getElementById('kill-edit-toolbar');
    root?.classList.toggle('edit-mode', killEditMode);
    if (toolbar) toolbar.hidden = !killEditMode;
    refreshSelectedStyleMarker();
    if (killEditMode) syncKillEditToolbar();
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

function postKillHostCommand(action) {
    if (!window.chrome?.webview) return;
    window.chrome.webview.postMessage({ action });
}

function bindWindowControls() {
    const root = document.getElementById('kill-display-root');
    const grip = document.getElementById('kill-resize-grip');
    if (root) {
        root.addEventListener('pointerdown', beginKillLayoutDrag);
        root.addEventListener('mousedown', (event) => {
            if (event.button !== 0) return;
            if (killEditMode) return;
            if (event.target?.closest?.('.kill-resize-grip, .kill-edit-toolbar')) return;
            event.preventDefault();
            postKillHostCommand('cmd_kill_window_drag');
        });
        root.addEventListener('dblclick', (event) => {
            if (event.target?.closest?.('.kill-edit-toolbar')) return;
            if (event.target?.closest?.('[data-style-key]')) return;
            toggleKillEditMode();
        });
        root.addEventListener('click', (event) => {
            if (!killEditMode) return;
            if (suppressClickAfterDrag) return;
            if (event.target?.closest?.('.kill-edit-toolbar, .kill-resize-grip')) return;
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
bindKillEditToolbar();
bindWindowControls();
fetchKillDisplayState();
setInterval(fetchKillDisplayState, 250);
