// ==========================================
// 1. 核心：WebView2 同步引擎
// ==========================================
let playerDB = {};
let savedDB = {};
let isSyncingFromServer = false;
let hasReceivedInitialData = false;
let isMonitoring = false;
let isProMode = false;
let draggedRow = null;
let isDbInitialized = false;
// Web 端编辑小号后，C++ 可能会立刻推回一次旧状态；这里短时间记录改名映射，避免旧小号被同步回来。
let pendingAliasRenameRecords = [];
// 新主号首次绑定小号时，弹窗会让输入框失焦；用这个标记避免 blur 提前同步空小号状态。
let pendingAliasPromptActive = false;
let pendingAliasPromptName = '';

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

function uniqueAliasArray(arr) {
    const out = [];
    const seen = new Set();
    (arr || []).forEach(item => {
        const clean = normalizeAliasTextForCompare(item);
        if (!clean) return;
        const key = clean;
        if (seen.has(key)) return;
        seen.add(key);
        out.push(clean);
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
    return uniqueAliasArray((arr || []).filter(a => normalizeAliasTextForCompare(a) !== target));
}

function applyPendingAliasRenamesToDb(dbObj) {
    const now = Date.now();
    pendingAliasRenameRecords = pendingAliasRenameRecords.filter(r => r && r.until > now);
    for (const r of pendingAliasRenameRecords) {
        if (!r.playerName || !dbObj[r.playerName]) continue;
        dbObj[r.playerName] = removeAliasFromArray(dbObj[r.playerName], r.oldAlias);
        const newAlias = normalizeAliasTextForCompare(r.newAlias);
        dbObj[r.playerName] = uniqueAliasArray(dbObj[r.playerName]);
        if (newAlias && !dbObj[r.playerName].includes(newAlias)) dbObj[r.playerName].push(newAlias);
        dbObj[r.playerName] = uniqueAliasArray(dbObj[r.playerName]);
    }
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
                            newFromMFC.forEach(a => { if (!updatedPlayerDB.includes(a)) updatedPlayerDB.push(a); });
                            updatedPlayerDB = updatedPlayerDB.filter(a => !deletedFromMFC.includes(a));

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

                let activeRowInput = document.querySelector('.player-row.active-row .name-input');
                let aliasPopover = document.querySelector('.alias-popover.active');
                if (activeRowInput && aliasPopover) {
                    let activeName = activeRowInput.value.trim();
                    if (activeName && playerDB[activeName]) {
                        renderAliasMenu(activeName, aliasPopover);
                        aliasPopover.classList.add('active');
                    }
                }
            }
            else if (msg.action === 'auth_result') { showAlert(msg.message); }
        } catch (e) { console.error('解析 C++ 消息失败', e); }
    });

    let handshakeTimer = setInterval(() => {
        if (!hasReceivedInitialData) window.chrome.webview.postMessage({ action: "page_ready" });
        else clearInterval(handshakeTimer);
    }, 500);
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

    let formattedDB = {};
    // 🚨 注意：发给 C++ 的永远是不受“临时解绑”影响的永久库
    normalizeAllAliasStores();
    for (let key in savedDB) {
        savedDB[key] = uniqueAliasArray(savedDB[key]);
        if (savedDB[key] && savedDB[key].length > 0) {
            formattedDB[key] = formatAliasArrayForCpp(savedDB[key]);
        }
    }

    let state = {
        blueScore: parseInt(document.querySelector('#team-blue .team-score-input').value) || 0,
        redScore: parseInt(document.querySelector('#team-red .team-score-input').value) || 0,
        players: [],
        fullAliasDB: formattedDB
    };

    document.querySelectorAll('#team-red .player-row').forEach(row => state.players.push(getRowData(row, 0)));
    document.querySelectorAll('#team-blue .player-row').forEach(row => state.players.push(getRowData(row, 1)));
    window.chrome.webview.postMessage({ action: "update_state", data: state });
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

