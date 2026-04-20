// ==========================================
// 1. 核心：WebView2 同步引擎
// ==========================================
let playerDB = {}; 
let isSyncingFromServer = false;
let hasReceivedInitialData = false;
let isMonitoring = false;
let isProMode = false;

if (window.chrome && window.chrome.webview) {
    window.chrome.webview.addEventListener('message', function(event) {
        try {
            const msg = typeof event.data === 'string' ? JSON.parse(event.data) : event.data; 
            if (msg.action === 'sync_state') {
                hasReceivedInitialData = true;
                if (msg.data.fullAliasDB) {
                    playerDB = {}; 
                    for (let key in msg.data.fullAliasDB) {
                        playerDB[key] = msg.data.fullAliasDB[key].split(/[()（）]/).filter(s => s.trim());
                    }
                }
                applyStateFromServer(msg.data);
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
    let state = {
        blueScore: parseInt(document.querySelector('#team-blue .team-score-input').value) || 0,
        redScore: parseInt(document.querySelector('#team-red .team-score-input').value) || 0,
        players: []
    };
    document.querySelectorAll('#team-red .player-row').forEach(row => state.players.push(getRowData(row, 0)));
    document.querySelectorAll('#team-blue .player-row').forEach(row => state.players.push(getRowData(row, 1)));
    window.chrome.webview.postMessage({ action: "update_state", data: state });
}

function getRowData(row, teamId) {
    let nameElem = row.querySelector('.name-input');
    let name = nameElem.value.trim();
    if (nameElem.classList.contains('input-error')) name = '';
    return {
        team: teamId, name: name,
        kills: parseInt(row.querySelector('.stat-kill').value) || 0,
        deaths: parseInt(row.querySelector('.stat-death').value) || 0,
        akCount: parseInt(row.querySelector('.stat-ak').value) || 0,
        aliases: playerDB[name] || []
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
}

const triggerSync = () => pushStateToServer();

// ==========================================
// 2. 内置弹窗系统
// ==========================================
const customModal = document.getElementById('custom-modal');
const modalMsg = document.getElementById('modal-msg');
const modalInput = document.getElementById('modal-input');
const modalCancel = document.getElementById('modal-cancel');
const modalOk = document.getElementById('modal-ok');
let currentModalCallback = null;

function showConfirm(msg, callback) { modalMsg.innerHTML = msg; modalInput.style.display = 'none'; customModal.classList.add('active'); currentModalCallback = callback; }
function showPrompt(msg, callback) { modalMsg.innerHTML = msg; modalInput.style.display = 'inline-block'; modalInput.value = ''; customModal.classList.add('active'); modalInput.focus(); currentModalCallback = callback; }
function showAlert(msg) { modalMsg.innerHTML = msg.replace(/\n/g, '<br>'); modalInput.style.display = 'none'; modalCancel.style.display = 'none'; customModal.classList.add('active'); currentModalCallback = () => { modalCancel.style.display = 'inline-block'; }; }
modalCancel.onclick = () => { customModal.classList.remove('active'); if (currentModalCallback) currentModalCallback(null); };
modalOk.onclick = () => { customModal.classList.remove('active'); if (currentModalCallback) { let res = modalInput.style.display === 'none' ? true : modalInput.value; currentModalCallback(res); } };

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
// ==========================================
// ==========================================
// 4. 渲染选手行与交互 (已修复键盘方向键与回车补齐)
// ==========================================
function createPlayerRow() {
    const row = document.createElement('div');
    row.className = 'player-row';
    row.innerHTML = `
        <div class="name-wrapper">
            <input type="text" class="name-input" placeholder="名字" autocomplete="off">
            <button class="gear-btn">⚙️</button>
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
    const gearBtn = row.querySelector('.gear-btn');

    bindProNumberControls(row.querySelector('.stat-kill'));
    bindProNumberControls(row.querySelector('.stat-death'));
    bindProNumberControls(row.querySelector('.stat-ak'), true);

    // 🚨 新增：键盘导航焦点追踪器
    let currentFocusIndex = -1;
    function clearActiveItems() {
        autoPopover.querySelectorAll('.suggestion-item').forEach(item => item.classList.remove('keyboard-focus'));
    }

    // 🚨 恢复：键盘上下方向键与回车选择逻辑
    nameInput.addEventListener('keydown', function(e) {
        if (e.key === 'Enter') {
            e.preventDefault();
            if (autoPopover.classList.contains('active') && currentFocusIndex > -1) {
                const items = autoPopover.querySelectorAll('.suggestion-item');
                if (items[currentFocusIndex]) items[currentFocusIndex].click(); 
            } else {
                this.blur(); // 没选列表的话，回车直接让输入框失去焦点并同步
            }
            return;
        }

        if (!autoPopover.classList.contains('active')) return;
        const items = autoPopover.querySelectorAll('.suggestion-item');
        if (items.length === 0) return;

        if (e.key === 'ArrowDown') {
            e.preventDefault();
            currentFocusIndex++;
            if (currentFocusIndex >= items.length) currentFocusIndex = 0;
            clearActiveItems();
            items[currentFocusIndex].classList.add('keyboard-focus');
            items[currentFocusIndex].scrollIntoView({block: "nearest"});
        } else if (e.key === 'ArrowUp') {
            e.preventDefault();
            currentFocusIndex--;
            if (currentFocusIndex < 0) currentFocusIndex = items.length - 1;
            clearActiveItems();
            items[currentFocusIndex].classList.add('keyboard-focus');
            items[currentFocusIndex].scrollIntoView({block: "nearest"});
        }
    });

    nameInput.addEventListener('click', (e) => {
        e.stopPropagation();
        document.querySelectorAll('.popover').forEach(p => { if (p !== autoPopover) p.classList.remove('active'); });
        document.querySelectorAll('.player-row').forEach(r => r.classList.remove('active-row'));
        row.classList.add('active-row');
        processInputLogic(nameInput, true);
    });

    nameInput.addEventListener('focus', () => {
        document.querySelectorAll('.popover').forEach(p => { if (p !== autoPopover) p.classList.remove('active'); });
        document.querySelectorAll('.player-row').forEach(r => r.classList.remove('active-row'));
        row.classList.add('active-row');
        processInputLogic(nameInput, true);
    });

    nameInput.addEventListener('blur', function() {
        if (this.classList.contains('input-error')) {
            showAlert(this.getAttribute('data-error-msg'));
            this.value = ''; this.classList.remove('input-error');
        }
        triggerSync();
    });

    let isComposing = false;
    nameInput.addEventListener('compositionstart', () => isComposing = true);
    nameInput.addEventListener('compositionend', function() { isComposing = false; processInputLogic(this, false); });
    nameInput.addEventListener('input', function() { if (!isComposing) processInputLogic(this, false); });

    function processInputLogic(inputElem, forceShowAll) {
        let val = inputElem.value.trim();
        let conflict = getFieldConflict(val, inputElem);
        if (val && conflict) {
            inputElem.classList.add('input-error');
            inputElem.setAttribute('data-error-msg', `❌ 无法上场！已被【${conflict.owner}】占用。\n原因：${conflict.reason}`);
            autoPopover.classList.remove('active'); return;
        }
        inputElem.classList.remove('input-error');
        
        let availableMains = Object.keys(playerDB).filter(name => getFieldConflict(name, inputElem) === null);
        let matches = !val ? (forceShowAll ? availableMains : []) : availableMains.filter(n => n.includes(val));
        
        currentFocusIndex = -1; // 每次列表刷新时，重置键盘焦点

        if (matches.length > 0) {
            matches.sort((a, b) => a.localeCompare(b, 'zh-Hans-CN', { sensitivity: 'accent' }));
            autoPopover.innerHTML = matches.map(n => `<div class="popover-item suggestion-item">${n}</div>`).join('');
            autoPopover.classList.add('active');
            autoPopover.querySelectorAll('.suggestion-item').forEach(item => {
                item.addEventListener('click', (e) => {
                    e.stopPropagation();
                    inputElem.value = item.innerText;
                    autoPopover.classList.remove('active');
                    renderAliasMenu(inputElem.value, aliasPopover);
                    aliasPopover.classList.add('active');
                    triggerSync();
                });
            });
        } else { autoPopover.classList.remove('active'); }
    }

    gearBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        document.querySelectorAll('.popover').forEach(p => p.classList.remove('active'));
        document.querySelectorAll('.player-row').forEach(r => r.classList.remove('active-row'));
        
        if (nameInput.value.trim()) { 
            row.classList.add('active-row');
            renderAliasMenu(nameInput.value.trim(), aliasPopover); 
            aliasPopover.classList.add('active'); 
        }
    });

    return row;
}

// 辅助函数：数字控制 (支持滚轮、右键、以及手动打字输入)
function bindProNumberControls(inputElem, isAK = false) {
    inputElem.addEventListener('contextmenu', e => e.preventDefault());
    
    // 🚨 修复 1：获取焦点时自动全选，方便直接打字覆盖
    inputElem.addEventListener('focus', function() { 
        if (this.value === '-') this.value = ''; 
        this.select(); 
    });

    inputElem.addEventListener('mousedown', function(e) {
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

    inputElem.addEventListener('wheel', function(e) {
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
    inputElem.addEventListener('blur', function() {
        let val = parseInt(this.value);
        if (isNaN(val) || val < 0) {
            this.value = isAK ? '-' : '0';
        } else {
            this.value = (isAK && val === 0) ? '-' : val;
        }
        triggerSync();
    });

    // 🚨 修复 4：支持打完字按回车立刻生效
    inputElem.addEventListener('keydown', function(e) {
        if (e.key === 'Enter') this.blur();
    });
}

function renderAliasMenu(playerName, popElement) {
    let html = (playerDB[playerName] || []).map((a, i) => `<div class="popover-item"><span>🎮 ${a}</span><span class="unbind-btn" data-idx="${i}">解绑</span></div>`).join('');
    html += `<div class="popover-item add-alias-btn">+ 绑定新小号</div>`;
    popElement.innerHTML = html;

    popElement.querySelector('.add-alias-btn').addEventListener('click', (e) => {
        e.stopPropagation(); 
        showPrompt(`为【${playerName}】绑定新小号:`, (newAlias) => {
            if (newAlias && newAlias.trim() !== '') {
                const aliasTrimmed = newAlias.trim();
                let fieldInputs = document.querySelectorAll('.name-input');
                for (let inp of fieldInputs) {
                    let otherMain = inp.value.trim();
                    if (!otherMain || otherMain === playerName) continue;
                    if (otherMain === aliasTrimmed || (playerDB[otherMain] && playerDB[otherMain].includes(aliasTrimmed))) {
                        showAlert(`❌ 绑定失败！该小号已被场上选手【${otherMain}】占用！`);
                        return;
                    }
                }
                if(!playerDB[playerName]) playerDB[playerName] = [];
                if (playerDB[playerName].includes(aliasTrimmed)) return;
                playerDB[playerName].push(aliasTrimmed);
                renderAliasMenu(playerName, popElement); 
                popElement.classList.add('active');
                triggerSync(); 
            }
        });
    });

    popElement.querySelectorAll('.unbind-btn').forEach(btn => {
        btn.addEventListener('click', (e) => {
            e.stopPropagation(); 
            const idx = e.target.getAttribute('data-idx');
            showConfirm(`⚠️ 确定要解绑小号【${playerDB[playerName][idx]}】吗？`, (isOk) => {
                if(isOk) {
                    playerDB[playerName].splice(idx, 1); 
                    renderAliasMenu(playerName, popElement); 
                    popElement.classList.add('active');
                    triggerSync();
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

document.addEventListener('click', (e) => {
    if (e.target.closest('#custom-modal') || e.target.classList.contains('name-input') || e.target.classList.contains('gear-btn')) return;
    document.querySelectorAll('.popover').forEach(p => p.classList.remove('active'));
    // 点击任意空白处，撤销所有选手的置顶层级
    document.querySelectorAll('.player-row').forEach(r => r.classList.remove('active-row'));
});

document.getElementById('btn-swap').addEventListener('click', () => window.chrome.webview.postMessage({ action: "cmd_swap" }));
document.getElementById('btn-monitor').addEventListener('click', () => window.chrome.webview.postMessage({ action: "cmd_monitor", state: !isMonitoring }));
document.getElementById('btn-auth').addEventListener('click', () => { showPrompt("请输入授权卡密 (CDK):", (c) => { if(c) window.chrome.webview.postMessage({ action: "cmd_auth", code: c.trim() }); }); });
document.getElementById('btn-pro').addEventListener('click', () => window.chrome.webview.postMessage({ action: "cmd_toggle_mfc", show: !isProMode }));

// 🚨 绑定新加的“更改目录”事件
document.getElementById('dir-display').addEventListener('click', () => {
    if(window.chrome?.webview) window.chrome.webview.postMessage({ action: "cmd_browse_dir" });
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
        if(res) {
            document.querySelectorAll('.stat-kill, .stat-death').forEach(i => i.value = '0');
            document.querySelectorAll('.stat-ak').forEach(i => i.value = '-');
            document.querySelectorAll('.team-score-input').forEach(i => i.value = '0');
            triggerSync();
        }
    });
});