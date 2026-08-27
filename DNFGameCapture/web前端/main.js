// ==========================================
// 1. 核心：WebView2 同步引擎
// ==========================================
let playerDB = {};
let savedDB = {};
let isSyncingFromServer = false;
let hasReceivedInitialData = false;
let isMonitoring = false;
let isStartPending = false;
let isProMode = false;
let isKillDisplayWindowVisible = false;
let deathXAlgorithm = 0;
let deathPatchInstalled = false;
let outputSeatLabelToKillFile = false;
let redPickMode = 'second';
let scoreboardTextStyles = {};
let killDisplaySettings = {};
let keyMappingSettings = null;
let selectedKeyMappingSlot = 0;
let keyMappingCaptureSlot = -1;
let keyMappingSyncTimer = null;
let keyMappingAdminPromptVisible = false;
let pendingTeamSyncSnapshot = null;
let cloudMatchState = null;
let cloudRoomFirstRun = false;
let cloudSelectedRoomId = '';
let cloudRoomChoosing = false;
let cloudRoomJoinTarget = null;
let systemFonts = [];
let appearanceScope = 'scoreboard';
let activeScoreboardStyleKey = 'teamName';
let activeKillDisplayStyleKey = 'teamName';
let scoreboardStyleSyncTimer = null;
let killDisplaySettingsSyncTimer = null;
let recentEvents = [];
let isReviewPanelOpen = false;
let cxxConsoleLogs = [];
let isConsolePanelOpen = false;
let lastSentWebHeight = 0;
let webResizeTimer = null;
let draggedRow = null;
let isDbInitialized = false;
let isCloudDirectMode = false;
let cloudDirectToggleLock = false;
let lastDirectAliasPayload = '';
let directAliasSyncTimer = null;
const cloudDirectPressedKeys = new Set();
// Web 端编辑小号后，C++ 可能会立刻推回一次旧状态；这里短时间记录改名映射，避免旧小号被同步回来。
let pendingAliasRenameRecords = [];
// 新主号首次绑定小号时，弹窗会让输入框失焦；用这个标记避免 blur 提前同步空小号状态。
let pendingAliasPromptActive = false;
let pendingAliasPromptName = '';
let pendingAliasPopoverName = '';
let pendingAliasPopoverInput = null;
let activeAliasPopoverInput = null;
let ignoreNextDocumentClickUntil = 0;
const WEB_LAYOUT_VERSION = '20260828-cloud-match-room';
const KEY_MAPPING_SLOT_COUNT = 14;
const KEY_MAPPING_DEFAULT_LABELS = ['Q', 'W', 'E', 'R', 'T', 'Y', 'Ctrl', 'A', 'S', 'D', 'F', 'G', 'H', 'Alt'];
const KEY_MAPPING_DEFAULT_VKS = [81, 87, 69, 82, 84, 89, 17, 65, 83, 68, 70, 71, 72, 18];
let lastLayoutFitScale = 1;
let lastLayoutDiagSignature = '';
let layoutDiagnosticsTimer = null;
const randomToolState = {
    fixedIds: new Set(),
    fixedOrder: [],
    lastResult: null,
    activeSuggestLine: -1
};
let pendingClearAllUntil = 0;

// Ctrl 选择互换模式状态（与所在行无关，模块级即可，但放在 createPlayerRow 外更好）
// 建议放在文件顶部全局区域，或至少在 createPlayerRow 外定义
let ctrlSwapState = {
    active: false,
    sourceRow: null,
    targetRow: null,
    currentIndex: -1
};

function normalizeAliasTextForCompare(s) {
    return (s || '').trim();
}

function getAliasDuplicateId(aliasName) {
    const clean = normalizeAliasTextForCompare(aliasName);
    if (!clean) return '';
    const halfSharp = clean.indexOf('#');
    const fullSharp = clean.indexOf('＃');
    let sharp = -1;
    if (halfSharp >= 0 && fullSharp >= 0) sharp = Math.min(halfSharp, fullSharp);
    else sharp = halfSharp >= 0 ? halfSharp : fullSharp;
    return (sharp >= 0 ? clean.slice(0, sharp) : clean).trim();
}

function sameAliasId(a, b) {
    const aa = getAliasDuplicateId(a);
    const bb = getAliasDuplicateId(b);
    return !!aa && !!bb && aa === bb;
}

function getAliasJobKey(aliasName) {
    const clean = normalizeAliasTextForCompare(aliasName);
    if (!clean) return '';
    const halfSharp = clean.indexOf('#');
    const fullSharp = clean.indexOf('＃');
    let sharp = -1;
    if (halfSharp >= 0 && fullSharp >= 0) sharp = Math.min(halfSharp, fullSharp);
    else sharp = halfSharp >= 0 ? halfSharp : fullSharp;
    return sharp >= 0 ? clean.slice(sharp + 1).trim() : '';
}

function aliasHasDeclaredJob(aliasName) {
    return !!getAliasJobKey(aliasName);
}

function positionMoreControlsMenu() {
    const menu = document.getElementById('more-controls-menu');
    const button = document.getElementById('btn-more-controls');
    if (!menu || !button) return;
    // .app-shell is scaled to fit the WebView. A fixed element inside that
    // transformed tree is positioned against the shell instead of the viewport.
    if (menu.parentElement !== document.body) document.body.appendChild(menu);
    const margin = 12;
    const gap = 8;
    const buttonRect = button.getBoundingClientRect();
    const menuWidth = Math.max(260, Math.min(520, window.innerWidth - margin * 2));
    const left = Math.max(margin, Math.min(buttonRect.right - menuWidth,
        window.innerWidth - margin - menuWidth));
    const availableAbove = Math.max(0, buttonRect.top - margin - gap);
    const availableBelow = Math.max(0, window.innerHeight - buttonRect.bottom - margin - gap);
    const openAbove = availableAbove >= availableBelow;
    const chosenSpace = openAbove ? availableAbove : availableBelow;
    const useViewportFallback = chosenSpace < 160;
    const moreMenuMaxHeight = useViewportFallback
        ? Math.max(80, window.innerHeight - margin * 2)
        : chosenSpace;
    menu.style.setProperty('--more-menu-left', `${left}px`);
    menu.style.setProperty('--more-menu-top', useViewportFallback
        ? `${margin}px` : (openAbove ? 'auto' : `${buttonRect.bottom + gap}px`));
    menu.style.setProperty('--more-menu-bottom', useViewportFallback
        ? 'auto' : (openAbove ? `${window.innerHeight - buttonRect.top + gap}px` : 'auto'));
    menu.style.setProperty('--more-menu-width', `${menuWidth}px`);
    menu.style.setProperty('--more-menu-max-height', `${moreMenuMaxHeight}px`);
    menu.dataset.placement = openAbove ? 'top' : 'bottom';
}

function setMoreControlsOpen(open) {
    const menu = document.getElementById('more-controls-menu');
    const button = document.getElementById('btn-more-controls');
    const panel = document.querySelector('.control-panel');
    if (!menu || !button) return;
    if (open) positionMoreControlsMenu();
    menu.classList.toggle('active', open);
    menu.setAttribute('aria-hidden', open ? 'false' : 'true');
    button.classList.toggle('active', open);
    button.setAttribute('aria-expanded', open ? 'true' : 'false');
    panel?.classList.toggle('more-controls-open', open);
}

function toggleMoreControlsMenu() {
    const menu = document.getElementById('more-controls-menu');
    setMoreControlsOpen(!menu?.classList.contains('active'));
}

function sameAliasStorageEntry(a, b) {
    const aa = normalizeAliasTextForCompare(a);
    const bb = normalizeAliasTextForCompare(b);
    if (!aa || !bb) return false;
    if (aa === bb) return true;
    if (!sameAliasId(aa, bb)) return false;
    const aj = getAliasJobKey(aa);
    const bj = getAliasJobKey(bb);
    if (aj || bj) return !!aj && !!bj && aj === bj;
    return true;
}

function aliasBlocksSamePlayerAlias(existingAlias, candidateAlias) {
    const existing = normalizeAliasTextForCompare(existingAlias);
    const candidate = normalizeAliasTextForCompare(candidateAlias);
    if (!existing || !candidate) return false;
    if (sameAliasStorageEntry(existing, candidate)) return true;
    if (!aliasHasDeclaredJob(candidate)) {
        if (aliasHasDeclaredJob(existing) && sameAliasId(existing, candidate)) return true;
        if (existing !== candidate && existing.includes(candidate)) return true;
    }
    return false;
}

function mergeAliasIntoArray(out, rawAlias) {
    const alias = normalizeAliasTextForCompare(rawAlias);
    if (!alias) return 'none';
    const aliasHasJob = aliasHasDeclaredJob(alias);
    for (let i = 0; i < out.length; i++) {
        const existing = normalizeAliasTextForCompare(out[i]);
        if (!existing) continue;
        const existingHasJob = aliasHasDeclaredJob(existing);
        if (sameAliasStorageEntry(existing, alias)) {
            if (!existingHasJob && aliasHasJob) {
                out[i] = alias;
                return 'upgraded';
            }
            return 'none';
        }
        if (sameAliasId(existing, alias)) {
            if (!existingHasJob && aliasHasJob) {
                out[i] = alias;
                return 'upgraded';
            }
            if (existingHasJob && !aliasHasJob) return 'none';
        }
        if (!aliasHasJob && existing !== alias && existing.includes(alias)) {
            return 'none';
        }
    }
    out.push(alias);
    return 'added';
}

function findAliasByDuplicateId(arr, aliasName) {
    const targetId = getAliasDuplicateId(aliasName);
    if (!targetId) return '';
    return uniqueAliasArray(arr || []).find(alias => getAliasDuplicateId(alias) === targetId) || '';
}

function aliasArrayHasDuplicateId(arr, aliasName) {
    return !!findAliasByDuplicateId(arr, aliasName);
}

function findSamePlayerAliasBlock(arr, aliasName) {
    const clean = normalizeAliasTextForCompare(aliasName);
    if (!clean) return '';
    return uniqueAliasArray(arr || []).find(alias => aliasBlocksSamePlayerAlias(alias, clean)) || '';
}

function aliasArrayHasSamePlayerBlock(arr, aliasName) {
    return !!findSamePlayerAliasBlock(arr, aliasName);
}

function uniqueAliasArray(arr) {
    const out = [];
    (arr || []).forEach(item => {
        mergeAliasIntoArray(out, item);
    });
    return out;
}

function formatAliasArrayForCpp(arr) {
    return uniqueAliasArray(arr).map(alias => `(${alias})`).join('');
}

function normalizeAllAliasStores() {
    for (const key in playerDB) {
        playerDB[key] = uniqueAliasArray(playerDB[key]);
    }
    for (const key in savedDB) {
        savedDB[key] = uniqueAliasArray(savedDB[key]);
    }
}

function removeAliasFromArray(arr, aliasName) {
    const target = normalizeAliasTextForCompare(aliasName);
    return uniqueAliasArray((arr || []).filter(a => {
        const clean = normalizeAliasTextForCompare(a);
        return !target || !sameAliasStorageEntry(clean, target);
    }));
}

function applyPendingAliasRenamesToDb(dbObj) {
    const now = Date.now();
    pendingAliasRenameRecords = pendingAliasRenameRecords.filter(r => r && r.until > now);
    for (const r of pendingAliasRenameRecords) {
        if (!r.playerName || !dbObj[r.playerName]) continue;
        dbObj[r.playerName] = removeAliasFromArray(dbObj[r.playerName], r.oldAlias);
        const newAlias = normalizeAliasTextForCompare(r.newAlias);
        dbObj[r.playerName] = uniqueAliasArray(dbObj[r.playerName]);
        if (newAlias) mergeAliasIntoArray(dbObj[r.playerName], newAlias);
        dbObj[r.playerName] = uniqueAliasArray(dbObj[r.playerName]);
    }
}

function restorePendingAliasPopover() {
    if (!pendingAliasPopoverName) return false;

    let inputElem = pendingAliasPopoverInput;
    if (!inputElem || !document.contains(inputElem)) {
        inputElem = document.querySelector('.player-row.active-row .name-input');
    }
    if (!inputElem) return false;

    inputElem.value = pendingAliasPopoverName;
    inputElem.classList.remove('input-error');
    inputElem.removeAttribute('data-error-msg');
    return !!openAliasPopover(inputElem, pendingAliasPopoverName);
}

function clearPendingAliasPopoverLock() {
    pendingAliasPopoverName = '';
    pendingAliasPopoverInput = null;
    activeAliasPopoverInput = null;
}

function closeAliasPopovers() {
    document.querySelectorAll('.alias-popover').forEach(popElement => popElement.classList.remove('active'));
    clearPendingAliasPopoverLock();
}

function closeAliasPopoverForInput(inputElem) {
    const row = inputElem?.closest('.player-row');
    const aliasPopover = row?.querySelector('.alias-popover');
    if (aliasPopover) aliasPopover.classList.remove('active');
    if (!activeAliasPopoverInput || activeAliasPopoverInput === inputElem) clearPendingAliasPopoverLock();
}

function openAliasPopover(inputElem, playerName, options = {}) {
    const cleanName = String(playerName || '').trim();
    const row = inputElem?.closest('.player-row');
    if (!row || !cleanName) return null;

    const autoPopover = row.querySelector('.autocomplete-popover');
    const aliasPopover = row.querySelector('.alias-popover');
    if (!aliasPopover) return null;

    document.querySelectorAll('.alias-popover').forEach(popElement => {
        if (popElement !== aliasPopover) popElement.classList.remove('active');
    });
    document.querySelectorAll('.player-row').forEach(item => {
        if (item !== row) item.classList.remove('active-row');
    });

    if (autoPopover && !options.keepAutoPopover) autoPopover.classList.remove('active');
    row.classList.add('active-row');
    renderAliasMenu(cleanName, aliasPopover);
    aliasPopover.classList.add('active');

    activeAliasPopoverInput = inputElem;
    pendingAliasPopoverInput = inputElem;
    pendingAliasPopoverName = cleanName;

    scheduleAliasPopoverLayout(aliasPopover);
    return aliasPopover;
}

function alignAliasPopoverToDragHandle(popElement) {
    const row = popElement?.closest('.player-row');
    const dragHandle = row?.querySelector('.drag-handle');
    if (!popElement || !dragHandle) return;

    popElement.style.left = `${Math.round(dragHandle.offsetLeft)}px`;
}

function clampAliasPopoverToViewport(popElement) {
    if (!popElement || !popElement.classList.contains('active')) return;

    const margin = 8;
    const viewportWidth = document.documentElement.clientWidth || window.innerWidth || 0;
    const viewportHeight = document.documentElement.clientHeight || window.innerHeight || 0;

    popElement.classList.remove('alias-fit-sm', 'alias-fit-xs');
    popElement.style.left = '';
    popElement.style.width = '';
    popElement.style.maxHeight = '';
    alignAliasPopoverToDragHandle(popElement);

    let rect = popElement.getBoundingClientRect();
    if (!rect.width || !rect.height) return;

    const normalLeft = popElement.offsetLeft;
    const rightEdge = viewportWidth ? viewportWidth - margin : 0;
    const preferredMaxWidth = Math.min(320, Math.max(190, (viewportWidth || 336) - margin * 2));
    const minWidth = Math.min(190, preferredMaxWidth);
    const rightSpaceWidth = viewportWidth ? rightEdge - rect.left : rect.width;

    if (viewportWidth && rightSpaceWidth < preferredMaxWidth && rightSpaceWidth >= minWidth) {
        popElement.style.width = `${Math.floor(rightSpaceWidth)}px`;
        rect = popElement.getBoundingClientRect();
    }

    const applyFontFit = () => {
        const aliases = Array.from(popElement.querySelectorAll('.alias-name'));
        const overflowed = aliases.some(item => item.scrollWidth > item.clientWidth + 1);
        if (!overflowed) return false;

        popElement.classList.add('alias-fit-sm');
        const smOverflowed = aliases.some(item => item.scrollWidth > item.clientWidth + 1);
        if (!smOverflowed) return false;

        popElement.classList.add('alias-fit-xs');
        return aliases.some(item => item.scrollWidth > item.clientWidth + 1);
    };

    const stillOverflowed = applyFontFit();
    rect = popElement.getBoundingClientRect();

    if (viewportWidth && rect.right > rightEdge && stillOverflowed) {
        let left = normalLeft - (rect.right - rightEdge);
        popElement.style.left = `${Math.round(left)}px`;
        rect = popElement.getBoundingClientRect();
    }

    if (viewportWidth && rect.left < margin) {
        let left = popElement.offsetLeft;
        left += margin - rect.left;
        popElement.style.left = `${Math.round(left)}px`;
        rect = popElement.getBoundingClientRect();
    }

    if (viewportHeight) {
        const availableHeight = viewportHeight - margin - Math.max(margin, rect.top);
        popElement.style.maxHeight = `${Math.min(240, Math.max(64, Math.floor(availableHeight)))}px`;
    }
}

function scheduleAliasPopoverLayout(popElement) {
    if (!popElement) return;
    const run = () => clampAliasPopoverToViewport(popElement);
    if (window.requestAnimationFrame) {
        window.requestAnimationFrame(run);
    } else {
        setTimeout(run, 0);
    }
}

function layoutActiveAliasPopovers() {
    document.querySelectorAll('.alias-popover.active').forEach(popElement => {
        scheduleAliasPopoverLayout(popElement);
    });
}

function getLayoutDiagnostics() {
    const shell = document.querySelector('.app-shell');
    const body = document.body;
    const html = document.documentElement;
    const previousScale = lastLayoutFitScale || 1;

    if (shell) shell.style.setProperty('--layout-fit-scale', '1');
    const shellRect = withDefaultScoreboardLayoutMeasurement(() =>
        shell ? shell.getBoundingClientRect() : { width: 0, height: 0 }
    );
    const viewportW = window.innerWidth || html.clientWidth || 0;
    const viewportH = window.innerHeight || html.clientHeight || 0;
    const bodyStyle = window.getComputedStyle(body);
    const padLeft = parseFloat(bodyStyle.paddingLeft) || 0;
    const padRight = parseFloat(bodyStyle.paddingRight) || 0;
    const padTop = parseFloat(bodyStyle.paddingTop) || 0;
    const padBottom = parseFloat(bodyStyle.paddingBottom) || 0;
    const usableW = Math.max(1, viewportW - padLeft - padRight);
    const usableH = Math.max(1, viewportH - padTop - padBottom);
    const naturalW = Math.ceil(shellRect.width || body.scrollWidth || 0);
    const naturalH = Math.ceil(shellRect.height || body.scrollHeight || 0);
    const appearanceOpen = isAppearancePanelOpen();
    const widthScale = naturalW > usableW ? usableW / naturalW : 1;
    const heightScale = !appearanceOpen && naturalH > usableH ? usableH / naturalH : 1;
    const fitScale = Math.max(0.5, Math.min(1, widthScale, heightScale));

    if (shell) shell.style.setProperty('--layout-fit-scale', String(fitScale));
    lastLayoutFitScale = fitScale;

    return {
        layoutVersion: WEB_LAYOUT_VERSION,
        metaLayoutVersion: document.querySelector('meta[name="dnf-web-layout-version"]')?.content || '',
        href: location.href,
        innerWidth: viewportW,
        innerHeight: viewportH,
        devicePixelRatio: Number(window.devicePixelRatio || 1),
        bodyScrollWidth: body.scrollWidth,
        bodyScrollHeight: body.scrollHeight,
        htmlClientWidth: html.clientWidth,
        htmlClientHeight: html.clientHeight,
        appShellNaturalWidth: naturalW,
        appShellNaturalHeight: naturalH,
        usableWidth: Math.round(usableW),
        usableHeight: Math.round(usableH),
        fitScale,
        previousScale,
        appearanceOpen
    };
}

function applyLayoutFitScale(sendDiag = false, reason = 'layout') {
    const diag = getLayoutDiagnostics();
    if (sendDiag && window.chrome?.webview) {
        const signature = [
            reason,
            diag.innerWidth,
            diag.bodyScrollWidth,
            diag.appShellNaturalWidth,
            diag.fitScale.toFixed(3),
            diag.devicePixelRatio
        ].join('|');
        if (signature !== lastLayoutDiagSignature) {
            lastLayoutDiagSignature = signature;
            window.chrome.webview.postMessage({
                action: 'web_layout_diagnostics',
                reason,
                data: diag
            });
        }
    }
    layoutActiveAliasPopovers();
    return diag;
}

function scheduleLayoutFit(sendDiag = false, reason = 'layout') {
    if (layoutDiagnosticsTimer) clearTimeout(layoutDiagnosticsTimer);
    layoutDiagnosticsTimer = setTimeout(() => {
        applyLayoutFitScale(sendDiag, reason);
    }, 50);
}

if (window.chrome && window.chrome.webview) {
    window.chrome.webview.addEventListener('message', function (event) {
        try {
            const msg = typeof event.data === 'string' ? JSON.parse(event.data) : event.data;
            if (msg.action === 'sync_state') {
                hasReceivedInitialData = true;

                if (msg.data.fullAliasDB) {
                    let newSavedDB = {};

                    for (let key in msg.data.fullAliasDB) {
                        let arr = uniqueAliasArray(msg.data.fullAliasDB[key].split(/[()（）]/).filter(s => s.trim()));
                        newSavedDB[key] = [...arr];

                        if (!playerDB[key]) {
                            playerDB[key] = uniqueAliasArray(arr);
                        } else {
                            let oldSaved = savedDB[key] || [];
                            let newFromMFC = arr.filter(a => !oldSaved.includes(a));
                            let deletedFromMFC = oldSaved.filter(a => !arr.includes(a));

                            let updatedPlayerDB = uniqueAliasArray(playerDB[key]);
                            deletedFromMFC.forEach(a => { updatedPlayerDB = removeAliasFromArray(updatedPlayerDB, a); });
                            newFromMFC.forEach(a => {
                                updatedPlayerDB = removeAliasFromArray(updatedPlayerDB, a);
                                updatedPlayerDB.push(a);
                            });

                            playerDB[key] = uniqueAliasArray(updatedPlayerDB);
                        }
                    }

                    // 如果刚刚在 Web 端改过小号名，而 C++ 推回来的是旧库，先按本地改名记录修正。
                    applyPendingAliasRenamesToDb(newSavedDB);
                    for (let key in newSavedDB) {
                        if (!playerDB[key]) playerDB[key] = [];
                        applyPendingAliasRenamesToDb(playerDB);
                    }

                    for (let key in playerDB) {
                        if (!msg.data.fullAliasDB[key]) {
                            delete playerDB[key];
                        }
                    }
                    savedDB = newSavedDB;
                    normalizeAllAliasStores();
                }

                // ========================================================
                // 🚨 核心修复：无条件服从 C++ 的场上活跃选手状态！
                // ========================================================
                if (msg.data.players) {
                    msg.data.players.forEach(p => {
                        // 如果这个选手正在场上（红蓝两队 8 个框里）
                        if (p.name && p.name.trim() !== '') {
                            // 直接用 C++ 传来的最新小号列表，强行覆盖 Web 端的展示库！
                            // 这样 C++ 无论是加回来、还是在 C++ 里临时删掉，Web 端都能瞬间无缝同步！
                            // C++ 推回的场上小号也要去重；否则 Web 添加一次后，旧同步/新同步叠加会显示多份。
                            playerDB[p.name] = uniqueAliasArray(p.aliases);
                            if (!savedDB[p.name]) savedDB[p.name] = [];
                            savedDB[p.name] = uniqueAliasArray(savedDB[p.name]);
                        }
                    });
                }
                normalizeAllAliasStores();
                applyStateFromServer(msg.data);
                scheduleLayoutFit(true, 'sync-state');

                if (!restorePendingAliasPopover()) {
                    let activeRowInput = document.querySelector('.player-row.active-row .name-input');
                    let aliasPopover = document.querySelector('.alias-popover.active');
                    if (activeRowInput && aliasPopover) {
                        let activeName = activeRowInput.value.trim();
                        if (activeName && playerDB[activeName]) {
                            openAliasPopover(activeRowInput, activeName);
                        }
                    }
                }
            }
            else if (msg.action === 'console_logs') {
                appendConsoleLogs(msg.logs || []);
            }
            else if (msg.action === 'web_zoom_calibrated') {
                scheduleLayoutFit(true, 'zoom-calibrated');
            }
            else if (msg.action === 'key_mapping_admin_required') {
                showKeyMappingAdminPrompt(msg.message);
            }
            else if (msg.action === 'team_sync_snapshot') {
                showTeamSyncSnapshot(msg.data);
            }
            else if (msg.action === 'team_sync_error') {
                pendingTeamSyncSnapshot = null;
                showAlert(msg.message);
            }
            else if (msg.action === 'cloud_room_prompt') {
                if (!cloudMatchState) cloudMatchState = normalizeCloudMatchState({ shouldPrompt: true });
                else cloudMatchState.shouldPrompt = true;
                restoreCloudRoomPromptFromState();
            }
            else if (msg.action === 'auth_result' || msg.action === 'start_guard' || msg.action === 'patch_result' || msg.action === 'alias_submit_result' || msg.action === 'alias_sync_result' || msg.action === 'copy_window_clipboard_result' || msg.action === 'kill_obs_url_result' || msg.action === 'key_mapping_error') { showAlert(msg.message); }
            else if (msg.action === 'alias_direct_sync_result') {
                if (String(msg.message || '').includes('失败')) showAlert(msg.message);
                else console.info('[alias direct sync]', msg.message);
            }
        } catch (e) { console.error('解析 C++ 消息失败', e); }
    });

    let handshakeTimer = setInterval(() => {
        if (!hasReceivedInitialData) window.chrome.webview.postMessage({ action: "page_ready" });
        else clearInterval(handshakeTimer);
    }, 500);
}

function buildFormattedAliasDB() {
    let formattedDB = {};
    normalizeAllAliasStores();
    for (let key in savedDB) {
        savedDB[key] = uniqueAliasArray(savedDB[key]);
        if (key && key.trim()) formattedDB[key] = formatAliasArrayForCpp(savedDB[key]);
    }
    return formattedDB;
}

function stableAliasDbPayload(dbObj) {
    const sorted = {};
    Object.keys(dbObj || {}).sort().forEach(key => {
        sorted[key] = dbObj[key];
    });
    return JSON.stringify(sorted);
}

function queueDirectAliasDbSync(formattedDB, force = false) {
    if (!isCloudDirectMode || !window.chrome?.webview) return;

    const payload = stableAliasDbPayload(formattedDB);
    if (!force && payload === lastDirectAliasPayload) return;
    lastDirectAliasPayload = payload;

    clearTimeout(directAliasSyncTimer);
    directAliasSyncTimer = setTimeout(() => {
        window.chrome.webview.postMessage({
            action: "cmd_direct_sync_alias_db",
            data: { fullAliasDB: formattedDB }
        });
    }, force ? 50 : 800);
}

