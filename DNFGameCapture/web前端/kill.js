const KILL_STATE_URL = 'http://127.0.0.1:18777/api/state';
const KILL_VOICE_URL = 'http://127.0.0.1:18777/api/voice/play';
const KILL_SETTINGS_URL = 'http://127.0.0.1:18777/api/kill-display-settings';
// 场景规则（scene-rules.js 提供规则定义与匹配；SceneRuntime 负责从主程序加载 / 热更新规则）
const SCENE_RULES_URL = 'http://127.0.0.1:18777/api/scene-rules';
const SCENE_STATE_URL = 'http://127.0.0.1:18777/api/scene-state';
const KILL_SAY_URL = 'http://127.0.0.1:18777/api/voice/say';
const KILL_VOICE_FILE_URL = 'http://127.0.0.1:18777/api/voice/file';
const KILL_TTS_URL = 'http://127.0.0.1:18777/api/voice/tts';
const KILL_LIB_URL = 'http://127.0.0.1:18777/api/voice/lib';
let SceneRuntime = null;
// 全屏特效窗口（C++ CKillFxDlg）以 kill.html?mode=fx 打开同一页面：只渲染特效层，记分板隐藏。
const KILL_FX_FULLSCREEN = /(?:^|[?&])mode=fx(?:&|$)/.test(location.search);
if (KILL_FX_FULLSCREEN) document.documentElement.classList.add('kill-fx-fullscreen');

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
// 默认风格：水墨国风（未保存过风格、或保存值无效时使用）
const KILL_DISPLAY_DEFAULT_SKIN = Math.max(0, KILL_DISPLAY_SKINS.findIndex(skin => skin.id === 'ink'));

function getKillDisplaySkin(index) {
    return KILL_DISPLAY_SKINS[clampNumber(index, 0, KILL_DISPLAY_SKIN_MAX, KILL_DISPLAY_DEFAULT_SKIN)] || KILL_DISPLAY_SKINS[KILL_DISPLAY_DEFAULT_SKIN];
}

