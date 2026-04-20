
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

if (window.chrome && window.chrome.webview) {
window.chrome.webview.addEventListener('message', function(event) {
        try {
            const msg = typeof event.data === 'string' ? JSON.parse(event.data) : event.data; 
            if (msg.action === 'sync_state') {
                hasReceivedInitialData = true;
                
                if (msg.data.fullAliasDB) {
                    let newSavedDB = {};
                    
                    for (let key in msg.data.fullAliasDB) {
                        let arr = msg.data.fullAliasDB[key].split(/[()（）]/).filter(s => s.trim());
                        newSavedDB[key] = [...arr];

                        if (!playerDB[key]) {
                            playerDB[key] = [...arr];
                        } else {
                            let oldSaved = savedDB[key] || [];
                            let newFromMFC = arr.filter(a => !oldSaved.includes(a));
                            let deletedFromMFC = oldSaved.filter(a => !arr.includes(a));

                            let updatedPlayerDB = [...playerDB[key]];
                            newFromMFC.forEach(a => { if (!updatedPlayerDB.includes(a)) updatedPlayerDB.push(a); });
                            updatedPlayerDB = updatedPlayerDB.filter(a => !deletedFromMFC.includes(a));

                            playerDB[key] = updatedPlayerDB;
                        }
                    }
                    
                    for (let key in playerDB) {
                        if (!msg.data.fullAliasDB[key]) {
                            delete playerDB[key];
                        }
                    }
                    savedDB = newSavedDB; 
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
                            playerDB[p.name] = [...p.aliases];
                        }
                    });
                }
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
            playerDB[name] = [...savedDB[name]]; 
        }
    }
    // ==========================================

    let formattedDB = {};
    // 🚨 注意：发给 C++ 的永远是不受“临时解绑”影响的永久库
    for (let key in savedDB) { 
        if (savedDB[key] && savedDB[key].length > 0) {
            formattedDB[key] = savedDB[key].join('()');
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
function createPlayerRow() {
    const row = document.createElement('div');
    row.className = 'player-row';
    // 🚨 默认关闭，防止平时点到输入框误触拖拽
    row.draggable = false; 

    // 🚨 1. HTML 结构：把拖拽柄放回 name-wrapper 里面（原来齿轮的位置）
    row.innerHTML = `
        <div class="name-wrapper">
            <input type="text" class="name-input" placeholder="名字" autocomplete="off">
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

    row.addEventListener('dragstart', function(e) {
        document.querySelectorAll('.popover').forEach(p => p.classList.remove('active'));
        draggedRow = this;
        e.dataTransfer.effectAllowed = 'move';
        e.dataTransfer.setData('text/plain', ''); 
        setTimeout(() => this.classList.add('dragging'), 0);
    });

    row.addEventListener('dragend', function() {
        this.classList.remove('dragging');
        draggedRow = null;
        document.querySelectorAll('.player-row').forEach(r => r.classList.remove('drag-over'));
        this.draggable = false; // 拖拽完立刻关掉
    });

    row.addEventListener('dragover', function(e) {
        e.preventDefault(); 
        if (draggedRow !== this) this.classList.add('drag-over');
        return false;
    });

    row.addEventListener('dragleave', function() {
        this.classList.remove('drag-over');
    });

    row.addEventListener('drop', function(e) {
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

    nameInput.addEventListener('keydown', function(e) {
        if (e.key === 'Enter') {
            e.preventDefault();
            if (autoPopover.classList.contains('active') && currentFocusIndex > -1) {
                const items = autoPopover.querySelectorAll('.suggestion-item');
                // 🚨 修复 1：因为鼠标点击改成了 mousedown，键盘回车也要模拟触发 mousedown 才能生效
                if (items[currentFocusIndex]) {
                    items[currentFocusIndex].dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true }));
                }
            } else if (this.value.trim() === '') {
                // 🚨 修复 2：如果输入框是空的，按回车什么也不做（不触发失去焦点）
                // 这样光标依然在输入框里闪烁，你可以继续按方向键找人
                return;
            } else { 
                // 只有当输入框有字，且不需要从列表选人时，按回车才完成输入并失去焦点
                this.blur(); 
            }
            return;
        }
        
        if (!autoPopover.classList.contains('active')) return;
        const items = autoPopover.querySelectorAll('.suggestion-item');
        if (items.length === 0) return;

        if (e.key === 'ArrowDown') {
            e.preventDefault(); currentFocusIndex++; if (currentFocusIndex >= items.length) currentFocusIndex = 0;
            clearActiveItems(); items[currentFocusIndex].classList.add('keyboard-focus'); items[currentFocusIndex].scrollIntoView({block: "nearest"});
        } else if (e.key === 'ArrowUp') {
            e.preventDefault(); currentFocusIndex--; if (currentFocusIndex < 0) currentFocusIndex = items.length - 1;
            clearActiveItems(); items[currentFocusIndex].classList.add('keyboard-focus'); items[currentFocusIndex].scrollIntoView({block: "nearest"});
        }
    });

    nameInput.addEventListener('click', (e) => e.stopPropagation());

    // 🌟 核心改动点：判断当前输入框有没有值
    nameInput.addEventListener('focus', function() {
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
    });

    nameInput.addEventListener('blur', function() {
        if (this.classList.contains('input-error')) {
            showAlert(this.getAttribute('data-error-msg'));
            this.value = ''; this.classList.remove('input-error');
        }
        setTimeout(() => aliasPopover.classList.remove('active'), 150); 
        triggerSync();
    });

    let isComposing = false;
    nameInput.addEventListener('compositionstart', () => isComposing = true);
    nameInput.addEventListener('compositionend', function() { isComposing = false; processInputLogic(this, false); });
    
    nameInput.addEventListener('input', function() { 
            if (!isComposing) {
                let val = this.value.trim();
                
                // 🚨 1. 先跑一遍查重和补全逻辑，如果名字重复，这里会给输入框加上 'input-error' 的红框
                processInputLogic(this, val === '');          

                // 🚨 2. 判断：如果名字在库里，且【没有被判定为重复报错】
                if (val !== '' && playerDB.hasOwnProperty(val) && !this.classList.contains('input-error')) {
                    autoPopover.classList.remove('active'); 
                    renderAliasMenu(val, aliasPopover);
                    aliasPopover.classList.add('active');
                } else {
                    // 如果有报错，或者名字没打完，就把小号列表藏起来
                    aliasPopover.classList.remove('active'); 
                }
            } 
        
    });;

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
    let html = (playerDB[playerName] || []).map((a, i) => {
        // 🚨 核心改动：判断名字长度，超过 6 个字符就截断并拼上 "..."
        let displayName = a.length > 6 ? a.substring(0, 6) + '...' : a;

        return `
        <div class="popover-item">
            <span class="alias-name" title="${a}">🎮 ${displayName}</span>
            <div class="alias-actions">
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
                if(!savedDB[playerName]) savedDB[playerName] = []; 
                
                if (playerDB[playerName].includes(aliasTrimmed)) return;
                
                playerDB[playerName].push(aliasTrimmed);
                savedDB[playerName].push(aliasTrimmed); 

                renderAliasMenu(playerName, popElement); 
                popElement.classList.add('active');
                triggerSync(); 
            }
        });
    });

    // ==========================================
    // 2. 临时解绑逻辑 (只删 UI 内存，换人后自动恢复)
    // ==========================================
    popElement.querySelectorAll('.btn-temp-unbind').forEach(btn => {
        btn.addEventListener('mousedown', (e) => {
            e.preventDefault(); e.stopPropagation(); 
            const idx = e.target.getAttribute('data-idx');
            
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
            const targetAlias = playerDB[playerName][idx];

            showConfirm(`⚠️ 确定要【永久删除】小号 [${targetAlias}] 吗？`, (isOk) => {
                if(isOk) {
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