function toggleCloudDirectMode() {
    isCloudDirectMode = !isCloudDirectMode;
    const fullAliasDB = buildFormattedAliasDB();

    if (isCloudDirectMode) {
        showAlert('管理员直写模式已开启。<br>当前小号库会同步一次，之后你对小号库的修改会直接写入云端公共库。');
        queueDirectAliasDbSync(fullAliasDB, true);
    } else {
        showAlert('管理员直写模式已关闭。');
        if (window.chrome?.webview) {
            window.chrome.webview.postMessage({ action: "cmd_set_alias_direct_mode", enabled: false });
        }
    }
}


function pushStateToServer() {
    if (!window.chrome || !window.chrome.webview || isSyncingFromServer) return;

    // ==========================================
    // 🚨 核心逻辑：智能恢复“临时解绑”状态
    // ==========================================
    // 1. 获取当前所有还在 8 个输入框（场上）的选手名字
    let activeNames = Array.from(document.querySelectorAll('.name-input'))
        .map(inp => inp.value.trim())
        .filter(name => name !== '');

    // 2. 遍历永久库，如果选手已经下场了，就自动恢复他的所有小号
    for (let name in savedDB) {
        if (!activeNames.includes(name)) {
            playerDB[name] = uniqueAliasArray(savedDB[name]);
        }
    }
    // ==========================================

    // 🚨 注意：发给 C++ 的永远是不受“临时解绑”影响的永久库
    let formattedDB = buildFormattedAliasDB();

    let state = {
        blueScore: parseInt(document.querySelector('#team-blue .team-score-input').value) || 0,
        redScore: parseInt(document.querySelector('#team-red .team-score-input').value) || 0,
        redPickMode,
        players: [],
        fullAliasDB: formattedDB
    };

    document.querySelectorAll('#team-red .player-row').forEach(row => state.players.push(getRowData(row, 0)));
    document.querySelectorAll('#team-blue .player-row').forEach(row => state.players.push(getRowData(row, 1)));
    window.chrome.webview.postMessage({ action: "update_state", data: state });
    if (isCloudDirectMode) queueDirectAliasDbSync(formattedDB);
}

function getRowData(row, teamId) {
    let nameElem = row.querySelector('.name-input');
    let name = nameElem.value.trim();
    if (nameElem.classList.contains('input-error')) name = '';

    const aliases = name ? getCleanAliases(name) : [];

    return {
        team: teamId, name: name,
        kills: parseInt(row.querySelector('.stat-kill').value) || 0,
        deaths: parseInt(row.querySelector('.stat-death').value) || 0,
        akCount: parseInt(row.querySelector('.stat-ak').value) || 0,
        aliases: name ? aliases : []
    };
}

const PICK_LABEL_SEQUENCES = {
    first: {
        red: ['x选', '1选', '4选', '6选'],
        blue: ['h选', '2选', '3选', '5选']
    },
    second: {
        red: ['h选', '2选', '4选', '5选'],
        blue: ['x选', '1选', '3选', '6选']
    }
};

function getSeatLabelInput(row) {
    return row?.querySelector('.seat-label-input') || row?.querySelector('.seat-number');
}

function getSeatLabelToggle(row) {
    return row?.querySelector('.seat-label-toggle');
}

function getTeamRows(teamId) {
    const selector = teamId === 0 ? '#team-red .player-row' : '#team-blue .player-row';
    return Array.from(document.querySelectorAll(selector));
}

function getSeatTeamId(row) {
    if (row?.closest('#team-red')) return 0;
    if (row?.closest('#team-blue')) return 1;
    return -1;
}

function getSeatRowIndex(row) {
    const teamId = getSeatTeamId(row);
    return teamId >= 0 ? getTeamRows(teamId).indexOf(row) : -1;
}

function normalizeRedPickMode(value) {
    return String(value || '').toLowerCase() === 'second' ? 'second' : 'first';
}

function cleanSeatLabel(value) {
    return String(value || '').replace(/[|\r\n]/g, '').trim().slice(0, 16);
}

function normalizePickChoice(value) {
    const raw = cleanSeatLabel(value).toLowerCase();
    if (!raw) return '';
    if (raw.startsWith('x') || raw.includes('先')) return 'x选';
    if (raw.startsWith('h') || raw.includes('后')) return 'h选';
    return '';
}

function displayPickChoiceLabel(value) {
    const normalized = normalizePickChoice(value);
    if (normalized === 'x选') return '先选';
    if (normalized === 'h选') return '后选';
    return value || '';
}

function getPickLabelsForTeam(teamId) {
    const mode = normalizeRedPickMode(redPickMode);
    const side = teamId === 0 ? 'red' : 'blue';
    return PICK_LABEL_SEQUENCES[mode][side];
}

function getPickLabelForRow(row) {
    const teamId = getSeatTeamId(row);
    const idx = getSeatRowIndex(row);
    if (teamId < 0 || idx < 0) return '';
    return getPickLabelsForTeam(teamId)[idx] || '';
}

function getRedPickModeFromTeamChoice(teamId, choice) {
    const normalized = normalizePickChoice(choice);
    if (normalized === 'x选') return teamId === 0 ? 'first' : 'second';
    if (normalized === 'h选') return teamId === 0 ? 'second' : 'first';
    return redPickMode;
}

function refreshPickLabels() {
    [0, 1].forEach(teamId => {
        const rows = getTeamRows(teamId);
        const labels = getPickLabelsForTeam(teamId);
        rows.forEach((row, idx) => {
            const input = getSeatLabelInput(row);
            const toggle = getSeatLabelToggle(row);
            if (!input && !toggle) return;
            const isTop = idx === 0;
            const label = labels[idx] || '';
            if (input) {
                if ('value' in input) input.value = label;
                else input.textContent = label;
                input.readOnly = true;
                input.hidden = isTop;
                input.setAttribute('aria-readonly', 'true');
                input.tabIndex = -1;
                input.title = '按先后手自动生成';
            }
            if (toggle) {
                toggle.textContent = displayPickChoiceLabel(label);
                toggle.value = label;
                toggle.dataset.pickLabel = label;
                toggle.hidden = !isTop;
                toggle.disabled = !isTop;
                toggle.tabIndex = isTop ? 0 : -1;
                toggle.title = '点击切换先选 / 后选';
                toggle.setAttribute('aria-label', `当前${displayPickChoiceLabel(label)}，点击切换`);
            }
        });
    });
}

function postRedPickModeToServer() {
    if (window.chrome?.webview) {
        window.chrome.webview.postMessage({ action: 'cmd_set_red_pick_mode', mode: redPickMode });
    }
}

function setRedPickMode(mode, sync = false) {
    const next = normalizeRedPickMode(mode);
    const changed = redPickMode !== next;
    redPickMode = next;
    refreshPickLabels();
    if (sync && !isSyncingFromServer) {
        if (changed) postRedPickModeToServer();
        triggerSync();
    }
}

function setPickModeFromInput(row, value, sync = true) {
    const teamId = getSeatTeamId(row);
    if (teamId < 0) {
        refreshPickLabels();
        return;
    }
    setRedPickMode(getRedPickModeFromTeamChoice(teamId, value), sync);
}

function togglePickModeForRow(row, sync = true) {
    const current = normalizePickChoice(getPickLabelForRow(row));
    const next = current === 'x选' ? 'h选' : 'x选';
    setPickModeFromInput(row, next, sync);
}

function resetSeatLabelsToDefault() {
    setRedPickMode('second', false);
}

const SCOREBOARD_TEXT_STYLE_TYPES = [
    {
        key: 'teamName',
        cssKey: 'team-name',
        label: '队名',
        allowTeamColor: true,
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 38, colorMode: 'team', color: '#ffffff', strokeColor: '#000000', strokeWidth: 0, glow: 8 }
    },
    {
        key: 'score',
        cssKey: 'score',
        label: '比分',
        allowTeamColor: true,
        defaults: { fontFamily: 'Arial Black', fontSize: 39, colorMode: 'team', color: '#ffffff', strokeColor: '#000000', strokeWidth: 0, glow: 12 }
    },
    {
        key: 'header',
        cssKey: 'header',
        label: '表头',
        allowTeamColor: false,
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 22, colorMode: 'custom', color: '#8b8b9f', strokeColor: '#000000', strokeWidth: 0, glow: 0 }
    },
    {
        key: 'pickLabel',
        cssKey: 'pick-label',
        label: '选人顺序',
        allowTeamColor: false,
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 18, colorMode: 'custom', color: '#a6b7bf', strokeColor: '#000000', strokeWidth: 1, glow: 0 }
    },
    {
        key: 'playerName',
        cssKey: 'player-name',
        label: '主号名',
        allowTeamColor: false,
        defaults: { fontFamily: 'Arial Black', fontSize: 22, colorMode: 'custom', color: '#ffffff', strokeColor: '#000000', strokeWidth: 1, glow: 2 }
    },
    {
        key: 'statNumber',
        cssKey: 'stat-number',
        label: '战绩数字',
        allowTeamColor: false,
        defaults: { fontFamily: 'Microsoft YaHei', fontSize: 25, colorMode: 'custom', color: '#ffffff', strokeColor: '#000000', strokeWidth: 1, glow: 0 }
    }
];

