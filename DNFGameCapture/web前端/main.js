// ==========================================
// 1. 本地数据库
// ==========================================
let playerDB = {
    "大崩": ["散打猪猪", "抖音庄大崩"],
    "月海": ["战斗法师", "抖音月与海上海1"],
    "阵雨": ["极诣剑魂", "一阵雨的小号"],
    "老仇": ["魔道学者", "老仇的鼠标"],
    "新仇": [] 
};

// ==========================================
// 2. 混合电竞级数字控制器 (键盘 + 滚轮 + 左右键)
// ==========================================
function bindProNumberControls(inputElem, isAK = false) {
    // 屏蔽原生的右键菜单
    inputElem.addEventListener('contextmenu', e => e.preventDefault());

    // 聚焦时自动全选
    inputElem.addEventListener('focus', function() { 
        if (this.value === '-') this.value = '';
        this.select(); 
    });

    // 鼠标点击逻辑
    inputElem.addEventListener('mousedown', function(e) {
        if (e.button !== 0 && e.button !== 2) return;
        if (document.activeElement !== this) return; 

        e.preventDefault(); 
        let val = parseInt(this.value) || 0;

        if (e.button === 0) val++;      
        else if (e.button === 2) val--; 
        
        // 【核心拦截】：全局不允许出现负数
        if (val < 0) val = 0;

        // 【显示逻辑】：AK 模式下 0 显示为 -
        this.value = (isAK && val === 0) ? '-' : val;
        this.select(); 
    });

    // 鼠标滚轮逻辑
    inputElem.addEventListener('wheel', function(e) {
        if (document.activeElement === this) {
            e.preventDefault();
            let val = parseInt(this.value) || 0;

            if (e.deltaY < 0) val++;      
            else val--;                   

            if (val < 0) val = 0;
            this.value = (isAK && val === 0) ? '-' : val;
            this.select();
        }
    });

    // 键盘输入拦截：只允许数字，禁止负号
    inputElem.addEventListener('input', function() {
        this.value = this.value.replace(/[^0-9]/g, '');
    });

    // 失去焦点格式化
    inputElem.addEventListener('blur', function() {
        if (this.value.trim() === '' || this.value === '0') {
            this.value = isAK ? '-' : '0';
        }
    });
}

// 初始化大比分
document.querySelectorAll('.team-score-input').forEach(input => {
    input.type = 'text'; // 统一用 text 类型
    bindProNumberControls(input);
});

// ==========================================
// 3. 核心功能函数
// ==========================================
function checkDuplicateName(currentInput) {
    const val = currentInput.value.trim();
    if (!val) return false;

    const allInputs = document.querySelectorAll('.name-input');
    for (let input of allInputs) {
        if (input !== currentInput && input.value.trim() === val) {
            currentInput.value = ''; 
            return true;
        }
    }
    return false;
}