const KILL_DISPLAY_LAYOUT_DEFAULTS = {
    skin: KILL_DISPLAY_DEFAULT_SKIN,
    bgImageRev: 0,
    bgImageScale: 100,
    bgImageX: 0,
    bgImageY: 0,
    bgImageOpacity: 100,
    fxEnabled: 1,
    fxFullscreen: 0,
    fxFullscreenScale: 100,
    fxFullscreenTipOff: 0,
    // 特效管理（主窗口「特效管理」面板维护）
    fxTextOn: 1, fxKillOn: 1,
    fxEvtDouble: 1, fxEvtTriple: 1, fxEvtFirst: 1, fxEvtShutdown: 1, fxEvtRevenge: 1, fxEvtAk: 1, fxEvtVictory: 1,
    fxTextDelay: 0, fxTextIn: 0, fxTextMs: 3500,
    fxKillDelay: 0, fxKillIn: 0, fxKillMs: 3500,
    fxFsDelay: 0, fxFsIn: 0, fxFsMs: 3500,
    fxVoiceOn: 0, fxVoice: 0, fxVoiceStyle: 0,
    showDeathNumber: 1,
    bgAlpha: 0,
    panelAlpha: 31,
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
    fxFullscreen: [0, 1],
    fxFullscreenScale: [50, 200],
    fxFullscreenTipOff: [0, 1],
    fxTextOn: [0, 1], fxKillOn: [0, 1],
    fxEvtDouble: [0, 1], fxEvtTriple: [0, 1], fxEvtFirst: [0, 1], fxEvtShutdown: [0, 1], fxEvtRevenge: [0, 1], fxEvtAk: [0, 1], fxEvtVictory: [0, 1],
    fxTextDelay: [0, 5000], fxTextIn: [0, 2000], fxTextMs: [1000, 10000],
    fxKillDelay: [0, 5000], fxKillIn: [0, 2000], fxKillMs: [1000, 10000],
    fxFsDelay: [0, 5000], fxFsIn: [0, 2000], fxFsMs: [1000, 10000],
    fxVoiceOn: [0, 1], fxVoice: [0, 65535], fxVoiceStyle: [0, 9],
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
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 31, colorMode: 'custom', color: '#b9ab8f', strokeColor: '#000000', strokeWidth: 2, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'pickLabel',
        cssKey: 'pick-label',
        label: '选人顺序',
        allowTeamColor: false,
        defaults: { fontFamily: 'Arial Black', fontSize: 27, colorMode: 'custom', color: '#c8a86a', strokeColor: '#000000', strokeWidth: 3, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'playerName',
        cssKey: 'player-name',
        label: '选手',
        allowTeamColor: false,
        defaults: { fontFamily: 'Arial', fontSize: 43, colorMode: 'custom', color: '#f2ead8', strokeColor: '#000000', strokeWidth: 5, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'killNumber',
        cssKey: 'kill-number',
        label: '杀',
        allowTeamColor: false,
        defaults: { fontFamily: 'FZXS24', fontSize: 50, colorMode: 'custom', color: '#f2ead8', strokeColor: '#000000', strokeWidth: 4, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'deathNumber',
        cssKey: 'death-number',
        label: '死',
        allowTeamColor: false,
        defaults: { fontFamily: 'FZXS24', fontSize: 50, colorMode: 'custom', color: '#9c9486', strokeColor: '#000000', strokeWidth: 4, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'akMark',
        cssKey: 'ak-mark',
        label: 'AK',
        allowTeamColor: false,
        defaults: { fontFamily: 'FZXS24', fontSize: 40, colorMode: 'custom', color: '#e0473a', strokeColor: '#000000', strokeWidth: 3, glow: 0, letterSpacing: 0 }
    },
    {
        key: 'akCountBadge',
        cssKey: 'ak-count',
        label: 'AK次数',
        allowTeamColor: false,
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 30, colorMode: 'custom', color: '#e0473a', strokeColor: '#000000', strokeWidth: 1, glow: 0, letterSpacing: 0 }
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
let killSettingsWriteFailed = false;
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
        skin.value = String(clampNumber(killDisplaySettings.layout.skin, 0, KILL_DISPLAY_SKIN_MAX, KILL_DISPLAY_DEFAULT_SKIN));
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
    killDisplaySettings.layout.skin = clampNumber(index, 0, KILL_DISPLAY_SKIN_MAX, KILL_DISPLAY_DEFAULT_SKIN);
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
        let payload = null;
        try { payload = await response.json(); } catch (parseErr) { payload = null; }
        if (payload?.error === 'config_write_failed') {
            // 配置写不进去：保留当前编辑结果，不要被主程序读回的旧值"拽回去"
            killSettingsWriteFailed = true;
            showStatus(payload.message || '保存失败：config.ini 无法写入，请检查是否只读或权限不足', true);
            return;
        }
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        killSettingsWriteFailed = false;
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
    killDisplaySettings.layout.skin = clampNumber(index, 0, KILL_DISPLAY_SKIN_MAX, KILL_DISPLAY_DEFAULT_SKIN);
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
            if (!isSavingKillSettings && !killSettingsWriteFailed && Date.now() >= suppressRemoteKillSettingsUntil) {
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

    /* ---------- 特效分组：延迟 / 出现（淡入）/ 持续时间倍率 ----------
     * cur.host = 本组特效的容器；cur.dur = 持续时间（毫秒，横幅 / 行高亮直接用这个时长）；
     * cur.k = 时间倍率（AK 这类固定编排的大场面按「持续时间 / 4300ms」整体缩放）。 */
    let cur = { host: layer, k: 1, dur: 0 };
    function withCtx(c, fn) { const prev = cur; cur = c; try { fn(); } finally { cur = prev; } }
    function later(fn, ms) { const c = cur; return setTimeout(() => withCtx(c, fn), ms * c.k); }
    function speed(el, subtree = true) {
        if (cur.k === 1 || !el?.getAnimations) return el;
        try { el.getAnimations({ subtree }).forEach(a => { a.playbackRate = 1 / cur.k; }); } catch (err) { /* ignore */ }
        return el;
    }
    function add(el, ms) { cur.host.appendChild(el); speed(el); setTimeout(() => el.remove(), ms * cur.k); return el; }
    function group(delayMs, inMs, durMs, fn) {
        const c = { host: layer, k: 1, dur: Math.max(1000, Math.min(10000, Number(durMs) || 3500)) };
        const start = () => {
            if (!enabled) return;
            const host = document.createElement('div');
            host.className = 'fx-group';
            const fade = Math.max(0, Number(inMs) || 0);
            if (fade > 0) host.style.animation = `fx-group-in ${fade}ms ease-out both`;
            layer.appendChild(host);
            c.host = host;
            withCtx(c, fn);
            setTimeout(() => host.remove(), c.dur + 1500 + fade);
        };
        const delay = Math.max(0, Number(delayMs) || 0);
        if (delay > 0) setTimeout(start, delay); else start();
    }
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
        const k = cur.k; // 持续时间倍率：粒子寿命变长、速度变慢，轨迹基本不变
        for (let i = 0; i < n; i++) {
            const a = (opts.dir ?? 0) + (Math.random() - 0.5) * (opts.spread ?? Math.PI * 2);
            const v = (opts.speed ?? 3) * (0.35 + Math.random()) / k;
            particles.push({
                type, x, y, vx: Math.cos(a) * v, vy: Math.sin(a) * v - (opts.lift ?? 0.6) / k,
                life: 0, max: (opts.life ?? 50) * (0.6 + Math.random() * 0.8) * k,
                size: (opts.size ?? 2.4) * (0.5 + Math.random()),
                hue: opts.hue ?? (type === 'neon' ? (Math.random() < .5 ? 186 : 312) : type === 'flame' ? rnd(8, 40) : type === 'snow' ? 195 : rnd(32, 54)),
                g: (opts.gravity ?? pre.g) / (k * k), drag: Math.pow(pre.drag, 1 / k), add: pre.add,
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
        const c = cur;
        let n = 0;
        const iv = setInterval(() => withCtx(c, () => {
            const t = theme.p;
            if (t === 'flame' || t === 'bubble') burst(Math.random() * w, h + 6, 3, { dir: -Math.PI / 2, spread: .5, speed: 1.4, life: 90, size: 2.2, lift: 0 });
            else if (t === 'confetti' || t === 'pixel') burst(Math.random() * w, -6, 3, { dir: Math.PI / 2, spread: .8, speed: 1.5, life: 140, size: 3, lift: 0, gravity: .02 });
            else if (t === 'neon') burst(Math.random() * w, Math.random() * h, 2, { speed: 3, life: 25, size: 2 });
            else if (t === 'ink') { if (n % 3 === 0) burst(Math.random() * w, Math.random() * h, 1, { speed: .2, life: 60, size: 4, lift: 0, gravity: 0 }); }
            else burst(Math.random() * w, -6, 3, { dir: Math.PI / 2, spread: 0.6, speed: 1.2, life: 120, size: t === 'snow' ? 2.4 : 1.8, lift: 0, gravity: 0.01 });
            if (++n > count) clearInterval(iv);
        }), every * c.k);
    }

    /* ---------- 基础：击杀者行 + 阵亡行 ---------- */
    function rowHit(team, idx, slashes, dur) {
        if (cur.dur) dur = cur.dur;
        const row = rowOf(team, idx);
        if (!row) return null;
        const r = localRect(row);
        const glow = add(div('fx-rowglow', { left: r.x + 'px', top: r.y + 'px', width: r.w + 'px', height: r.h + 'px' }), dur);
        glow.style.setProperty('--fx-dur', dur + 'ms');

        const num = row.querySelector('.kill-kill-number');
        if (num) {
            num.style.setProperty('--fx-pop', getComputedStyle(layer).getPropertyValue('--fx-accent'));
            num.classList.remove('fx-pop'); void num.offsetWidth; num.classList.add('fx-pop');
            speed(num, false);
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
        speed(row, false);
        later(() => row.classList.remove('fx-dead'), 1500);
        const deadOpt = { ember: { hue: 0 }, flame: { hue: 0 }, spark: { hue: 0, type: 'ember' } }[theme.p] || {};
        burst(nr.x + 30, nr.y + nr.h / 2, 12, { speed: 2, life: 30, size: 1.8, ...deadOpt });
    }

    /* ---------- 横幅 ---------- */
    function banner(team, title, sub, name, dur, extra = '', mini = false) {
        if (cur.dur) dur = cur.dur;
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
        speed(board, false);
        later(() => board.classList.remove('fx-shake'), 600);
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
    function ak(team, idx, name, ov = null) {
        if (cur.dur) { // 按持续时间整体缩放 AK 编排（默认 4.3 秒）
            const c2 = { host: cur.host, k: Math.max(0.3, Math.min(3, cur.dur / 4300)), dur: 0 };
            withCtx(c2, () => ak(team, idx, name, ov));
            return;
        }
        const wrap = div('fx-ak' + (theme.bands ? ' bands' : ''));
        const flash = ['ink', 'glass', 'broadcast'].includes(skin) ? '' : '<div class="fx-flash"></div>';
        wrap.innerHTML = `<div class="fx-ak-dim"></div><div class="fx-ak-band t"></div><div class="fx-ak-band b"></div>
            ${akDeco(team)}
            <div class="fx-ak-core"><div class="fx-ak-text" data-text="${esc(ov?.title || theme.akText)}">${esc(ov?.title || theme.akText)}</div>
            <div class="fx-ak-sub"><b>${esc(name)}</b> ${esc(ov?.sub || theme.akSub)}</div>${theme.akEn ? `<div class="fx-ak-en">${esc(theme.akEn)}</div>` : ''}</div>
            ${skin === 'ink' ? '<div class="fx-ak-seal">团灭</div>' : ''}${flash}`;
        add(wrap, 4300);
        const { cx, cy } = center();
        const big = { ember: { shard: true }, flame: { lift: 2 }, snow: { speed: 4 }, ink: { speed: 3, size: 3 }, bubble: { speed: 2.5, size: 3 }, pixel: { size: 3 }, confetti: { size: 3.2, lift: 2 } }[theme.p] || {};
        later(() => { if (!['glass', 'ink'].includes(skin)) shake(); burst(cx, cy, 90, { speed: 6, life: 70, size: 2.6, gravity: undefined, ...big }); }, 350);
        later(() => burst(cx, cy, 40, { speed: 3.5, life: 90, size: 2.2, lift: 1.2 }), 700);
        drizzle(40, 70);
        if (!KILL_FX_FULLSCREEN) later(() => rowOf(team, idx)?.querySelector('.kill-ak-mark')?.classList.add('fx-ak-glow'), 3800);
    }

    const LEVEL_TEXT = {
        2: ['双杀', 'DOUBLE KILL'], 3: ['三杀', 'TRIPLE KILL'],
        first: ['一血', 'FIRST BLOOD'], shutdown: ['终结', 'SHUT DOWN'], revenge: ['复仇', 'REVENGE']
    };
    const INK_TEXT = { 2: '双斩', 3: '三斩', first: '首胜', shutdown: '断其锋', revenge: '雪耻' };
    const PIXEL_TEXT = { 2: 'DOUBLE!', 3: 'TRIPLE!!', first: '1ST BLOOD', shutdown: 'STOPPED!', revenge: 'REVENGE!' };

    let enabled = true;
    function setEnabled(on) { enabled = !!on; if (!enabled) { cancelVoiceRequests(); particles.length = 0; layer.innerHTML = ''; layer.appendChild(canvas); } }

    // 每种大场面对应「特效管理」里的触发事件开关
    const EVENT_KEY = { 2: 'fxEvtDouble', 3: 'fxEvtTriple', first: 'fxEvtFirst', shutdown: 'fxEvtShutdown', revenge: 'fxEvtRevenge', ak: 'fxEvtAk', victory: 'fxEvtVictory' };

    // 复仇被双杀 / 三杀 / AK 等更高优先级盖住时，主特效播完再追加一次短促的复仇（水墨风格为「雪耻」）
    const REVENGE_FOLLOW_MS = 1500;

    // 场景规则扩展：
    //   scene   = 规则动作 { effect, style, title, sub, voice, voiceText, durationMs }（文字已替换好变量）；
    //   follows = 主特效之后追加播放 [{ level, scene, ms }]；ruled = 由场景规则决定（不再看「触发事件」勾选）；
    //   kind    = 'scene' 表示血量 / 每局开始等非击杀事件：没有击杀行，不播文字特效。
    function play({ team = 'red', killerRow = 0, victimRow = 0, level = 1, name = '', victim = '', revengeAfter = false,
        scene = null, follows = null, ruled = false, kind = 'kill' }) {
        if (!enabled) return;
        syncSkin();
        layer.style.setProperty('--fx-team', TEAM_RGB[team]);
        const L = killDisplaySettings?.layout || {};
        const enemy = team === 'red' ? 'blue' : 'red';
        if (level === 'victory') cancelVoiceRequests();

        // 文字特效（行高亮 / +1 / 数字跳动 / 刀光 / 阵亡标记）：只在击杀小窗播放
        if (kind !== 'scene' && level !== 'victory' && !KILL_FX_FULLSCREEN && L.fxTextOn !== 0) {
            group(L.fxTextDelay, L.fxTextIn, L.fxTextMs, () => {
                if (level === 'ak') rowHit(team, killerRow, 3, 1800);
                else rowHit(team, killerRow, typeof level === 'string' ? 1 : level, level === 1 ? 1500 : 2000);
                rowDead(enemy, victimRow);
            });
        }

        // 击杀特效（横幅 / AK / 粒子）：全屏窗口开启时只在全屏窗口播放，小窗不再触发
        const ownsBig = KILL_FX_FULLSCREEN || (L.fxKillOn !== 0 && L.fxFullscreen !== 1);
        // 语音由负责大场面的窗口请求（全屏特效开启时 = 全屏窗口，否则 = 击杀小窗），不会重复朗读；
        // 只在软件内置窗口里触发（OBS 浏览器源里没有 chrome.webview，不播）。
        const ownsVoice = L.fxVoiceOn === 1 && !!window.chrome?.webview && (KILL_FX_FULLSCREEN || L.fxFullscreen !== 1);
        const [d, i, u0] = KILL_FX_FULLSCREEN ? [L.fxFsDelay, L.fxFsIn, L.fxFsMs] : [L.fxKillDelay, L.fxKillIn, L.fxKillMs];
        const u = scene && scene.durationMs > 0 ? scene.durationMs : u0;
        const delay = Math.max(0, Number(d) || 0);
        const hasBig = level !== 1 && level !== 0;
        const eventOn = ruled || L[EVENT_KEY[level]] !== 0;
        if (hasBig && eventOn && ownsBig) {
            group(d, i, u, () => playBig(team, killerRow, level, name, victim, scene));
        } else if (level === 'ak' && !KILL_FX_FULLSCREEN) {
            // 小窗不播 AK 大场面时，仍给 AK 标记加上常亮光效
            setTimeout(() => rowOf(team, killerRow)?.querySelector('.kill-ak-mark')?.classList.add('fx-ak-glow'), 600);
        }

        // 语音播报：默认读当前风格横幅上的文字（C++ 按 event + skin 选词）；场景规则可改成别的台词或自定义文字
        if (eventOn && ownsVoice) speakFor(level, scene, delay, ruled);

        // 追加播放：主特效的持续时间走完后再播（默认 = 复仇 / 雪耻 1.5 秒）
        const extra = Array.isArray(follows) ? follows
            : (revengeAfter && level !== 'revenge' && L.fxEvtRevenge !== 0 ? [{ level: 'revenge', scene: null, ms: REVENGE_FOLLOW_MS }] : []);
        if (!extra.length) return;
        let after = delay + (hasBig ? Math.max(1000, Math.min(10000, Number(u) || 3500)) : 0);
        extra.forEach(f => {
            const ms = Math.max(500, Math.min(10000, Number(f.ms) || REVENGE_FOLLOW_MS));
            const fLevel = f.level;
            if (fLevel !== 1 && fLevel !== 0 && ownsBig) group(after, i, ms, () => playBig(team, killerRow, fLevel, name, victim, f.scene));
            if (ownsVoice) speakFor(fLevel, f.scene, after, true);
            after += ms;
        });
    }

    function speakFor(level, scene, delay, ruled) {
        const L = killDisplaySettings?.layout || {};
        const mode = scene ? (scene.voice || 'default') : 'default';
        if (mode === 'none') return;
        if (mode === 'text') {
            const text = String(scene.voiceText || '').trim();
            if (text) scheduleKillSay(text, delay);
            return;
        }
        if (mode === 'file') {
            if (scene.voiceFile) scheduleKillSay(scene.voiceFile, delay, true);
            return;
        }
        if (mode === 'lib') {
            // 服务器音频库：C++ 按需下载（不计费、本地缓存）后排队播放
            // voicePrefix：先用同一音色念这段（例如选手的搞名），紧接着播服务器音频；C++ 保证两段按顺序排队
            if (scene.voiceLib || scene.voiceLibTitle) scheduleKillSay(scene.voiceLib || '', delay, 'lib:' + String(scene.voiceLibTitle || ''), String(scene.voicePrefix || ''));
            return;
        }
        if (mode === 'tts') {
            // 豆包音色合成：C++ 先查本地缓存，没有才向服务器生成（扣主播额度）；失败时自动用 Windows 系统语音念
            const text = String(scene.voiceText || '').trim();
            if (text) scheduleKillSay(text, delay, 'tts:' + String(scene.voiceTts || ''));
            return;
        }
        const ev = mode.startsWith('event:') ? mode.slice(6) : VOICE_EVENT[level];
        if (ev && Object.values(VOICE_EVENT).includes(ev)) scheduleKillVoice(ev, voiceSkinFor(L), L.fxVoice, delay, ruled || mode !== 'default');
    }

    const VOICE_EVENT = { 2: 'double', 3: 'triple', first: 'first', shutdown: 'shutdown', revenge: 'revenge', ak: 'ak', victory: 'victory' };
    // 台词套：0 = 跟随当前展示风格；1..9 = 固定使用第 (n-1) 套风格的台词（C++ 支持以序号传 skin）
    function voiceSkinFor(L) {
        const style = Number(L?.fxVoiceStyle) || 0;
        return style >= 1 && style <= 9 ? String(style - 1) : skin;
    }
    const voiceTimers = new Set();
    let voiceSettingsSignature = '';
    function cancelVoiceRequests() {
        voiceTimers.forEach(id => clearTimeout(id));
        voiceTimers.clear();
    }
    function syncVoiceSettings() {
        const L = killDisplaySettings?.layout || {};
        const signature = JSON.stringify(['fxEnabled', 'fxVoiceOn', 'fxVoice', 'fxVoiceStyle', 'skin', 'fxFullscreen',
            ...Object.values(EVENT_KEY)].map(key => L[key]));
        if (signature !== voiceSettingsSignature) cancelVoiceRequests();
        voiceSettingsSignature = signature;
    }
    function scheduleKillVoice(eventName, skinId, voice, delay, ruled = false) {
        syncVoiceSettings();
        const id = setTimeout(() => {
            voiceTimers.delete(id);
            requestKillVoice(eventName, skinId, voice, ruled);
        }, delay);
        voiceTimers.add(id);
    }
    // 场景规则的自定义语音文字：C++ 用 Windows 系统语音合成（按文字缓存），与击杀语音共用开关 / 取消逻辑
    // isFile = true：text 是「自定义语音」里导入的本地音频文件名；isFile = 'tts:<音色>'：用豆包音色合成 text
    function scheduleKillSay(text, delay, isFile = false, prefix = '') {
        syncVoiceSettings();
        const voice = killDisplaySettings?.layout?.fxVoice;
        const id = setTimeout(() => {
            voiceTimers.delete(id);
            requestKillSay(text, voice, isFile, prefix);
        }, delay);
        voiceTimers.add(id);
    }
    function requestKillSay(text, voice, isFile = false, prefix = '') {
        const L = killDisplaySettings?.layout || {};
        if (!enabled || document.hidden || !window.chrome?.webview || L.fxVoiceOn !== 1 || L.fxEnabled === 0
            || L.fxVoice !== voice || (KILL_FX_FULLSCREEN ? L.fxFullscreen !== 1 : L.fxFullscreen === 1)) return;
        if (typeof isFile === 'string' && isFile.startsWith('lib:')) {
            const pre = String(prefix || '').trim().slice(0, 30);
            fetch(`${KILL_LIB_URL}?sha=${encodeURIComponent(String(text))}&title=${encodeURIComponent(isFile.slice(4))}`
                + (pre ? `&prefix=${encodeURIComponent(pre)}&index=${Number(voice) || 0}` : ''), { method: 'POST', cache: 'no-store' }).catch(() => {});
            return;
        }
        if (typeof isFile === 'string' && isFile.startsWith('tts:')) {
            const ttsVoice = isFile.slice(4);
            fetch(`${KILL_TTS_URL}?voice=${encodeURIComponent(ttsVoice)}&index=${Number(voice) || 0}`, { method: 'POST', cache: 'no-store',
                headers: { 'Content-Type': 'text/plain; charset=utf-8' }, body: String(text).slice(0, 120) }).catch(() => {});
            return;
        }
        if (isFile) {
            fetch(`${KILL_VOICE_FILE_URL}?id=${encodeURIComponent(String(text))}`, { method: 'POST', cache: 'no-store' }).catch(() => {});
            return;
        }
        fetch(KILL_SAY_URL, { method: 'POST', cache: 'no-store', headers: { 'Content-Type': 'text/plain; charset=utf-8' },
            body: String(text).slice(0, 120) }).catch(() => {});
    }
    function requestKillVoice(eventName, skinId, voice, ruled = false) {
        // 延迟期间取消勾选、关闭特效或切换播放窗口后，不得再发出旧的播放请求。
        const L = killDisplaySettings?.layout || {};
        const level = Object.keys(VOICE_EVENT).find(key => VOICE_EVENT[key] === eventName);
        if (!enabled || document.hidden || !window.chrome?.webview || L.fxVoiceOn !== 1 || L.fxEnabled === 0
            || !level || (!ruled && L[EVENT_KEY[level]] === 0) || L.fxVoice !== voice
            || (KILL_FX_FULLSCREEN ? L.fxFullscreen !== 1 : L.fxFullscreen === 1)) return;
        const url = `${KILL_VOICE_URL}?event=${encodeURIComponent(eventName)}&skin=${encodeURIComponent(skinId || '')}&voice=${Number(voice) || 0}`;
        fetch(url, { method: 'POST', cache: 'no-store' }).catch(() => {});
    }

    let bigOverride = null; // 场景规则自定义的胜利标题 / 副标题（playBig 调用 victory 前设置）
    function victory(team) {
        const ov = bigOverride; bigOverride = null;
        const copy0 = {
            dnf: ['胜利', '荣耀归于胜者'], classic: ['VICTORY', 'THE BATTLE IS WON'],
            neon: ['VICTORY', 'MISSION COMPLETE'], ink: ['大捷', '凯歌还 · 胜局定'],
            glass: ['胜利', '这一刻，属于你'], pixel: ['VICTORY!', 'STAGE CLEAR'],
            inferno: ['凯旋', '烈焰铸就胜名'], broadcast: ['比赛胜利', 'WINNER · FIRST TO SEVEN'],
            frost: ['凯旋', '冰封全场 · 荣耀归来']
        }[skin] || ['胜利', 'VICTORY'];
        const copy = [ov?.title || copy0[0], ov?.sub || copy0[1]];
        const wrap = div('fx-victory fx-victory-' + skin);
        const crown = '<svg viewBox="0 0 96 64" aria-hidden="true"><path d="M12 16l18 17L48 8l18 25 18-17-9 36H21z" fill="currentColor"/><path d="M22 59h52" fill="none" stroke="currentColor" stroke-width="4"/></svg>';
        const laurel = '<svg viewBox="0 0 80 180" aria-hidden="true"><path d="M64 166C9 137 8 62 56 12" fill="none" stroke="currentColor" stroke-width="2"/>'
            + [0,1,2,3,4,5].map(i => '<ellipse cx="'+(28 + Math.abs(2-i)*4)+'" cy="'+(36+i*22)+'" rx="7" ry="16" transform="rotate(-38 '+(28+Math.abs(2-i)*4)+' '+(36+i*22)+')" fill="currentColor"/>').join('') + '</svg>';
        wrap.innerHTML = '<div class="fx-victory-dim"></div><div class="fx-victory-rays"></div><div class="fx-victory-rule top"></div><div class="fx-victory-rule bottom"></div>'
            + '<div class="fx-victory-laurel left">'+laurel+'</div><div class="fx-victory-laurel right">'+laurel+'</div>'
            + '<div class="fx-victory-core"><div class="fx-victory-crown">'+crown+'</div><div class="fx-victory-kicker">FIRST TO SEVEN</div>'
            + '<div class="fx-victory-title">'+esc(copy[0])+'</div><div class="fx-victory-sub">'+esc(copy[1])+'</div><div class="fx-victory-seal">7</div></div>';
        wrap.style.setProperty('--victory-life', (cur.dur || 4300) + 'ms');
        add(wrap, cur.dur || 4300);
        const { cx, cy } = center();
        burst(cx, cy, 100, { speed: 5, life: 90, size: 3, hue: skin === 'ink' ? 40 : undefined });
        later(() => burst(cx, cy * .6, 50, { speed: 3, life: 70, size: 2.4 }), 550);
    }

    function playBig(team, killerRow, level, name, victim, scene = null) {
        const ov = scene && (scene.title || scene.sub) ? { title: scene.title, sub: scene.sub } : null;
        bigOverride = ov;
        if (level === 'victory') { victory(team); return; }
        bigOverride = null;
        const { cx, cy } = center();
        if (level === 'ak') { later(() => ak(team, killerRow, name, ov), 250); return; }
        if (level === 'custom') { customBanner(team, name, scene || {}); return; }
        if (!LEVEL_TEXT[level]) return;

        let [title, sub] = LEVEL_TEXT[level];
        if (skin === 'ink') title = INK_TEXT[level];
        if (skin === 'pixel') { title = PIXEL_TEXT[level]; }
        if (ov?.title) title = ov.title;
        if (ov?.sub) sub = ov.sub;
        const extra = level === 'shutdown' && victim ? `终结了 ${esc(victim)} 的连杀`
            : level === 'revenge' && victim ? `向 ${esc(victim)} 复仇` : '';
        if (level === 2) {
            banner(team, title, sub, name, 2200);
            later(() => burst(cx, cy, 40, { speed: 4, life: 50 }), 200);
        } else if (level === 3) {
            add(div('fx-edge'), cur.dur || 2500);
            add(div('fx-ring'), 900);
            later(() => add(div('fx-ring'), 900), 160);
            banner(team, title, sub, name, 2600);
            shake();
            later(() => burst(cx, cy, 80, { speed: 5.5, life: 60, size: 2.6, shard: true }), 180);
        } else {
            // 一血 / 终结 / 复仇：迷你横幅
            banner(team, title, sub, name, 1900, extra, true);
            if (level === 'first') later(() => burst(cx, cy, 36, { speed: 3.5, life: 45, hue: theme.p === 'ember' ? 0 : undefined }), 150);
            if (level === 'shutdown') { add(div('fx-ring'), 900); later(() => burst(cx, cy, 44, { speed: 4.5, life: 50, shard: true }), 150); }
            if (level === 'revenge') later(() => burst(cx, cy, 30, { speed: 3, life: 55, hue: TEAM_HUE[team] }), 150);
        }
    }

    // 场景规则「自定义横幅」：小 = 一血同款迷你横幅；大 = 双杀同款；超大 = 三杀同款（边框光 + 冲击环 + 震屏）
    function customBanner(team, name, scene) {
        const { cx, cy } = center();
        const title = scene.title || '精彩';
        const sub = scene.sub || '';
        if (scene.style === 'mini') {
            banner(team, title, sub, name, 1900, '', true);
            later(() => burst(cx, cy, 36, { speed: 3.5, life: 45 }), 150);
        } else if (scene.style === 'huge') {
            add(div('fx-edge'), cur.dur || 2500);
            add(div('fx-ring'), 900);
            later(() => add(div('fx-ring'), 900), 160);
            banner(team, title, sub, name, 2600);
            shake();
            later(() => burst(cx, cy, 80, { speed: 5.5, life: 60, size: 2.6, shard: true }), 180);
        } else {
            banner(team, title, sub, name, 2200);
            later(() => burst(cx, cy, 40, { speed: 4, life: 50 }), 200);
        }
    }
    return { play, burst, setEnabled, syncVoiceSettings, cancelVoiceRequests, THEMES };
})();

/* ================= 击杀事件识别 =================
 * 依据 /api/state 的 kills / deaths / akCount / currentStreak 变化推断：
 *   单杀 / 双杀 / 三杀 / AK / 一血 / 终结 / 复仇
 * 连杀直接使用主程序的 currentStreak（任何人击杀会清零其他人，每局结束全部清零）。
 * 只做推断，不改动任何战绩数据。
 * ================================================= */
// Pure score-edge tracker: physical left follows the same isFlipped mapping as renderKillDisplay.
function createLeftVictoryTracker() {
    let previous = null, announced = false;
    return {
        resync() { previous = null; },
        ingest(data, canPlay = true) {
            if (data?.redScore == null || data?.blueScore == null) return null;
            const scores = { red: Number(data.redScore), blue: Number(data.blueScore) };
            if (![scores.red, scores.blue].every(n => Number.isInteger(n) && n >= 0)) return null;
            if (scores.red === 0 && scores.blue === 0) announced = false;
            const team = data.isFlipped === true ? 'blue' : 'red';
            if (!previous) {
                previous = scores;
                if (scores[team] >= 7) announced = true;
                return null; // Never replay a historical victory on refresh/reopen.
            }
            const crossed = previous[team] < 7 && scores[team] >= 7;
            previous = scores;
            if (!crossed || announced) return null;
            announced = true; // Consume hidden/disabled events too, without deferred playback.
            return canPlay ? { team, score: scores[team], level: 'victory', killerRow: -1, victimRow: -1 } : null;
        }
    };
}

const KillFxTracker = (() => {
    const victoryTracker = createLeftVictoryTracker();
    const QUEUE_STEP = 240;        // 同一次刷新里多次击杀的播放间隔
    const MAX_QUEUE = 12;          // 队列上限，防止异常数据堆积

    const keyOf = p => `${Number(p?.team) === 1 ? 1 : 0}|${p?.name || ''}`;
    // 当前局序号 = 红蓝大比分之和（第 1 局为 0）
    const roundOf = d => (Number(d?.redScore) || 0) + (Number(d?.blueScore) || 0);

    let prev = null;            // Map<key, {kills, deaths, ak, streak, team, name}>
    // 场景规则用的比赛记录：本场击杀顺序 / 本局阵亡的选手（比分变动 = 新的一局，全部复活）/ 连续阵亡没拿人头
    let killLog = [];
    let deadKeys = new Set();
    let deadRound = null;
    let drought = new Map();
    const normPlayer = s => String(s || '').trim().toLowerCase();
    // 搞名：主窗口保存规则时附带的「选手名 → 以搞结尾的别名」表；名字本身以搞结尾就用它；都没有 = 原名
    function gaoOf(name) {
        const n = String(name || '').trim();
        const map = (typeof SceneRuntime !== 'undefined' && SceneRuntime && SceneRuntime.gao) ? SceneRuntime.gao() : null;
        return (map && map[normPlayer(n)]) || n;
    }
    function playerStateCtx(snap, allAlive = false) {
        const alive = {}, teams = {};
        (snap || new Map()).forEach((p, key) => {
            const n = normPlayer(p.name);
            if (!n) return;
            alive[n] = allAlive || !deadKeys.has(key);
            teams[n] = p.team;
        });
        return { alive, teams };
    }
    function markDeath(d) {
        if (!d || d.counted) return 0;
        d.counted = true;
        deadKeys.add(d.key);
        const n = (drought.get(d.key) || 0) + 1;
        drought.set(d.key, n);
        return n;
    }
    let prevRound = 0;          // prev 对应的局序号
    let killsByRound = new Map(); // 局序号 -> Set<"凶手key>死者key">，用于复仇判定
    let firstBlood = false;
    let queue = [];
    let timer = null;
    let enabled = true;

    /* ---------- 场景规则 ----------
     * 规则由 SceneRuntime（本文件末尾）从主程序加载；scene-rules.js 未加载时退回内置优先级逻辑。 */
    const ruleMemo = {};             // 冷却记录
    let prevScores = { red: 0, blue: 0 };
    let lastRoundNo;                 // 主程序识别到「开始!!!」的回合序号
    let lastTestId;                  // 主窗口「测试」按钮
    const hpFired = new Set();       // 血量规则：每条规则每方每回合只触发一次
    const sceneRules = () => (typeof SceneRules !== 'undefined' && typeof SceneRuntime !== 'undefined' && SceneRuntime)
        ? SceneRuntime.rules() : null;
    const EFFECT_LEVEL = { none: 1, double: 2, triple: 3 };
    const levelOf = effect => EFFECT_LEVEL[effect] || effect || 1;
    const teamColor = t => (t === 1 ? 'blue' : 'red');
    const teamLabel = t => (t === 1 ? '蓝队' : '红队');
    const hpNum = v => { const n = Number(v); return Number.isFinite(n) && n >= 0 ? Math.round(n) : null; };
    // 画面左侧是哪一队（0 = 红，1 = 蓝）：与 renderKillDisplay 相同，只取决于 isFlipped
    const leftTeamOf = data => (data?.isFlipped === true ? 1 : 0);
    function sceneOf(r, ctx) {
        const a = r.action || {};
        return {
            effect: a.effect, style: a.style, voice: a.voice, voiceFile: a.voiceFile || '', voiceTts: a.voiceTts || '', voiceLib: a.voiceLib || '', voiceLibTitle: a.voiceLibTitle || '', durationMs: Number(a.durationMs) || 0,
            // 只保留汉字 / 字母 / 数字：表情、符号豆包念不了，会退回 Windows 语音
            voicePrefix: SceneRules.fillText(a.voicePrefix, ctx).replace(/[^\p{Script=Han}A-Za-z0-9]/gu, ''),
            title: SceneRules.fillText(a.title, ctx), sub: SceneRules.fillText(a.sub, ctx),
            voiceText: SceneRules.fillText(a.voiceText, ctx)
        };
    }
    function ruledEvent(res, ctx) {
        if (!res || !res.main) return null;
        return {
            level: levelOf(res.main.action.effect), scene: sceneOf(res.main, ctx), ruled: true,
            follows: res.follows.map(f => ({ level: levelOf(f.action.effect), scene: sceneOf(f, ctx), ms: f.appendMs }))
        };
    }
    // 血量数据（主程序实时计算，0-100；-1 = 未识别）。hp.left / hp.right 是画面左右，队伍在左还是右取决于 isFlipped。
    // 游戏画面上哪一队在左边：和展示页面的左右（isFlipped）无关——主播常把自己队伍翻到展示页左侧，但游戏里不一定在左边。
    // 每次击杀时从「哪边血条归零 / 哪边赢了这一局」推断，整场比赛内沿用；推断不出来时才退回 isFlipped。
    let physLeftTeam = null;
    function learnPhysSide(data, killerTeam) {
        const hp = data?.hp || {};
        const r = hp.round || {};
        const ended = r.no > 0 && !r.active && Number(r.endAgoMs) >= 0 && Number(r.endAgoMs) < 8000;
        let s = null;   // 击杀者在游戏画面的哪一边：0 = 左，1 = 右
        if (ended && (r.winner === 0 || r.winner === 1)) s = r.winner;
        else {
            const L = hpNum(hp.left), R = hpNum(hp.right);
            if (L === 0 && R > 0) s = 1;
            else if (R === 0 && L > 0) s = 0;
        }
        if (s !== null) physLeftTeam = s === 0 ? killerTeam : 1 - killerTeam;
    }
    function hpOf(data, team) {
        const hp = data?.hp || {};
        const r = hp.round || {};
        const left = physLeftTeam !== null ? physLeftTeam : (data?.isFlipped === true ? 1 : 0);
        const s = team === left ? 0 : 1;
        const o = 1 - s;
        const pick = (arr, i) => (Array.isArray(arr) ? hpNum(arr[i]) : null);
        const cur = [hpNum(hp.left), hpNum(hp.right)];
        // 击杀往往和回合结束同时到达：刚结束 8 秒内用「结束血量」（比实时读数更准：死者已归 0）
        const ended = r.no > 0 && !r.active && Number(r.endAgoMs) >= 0 && Number(r.endAgoMs) < 8000;
        return {
            now: ended && pick(r.end, s) != null ? pick(r.end, s) : cur[s],
            enemyNow: ended && pick(r.end, o) != null ? pick(r.end, o) : cur[o],
            start: pick(r.start, s), enemyStart: pick(r.start, o), min: pick(r.min, s),
            roundSec: Number(r.durationMs) >= 0 && r.no > 0 ? Math.round(Number(r.durationMs) / 1000) : null
        };
    }
    function scoreCtx(data, team) {
        const red = Number(data?.redScore) || 0, blue = Number(data?.blueScore) || 0;
        const my = team === 1 ? blue : red, en = team === 1 ? red : blue;
        return { myScore: my, enemyScore: en, scoreDiff: my - en, redScore: red, blueScore: blue };
    }
    function ingestScene(data, rules) {
        const r = data?.hp?.round;
        // 主窗口「测试」：首次拿到数据时只记下编号，不回放
        const test = data?.sceneTest;
        const testId = Number(test?.id) || 0;
        if (lastTestId === undefined) lastTestId = testId;
        else if (testId !== lastTestId) { lastTestId = testId; playTest(test.rule, data); }
        if (!rules) return;
        // 每局开始（识别到「开始!!!」）
        const no = Number(r?.no) || 0;
        if (lastRoundNo === undefined) lastRoundNo = no;
        else if (no !== lastRoundNo) {
            lastRoundNo = no;
            if (no > 0 && r.active) {
                const red = Number(data?.redScore) || 0, blue = Number(data?.blueScore) || 0;
                const ctx = { eventKey: 'round#' + no + '#' + red + ':' + blue, redScore: red, blueScore: blue,
                    isMatchPoint: red === 6 || blue === 6, roundNo: red + blue + 1, hour: new Date().getHours(),
                    history: killLog, historyHasCurrent: false, historyRound: roundOf(data), ...playerStateCtx(prev, true) };
                const evt = ruledEvent(SceneRules.evaluate(rules, 'round', ctx, ruleMemo), ctx);
                if (evt) enqueue({ ...evt, team: 'red', killerRow: -1, victimRow: -1, name: '', victim: '', kind: 'scene' });
            }
        }
        // 血量：条件从不满足变成满足时触发，每条规则每方每回合一次
        if (!r || !r.active || no <= 0) return;
        const hpRules = rules.filter(x => x.enabled && x.trigger === 'hp');
        if (!hpRules.length) return;
        [0, 1].forEach(team => {
            const h = hpOf(data, team);
            const ctx = { eventKey: 'hp#' + no + '#' + team, team: teamColor(team), teamLabel: teamLabel(team), teamLeft: team === leftTeamOf(data),
                selfHp: h.now, enemyHp: h.enemyNow, selfStartHp: h.start, enemyStartHp: h.enemyStart,
                roundSec: h.roundSec, ...scoreCtx(data, team), roundNo: no, hour: new Date().getHours(),
                isMatchPoint: (Number(data?.redScore) || 0) === 6 || (Number(data?.blueScore) || 0) === 6,
                history: killLog, historyHasCurrent: false, historyRound: roundOf(data), ...playerStateCtx(prev) };
            hpRules.forEach(rule => {
                const key = rule.id + '|' + team + '|' + no;
                if (hpFired.has(key)) return;
                const res = SceneRules.evaluate([rule], 'hp', ctx, ruleMemo);
                if (!res.main) return;
                hpFired.add(key);
                if (hpFired.size > 400) hpFired.clear();
                const evt = ruledEvent({ main: res.main, follows: [] }, ctx);
                enqueue({ ...evt, team: teamColor(team), killerRow: -1, victimRow: -1, name: teamLabel(team), victim: '', kind: 'scene' });
            });
        });
    }
    // 「测试」：用一组示例数据播放这条规则（不看条件）
    function playTest(rule, data) {
        if (typeof SceneRules === 'undefined' || !rule) return;
        const r = SceneRules.normalizeRule(rule);
        if (!r) return;
        const ctx = { eventKey: 'test', killer: '90老王', victim: '对手', teamLabel: '红队', killerLeft: true, victimLeft: false, streak: 2, killerKills: 3, killerDeaths: 1,
            killerHp: 23, killerStartHp: 100, victimStartHp: 100, selfHp: 15, enemyHp: 80, selfStartHp: 100, enemyStartHp: 100,
            roundSec: 42, myScore: 5, enemyScore: 4, redScore: 6, blueScore: 5, roundNo: 12 };
        const evt = ruledEvent({ main: r, follows: [] }, ctx);
        const isKill = r.trigger === 'kill';
        enqueue({ ...evt, team: 'red', killerRow: isKill ? 0 : -1, victimRow: -1,
            name: '90老王', victim: '对手', kind: isKill ? 'kill' : 'scene' });
    }

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

    // 主窗口手动改比分后的调整（C++ 状态里的 sceneReset.id 变化时执行一次）
    let lastSceneResetId;
    function checkSceneReset(data) {
        const sr = data?.sceneReset;
        const id = Number(sr?.id) || 0;
        if (lastSceneResetId === undefined) { lastSceneResetId = id; return; }
        if (id === lastSceneResetId) return;
        lastSceneResetId = id;
        deadKeys.clear();                       // 比分变了 = 新的一局，全部复活
        deadRound = roundOf(data);
        if (sr?.drought) drought = new Map();   // 连续阵亡没人头清零
        if (sr?.history) {                      // 当作新的一场：击杀记录 / 一血 / 复仇
            killLog = [];
            killsByRound = new Map();
            firstBlood = false;
        }
    }

    function resetMatch() {
        killsByRound = new Map();
        killLog = [];
        physLeftTeam = null;
        deadKeys = new Set();
        drought = new Map();
        queue = [];
        firstBlood = false;
        if (timer) { clearTimeout(timer); timer = null; }
    }

    /* ---------- 撤销 / 恢复 / 重做 / 回溯：静默同步情景数据 ----------
     * 主程序在这些操作前下发 statsRewrite；5 秒内的下一次战绩变化不当作击杀播放，
     * 而是按增减修正比赛记录：击杀变少 = 删除该选手最近的击杀记录（含复仇记录），
     * 击杀变多 = 静默补记；死亡变少 = 复活并回退连续没人头，死亡变多 = 记阵亡。 */
    let statsRewriteUntil = 0;
    let statsRewriteHint = null;     // 复盘单条撤销 / 恢复时主程序附带的 { killer, victim }
    const statsRewriteActive = () => statsRewriteUntil > Date.now();
    // 连续没人头 / 复仇 / 本局阵亡 / 一血全部由击杀记录重新推导：一次跳过多步的回溯也不会累积误差
    function rebuildFromLog(cur, round) {
        const keyOfName = name => { for (const [key, c] of cur) if (c.name === name) return key; return ''; };
        drought = new Map();
        killsByRound = new Map();
        deadKeys = new Set();
        deadRound = round;
        killLog.forEach(k => {
            const kk = keyOfName(k.killer), vk = keyOfName(k.victim);
            if (kk) drought.set(kk, 0);
            if (!vk) return;
            drought.set(vk, (drought.get(vk) || 0) + 1);
            if (kk) {
                let set = killsByRound.get(k.round);
                if (!set) { set = new Set(); killsByRound.set(k.round, set); }
                set.add(`${kk}>${vk}`);
            }
            if (k.round === round) deadKeys.add(vk);
        });
        killsByRound.forEach((_, r) => { if (r < round - 1) killsByRound.delete(r); });
        firstBlood = killLog.length > 0;
    }
    function rewriteStats(prevSnap, cur, data, killRound) {
        const hint = statsRewriteHint;
        const pool = { 0: [], 1: [] };
        const adds = [];
        let changed = false;
        cur.forEach((c, key) => {
            const p = prevSnap.get(key);
            if (!p) return;
            const dk = c.kills - p.kills;
            const dd = c.deaths - p.deaths;
            if (dk || dd) changed = true;
            for (let i = 0; i < -dk; i++) {
                // 单条撤销：优先删「同一凶手 + 同一死者」最近的那条；否则删该凶手最近的一条（回溯 = 撤掉最后发生的击杀）
                let at = -1;
                if (hint && hint.killer === c.name && hint.victim) {
                    for (let j = killLog.length - 1; j >= 0; j--) {
                        if (killLog[j].killer === c.name && killLog[j].victim === hint.victim) { at = j; break; }
                    }
                }
                if (at < 0) for (let j = killLog.length - 1; j >= 0; j--) if (killLog[j].killer === c.name) { at = j; break; }
                if (at >= 0) killLog.splice(at, 1);
            }
            for (let i = 0; i < dd; i++) pool[c.team].push({ key, c });
            if (dk > 0) adds.push({ key, c, dk });
        });
        if (!changed) return false;
        // 单条恢复：优先把死者对应回原来那个人
        if (hint && hint.victim) [0, 1].forEach(t => {
            const i = pool[t].findIndex(v => v.c.name === hint.victim);
            if (i > 0) pool[t].unshift(pool[t].splice(i, 1)[0]);
        });
        adds.forEach(k => {
            const enemy = k.c.team === 1 ? 0 : 1;
            for (let i = 0; i < k.dk; i++) {
                const v = pool[enemy].shift();
                killLog.push({ killer: k.c.name, victim: v ? v.c.name : '', killerTeam: teamColor(k.c.team),
                    killerLeft: k.c.team === leftTeamOf(data), streak: Math.max(1, k.c.streak || 1), round: killRound });
                if (killLog.length > 1000) killLog.shift();
            }
        });
        // 快照分几次到达（先到击杀、后到阵亡）：把后到的阵亡补回刚才没对应上死者的击杀记录
        [0, 1].forEach(t => {
            const killerColor = teamColor(t === 1 ? 0 : 1);
            pool[t] = pool[t].filter(v => {
                for (let j = killLog.length - 1; j >= 0 && j >= killLog.length - 20; j--) {
                    if (killLog[j].victim === '' && killLog[j].killerTeam === killerColor) { killLog[j].victim = v.c.name; return false; }
                }
                return true;
            });
        });
        rebuildFromLog(cur, roundOf(data));
        // 没对应到凶手的阵亡（同一帧多人阵亡等）击杀记录里没有，补记本局阵亡 / 连续没人头
        [0, 1].forEach(t => pool[t].forEach(v => { deadKeys.add(v.key); drought.set(v.key, (drought.get(v.key) || 0) + 1); }));
        return true;
    }

    /* ---------- 情景状态：回报给主程序 / 执行主窗口的手动修改 ---------- */
    const SCENE_SRC = (KILL_FX_FULLSCREEN ? 'fx-' : 'board-') + Math.random().toString(36).slice(2, 8);
    let lastSceneEditId;
    let sceneReportSig = '';
    let sceneReportAt = 0;
    const clearCooldowns = id => {
        if (!ruleMemo.lastFire) return;
        if (id) delete ruleMemo.lastFire[id]; else ruleMemo.lastFire = {};
    };
    function applySceneOp(o) {
        if (!o || typeof o !== 'object') return;
        const key = String(o.key || '');
        switch (o.op) {
            case 'alive':
                if (deadRound !== prevRound) { deadKeys.clear(); deadRound = prevRound; }
                if (o.value) deadKeys.delete(key); else deadKeys.add(key);
                break;
            case 'drought': drought.set(key, Math.max(0, Math.min(99, Math.round(Number(o.value) || 0)))); break;
            case 'firstBlood': firstBlood = !!o.value; break;
            case 'physLeft': physLeftTeam = o.value === 0 || o.value === 1 ? o.value : null; break;
            case 'reviveAll': deadKeys.clear(); deadRound = prevRound; break;
            case 'clearDrought': drought = new Map(); break;
            case 'clearHistory': killLog = []; killsByRound = new Map(); firstBlood = false; break;
            case 'removeKill': {
                const i = Number(o.index);
                if (Number.isInteger(i) && i >= 0 && i < killLog.length) killLog.splice(i, 1);
                break;
            }
            case 'revenge': {   // 删除一条复仇记录（上一局 凶手>死者）
                const set = killsByRound.get(Number(o.round));
                if (set) set.delete(String(o.pair || ''));
                break;
            }
            case 'clearCooldown': clearCooldowns(o.id ? String(o.id) : ''); if (!o.id) hpFired.clear(); break;
            case 'full':
                resetMatch();
                clearCooldowns('');
                hpFired.clear();
                deadRound = prevRound;
                break;
            case 'statsRewrite':
                statsRewriteUntil = Date.now() + 5000;
                statsRewriteHint = o.killer ? { killer: String(o.killer), victim: String(o.victim || '') } : null;
                break;
            case 'statsRewriteCancel': statsRewriteUntil = 0; statsRewriteHint = null; break;
            default: break;
        }
    }
    function checkSceneEdit(data) {
        const se = data?.sceneEdit;
        const id = Number(se?.id) || 0;
        if (lastSceneEditId === undefined) { lastSceneEditId = id; return; }   // 打开页面前的修改不补执行
        if (id <= lastSceneEditId) { if (id < lastSceneEditId) lastSceneEditId = id; return; }  // 主程序重启后编号归零
        (Array.isArray(se?.list) ? se.list : []).forEach(e => {
            if (!e || Number(e.id) <= lastSceneEditId) return;
            (Array.isArray(e.ops) ? e.ops : []).forEach(o => { try { applySceneOp(o); } catch (err) { /* ignore */ } });
        });
        lastSceneEditId = id;
        sceneReportAt = 0;   // 立刻回报修改后的状态
    }
    function exportSceneState(data) {
        const round = roundOf(data);
        const deadValid = deadRound === round;
        const players = [];
        (Array.isArray(data?.players) ? data.players : []).forEach(p => {
            if (!p || !p.name) return;
            const key = keyOf(p);
            players.push({ key, name: p.name, team: Number(p.team) === 1 ? 1 : 0,
                alive: !(deadValid && deadKeys.has(key)), drought: drought.get(key) || 0,
                streak: Number(p.currentStreak) || 0, gao: gaoOf(p.name) });
        });
        const rules = sceneRules() || [];
        const now = Date.now();
        const cooldowns = [];
        Object.entries(ruleMemo.lastFire || {}).forEach(([id, at]) => {
            const r = rules.find(x => x.id === id);
            if (!r || !(r.cooldownSec > 0)) return;
            const left = Math.ceil((r.cooldownSec * 1000 - (now - at)) / 1000);
            if (left > 0) cooldowns.push({ id, name: r.name, left, total: r.cooldownSec });
        });
        const revenge = [];
        (killsByRound.get(round - 1) || new Set()).forEach(pair => revenge.push({ round: round - 1, pair }));
        const start = Math.max(0, killLog.length - 30);
        return {
            mode: KILL_FX_FULLSCREEN ? 'fx' : 'board', visible: !document.hidden, enabled, rulesOn: !!sceneRules(),
            round, firstBlood, physLeftTeam, flipped: data?.isFlipped === true, players,
            killTotal: killLog.length,
            killLog: killLog.slice(start).map((k, i) => ({ index: start + i, killer: k.killer, victim: k.victim,
                killerTeam: k.killerTeam, streak: k.streak, round: k.round })),
            revenge, cooldowns, hpFired: hpFired.size
        };
    }
    function reportSceneState(data) {
        lastSceneData = data;
        const now = Date.now();
        if (now - sceneReportAt < 1000) return;
        const body = JSON.stringify(exportSceneState(data));
        if (body === sceneReportSig && now - sceneReportAt < 5000) return;
        sceneReportSig = body;
        sceneReportAt = now;
        // text/plain：不触发跨域预检
        fetch(SCENE_STATE_URL + '?src=' + encodeURIComponent(SCENE_SRC), { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body })
            .catch(() => { /* 主程序未运行 */ });
    }
    // 状态只在数据变化时 ingest；另起心跳，保证击杀处理后的最新记录与「窗口仍在运行」都能回报
    let lastSceneData = null;
    setInterval(() => { try { if (lastSceneData) reportSceneState(lastSceneData); } catch (err) { /* ignore */ } }, 1000);

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

    // 页面从隐藏恢复显示时丢弃旧快照，避免把隐藏期间的击杀一次性补播
    function resync() { prev = null; victoryTracker.resync(); }

    function ingest(data) {
        try { checkSceneEdit(data); } catch (err) { /* ignore */ }
        const silent = statsRewriteActive();   // 撤销 / 重做带来的比分变化不播胜利特效
        const victory = victoryTracker.ingest(data, !silent && enabled && !document.hidden && killDisplaySettings?.layout?.fxEvtVictory !== 0);
        const rules = sceneRules();
        const scoresBefore = prevScores;
        prevScores = { red: Number(data?.redScore) || 0, blue: Number(data?.blueScore) || 0 };
        try { checkSceneReset(data); } catch (err) { /* ignore */ }
        try { reportSceneState(data); } catch (err) { /* ignore */ }
        if (!enabled || document.hidden) return;
        try { ingestScene(data, rules); } catch (err) { /* 场景规则异常不影响击杀特效 */ }
        if (victory && !silent) {
            queue = [];
            if (timer) { clearTimeout(timer); timer = null; }
            prev = snapshot(Array.isArray(data?.players) ? data.players : []);
            prevRound = roundOf(data);
            if (rules) {
                // 场景规则：胜利规则决定播什么（「触发事件」里的胜利开关已同步成预设规则的开关）
                const t = victory.team === 'blue' ? 1 : 0;
                const ctx = { eventKey: 'victory#' + prevScores.red + ':' + prevScores.blue, winnerTeam: victory.team,
                    winnerLeft: t === leftTeamOf(data), teamLabel: teamLabel(t), ...scoreCtx(data, t), hour: new Date().getHours(),
                    history: killLog, historyHasCurrent: false, historyRound: Math.max(0, roundOf(data) - 1), ...playerStateCtx(prev) };
                const evt = ruledEvent(SceneRules.evaluate(rules, 'victory', ctx, ruleMemo), ctx);
                if (evt) enqueue({ ...victory, ...evt, name: evt.level === 'victory' ? victory.name : teamLabel(t), kind: 'scene' });
                return;
            }
            enqueue(victory); // Victory takes precedence over the final kill/AK of the match.
            return;
        }
        // 本次新增的击杀归属上一次状态所在的局：决胜击杀与大比分 +1 会在同一次刷新里到达。
        const killRound = prevRound;
        prevRound = roundOf(data);
        if (deadRound !== killRound) { deadKeys.clear(); deadRound = killRound; }
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
        // 撤销 / 回溯期间名单变化也照样修正（只处理前后都在场的选手）
        if (total === 0 || (roster !== prevRoster && !silent)) {
            // 只在「战绩刚归零」时重置一次；战绩持续为 0（如刚重置战绩、还没打）时每次刷新都重置
            // 会把队列清空，导致「测试播放」和开局规则的特效入队后立刻被丢掉
            let prevTotal = 0;
            prev.forEach(p => { prevTotal += p.kills + p.deaths; });
            prev = cur;
            if (total === 0 && prevTotal > 0) resetMatch();
            return;
        }

        if (silent) {
            if (rewriteStats(prev, cur, data, killRound)) {
                // 快照可能分几次刷新到达：第一次变化后再静默 1.5 秒，避免后半段被当成击杀播放
                statsRewriteUntil = Math.min(statsRewriteUntil, Date.now() + 1500);
                sceneReportAt = 0;   // 立刻回报修正后的状态
            }
            prev = cur;
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
        // 没对应到击杀者的阵亡（例如同一帧里多人阵亡）也要记成阵亡 / 连续没人头
        const settleDeaths = () => [0, 1].forEach(team => deathsByTeam[team].forEach(markDeath));
        if (!killers.length) { settleDeaths(); return; }

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
                // 比赛记录：先记死者（本局阵亡 + 连续没人头），再清零击杀者的没人头次数，最后记这一次击杀
                const victimDrought = v ? (v.counted ? drought.get(v.key) || 0 : markDeath(v)) : 0;
                const killerDrought = drought.get(k.key) || 0;
                drought.set(k.key, 0);
                killLog.push({ killer: k.c.name, victim: v ? v.c.name : '', killerTeam: teamColor(k.c.team),
                    killerLeft: k.c.team === leftTeamOf(data), streak: isAk ? Math.max(4, n) : n, round: killRound });
                if (killLog.length > 1000) killLog.shift();

                if (rules) {
                    // 场景规则：从上到下第一条满足条件的规则 = 主特效；排在后面、勾了「追加播放」的规则在主特效后追加
                    learnPhysSide(data, k.c.team);
                    const h = hpOf(data, k.c.team);
                    const ctx = {
                        eventKey: `${k.key}>${v ? v.key : ''}#${killRound}#${k.c.kills - (k.dk - 1 - i)}`,
                        killer: k.c.name, victim: v ? v.c.name : '', killerTeam: teamColor(k.c.team), teamLabel: teamLabel(k.c.team),
                        killerLeft: k.c.team === leftTeamOf(data), victimLeft: !!v && enemyTeam === leftTeamOf(data),
                        streak: isAk ? Math.max(4, n) : n, killerKills: k.c.kills - (k.dk - 1 - i), killerDeaths: k.c.deaths, killerAk: k.c.ak,
                        victimKills: v ? v.c.kills : null, victimDeaths: v ? v.c.deaths : null, victimStreak: vCount,
                        isFirst: !firstBlood, isRevenge: revenge, isAk,
                        killerHp: h.now, killerStartHp: h.start, victimStartHp: h.enemyStart, killerMinHp: h.min,
                        hpLost: h.start != null && h.now != null ? Math.max(0, h.start - h.now) : null, roundSec: h.roundSec,
                        ...scoreCtx(data, k.c.team), isMatchPoint: scoresBefore.red === 6 || scoresBefore.blue === 6,
                        roundNo: killRound + 1, hour: new Date().getHours(),
                        victimDrought, killerDrought, killerGao: gaoOf(k.c.name), victimGao: v ? gaoOf(v.c.name) : '',
                        history: killLog, historyHasCurrent: true, historyRound: killRound, ...playerStateCtx(cur)
                    };
                    firstBlood = true;
                    if (v) recordKill(killRound, k.key, v.key);
                    const evt = ruledEvent(SceneRules.evaluate(rules, 'kill', ctx, ruleMemo), ctx)
                        || { level: 1, scene: null, follows: [], ruled: true };
                    enqueue({
                        ...evt,
                        team: teamColor(k.c.team),
                        killerRow: rowIndexOf(teamColor(k.c.team), k.c.name),
                        victimRow: v ? rowIndexOf(teamColor(enemyTeam), v.c.name) : -1,
                        name: k.c.name,
                        victim: v ? v.c.name : ''
                    });
                    if (isAk) break;
                    continue;
                }

                // 内置优先级（scene-rules.js 未加载时）：AK > 三杀 / 双杀 > 一血 > 终结 > 复仇 > 单杀。
                // 同时满足复仇时不丢：主特效播完后再追加 1.5 秒的复仇（KillFx.play 的 revengeAfter）。
                let level;
                if (isAk) level = 'ak';
                else if (n >= 2) level = n;
                else if (!firstBlood) level = 'first';
                else if (vCount >= 2) level = 'shutdown';
                else if (revenge) level = 'revenge';
                else level = n;
                firstBlood = true;

                if (v) recordKill(killRound, k.key, v.key);

                enqueue({
                    team: k.c.team === 1 ? 'blue' : 'red',
                    killerRow: rowIndexOf(k.c.team === 1 ? 'blue' : 'red', k.c.name),
                    victimRow: v ? rowIndexOf(enemyTeam === 1 ? 'blue' : 'red', v.c.name) : -1,
                    level,
                    name: k.c.name,
                    victim: v ? v.c.name : '',
                    revengeAfter: revenge && level !== 'revenge'
                });
                if (isAk) break;
            }
        });
        settleDeaths();
    }

    return { ingest, setEnabled, resetMatch, resync };
})();

/* ---------- 与记分板 / 设置面板联动 ---------- */
(function () {
    const origRender = renderKillDisplay;
    renderKillDisplay = function (data) {
        origRender(data);
        try { if (SceneRuntime) SceneRuntime.sync(data); } catch (err) { /* ignore */ }
        try { KillFxTracker.ingest(data); } catch (err) { /* ignore */ }
    };

    // 场景规则：主程序保存在 %APPDATA%\DNFGameCapture\scene_rules.json；
    // 状态里的 sceneRulesRev 变化时重新拉取（主窗口编辑后约 0.5 秒生效）。没有保存过 = 用预设（开关跟随「触发事件」勾选）。
    if (typeof SceneRules !== 'undefined') {
        SceneRuntime = (() => {
            let saved = null;     // null = 从未保存，使用预设
            let gao = {};         // 选手名（小写）→ 搞名
            let rev = null;
            let loading = false;
            function load(r) {
                if (loading) return;
                loading = true;
                fetch(SCENE_RULES_URL, { cache: 'no-store' })
                    .then(res => res.json())
                    .then(j => {
                        const list = j && j.data && Array.isArray(j.data.rules) ? j.data.rules : null;
                        saved = list ? SceneRules.normalize(list) : null;
                        gao = j && j.data && j.data.gao && typeof j.data.gao === 'object' ? j.data.gao : {};
                        if (SceneRules.setAliases) SceneRules.setAliases(j && j.data ? j.data.aliases : null);
                        rev = r;
                    })
                    .catch(() => {})
                    .finally(() => { loading = false; });
            }
            function sync(data) {
                const r = Number(data?.sceneRulesRev) || 0;
                if (r !== rev) load(r);
            }
            function rules() { return saved || SceneRules.presets(killDisplaySettings?.layout); }
            return { sync, rules, gao: () => gao };
        })();
    }

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
        KillFx.syncVoiceSettings();
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
    document.addEventListener('visibilitychange', () => { if (document.hidden) KillFx.cancelVoiceRequests(); else KillFxTracker.resync(); });

    // 全屏特效大小（开关在主窗口「特效管理」，保存到 ini 后由主程序显示 / 隐藏透明全屏窗口）
    const fsBox = null;
    const fsScale = document.getElementById('kill-fx-scale');
    const fsScaleVal = document.getElementById('kill-fx-scale-val');
    const syncFullscreenControls = () => {
        const layout = killDisplaySettings?.layout || {};
        const on = layout.fxFullscreen === 1;
        const scale = Number(layout.fxFullscreenScale) || 100;
        if (fsBox) fsBox.checked = on;
        if (fsScale) {
            if (document.activeElement !== fsScale) fsScale.value = String(scale);
            fsScale.disabled = !on;
        }
        if (fsScaleVal) fsScaleVal.textContent = `${scale}%`;
    };
    const origSyncSkinPanel2 = syncKillSkinPanel;
    syncKillSkinPanel = function () {
        origSyncSkinPanel2();
        syncFullscreenControls();
    };
    fsBox?.addEventListener('change', () => {
        killDisplaySettings.layout.fxFullscreen = fsBox.checked ? 1 : 0;
        syncFullscreenControls();
        queueKillDisplaySettingsSave();
        showStatus(fsBox.checked ? '全屏特效窗口：开（直播伴侣用「窗口」采集 DNF Kill FX Fullscreen）' : '全屏特效窗口：关', false);
    });
    fsScale?.addEventListener('input', () => {
        killDisplaySettings.layout.fxFullscreenScale = clampNumber(fsScale.value, 50, 200, 100);
        syncFullscreenControls();
        queueKillDisplaySettingsSave();
    });
    syncFullscreenControls();

    // 调试预览：kill.html?mode=fx#demo=ak（可选 2 / 3 / first / shutdown / revenge）
    const demo = KILL_FX_FULLSCREEN ? /demo=(\w+)/.exec(location.hash) : null;
    if (demo) {
        const level = /^\d$/.test(demo[1]) ? Number(demo[1]) : demo[1];
        setTimeout(() => KillFx.play({ team: 'red', level, name: '90老王', victim: '对手' }), 300);
        // #demo=ak&freeze=900：把所有 CSS 动画定格在第 900ms，便于截图检查
        const freeze = /freeze=(\d+)/.exec(location.hash);
        if (freeze) {
            const at = Number(freeze[1]);
            setTimeout(() => document.getAnimations().forEach(a => { try { a.pause(); a.currentTime = at; } catch (err) { /* ignore */ } }), 300 + Math.min(at, 600));
        }
    }

    window.KillFx = KillFx;
    window.KillFxTracker = KillFxTracker;
})();