const KILL_DISPLAY_LAYOUT_DEFAULTS = {
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

const KILL_DISPLAY_LAYOUT_FIELDS = [
    { key: 'bgAlpha', label: '背景透明度', min: 0, max: 100, unit: '%' },
    { key: 'panelAlpha', label: '面板透明度', min: 0, max: 100, unit: '%' },
    { key: 'rowAlpha', label: '行背景透明度', min: 0, max: 100, unit: '%' },
    { key: 'canvasPadding', label: '整体留边', min: 0, max: 40, unit: 'px' },
    { key: 'panelPadding', label: '面板内边距', min: 0, max: 40, unit: 'px' },
    { key: 'teamGap', label: '队伍间距', min: 0, max: 40, unit: 'px' },
    { key: 'rowGap', label: '行距', min: 0, max: 20, unit: 'px' },
    { key: 'rowHeight', label: '行高', min: 32, max: 90, unit: 'px' },
    { key: 'panelRadius', label: '面板圆角', min: 0, max: 28, unit: 'px' },
    { key: 'rowRadius', label: '行圆角', min: 0, max: 22, unit: 'px' },
    { key: 'boardBorder', label: '边框宽度', min: 0, max: 6, unit: 'px' },
    { key: 'shadow', label: '整体阴影', min: 0, max: 48, unit: 'px' },
    { key: 'pickColumnWidth', label: '选人列宽', min: 36, max: 110, unit: 'px' },
    { key: 'statColumnWidth', label: '战绩列宽', min: 28, max: 90, unit: 'px' },
    { key: 'akColumnWidth', label: 'AK列宽', min: 24, max: 80, unit: 'px' },
    { key: 'pageScale', label: '整体缩放', min: 60, max: 180, unit: '%' },
    { key: 'teamNameOffsetX', label: '队名横移', min: -180, max: 180, unit: 'px', hidden: true },
    { key: 'teamNameOffsetY', label: '队名纵移', min: -120, max: 120, unit: 'px', hidden: true },
    { key: 'pickLabelOffsetX', label: '选人横移', min: -180, max: 180, unit: 'px', hidden: true },
    { key: 'pickLabelOffsetY', label: '选人纵移', min: -120, max: 120, unit: 'px', hidden: true },
    { key: 'playerNameOffsetX', label: '主号横移', min: -180, max: 180, unit: 'px', hidden: true },
    { key: 'playerNameOffsetY', label: '主号纵移', min: -120, max: 120, unit: 'px', hidden: true },
    { key: 'killNumberOffsetX', label: '杀横移', min: -180, max: 180, unit: 'px', hidden: true },
    { key: 'killNumberOffsetY', label: '杀纵移', min: -120, max: 120, unit: 'px', hidden: true },
    { key: 'deathNumberOffsetX', label: '死横移', min: -180, max: 180, unit: 'px', hidden: true },
    { key: 'deathNumberOffsetY', label: '死纵移', min: -120, max: 120, unit: 'px', hidden: true },
    { key: 'akMarkOffsetX', label: 'AK横移', min: -180, max: 180, unit: 'px', hidden: true },
    { key: 'akMarkOffsetY', label: 'AK纵移', min: -120, max: 120, unit: 'px', hidden: true },
    { key: 'akCountBadgeOffsetX', label: 'AK次数横移', min: -80, max: 80, unit: 'px', hidden: true },
    { key: 'akCountBadgeOffsetY', label: 'AK次数纵移', min: -80, max: 80, unit: 'px', hidden: true }
];
const KILL_HORIZONTAL_ONLY_OFFSET_Y_KEYS = [
    'pickLabelOffsetY',
    'playerNameOffsetY',
    'killNumberOffsetY',
    'deathNumberOffsetY',
    'akMarkOffsetY'
];

const KILL_DISPLAY_TEXT_STYLE_TYPES = [
    {
        key: 'teamName',
        cssKey: 'team-name',
        label: '队名',
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
        label: '主号名',
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
        label: 'AK标记',
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

function getScoreboardStyleType(key) {
    return SCOREBOARD_TEXT_STYLE_TYPES.find(t => t.key === key) || SCOREBOARD_TEXT_STYLE_TYPES[0];
}

function getKillDisplayStyleType(key) {
    return KILL_DISPLAY_TEXT_STYLE_TYPES.find(t => t.key === key) || KILL_DISPLAY_TEXT_STYLE_TYPES[0];
}

function getCurrentAppearanceStyleTypes() {
    return appearanceScope === 'kill' ? KILL_DISPLAY_TEXT_STYLE_TYPES : SCOREBOARD_TEXT_STYLE_TYPES;
}

function getCurrentAppearanceStyleType(key) {
    return appearanceScope === 'kill' ? getKillDisplayStyleType(key) : getScoreboardStyleType(key);
}

function getCurrentAppearanceStyleKey() {
    return appearanceScope === 'kill' ? activeKillDisplayStyleKey : activeScoreboardStyleKey;
}

function setCurrentAppearanceStyleKey(key) {
    if (appearanceScope === 'kill') activeKillDisplayStyleKey = key;
    else activeScoreboardStyleKey = key;
}

function getCurrentAppearanceStyles() {
    return appearanceScope === 'kill' ? (killDisplaySettings.textStyles || {}) : scoreboardTextStyles;
}

function cloneScoreboardStyle(style) {
    return { ...style };
}

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

function getDefaultScoreboardTextStyles() {
    return SCOREBOARD_TEXT_STYLE_TYPES.reduce((acc, type) => {
        acc[type.key] = cloneScoreboardStyle(type.defaults);
        return acc;
    }, {});
}

function normalizeScoreboardTextStyle(key, value = {}) {
    const type = getScoreboardStyleType(key);
    const defaults = type.defaults;
    const style = value && typeof value === 'object' ? value : {};
    const requestedColorMode = String(style.colorMode || defaults.colorMode);
    const colorMode = type.allowTeamColor && requestedColorMode === 'team' ? 'team' : 'custom';
    return {
        fontFamily: cleanFontFamilyName(style.fontFamily, defaults.fontFamily),
        fontSize: clampNumber(style.fontSize, 10, 48, defaults.fontSize),
        colorMode: type.allowTeamColor ? (requestedColorMode === 'custom' ? 'custom' : colorMode) : 'custom',
        color: normalizeHexColor(style.color, defaults.color),
        strokeColor: normalizeHexColor(style.strokeColor, defaults.strokeColor),
        strokeWidth: clampNumber(style.strokeWidth, 0, 4, defaults.strokeWidth),
        glow: clampNumber(style.glow, 0, 24, defaults.glow)
    };
}

function normalizeScoreboardTextStyles(styles = {}) {
    const normalized = {};
    SCOREBOARD_TEXT_STYLE_TYPES.forEach(type => {
        normalized[type.key] = normalizeScoreboardTextStyle(type.key, styles?.[type.key]);
    });
    return normalized;
}

function getDefaultKillDisplayLayout() {
    return { ...KILL_DISPLAY_LAYOUT_DEFAULTS };
}

function getDefaultKillDisplayTextStyles() {
    return KILL_DISPLAY_TEXT_STYLE_TYPES.reduce((acc, type) => {
        acc[type.key] = cloneScoreboardStyle(type.defaults);
        return acc;
    }, {});
}

function getDefaultKillDisplaySettings() {
    return {
        obsUrl: 'http://127.0.0.1:18777/kill.html',
        layout: getDefaultKillDisplayLayout(),
        textStyles: getDefaultKillDisplayTextStyles()
    };
}

function normalizeKillDisplayLayout(layout = {}) {
    const normalized = {};
    KILL_DISPLAY_LAYOUT_FIELDS.forEach(field => {
        normalized[field.key] = KILL_HORIZONTAL_ONLY_OFFSET_Y_KEYS.includes(field.key)
            ? 0
            : clampNumber(layout?.[field.key], field.min, field.max, KILL_DISPLAY_LAYOUT_DEFAULTS[field.key]);
    });
    normalized.showDeathNumber = clampNumber(layout?.showDeathNumber, 0, 1, KILL_DISPLAY_LAYOUT_DEFAULTS.showDeathNumber) ? 1 : 0;
    return normalized;
}

function normalizeKillDisplayTextStyle(key, value = {}) {
    const type = getKillDisplayStyleType(key);
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

function normalizeKillDisplaySettings(settings = {}) {
    const defaults = getDefaultKillDisplaySettings();
    return {
        obsUrl: String(settings?.obsUrl || defaults.obsUrl),
        layout: normalizeKillDisplayLayout(settings?.layout || defaults.layout),
        textStyles: normalizeKillDisplayTextStyles(settings?.textStyles || defaults.textStyles)
    };
}

function applyKillDisplaySettings(settings = killDisplaySettings) {
    killDisplaySettings = normalizeKillDisplaySettings(settings);
    syncMainShowDeathToggle();
}

function syncMainShowDeathToggle() {
    const input = document.getElementById('kill-show-death-toggle-main');
    if (!input) return;
    const enabled = normalizeKillDisplayLayout(killDisplaySettings.layout).showDeathNumber === 1;
    input.checked = enabled;
    input.closest('.kill-show-death-toggle')?.classList.toggle('active', enabled);
}

function normalizeSystemFonts(fonts = []) {
    const seen = new Set();
    const result = [];
    [...(Array.isArray(fonts) ? fonts : []), 'Microsoft YaHei', 'SimHei', 'Arial', 'Arial Black'].forEach(name => {
        const cleaned = cleanFontFamilyName(name, '');
        const key = cleaned.toLowerCase();
        if (!cleaned || seen.has(key)) return;
        seen.add(key);
        result.push(cleaned);
    });
    return result;
}

function getStyleColorForSide(type, style, side) {
    if (type.allowTeamColor && style.colorMode === 'team') {
        return side === 'red' ? '#ff0055' : '#00e5ff';
    }
    return style.color;
}

function setScoreboardStyleVars(type, style) {
    const root = document.documentElement;
    const base = `--sb-${type.cssKey}`;
    root.style.setProperty(`${base}-font-family`, cssFontFamily(style.fontFamily));
    root.style.setProperty(`${base}-font-size`, `${style.fontSize}px`);
    root.style.setProperty(`${base}-stroke-color`, style.strokeColor);
    root.style.setProperty(`${base}-stroke-width`, `${style.strokeWidth}px`);
    root.style.setProperty(`${base}-glow`, `${style.glow}px`);

    if (type.allowTeamColor) {
        const redColor = getStyleColorForSide(type, style, 'red');
        const blueColor = getStyleColorForSide(type, style, 'blue');
        root.style.setProperty(`${base}-red-color`, redColor);
        root.style.setProperty(`${base}-blue-color`, blueColor);
        root.style.setProperty(`${base}-red-glow-rgb`, hexToRgbParts(redColor));
        root.style.setProperty(`${base}-blue-glow-rgb`, hexToRgbParts(blueColor));
    } else {
        root.style.setProperty(`${base}-color`, style.color);
        root.style.setProperty(`${base}-glow-rgb`, hexToRgbParts(style.color));
    }
}

function getScoreboardStyleVarNames(type) {
    const base = `--sb-${type.cssKey}`;
    const names = [
        `${base}-font-family`,
        `${base}-font-size`,
        `${base}-stroke-color`,
        `${base}-stroke-width`,
        `${base}-glow`
    ];
    if (type.allowTeamColor) {
        names.push(
            `${base}-red-color`,
            `${base}-blue-color`,
            `${base}-red-glow-rgb`,
            `${base}-blue-glow-rgb`
        );
    } else {
        names.push(`${base}-color`, `${base}-glow-rgb`);
    }
    return names;
}

function withDefaultScoreboardLayoutMeasurement(callback) {
    const root = document.documentElement;
    const previousVars = new Map();

    SCOREBOARD_TEXT_STYLE_TYPES.forEach(type => {
        getScoreboardStyleVarNames(type).forEach(name => {
            if (!previousVars.has(name)) {
                previousVars.set(name, {
                    value: root.style.getPropertyValue(name),
                    priority: root.style.getPropertyPriority(name)
                });
            }
        });
    });

    root.classList.add('layout-fit-measure-defaults');
    SCOREBOARD_TEXT_STYLE_TYPES.forEach(type => {
        setScoreboardStyleVars(type, type.defaults);
    });

    try {
        return callback();
    } finally {
        previousVars.forEach((prev, name) => {
            if (prev.value) root.style.setProperty(name, prev.value, prev.priority);
            else root.style.removeProperty(name);
        });
        root.classList.remove('layout-fit-measure-defaults');
    }
}

function applyScoreboardTextStyles(styles = scoreboardTextStyles) {
    scoreboardTextStyles = normalizeScoreboardTextStyles(styles);
    SCOREBOARD_TEXT_STYLE_TYPES.forEach(type => {
        setScoreboardStyleVars(type, scoreboardTextStyles[type.key]);
    });
}

function isAppearancePanelOpen() {
    return !!document.getElementById('appearance-overlay')?.classList.contains('active');
}

function fillAppearanceFontOptions(selectedFont) {
    const select = document.getElementById('style-font-family');
    if (!select) return;
    const fonts = normalizeSystemFonts(systemFonts);
    select.innerHTML = '';
    fonts.forEach(font => {
        const option = document.createElement('option');
        option.value = font;
        option.textContent = font;
        option.style.fontFamily = cssFontFamily(font);
        select.appendChild(option);
    });
    const cleaned = cleanFontFamilyName(selectedFont);
    if (!fonts.some(font => font.toLowerCase() === cleaned.toLowerCase())) {
        const option = document.createElement('option');
        option.value = cleaned;
        option.textContent = cleaned;
        select.appendChild(option);
    }
    select.value = cleaned;
}

function renderAppearanceScopeTabs() {
    const title = document.getElementById('appearance-title');
    const subtitle = document.querySelector('.appearance-subtitle');
    if (title) title.textContent = appearanceScope === 'kill' ? '击杀展示页外观' : '记分板外观';
    if (subtitle) subtitle.textContent = appearanceScope === 'kill'
        ? '只影响 OBS/直播伴侣展示页'
        : '只影响上方红蓝记分板文字';

    const tabs = document.getElementById('appearance-scope-tabs');
    if (!tabs) return;
    const scopes = [
        { key: 'scoreboard', label: '主计分板' },
        { key: 'kill', label: '击杀展示页' }
    ];
    tabs.innerHTML = '';
    scopes.forEach(scope => {
        const btn = document.createElement('button');
        btn.type = 'button';
        btn.id = scope.key === 'kill' ? 'appearance-scope-kill' : 'appearance-scope-scoreboard';
        btn.className = `appearance-scope-tab${appearanceScope === scope.key ? ' active' : ''}`;
        btn.textContent = scope.label;
        btn.addEventListener('click', () => {
            appearanceScope = scope.key;
            renderAppearanceScopeTabs();
            renderAppearanceStyleList();
            renderKillLayoutEditor();
            syncAppearanceEditorFromActiveStyle();
        });
        tabs.appendChild(btn);
    });
}

function renderAppearanceStyleList() {
    const list = document.getElementById('appearance-style-list');
    if (!list) return;
    list.innerHTML = '';
    const activeKey = getCurrentAppearanceStyleKey();
    getCurrentAppearanceStyleTypes().forEach(type => {
        const btn = document.createElement('button');
        btn.type = 'button';
        btn.className = `appearance-style-item${type.key === activeKey ? ' active' : ''}`;
        btn.textContent = type.label;
        btn.addEventListener('click', () => {
            setCurrentAppearanceStyleKey(type.key);
            renderAppearanceStyleList();
            syncAppearanceEditorFromActiveStyle();
        });
        list.appendChild(btn);
    });
}

function renderKillLayoutEditor() {
    const section = document.getElementById('kill-layout-section');
    if (!section) return;
    section.hidden = appearanceScope !== 'kill';
    section.innerHTML = '';
    if (appearanceScope !== 'kill') return;

    const layout = normalizeKillDisplayLayout(killDisplaySettings.layout);
    const title = document.createElement('div');
    title.className = 'kill-layout-title';
    title.textContent = '展示页空间参数';
    section.appendChild(title);

    const showDeathToggle = document.createElement('label');
    showDeathToggle.className = 'kill-layout-toggle';
    showDeathToggle.innerHTML = `
        <input id="kill-layout-showDeathNumber" type="checkbox" ${layout.showDeathNumber === 1 ? 'checked' : ''}>
        <span>显示死亡次数</span>
    `;
    section.appendChild(showDeathToggle);

    const grid = document.createElement('div');
    grid.className = 'kill-layout-grid';
    KILL_DISPLAY_LAYOUT_FIELDS.filter(field => !field.hidden).forEach(field => {
        const value = layout[field.key];
        const item = document.createElement('label');
        item.className = 'kill-layout-field';
        item.innerHTML = `
            <span>${field.label}</span>
            <div class="style-range-row">
                <input id="kill-layout-${field.key}" type="range" min="${field.min}" max="${field.max}" step="1" value="${value}">
                <input id="kill-layout-${field.key}-value" type="number" min="${field.min}" max="${field.max}" step="1" value="${value}">
                <em>${field.unit}</em>
            </div>
        `;
        grid.appendChild(item);
    });
    section.appendChild(grid);

    document.getElementById('kill-layout-showDeathNumber')?.addEventListener('change', (event) => {
        setKillDisplayShowDeathNumber(event.target.checked);
    });

    KILL_DISPLAY_LAYOUT_FIELDS.forEach(field => {
        const range = document.getElementById(`kill-layout-${field.key}`);
        const input = document.getElementById(`kill-layout-${field.key}-value`);
        const handle = (source) => {
            const value = clampNumber(source.value, field.min, field.max, KILL_DISPLAY_LAYOUT_DEFAULTS[field.key]);
            if (range) range.value = value;
            if (input) input.value = value;
            killDisplaySettings.layout[field.key] = value;
            queueKillDisplaySettingsSync();
        };
        range?.addEventListener('input', () => handle(range));
        input?.addEventListener('change', () => handle(input));
    });
}

function setPairedInputValue(primaryId, valueId, value) {
    const primary = document.getElementById(primaryId);
    const mirror = document.getElementById(valueId);
    if (primary) primary.value = value;
    if (mirror) mirror.value = value;
}

function setPairedColorValue(colorId, textId, value) {
    const color = normalizeHexColor(value);
    const colorInput = document.getElementById(colorId);
    const textInput = document.getElementById(textId);
    if (colorInput) colorInput.value = color;
    if (textInput) textInput.value = color;
}

function syncAppearanceEditorFromActiveStyle() {
    const type = getCurrentAppearanceStyleType(getCurrentAppearanceStyleKey());
    const styles = getCurrentAppearanceStyles();
    const style = appearanceScope === 'kill'
        ? normalizeKillDisplayTextStyle(type.key, styles[type.key])
        : normalizeScoreboardTextStyle(type.key, styles[type.key]);
    fillAppearanceFontOptions(style.fontFamily);

    const fontMax = appearanceScope === 'kill' ? 76 : 48;
    const strokeMax = appearanceScope === 'kill' ? 8 : 4;
    const glowMax = appearanceScope === 'kill' ? 36 : 24;
    ['style-font-size', 'style-font-size-value'].forEach(id => document.getElementById(id)?.setAttribute('max', String(fontMax)));
    ['style-stroke-width', 'style-stroke-width-value'].forEach(id => document.getElementById(id)?.setAttribute('max', String(strokeMax)));
    ['style-glow', 'style-glow-value'].forEach(id => document.getElementById(id)?.setAttribute('max', String(glowMax)));

    setPairedInputValue('style-font-size', 'style-font-size-value', style.fontSize);
    setPairedInputValue('style-stroke-width', 'style-stroke-width-value', style.strokeWidth);
    setPairedInputValue('style-glow', 'style-glow-value', style.glow);
    setPairedColorValue('style-color', 'style-color-text', style.color);
    setPairedColorValue('style-stroke-color', 'style-stroke-color-text', style.strokeColor);

    const colorMode = document.getElementById('style-color-mode');
    const colorModeRow = colorMode?.closest('.style-field');
    if (colorMode) {
        colorMode.value = type.allowTeamColor ? style.colorMode : 'custom';
        colorMode.disabled = !type.allowTeamColor;
    }
    colorModeRow?.classList.toggle('disabled', !type.allowTeamColor);

    const colorDisabled = type.allowTeamColor && style.colorMode === 'team';
    const colorRow = document.getElementById('style-color')?.closest('.style-field');
    colorRow?.classList.toggle('disabled', colorDisabled);
    document.getElementById('style-color')?.toggleAttribute('disabled', colorDisabled);
    document.getElementById('style-color-text')?.toggleAttribute('disabled', colorDisabled);
}

function readAppearanceNumber(rangeId, inputId, min, max, fallback) {
    const textValue = document.getElementById(inputId)?.value;
    const rangeValue = document.getElementById(rangeId)?.value;
    return clampNumber(textValue !== '' ? textValue : rangeValue, min, max, fallback);
}

function readAppearanceColor(colorId, textId, fallback) {
    const textValue = document.getElementById(textId)?.value;
    const colorValue = document.getElementById(colorId)?.value;
    return normalizeHexColor(textValue || colorValue, fallback);
}

function updateActiveStyleFromEditor(changedId = '') {
    const type = getCurrentAppearanceStyleType(getCurrentAppearanceStyleKey());
    const styles = getCurrentAppearanceStyles();
    const current = appearanceScope === 'kill'
        ? normalizeKillDisplayTextStyle(type.key, styles[type.key])
        : normalizeScoreboardTextStyle(type.key, styles[type.key]);
    if (changedId === 'style-color') {
        const text = document.getElementById('style-color-text');
        if (text) text.value = normalizeHexColor(document.getElementById('style-color')?.value, current.color);
    }
    if (changedId === 'style-stroke-color') {
        const text = document.getElementById('style-stroke-color-text');
        if (text) text.value = normalizeHexColor(document.getElementById('style-stroke-color')?.value, current.strokeColor);
    }

    const next = {
        fontFamily: cleanFontFamilyName(document.getElementById('style-font-family')?.value, current.fontFamily),
        fontSize: readAppearanceNumber('style-font-size', 'style-font-size-value', 10, appearanceScope === 'kill' ? 76 : 48, current.fontSize),
        colorMode: type.allowTeamColor ? (document.getElementById('style-color-mode')?.value === 'team' ? 'team' : 'custom') : 'custom',
        color: readAppearanceColor('style-color', 'style-color-text', current.color),
        strokeColor: readAppearanceColor('style-stroke-color', 'style-stroke-color-text', current.strokeColor),
        strokeWidth: readAppearanceNumber('style-stroke-width', 'style-stroke-width-value', 0, appearanceScope === 'kill' ? 8 : 4, current.strokeWidth),
        glow: readAppearanceNumber('style-glow', 'style-glow-value', 0, appearanceScope === 'kill' ? 36 : 24, current.glow)
    };
    if (appearanceScope === 'kill') {
        next.letterSpacing = clampNumber(current.letterSpacing, -4, 16, 0);
    }
    if (appearanceScope === 'kill') {
        killDisplaySettings.textStyles[type.key] = normalizeKillDisplayTextStyle(type.key, next);
        applyKillDisplaySettings(killDisplaySettings);
    } else {
        scoreboardTextStyles[type.key] = normalizeScoreboardTextStyle(type.key, next);
        applyScoreboardTextStyles(scoreboardTextStyles);
    }
    syncAppearanceEditorFromActiveStyle();
    if (appearanceScope === 'kill') queueKillDisplaySettingsSync();
    else queueScoreboardTextStyleSync();
}

function queueScoreboardTextStyleSync() {
    if (isSyncingFromServer) return;
    clearTimeout(scoreboardStyleSyncTimer);
    scoreboardStyleSyncTimer = setTimeout(() => {
        if (window.chrome?.webview) {
            window.chrome.webview.postMessage({
                action: 'cmd_set_scoreboard_text_styles',
                styles: scoreboardTextStyles
            });
        }
    }, 160);
}

function queueKillDisplaySettingsSync() {
    if (isSyncingFromServer) return;
    clearTimeout(killDisplaySettingsSyncTimer);
    killDisplaySettingsSyncTimer = setTimeout(() => {
        if (window.chrome?.webview) {
            window.chrome.webview.postMessage({
                action: 'cmd_set_kill_display_settings',
                settings: killDisplaySettings
            });
        }
    }, 160);
}

function setKillDisplayShowDeathNumber(enabled) {
    killDisplaySettings = normalizeKillDisplaySettings(killDisplaySettings);
    const nextValue = enabled ? 1 : 0;
    if (killDisplaySettings.layout.showDeathNumber === nextValue) {
        syncMainShowDeathToggle();
        return;
    }
    killDisplaySettings.layout.showDeathNumber = nextValue;
    applyKillDisplaySettings(killDisplaySettings);
    if (isAppearancePanelOpen() && appearanceScope === 'kill') {
        renderKillLayoutEditor();
    }
    queueKillDisplaySettingsSync();
}

function resetCurrentAppearanceStyle() {
    const type = getCurrentAppearanceStyleType(getCurrentAppearanceStyleKey());
    if (appearanceScope === 'kill') {
        killDisplaySettings.textStyles[type.key] = cloneScoreboardStyle(type.defaults);
        applyKillDisplaySettings(killDisplaySettings);
        queueKillDisplaySettingsSync();
    } else {
        scoreboardTextStyles[type.key] = cloneScoreboardStyle(type.defaults);
        applyScoreboardTextStyles(scoreboardTextStyles);
        queueScoreboardTextStyleSync();
    }
    syncAppearanceEditorFromActiveStyle();
}

function resetAllAppearanceSettings() {
    if (appearanceScope === 'kill') {
        killDisplaySettings = getDefaultKillDisplaySettings();
        applyKillDisplaySettings(killDisplaySettings);
        renderKillLayoutEditor();
        queueKillDisplaySettingsSync();
    } else {
        scoreboardTextStyles = getDefaultScoreboardTextStyles();
        applyScoreboardTextStyles(scoreboardTextStyles);
        queueScoreboardTextStyleSync();
    }
    renderAppearanceStyleList();
    syncAppearanceEditorFromActiveStyle();
}

function resetCurrentScoreboardStyle() {
    const previousScope = appearanceScope;
    appearanceScope = 'scoreboard';
    resetCurrentAppearanceStyle();
    appearanceScope = previousScope;
}

function resetAllScoreboardStyles() {
    const previousScope = appearanceScope;
    appearanceScope = 'scoreboard';
    resetAllAppearanceSettings();
    appearanceScope = previousScope;
}

function setAppearancePanelWindowExpanded(expanded) {
    if (!window.chrome?.webview) return;
    window.chrome.webview.postMessage({
        action: 'cmd_set_appearance_panel_open',
        open: !!expanded
    });
}

function openAppearancePanel() {
    const overlay = document.getElementById('appearance-overlay');
    if (!overlay) return;
    renderAppearanceScopeTabs();
    renderAppearanceStyleList();
    renderKillLayoutEditor();
    syncAppearanceEditorFromActiveStyle();
    overlay.classList.add('active');
    overlay.setAttribute('aria-hidden', 'false');
    document.documentElement.classList.add('appearance-open');
    setAppearancePanelWindowExpanded(true);
    scheduleLayoutFit(false, 'appearance-open');
}

function closeAppearancePanel() {
    const overlay = document.getElementById('appearance-overlay');
    if (!overlay) return;
    overlay.classList.remove('active');
    overlay.setAttribute('aria-hidden', 'true');
    document.documentElement.classList.remove('appearance-open');
    setAppearancePanelWindowExpanded(false);
    scheduleLayoutFit(false, 'appearance-close');
}

scoreboardTextStyles = getDefaultScoreboardTextStyles();
killDisplaySettings = getDefaultKillDisplaySettings();
systemFonts = normalizeSystemFonts(systemFonts);
applyScoreboardTextStyles(scoreboardTextStyles);
applyKillDisplaySettings(killDisplaySettings);

function getRandomGroupRowData(row) {
    const nameElem = row.querySelector('.name-input');
    const name = (nameElem?.value || '').trim();
    return {
        name,
        inputError: !!nameElem?.classList.contains('input-error'),
        errorMsg: nameElem?.getAttribute('data-error-msg') || '',
        kills: row.querySelector('.stat-kill')?.value || '0',
        deaths: row.querySelector('.stat-death')?.value || '0',
        akCount: row.querySelector('.stat-ak')?.value || '-'
    };
}

function setRandomGroupRowData(row, data) {
    const nameElem = row.querySelector('.name-input');
    if (nameElem) {
        nameElem.value = data?.name || '';
        nameElem.classList.toggle('input-error', !!data?.inputError);
        if (data?.errorMsg) nameElem.setAttribute('data-error-msg', data.errorMsg);
        else nameElem.removeAttribute('data-error-msg');
    }
    row.querySelector('.stat-kill').value = data?.kills || '0';
    row.querySelector('.stat-death').value = data?.deaths || '0';
    row.querySelector('.stat-ak').value = data?.akCount || '-';
}

function shuffleInPlace(items) {
    for (let i = items.length - 1; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        [items[i], items[j]] = [items[j], items[i]];
    }
    return items;
}

function resetRandomGroupTransientUi() {
    document.querySelectorAll('.popover').forEach(p => p.classList.remove('active'));
    clearPendingAliasPopoverLock();
    draggedRow = null;
    ctrlSwapState.active = false;
    ctrlSwapState.sourceRow = null;
    ctrlSwapState.targetRow = null;
    ctrlSwapState.currentIndex = -1;
    document.querySelectorAll('.player-row').forEach(row => {
        row.draggable = false;
        row.classList.remove('active-row', 'dragging', 'drag-over', 'drag-source', 'drag-target');
    });
}

function cloneRandomRowData(data) {
    return {
        seatNumber: data?.seatNumber || '',
        name: data?.name || '',
        inputError: !!data?.inputError,
        errorMsg: data?.errorMsg || '',
        kills: data?.kills || '0',
        deaths: data?.deaths || '0',
        akCount: data?.akCount || '-'
    };
}

function makeRandomRowData(name, seatNumber = '') {
    return {
        seatNumber,
        name: name || '',
        inputError: false,
        errorMsg: '',
        kills: '0',
        deaths: '0',
        akCount: '-'
    };
}

function getRandomParticipantLabel(data, fallback) {
    const name = (data?.name || '').trim();
    if (name) return name;
    const seat = (data?.seatNumber || '').trim();
    return seat ? `位置${seat}` : fallback;
}

function getAllLibraryMainNames() {
    const names = new Set([...Object.keys(savedDB || {}), ...Object.keys(playerDB || {})]);
    return Array.from(names)
        .map(name => String(name || '').trim())
        .filter(Boolean)
        .sort((a, b) => a.localeCompare(b, 'zh-Hans-CN'));
}

function normalizeRandomRosterLines(raw) {
    const text = String(raw || '').replace(/\r/g, '');
    if (!text.trim()) return [];
    const lines = text.split('\n');
    while (lines.length > 0 && lines[lines.length - 1].trim() === '') lines.pop();
    return lines;
}

function getRandomRosterLines() {
    const input = document.getElementById('random-roster-input');
    return normalizeRandomRosterLines(input?.value || '');
}

function getRandomSeatLabel(row, fallbackIndex) {
    return getPickLabelForRow(row) || String(fallbackIndex + 1);
}

function getRandomSeatSortValue(seatLabel, fallbackIndex) {
    const n = Number.parseInt(seatLabel, 10);
    return Number.isFinite(n) ? n : fallbackIndex + 1;
}

function getRandomPlaceholderSeatNumber(seatLabel, fallbackIndex) {
    const normalized = normalizePickChoice(seatLabel);
    if (normalized === 'x选') return '7';
    if (normalized === 'h选') return '8';

    const n = Number.parseInt(seatLabel, 10);
    return Number.isFinite(n) ? String(n) : String(fallbackIndex + 1);
}

function makeRandomSeatPlaceholder(seatNumber) {
    return `[编号${seatNumber}]`;
}

function isRandomSeatPlaceholderName(name) {
    return /^\[编号\d+\]$/.test(String(name || '').trim());
}

function normalizeRandomRosterSourceName(name) {
    const clean = String(name || '').trim();
    return isRandomSeatPlaceholderName(clean) ? '' : clean;
}

function getRandomApplyName(displayName, seatNumber, isPlaceholder = false) {
    const clean = String(displayName || '').trim();
    if (!clean || isPlaceholder || isRandomSeatPlaceholderName(clean)) return '';

    const duplicateSuffix = clean.match(/^(.*)\[编号\d+\]$/);
    if (duplicateSuffix && duplicateSuffix[1].trim()) return duplicateSuffix[1].trim();

    return clean;
}

function buildUniqueRandomRosterItems(rawItems) {
    const prepared = rawItems.map((item, idx) => {
        const seatNumber = String(item.seatNumber || idx + 1).trim() || String(idx + 1);
        const rawName = normalizeRandomRosterSourceName(item.name);
        const isPlaceholder = rawName === '';
        const baseName = rawName || makeRandomSeatPlaceholder(seatNumber);
        return { rawName, baseName, seatNumber, isPlaceholder };
    });
    const counts = new Map();
    prepared.forEach(item => counts.set(item.baseName, (counts.get(item.baseName) || 0) + 1));
    return prepared.map(item => {
        const duplicated = !item.isPlaceholder && (counts.get(item.baseName) || 0) > 1;
        const displayName = duplicated ? `${item.baseName}[编号${item.seatNumber}]` : item.baseName;
        return { ...item, displayName, duplicated };
    });
}

function buildDefaultRandomRosterLinesFromSeats() {
    const rows = Array.from(document.querySelectorAll('.player-row')).map((row, idx) => {
        const seatNumber = getRandomPlaceholderSeatNumber(getRandomSeatLabel(row, idx), idx);
        const name = normalizeRandomRosterSourceName(row.querySelector('.name-input')?.value || '');
        return {
            name,
            seatNumber,
            sortValue: getRandomSeatSortValue(seatNumber, idx),
            originalIndex: idx
        };
    });
    rows.sort((a, b) => (a.sortValue - b.sortValue) || (a.originalIndex - b.originalIndex));
    return buildUniqueRandomRosterItems(rows).map(item => item.displayName);
}

function ensureRandomRosterDefaultFromSeats() {
    const input = document.getElementById('random-roster-input');
    if (!input || input.value.trim()) return false;
    const lines = buildDefaultRandomRosterLinesFromSeats();
    if (lines.length === 0) return false;
    input.value = lines.join('\n');
    randomToolState.fixedIds.clear();
    randomToolState.fixedOrder = [];
    invalidateRandomToolResult('已默认载入当前 8 个座位，可以随机分组或者抽签。');
    return true;
}

function getRandomRosterLineRange(textarea) {
    const value = textarea?.value || '';
    const caret = textarea?.selectionStart || 0;
    const start = value.lastIndexOf('\n', Math.max(0, caret - 1)) + 1;
    let end = value.indexOf('\n', caret);
    if (end < 0) end = value.length;
    return { start, end, text: value.slice(start, end), index: value.slice(0, start).split('\n').length - 1 };
}

function getRosterRandomParticipants() {
    const lines = getRandomRosterLines();
    const rosterItems = buildUniqueRandomRosterItems(lines.map((line, idx) => ({
        name: line,
        seatNumber: String(idx + 1)
    })));
    return rosterItems.map((item, idx) => {
        return {
            id: `roster:${idx}:${item.displayName}`,
            source: 'roster',
            label: item.displayName,
            isPlaceholder: item.isPlaceholder,
            meta: item.duplicated ? `重复名/编号${item.seatNumber}` : (item.isPlaceholder ? '空名字' : '名单'),
            rowData: makeRandomRowData(getRandomApplyName(item.displayName, item.seatNumber, item.isPlaceholder), item.seatNumber)
        };
    });
}

function getRandomToolParticipants() {
    return getRosterRandomParticipants();
}

function invalidateRandomToolResult(message = '输入名单后，可以随机分组或者抽签。') {
    randomToolState.lastResult = null;
    const result = document.getElementById('random-result');
    if (result) result.innerHTML = `<div class="random-empty">${escapeHtml(message)}</div>`;
}

function updateRandomRosterSuggestions() {
    const textarea = document.getElementById('random-roster-input');
    const box = document.getElementById('random-roster-suggestions');
    if (!textarea || !box) return;
    const range = getRandomRosterLineRange(textarea);
    randomToolState.activeSuggestLine = range.index;
    const query = range.text.trim();
    if (!query) {
        box.classList.remove('active');
        box.innerHTML = '';
        return;
    }
    const names = getAllLibraryMainNames()
        .filter(name => name.includes(query))
        .slice(0, 8);
    if (names.length === 0) {
        box.classList.remove('active');
        box.innerHTML = '';
        return;
    }
    box.innerHTML = names.map(name => `<div class="random-roster-suggestion" data-name="${escapeHtml(name)}">${escapeHtml(name)}</div>`).join('');
    box.classList.add('active');
    box.querySelectorAll('.random-roster-suggestion').forEach(item => {
        item.addEventListener('mousedown', (e) => {
            e.preventDefault();
            applyRandomRosterSuggestion(item.getAttribute('data-name') || '');
        });
    });
}

function applyRandomRosterSuggestion(name) {
    const textarea = document.getElementById('random-roster-input');
    if (!textarea || !name) return;
    const range = getRandomRosterLineRange(textarea);
    textarea.value = textarea.value.slice(0, range.start) + name + textarea.value.slice(range.end);
    const caret = range.start + name.length;
    textarea.focus();
    textarea.setSelectionRange(caret, caret);
    document.getElementById('random-roster-suggestions')?.classList.remove('active');
    invalidateRandomToolResult('名单已更新，请重新随机分组或者抽签。');
    renderRandomParticipants();
}

function handleRandomRosterInput() {
    invalidateRandomToolResult('名单已更新，请重新随机分组或者抽签。');
    renderRandomParticipants();
    updateRandomRosterSuggestions();
}

function syncRandomFixedState(participants) {
    const ids = new Set(participants.map(p => p.id));
    randomToolState.fixedOrder = randomToolState.fixedOrder.filter(id => ids.has(id));
    randomToolState.fixedIds = new Set(randomToolState.fixedOrder);
}

function toggleRandomFixed(id) {
    if (randomToolState.fixedIds.has(id)) {
        randomToolState.fixedIds.delete(id);
        randomToolState.fixedOrder = randomToolState.fixedOrder.filter(item => item !== id);
    } else {
        randomToolState.fixedIds.add(id);
        randomToolState.fixedOrder.push(id);
    }
    invalidateRandomToolResult('固定人员已更新，请重新随机分组。');
    renderRandomParticipants();
}

function renderRandomParticipants() {
    const list = document.getElementById('random-participant-list');
    if (!list) return;
    const participants = getRandomToolParticipants();
    const countEl = document.getElementById('random-participant-count');
    if (countEl) countEl.textContent = String(participants.length);
    syncRandomFixedState(participants);
    if (participants.length === 0) {
        list.innerHTML = '<div class="random-empty">暂无参与人员。</div>';
        return;
    }
    list.innerHTML = participants.map(p => {
        const fixed = randomToolState.fixedIds.has(p.id);
        return `<div class="random-participant-row">
            <span class="random-person-name" title="${escapeHtml(p.label)}">${escapeHtml(p.label)}</span>
            <span class="random-person-meta">${escapeHtml(p.meta || '')}</span>
            <button class="random-fixed-btn${fixed ? ' active' : ''}" data-id="${escapeHtml(p.id)}">${fixed ? '已固定' : '固定'}</button>
        </div>`;
    }).join('');
    list.querySelectorAll('.random-fixed-btn').forEach(btn => {
        btn.addEventListener('click', () => toggleRandomFixed(btn.getAttribute('data-id') || ''));
    });
}

function getDefaultRandomGroupSizes(count) {
    const larger = Math.ceil(count / 2);
    const smaller = Math.floor(count / 2);
    if (count % 2 === 1 && Math.random() < 0.5) return [smaller, larger];
    return [larger, smaller];
}

function buildBalancedRandomGroupSizes(count, groupCount) {
    const base = Math.floor(count / groupCount);
    const sizes = Array(groupCount).fill(base);
    const extraIndexes = Array.from({ length: groupCount }, (_, idx) => idx);
    shuffleInPlace(extraIndexes);
    for (let i = 0; i < count % groupCount; i++) {
        sizes[extraIndexes[i]] += 1;
    }
    return sizes;
}

function parseRandomGroupSizes(count) {
    const input = document.getElementById('random-group-sizes');
    const raw = (input?.value || '2').trim() || '2';
    const groupCount = Number(raw);
    if (!Number.isInteger(groupCount) || groupCount <= 0) {
        return { error: '分成几组必须是大于 0 的整数。' };
    }
    if (groupCount > count) {
        return { error: `当前只有 ${count} 人，不能分成 ${groupCount} 组。` };
    }
    if (groupCount === 2 && count > 0) return { sizes: getDefaultRandomGroupSizes(count) };
    return { sizes: buildBalancedRandomGroupSizes(count, groupCount) };
}

function getRandomGroupCountFromSizes(sizes) {
    return Array.isArray(sizes) ? sizes.length : 0;
}

function buildRandomSlotOrder(sizes) {
    const slots = [];
    const maxSize = Math.max(...sizes);
    for (let pos = 0; pos < maxSize; pos++) {
        for (let groupIdx = 0; groupIdx < sizes.length; groupIdx++) {
            if (pos < sizes[groupIdx]) slots.push(groupIdx);
        }
    }
    return slots;
}

function buildRandomGrouping(participants, sizes) {
    const byId = new Map(participants.map(p => [p.id, p]));
    const fixed = randomToolState.fixedOrder.map(id => byId.get(id)).filter(Boolean);
    const capacity = sizes.reduce((sum, n) => sum + n, 0);
    if (fixed.length > capacity) {
        return { error: `固定人员 ${fixed.length} 人，超过当前分组容量 ${capacity} 人。` };
    }
    const fixedIds = new Set(fixed.map(p => p.id));
    const randomPool = participants.filter(p => !fixedIds.has(p.id));
    shuffleInPlace(randomPool);

    if (fixed.length === 0) {
        const groups = [];
        let cursor = 0;
        sizes.forEach(size => {
            groups.push(randomPool.slice(cursor, cursor + size));
            cursor += size;
        });
        return {
            type: 'groups',
            groups,
            leftover: randomPool.slice(cursor),
            sizes,
            fixedCount: 0
        };
    }

    const slots = buildRandomSlotOrder(sizes);
    const groups = sizes.map(() => []);
    const selected = [...fixed, ...randomPool.slice(0, capacity - fixed.length)];
    selected.forEach((participant, idx) => {
        const groupIdx = slots[idx];
        if (Number.isInteger(groupIdx)) groups[groupIdx].push(participant);
    });
    return {
        type: 'groups',
        groups,
        leftover: randomPool.slice(Math.max(0, capacity - fixed.length)),
        sizes,
        fixedCount: fixed.length
    };
}

function formatRandomParticipant(participant) {
    return participant?.label || participant?.rowData?.name || '空位';
}

function formatRandomResultText(result) {
    if (!result) return '';
    if (result.type === 'draw') {
        return `抽签：${(result.picks || []).map(formatRandomParticipant).join('、') || '空'}`;
    }
    if (!Array.isArray(result.groups)) return '';
    const lines = [];
    result.groups.forEach((group, idx) => {
        lines.push(`${idx + 1}组：${group.map(formatRandomParticipant).join('、') || '空'}`);
    });
    if (result.leftover?.length) {
        lines.push(`候补：${result.leftover.map(formatRandomParticipant).join('、')}`);
    }
    return lines.join('\n');
}

function renderRandomResult(result) {
    const box = document.getElementById('random-result');
    if (!box) return;
    if (!result) {
        box.innerHTML = '<div class="random-empty">输入名单后，可以随机分组或者抽签。</div>';
        return;
    }
    if (result.error) {
        box.innerHTML = `<div class="random-error">${escapeHtml(result.error)}</div>`;
        return;
    }
    if (result.type === 'draw') {
        box.innerHTML = `<div class="random-draw-result">抽签结果：<b>${escapeHtml((result.picks || []).map(formatRandomParticipant).join('、') || '空')}</b></div>`;
        return;
    }
    const groupsHtml = result.groups.map((group, idx) => `
        ${idx === 0 ? '<div class="random-success">已生成随机分组。</div>' : ''}
        <div class="random-group">
            <div class="random-group-title">${idx + 1}组 (${group.length}人)</div>
            <div class="random-group-list">
                ${group.map(p => `<span class="random-chip" title="${escapeHtml(formatRandomParticipant(p))}">${escapeHtml(formatRandomParticipant(p))}</span>`).join('') || '<span class="random-empty">空</span>'}
            </div>
        </div>
    `).join('');
    const leftoverHtml = result.leftover?.length ? `
        <div class="random-group">
            <div class="random-group-title">候补/未分配 (${result.leftover.length}人)</div>
            <div class="random-group-list">
                ${result.leftover.map(p => `<span class="random-chip" title="${escapeHtml(formatRandomParticipant(p))}">${escapeHtml(formatRandomParticipant(p))}</span>`).join('')}
            </div>
        </div>` : '';
    box.innerHTML = groupsHtml + leftoverHtml;
}

function requestWebWindowClipboardShot() {
    if (!window.chrome?.webview) return;
    window.requestAnimationFrame(() => {
        setTimeout(() => {
            window.chrome.webview.postMessage({ action: 'cmd_copy_web_window_to_clipboard' });
        }, 80);
    });
}

function runRandomToolGrouping(options = {}) {
    const notify = options.notify !== false;
    const returnError = options.returnError === true;
    if (isMonitoring) {
        const result = { error: '请先停止监控，再进行随机分组。' };
        if (notify) showAlert(`随机分组失败：${result.error}`);
        return returnError ? result : null;
    }

    const participants = getRandomToolParticipants();
    syncRandomFixedState(participants);
    if (participants.length === 0) {
        const result = { error: '当前没有可随机的人员。' };
        randomToolState.lastResult = result;
        renderRandomResult(result);
        if (notify) showAlert(`随机分组失败：${result.error}`);
        return returnError ? result : null;
    }

    const parsed = parseRandomGroupSizes(participants.length);
    if (parsed.error) {
        const result = { error: parsed.error };
        randomToolState.lastResult = result;
        renderRandomResult(result);
        if (notify) showAlert(`随机分组失败：${result.error}`);
        return returnError ? result : null;
    }

    const result = buildRandomGrouping(participants, parsed.sizes);
    randomToolState.lastResult = result;
    renderRandomResult(result);
    if (result.error) {
        if (notify) showAlert(`随机分组失败：${result.error}`);
        return returnError ? result : null;
    }
    if (notify) {
        const assignedCount = result.groups.reduce((sum, group) => sum + group.length, 0);
        const leftoverCount = result.leftover?.length || 0;
        const suffix = leftoverCount > 0 ? `，候补 ${leftoverCount} 人` : '';
        showAlert(
            `随机分组成功：已分成 ${getRandomGroupCountFromSizes(result.sizes)} 组，已分配 ${assignedCount} 人${suffix}。`,
            requestWebWindowClipboardShot
        );
    }
    return result;
}

function applyRandomResultToTeams() {
    if (isMonitoring) {
        showAlert('请先停止监控，再应用随机分组。');
        return;
    }
    const result = randomToolState.lastResult && randomToolState.lastResult.type === 'groups' && !randomToolState.lastResult.error
        ? randomToolState.lastResult
        : runRandomToolGrouping({ notify: false, returnError: true });
    if (!result) {
        showAlert('应用失败：当前没有可应用的随机分组结果。');
        return;
    }
    if (result.error) {
        showAlert(`应用失败：${result.error}`);
        return;
    }
    if (result.groups.length !== 2) {
        showAlert('应用失败：只有 2 组结果可以直接应用到红蓝座位。');
        return;
    }
    if (result.groups[0].length > 4 || result.groups[1].length > 4) {
        showAlert('应用失败：红蓝每队最多 4 个座位，请调整分成几组。');
        return;
    }

    resetRandomGroupTransientUi();
    const redRows = Array.from(document.querySelectorAll('#team-red .player-row'));
    const blueRows = Array.from(document.querySelectorAll('#team-blue .player-row'));
    const redPlayers = result.groups[0];
    const bluePlayers = result.groups[1];

    redRows.forEach((row, idx) => setRandomGroupRowData(row, redPlayers[idx]?.rowData || null));
    blueRows.forEach((row, idx) => setRandomGroupRowData(row, bluePlayers[idx]?.rowData || null));
    refreshPickLabels();

    triggerSync();
    showAlert(`应用成功：已写入红蓝座位，共 ${redPlayers.length + bluePlayers.length} 人。`);
}

function parseRandomDrawCount(maxCount) {
    const raw = (document.getElementById('random-draw-count')?.value || '1').trim();
    const count = Number(raw);
    if (!Number.isInteger(count) || count <= 0) {
        return { error: '抽签人数必须是大于 0 的整数。' };
    }
    if (count > maxCount) {
        return { error: `抽签人数 ${count} 人，但当前名单只有 ${maxCount} 人。` };
    }
    return { count };
}

function drawRandomParticipant() {
    const participants = getRandomToolParticipants();
    syncRandomFixedState(participants);
    if (participants.length === 0) {
        const result = { error: '当前没有可抽签的人员。' };
        randomToolState.lastResult = result;
        renderRandomResult(result);
        showAlert(`抽签失败：${result.error}`);
        return;
    }
    const parsed = parseRandomDrawCount(participants.length);
    if (parsed.error) {
        const result = { error: parsed.error };
        randomToolState.lastResult = result;
        renderRandomResult(result);
        showAlert(`抽签失败：${result.error}`);
        return;
    }
    const pool = [...participants];
    shuffleInPlace(pool);
    const result = { type: 'draw', picks: pool.slice(0, parsed.count) };
    randomToolState.lastResult = result;
    renderRandomResult(result);
    showAlert(`抽签成功：${result.picks.map(formatRandomParticipant).join('、')}。`, requestWebWindowClipboardShot);
}

function copyRandomResult() {
    const text = formatRandomResultText(randomToolState.lastResult);
    if (!text) {
        showAlert('复制失败：当前还没有可复制的随机结果。');
        return;
    }
    const notifyCopySuccess = () => showAlert('复制成功：随机结果已复制。');
    const notifyCopyFail = () => showAlert('复制失败：浏览器暂时无法写入剪贴板。');
    const fallbackCopy = () => {
        try {
            const area = document.createElement('textarea');
            area.value = text;
            area.style.position = 'fixed';
            area.style.left = '-9999px';
            document.body.appendChild(area);
            area.select();
            const ok = document.execCommand('copy');
            document.body.removeChild(area);
            if (ok) notifyCopySuccess();
            else notifyCopyFail();
        } catch (e) {
            notifyCopyFail();
        }
    };
    if (navigator.clipboard?.writeText) {
        navigator.clipboard.writeText(text).then(notifyCopySuccess).catch(fallbackCopy);
    } else {
        fallbackCopy();
    }
}

function resetRandomToolToInitialState(options = {}) {
    const notify = options.notify !== false;
    const input = document.getElementById('random-roster-input');
    if (input) {
        input.value = buildDefaultRandomRosterLinesFromSeats().join('\n');
    }

    const groupSizesInput = document.getElementById('random-group-sizes');
    if (groupSizesInput) groupSizesInput.value = '2';

    const drawCountInput = document.getElementById('random-draw-count');
    if (drawCountInput) drawCountInput.value = '1';

    randomToolState.fixedIds.clear();
    randomToolState.fixedOrder = [];
    randomToolState.lastResult = null;
    randomToolState.activeSuggestLine = -1;
    document.getElementById('random-roster-suggestions')?.classList.remove('active');
    renderRandomParticipants();
    invalidateRandomToolResult('已恢复到默认名单，可以随机分组或者抽签。');
    if (notify) showAlert('重置成功：随机工具已重新读取当前 8 个座位。');
}

function openRandomTool() {
    if (isMonitoring) {
        showAlert('请先停止监控，再打开随机分组。');
        return;
    }
    resetRandomGroupTransientUi();
    const overlay = document.getElementById('random-tool-overlay');
    if (!overlay) return;
    overlay.classList.add('active');
    overlay.setAttribute('aria-hidden', 'false');
    resetRandomToolToInitialState({ notify: false });
    updateRandomRosterSuggestions();
}

function closeRandomTool() {
    const overlay = document.getElementById('random-tool-overlay');
    if (!overlay) return;
    overlay.classList.remove('active');
    overlay.setAttribute('aria-hidden', 'true');
    document.getElementById('random-roster-suggestions')?.classList.remove('active');
}

function shouldPreserveNoAliasInput(inputElem, serverName) {
    if (!inputElem || inputElem.classList.contains('input-error')) return false;

    const currentName = inputElem.value.trim();
    const incomingName = String(serverName || '').trim();
    if (!currentName || incomingName) return false;

    const aliases = uniqueAliasArray([...(playerDB[currentName] || []), ...(savedDB[currentName] || [])]);
    return aliases.length === 0;
}

window.addEventListener('resize', () => {
    if (document.getElementById('more-controls-menu')?.classList.contains('active')) {
        positionMoreControlsMenu();
    }
});

document.getElementById('more-controls-menu')?.addEventListener('focusin', (event) => {
    event.target?.scrollIntoView?.({ block: 'nearest' });
});

const CLOUD_ROOM_NAMES = Object.freeze({
    'li-yong': '李永房',
    'wen-rou': '温柔房',
    '59': '59房'
});
const cloudRoomNameSegmenter = typeof Intl.Segmenter === 'function'
    ? new Intl.Segmenter('zh-CN', { granularity: 'grapheme' }) : null;

function normalizeCloudMatchState(value = {}) {
    const roomId = Object.prototype.hasOwnProperty.call(CLOUD_ROOM_NAMES, value.roomId)
        ? value.roomId : '';
    return {
        joined: value.joined === true && !!roomId,
        roomId,
        roomName: String(value.roomName || CLOUD_ROOM_NAMES[roomId] || ''),
        broadcasterName: String(value.broadcasterName || ''),
        configured: value.configured === true,
        connected: value.connected === true,
        connecting: value.connecting === true,
        reconnecting: value.reconnecting === true,
        joining: value.joining === true,
        registering: value.registering === true,
        renaming: value.renaming === true,
        leaving: value.leaving === true,
        lastError: String(value.lastError || ''),
        clientRevision: Math.max(0, Number(value.clientRevision || 0)),
        shouldPrompt: value.shouldPrompt === true
    };
}

function isCloudRoomBusy(state = cloudMatchState) {
    return !!(state?.joining || state?.registering || state?.renaming || state?.leaving);
}

function cloudRoomNameInfo(value) {
    const normalized = String(value || '').normalize('NFC').trim();
    const graphemeCount = cloudRoomNameSegmenter
        ? Array.from(cloudRoomNameSegmenter.segment(normalized)).length
        : Array.from(normalized).length;
    const forbidden = /[\p{Cc}\p{Cf}\p{Zl}\p{Zp}]/u.test(normalized);
    const visible = /[\p{L}\p{N}\p{P}\p{S}]/u.test(normalized);
    return {
        normalized,
        graphemeCount,
        valid: graphemeCount >= 1 && graphemeCount <= 32 && !forbidden && visible
    };
}

function isCloudRoomPanelOpen() {
    return document.getElementById('cloud-room-overlay')?.classList.contains('active') === true;
}

function setCloudRoomInlineError(message) {
    const error = document.getElementById('cloud-room-error');
    if (error) error.textContent = message || '';
}

function updateCloudRoomNameValidation() {
    const input = document.getElementById('cloud-room-name-input');
    const joinButton = document.getElementById('btn-cloud-room-join');
    const count = document.getElementById('cloud-room-name-count');
    if (!input || !joinButton) return false;
    const info = cloudRoomNameInfo(input.value);
    if (count) count.textContent = `${info.graphemeCount} / 32`;
    const busy = isCloudRoomBusy();
    joinButton.disabled = !cloudSelectedRoomId || !info.valid || !!busy;
    if (info.graphemeCount > 32) setCloudRoomInlineError('主播名称不能超过 32 个字符。');
    else if (info.normalized && !info.valid) setCloudRoomInlineError('主播名称包含不可用字符。');
    else if (!cloudMatchState?.lastError) setCloudRoomInlineError('');
    return info.valid;
}

function renderCloudRoomPanel() {
    if (!cloudMatchState) cloudMatchState = normalizeCloudMatchState();
    const state = cloudMatchState;
    const current = document.getElementById('cloud-room-current');
    const chooser = document.getElementById('cloud-room-first-run');
    const nameStage = document.getElementById('cloud-room-name-stage');
    const close = document.getElementById('btn-cloud-room-close');
    const joinedRequestedRoom = state.joined && cloudRoomChoosing && cloudRoomJoinTarget &&
        !state.joining && !state.registering && !state.lastError &&
        state.roomId === cloudRoomJoinTarget.roomId &&
        cloudRoomNameInfo(state.broadcasterName).normalized === cloudRoomJoinTarget.broadcasterName;
    if (joinedRequestedRoom) {
        cloudRoomFirstRun = false;
        cloudRoomChoosing = false;
        cloudSelectedRoomId = '';
        cloudRoomJoinTarget = null;
    }
    const showChooser = !state.joined || cloudRoomChoosing;
    if (close) close.hidden = cloudRoomFirstRun && !state.joined;
    if (current) current.hidden = !state.joined || showChooser;
    if (chooser) chooser.hidden = !showChooser;
    if (nameStage) nameStage.hidden = !showChooser || !cloudSelectedRoomId;

    const room = document.getElementById('cloud-room-current-room');
    if (room) room.textContent = state.joined
        ? `${state.roomName || CLOUD_ROOM_NAMES[state.roomId]} · ${state.broadcasterName}`
        : '未加入房间';
    const connection = document.getElementById('cloud-room-connection');
    if (connection) {
        const working = state.joining || state.registering || state.renaming || state.leaving;
        connection.textContent = working ? '处理中' : (state.connected ? '已连接' :
            (state.reconnecting ? '重连中' : (state.connecting ? '连接中' : '离线')));
        connection.dataset.state = state.connected ? 'online' :
            (working || state.connecting || state.reconnecting ? 'working' : 'offline');
    }

    const renameInput = document.getElementById('cloud-room-rename-input');
    if (renameInput && document.activeElement !== renameInput) {
        renameInput.value = state.broadcasterName;
    }
    if (renameInput) renameInput.disabled = isCloudRoomBusy(state);
    const renameButton = document.getElementById('btn-cloud-room-rename');
    const changeButton = document.getElementById('btn-cloud-room-change');
    const leaveButton = document.getElementById('btn-cloud-room-leave');
    const busy = isCloudRoomBusy(state);
    if (renameButton) {
        renameButton.disabled = busy;
        renameButton.textContent = state.renaming ? '修改中...' : '修改名称';
    }
    if (changeButton) changeButton.disabled = busy;
    if (leaveButton) {
        leaveButton.disabled = busy;
        leaveButton.textContent = state.leaving ? '退出中...' : '退出房间';
    }

    document.querySelectorAll('.cloud-room-option').forEach(option => {
        const selected = option.dataset.cloudRoomId === cloudSelectedRoomId;
        option.classList.toggle('selected', selected);
        option.setAttribute('aria-checked', selected ? 'true' : 'false');
        option.disabled = busy;
    });
    const selectedLabel = document.getElementById('cloud-room-selected');
    if (selectedLabel) selectedLabel.textContent = cloudSelectedRoomId
        ? `准备加入：${CLOUD_ROOM_NAMES[cloudSelectedRoomId]}` : '尚未选择房间';
    const joinButton = document.getElementById('btn-cloud-room-join');
    const joinBusy = state.joining || state.registering;
    if (joinButton) {
        joinButton.hidden = joinBusy;
        joinButton.textContent = joinBusy ? '加入中...' : '加入房间';
    }
    const cancelJoinButton = document.getElementById('btn-cloud-room-cancel-join');
    if (cancelJoinButton) {
        cancelJoinButton.hidden = !joinBusy;
        cancelJoinButton.disabled = !joinBusy;
        cancelJoinButton.textContent = '取消加入';
    }
    const backButton = document.getElementById('btn-cloud-room-back');
    if (backButton) backButton.disabled = busy;
    const nameInput = document.getElementById('cloud-room-name-input');
    if (nameInput) nameInput.disabled = busy;
    setCloudRoomInlineError(state.lastError);
    updateCloudRoomNameValidation();
}

function openCloudRoomPanel(firstRun = false) {
    const overlay = document.getElementById('cloud-room-overlay');
    if (!overlay) return;
    setMoreControlsOpen(false);
    cloudRoomFirstRun = firstRun && !cloudMatchState?.joined;
    cloudRoomChoosing = !cloudMatchState?.joined;
    cloudSelectedRoomId = '';
    cloudRoomJoinTarget = null;
    overlay.classList.add('active');
    overlay.setAttribute('aria-hidden', 'false');
    renderCloudRoomPanel();
}

function closeCloudRoomPanel(force = false) {
    if (cloudRoomFirstRun && !cloudMatchState?.joined && !force) return;
    const overlay = document.getElementById('cloud-room-overlay');
    if (!overlay) return;
    overlay.classList.remove('active');
    overlay.setAttribute('aria-hidden', 'true');
    cloudRoomFirstRun = false;
    cloudRoomChoosing = false;
    cloudSelectedRoomId = '';
    cloudRoomJoinTarget = null;
}

function restoreCloudRoomPromptFromState() {
    if (!cloudMatchState || cloudMatchState.joined || isCloudRoomBusy()) return;
    if (cloudMatchState.shouldPrompt && !isCloudRoomPanelOpen()) {
        openCloudRoomPanel(true);
    }
}

function sendCloudRoomCommand(action, payload = {}) {
    window.chrome?.webview?.postMessage({ action, ...payload });
}

function createDefaultKeyMappingSettings() {
    return {
        enabled: false,
        windowVisible: false,
        httpReady: false,
        lan: {
            role: 'standalone',
            running: false,
            connected: false,
            reconnecting: false,
            discovering: false,
            status: '',
            isAdmin: false,
            adminRequired: false,
            port: 18778,
            serverAddress: '',
            serverPairCode: '----',
            clientPairCode: '',
            serverId: '',
            remoteDeviceName: '',
            localAddresses: [],
            servers: [],
            teamSyncPending: false,
            teamSyncCanUndo: false,
            teamSyncSupported: true,
            teamSyncPushSupported: false,
            teamSyncSubscribed: false,
            teamSyncAutoReceive: false,
            teamSyncLastAutoResult: ''
        },
        slots: Array.from({ length: KEY_MAPPING_SLOT_COUNT }, (_, index) => ({
            index,
            vk: KEY_MAPPING_DEFAULT_VKS[index],
            label: KEY_MAPPING_DEFAULT_LABELS[index],
            color: '#00E5FF',
            opacity: 42
        }))
    };
}

function normalizeKeyColor(value) {
    const text = String(value || '').trim().toUpperCase();
    return /^#[0-9A-F]{6}$/.test(text) ? text : '#00E5FF';
}

function normalizeKeyMappingSettings(value = {}) {
    const defaults = createDefaultKeyMappingSettings();
    const incomingSlots = Array.isArray(value.slots) ? value.slots : [];
    const incomingLan = value.lan && typeof value.lan === 'object' ? value.lan : {};
    const role = ['standalone', 'server', 'client'].includes(incomingLan.role)
        ? incomingLan.role : 'standalone';
    return {
        enabled: value.enabled === true,
        windowVisible: value.windowVisible === true,
        httpReady: value.httpReady === true,
        lan: {
            ...defaults.lan,
            ...incomingLan,
            role,
            port: Math.max(1024, Math.min(65535, Number(incomingLan.port || 18778))),
            localAddresses: Array.isArray(incomingLan.localAddresses) ? incomingLan.localAddresses : [],
            servers: Array.isArray(incomingLan.servers) ? incomingLan.servers : []
        },
        slots: defaults.slots.map((slot, index) => {
            const incoming = incomingSlots[index];
            if (!incoming || typeof incoming !== 'object') return { ...slot };
            return {
                index,
                vk: Math.max(0, Math.min(254, Number(incoming.vk ?? slot.vk))),
                label: String(incoming.label ?? slot.label).slice(0, 16),
                color: normalizeKeyColor(incoming.color ?? slot.color),
                opacity: Math.max(0, Math.min(100, Number(incoming.opacity ?? 42)))
            };
        })
    };
}

function sendKeyLanCommand(action, payload = {}) {
    window.chrome?.webview?.postMessage({ action, ...payload });
}

function keyLanStatusText(lan) {
    if (lan.role === 'standalone') return lan.isAdmin ? '单机模式 · 管理员权限' : '单机模式 · 需要管理员权限读取 DNF';
    const labels = {
        starting: '正在启动',
        listening: '等待客户端连接',
        connecting: '正在连接',
        connected: `已连接${lan.remoteDeviceName ? ` · ${lan.remoteDeviceName}` : ''}`,
        reconnecting: '连接已断开，2 秒后重连',
        stopped: '未启动',
        bind_failed: '启动失败：端口可能被占用',
        socket_failed: '网络初始化失败',
        rejected_pair_code: '配对码错误',
        rejected_busy: '服务器忙',
        rejected_version: '协议版本不一致'
    };
    if (lan.discovering) return '正在搜索局域网服务器';
    return labels[lan.status] || (lan.role === 'server' ? '服务器未启动' : '客户端未连接');
}

function showKeyMappingAdminPrompt(message) {
    if (keyMappingAdminPromptVisible) return;
    keyMappingAdminPromptVisible = true;
    showConfirm(`${escapeHtml(message || 'DNF 内按键需要管理员权限。').replace(/\n/g, '<br>')}<br><br>` +
        '点击“管理员重启”保存配置并以管理员身份重启；取消后按键响应保持关闭。', (ok) => {
        keyMappingAdminPromptVisible = false;
        if (ok) {
            sendKeyLanCommand('cmd_restart_as_admin');
            return;
        }
        if (keyMappingSettings) {
            keyMappingSettings.enabled = false;
            sendKeyMappingSettings(true);
            renderKeyMappingPanel();
        }
    }, { okText: '管理员重启', cancelText: '取消' });
}

function renderKeyLanPanel() {
    if (!keyMappingSettings) return;
    const lan = keyMappingSettings.lan || createDefaultKeyMappingSettings().lan;
    const roleSelect = document.getElementById('key-lan-role');
    if (roleSelect) roleSelect.value = lan.role;

    const status = document.getElementById('key-lan-status');
    if (status) {
        status.textContent = keyLanStatusText(lan);
        status.dataset.state = lan.connected ? 'connected' : (lan.running || lan.reconnecting ? 'working' : 'idle');
    }

    const serverPanel = document.getElementById('key-lan-server-panel');
    const clientPanel = document.getElementById('key-lan-client-panel');
    if (serverPanel) serverPanel.hidden = lan.role !== 'server';
    if (clientPanel) clientPanel.hidden = lan.role !== 'client';

    if (lan.role === 'server') {
        document.getElementById('key-lan-local-addresses').textContent =
            lan.localAddresses.length ? lan.localAddresses.join(' / ') : '启动后显示';
        document.getElementById('key-lan-server-pair-code').textContent = lan.serverPairCode || '----';
        document.getElementById('key-lan-server-client').textContent = lan.connected
            ? `已连接：${lan.remoteDeviceName || '客户端'}` : '等待客户端';
        const port = document.getElementById('key-lan-server-port');
        if (document.activeElement !== port) port.value = String(lan.port);
        const toggle = document.getElementById('btn-key-lan-server-toggle');
        toggle.textContent = lan.running ? '停止服务器' : '启动服务器';
        toggle.classList.toggle('danger', lan.running);
    }

    if (lan.role === 'client') {
        const address = document.getElementById('key-lan-server-address');
        const pairCode = document.getElementById('key-lan-pair-code');
        const port = document.getElementById('key-lan-client-port');
        if (document.activeElement !== address) address.value = lan.serverAddress || '';
        if (document.activeElement !== pairCode) pairCode.value = lan.clientPairCode || '';
        if (document.activeElement !== port) port.value = String(lan.port);

        const list = document.getElementById('key-lan-server-list');
        const previousAddress = list.selectedOptions[0]?.dataset?.address || '';
        list.replaceChildren();
        if (!lan.servers.length) {
            const option = document.createElement('option');
            option.value = '';
            option.textContent = lan.discovering ? '正在搜索...' : '没有发现服务器，可手动输入 IP';
            list.appendChild(option);
        } else {
            lan.servers.forEach((server, index) => {
                const option = document.createElement('option');
                option.value = String(index);
                option.dataset.address = String(server.address || '');
                option.dataset.port = String(server.port || lan.port);
                option.textContent = `${server.name || '按键服务器'} · ${server.address}`;
                if (server.address === previousAddress || (!previousAddress && server.address === lan.serverAddress)) {
                    option.selected = true;
                }
                list.appendChild(option);
            });
        }
        const discover = document.getElementById('btn-key-lan-discover');
        discover.disabled = lan.discovering;
        discover.textContent = lan.discovering ? '搜索中...' : '搜索服务器';
        const toggle = document.getElementById('btn-key-lan-client-toggle');
        toggle.textContent = lan.running ? '断开连接' : '连接';
        toggle.classList.toggle('danger', lan.running);

    }

    const keyStatus = document.getElementById('key-lan-key-status');
    if (keyStatus) {
        if (lan.role === 'server') {
            keyStatus.textContent = lan.connected
                ? `正在接收 ${lan.remoteDeviceName || '客户端'} 的 14 位按键状态，使用服务器本机样式显示。`
                : '服务器启动后，将接收客户端的 14 位按键亮灭状态。';
        } else if (lan.role === 'client') {
            keyStatus.textContent = lan.connected
                ? '正在向服务器发送 14 位按键亮灭状态，不传输键名和样式。'
                : '连接服务器后发送本机按键位置状态。';
        } else {
            keyStatus.textContent = '单机模式只在本机响应和显示按键，不进行网络传输。';
        }
    }

    const autoWrap = document.getElementById('key-team-auto-wrap');
    const autoReceive = document.getElementById('team-sync-auto-receive');
    const allowWriteWrap = document.getElementById('key-team-allow-write-wrap');
    const allowWrite = document.getElementById('team-sync-allow-client-write');
    const autoSendWrap = document.getElementById('key-team-auto-send-wrap');
    const autoSend = document.getElementById('team-sync-auto-send');
    const syncActions = document.getElementById('key-team-sync-actions');
    const syncButton = document.getElementById('btn-key-team-sync');
    const undoButton = document.getElementById('btn-key-team-undo-sync');
    const syncStatus = document.getElementById('key-team-sync-status');
    const isClient = lan.role === 'client';
    const isServer = lan.role === 'server';
    if (autoWrap) autoWrap.hidden = !isClient;
    if (allowWriteWrap) allowWriteWrap.hidden = !isServer;
    if (autoSendWrap) autoSendWrap.hidden = !isClient;
    if (syncActions) syncActions.hidden = !isClient;
    if (autoReceive) {
        autoReceive.checked = lan.teamSyncAutoReceive === true;
        autoReceive.disabled = isClient && lan.connected && lan.teamSyncPushSupported === false;
    }
    if (allowWrite) {
        allowWrite.checked = lan.teamSyncAllowClientWrite === true;
        allowWrite.disabled = !isServer;
    }
    if (autoSend) {
        autoSend.checked = lan.teamSyncAutoSend === true;
        autoSend.disabled = !isClient || (lan.connected &&
            (lan.teamSyncBidirectionalSupported === false ||
                lan.teamSyncClientWriteAllowed === false));
        autoSend.title = lan.connected && lan.teamSyncClientWriteAllowed === false
            ? '需要服务器先勾选允许客户端修改比赛状态' : '';
    }
    if (syncButton) {
        syncButton.disabled = !isClient || lan.teamSyncAutoReceive === true || !lan.connected ||
            lan.teamSyncPending || lan.teamSyncSupported === false;
        syncButton.textContent = lan.teamSyncPending ? '获取中...' : '获取服务器比赛状态';
    }
    if (undoButton) {
        undoButton.hidden = lan.teamSyncAutoReceive === true || !lan.teamSyncCanUndo;
        undoButton.disabled = lan.teamSyncAutoReceive === true;
    }
    if (syncStatus) {
        if (lan.role === 'server') {
            syncStatus.textContent = lan.connected
                ? `${lan.teamSyncSubscribed
                    ? '客户端已开启自动接收；服务器变化会主动推送。'
                    : '客户端未开启自动接收。'} ${lan.teamSyncAllowClientWrite
                    ? '已允许客户端提交修改。' : '客户端修改权限已关闭。'}`
                : '客户端连接后，可手动获取或订阅服务器比赛状态。';
        } else if (!isClient) {
            syncStatus.textContent = '切换为服务器或客户端后，可以同步比分、名单和战绩。';
        } else if (lan.connected && lan.teamSyncPushSupported === false) {
            syncStatus.textContent = '服务器版本不支持自动同步，请升级服务器软件；手动同步仍可使用。';
        } else if (lan.teamSyncAutoReceive) {
            const base = lan.connected
                ? (lan.teamSyncSubscribed ? '已自动订阅，服务器变化后会直接覆盖本地状态。' : '正在订阅服务器比赛状态。')
                : '已记住自动接收，连接服务器后会立即同步。';
            syncStatus.textContent = lan.teamSyncLastAutoResult ? `${base} ${lan.teamSyncLastAutoResult}` : base;
            if (lan.teamSyncAutoSend) {
                syncStatus.textContent += lan.teamSyncClientWriteAllowed
                    ? ' 本机后续修改会自动提交给服务器。'
                    : ' 正在等待服务器授权客户端修改。';
            }
        } else if (lan.teamSyncPending) {
            syncStatus.textContent = '正在读取服务器快照，本地状态不会改变。';
        } else if (lan.connected) {
            syncStatus.textContent = '手动获取比分、8 人队伍和战绩；确认前不会覆盖本地。';
        } else {
            syncStatus.textContent = '连接后可手动获取服务器的比分、队伍和战绩。';
        }
    }
}

function getCurrentMatchSnapshotFromWeb() {
    const players = [];
    document.querySelectorAll('#team-red .player-row').forEach(row => players.push(getRowData(row, 0)));
    document.querySelectorAll('#team-blue .player-row').forEach(row => players.push(getRowData(row, 1)));
    return {
        redScore: Number(document.querySelector('#team-red .team-score-input')?.value || 0),
        blueScore: Number(document.querySelector('#team-blue .team-score-input')?.value || 0),
        redPickMode,
        isFlipped: (document.getElementById('teams-wrap') || document.getElementById('main-container'))
            ?.style?.flexDirection === 'row-reverse',
        outputSeatLabelToKillFile,
        players
    };
}

function buildTeamSyncDiffHtml(snapshot, localBaseline) {
    const local = localBaseline || getCurrentMatchSnapshotFromWeb();
    const lines = [];
    const add = (label, before, after) => {
        if (String(before) === String(after)) return;
        lines.push(`<div><strong>${escapeHtml(label)}</strong>：` +
            `<span style="color:#f3a6a6">${escapeHtml(String(before))}</span> → ` +
            `<span style="color:#9ed4ff">${escapeHtml(String(after))}</span></div>`);
    };

    add('红队比分', local.redScore, snapshot.redScore);
    add('蓝队比分', local.blueScore, snapshot.blueScore);
    add('红队选人', local.redPickMode === 'first' ? '先选' : '后选', snapshot.redPickMode === 'first' ? '先选' : '后选');
    add('红蓝翻转', local.isFlipped ? '已翻转' : '未翻转', snapshot.isFlipped ? '已翻转' : '未翻转');
    add('TXT 选人顺序', local.outputSeatLabelToKillFile ? '输出' : '不输出',
        snapshot.outputSeatLabelToKillFile ? '输出' : '不输出');
    const teamText = value => Number(value) === 0 ? '红队' : (Number(value) === 1 ? '蓝队' : '无');
    add('上一击击杀队伍', teamText(local.lastKillerTeam), teamText(snapshot.lastKillerTeam));
    (Array.isArray(snapshot.players) ? snapshot.players : []).forEach((player, index) => {
        const before = local.players[index] || {};
        const seat = `${index < 4 ? '红' : '蓝'}${(index % 4) + 1}`;
        add(`${seat} 主号`, before.name || '空', player.name || '空');
        add(`${seat} 小号`, (before.aliases || []).join('、') || '无', (player.aliases || []).join('、') || '无');
        add(`${seat} 战绩`, `${before.kills || 0}/${before.deaths || 0}/A${before.akCount || 0}`,
            `${player.kills || 0}/${player.deaths || 0}/A${player.akCount || 0}`);
        add(`${seat} 连杀进度`, before.currentStreak || 0, player.currentStreak || 0);
    });

    if (!lines.length) lines.push('<div>服务器比赛状态与本地相同。</div>');
    return '<div style="max-height:300px;overflow:auto;text-align:left;line-height:1.7">' +
        '<div style="margin-bottom:8px;color:#ffd87a">确认后将覆盖本地比分、队伍和战绩，最近识别记录不会同步。</div>' +
        lines.join('') + '</div>';
}

function showTeamSyncSnapshot(data) {
    const snapshot = data?.snapshot;
    const localBaseline = data?.localBaseline;
    if (!snapshot || !Array.isArray(snapshot.players) || snapshot.players.length !== 8) {
        pendingTeamSyncSnapshot = null;
        showAlert('服务器比赛状态格式无效，本地数据未修改。');
        return;
    }
    pendingTeamSyncSnapshot = snapshot;
    showConfirm(buildTeamSyncDiffHtml(snapshot, localBaseline), (ok) => {
        if (ok) {
            sendKeyLanCommand('cmd_apply_team_sync');
        } else {
            sendKeyLanCommand('cmd_cancel_team_sync');
        }
        pendingTeamSyncSnapshot = null;
    }, { okText: '确认覆盖本地', cancelText: '取消' });
}

function isKeyMappingPanelOpen() {
    return document.getElementById('key-mapping-overlay')?.classList.contains('active') === true;
}

function openKeyMappingPanel() {
    const overlay = document.getElementById('key-mapping-overlay');
    if (!overlay) return;
    keyMappingSettings = normalizeKeyMappingSettings(keyMappingSettings || {});
    keyMappingCaptureSlot = -1;
    setMoreControlsOpen(false);
    overlay.classList.add('active');
    overlay.setAttribute('aria-hidden', 'false');
    renderKeyMappingPanel();
    window.chrome?.webview?.postMessage({ action: 'cmd_set_appearance_panel_open', open: true });
}

function closeKeyMappingPanel() {
    const overlay = document.getElementById('key-mapping-overlay');
    if (!overlay) return;
    keyMappingCaptureSlot = -1;
    overlay.classList.remove('active');
    overlay.setAttribute('aria-hidden', 'true');
    window.chrome?.webview?.postMessage({ action: 'cmd_set_appearance_panel_open', open: false });
}

function isKeyLanPanelOpen() {
    return document.getElementById('key-lan-overlay')?.classList.contains('active') === true;
}

function openKeyLanPanel() {
    const overlay = document.getElementById('key-lan-overlay');
    if (!overlay) return;
    keyMappingSettings = normalizeKeyMappingSettings(keyMappingSettings || {});
    setMoreControlsOpen(false);
    overlay.classList.add('active');
    overlay.setAttribute('aria-hidden', 'false');
    renderKeyLanPanel();
    window.chrome?.webview?.postMessage({ action: 'cmd_set_appearance_panel_open', open: true });
}

function closeKeyLanPanel() {
    const overlay = document.getElementById('key-lan-overlay');
    if (!overlay) return;
    overlay.classList.remove('active');
    overlay.setAttribute('aria-hidden', 'true');
    window.chrome?.webview?.postMessage({ action: 'cmd_set_appearance_panel_open', open: false });
}

function sendKeyMappingSettings(immediate = false) {
    if (!window.chrome?.webview || !keyMappingSettings) return;
    if (keyMappingSyncTimer) clearTimeout(keyMappingSyncTimer);
    const send = () => window.chrome.webview.postMessage({
        action: 'cmd_set_key_mapping_settings',
        settings: {
            enabled: keyMappingSettings.enabled,
            slots: keyMappingSettings.slots.map(slot => ({
                vk: slot.vk,
                label: slot.label,
                color: slot.color,
                opacity: slot.opacity
            }))
        }
    });
    if (immediate) send();
    else keyMappingSyncTimer = setTimeout(send, 90);
}

function keyLabelFromEvent(event) {
    const named = {
        ' ': 'Space',
        Spacebar: 'Space',
        Control: 'Ctrl',
        CapsLock: 'Caps',
        ArrowUp: 'Up',
        ArrowDown: 'Down',
        ArrowLeft: 'Left',
        ArrowRight: 'Right'
    };
    if (named[event.key]) return named[event.key];
    if (String(event.key || '').length === 1) return String(event.key).toUpperCase();
    return String(event.key || event.code || '').replace(/^Key/, '').replace(/^Digit/, '').slice(0, 16);
}

function beginKeyBindingCapture(index = selectedKeyMappingSlot) {
    selectedKeyMappingSlot = Math.max(0, Math.min(KEY_MAPPING_SLOT_COUNT - 1, Number(index) || 0));
    keyMappingCaptureSlot = selectedKeyMappingSlot;
    renderKeyMappingPanel();
}

function renderKeyMappingGrid() {
    const grid = document.getElementById('key-mapping-grid');
    if (!grid || !keyMappingSettings) return;
    grid.replaceChildren();
    keyMappingSettings.slots.forEach((slot, index) => {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'key-slot-button';
        if (index === selectedKeyMappingSlot) button.classList.add('selected');
        if (index === keyMappingCaptureSlot) button.classList.add('capturing');
        button.style.setProperty('--slot-color', slot.color);
        button.title = `技能 ${index + 1}：${slot.label || '未绑定'}`;
        button.innerHTML = `<span class="key-slot-number">${index + 1}</span>` +
            `<span class="key-slot-label">${escapeHtml(slot.label || '--')}</span>`;
        button.addEventListener('click', () => beginKeyBindingCapture(index));
        grid.appendChild(button);
    });
}

function syncKeySlotEditor() {
    if (!keyMappingSettings) return;
    const slot = keyMappingSettings.slots[selectedKeyMappingSlot];
    document.getElementById('key-slot-editor-index').textContent = `技能 ${selectedKeyMappingSlot + 1}`;
    const captureState = document.getElementById('key-capture-state');
    captureState.textContent = keyMappingCaptureSlot >= 0 ? '请按一个键盘按键' : '点击格子后按键';
    captureState.classList.toggle('active', keyMappingCaptureSlot >= 0);
    const capture = document.getElementById('key-binding-capture');
    capture.textContent = slot.label || '未绑定';
    capture.style.borderColor = slot.color;
    document.getElementById('key-slot-color').value = slot.color;
    document.getElementById('key-slot-color-text').value = slot.color;
    document.getElementById('key-slot-opacity').value = String(slot.opacity);
    document.getElementById('key-slot-opacity-value').value = String(slot.opacity);
}

function renderKeyMappingPanel() {
    if (!keyMappingSettings) keyMappingSettings = createDefaultKeyMappingSettings();
    const status = document.getElementById('key-mapping-status');
    status.textContent = '默认 QWERTY+Ctrl / ASDFGH+Alt · 点击技能格后直接按键绑定';
    document.getElementById('key-mapping-enabled').checked = keyMappingSettings.enabled;
    const displayButton = document.getElementById('btn-key-display-toggle');
    displayButton.textContent = keyMappingSettings.windowVisible ? '关闭响应窗口' : '打开响应窗口';
    displayButton.classList.toggle('accent', keyMappingSettings.windowVisible);
    document.getElementById('btn-key-display-toggle').disabled = !keyMappingSettings.httpReady;
    renderKeyMappingGrid();
    syncKeySlotEditor();
}

function resetKeyMappingDefaults() {
    if (!keyMappingSettings) keyMappingSettings = createDefaultKeyMappingSettings();
    keyMappingSettings.slots.forEach((slot, index) => {
        slot.vk = KEY_MAPPING_DEFAULT_VKS[index];
        slot.label = KEY_MAPPING_DEFAULT_LABELS[index];
    });
    selectedKeyMappingSlot = 0;
    keyMappingCaptureSlot = -1;
    renderKeyMappingPanel();
    sendKeyMappingSettings(true);
}

function applyKeySlotStyleFromInputs(sourceId) {
    if (!keyMappingSettings) return;
    const slot = keyMappingSettings.slots[selectedKeyMappingSlot];
    if (sourceId === 'key-slot-color' || sourceId === 'key-slot-color-text') {
        const raw = document.getElementById(sourceId).value;
        if (/^#[0-9a-f]{6}$/i.test(raw)) slot.color = normalizeKeyColor(raw);
    } else {
        const value = Math.max(0, Math.min(100, Number(document.getElementById(sourceId).value || 0)));
        slot.opacity = value;
    }
    renderKeyMappingPanel();
    sendKeyMappingSettings();
}

document.addEventListener('keydown', (event) => {
    if (keyMappingCaptureSlot < 0 || !isKeyMappingPanelOpen()) return;
    const vk = Number(event.keyCode || event.which || 0);
    if (vk <= 0 || vk > 254) return;
    event.preventDefault();
    event.stopImmediatePropagation();
    const slot = keyMappingSettings.slots[keyMappingCaptureSlot];
    slot.vk = vk;
    slot.label = keyLabelFromEvent(event);
    selectedKeyMappingSlot = keyMappingCaptureSlot;
    keyMappingCaptureSlot = -1;
    renderKeyMappingPanel();
    sendKeyMappingSettings(true);
}, true);

function applyStateFromServer(state) {
    isSyncingFromServer = true;

    isMonitoring = state.isMonitoring;
    isStartPending = !!state.isStartPending && !isMonitoring;
    const btnMonitor = document.getElementById('btn-monitor');
    btnMonitor.disabled = false;
    btnMonitor.removeAttribute('aria-busy');
    if (isMonitoring) {
        btnMonitor.innerHTML = '🛑 停止';
        btnMonitor.className = 'ctrl-btn btn-monitor-stop';
    } else if (isStartPending) {
        btnMonitor.innerHTML = '⏳ 开启OCR服务中...';
        btnMonitor.className = 'ctrl-btn btn-monitor-pending';
        btnMonitor.disabled = true;
        btnMonitor.setAttribute('aria-busy', 'true');
        btnMonitor.title = 'OCR 服务正在启动中，请稍候...';
    } else {
        btnMonitor.innerHTML = '▶ 运行';
        btnMonitor.className = 'ctrl-btn btn-monitor-start';
    }
    syncKillDisplayToggle(state);

    const teamsWrap = document.getElementById('teams-wrap') || document.getElementById('main-container');
    teamsWrap.style.flexDirection = state.isFlipped ? 'row-reverse' : 'row';

    setRedPickMode(state.redPickMode || (state.redPickFirst === false ? 'second' : 'first'), false);

    outputSeatLabelToKillFile = !!state.outputSeatLabelToKillFile;
    const outputSeatToggle = document.getElementById('output-seat-label-toggle');
    if (outputSeatToggle) outputSeatToggle.checked = outputSeatLabelToKillFile;

    systemFonts = normalizeSystemFonts(state.systemFonts);
    scoreboardTextStyles = normalizeScoreboardTextStyles(state.scoreboardTextStyles);
    killDisplaySettings = normalizeKillDisplaySettings(state.killDisplaySettings);
    keyMappingSettings = normalizeKeyMappingSettings(state.keyMappingSettings || keyMappingSettings || {});
    cloudMatchState = normalizeCloudMatchState(state.cloudMatch || cloudMatchState || {});
    applyScoreboardTextStyles(scoreboardTextStyles);
    applyKillDisplaySettings(killDisplaySettings);
    if (isKeyMappingPanelOpen()) renderKeyMappingPanel();
    if (isKeyLanPanelOpen()) renderKeyLanPanel();
    if (isCloudRoomPanelOpen()) renderCloudRoomPanel();
    restoreCloudRoomPromptFromState();
    if (keyMappingSettings.lan.adminRequired && !keyMappingSettings.lan.isAdmin) {
        setTimeout(() => showKeyMappingAdminPrompt('DNF 以更高权限运行，按键映射需要管理员权限才能读取游戏内按键。'), 0);
    }
    if (isAppearancePanelOpen()) {
        renderAppearanceScopeTabs();
        renderAppearanceStyleList();
        renderKillLayoutEditor();
        syncAppearanceEditorFromActiveStyle();
    }

    deathXAlgorithm = Number(state.deathXAlgorithm || 0);
    deathPatchInstalled = !!state.deathPatchInstalled;
    const algoSelect = document.getElementById('death-algo-select');
    if (algoSelect && algoSelect.value !== String(deathXAlgorithm)) {
        algoSelect.value = String(deathXAlgorithm);
    }

    isProMode = state.isMfcVisible;
    const btnPro = document.getElementById('btn-pro');
    if (btnPro) {
        btnPro.innerHTML = isProMode ? '🙈 隐藏专业' : '👑 专业模式';
        btnPro.style.background = isProMode ? '#3a1a52' : '#5a2a82';
    }

    const btnAuth = document.getElementById('btn-auth');
    const authText = state.authText || '未激活';
    const authValidLike = state.isAuthValid || (state.authText && state.authText.includes("试用"));
    if (btnAuth) {
        // 按钮内显示精简状态，超出部分用 ...；完整授权日期放到悬浮提示里。
        btnAuth.innerHTML = `🔑 授权 <span class="auth-short">${escapeHtml(authText)}</span>`;
        btnAuth.title = '';
        btnAuth.setAttribute('data-auth-tooltip', `授权状态：${authText}`);
        btnAuth.style.borderColor = authValidLike ? "#00e5ff" : "#ff0055";
        btnAuth.style.color = authValidLike ? "#00e5ff" : "#ff0055";
    }

    // 🚨 接收来自 C++ 的最新输出目录并显示
    if (state.outputDir) {
        document.getElementById('dir-display').innerText = `📁 输出目录: ${state.outputDir}`;
    }

    if (Date.now() < pendingClearAllUntil) {
        recentEvents = [];
        renderReviewEvents();
        document.querySelector('#team-blue .team-score-input').value = 0;
        document.querySelector('#team-red .team-score-input').value = 0;
        document.querySelectorAll('.player-row').forEach(row => {
            const nameInp = row.querySelector('.name-input');
            if (nameInp) {
                nameInp.value = '';
                nameInp.classList.remove('input-error');
                nameInp.removeAttribute('data-error-msg');
            }
            row.querySelector('.stat-kill').value = '0';
            row.querySelector('.stat-death').value = '0';
            row.querySelector('.stat-ak').value = '-';
        });
        resetSeatLabelsToDefault();
        isSyncingFromServer = false;
        updateStartButtonGuard();
        return;
    }

    recentEvents = Array.isArray(state.recentEvents) ? state.recentEvents : [];
    renderReviewEvents();

    document.querySelector('#team-blue .team-score-input').value = state.blueScore;
    document.querySelector('#team-red .team-score-input').value = state.redScore;

    const rows = document.querySelectorAll('.player-row');
    state.players.forEach((p, idx) => {
        if (idx >= 8) return;
        let row = rows[idx];
        let nameInp = row.querySelector('.name-input');
        if (!nameInp.classList.contains('input-error') && !shouldPreserveNoAliasInput(nameInp, p.name)) {
            nameInp.value = p.name;
        }
        row.querySelector('.stat-kill').value = p.kills;
        row.querySelector('.stat-death').value = p.deaths;
        row.querySelector('.stat-ak').value = p.akCount === 0 ? '-' : p.akCount;
    });
    refreshPickLabels();
    isSyncingFromServer = false;
    updateStartButtonGuard();
}

const triggerSync = () => {
    updateStartButtonGuard();
    pushStateToServer();
};

function getWebContentHeight() {
    const scrollTop = window.scrollY || document.documentElement.scrollTop || document.body.scrollTop || 0;
    let bottom = 0;

    document.querySelectorAll('body *').forEach(el => {
        if (el.closest('.popover')) return;
        if (el.id === 'custom-modal' && !el.classList.contains('active')) return;
        if (el.closest('#custom-modal') && !document.getElementById('custom-modal')?.classList.contains('active')) return;

        const style = window.getComputedStyle(el);
        if (style.display === 'none' || style.visibility === 'hidden' || style.position === 'fixed') return;

        const rect = el.getBoundingClientRect();
        if (rect.width > 0 && rect.height > 0) bottom = Math.max(bottom, rect.bottom + scrollTop);
    });

    const body = document.body;
    const html = document.documentElement;
    return Math.ceil(Math.max(
        bottom + 36,
        body.scrollHeight, body.offsetHeight,
        html.scrollHeight, html.offsetHeight,
        700
    ));
}

function requestWebWindowResize(force = false) {
    scheduleLayoutFit(!!force, force ? 'resize-force' : 'resize');
}

function getActivePlayerNames() {
    return Array.from(document.querySelectorAll('.name-input'))
        .map(input => input.value.trim())
        .filter(Boolean);
}

function renderReviewEvents() {
    const list = document.getElementById('review-list');
    const count = document.getElementById('review-count');
    const toggleCount = document.getElementById('review-toggle-count');
    if (!list || !count) return;

    count.textContent = String(recentEvents.length);
    if (toggleCount) toggleCount.textContent = String(recentEvents.length);
    if (recentEvents.length === 0) {
        list.innerHTML = '<div class="review-empty">暂无识别记录</div>';
        requestWebWindowResize(true);
        return;
    }

    list.innerHTML = recentEvents.map(ev => {
        const status = ev.status || (ev.statsApplied ? '已计入' : '未计入');
        const cls = ev.undone ? 'is-undone' : (ev.statsApplied ? 'is-applied' : 'is-pending');
        const killer = ev.killer || '待定';
        const dead = ev.dead || '待定';
        const matchText = `${killer}击杀${dead}`;
        const detail = [
            ev.algorithm ? `算法：${ev.algorithm}` : '',
            ev.triggerSide ? `触发：${ev.triggerSide}` : '',
            ev.snapshotPath ? `截图：${ev.snapshotPath}` : '',
            ev.ocrSummary ? `OCR：${ev.ocrSummary}` : '',
            ev.candidateSummary ? `候选：${ev.candidateSummary}` : ''
        ].filter(Boolean).join('\n');

        return `
            <div class="review-item ${cls}" data-id="${ev.id}">
                <div class="review-main">
                    <span class="review-time">${escapeHtml(ev.time || '--:--:--')}</span>
                    <span class="review-match" title="${escapeHtml(matchText)}"><span class="review-winner">${escapeHtml(killer)}</span><b>击杀</b><span class="review-loser">${escapeHtml(dead)}</span></span>
                    <span class="review-status">${escapeHtml(status)}</span>
                </div>
                <div class="review-sub" title="${escapeHtml(detail)}">${escapeHtml(ev.triggerSide || '未知触发侧')}</div>
                <div class="review-actions">
                    <button class="review-btn btn-review-undo" data-id="${ev.id}" ${(!ev.undone && !ev.statsApplied) ? 'disabled' : ''}>${ev.undone ? '恢复' : '撤销'}</button>
                </div>
            </div>`;
    }).join('');
    requestWebWindowResize(true);
}

function sendReviewUndo(eventId) {
    if (!window.chrome?.webview) return;
    window.chrome.webview.postMessage({ action: 'cmd_undo_event', id: Number(eventId) });
}

function setReviewPanelOpen(open) {
    isReviewPanelOpen = !!open;
    const panel = document.getElementById('review-panel');
    const backdrop = document.getElementById('review-backdrop');
    const toggle = document.getElementById('btn-review-toggle');
    panel?.classList.toggle('is-open', isReviewPanelOpen);
    backdrop?.classList.toggle('is-open', isReviewPanelOpen);
    panel?.setAttribute('aria-hidden', isReviewPanelOpen ? 'false' : 'true');
    toggle?.setAttribute('aria-expanded', isReviewPanelOpen ? 'true' : 'false');
    if (isReviewPanelOpen) setConsolePanelOpen(false);
}

function toggleReviewPanel() {
    setReviewPanelOpen(!isReviewPanelOpen);
}

function syncKillDisplayToggle(state = {}) {
    const btn = document.getElementById('btn-kill-display-toggle');
    if (!btn) return;

    const ready = state.killDisplayHttpReady !== false;
    isKillDisplayWindowVisible = !!state.killDisplayWindowVisible;
    btn.classList.toggle('is-open', isKillDisplayWindowVisible);
    btn.setAttribute('aria-pressed', isKillDisplayWindowVisible ? 'true' : 'false');
    btn.disabled = !ready;
    btn.title = ready
        ? (isKillDisplayWindowVisible ? '点击关闭击杀展示页面' : '点击打开击杀展示页面')
        : (state.killDisplayHttpError || '击杀展示页本地服务未启动');
}

// ==========================================
// 2. 内置弹窗系统
// ==========================================
function sanitizeConsoleColor(value) {
    const text = String(value || '').trim();
    return /^#[0-9a-fA-F]{6}$/.test(text) ? text : '#d7dde8';
}

function renderConsoleLogs() {
    const list = document.getElementById('console-list');
    const count = document.getElementById('console-count');
    const toggleCount = document.getElementById('console-toggle-count');
    if (!list || !count) return;

    count.textContent = String(cxxConsoleLogs.length);
    if (toggleCount) toggleCount.textContent = String(cxxConsoleLogs.length);
    if (cxxConsoleLogs.length === 0) {
        list.innerHTML = '<div class="console-empty">暂无日志</div>';
        return;
    }

    list.innerHTML = cxxConsoleLogs.map(log => {
        const color = sanitizeConsoleColor(log.color);
        return `<div class="console-line" style="color:${color}">${escapeHtml(log.text || '')}</div>`;
    }).join('');
    list.scrollTop = list.scrollHeight;
}

function appendConsoleLogs(logs) {
    if (!Array.isArray(logs) || logs.length === 0) return;
    for (const log of logs) {
        cxxConsoleLogs.push({
            text: String(log?.text || ''),
            color: sanitizeConsoleColor(log?.color)
        });
    }
    if (cxxConsoleLogs.length > 400) cxxConsoleLogs = cxxConsoleLogs.slice(-400);
    renderConsoleLogs();
}

function setConsolePanelOpen(open) {
    isConsolePanelOpen = !!open;
    const panel = document.getElementById('console-panel');
    const toggle = document.getElementById('btn-console-toggle');
    panel?.classList.toggle('is-open', isConsolePanelOpen);
    panel?.setAttribute('aria-hidden', isConsolePanelOpen ? 'false' : 'true');
    toggle?.setAttribute('aria-expanded', isConsolePanelOpen ? 'true' : 'false');
    if (isConsolePanelOpen) {
        renderConsoleLogs();
        setReviewPanelOpen(false);
    }
}

function toggleConsolePanel() {
    setConsolePanelOpen(!isConsolePanelOpen);
}

const customModal = document.getElementById('custom-modal');
const modalMsg = document.getElementById('modal-msg');
const modalInput = document.getElementById('modal-input');
const modalInputHelp = document.getElementById('modal-input-help');
const modalCancel = document.getElementById('modal-cancel');
const modalOk = document.getElementById('modal-ok');
let currentModalCallback = null;

let currentModalOptions = {};

const DNF_AREA_RE = /(广东|北京|上海|江苏|浙江|福建|四川|山东|河南|湖北|湖南|河北|辽宁|吉林|黑龙江|安徽|江西|广西|陕西|山西|重庆|天津|云南|贵州|新疆|西藏|青海|甘肃|宁夏|内蒙古|东北|西北|西南|跨)([1-9])/;
const DNF_JOB_KEYWORDS = [
    '鬼剑士','剑魂','狂战士','阿修罗','鬼泣','剑影','驭剑士','暗殿骑士','契魔者','流浪武士','刃影',
    '格斗家','气功师','散打','街霸','柔道','柔道家','男柔道','女柔道',
    '神枪手','漫游枪手','枪炮师','机械师','弹药专家','合金战士',
    '魔法师','元素师','召唤师','战斗法师','魔道学者','小魔女','次元行者','血法师','逐风者','冰结师',
    '圣职者','圣骑士','蓝拳','蓝拳圣使','蓝拳使者','驱魔师','复仇者','巫女','异端审判者','诱魔者','帕拉丁',
    '暗夜使者','刺客','死灵术士','忍者','影舞者',
    '守护者','精灵骑士','混沌魔灵','龙骑士','黑暗武士','缔造者'
];

function escapeHtml(str) {
    return String(str || '').replace(/[&<>'"]/g, ch => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;' }[ch]));
}

function parseInlineMainAliasInput(raw) {
    const text = (raw || '').trim();
    const sep = text.indexOf('|');
    if (sep <= 0 || sep >= text.length - 1) return null;
    const mainName = text.slice(0, sep).trim();
    const aliasName = text.slice(sep + 1).trim();
    if (!mainName || !aliasName) return null;
    return { mainName, aliasName };
}

function parseAliasInput(raw) {
    const original = (raw || '').trim();
    const parts = original.split('#');
    let body = (parts[0] || '').trim();
    const declaredJob = parts.length > 1 ? parts.slice(1).join('#').trim() : '';
    const areaMatch = body.match(DNF_AREA_RE);
    const declaredArea = areaMatch ? areaMatch[0] : '';
    let realId = body;
    if (declaredArea) realId = (body.slice(0, areaMatch.index) + body.slice(areaMatch.index + declaredArea.length)).trim();
    const isOnlyArea = !!declaredArea && realId === '' && !declaredJob;
    const jobLike = DNF_JOB_KEYWORDS.some(job => original === job || original.includes(job));
    const isSymbolLike = realId !== '' && !/[\u4e00-\u9fa5A-Za-z0-9]/.test(realId);
    return { original, realId, declaredArea, declaredJob, isOnlyArea, jobLike, isSymbolLike };
}

function getAliasValidationError(raw) {
    const alias = (raw || '').trim();
    if (!alias) return '小号不能为空';
    const p = parseAliasInput(alias);
    if (!p.realId) return `小号【${alias}】缺少真实ID，不能只填大区或职业。`;
    // 允许小号列表保留 2 字短 ID；短 ID 只做警告，并在开始监控时拦截。
    return '';
}

function isLegacyShortAliasWithoutMeta(raw) {
    const alias = (raw || '').trim();
    if (!alias) return false;
    const p = parseAliasInput(alias);
    const realLen = Array.from(p.realId || '').length;
    return !!p.realId && realLen < 3 && !p.declaredArea && !p.declaredJob;
}

function getLegacyShortAliasDeleteReason(raw) {
    const id = parseAliasInput(raw).realId || raw;
    return `小号【${raw}】是旧库短ID，真实ID少于3个字符且没有大区/#职业，容易误识别。不会自动删除；建议后续改成“上海1${id}”或“${id}#职业”。`;
}

function getActiveShortIdViolations() {
    const violations = [];
    document.querySelectorAll('.player-row').forEach(row => {
        const input = row.querySelector('.name-input');
        if (!input) return;
        const playerName = input.value.trim();
        if (!playerName || input.classList.contains('input-error')) return;
        const aliases = getCleanAliases(playerName);
        const badAliases = aliases.filter(a => isLegacyShortAliasWithoutMeta(a));
        if (badAliases.length > 0) {
            violations.push({ row, input, playerName, badAliases });
        }
    });
    return violations;
}

function getActiveNoAliasViolations() {
    const violations = [];
    document.querySelectorAll('.player-row').forEach(row => {
        const input = row.querySelector('.name-input');
        if (!input) return;
        const playerName = input.value.trim();
        if (!playerName || input.classList.contains('input-error')) return;
        const aliases = getCleanAliases(playerName);
        if (aliases.length === 0) {
            violations.push({ row, input, playerName });
        }
    });
    return violations;
}

function getActiveSameJobShortWarnings() {
    const warnings = [];
    const teamGroups = [
        Array.from(document.querySelectorAll('#team-red .player-row')),
        Array.from(document.querySelectorAll('#team-blue .player-row'))
    ];

    teamGroups.forEach((rows, teamIdx) => {
        const byJob = new Map();
        rows.forEach(row => {
            const input = row.querySelector('.name-input');
            if (!input) return;
            const playerName = input.value.trim();
            if (!playerName || input.classList.contains('input-error')) return;
            const aliases = getCleanAliases(playerName);
            aliases.forEach(alias => {
                const meta = parseAliasInput(alias);
                const realLen = Array.from(meta.realId || '').length;
                const isShortOrSymbol = meta.isSymbolLike || (realLen > 0 && realLen < 3);
                if (!isShortOrSymbol || !meta.declaredJob) return;
                const key = meta.declaredJob.trim();
                if (!key) return;
                if (!byJob.has(key)) byJob.set(key, []);
                byJob.get(key).push({ row, input, playerName, alias, job: key, teamIdx });
            });
        });

        byJob.forEach(items => {
            const uniquePlayers = new Set(items.map(x => x.playerName));
            if (uniquePlayers.size < 2) return;
            const names = Array.from(uniquePlayers).join('、');
            items.forEach(item => {
                warnings.push({
                    row: item.row,
                    input: item.input,
                    playerName: item.playerName,
                    alias: item.alias,
                    job: item.job,
                    message: `同队存在多个短ID/纯符号ID声明了 #${item.job}：${names}。如果 OCR 只读到职业，程序不会强判；建议补更完整ID或大区。`
                });
            });
        });
    });

    return warnings;
}

function updateStartButtonGuard() {
    const btnMonitor = document.getElementById('btn-monitor');
    if (!btnMonitor) return [];

    document.querySelectorAll('.player-row.short-id-block-row, .player-row.short-id-warn-row, .player-row.same-job-warn-row, .player-row.no-alias-block-row').forEach(row => {
        row.classList.remove('short-id-block-row', 'short-id-warn-row', 'same-job-warn-row', 'no-alias-block-row');
        row.removeAttribute('data-short-id-warning');
        row.removeAttribute('data-no-alias-warning');
        row.removeAttribute('data-same-job-warning');
    });

    const shortWarnings = getActiveShortIdViolations();
    shortWarnings.forEach(v => {
        const msg = `该选手存在旧库短ID：${v.badAliases.join('、')}。不会自动删除，但建议补充大区或 #职业。`;
        v.row.classList.add('short-id-warn-row');
        v.row.setAttribute('data-short-id-warning', msg);
        if (v.input) v.input.title = msg;
    });

    const sameJobWarnings = getActiveSameJobShortWarnings();
    sameJobWarnings.forEach(v => {
        v.row.classList.add('same-job-warn-row');
        v.row.setAttribute('data-same-job-warning', v.message);
        if (v.input && !v.input.title) v.input.title = v.message;
    });

    const noAliasViolations = getActiveNoAliasViolations();
    noAliasViolations.forEach(v => {
        const msg = `该选手只有主号，没有绑定小号。主号不参与 OCR 名称匹配，请至少绑定一个小号。`;
        v.row.classList.add('no-alias-block-row');
        v.row.setAttribute('data-no-alias-warning', msg);
        if (v.input) v.input.title = msg;
    });

    if (!isMonitoring && isStartPending) {
        btnMonitor.disabled = true;
        btnMonitor.classList.add('btn-monitor-disabled');
        btnMonitor.title = 'OCR 服务正在启动中，请稍候...';
        return noAliasViolations.concat(shortWarnings);
    }

    // 允许小号列表存在 2 字短 ID，但开始监控必须拦截：短 ID 容易被 OCR 误识别。
    const shouldBlockStart = !isMonitoring && (noAliasViolations.length > 0 || shortWarnings.length > 0);
    btnMonitor.disabled = shouldBlockStart;
    btnMonitor.classList.toggle('btn-monitor-disabled', shouldBlockStart);

    if (shouldBlockStart) {
        const titleParts = [];
        if (noAliasViolations.length) {
            const names = noAliasViolations.map(v => v.playerName).join('、');
            titleParts.push(`以下选手没有绑定小号：${names}`);
        }
        if (shortWarnings.length) {
            const names = shortWarnings.map(v => `【${v.playerName}】${v.badAliases.join('、')}`).join('；');
            titleParts.push(`以下选手存在未加大区/#职业的2字短ID：${names}`);
        }
        btnMonitor.title = `无法开始：${titleParts.join('；')}`;
    } else {
        const titleParts = [];
        if (sameJobWarnings.length) titleParts.push('同队存在相同 #职业 的短ID/纯符号ID；只靠职业时不会强判。');
        btnMonitor.title = titleParts.join(' ');
    }

    return noAliasViolations.concat(shortWarnings);
}

function getStartGuardMessage() {
    const noAlias = getActiveNoAliasViolations();
    const shortIds = getActiveShortIdViolations();
    const lines = [];
    noAlias.forEach(v => lines.push(`【${v.playerName}】没有绑定任何小号`));
    shortIds.forEach(v => lines.push(`【${v.playerName}】存在未加大区/#职业的2字短ID：${v.badAliases.join('、')}`));
    if (!lines.length) return '';
    return `检测到上场选手信息不完整，暂不能开始监控：\n\n${lines.join('\n')}\n\n处理方式：没有小号的选手请至少绑定一个小号；2字短ID请补充大区或 #职业。`;
}

function getNoAliasGuardMessage(violations = getActiveNoAliasViolations()) {
    if (!violations.length) return '';
    const lines = violations.map(v => `【${v.playerName}】没有绑定任何小号`);
    return `检测到上场选手只有主号、没有小号，暂不能开始监控：\n\n${lines.join('\n')}\n\n主号不参与 OCR 名称匹配，请至少绑定一个小号。`;
}

function isAliasInputValid(raw) {
    return !getAliasValidationError(raw);
}

function renderAliasInputHelp() {
    if (!modalInputHelp || currentModalOptions.type !== 'alias') return;
    const val = modalInput.value.trim();
    const p = parseAliasInput(val);
    let html = `
        <div class="alias-help-title">推荐格式：</div>
        <div class="alias-help-examples">
            <span>王大枪</span><span>上海1王大枪</span><span>王大枪上海1</span><span>上海1王大枪#枪炮师</span>
        </div>
        <div class="alias-help-note">职业属性写在 <b>#</b> 后面。更容易识别成功</div>
        <div class="alias-help-note">真实ID少于3个字符时最好加上大区或者职业。</div>
    `;
    if (val) {
        html += `<div class="alias-parse-result">已识别：ID=<b>${escapeHtml(p.realId || '未检测到')}</b>`;
        if (p.declaredArea) html += `　大区=<b>${escapeHtml(p.declaredArea)}</b>`;
        if (p.declaredJob) html += `　职业=<b>${escapeHtml(p.declaredJob)}</b>`;
        html += `</div>`;
        const aliasRuleError = getAliasValidationError(val);
        if (p.isOnlyArea) html += `<div class="alias-help-warning">当前只检测到大区，缺少真实ID，例如“${escapeHtml(p.declaredArea)}一~一.”。</div>`;
        else if (aliasRuleError) html += `<div class="alias-help-warning">${escapeHtml(aliasRuleError)}</div>`;
        else if (p.realId && !p.declaredArea && p.isSymbolLike) html += `<div class="alias-help-warning">这是纯符号/难OCR ID，建议加大区提高命中率。</div>`;
        else if (p.jobLike && !p.declaredJob && !p.declaredArea) html += `<div class="alias-help-info">检测到职业词：会按真实ID保存；如要声明职业属性，请写成“真实ID#职业”。</div>`;
    }
    modalInputHelp.innerHTML = html;
}

function resetModalInputUi() {
    modalInput.placeholder = '';
    currentModalOptions = {};
    modalOk.textContent = '确定';
    modalCancel.textContent = '取消';
    if (modalInputHelp) {
        modalInputHelp.style.display = 'none';
        modalInputHelp.innerHTML = '';
    }
}

modalInput.addEventListener('input', () => {
    if (currentModalOptions.type === 'alias') renderAliasInputHelp();
});

function showAliasPrompt(playerName, callback, msg = null, initialValue = '') {
    pendingAliasPromptActive = true;
    pendingAliasPromptName = (playerName || '').trim();

    showPrompt(msg || `为【${playerName}】绑定新小号:`, (val) => {
        pendingAliasPromptActive = false;
        pendingAliasPromptName = '';
        callback(val);
    }, {
        type: 'alias',
        placeholder: '例：“王大枪”或者“上海1王大枪#枪炮师”',
        value: initialValue || ''
    });
}


function showConfirm(msg, callback, options = {}) {
    resetModalInputUi();
    currentModalOptions = {};
    modalMsg.innerHTML = msg;
    modalInput.style.display = 'none';
    modalCancel.style.display = 'inline-block';
    modalOk.textContent = options.okText || '确定';
    modalCancel.textContent = options.cancelText || '取消';
    customModal.classList.add('active');
    currentModalCallback = callback;
}
function showPrompt(msg, callback, options = {}) {
    currentModalOptions = options || {};
    modalMsg.innerHTML = msg;
    modalInput.style.display = 'inline-block';
    modalInput.value = options.value || '';
    modalInput.placeholder = options.placeholder || '';
    if (modalInputHelp) {
        modalInputHelp.style.display = options.type === 'alias' ? 'block' : 'none';
        modalInputHelp.innerHTML = '';
    }
    customModal.classList.add('active');
    if (options.type === 'alias') renderAliasInputHelp();
    modalInput.focus();
    currentModalCallback = callback;
}
function showAlert(msg, onOk = null) {
    resetModalInputUi();
    modalMsg.innerHTML = msg.replace(/\n/g, '<br>');
    modalInput.style.display = 'none';
    modalCancel.style.display = 'none';
    customModal.classList.add('active');
    currentModalCallback = (res) => {
        modalCancel.style.display = 'inline-block';
        if (res === true && typeof onOk === 'function') onOk();
    };
}
modalCancel.onclick = () => { customModal.classList.remove('active'); if (currentModalCallback) currentModalCallback(null); };
// 对话框的键盘事件（原位置，只需增加 stopImmediatePropagation）
document.addEventListener('keydown', function (e) {
    if (!customModal.classList.contains('active')) return;

    if (e.key === 'Enter') {
        e.preventDefault();
        modalOk.click();
        e.stopImmediatePropagation();   // ← 阻止后续监听器执行
    } else if (e.key === 'Escape') {
        e.preventDefault();
        modalCancel.click();
        e.stopImmediatePropagation();   // ← 阻止后续监听器执行
    }
});

modalOk.onclick = () => { customModal.classList.remove('active'); if (currentModalCallback) { let res = modalInput.style.display === 'none' ? true : modalInput.value; currentModalCallback(res); } };

function getCleanAliases(playerName) {
    playerDB[playerName] = uniqueAliasArray(playerDB[playerName]);
    return playerDB[playerName];
}

function hasAtLeastOneAlias(playerName) {
    return getCleanAliases(playerName).length > 0;
}

function bindAliasToPlayer(playerName, aliasName) {
    const aliasClean = normalizeAliasTextForCompare(aliasName);
    if (!playerName || !aliasClean) return false;
    if (!playerDB[playerName]) playerDB[playerName] = [];
    if (!savedDB[playerName]) savedDB[playerName] = [];
    playerDB[playerName] = uniqueAliasArray(playerDB[playerName]);
    savedDB[playerName] = uniqueAliasArray(savedDB[playerName]);
    mergeAliasIntoArray(playerDB[playerName], aliasClean);
    mergeAliasIntoArray(savedDB[playerName], aliasClean);
    playerDB[playerName] = uniqueAliasArray(playerDB[playerName]);
    savedDB[playerName] = uniqueAliasArray(savedDB[playerName]);
    return true;
}

function updateAliasForPlayer(playerName, oldAlias, newAlias) {
    const oldClean = (oldAlias || '').trim();
    const newClean = (newAlias || '').trim();
    if (!playerName || !oldClean || !newClean) return false;

    if (!playerDB[playerName]) playerDB[playerName] = [];
    if (!savedDB[playerName]) savedDB[playerName] = [];

    // 关键：先从“当前列表”和“永久库”里彻底删掉旧名称，再写入新名称。
    // 这样不会出现库里同时存在“修改前名称”和“修改后名称”。
    playerDB[playerName] = removeAliasFromArray(playerDB[playerName], oldClean);
    savedDB[playerName] = removeAliasFromArray(savedDB[playerName], oldClean);

    mergeAliasIntoArray(playerDB[playerName], newClean);
    mergeAliasIntoArray(savedDB[playerName], newClean);

    playerDB[playerName] = uniqueAliasArray(playerDB[playerName]);
    savedDB[playerName] = uniqueAliasArray(savedDB[playerName]);

    // 短时间内屏蔽 C++ 旧状态回推，把旧名称再次刷回 Web 库。
    pendingAliasRenameRecords.push({
        playerName,
        oldAlias: oldClean,
        newAlias: newClean,
        until: Date.now() + 10000
    });
    return true;
}

function findAliasConflict(playerName, aliasName, selfInput = null) {
    const aliasClean = (aliasName || '').trim();
    if (!aliasClean) return null;
    for (let inp of document.querySelectorAll('.name-input')) {
        if (selfInput && inp === selfInput) continue;
        let otherMain = inp.value.trim();
        if (!otherMain || otherMain === playerName) continue;
        if (otherMain === aliasClean) return otherMain;
        if (playerDB[otherMain] && aliasArrayHasDuplicateId(playerDB[otherMain], aliasClean)) return otherMain;
    }
    return null;
}

// ==========================================
// 3. 战场级查重引擎
// ==========================================
function getFieldConflict(newMainName, excludeInput) {
    if (!newMainName) return null;
    let newAliases = playerDB[newMainName] || [];
    let fieldInputs = document.querySelectorAll('.name-input');
    for (let inp of fieldInputs) {
        if (inp === excludeInput) continue;
        let otherMain = inp.value.trim();
        if (!otherMain) continue;
        let otherAliases = playerDB[otherMain] || [];
        if (otherMain === newMainName) return { owner: otherMain, reason: '主号已被占用' };
        if (otherAliases.some(alias => normalizeAliasTextForCompare(alias) === newMainName)) return { owner: otherMain, reason: `名字是[${otherMain}]的小号` };
        if (newAliases.some(alias => normalizeAliasTextForCompare(alias) === otherMain)) return { owner: otherMain, reason: `携带的小号包含了[${otherMain}]` };
        for (let a of newAliases) {
            const matchedAlias = findAliasByDuplicateId(otherAliases, a);
            if (matchedAlias) {
                return { owner: otherMain, reason: `小号ID[${getAliasDuplicateId(a)}]与对方小号[${matchedAlias}]冲突` };
            }
        }
    }
    return null;
}

// ==========================================
// 4. 渲染选手行与交互
function createPlayerRow(seatNumber = '') {

    const row = document.createElement('div');
    row.className = 'player-row';
    // 🚨 默认关闭，防止平时点到输入框误触拖拽
    row.draggable = false;

    // 🚨 1. HTML 结构：把拖拽柄放回 name-wrapper 里面（原来齿轮的位置）
    row.innerHTML = `
        <div class="name-wrapper">
            <input type="text" class="seat-number seat-label-input" value="" autocomplete="off" title="按先后手自动生成" readonly>
            <button type="button" class="seat-number seat-label-toggle" title="点击切换先选 / 后选" hidden></button>
            <input type="text" class="name-input" placeholder="" autocomplete="off">
            <div class="drag-handle" title="按住拖动以交换位置">⋮⋮</div>
            <div class="popover autocomplete-popover"></div>
            <div class="popover alias-popover"></div>
        </div>
        <div class="stats-group">
            <div class="stat-item"><input type="text" class="stat-kill" value="0"></div>
            <div class="stat-item"><input type="text" class="stat-death" value="0"></div>
            <div class="stat-item"><input type="text" class="stat-ak" value="-"></div>
        </div>`;

    const nameInput = row.querySelector('.name-input');
    const autoPopover = row.querySelector('.autocomplete-popover');
    const aliasPopover = row.querySelector('.alias-popover');
    const dragHandle = row.querySelector('.drag-handle'); // 获取拖拽柄

    bindProNumberControls(row.querySelector('.stat-kill'));
    bindProNumberControls(row.querySelector('.stat-death'));
    bindProNumberControls(row.querySelector('.stat-ak'), true);

    const seatInput = row.querySelector('.seat-label-input');
    seatInput.addEventListener('focus', function () {
        nameInput.focus();
    });
    const seatToggle = row.querySelector('.seat-label-toggle');
    seatToggle.addEventListener('click', function () {
        togglePickModeForRow(row, true);
    });

    // ==========================================
    // 🚨 2. 精确拖拽控制 (只在按住柄时开启)
    // ==========================================
    dragHandle.addEventListener('mousedown', () => row.draggable = true);
    dragHandle.addEventListener('mouseup', () => row.draggable = false);
    dragHandle.addEventListener('mouseleave', () => row.draggable = false);

    row.addEventListener('dragstart', function (e) {
        document.querySelectorAll('.popover').forEach(p => p.classList.remove('active'));
        clearPendingAliasPopoverLock();
        draggedRow = this;
        e.dataTransfer.effectAllowed = 'move';
        e.dataTransfer.setData('text/plain', '');
        setTimeout(() => this.classList.add('dragging'), 0);
    });

    row.addEventListener('dragend', function () {
        this.classList.remove('dragging');
        draggedRow = null;
        document.querySelectorAll('.player-row').forEach(r => r.classList.remove('drag-over'));
        this.draggable = false; // 拖拽完立刻关掉
    });

    row.addEventListener('dragover', function (e) {
        e.preventDefault();
        if (draggedRow !== this) this.classList.add('drag-over');
        return false;
    });

    row.addEventListener('dragleave', function () {
        this.classList.remove('drag-over');
    });

    row.addEventListener('drop', function (e) {
        e.stopPropagation();
        this.classList.remove('drag-over');
        if (draggedRow !== this && draggedRow) {
            swapDOMNodes(draggedRow, this); // 调用前面写的 DOM 互换函数
            refreshPickLabels();
            triggerSync();
        }
        return false;
    });

    // ==========================================
    // 🚨 3. 智能焦点与补全逻辑融合
    // ==========================================
    let currentFocusIndex = -1;
    function clearActiveItems() { autoPopover.querySelectorAll('.suggestion-item').forEach(item => item.classList.remove('keyboard-focus')); }

    nameInput.addEventListener('keydown', function (e) {
        // 对话框激活时，禁止任何名字输入框的键盘行为
        if (customModal.classList.contains('active')) return;

         // ========== Ctrl 选择互换模式 ==========
        if (e.ctrlKey && (e.key === 'ArrowUp' || e.key === 'ArrowDown' || e.key === 'ArrowLeft' || e.key === 'ArrowRight')) {
            e.preventDefault();
            e.stopPropagation();

            const allRows = Array.from(document.querySelectorAll('.player-row'));
            const sourceRow = this.closest('.player-row');
            if (!sourceRow) return;

            if (!ctrlSwapState.active) {
                ctrlSwapState.active = true;
                ctrlSwapState.sourceRow = sourceRow;
                ctrlSwapState.currentIndex = allRows.indexOf(sourceRow);
                sourceRow.classList.add('drag-source');
            }

            let newIndex = ctrlSwapState.currentIndex;
            // 向上/向下：纵向移动（同一队内）
            if (e.key === 'ArrowUp') {
                newIndex = Math.max(0, newIndex - 1);
            } else if (e.key === 'ArrowDown') {
                newIndex = Math.min(allRows.length - 1, newIndex + 1);
            } else if (e.key === 'ArrowLeft') {
                // 左键：如果是蓝队行（索引>=4），则跳到对应的红队行；否则不变
                if (newIndex >= 4) newIndex = newIndex - 4;
            } else if (e.key === 'ArrowRight') {
                // 右键：如果是红队行（索引<4），则跳到对应的蓝队行；否则不变
                if (newIndex < 4) newIndex = newIndex + 4;
            }

            // 更新目标行高亮
            if (newIndex !== ctrlSwapState.currentIndex) {
                if (ctrlSwapState.targetRow) ctrlSwapState.targetRow.classList.remove('drag-target');
                ctrlSwapState.currentIndex = newIndex;
                ctrlSwapState.targetRow = allRows[newIndex];
                ctrlSwapState.targetRow.classList.add('drag-target');
            }
            return;
        }

        if (e.key === 'Enter') {
            e.preventDefault();
            const name = this.value.trim();

            // 如果补全列表有高亮项，优先选中
            if (autoPopover.classList.contains('active') && currentFocusIndex > -1) {
                const items = autoPopover.querySelectorAll('.suggestion-item');
                if (items[currentFocusIndex]) {
                    items[currentFocusIndex].dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true }));
                }
                return;
            }

            if (name === '') return;

            const inlinePair = parseInlineMainAliasInput(name);
            if (inlinePair) {
                const aliasError = getAliasValidationError(inlinePair.aliasName);
                if (aliasError) {
                    showAlert(`❌ ${aliasError}`);
                    return;
                }
                const conflictOwner = findAliasConflict(inlinePair.mainName, inlinePair.aliasName, this);
                if (conflictOwner) {
                    showAlert(`小号【${inlinePair.aliasName}】已被场上选手【${conflictOwner}】占用，无法添加！`);
                    return;
                }
                this.value = inlinePair.mainName;
                bindAliasToPlayer(inlinePair.mainName, inlinePair.aliasName);
                autoPopover.classList.remove('active');
                openAliasPopover(this, inlinePair.mainName);
                triggerSync();
                return;
            }

            // 检查冲突
            if (this.classList.contains('input-error')) {
                showAlert(this.getAttribute('data-error-msg'));
                return;
            }

            // 获取该选手的小号（过滤空字符串）
            const rawAliases = playerDB[name] || [];
            const aliases = rawAliases.filter(a => a && a.trim());

            // 隐藏补全弹窗，保留小号面板
            autoPopover.classList.remove('active');

            // 无论是否有小号，按回车都弹出添加对话框
            const self = this;
            setTimeout(() => {
                showAliasPrompt(name, (newAlias) => {
                    if (newAlias && newAlias.trim()) {
                        const aliasClean = newAlias.trim();
                        const aliasError = getAliasValidationError(aliasClean);
                        if (aliasError) {
                            customModal.classList.remove('active');
                            setTimeout(() => {
                                showAlert(`❌ ${aliasError}`);
                                self.focus();
                            }, 100);
                            return;
                        }
                        let conflictOwner = findAliasConflict(name, aliasClean, self);
                        if (conflictOwner) {
                            customModal.classList.remove('active');
                            setTimeout(() => {
                                showAlert(`小号【${aliasClean}】已被场上选手【${conflictOwner}】占用，无法添加！`);
                                self.focus();
                            }, 100);
                            return;
                        }
                        const existingAlias = findSamePlayerAliasBlock(getCleanAliases(name), aliasClean);
                        if (existingAlias) {
                            showAlert(`小号【${aliasClean}】已存在或被包含于【${existingAlias}】，不会重复添加。`);
                            openAliasPopover(self, name);
                            return;
                        }
                        bindAliasToPlayer(name, aliasClean);
                        triggerSync();
                        openAliasPopover(self, name);
                    }
                    self.focus();
                });
            }, 10);
            return;
        }

      
        // ----- 方向键处理 (上/下/左/右) -----
        if (e.key === 'ArrowDown' || e.key === 'ArrowUp' || e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
            // 补全列表激活 + 上下键 → 用补全列表的选项切换
            if (autoPopover.classList.contains('active') && (e.key === 'ArrowDown' || e.key === 'ArrowUp')) {
                const items = autoPopover.querySelectorAll('.suggestion-item');
                if (items.length === 0) return;
                e.preventDefault();
                if (e.key === 'ArrowDown') {
                    currentFocusIndex++;
                    if (currentFocusIndex >= items.length) currentFocusIndex = 0;
                } else {
                    currentFocusIndex--;
                    if (currentFocusIndex < 0) currentFocusIndex = items.length - 1;
                }
                clearActiveItems();
                items[currentFocusIndex].classList.add('keyboard-focus');
                items[currentFocusIndex].scrollIntoView({ block: 'nearest' });
            } else {
                // 补全列表未激活 或 按下的是左右键 → 用方向键移动焦点到其他输入框
                e.preventDefault();
                const allInputs = Array.from(document.querySelectorAll('.name-input'));
                const total = allInputs.length;
                let idx = allInputs.indexOf(this);
                if (idx === -1) return;

                if (e.key === 'ArrowDown') {
                    idx = (idx + 1) % total;
                } else if (e.key === 'ArrowUp') {
                    idx = (idx - 1 + total) % total;
                } else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
                    // 红队索引 0-3，蓝队索引 4-7，左右键跳到另一队的同一行
                    const isRed = idx < 4;
                    const rowInTeam = idx % 4;
                    if ((isRed && e.key === 'ArrowLeft') || (!isRed && e.key === 'ArrowRight')) {
                        // 已经是最左边或最右边，不移动
                        return;
                    }
                    idx = isRed ? (4 + rowInTeam) : rowInTeam;
                }
                allInputs[idx].focus();
            }
            return;   // 方向键处理后直接返回，不继续执行其他逻辑
        }
    });

    nameInput.addEventListener('click', (e) => e.stopPropagation());

    // 🌟 核心改动点：判断当前输入框有没有值
    nameInput.addEventListener('focus', function () {
        if (pendingAliasPopoverInput && pendingAliasPopoverInput !== this) {
            clearPendingAliasPopoverLock();
        }
        this.dataset.previousMainName = this.value.trim();
        document.querySelectorAll('.popover').forEach(p => { if (p !== autoPopover && p !== aliasPopover) p.classList.remove('active'); });
        document.querySelectorAll('.player-row').forEach(r => r.classList.remove('active-row'));
        row.classList.add('active-row');

        if (this.value.trim() !== '') {
            // 【有名字】：弹小号设置
            openAliasPopover(this, this.value.trim());
        } else {
            // 【没名字】：弹所有的补全列表！
            processInputLogic(this, true);
        }

        // 自动将光标移动到文本末尾
        const currentVal = this.value;
        if (currentVal.length > 0) {
            setTimeout(() => this.setSelectionRange(currentVal.length, currentVal.length), 0);
        }
    });

    nameInput.addEventListener('blur', function () {
        const row = this.closest('.player-row');
        const autoPopover = row.querySelector('.autocomplete-popover');
        const aliasPopover = row.querySelector('.alias-popover');

        if (this.classList.contains('input-error')) {
            showAlert(this.getAttribute('data-error-msg'));
            this.value = ''; this.classList.remove('input-error');
        }

        const currentName = this.value.trim();
        this.dataset.previousMainName = currentName;

        const inlinePair = parseInlineMainAliasInput(currentName);
        if (inlinePair) {
            const aliasError = getAliasValidationError(inlinePair.aliasName);
            if (aliasError) {
                this.value = '';
                showAlert(`❌ ${aliasError}`);
                triggerSync();
                return;
            }
            const conflictOwner = findAliasConflict(inlinePair.mainName, inlinePair.aliasName, this);
            if (conflictOwner) {
                this.value = '';
                showAlert(`小号【${inlinePair.aliasName}】已被场上选手【${conflictOwner}】占用，无法添加！`);
                triggerSync();
                return;
            }
            this.value = inlinePair.mainName;
            bindAliasToPlayer(inlinePair.mainName, inlinePair.aliasName);
            openAliasPopover(this, inlinePair.mainName);
            triggerSync();
            return;
        }

        // 如果是“新主号首次绑定小号”弹窗导致的失焦，不能立刻 triggerSync；
        // 否则 getRowData 会因为小号为空把主号清空，C++ 再同步回来就会把输入框清掉。
        if (currentName && !hasAtLeastOneAlias(currentName) && pendingAliasPromptActive && pendingAliasPromptName === currentName) {
            return;
        }

        if (currentName && !hasAtLeastOneAlias(currentName)) {
            // 允许只输入主号留在选手框中；用红色高亮和运行按钮拦截提醒，不再清空输入框。
            openAliasPopover(this, currentName);
            triggerSync();
            return;
        }
        triggerSync();
    });

    let isComposing = false;
    nameInput.addEventListener('compositionstart', () => isComposing = true);
    nameInput.addEventListener('compositionend', function () { isComposing = false; updateNameInputPopovers(this); });

    nameInput.addEventListener('input', function () {
        if (!isComposing) {
            updateNameInputPopovers(this);
        }
    });

    function updateNameInputPopovers(inputElem) {
        const val = inputElem.value.trim();
        processInputLogic(inputElem, val === '');
        const hasExactMain = Object.prototype.hasOwnProperty.call(playerDB, val) ||
            Object.prototype.hasOwnProperty.call(savedDB, val);
        const autoPopover = inputElem.closest('.player-row')?.querySelector('.autocomplete-popover');
        const hasSuggestions = !!(autoPopover?.classList.contains('active') &&
            autoPopover.querySelector('.suggestion-item'));

        if (val === '' || inputElem.classList.contains('input-error')) {
            closeAliasPopoverForInput(inputElem);
        } else if (hasExactMain) {
            if (!playerDB[val] && savedDB[val]) playerDB[val] = uniqueAliasArray(savedDB[val]);
            openAliasPopover(inputElem, val, { keepAutoPopover: true });
        } else if (!hasSuggestions) {
            openAliasPopover(inputElem, val);
        } else {
            closeAliasPopoverForInput(inputElem);
        }
    }

    function processInputLogic(inputElem, forceShowAll) {
        // ========================================================
        // 🚨 终极强行同步：键盘每敲一下，就扫描全场，立刻恢复被“临时解绑”的下场选手！
        // ========================================================
        let activeNames = Array.from(document.querySelectorAll('.name-input'))
            .map(inp => inp.value.trim())
            .filter(name => name !== '');

        for (let name in savedDB) {
            // 只要发现永久库里的人没在场上，无条件瞬间恢复他的所有小号！
            if (!activeNames.includes(name)) {
                playerDB[name] = [...savedDB[name]];
            }
        }

        let val = inputElem.value.trim();
        let conflict = getFieldConflict(val, inputElem);

        if (val && conflict) {
            inputElem.classList.add('input-error');
            inputElem.setAttribute('data-error-msg', `❌ 无法上场！已被【${conflict.owner}】占用。\n原因：${conflict.reason}`);
            closeAliasPopoverForInput(inputElem); // 有冲突时强制关掉小号列表
        } else {
            inputElem.classList.remove('input-error');
            inputElem.removeAttribute('data-error-msg');
        }

        let activeMainSet = new Set(activeNames);
        let availableMains = Object.keys(playerDB).filter(name => !activeMainSet.has(name));
        let matches = !val ? (forceShowAll ? availableMains : []) : availableMains.filter(n => n.includes(val));

        currentFocusIndex = -1;

        if (matches.length > 0) {
            matches.sort((a, b) => a.localeCompare(b, 'zh-Hans-CN', { sensitivity: 'accent' }));
            autoPopover.innerHTML = matches.map(n => {
                const itemConflict = getFieldConflict(n, inputElem);
                const conflictText = itemConflict ? `已被【${itemConflict.owner}】占用：${itemConflict.reason}` : '';
                const itemClass = `popover-item suggestion-item${itemConflict ? ' suggestion-conflict' : ''}`;
                const conflictAttrs = itemConflict ? ` title="${escapeHtml(conflictText)}" data-conflict-reason="${escapeHtml(conflictText)}"` : '';
                const conflictBadge = itemConflict ? '<span class="suggestion-conflict-badge">冲突</span>' : '';
                return `<div class="${itemClass}" data-name="${escapeHtml(n)}"${conflictAttrs}><span class="suggestion-name">${escapeHtml(n)}</span>${conflictBadge}</div>`;
            }).join('');
            autoPopover.classList.add('active');

            autoPopover.querySelectorAll('.suggestion-item').forEach(item => {
                // 🚨 修复：将 click 改为 mousedown
                item.addEventListener('mousedown', (e) => {
                    // 🚨 核心：阻止默认事件！防止输入框失去焦点触发 blur 里的隐藏代码
                    e.preventDefault();
                    e.stopPropagation();

                    inputElem.value = item.dataset.name || item.innerText;

                    // 赋值后再跑一遍查重逻辑确保万无一失
                    processInputLogic(inputElem, false);

                    // 如果选中的人没冲突，就无缝切出他的小号列表
                    if (!inputElem.classList.contains('input-error')) {
                        ignoreNextDocumentClickUntil = Date.now() + 350;
                        autoPopover.classList.remove('active');
                        openAliasPopover(inputElem, inputElem.value);
                        setTimeout(() => {
                            if (pendingAliasPopoverInput === inputElem && pendingAliasPopoverName === inputElem.value.trim()) {
                                triggerSync();
                            }
                        }, 80);
                    }
                });
            });
        } else {
            autoPopover.classList.remove('active');
        }
    }

    return row;
}

