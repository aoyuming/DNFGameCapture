// ==========================================
// 1. 核心：WebView2 同步引擎
// ==========================================
let playerDB = {}; 
let isSyncingFromServer = false;
let hasReceivedInitialData = false;

if (window.chrome && window.chrome.webview) {
    window.chrome.webview.addEventListener('message', function(event) {
        try {
            const msg = typeof event.data === 'string' ? JSON.parse(event.data) : event.data; 
            if (msg.action === 'sync_state') {
                hasReceivedInitialData = true;
                // 🌟 同步库信息：将 MFC 的数据库同步给前端
                if (msg.data.fullAliasDB) {
                    playerDB = {}; // 先清空，再解析
                    for (let key in msg.data.fullAliasDB) {
                        // 将 "(小号1)(小号2)" 格式转为数组
                        playerDB[key] = msg.data.fullAliasDB[key].split(/[()（）]/).filter(s => s.trim());
                    }
                }
                applyStateFromServer(msg.data);
            }
            else if (msg.action === 'auth_result') {// 🚨 【新增】：接收 MFC 的云端验证结果，并用内置弹窗显示！
                showAlert(msg.message); 
            }
        } catch (e) { console.error('解析 C++ 消息失败', e); }
    });

    let handshakeTimer = setInterval(() => {
        if (!hasReceivedInitialData) {
            window.chrome.webview.postMessage({ action: "page_ready" });
        } else {
            clearInterval(handshakeTimer);
        }
    }, 500);
}

// ==========================================
// 2. 核心逻辑：名字校验与处理
// ==========================================
function processInputLogic(inputElem) {
    let newVal = inputElem.value.trim();
    
    // 🚨 【新增】：重名实时检测
    let allNameInputs = document.querySelectorAll('.name-input');
    let duplicate = false;
    allNameInputs.forEach(otherInput => {
        if (otherInput !== inputElem && otherInput.value.trim() === newVal && newVal !== "") {
            duplicate = true;
        }
    });

    if (duplicate) {
        inputElem.classList.add('input-error'); // 触发红色闪烁
        return; // 重名时不执行同步
    } else {
        inputElem.classList.remove('input-error');
    }

    // 自动提示逻辑
    const row = inputElem.closest('.player-row');
    const autoPop = row.querySelector('.autocomplete-popover');
    const aliasPop = row.querySelector('.alias-popover');
    
    aliasPop.classList.remove('active'); 
    if (!newVal) { autoPop.classList.remove('active'); return; }

    const matches = Object.keys(playerDB).filter(name => name.includes(newVal));
    if (matches.length > 0) {
        autoPop.innerHTML = matches.map(name => `<div class="popover-item suggestion-item">${name}</div>`).join('');
        autoPop.classList.add('active');

        autoPop.querySelectorAll('.suggestion-item').forEach(item => {
            item.addEventListener('click', (e) => {
                e.stopPropagation();
                inputElem.value = item.innerText;
                autoPop.classList.remove('active');
                renderAliasMenu(inputElem.value.trim(), aliasPop);
                aliasPop.classList.add('active');
                triggerSync(); // 🌟 补全后立刻同步给 C++
            });
        });
    } else {
        autoPop.classList.remove('active');
    }
}

// 🌟 将数据推给 MFC
function pushStateToServer() {
    if (!window.chrome || !window.chrome.webview || isSyncingFromServer) return;

    let state = {
        blueScore: parseInt(document.querySelector('#team-blue .team-score-input').value) || 0,
        redScore: parseInt(document.querySelector('#team-red .team-score-input').value) || 0,
        players: []
    };

    // 严格按顺序打包：前4蓝(4-7)，后4红(0-3)
    document.querySelectorAll('#team-blue .player-row').forEach(row => {
        state.players.push(getRowData(row, 1));
    });
    document.querySelectorAll('#team-red .player-row').forEach(row => {
        state.players.push(getRowData(row, 0));
    });

    window.chrome.webview.postMessage({ action: "update_state", data: state });
}

function getRowData(row, teamId) {
    let name = row.querySelector('.name-input').value.trim();
    let akVal = row.querySelector('.stat-ak').value;
    return {
        team: teamId, name: name,
        kills: parseInt(row.querySelector('.stat-kill').value) || 0,
        deaths: parseInt(row.querySelector('.stat-death').value) || 0,
        akCount: (akVal === '-' || akVal === '') ? 0 : parseInt(akVal),
        aliases: playerDB[name] || []
    };
}