function applyStateFromServer(state) {
    isSyncingFromServer = true;

    isMonitoring = state.isMonitoring;
    const btnMonitor = document.getElementById('btn-monitor');
    btnMonitor.innerHTML = isMonitoring ? '🛑 停止' : '▶ 运行';
    btnMonitor.className = isMonitoring ? 'ctrl-btn btn-monitor-stop' : 'ctrl-btn btn-monitor-start';

    const container = document.getElementById('main-container');
    container.style.flexDirection = state.isFlipped ? 'row-reverse' : 'row';

    isProMode = state.isMfcVisible;
    const btnPro = document.getElementById('btn-pro');
    if (btnPro) {
        btnPro.innerHTML = isProMode ? '🙈 隐藏专业' : '👑 专业模式';
        btnPro.style.background = isProMode ? '#3a1a52' : '#5a2a82';
    }

    const btnAuth = document.getElementById('btn-auth');
    if (state.isAuthValid || (state.authText && state.authText.includes("试用"))) {
        btnAuth.innerHTML = `🔑 授权 <span style="font-size:11px;opacity:0.8">(${state.authText})</span>`;
        btnAuth.style.borderColor = "#00e5ff"; btnAuth.style.color = "#00e5ff";
    } else {
        btnAuth.innerHTML = `🔑 授权 <span style="font-size:11px;opacity:0.8">(${state.authText || '未激活'})</span>`;
        btnAuth.style.borderColor = "#ff0055"; btnAuth.style.color = "#ff0055";
    }

    // 🚨 接收来自 C++ 的最新输出目录并显示
    if (state.outputDir) {
        document.getElementById('dir-display').innerText = `📁 输出目录: ${state.outputDir}`;
    }

    document.querySelector('#team-blue .team-score-input').value = state.blueScore;
    document.querySelector('#team-red .team-score-input').value = state.redScore;

    const rows = document.querySelectorAll('.player-row');
    state.players.forEach((p, idx) => {
        if (idx >= 8) return;
        let row = rows[idx];
        let nameInp = row.querySelector('.name-input');
        if (!nameInp.classList.contains('input-error')) nameInp.value = p.name;
        row.querySelector('.stat-kill').value = p.kills;
        row.querySelector('.stat-death').value = p.deaths;
        row.querySelector('.stat-ak').value = p.akCount === 0 ? '-' : p.akCount;
    });
    isSyncingFromServer = false;
    updateStartButtonGuard();
}

const triggerSync = () => {
    updateStartButtonGuard();
    pushStateToServer();
};

// ==========================================
// 2. 内置弹窗系统
// ==========================================
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


function showConfirm(msg, callback) {
    resetModalInputUi();
    modalMsg.innerHTML = msg;
    modalInput.style.display = 'none';
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
function showAlert(msg) {
    resetModalInputUi();
    modalMsg.innerHTML = msg.replace(/\n/g, '<br>');
    modalInput.style.display = 'none';
    modalCancel.style.display = 'none';
    customModal.classList.add('active');
    currentModalCallback = () => { modalCancel.style.display = 'inline-block'; };
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
    if (!playerDB[playerName].includes(aliasClean)) playerDB[playerName].push(aliasClean);
    if (!savedDB[playerName].includes(aliasClean)) savedDB[playerName].push(aliasClean);
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

    if (!playerDB[playerName].includes(newClean)) playerDB[playerName].push(newClean);
    if (!savedDB[playerName].includes(newClean)) savedDB[playerName].push(newClean);

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
        if (playerDB[otherMain] && playerDB[otherMain].includes(aliasClean)) return otherMain;
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
        if (otherAliases.includes(newMainName)) return { owner: otherMain, reason: `名字是[${otherMain}]的小号` };
        if (newAliases.includes(otherMain)) return { owner: otherMain, reason: `携带的小号包含了[${otherMain}]` };
        for (let a of newAliases) { if (otherAliases.includes(a)) return { owner: otherMain, reason: `小号[${a}]与对方冲突` }; }
    }
    return null;
}