// 🚨 拖拽数据安全交换函数 (直接在底层互换整个 DOM 节点，解决一切幽灵状态)
function swapDOMNodes(node1, node2) {
    // 创建一个临时占位符
    const marker = document.createElement('div');

    // 将占位符插入到 node1 的位置
    node1.parentNode.insertBefore(marker, node1);

    // 把 node1 挪到 node2 的位置
    node2.parentNode.insertBefore(node1, node2);

    // 把 node2 挪到占位符的位置
    marker.parentNode.insertBefore(node2, marker);

    // 清理占位符
    marker.parentNode.removeChild(marker);
}

// 辅助函数：数字控制 (支持滚轮、右键、以及手动打字输入)
function bindProNumberControls(inputElem, isAK = false) {
    inputElem.addEventListener('contextmenu', e => e.preventDefault());

    // 🚨 修复 1：获取焦点时自动全选，方便直接打字覆盖
    inputElem.addEventListener('focus', function () {
        if (this.value === '-') this.value = '';
        this.select();
    });

    inputElem.addEventListener('mousedown', function (e) {
        if (e.button !== 0 && e.button !== 2) return;

        // 🚨 修复 2：如果还没获取焦点，直接 return 放行！让鼠标能正常点进去打字！
        // 只有当它已经被选中（光标在里面）时，再次点击才会触发加减功能。
        if (document.activeElement !== this) return;

        e.preventDefault();
        let v = parseInt(this.value) || 0;
        if (e.button === 0) v++; else v--;
        this.value = (isAK && v <= 0) ? '-' : Math.max(0, v);
        this.select(); // 保持全选状态
        triggerSync();
    });

    inputElem.addEventListener('wheel', function (e) {
        // 只有在聚焦状态下滚轮才生效，防止平时网页上下滚动时误触改变战绩
        if (document.activeElement !== this) return;

        e.preventDefault();
        let v = parseInt(this.value) || 0;
        if (e.deltaY < 0) v++; else v--;
        this.value = (isAK && v <= 0) ? '-' : Math.max(0, v);
        this.select();
        triggerSync();
    });

    // 🚨 修复 3：手动打字完毕后，失去焦点时自动规范化数据并同步给 C++
    inputElem.addEventListener('blur', function () {
        let val = parseInt(this.value);
        if (isNaN(val) || val < 0) {
            this.value = isAK ? '-' : '0';
        } else {
            this.value = (isAK && val === 0) ? '-' : val;
        }
        triggerSync();
    });

    // 🚨 修复 4：支持打完字按回车立刻生效
    inputElem.addEventListener('keydown', function (e) {
        if (e.key === 'Enter') this.blur();
    });
}