function applyStateFromServer(state) {
    isSyncingFromServer = true; 
    // 🚨【新增】：同步 MFC 的监控运行状态
    isMonitoring = state.isMonitoring;
    const btnMonitor = document.getElementById('btn-monitor');
    btnMonitor.innerHTML = isMonitoring ? '🛑 停止' : '▶ 运行';
    btnMonitor.className = isMonitoring ? 'ctrl-btn btn-monitor-stop' : 'ctrl-btn btn-monitor-start';

    document.querySelector('#team-blue .team-score-input').value = state.blueScore; 
    document.querySelector('#team-red .team-score-input').value = state.redScore;
    const rows = document.querySelectorAll('.player-row');
    state.players.forEach((p, idx) => {
        if (idx >= 8) return;
        let row = rows[idx];
        row.querySelector('.name-input').value = p.name;
        row.querySelector('.stat-kill').value = p.kills;
        row.querySelector('.stat-death').value = p.deaths;
        row.querySelector('.stat-ak').value = p.akCount === 0 ? '-' : p.akCount;
    });
    isSyncingFromServer = false; 
}

const triggerSync = () => pushStateToServer();
// ==========================================
// 2. 自定义弹窗引擎
// ==========================================
const customModal = document.getElementById('custom-modal');
const modalMsg = document.getElementById('modal-msg');
const modalInput = document.getElementById('modal-input');
const modalCancel = document.getElementById('modal-cancel');
const modalOk = document.getElementById('modal-ok');
let currentModalCallback = null;

function showConfirm(msg, callback) {
    modalMsg.innerHTML = msg; modalInput.style.display = 'none';
    customModal.classList.add('active'); currentModalCallback = callback;
}
function showPrompt(msg, callback) {
    modalMsg.innerHTML = msg; modalInput.style.display = 'inline-block';
    modalInput.value = ''; customModal.classList.add('active'); modalInput.focus();
    currentModalCallback = callback;
}
function showAlert(msg) {
    modalMsg.innerHTML = msg.replace(/\n/g, '<br>'); // 支持多行换行
    modalInput.style.display = 'none';
    modalCancel.style.display = 'none'; // 隐藏取消按钮
    customModal.classList.add('active');
    currentModalCallback = () => {
        modalCancel.style.display = 'inline-block'; // 点击确定后恢复取消按钮供别人使用
    };
}
modalCancel.onclick = () => { customModal.classList.remove('active'); if (currentModalCallback) currentModalCallback(null); };
modalOk.onclick = () => {
    customModal.classList.remove('active');
    if (currentModalCallback) {
        let res = modalInput.style.display === 'none' ? true : modalInput.value;
        currentModalCallback(res);
    }
};
modalInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') modalOk.click();
});

// ==========================================
// 3. 数字控制器
// ==========================================
function bindProNumberControls(inputElem, isAK = false) {
    inputElem.addEventListener('contextmenu', e => e.preventDefault());
    inputElem.addEventListener('focus', function() { if (this.value === '-') this.value = ''; this.select(); });

    inputElem.addEventListener('mousedown', function(e) {
        if (e.button !== 0 && e.button !== 2 || document.activeElement !== this) return;
        e.preventDefault();
        let val = parseInt(this.value) || 0;
        if (e.button === 0) val++; else if (e.button === 2) val--;
        if (val < 0) val = 0;
        this.value = (isAK && val === 0) ? '-' : val;
        this.select(); triggerSync();
    });

    inputElem.addEventListener('wheel', function(e) {
        if (document.activeElement !== this) return;
        e.preventDefault();
        let val = parseInt(this.value) || 0;
        if (e.deltaY < 0) val++; else val--;
        if (val < 0) val = 0;
        this.value = (isAK && val === 0) ? '-' : val;
        this.select(); triggerSync();
    });
    
    inputElem.addEventListener('blur', function() {
        if (this.value.trim() === '' || this.value === '0') this.value = isAK ? '-' : '0';
        triggerSync();
    });
}

document.querySelectorAll('.team-score-input').forEach(input => {
    input.type = 'text'; 
    bindProNumberControls(input);
});