// ==========================================
// 4. 渲染选手行与交互
function createPlayerRow() {

    const row = document.createElement('div');
    row.className = 'player-row';
    // 🚨 默认关闭，防止平时点到输入框误触拖拽
    row.draggable = false;

    // 🚨 1. HTML 结构：把拖拽柄放回 name-wrapper 里面（原来齿轮的位置）
    row.innerHTML = `
        <div class="name-wrapper">
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

    // ==========================================
    // 🚨 2. 精确拖拽控制 (只在按住柄时开启)
    // ==========================================
    dragHandle.addEventListener('mousedown', () => row.draggable = true);
    dragHandle.addEventListener('mouseup', () => row.draggable = false);
    dragHandle.addEventListener('mouseleave', () => row.draggable = false);

    row.addEventListener('dragstart', function (e) {
        document.querySelectorAll('.popover').forEach(p => p.classList.remove('active'));
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
                renderAliasMenu(inlinePair.mainName, aliasPopover);
                aliasPopover.classList.add('active');
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
                        if (getCleanAliases(name).includes(aliasClean)) {
                            showAlert(`小号【${aliasClean}】已经存在，不会重复添加。`);
                            renderAliasMenu(name, aliasPopover);
                            aliasPopover.classList.add('active');
                            return;
                        }
                        bindAliasToPlayer(name, aliasClean);
                        triggerSync();
                        renderAliasMenu(name, aliasPopover);
                        aliasPopover.classList.add('active');
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
        document.querySelectorAll('.popover').forEach(p => { if (p !== autoPopover && p !== aliasPopover) p.classList.remove('active'); });
        document.querySelectorAll('.player-row').forEach(r => r.classList.remove('active-row'));
        row.classList.add('active-row');

        if (this.value.trim() !== '') {
            // 【有名字】：弹小号设置
            renderAliasMenu(this.value.trim(), aliasPopover);
            aliasPopover.classList.add('active');
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
            renderAliasMenu(inlinePair.mainName, aliasPopover);
            if (aliasPopover) aliasPopover.classList.add('active');
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
            renderAliasMenu(currentName, aliasPopover);
            if (aliasPopover) aliasPopover.classList.add('active');
            triggerSync();
            return;
        }
        // 延迟隐藏两个弹窗（保持和原来一样的时间）
        setTimeout(() => {
            if (autoPopover) autoPopover.classList.remove('active');
            if (aliasPopover) aliasPopover.classList.remove('active');
        }, 150);
        triggerSync();
    });

    let isComposing = false;
    nameInput.addEventListener('compositionstart', () => isComposing = true);
    nameInput.addEventListener('compositionend', function () { isComposing = false; processInputLogic(this, false); });

    nameInput.addEventListener('input', function () {
        if (!isComposing) {
            let val = this.value.trim();

            // 先执行查重和补全逻辑
            processInputLogic(this, val === '');

            // 如果名字有效且无冲突，则显示小号面板（即使库中没有该选手）
            if (val !== '' && !this.classList.contains('input-error')) {
                renderAliasMenu(val, aliasPopover);
                aliasPopover.classList.add('active');
            } else {
                aliasPopover.classList.remove('active');
            }
        }
    });

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
            autoPopover.classList.remove('active');
            aliasPopover.classList.remove('active'); // 🚨 修复：有冲突时强制关掉小号列表
            return;
        }
        inputElem.classList.remove('input-error');

        let availableMains = Object.keys(playerDB).filter(name => getFieldConflict(name, inputElem) === null);
        let matches = !val ? (forceShowAll ? availableMains : []) : availableMains.filter(n => n.includes(val));

        currentFocusIndex = -1;

        if (matches.length > 0) {
            matches.sort((a, b) => a.localeCompare(b, 'zh-Hans-CN', { sensitivity: 'accent' }));
            autoPopover.innerHTML = matches.map(n => `<div class="popover-item suggestion-item">${n}</div>`).join('');
            autoPopover.classList.add('active');

            autoPopover.querySelectorAll('.suggestion-item').forEach(item => {
                // 🚨 修复：将 click 改为 mousedown
                item.addEventListener('mousedown', (e) => {
                    // 🚨 核心：阻止默认事件！防止输入框失去焦点触发 blur 里的隐藏代码
                    e.preventDefault();
                    e.stopPropagation();

                    inputElem.value = item.innerText;

                    // 赋值后再跑一遍查重逻辑确保万无一失
                    processInputLogic(inputElem, false);

                    // 如果选中的人没冲突，就无缝切出他的小号列表
                    if (!inputElem.classList.contains('input-error')) {
                        autoPopover.classList.remove('active');
                        renderAliasMenu(inputElem.value, aliasPopover);
                        aliasPopover.classList.add('active');
                    }
                    triggerSync();
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

function renderAliasMenu(playerName, popElement) {
    playerDB[playerName] = uniqueAliasArray(playerDB[playerName]);
    savedDB[playerName] = uniqueAliasArray(savedDB[playerName]);
    let html = getCleanAliases(playerName).map((a, i) => {
        // 🚨 核心改动：判断名字长度，超过 6 个字符就截断并拼上 "..."
        let displayName = a.length > 6 ? a.substring(0, 6) + '...' : a;
        const legacyShort = isLegacyShortAliasWithoutMeta(a);
        const itemClass = legacyShort ? 'popover-item alias-legacy-short' : 'popover-item';
        const aliasTitle = legacyShort ? getLegacyShortAliasDeleteReason(a) : a;

        return `
        <div class="${itemClass}">
            <span class="alias-name" title="${escapeHtml(aliasTitle)}">${legacyShort ? '⚠️' : '🎮'} ${escapeHtml(displayName)}</span>
            <div class="alias-actions">
                <span class="btn-edit-alias" data-idx="${i}" title="修改小号名称，并同步修改永久小号库">✎</span>
                <span class="btn-temp-unbind" data-idx="${i}" title="临时解绑 (本次添加隐藏此ID不参与名称匹配，删除主号后重新添加即可恢复)">X</span>
                <span class="btn-perm-unbind" data-idx="${i}" title="永久解绑 (从库选手信息里面彻底删除)">🗑️</span>
            </div>
        </div>`;
    }).join('');
    html += `<div class="popover-item add-alias-btn">+ 绑定新小号</div>`;
    popElement.innerHTML = html;

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

                if (getCleanAliases(playerName).includes(aliasTrimmed)) {
                    showAlert(`小号【${aliasTrimmed}】已经存在，不会重复添加。`);
                    renderAliasMenu(playerName, popElement);
                    popElement.classList.add('active');
                    return;
                }
                bindAliasToPlayer(playerName, aliasTrimmed);

                renderAliasMenu(playerName, popElement);
                popElement.classList.add('active');
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
                    renderAliasMenu(playerName, popElement);
                    popElement.classList.add('active');
                    return;
                }

                const samePlayerAliases = (playerDB[playerName] || []).filter((_, i) => i !== idx);
                if (samePlayerAliases.includes(aliasTrimmed)) {
                    showAlert(`❌ 修改失败！该选手已经有小号【${aliasTrimmed}】。`);
                    return;
                }

                const conflictOwner = findAliasConflict(playerName, aliasTrimmed);
                if (conflictOwner) {
                    showAlert(`❌ 修改失败！该小号已被场上选手【${conflictOwner}】占用！`);
                    return;
                }

                updateAliasForPlayer(playerName, oldAlias, aliasTrimmed);
                renderAliasMenu(playerName, popElement);
                popElement.classList.add('active');
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

            renderAliasMenu(playerName, popElement);
            popElement.classList.add('active');

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

                    renderAliasMenu(playerName, popElement);
                    popElement.classList.add('active');
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
    redTeam.querySelector('.rows-container').appendChild(createPlayerRow());
    blueTeam.querySelector('.rows-container').appendChild(createPlayerRow());
}

document.querySelectorAll('.team-score-input').forEach(input => { input.type = 'text'; bindProNumberControls(input); });
updateStartButtonGuard();

document.addEventListener('click', (e) => {
    if (e.target.closest('#custom-modal') || e.target.classList.contains('name-input') || e.target.classList.contains('gear-btn')) return;
    document.querySelectorAll('.popover').forEach(p => p.classList.remove('active'));
    // 点击任意空白处，撤销所有选手的置顶层级
    document.querySelectorAll('.player-row').forEach(r => r.classList.remove('active-row'));
});

document.addEventListener('input', (e) => {
    if (e.target && e.target.classList && e.target.classList.contains('name-input')) {
        updateStartButtonGuard();
    }
});

document.getElementById('btn-swap').addEventListener('click', () => window.chrome.webview.postMessage({ action: "cmd_swap" }));
document.getElementById('btn-monitor').addEventListener('click', () => {
    const violations = updateStartButtonGuard();
    if (!isMonitoring && violations.length > 0) {
        showAlert(getStartGuardMessage());
        return;
    }
    window.chrome.webview.postMessage({ action: "cmd_monitor", state: !isMonitoring });
});
document.getElementById('btn-auth').addEventListener('click', () => { showPrompt("请输入授权卡密 (CDK):", (c) => { if (c) window.chrome.webview.postMessage({ action: "cmd_auth", code: c.trim() }); }); });
document.getElementById('btn-pro').addEventListener('click', () => window.chrome.webview.postMessage({ action: "cmd_toggle_mfc", show: !isProMode }));

// 🚨 绑定新加的“更改目录”事件
document.getElementById('dir-display').addEventListener('click', () => {
    if (window.chrome?.webview) window.chrome.webview.postMessage({ action: "cmd_browse_dir" });
});

function clearTeamData(teamId) {
    const panel = document.getElementById(teamId);
    panel.querySelectorAll('.name-input').forEach(input => { input.value = ''; input.classList.remove('input-error'); });
    panel.querySelectorAll('.stat-kill, .stat-death').forEach(input => input.value = '0');
    panel.querySelectorAll('.stat-ak').forEach(input => input.value = '-');
    triggerSync();
}
document.getElementById('btn-clear-blue').addEventListener('click', () => { showConfirm('⚠️ 确定清空 <span style="color:#00e5ff">蓝队</span>？', (res) => { if (res) clearTeamData('team-blue'); }); });
document.getElementById('btn-clear-red').addEventListener('click', () => { showConfirm('⚠️ 确定清空 <span style="color:#ff0055">红队</span>？', (res) => { if (res) clearTeamData('team-red'); }); });
document.getElementById('btn-reset').addEventListener('click', () => {
    showConfirm('确定重置所有战绩吗？', (res) => {
        if (res) {
            document.querySelectorAll('.stat-kill, .stat-death').forEach(i => i.value = '0');
            document.querySelectorAll('.stat-ak').forEach(i => i.value = '-');
            document.querySelectorAll('.team-score-input').forEach(i => i.value = '0');
            triggerSync();
        }
    });
});

// 1) 先把所有战绩输入框的 Tab 顺序移除，这样 Tab 永远跳不到它们
document.querySelectorAll('.stat-kill, .stat-death, .stat-ak').forEach(inp => {
    inp.setAttribute('tabindex', '-1');
});

document.addEventListener('keydown', function (e) {
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
    if (e.key === 'Control' && ctrlSwapState.active) {
        // 执行交换
        if (ctrlSwapState.targetRow && ctrlSwapState.sourceRow !== ctrlSwapState.targetRow) {
            swapDOMNodes(ctrlSwapState.sourceRow, ctrlSwapState.targetRow);
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