function reopenAliasPopoverFromMenu(popElement, playerName) {
    const row = popElement?.closest('.player-row');
    const inputElem = row?.querySelector('.name-input');
    if (inputElem) {
        openAliasPopover(inputElem, playerName);
        return;
    }
    renderAliasMenu(playerName, popElement);
    popElement.classList.add('active');
    scheduleAliasPopoverLayout(popElement);
}

function renderAliasMenu(playerName, popElement) {
    playerDB[playerName] = uniqueAliasArray(playerDB[playerName]);
    savedDB[playerName] = uniqueAliasArray(savedDB[playerName]);
    let html = getCleanAliases(playerName).map((a, i) => {
        const legacyShort = isLegacyShortAliasWithoutMeta(a);
        const itemClass = legacyShort ? 'popover-item alias-row alias-legacy-short' : 'popover-item alias-row';
        const aliasTitle = legacyShort ? getLegacyShortAliasDeleteReason(a) : a;

        return `
        <div class="${itemClass}">
            <span class="alias-name" title="${escapeHtml(aliasTitle)}">${legacyShort ? '⚠️' : '🎮'} ${escapeHtml(a)}</span>
            <div class="alias-actions">
                <span class="btn-edit-alias" data-idx="${i}" title="修改小号名称，并同步修改永久小号库">✎</span>
                <span class="btn-temp-unbind" data-idx="${i}" title="临时解绑 (本次添加隐藏此ID不参与名称匹配，删除主号后重新添加即可恢复)">X</span>
                <span class="btn-perm-unbind" data-idx="${i}" title="永久解绑 (从库选手信息里面彻底删除)">🗑️</span>
            </div>
        </div>`;
    }).join('');
    html += `<div class="popover-item add-alias-btn">+ 绑定新小号</div>`;
    popElement.innerHTML = html;
    scheduleAliasPopoverLayout(popElement);

    // ==========================================
    // 1. 绑定新小号逻辑
    // ==========================================
    popElement.querySelector('.add-alias-btn').addEventListener('mousedown', (e) => {
        e.preventDefault(); e.stopPropagation();
        showAliasPrompt(playerName, (newAlias) => {
            if (newAlias && newAlias.trim() !== '') {
                const aliasTrimmed = newAlias.trim();
                const aliasError = getAliasValidationError(aliasTrimmed);
                if (aliasError) {
                    showAlert(`❌ ${aliasError}`);
                    return;
                }
                let conflictOwner = findAliasConflict(playerName, aliasTrimmed);
                if (conflictOwner) {
                    showAlert(`❌ 绑定失败！该小号已被场上选手【${conflictOwner}】占用！`);
                    return;
                }

                const existingAlias = findSamePlayerAliasBlock(getCleanAliases(playerName), aliasTrimmed);
                if (existingAlias) {
                    showAlert(`小号【${aliasTrimmed}】已存在或被包含于【${existingAlias}】，不会重复添加。`);
                    reopenAliasPopoverFromMenu(popElement, playerName);
                    return;
                }
                bindAliasToPlayer(playerName, aliasTrimmed);

                reopenAliasPopoverFromMenu(popElement, playerName);
                triggerSync();
            }
        });
    });

    // ==========================================
    // 2. 修改小号名称逻辑：当前选手列表 + 永久小号库一起修改
    // ==========================================
    popElement.querySelectorAll('.btn-edit-alias').forEach(btn => {
        btn.addEventListener('mousedown', (e) => {
            e.preventDefault(); e.stopPropagation();
            const idx = parseInt(e.target.getAttribute('data-idx'), 10);
            const oldAlias = getCleanAliases(playerName)[idx];
            if (!oldAlias) return;

            showAliasPrompt(playerName, (editedAlias) => {
                if (editedAlias === null) return;
                const aliasTrimmed = (editedAlias || '').trim();
                if (!aliasTrimmed) return;

                const aliasError = getAliasValidationError(aliasTrimmed);
                if (aliasError) {
                    showAlert(`❌ ${aliasError}`);
                    return;
                }

                if (aliasTrimmed === oldAlias) {
                    reopenAliasPopoverFromMenu(popElement, playerName);
                    return;
                }

                const samePlayerAliases = (playerDB[playerName] || []).filter((_, i) => i !== idx);
                const samePlayerAlias = findSamePlayerAliasBlock(samePlayerAliases, aliasTrimmed);
                if (samePlayerAlias) {
                    showAlert(`❌ 修改失败！该选手已有或包含小号【${samePlayerAlias}】。`);
                    return;
                }

                const conflictOwner = findAliasConflict(playerName, aliasTrimmed);
                if (conflictOwner) {
                    showAlert(`❌ 修改失败！该小号已被场上选手【${conflictOwner}】占用！`);
                    return;
                }

                updateAliasForPlayer(playerName, oldAlias, aliasTrimmed);
                reopenAliasPopoverFromMenu(popElement, playerName);
                triggerSync();
            }, `修改【${playerName}】的小号名称：`, oldAlias);
        });
    });

    // ==========================================
    // 2. 临时解绑逻辑 (只删 UI 内存，换人后自动恢复)
    // ==========================================
    popElement.querySelectorAll('.btn-temp-unbind').forEach(btn => {
        btn.addEventListener('mousedown', (e) => {
            e.preventDefault(); e.stopPropagation();
            const idx = e.target.getAttribute('data-idx');
            const targetAlias = getCleanAliases(playerName)[idx];

            // 允许临时解绑最后一个小号；选手保留，选手框会变红，运行按钮会被禁用。
            // 从当前活跃库中移除
            playerDB[playerName].splice(idx, 1);

            reopenAliasPopoverFromMenu(popElement, playerName);

            // 触发同步（这会告诉 C++ 场上目前没这个小号了，但不会从底层库里抹除它）
            triggerSync();
        });
    });

    // ==========================================
    // 3. 永久解绑逻辑 (通知 C++ 彻底删库)
    // ==========================================
    popElement.querySelectorAll('.btn-perm-unbind').forEach(btn => {
        btn.addEventListener('mousedown', (e) => {
            e.preventDefault(); e.stopPropagation();
            const idx = e.target.getAttribute('data-idx');
            const targetAlias = getCleanAliases(playerName)[idx];

            const confirmText = `⚠️ 确定要【永久删除】小号 [${targetAlias}] 吗？

如果这是最后一个小号，选手会保留在列表中，但运行按钮会变灰，直到重新绑定小号。`;

            showConfirm(confirmText, (isOk) => {
                if (isOk) {
                    // 发送专属的终极删除指令给 C++
                    if (window.chrome && window.chrome.webview) {
                        window.chrome.webview.postMessage({
                            action: "cmd_delete_alias",
                            mainName: playerName,
                            aliasName: targetAlias
                        });
                    }

                    // 前端同步清理内存，保证 UI 瞬间反应
                    playerDB[playerName].splice(idx, 1);
                    if (savedDB[playerName]) {
                        let sIdx = savedDB[playerName].indexOf(targetAlias);
                        if (sIdx > -1) savedDB[playerName].splice(sIdx, 1);
                    }

                    reopenAliasPopoverFromMenu(popElement, playerName);
                    pushStateToServer();
                    if (isCloudDirectMode) queueDirectAliasDbSync(buildFormattedAliasDB(), true);
                }
            });
        });
    });
}