// ==========================================
// 4. 渲染与逻辑
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

    bindProNumberControls(row.querySelector('.stat-kill'));
    bindProNumberControls(row.querySelector('.stat-death'));
    bindProNumberControls(row.querySelector('.stat-ak'), true);
    
    const gearBtn = row.querySelector('.gear-btn');
    const nameInput = row.querySelector('.name-input');
    const autocompletePopover = row.querySelector('.autocomplete-popover');
    const aliasPopover = row.querySelector('.alias-popover');

    let currentFocusIndex = -1;
    function clearActiveItems() {
        autocompletePopover.querySelectorAll('.suggestion-item').forEach(item => item.classList.remove('keyboard-focus'));
    }

    nameInput.addEventListener('keydown', function(e) {
        if (!autocompletePopover.classList.contains('active')) return;
        const items = autocompletePopover.querySelectorAll('.suggestion-item');
        if (items.length === 0) return;

        if (e.key === 'ArrowDown') { e.preventDefault(); currentFocusIndex++; if (currentFocusIndex >= items.length) currentFocusIndex = 0; clearActiveItems(); items[currentFocusIndex].classList.add('keyboard-focus'); } 
        else if (e.key === 'ArrowUp') { e.preventDefault(); currentFocusIndex--; if (currentFocusIndex < 0) currentFocusIndex = items.length - 1; clearActiveItems(); items[currentFocusIndex].classList.add('keyboard-focus'); } 
        else if (e.key === 'Enter') { e.preventDefault(); if (currentFocusIndex > -1) items[currentFocusIndex].click(); }
    });

    let isComposing = false; 
    nameInput.addEventListener('compositionstart', () => { isComposing = true; });
    nameInput.addEventListener('compositionend', function() { isComposing = false; processInputLogic(this); });
    nameInput.addEventListener('input', function() { if (!isComposing) processInputLogic(this); });

    function processInputLogic(inputElem) {
        let newVal = inputElem.value;
        aliasPopover.classList.remove('active'); 
        currentFocusIndex = -1; 

        if (!newVal.trim()) { autocompletePopover.classList.remove('active'); return; }

        const matches = Object.keys(playerDB).filter(name => name.includes(newVal.trim()));
        if (matches.length > 0) {
            autocompletePopover.innerHTML = matches.map(name => `<div class="popover-item suggestion-item">${name}</div>`).join('');
            autocompletePopover.classList.add('active');

            autocompletePopover.querySelectorAll('.suggestion-item').forEach(item => {
                item.addEventListener('click', (e) => {
                    e.stopPropagation();
                    inputElem.value = item.innerText;
                    autocompletePopover.classList.remove('active');
                    currentFocusIndex = -1;
                    
                    document.querySelectorAll('.popover').forEach(pop => pop.classList.remove('active'));
                    renderAliasMenu(inputElem.value.trim(), aliasPopover);
                    aliasPopover.classList.add('active');
                    triggerSync();
                });
            });
        } else {
            autocompletePopover.classList.remove('active');
        }
    }

    gearBtn.addEventListener('click', function(e) {
        e.stopPropagation();
        const isActive = aliasPopover.classList.contains('active');
        document.querySelectorAll('.popover').forEach(pop => pop.classList.remove('active'));
        
        const playerName = nameInput.value.trim();
        if (!playerName) {
            nameInput.classList.remove('input-error');
            void nameInput.offsetWidth; 
            nameInput.classList.add('input-error');
            nameInput.focus(); 
            return;
        }
        if (!isActive) {
            renderAliasMenu(playerName, aliasPopover); 
            aliasPopover.classList.add('active');
        }
    });

    nameInput.addEventListener('blur', triggerSync);
    nameInput.addEventListener('change', triggerSync);

    return row;
}