function createPlayerRow() {
    const row = document.createElement('div');
    row.className = 'player-row';
    
    // 【核心改动】：统一切换为 type="text"，AK默认值为 "-"
    row.innerHTML = `
        <div class="name-wrapper">
            <input type="text" class="name-input" placeholder="名字" autocomplete="off">
            <button class="gear-btn" title="管理小号">⚙️</button>
            <div class="popover autocomplete-popover"></div>
            <div class="popover alias-popover"></div>
        </div>
        <div class="stats-group">
            <div class="stat-item"><input type="text" class="stat-kill" value="0"></div>
            <div class="stat-item"><input type="text" class="stat-death" value="0"></div>
            <div class="stat-item"><input type="text" class="stat-ak" value="-"></div>
        </div>
    `;

    const nameInput = row.querySelector('.name-input');
    const gearBtn = row.querySelector('.gear-btn');
    const autocompletePopover = row.querySelector('.autocomplete-popover');
    const aliasPopover = row.querySelector('.alias-popover');

    bindProNumberControls(row.querySelector('.stat-kill'));
    bindProNumberControls(row.querySelector('.stat-death'));
    bindProNumberControls(row.querySelector('.stat-ak'), true); 

    // 键盘导航
    let currentFocusIndex = -1;
    function clearActiveItems() {
        const items = autocompletePopover.querySelectorAll('.suggestion-item');
        items.forEach(item => item.classList.remove('keyboard-focus'));
    }

    nameInput.addEventListener('keydown', function(e) {
        if (!autocompletePopover.classList.contains('active')) return;
        const items = autocompletePopover.querySelectorAll('.suggestion-item');
        if (items.length === 0) return;

        if (e.key === 'ArrowDown') {
            e.preventDefault(); 
            currentFocusIndex++;
            if (currentFocusIndex >= items.length) currentFocusIndex = 0; 
            clearActiveItems();
            items[currentFocusIndex].classList.add('keyboard-focus');
        } 
        else if (e.key === 'ArrowUp') {
            e.preventDefault();
            currentFocusIndex--;
            if (currentFocusIndex < 0) currentFocusIndex = items.length - 1; 
            clearActiveItems();
            items[currentFocusIndex].classList.add('keyboard-focus');
        } 
        else if (e.key === 'Enter') {
            e.preventDefault();
            if (currentFocusIndex > -1) {
                items[currentFocusIndex].click(); 
            }
        }
    });

    let isComposing = false; 
    nameInput.addEventListener('compositionstart', () => { isComposing = true; });
    nameInput.addEventListener('compositionend', function() {
        isComposing = false;
        processInputLogic(this); 
    });

    nameInput.addEventListener('input', function() {
        if (!isComposing) processInputLogic(this);
    });

    nameInput.addEventListener('change', function() {
        checkDuplicateName(this);
    });

    function processInputLogic(inputElem) {
        let val = inputElem.value;
        let byteLen = 0;
        let newVal = '';
        for (let i = 0; i < val.length; i++) {
            let charCode = val.charCodeAt(i);
            byteLen += (charCode > 127) ? 2 : 1;
            if (byteLen > 4) break;
            newVal += val[i];
        }
        if (inputElem.value !== newVal) inputElem.value = newVal; 

        aliasPopover.classList.remove('active'); 
        currentFocusIndex = -1; 

        if (!newVal.trim()) {
            autocompletePopover.classList.remove('active');
            return;
        }

        const matches = Object.keys(playerDB).filter(name => name.includes(newVal.trim()));
        if (matches.length > 0) {
            autocompletePopover.innerHTML = matches.map(name => 
                `<div class="popover-item suggestion-item">${name}</div>`
            ).join('');
            autocompletePopover.classList.add('active');

            autocompletePopover.querySelectorAll('.suggestion-item').forEach(item => {
                item.addEventListener('click', (e) => {
                    e.stopPropagation();
                    inputElem.value = item.innerText;
                    autocompletePopover.classList.remove('active');
                    currentFocusIndex = -1;
                    
                    if (!checkDuplicateName(inputElem)) {
                        document.querySelectorAll('.popover').forEach(pop => pop.classList.remove('active'));
                        renderAliasMenu(inputElem.value.trim());
                        aliasPopover.classList.add('active');
                    }
                });
            });
        } else {
            autocompletePopover.classList.remove('active');
        }
    }

    function renderAliasMenu(playerName) {
        let html = '';
        if (playerDB[playerName] && playerDB[playerName].length > 0) {
            html += playerDB[playerName].map((alias, idx) => 
                `<div class="popover-item" style="display:flex; justify-content:space-between; align-items:center;">
                    <span>🎮 ${alias}</span>
                    <span class="unbind-btn" data-idx="${idx}">解绑</span>
                </div>`
            ).join('');
        } else {
            html += `<div class="popover-item" style="color:var(--text-muted);">暂无小号</div>`;
        }
        html += `<div class="popover-item add-alias-btn">+ 绑定新小号</div>`;
        
        aliasPopover.innerHTML = html;

        aliasPopover.querySelector('.add-alias-btn').addEventListener('click', (e) => {
            e.stopPropagation(); 
            const newAlias = prompt(`为【${playerName}】绑定新小号:`);
            if (newAlias && newAlias.trim() !== '') {
                const aliasTrimmed = newAlias.trim();
                if(!playerDB[playerName]) playerDB[playerName] = [];
                if (playerDB.hasOwnProperty(aliasTrimmed)) return;
                for (const mainName in playerDB) {
                    if (playerDB[mainName].includes(aliasTrimmed)) return;
                }
                playerDB[playerName].push(aliasTrimmed);
                renderAliasMenu(playerName); 
            }
        });

        aliasPopover.querySelectorAll('.unbind-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                e.stopPropagation(); 
                const idx = e.target.getAttribute('data-idx');
                playerDB[playerName].splice(idx, 1); 
                renderAliasMenu(playerName); 
            });
        });
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
            renderAliasMenu(playerName); 
            aliasPopover.classList.add('active');
        }
    });

    return row;
}

// 初始化 UI
const blueTeam = document.getElementById('team-blue');
const redTeam = document.getElementById('team-red');
for (let i = 0; i < 4; i++) {
    blueTeam.appendChild(createPlayerRow());
    redTeam.appendChild(createPlayerRow());
}

document.addEventListener('click', function(e) {
    document.querySelectorAll('.popover').forEach(pop => {
        if (!pop.contains(e.target)) {
            pop.classList.remove('active');
            currentFocusIndex = -1; 
        }
    });
});

// 控制台按钮
document.getElementById('btn-swap').addEventListener('click', function() {
    const container = document.getElementById('main-container');
    container.style.flexDirection = container.style.flexDirection === 'row-reverse' ? 'row' : 'row-reverse';
});

let isMonitoring = false;
const btnMonitor = document.getElementById('btn-monitor');
btnMonitor.addEventListener('click', function() {
    isMonitoring = !isMonitoring;
    this.innerHTML = isMonitoring ? '🛑 停止' : '▶ 运行';
    this.className = isMonitoring ? 'ctrl-btn btn-monitor-stop' : 'ctrl-btn btn-monitor-start';
});

document.getElementById('btn-auth').addEventListener('click', function() {
    const code = prompt("请输入新的授权卡密 (CDK):");
});