// ==========================================
// 5. 初始化与全局事件
// ==========================================
const blueTeam = document.getElementById('team-blue');
const redTeam = document.getElementById('team-red');
for (let i = 0; i < 4; i++) {
    redTeam.querySelector('.rows-container').appendChild(createPlayerRow(i + 1));
    blueTeam.querySelector('.rows-container').appendChild(createPlayerRow(i + 5));
}
refreshPickLabels();

document.querySelectorAll('.team-score-input').forEach(input => { input.type = 'text'; bindProNumberControls(input); });
updateStartButtonGuard();

document.addEventListener('click', (e) => {
    if (Date.now() < ignoreNextDocumentClickUntil) return;
    if (
        e.target.closest('#custom-modal') ||
        e.target.closest('.popover') ||
        e.target.closest('.more-controls-wrap') ||
        e.target.closest('.more-controls-menu') ||
        e.target.classList.contains('name-input') ||
        e.target.classList.contains('gear-btn')
    ) return;
    setMoreControlsOpen(false);
    document.querySelectorAll('.popover').forEach(p => p.classList.remove('active'));
    clearPendingAliasPopoverLock();
    // 点击任意空白处，撤销所有选手的置顶层级
    document.querySelectorAll('.player-row').forEach(r => r.classList.remove('active-row'));
});