function renderAliasMenu(playerName, popElement) {
    let html = '';
    if (playerDB[playerName] && playerDB[playerName].length > 0) {
        html += playerDB[playerName].map((alias, idx) => 
            `<div class="popover-item">
                <span>🎮 ${alias}</span>
                <span class="unbind-btn" data-idx="${idx}">解绑</span>
            </div>`
        ).join('');
    } else {
        html += `<div class="popover-item" style="color:var(--text-muted); justify-content:center;">暂无小号</div>`;
    }
    html += `<div class="popover-item add-alias-btn">+ 绑定新小号</div>`;
    
    popElement.innerHTML = html;

    popElement.querySelector('.add-alias-btn').addEventListener('click', (e) => {
        e.stopPropagation(); 
        showPrompt(`为【${playerName}】绑定新小号:`, (newAlias) => {
            if (newAlias && newAlias.trim() !== '') {
                const aliasTrimmed = newAlias.trim();
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
            const aliasName = playerDB[playerName][idx];
            
            showConfirm(`⚠️ 确定要解绑小号【${aliasName}】吗？`, (isOk) => {
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

// 初始化 UI
const blueTeam = document.getElementById('team-blue');
const redTeam = document.getElementById('team-red');
for (let i = 0; i < 4; i++) {
    // 蓝队先加，红队后加
    blueTeam.querySelector('.rows-container').appendChild(createPlayerRow());
    redTeam.querySelector('.rows-container').appendChild(createPlayerRow());
}

document.addEventListener('click', function(e) {
    if (e.target.closest('#custom-modal')) return;
    document.querySelectorAll('.popover').forEach(pop => {
        if (!pop.contains(e.target)) pop.classList.remove('active');
    });
});

// ==========================================
// 5. 全局按钮与清理功能逻辑
// ==========================================
document.getElementById('btn-swap').addEventListener('click', function() {
    const container = document.getElementById('main-container');
    container.style.flexDirection = container.style.flexDirection === 'row-reverse' ? 'row' : 'row-reverse';
    if(window.chrome?.webview) window.chrome.webview.postMessage({ action: "cmd_swap" });
});

let isMonitoring = false;
const btnMonitor = document.getElementById('btn-monitor');
btnMonitor.addEventListener('click', function() {
    isMonitoring = !isMonitoring;
    this.innerHTML = isMonitoring ? '🛑 停止' : '▶ 运行';
    this.className = isMonitoring ? 'ctrl-btn btn-monitor-stop' : 'ctrl-btn btn-monitor-start';
    if(window.chrome?.webview) window.chrome.webview.postMessage({ action: "cmd_monitor", state: isMonitoring });
});

// 🚨 修改授权逻辑：自己不弹窗验证，把卡密发给 C++ 去云端验证
document.getElementById('btn-auth').addEventListener('click', function() {
    showPrompt("请输入您的授权卡密 (CDK):", (code) => {
        if(code && code.trim() !== '') {
            if(window.chrome?.webview) {
                // 将卡密发给 C++
                window.chrome.webview.postMessage({ action: "cmd_auth", code: code.trim() });
            }
        }
    });
});

// 🚨 新增：专业模式双状态按钮
let isProMode = false;
document.getElementById('btn-pro').addEventListener('click', function() {
    isProMode = !isProMode;
    // 切换文案和状态
    this.innerHTML = isProMode ? '🙈 隐藏专业' : '👑 专业模式';
    this.style.background = isProMode ? '#3a1a52' : '#5a2a82'; // 颜色加深反馈
    if(window.chrome?.webview) {
        // 告诉 MFC 是显示还是隐藏
        window.chrome.webview.postMessage({ action: "cmd_toggle_mfc", show: isProMode });
    }
});

function clearTeamData(teamId) {
    const panel = document.getElementById(teamId);
    panel.querySelectorAll('.name-input').forEach(input => input.value = '');
    panel.querySelectorAll('.stat-kill, .stat-death').forEach(input => input.value = '0');
    panel.querySelectorAll('.stat-ak').forEach(input => input.value = '-');
    triggerSync();
}

document.getElementById('btn-clear-blue').addEventListener('click', () => {
    showConfirm('⚠️ 确定要清空 <span style="color:#00e5ff">蓝队</span> 场上所有人员和数据吗？', (res) => {
        if (res) clearTeamData('team-blue');
    });
});

document.getElementById('btn-clear-red').addEventListener('click', () => {
    showConfirm('⚠️ 确定要清空 <span style="color:#ff0055">红队</span> 场上所有人员和数据吗？', (res) => {
        if (res) clearTeamData('team-red');
    });
});

document.getElementById('btn-reset').addEventListener('click', () => {
    showConfirm('⚠️ 确定要重置所有战绩和大比分吗？<br><span style="font-size:13px; color:#8b8b9f">(保留选手的名字)</span>', (res) => {
        if (res) {
            document.querySelectorAll('.stat-kill, .stat-death').forEach(input => input.value = '0');
            document.querySelectorAll('.stat-ak').forEach(input => input.value = '-');
            document.querySelectorAll('.team-score-input').forEach(input => input.value = '0');
            triggerSync();
        }
    });
});