document.addEventListener('input', (e) => {
    if (e.target && e.target.classList && e.target.classList.contains('name-input')) {
        updateStartButtonGuard();
    }
});

document.getElementById('btn-swap').addEventListener('click', () => window.chrome.webview.postMessage({ action: "cmd_swap" }));
document.getElementById('btn-random-teams')?.addEventListener('click', openRandomTool);
document.getElementById('btn-key-mapping')?.addEventListener('click', openKeyMappingPanel);
document.getElementById('btn-key-lan')?.addEventListener('click', openKeyLanPanel);
document.getElementById('btn-cloud-match')?.addEventListener('click', () => openCloudRoomPanel(false));
document.getElementById('btn-more-controls')?.addEventListener('click', (e) => {
    e.stopPropagation();
    toggleMoreControlsMenu();
});
document.getElementById('more-controls-menu')?.addEventListener('click', (e) => {
    const actionButton = e.target.closest('button.ctrl-btn');
    if (actionButton) setMoreControlsOpen(false);
});
document.getElementById('btn-monitor').addEventListener('click', () => {
    if (isStartPending && !isMonitoring) {
        showAlert('OCR 服务正在启动中，请稍候...');
        return;
    }

    const violations = updateStartButtonGuard();
    if (!isMonitoring && violations.length > 0) {
        showAlert(getStartGuardMessage());
        return;
    }

    if (!isMonitoring && deathXAlgorithm === 1 && !deathPatchInstalled) {
        const msg = `⚠️ 当前选择的是【打补丁红蓝判断】。<br><br>` +
            `开始监控前需要把 EXE 同目录下的 <b>sprite(击杀大XX).NPK</b> 复制到游戏的 <b>ImagePacks2</b> 文件夹。<br><br>` +
            `请先关闭游戏客户端；如果游戏正在运行，复制后也需要重新上游戏才会生效。<br><br>` +
            `点击【确定】开始自动打补丁并继续监控；点击【取消】则不打补丁，也不会开始监控。`;
        showConfirm(msg, (ok) => {
            if (!ok) {
                updateStartButtonGuard();
                return;
            }
            window.chrome.webview.postMessage({ action: "cmd_monitor", state: true });
        });
        return;
    }

    window.chrome.webview.postMessage({ action: "cmd_monitor", state: !isMonitoring });
});
const deathAlgoSelect = document.getElementById('death-algo-select');
if (deathAlgoSelect) {
    deathAlgoSelect.addEventListener('change', () => {
        deathXAlgorithm = parseInt(deathAlgoSelect.value, 10) || 0;
        if (window.chrome?.webview) {
            window.chrome.webview.postMessage({ action: "cmd_set_death_algorithm", value: deathXAlgorithm });
        }
    });
}

document.getElementById('btn-auth').addEventListener('click', () => { showPrompt("请输入授权卡密 (CDK):", (c) => { if (c) window.chrome.webview.postMessage({ action: "cmd_auth", code: c.trim() }); }); });
document.getElementById('btn-kill-display-toggle')?.addEventListener('click', () => {
    if (window.chrome?.webview) window.chrome.webview.postMessage({ action: 'cmd_toggle_kill_display' });
});
document.getElementById('btn-key-mapping-close')?.addEventListener('click', closeKeyMappingPanel);
document.getElementById('key-mapping-overlay')?.addEventListener('click', (event) => {
    if (event.target?.id === 'key-mapping-overlay') closeKeyMappingPanel();
});
document.getElementById('btn-key-lan-close')?.addEventListener('click', closeKeyLanPanel);
document.getElementById('key-lan-overlay')?.addEventListener('click', (event) => {
    if (event.target?.id === 'key-lan-overlay') closeKeyLanPanel();
});
document.getElementById('btn-cloud-room-close')?.addEventListener('click', () => closeCloudRoomPanel(false));
document.getElementById('cloud-room-overlay')?.addEventListener('click', (event) => {
    if (event.target?.id === 'cloud-room-overlay') closeCloudRoomPanel(false);
});
document.querySelectorAll('.cloud-room-option').forEach(option => {
    option.addEventListener('click', () => {
        if (isCloudRoomBusy()) return;
        const roomId = option.dataset.cloudRoomId || '';
        if (roomId === 'none') {
            cloudRoomJoinTarget = null;
            if (cloudMatchState?.joined) {
                cloudRoomChoosing = false;
                cloudSelectedRoomId = '';
                renderCloudRoomPanel();
            } else {
                sendCloudRoomCommand('cmd_cloud_room_skip_once');
                if (cloudMatchState) cloudMatchState.shouldPrompt = false;
                closeCloudRoomPanel(true);
            }
            return;
        }
        if (!Object.prototype.hasOwnProperty.call(CLOUD_ROOM_NAMES, roomId)) return;
        cloudSelectedRoomId = roomId;
        cloudRoomJoinTarget = null;
        setCloudRoomInlineError('');
        const input = document.getElementById('cloud-room-name-input');
        if (input && !input.value.trim() && cloudMatchState?.broadcasterName) {
            input.value = cloudMatchState.broadcasterName;
        }
        renderCloudRoomPanel();
        input?.focus();
    });
});
document.getElementById('cloud-room-name-input')?.addEventListener('input', () => {
    if (cloudMatchState) cloudMatchState.lastError = '';
    updateCloudRoomNameValidation();
});
document.getElementById('btn-cloud-room-back')?.addEventListener('click', () => {
    if (isCloudRoomBusy()) return;
    cloudSelectedRoomId = '';
    cloudRoomJoinTarget = null;
    setCloudRoomInlineError('');
    renderCloudRoomPanel();
});
document.getElementById('btn-cloud-room-join')?.addEventListener('click', () => {
    if (isCloudRoomBusy()) return;
    const input = document.getElementById('cloud-room-name-input');
    const info = cloudRoomNameInfo(input?.value || '');
    if (!cloudSelectedRoomId || !info.valid) {
        updateCloudRoomNameValidation();
        if (!info.normalized) setCloudRoomInlineError('请输入主播名称。');
        return;
    }
    cloudRoomJoinTarget = {
        roomId: cloudSelectedRoomId,
        broadcasterName: info.normalized
    };
    cloudMatchState = normalizeCloudMatchState({ ...(cloudMatchState || {}),
        joining: true, lastError: '' });
    renderCloudRoomPanel();
    sendCloudRoomCommand('cmd_cloud_room_join', {
        roomId: cloudSelectedRoomId,
        broadcasterName: info.normalized
    });
});
document.getElementById('btn-cloud-room-cancel-join')?.addEventListener('click', () => {
    if (!cloudMatchState?.joining && !cloudMatchState?.registering) return;
    cloudRoomJoinTarget = null;
    const button = document.getElementById('btn-cloud-room-cancel-join');
    if (button) {
        button.disabled = true;
        button.textContent = '取消中...';
    }
    sendCloudRoomCommand('cmd_cloud_room_cancel_join');
});
document.getElementById('btn-cloud-room-rename')?.addEventListener('click', () => {
    if (isCloudRoomBusy()) return;
    const input = document.getElementById('cloud-room-rename-input');
    const info = cloudRoomNameInfo(input?.value || '');
    if (!info.valid) {
        showAlert('主播名称需要填写 1 到 32 个可见字符。');
        return;
    }
    cloudMatchState = normalizeCloudMatchState({ ...(cloudMatchState || {}),
        renaming: true, lastError: '' });
    renderCloudRoomPanel();
    sendCloudRoomCommand('cmd_cloud_room_rename', { broadcasterName: info.normalized });
});
document.getElementById('btn-cloud-room-change')?.addEventListener('click', () => {
    if (isCloudRoomBusy()) return;
    cloudRoomChoosing = true;
    cloudSelectedRoomId = '';
    cloudRoomJoinTarget = null;
    setCloudRoomInlineError('');
    renderCloudRoomPanel();
});
document.getElementById('btn-cloud-room-leave')?.addEventListener('click', () => {
    if (isCloudRoomBusy()) return;
    showConfirm('退出后会停止向当前云端房间上传，但不会清空本机比分、名单或战绩。确定退出吗？', (ok) => {
        if (!ok || isCloudRoomBusy()) return;
        cloudMatchState = normalizeCloudMatchState({ ...(cloudMatchState || {}),
            leaving: true, lastError: '' });
        renderCloudRoomPanel();
        sendCloudRoomCommand('cmd_cloud_room_leave');
    }, { okText: '退出房间', cancelText: '取消' });
});
document.getElementById('key-mapping-enabled')?.addEventListener('change', function () {
    keyMappingSettings.enabled = this.checked;
    sendKeyMappingSettings(true);
    renderKeyMappingPanel();
});
document.getElementById('btn-key-display-toggle')?.addEventListener('click', () => {
    window.chrome?.webview?.postMessage({ action: 'cmd_toggle_key_display' });
});
document.getElementById('key-lan-role')?.addEventListener('change', function () {
    const role = ['standalone', 'server', 'client'].includes(this.value) ? this.value : 'standalone';
    keyMappingSettings.lan.role = role;
    keyMappingSettings.lan.running = false;
    keyMappingSettings.lan.connected = false;
    renderKeyLanPanel();
    sendKeyLanCommand('cmd_set_key_lan_role', { role });
});
document.getElementById('btn-key-lan-server-toggle')?.addEventListener('click', () => {
    const lan = keyMappingSettings.lan;
    if (lan.running) {
        sendKeyLanCommand('cmd_stop_key_lan_server');
        return;
    }
    const port = Math.max(1024, Math.min(65535,
        Number(document.getElementById('key-lan-server-port').value || 18778)));
    sendKeyLanCommand('cmd_start_key_lan_server', { port });
});
document.getElementById('btn-key-lan-regenerate')?.addEventListener('click', () => {
    showConfirm('重新生成配对码后，已保存旧配对码的客户端需要输入新码。确定继续吗？', (ok) => {
        if (ok) sendKeyLanCommand('cmd_regenerate_key_pair_code');
    });
});
document.getElementById('btn-key-lan-discover')?.addEventListener('click', () => {
    const port = Math.max(1024, Math.min(65535,
        Number(document.getElementById('key-lan-client-port').value || 18778)));
    sendKeyLanCommand('cmd_discover_key_lan_servers', { port });
});
document.getElementById('key-lan-server-list')?.addEventListener('change', function () {
    const option = this.selectedOptions[0];
    if (!option?.dataset?.address) return;
    document.getElementById('key-lan-server-address').value = option.dataset.address;
    document.getElementById('key-lan-client-port').value = option.dataset.port || '18778';
});
document.getElementById('key-lan-pair-code')?.addEventListener('input', function () {
    this.value = this.value.replace(/\D/g, '').slice(0, 4);
});
document.getElementById('btn-key-lan-client-toggle')?.addEventListener('click', () => {
    const lan = keyMappingSettings.lan;
    if (lan.running) {
        sendKeyLanCommand('cmd_disconnect_key_lan');
        return;
    }
    const address = document.getElementById('key-lan-server-address').value.trim();
    const pairCode = document.getElementById('key-lan-pair-code').value.trim();
    const port = Math.max(1024, Math.min(65535,
        Number(document.getElementById('key-lan-client-port').value || 18778)));
    if (!address || !/^\d{4}$/.test(pairCode)) {
        showAlert('请输入服务器 IP 和 4 位配对码。');
        return;
    }
    sendKeyLanCommand('cmd_connect_key_lan', { address, pairCode, port });
});
document.getElementById('btn-key-team-sync')?.addEventListener('click', () => {
    if (!keyMappingSettings?.lan?.connected) {
        showAlert('请先连接局域网服务器。');
        return;
    }
    pendingTeamSyncSnapshot = null;
    pushStateToServer();
    sendKeyLanCommand('cmd_request_team_sync');
});
document.getElementById('btn-key-team-undo-sync')?.addEventListener('click', () => {
    showConfirm('确定撤销本次比赛状态同步，恢复同步前的本地比分、队伍和战绩吗？', (ok) => {
        if (ok) sendKeyLanCommand('cmd_undo_team_sync');
    }, { okText: '确认撤销', cancelText: '取消' });
});
document.getElementById('team-sync-auto-receive')?.addEventListener('change', function () {
    const enabled = this.checked;
    keyMappingSettings.lan.teamSyncAutoReceive = enabled;
    keyMappingSettings.lan.teamSyncCanUndo = false;
    pendingTeamSyncSnapshot = null;
    renderKeyLanPanel();
    sendKeyLanCommand('cmd_set_team_sync_auto_receive', { enabled });
});
document.getElementById('team-sync-allow-client-write')?.addEventListener('change', function () {
    keyMappingSettings.lan.teamSyncAllowClientWrite = this.checked;
    renderKeyLanPanel();
    sendKeyLanCommand('cmd_set_team_sync_allow_client_write', { enabled: this.checked });
});
document.getElementById('team-sync-auto-send')?.addEventListener('change', function () {
    const enabled = this.checked;
    keyMappingSettings.lan.teamSyncAutoSend = enabled;
    if (enabled) keyMappingSettings.lan.teamSyncAutoReceive = true;
    renderKeyLanPanel();
    sendKeyLanCommand('cmd_set_team_sync_auto_send', { enabled });
});
document.getElementById('btn-key-reset-defaults')?.addEventListener('click', resetKeyMappingDefaults);
document.getElementById('key-binding-capture')?.addEventListener('click', () => beginKeyBindingCapture());
document.getElementById('btn-key-binding-clear')?.addEventListener('click', () => {
    const slot = keyMappingSettings.slots[selectedKeyMappingSlot];
    slot.vk = 0;
    slot.label = '';
    keyMappingCaptureSlot = -1;
    renderKeyMappingPanel();
    sendKeyMappingSettings(true);
});
['key-slot-color', 'key-slot-color-text', 'key-slot-opacity', 'key-slot-opacity-value'].forEach(id => {
    const element = document.getElementById(id);
    if (!element) return;
    element.addEventListener(element.type === 'text' || element.type === 'number' ? 'change' : 'input', () => {
        applyKeySlotStyleFromInputs(id);
    });
});
document.getElementById('btn-key-style-apply-all')?.addEventListener('click', () => {
    const source = keyMappingSettings.slots[selectedKeyMappingSlot];
    keyMappingSettings.slots.forEach(slot => {
        slot.color = source.color;
        slot.opacity = source.opacity;
    });
    renderKeyMappingPanel();
    sendKeyMappingSettings(true);
});
document.getElementById('kill-show-death-toggle-main')?.addEventListener('change', function () {
    setKillDisplayShowDeathNumber(this.checked);
});
document.getElementById('btn-sync-alias-db')?.addEventListener('click', () => {
    showConfirm('确定从云端公共库同步小号数据吗？<br><br>只会合并审核通过的数据，不会删除你本地已有的小号。', (ok) => {
        if (!ok || !window.chrome?.webview) return;
        window.chrome.webview.postMessage({ action: "cmd_sync_alias_db" });
    });
});
document.getElementById('btn-push-alias-db')?.addEventListener('click', () => {
    showConfirm('确定把本地小号库推送到云端待审核吗？<br><br>云端会对比共享库生成新增/删除差异，只有本地库发生变化时才会真正提交。', (ok) => {
        if (!ok || !window.chrome?.webview) return;
        const fullAliasDB = buildFormattedAliasDB();
        window.chrome.webview.postMessage({
            action: "cmd_push_alias_db",
            data: { fullAliasDB }
        });
    });
});
document.getElementById('btn-pro').addEventListener('click', () => window.chrome.webview.postMessage({ action: "cmd_toggle_mfc", show: !isProMode }));
document.getElementById('btn-appearance')?.addEventListener('click', openAppearancePanel);
document.getElementById('btn-appearance-close')?.addEventListener('click', closeAppearancePanel);
document.getElementById('appearance-overlay')?.addEventListener('click', (e) => {
    if (e.target?.id === 'appearance-overlay') closeAppearancePanel();
});
document.getElementById('btn-style-reset-current')?.addEventListener('click', resetCurrentAppearanceStyle);
document.getElementById('btn-style-reset-all')?.addEventListener('click', resetAllAppearanceSettings);
document.getElementById('btn-kill-display-open')?.addEventListener('click', () => {
    if (window.chrome?.webview) window.chrome.webview.postMessage({ action: 'cmd_open_kill_display' });
});
document.getElementById('btn-kill-obs-copy')?.addEventListener('click', () => {
    if (window.chrome?.webview) window.chrome.webview.postMessage({ action: 'cmd_copy_kill_obs_url' });
});
[
    'style-font-family',
    'style-font-size',
    'style-font-size-value',
    'style-color-mode',
    'style-color',
    'style-color-text',
    'style-stroke-color',
    'style-stroke-color-text',
    'style-stroke-width',
    'style-stroke-width-value',
    'style-glow',
    'style-glow-value'
].forEach(id => {
    const el = document.getElementById(id);
    if (!el) return;
    const eventName = el.type === 'range' || el.type === 'color' ? 'input' : 'change';
    el.addEventListener(eventName, () => updateActiveStyleFromEditor(id));
});
document.getElementById('output-seat-label-toggle')?.addEventListener('change', function () {
    outputSeatLabelToKillFile = !!this.checked;
    if (window.chrome?.webview) {
        window.chrome.webview.postMessage({ action: 'cmd_set_output_seat_label', enabled: outputSeatLabelToKillFile });
    }
});

function normalizeFunctionKey(e) {
    if (e.key === 'F1' || e.code === 'F1') return 'F1';
    if (e.key === 'F12' || e.code === 'F12') return 'F12';
    if (e.key === 'Control' || e.code === 'ControlLeft' || e.code === 'ControlRight') return 'Control';
    return '';
}

document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
        setMoreControlsOpen(false);
    }

    const key = normalizeFunctionKey(e);
    if (!key) return;
    if (isAppearancePanelOpen()) return;
    cloudDirectPressedKeys.add(key);

    if (key === 'F1' || key === 'F12') {
        e.preventDefault();
        e.stopImmediatePropagation();
    }

    if ((e.ctrlKey || cloudDirectPressedKeys.has('Control')) &&
        cloudDirectPressedKeys.has('F1') &&
        cloudDirectPressedKeys.has('F12') &&
        !cloudDirectToggleLock) {
        cloudDirectToggleLock = true;
        e.preventDefault();
        e.stopImmediatePropagation();
        toggleCloudDirectMode();
    }
}, true);

document.addEventListener('keyup', (e) => {
    const key = normalizeFunctionKey(e);
    if (key) cloudDirectPressedKeys.delete(key);
    if (!cloudDirectPressedKeys.has('F1') || !cloudDirectPressedKeys.has('F12') || !e.ctrlKey) {
        cloudDirectToggleLock = false;
    }
}, true);

// 🚨 绑定新加的“更改目录”事件
document.getElementById('dir-display').addEventListener('click', () => {
    if (window.chrome?.webview) window.chrome.webview.postMessage({ action: "cmd_browse_dir" });
});

document.getElementById('review-list')?.addEventListener('click', (e) => {
    const undoBtn = e.target.closest('.btn-review-undo');
    if (undoBtn) {
        const eventId = undoBtn.getAttribute('data-id');
        const actionText = undoBtn.textContent.trim() === '恢复' ? '恢复' : '撤销';
        showConfirm(`确定${actionText}这条自动识别战绩吗？`, (ok) => {
            if (ok) sendReviewUndo(eventId);
        });
    }
});
document.getElementById('btn-review-toggle')?.addEventListener('click', toggleReviewPanel);
document.getElementById('btn-review-close')?.addEventListener('click', () => setReviewPanelOpen(false));
document.getElementById('review-backdrop')?.addEventListener('click', () => setReviewPanelOpen(false));
document.getElementById('btn-console-toggle')?.addEventListener('click', toggleConsolePanel);
document.getElementById('btn-console-close')?.addEventListener('click', () => setConsolePanelOpen(false));
document.getElementById('btn-console-clear')?.addEventListener('click', () => {
    cxxConsoleLogs = [];
    renderConsoleLogs();
});

document.getElementById('btn-random-tool-close')?.addEventListener('click', closeRandomTool);
document.getElementById('random-tool-overlay')?.addEventListener('mousedown', (e) => {
    if (e.target?.id === 'random-tool-overlay') closeRandomTool();
});
const randomRosterInput = document.getElementById('random-roster-input');
randomRosterInput?.addEventListener('input', handleRandomRosterInput);
randomRosterInput?.addEventListener('keyup', updateRandomRosterSuggestions);
randomRosterInput?.addEventListener('click', updateRandomRosterSuggestions);
randomRosterInput?.addEventListener('focus', updateRandomRosterSuggestions);
randomRosterInput?.addEventListener('blur', () => {
    setTimeout(() => document.getElementById('random-roster-suggestions')?.classList.remove('active'), 120);
});
document.getElementById('random-group-sizes')?.addEventListener('input', () => {
    invalidateRandomToolResult('分组数量已更新，请重新随机。');
});
document.getElementById('random-draw-count')?.addEventListener('input', () => {
    invalidateRandomToolResult('抽签人数已更新，请重新抽签或随机分组。');
});
document.getElementById('btn-random-run')?.addEventListener('click', runRandomToolGrouping);
document.getElementById('btn-random-draw')?.addEventListener('click', drawRandomParticipant);
document.getElementById('btn-random-apply')?.addEventListener('click', applyRandomResultToTeams);
document.getElementById('btn-random-copy')?.addEventListener('click', copyRandomResult);
document.getElementById('btn-random-reset')?.addEventListener('click', resetRandomToolToInitialState);

function resetSeatNumbers() {
    resetSeatLabelsToDefault();
}

function clearAllTeamsData() {
    if (isMonitoring) {
        showAlert('请先停止监控，再清空场上数据。');
        return;
    }
    pendingClearAllUntil = Date.now() + 3000;
    resetRandomGroupTransientUi();
    closeRandomTool();
    document.querySelectorAll('.name-input').forEach(input => {
        input.value = '';
        input.classList.remove('input-error');
        input.removeAttribute('data-error-msg');
    });
    document.querySelectorAll('.stat-kill, .stat-death').forEach(input => input.value = '0');
    document.querySelectorAll('.stat-ak').forEach(input => input.value = '-');
    document.querySelectorAll('.team-score-input').forEach(input => input.value = '0');
    resetSeatNumbers();
    recentEvents = [];
    renderReviewEvents();
    if (window.chrome?.webview) {
        window.chrome.webview.postMessage({ action: 'cmd_reset_stats', clearPlayers: true });
        triggerSync();
        [80, 250, 800, 1800].forEach(delay => setTimeout(triggerSync, delay));
        setTimeout(() => {
            pendingClearAllUntil = 0;
            triggerSync();
        }, 2600);
    } else {
        triggerSync();
        pendingClearAllUntil = 0;
    }
    showAlert('清空成功：场上座位、战绩、大比分、最近识别和冷却已清空。');
}
document.getElementById('btn-clear-teams')?.addEventListener('click', () => {
    showConfirm('⚠️ 确定清空当前场上 8 个座位、战绩、大比分、最近识别和冷却吗？', (res) => {
        if (res) clearAllTeamsData();
    });
});
document.getElementById('btn-reset').addEventListener('click', () => {
    showConfirm('确定重置所有战绩吗？', (res) => {
        if (res) {
            document.querySelectorAll('.stat-kill, .stat-death').forEach(i => i.value = '0');
            document.querySelectorAll('.stat-ak').forEach(i => i.value = '-');
            document.querySelectorAll('.team-score-input').forEach(i => i.value = '0');
            resetSeatLabelsToDefault();
            recentEvents = [];
            renderReviewEvents();
            if (window.chrome?.webview) {
                window.chrome.webview.postMessage({ action: 'cmd_reset_stats' });
            } else {
                triggerSync();
            }
        }
    });
});

// 1) 先把所有战绩输入框的 Tab 顺序移除，这样 Tab 永远跳不到它们
document.querySelectorAll('.stat-kill, .stat-death, .stat-ak').forEach(inp => {
    inp.setAttribute('tabindex', '-1');
});

document.addEventListener('keydown', function (e) {
    if (isAppearancePanelOpen()) {
        if (e.key === 'Escape') {
            e.preventDefault();
            closeAppearancePanel();
        }
        return;
    }
    // ==========================================
    // 1. Tab：名字输入框之间循环切换
    // ==========================================
    if (e.key === 'Tab') {
        if (customModal.classList.contains('active')) return;
        const focused = document.activeElement;
        if (!focused || !focused.classList.contains('name-input')) return;
        e.preventDefault();
        const nameInputs = Array.from(document.querySelectorAll('.name-input'));
        const total = nameInputs.length;
        let currentIdx = nameInputs.indexOf(focused);
        const forward = !e.shiftKey;
        const nextIdx = forward ? (currentIdx + 1) % total : (currentIdx - 1 + total) % total;
        nameInputs[nextIdx].focus();
    }
    // ==========================================
    // 2. Escape：对话框关闭 / 输入框失焦
    // ==========================================
    else if (e.key === 'Escape') {
        // 对话框激活：由对话框自己的监听器处理（那边已经 stopImmediatePropagation）
        if (customModal.classList.contains('active')) return;
        if (isCloudRoomPanelOpen()) {
            e.preventDefault();
            closeCloudRoomPanel(false);
            return;
        }
        if (isKeyMappingPanelOpen()) {
            e.preventDefault();
            closeKeyMappingPanel();
            return;
        }
        if (isKeyLanPanelOpen()) {
            e.preventDefault();
            closeKeyLanPanel();
            return;
        }
        if (isReviewPanelOpen) {
            e.preventDefault();
            setReviewPanelOpen(false);
            return;
        }
        if (isConsolePanelOpen) {
            e.preventDefault();
            setConsolePanelOpen(false);
            return;
        }
        if (document.querySelector('.alias-popover.active')) {
            e.preventDefault();
            closeAliasPopovers();
            document.querySelectorAll('.player-row').forEach(r => r.classList.remove('active-row'));
            return;
        }

        const focused = document.activeElement;
        // 当前有输入焦点 → 失焦（自动保存、关闭弹窗）
        if (focused && (focused.tagName === 'INPUT' || focused.tagName === 'TEXTAREA')) {
            e.preventDefault();
            focused.blur();
        }
    }
    // ==========================================
    // 3. 方向键：无焦点时自动聚焦第一个名字框
    // ==========================================
    else if (e.key === 'ArrowUp' || e.key === 'ArrowDown' || e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
        // 对话框激活时不理会
        if (customModal.classList.contains('active')) return;

        const active = document.activeElement;
        // 如果已经聚焦在某个输入框或文本域，方向键交给原本的控件行为处理（比如名字框内移动光标、下拉切换等）
        if (active && (active.tagName === 'INPUT' || active.tagName === 'TEXTAREA')) return;

        // 没有任何输入焦点 → 把焦点给第一个名字输入框
        const firstInput = document.querySelector('.name-input');
        if (firstInput) {
            e.preventDefault();          // 阻止页面滚动（如果你不希望页面上下左右移动）
            firstInput.focus();
            // 如果名字框里已经有内容，全选方便直接覆盖；如果是空的，光标自然在开头
            if (firstInput.value) {
                firstInput.select();
            }
        }
    }
});

document.addEventListener('keyup', function (e) {
    if (isAppearancePanelOpen()) return;
    if (e.key === 'Control' && ctrlSwapState.active) {
        // 执行交换
        if (ctrlSwapState.targetRow && ctrlSwapState.sourceRow !== ctrlSwapState.targetRow) {
            swapDOMNodes(ctrlSwapState.sourceRow, ctrlSwapState.targetRow);
            refreshPickLabels();
            triggerSync();
        }
        // 清理高亮样式
        if (ctrlSwapState.sourceRow) ctrlSwapState.sourceRow.classList.remove('drag-source');
        if (ctrlSwapState.targetRow) ctrlSwapState.targetRow.classList.remove('drag-target');

        // 焦点切回源输入框（此时它已随 DOM 移动到目标位置）
        const sourceInput = ctrlSwapState.sourceRow?.querySelector('.name-input');
        if (sourceInput) setTimeout(() => sourceInput.focus(), 0);

        // 重置状态
        ctrlSwapState.active = false;
        ctrlSwapState.sourceRow = null;
        ctrlSwapState.targetRow = null;
        ctrlSwapState.currentIndex = -1;
    }
});

if (window.ResizeObserver) {
    const webSizeObserver = new ResizeObserver(() => requestWebWindowResize());
    webSizeObserver.observe(document.body);
}
window.addEventListener('load', () => scheduleLayoutFit(true, 'load'));
window.addEventListener('resize', requestWebWindowResize);
window.addEventListener('resize', layoutActiveAliasPopovers);
setTimeout(() => scheduleLayoutFit(true, 'startup-300ms'), 300);
