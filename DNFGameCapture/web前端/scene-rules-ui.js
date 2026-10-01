/* ================= 主播工具 → 场景规则（情景模式）编辑器 =================
 * 规则定义 / 匹配逻辑在 scene-rules.js（与击杀展示窗口共用）。
 * 数据流：C++ 在 sync_state 里带上 sceneRules { rev, data }；本页首次收到时载入，之后以本页编辑为准，
 * 修改 0.4 秒后通过 cmd_scene_rules_save 保存到 %APPDATA%\DNFGameCapture\scene_rules.json，
 * 击杀小窗 / 全屏特效窗口按 sceneRulesRev 热更新。
 * 预设规则（双杀 / 三杀 / 一血 / 终结 / 复仇 / AK / 胜利）的开关与「特效管理 → 触发事件」双向同步。
 * ======================================================================= */
const SceneRulesUI = (() => {
    'use strict';
    const S = () => window.SceneRules;
    const PRESET_EVT = {
        'preset-ak': 'fxEvtAk', 'preset-triple': 'fxEvtTriple', 'preset-double': 'fxEvtDouble',
        'preset-first': 'fxEvtFirst', 'preset-shutdown': 'fxEvtShutdown', 'preset-revenge': 'fxEvtRevenge',
        'preset-victory': 'fxEvtVictory'
    };
    let rules = null;
    let loaded = false;
    let selectedId = null;
    let saveTimer = null;
    let everSaved = false;
    let customVoices = [];           // [{ id, name, size }]：声音管理 → 自定义语音
    let customVoicesSig = '';
    // 豆包音色生成 + 服务器音频库（C++ sync_state 里的 ttsGen）
    let tts = { ready: false, loaded: false, enabled: false, voices: [], maxTextChars: 30, usage: null, library: [] };
    let ttsVoicesSig = '';
    let ttsLibrarySig = '';

    const $ = id => document.getElementById(id);
    const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
    const currentLayout = () => {
        try { return normalizeKillDisplayLayout(killDisplaySettings.layout); } catch (err) { return {}; }
    };
    const selected = () => (rules || []).find(r => r.id === selectedId) || null;

    /* ---------- 数据 ---------- */
    function receive(snapshot) {
        if (loaded || !S() || !snapshot) return;
        loaded = true;
        const list = snapshot.data && Array.isArray(snapshot.data.rules) ? snapshot.data.rules : null;
        everSaved = !!list;
        lastGaoJson = JSON.stringify([snapshot.data && snapshot.data.gao || {}, snapshot.data && snapshot.data.aliases || {}]);
        rules = S().normalize(list, currentLayout());
        addBundledScenes();
        if (!selectedId && rules.length) selectedId = rules[0].id;
        render();
    }

    // 按主播要求加入的场景：每台电脑只自动加一次（之后删掉或修改都以主播为准）
    const BUNDLED_SCENES = [{
        id: 'scene-maqu-left-dead',
        make: () => ({
            id: 'scene-maqu-left-dead', name: '马区左侧白给', enabled: true, trigger: 'kill', match: 'all',
            conditions: [
                { f: 'victim', op: 'is', v: ['马区', ...aliasesOf('马区')] },   // 选手是马区（含别名）且死了
                { f: 'victimStreak', op: '==', v: 0 },                          // 马区连杀为 0
                { f: 'hpLost', op: '<', v: 30 },                                // 击杀者本局掉血 < 30%（白给）
                { f: 'victimLeft', op: 'is', v: true }                          // 马区在左侧队伍（不分红蓝 / 翻转）
            ],
            action: { effect: 'none', voice: 'lib', voiceLibTitle: '马搞你是不是要搞' },
            append: true, appendMs: 500, cooldownSec: 0
        })
    }, {
        id: 'scene-maqu-right-dead',
        make: () => ({
            id: 'scene-maqu-right-dead', name: '马区右侧白给', enabled: true, trigger: 'kill', match: 'all',
            conditions: [
                { f: 'victim', op: 'is', v: ['马区', ...aliasesOf('马区')] },   // 选手是马区（含别名）且死了
                { f: 'victimStreak', op: '==', v: 0 },                          // 马区连杀为 0
                { f: 'hpLost', op: '<', v: 30 },                                // 击杀者本局掉血 < 30%（白给）
                { f: 'victimLeft', op: 'not', v: true }                         // 马区在右侧队伍（不分红蓝 / 翻转）
            ],
            action: { effect: 'none', voice: 'lib', voiceLibTitle: '哈哈还好有你', voicePrefix: '{死者搞名}' },
            append: true, appendMs: 500, cooldownSec: 0
        })
    }, {
        id: 'scene-awang-right-dead',
        make: () => ({
            id: 'scene-awang-right-dead', name: '阿旺右侧白给', enabled: true, trigger: 'kill', match: 'all',
            conditions: [
                { f: 'victim', op: 'is', v: ['阿旺', ...aliasesOf('阿旺')] },   // 选手是阿旺（含别名）且死了
                { f: 'victimStreak', op: '==', v: 0 },                          // 阿旺连杀为 0
                { f: 'hpLost', op: '<', v: 30 },                                // 击杀者本局掉血 < 30%（白给）
                { f: 'victimLeft', op: 'not', v: true }                         // 阿旺在右侧队伍（不分红蓝 / 翻转）
            ],
            action: { effect: 'none', voice: 'lib', voiceLibTitle: '真菜呀啊旺' },
            append: true, appendMs: 500, cooldownSec: 0
        })
    }, {
        id: 'scene-gao-drought4',
        make: () => ({
            id: 'scene-gao-drought4', name: '连死 4 次没人头 · 你是不是要搞', enabled: true, trigger: 'kill', match: 'all',
            conditions: [
                { f: 'victimDrought', op: '==', v: 4 }                          // 死者连续阵亡 4 次都没拿到人头（刚好第 4 次时播一次）
            ],
            action: { effect: 'none', voice: 'lib', voiceLibTitle: '你是不是要搞', voicePrefix: '{死者搞名}' },
            append: true, appendMs: 500, cooldownSec: 0
        })
    }, {
        id: 'scene-gao-finally',
        make: () => ({
            id: 'scene-gao-finally', name: '连死 4 次后终于拿人头 · 终于像个人了', enabled: true, trigger: 'kill', match: 'all',
            conditions: [
                { f: 'killerDrought', op: '>=', v: 4 }                          // 击杀者之前连续阵亡 ≥ 4 次没人头，这次终于拿到
            ],
            action: { effect: 'none', voice: 'lib', voiceLibTitle: '终于像个人了', voicePrefix: '{击杀者搞名}' },
            append: true, appendMs: 500, cooldownSec: 0
        })
    }];
    function addBundledScenes() {
        let added = false;
        BUNDLED_SCENES.forEach(scene => {
            const flag = 'dnf-scene-added:' + scene.id;
            try {
                if (localStorage.getItem(flag)) return;
                localStorage.setItem(flag, '1');
            } catch (err) { return; }
            if (rules.some(r => r.id === scene.id)) return;
            const r = S().normalizeRule(scene.make());
            if (r) { rules.push(r); added = true; }
        });
        // 更正：早先加入的「马区右侧阵亡」误配成了「真菜呀啊旺」（那句属于阿旺），马区右侧应播「哈哈还好有你」
        try {
            const fixFlag = 'dnf-scene-fix:maqu-right-voice';
            if (!localStorage.getItem(fixFlag)) {
                localStorage.setItem(fixFlag, '1');
                const r = rules.find(x => x.id === 'scene-maqu-right-dead');
                if (r && r.action.voice === 'lib' && r.action.voiceLibTitle === '真菜呀啊旺') {
                    r.action.voiceLibTitle = '哈哈还好有你';
                    r.action.voiceLib = '';
                    added = true;
                }
            }
        } catch (err) { /* ignore */ }
        // 改成「白给」：击杀者本局掉血 < 15%（替换原来的「击杀者剩余血量 < 20%」），名称改为 X左侧/右侧白给；语音等其他设置保留
        try {
            const fixFlag = 'dnf-scene-fix:baigei-hplost15';
            if (!localStorage.getItem(fixFlag)) {
                localStorage.setItem(fixFlag, '1');
                const names = { 'scene-maqu-left-dead': '马区左侧白给', 'scene-maqu-right-dead': '马区右侧白给', 'scene-awang-right-dead': '阿旺右侧白给' };
                rules.forEach(r => {
                    if (!names[r.id]) return;
                    r.name = names[r.id];
                    const hp = r.conditions.findIndex(c => c.f === 'killerHp');
                    const cond = S().normalizeRule({ trigger: 'kill', conditions: [{ f: 'hpLost', op: '<', v: 30 }] }).conditions[0];
                    if (hp >= 0) r.conditions[hp] = cond;
                    else if (!r.conditions.some(c => c.f === 'hpLost')) r.conditions.push(cond);
                    added = true;
                });
            }
        } catch (err) { /* ignore */ }
        // 马区右侧白给：服务器音频前面先念 {死者搞名}
        try {
            const fixFlag = 'dnf-scene-fix:maqu-right-prefix';
            if (!localStorage.getItem(fixFlag)) {
                localStorage.setItem(fixFlag, '1');
                const r = rules.find(x => x.id === 'scene-maqu-right-dead');
                if (r && r.action.voice === 'lib' && !r.action.voicePrefix) { r.action.voicePrefix = '{死者搞名}'; added = true; }
            }
        } catch (err) { /* ignore */ }
        // 白给的掉血阈值 15% → 30%
        try {
            const fixFlag = 'dnf-scene-fix:baigei-hplost30';
            if (!localStorage.getItem(fixFlag)) {
                localStorage.setItem(fixFlag, '1');
                rules.forEach(r => {
                    if (!['scene-maqu-left-dead', 'scene-maqu-right-dead', 'scene-awang-right-dead'].includes(r.id)) return;
                    r.conditions.forEach(c => { if (c.f === 'hpLost' && c.op === '<' && c.v !== 30) { c.v = 30; added = true; } });
                });
            }
        } catch (err) { /* ignore */ }
        if (added) save();
    }

    // 搞名表：选手（含所有别名，小写）→ 他以「搞」结尾的别名；随规则一起保存，展示窗口用来替换 {死者搞名} / {击杀者搞名}
    let lastGaoJson = null;
    function gaoMap() {
        const map = {};
        try {
            identityGroupsByName.forEach((group, name) => {
                const names = [name, ...(group && Array.isArray(group.names) ? group.names : [])].map(n => String(n || '').trim()).filter(Boolean);
                // 优先纯汉字的搞名（例如「马搞」而不是「🐴搞」，豆包念不了表情），再优先首字出现在他其他名字里的
                const cands = names.filter(n => n.length > 1 && n.endsWith('搞'));
                const score = n => (/^[\u4e00-\u9fff]+$/.test(n) ? 2 : 0) + (names.some(o => o !== n && o.includes(n[0])) ? 1 : 0);
                const gao = cands.sort((a, b) => score(b) - score(a))[0];
                if (gao) names.forEach(n => { map[n.toLowerCase()] = gao; });
            });
        } catch (err) { /* ignore */ }
        return map;
    }
    // 别名分组表：名字（小写）→ 组号。选手条件按「同一组」匹配，所以规则里只写一个名字也能匹配这个选手的所有别名
    function aliasGroupMap() {
        const map = {};
        let next = 1;
        try {
            identityGroupsByName.forEach((group, name) => {
                const names = [name, ...(group && Array.isArray(group.names) ? group.names : [])].map(n => String(n || '').trim().toLowerCase()).filter(Boolean);
                if (names.length < 2) return;
                const id = names.map(n => map[n]).find(x => x != null) || next++;
                names.forEach(n => { map[n] = id; });
            });
        } catch (err) { /* ignore */ }
        return map;
    }
    // 选手库是异步读的：读好 / 别名改了之后，搞名表 / 别名表有变化就重新保存一次
    function syncGaoMap() {
        if (!rules || !everSaved) return;
        if (JSON.stringify([gaoMap(), aliasGroupMap()]) !== lastGaoJson) save();
    }
    setInterval(syncGaoMap, 5000);

    function save(immediate = false) {
        if (!rules) return;
        clearTimeout(saveTimer);
        const run = () => {
            everSaved = true;
            const gao = gaoMap(), aliases = aliasGroupMap();
            lastGaoJson = JSON.stringify([gao, aliases]);
            window.chrome?.webview?.postMessage({ action: 'cmd_scene_rules_save', data: { version: S().VERSION, rules, gao, aliases } });
            setStatus('已保存，展示窗口约 1 秒内生效');
        };
        if (immediate) run(); else saveTimer = setTimeout(run, 400);
    }

    function setStatus(text) {
        const el = $('scr-status');
        if (el) el.textContent = text;
    }

    // 「特效管理 → 触发事件」勾选变化 → 同步对应预设规则的开关
    function onEventToggle(key, value) {
        if (!rules) return;
        const on = Number(value) !== 0;
        let changed = false;
        rules.forEach(r => {
            if (PRESET_EVT[r.preset] === key && r.enabled !== on) { r.enabled = on; changed = true; }
        });
        if (changed) { save(); renderList(); }
    }
    function syncFromLayout() {
        const L = currentLayout();
        Object.entries(PRESET_EVT).forEach(([, key]) => onEventToggle(key, L[key]));
    }
    // 预设规则开关变化 → 同步「触发事件」勾选（击杀语音也看这个开关）
    function mirrorToLayout(rule) {
        const key = PRESET_EVT[rule.preset];
        if (!key || typeof setFxManagerValue !== 'function') return;
        if ((currentLayout()[key] !== 0) !== rule.enabled) setFxManagerValue(key, rule.enabled ? 1 : 0);
    }

    /* ---------- 选手（含别名） ---------- */
    function knownPlayers() {
        const names = new Set();
        try { identityGroupsByName.forEach((_, name) => names.add(name)); } catch (err) { /* ignore */ }
        try { Object.keys(playerDB || {}).forEach(name => names.add(name)); } catch (err) { /* ignore */ }
        document.querySelectorAll('.player-row .name-input').forEach(input => { if (input.value.trim()) names.add(input.value.trim()); });
        return [...names].filter(Boolean).sort((a, b) => a.localeCompare(b, 'zh-CN'));
    }
    function aliasesOf(name) {
        try {
            const group = identityGroupsByName.get(name);
            if (group && Array.isArray(group.names)) return group.names.filter(n => n && n !== name);
        } catch (err) { /* ignore */ }
        return [];
    }

    /* ---------- 描述 ---------- */
    function summary(r) {
        const R = S();
        const conds = r.conditions.map(R.describeCondition).filter(Boolean);
        const when = conds.length ? conds.join(r.match === 'any' ? ' 或 ' : ' 且 ') : '无条件';
        let what = R.effectLabel(r.action.effect);
        if (r.action.effect === 'custom') what = '横幅「' + (r.action.title || '精彩') + '」';
        else if (r.action.effect === 'none') what = '不弹横幅';
        if (r.action.voice === 'text' && r.action.voiceText) what += ' + 语音「' + r.action.voiceText + '」';
        else if (r.action.voice === 'tts' && r.action.voiceText) what += ' + ' + ttsVoiceName(r.action.voiceTts) + '「' + r.action.voiceText + '」';
        else if (r.action.voice === 'file') what += ' + 自定义语音「' + voiceName(r.action.voiceFile) + '」';
        else if (r.action.voice === 'lib') what += ' + ' + (r.action.voicePrefix ? r.action.voicePrefix + ' + ' : '') + '服务器音频「' + (libItemFor(r.action)?.title || r.action.voiceLibTitle || '未选择') + '」';
        else if (r.action.voice === 'none') what += ' · 静音';
        return when + ' → ' + what;
    }

    /* ---------- 渲染 ---------- */
    function render() {
        renderList();
        renderEditor();
    }

    function renderList() {
        const box = $('scr-list');
        if (!box) return;
        if (!rules) { box.innerHTML = '<div class="scr-empty">等待主程序数据…</div>'; return; }
        const R = S();
        const view = listView();
        const firstOff = view.findIndex(r => !r.enabled);
        const onCount = firstOff < 0 ? view.length : firstOff;
        box.innerHTML = view.map((r, i) => `${i === firstOff ? `<div class="scr-group">已停用（${view.length - onCount} 条，不参与匹配）</div>` : ''}
            <div class="scr-item${r.enabled ? '' : ' is-off'}${r.id === selectedId ? ' is-sel' : ''}" data-id="${esc(r.id)}" role="listitem">
                <label class="fxm-switch" title="${r.enabled ? '已启用' : '已停用'}"><input type="checkbox" data-act="toggle"${r.enabled ? ' checked' : ''} aria-label="启用 ${esc(r.name)}"><i aria-hidden="true"></i></label>
                <div class="scr-item-main" data-act="select">
                    <div class="scr-item-name"><span class="scr-no">${r.enabled ? i + 1 : '–'}</span>${esc(r.name)}
                        <span class="scr-badge">${esc(R.triggerLabel(r.trigger).replace(/（.*$/, ''))}</span>${r.preset ? '<span class="scr-badge is-preset">预设</span>' : ''}${r.append ? '<span class="scr-badge is-append">追加</span>' : ''}</div>
                    <div class="scr-item-sum">${esc(summary(r))}</div>
                </div>
                <div class="scr-item-ord">
                    <button type="button" data-act="up" title="上移（优先级更高）"${i === 0 || i === firstOff ? ' disabled' : ''}>▲</button>
                    <button type="button" data-act="down" title="下移"${i === view.length - 1 || i === onCount - 1 ? ' disabled' : ''}>▼</button>
                </div>
            </div>`).join('') || '<div class="scr-empty">还没有规则，点击「新建规则」。</div>';
    }

    // 列表显示顺序：开着的在上（按优先级），关着的统一放到下面；停用的规则不参与匹配，所以不影响播放逻辑
    function listView() {
        return [...rules.filter(r => r.enabled), ...rules.filter(r => !r.enabled)];
    }
    // ▲▼ 在同一组（启用 / 停用）里和相邻的那条交换位置
    function moveRule(r, dir) {
        const view = listView();
        const vi = view.indexOf(r);
        const other = view[vi + dir];
        if (!other || other.enabled !== r.enabled) return false;
        const a = rules.indexOf(r), b = rules.indexOf(other);
        rules[a] = other; rules[b] = r;
        return true;
    }

    function optionList(pairs, value) {
        return pairs.map(([id, label]) => `<option value="${esc(id)}"${String(id) === String(value) ? ' selected' : ''}>${esc(label)}</option>`).join('');
    }

    function playerPickerHtml(names, index, side) {
        names = Array.isArray(names) ? names : [];
        const s = side == null ? '' : ` data-side="${side}"`;
        // 匹配时按选手库别名分组判断（SceneRules.samePlayer），写一个名字就等于匹配他的所有别名；
        // 这里只显示选手本身，别名数量放在徽标里、完整列表放在悬停提示里。旧规则里存下的别名同样折叠不显示。
        const R = window.SceneRules;
        if (R && R.setAliases) R.setAliases(aliasGroupMap());   // 主窗口也按选手库别名分组判断「同一个选手」
        const same = (a, b) => (R && R.samePlayer ? R.samePlayer(a, b) : a === b);
        const chips = names.map((n, k) => {
            if (names.slice(0, k).some(p => same(p, n))) return '';   // 前面某个选手的别名：折叠
            const alias = aliasesOf(n);
            const tip = alias.length ? '同时匹配所有别名：' + alias.join('、') : '同时匹配选手库里的所有别名';
            return `<span class="scr-chip is-main" title="${esc(tip)}">${esc(n)}${alias.length ? `<small>含 ${alias.length} 个别名</small>` : ''}<button type="button" data-act="del-name" data-i="${index}" data-k="${k}"${s} title="移除">×</button></span>`;
        }).join('');
        return `<div class="scr-player">
                <input type="text" list="scr-player-names" placeholder="${names.length ? '再添加其他选手，回车' : (side == null ? '输入或选择选手，回车' : (side === 0 ? '第一位选手，回车' : '第二位选手，回车'))}" data-cond="${index}" data-part="name"${s}>
                <div class="scr-chips">${chips || '<span class="scr-muted">未选择（自动匹配该选手的所有别名）</span>'}</div>
            </div>`;
    }
    function conditionValueHtml(c, f, index) {
        if (f.type === 'player' || f.type === 'alive') return playerPickerHtml(c.v, index);
        if (f.type === 'pair') {
            const v = Array.isArray(c.v) ? c.v : [[], []];
            return `<div class="scr-pair">${playerPickerHtml(v[0], index, 0)}<span class="scr-unit">和</span>${playerPickerHtml(v[1], index, 1)}</div>`;
        }
        if (f.type === 'team') return `<select data-cond="${index}" data-part="v">${optionList([['red', '红队'], ['blue', '蓝队']], c.v)}</select>`;
        if (f.type === 'bool') return '';
        const unit = f.unit ? `<span class="scr-unit">${esc(f.unit)}</span>` : '';
        return `<input type="number" data-cond="${index}" data-part="v" value="${esc(c.v)}" step="1">${unit}`;
    }

    function renderEditor() {
        const box = $('scr-editor');
        if (!box) return;
        const r = selected();
        if (!r) { box.innerHTML = '<div class="scr-empty">在左侧选择一条规则进行编辑。</div>'; return; }
        const R = S();
        const fields = R.FIELDS.filter(f => f.triggers.includes(r.trigger));
        const condRows = r.conditions.map((c, i) => {
            const f = R.FIELD_MAP[c.f];
            const ops = R.OPS[f.type];
            const opHtml = f.type === 'chance' ? '<span class="scr-unit">命中率</span>'
                : `<select data-cond="${i}" data-part="op">${optionList(ops, c.op)}</select>`;
            const histHtml = f.hist ? `<select data-cond="${i}" data-part="ref" title="回看哪一次击杀">${optionList(R.HIST_REFS, c.ref)}</select>
                <label class="scr-hist-n"><span class="scr-unit">N =</span><input type="number" data-cond="${i}" data-part="n" min="1" max="99" step="1" value="${Number(c.n) || 1}"></label>` : '';
            return `<div class="scr-cond${f.hist ? ' is-hist' : ''}">
                <select data-cond="${i}" data-part="f">${optionList(fields.map(x => [x.key, x.label]), c.f)}</select>
                ${histHtml}
                ${opHtml}
                ${conditionValueHtml(c, f, i)}
                <button type="button" class="scr-icon" data-act="del-cond" data-i="${i}" title="删除条件">×</button>
            </div>`;
        }).join('');
        const a = r.action;
        const custom = a.effect === 'custom';
        const bigEffect = a.effect !== 'none';
        const vars = R.VARS.map(v => '{' + v[0] + '}').join(' ');
        box.innerHTML = `
            <div class="scr-row"><label class="scr-label">名称</label><input type="text" class="scr-grow" data-field="name" value="${esc(r.name)}" maxlength="40"></div>
            <div class="scr-row"><label class="scr-label">触发时机</label><select class="scr-grow" data-field="trigger">${optionList(R.TRIGGERS.map(t => [t.id, t.label]), r.trigger)}</select></div>
            <div class="scr-block">
                <div class="scr-block-head"><strong>条件</strong>
                    <select data-field="match">${optionList([['all', '全部满足'], ['any', '任一满足']], r.match)}</select>
                    <button type="button" class="fxm-btn scr-small" data-act="add-cond">＋ 添加条件</button></div>
                ${condRows || '<div class="scr-muted">没有条件：该触发时机下总是满足（排在它后面的规则只能作为「追加播放」）。</div>'}
                ${r.trigger === 'hp' ? '<div class="scr-muted">血量规则对红蓝两队分别判断，条件从不满足变为满足时触发，每局每队最多一次。</div>' : ''}
                ${fields.some(f => f.hp) ? '<div class="scr-muted">血量来自顶部血条的实时识别；没识别到血条时，血量条件视为不满足。</div>' : ''}
            </div>
            <div class="scr-block">
                <div class="scr-block-head"><strong>播放</strong></div>
                <div class="scr-row"><label class="scr-label">特效</label><select class="scr-grow" data-field="effect">${optionList(R.EFFECTS, a.effect)}</select></div>
                ${custom ? `<div class="scr-row"><label class="scr-label">横幅大小</label><select class="scr-grow" data-field="style">${optionList(R.STYLES, a.style)}</select></div>` : ''}
                ${bigEffect ? `<div class="scr-row"><label class="scr-label">标题</label><input type="text" class="scr-grow" data-field="title" maxlength="30" value="${esc(a.title)}" placeholder="${custom ? '例如：残血反杀' : '留空 = 使用当前风格的默认文字'}"></div>
                <div class="scr-row"><label class="scr-label">副标题</label><input type="text" class="scr-grow" data-field="sub" maxlength="60" value="${esc(a.sub)}" placeholder="${custom ? '例如：仅剩 {血量}% 血量' : '留空 = 默认'}"></div>
                <div class="scr-row"><label class="scr-label">持续时间</label><input type="number" data-field="durationSec" min="0" max="10" step="0.5" value="${a.durationMs ? a.durationMs / 1000 : 0}"><span class="scr-unit">秒（0 = 跟随「特效管理」）</span></div>` : ''}
                <div class="scr-row"><label class="scr-label">语音</label><select class="scr-grow" data-field="voice">${voiceSelectOptions(a)}</select></div>
                ${a.voice === 'lib' ? libRowHtml(a) : ''}
                ${a.voice === 'file' ? `<div class="scr-row"><label class="scr-label">音频</label><select class="scr-grow" data-field="voiceFile">${voiceFileOptions(a.voiceFile)}</select><button type="button" class="fxm-btn scr-small" data-act="voice-test"${a.voiceFile ? '' : ' disabled'}>试听</button><button type="button" class="fxm-btn scr-small" data-act="voice-import">导入…</button></div>` : ''}
                ${a.voice === 'tts' ? `<div class="scr-row"><label class="scr-label">音色</label><select class="scr-grow" data-field="voiceTts">${ttsVoiceOptions(a.voiceTts)}</select></div>
                <div class="scr-row"><label class="scr-label">朗读文字</label><input type="text" class="scr-grow" data-field="voiceText" maxlength="80" value="${esc(a.voiceText)}" placeholder="例如：{击杀者}残血反杀！（替换变量后最多 ${Number(tts.maxTextChars) || 30} 字）"></div>
                <div class="scr-muted">${esc(ttsRuleHint())}</div>` : ''}
                ${a.voice === 'text' ? `<div class="scr-row"><label class="scr-label">朗读文字</label><input type="text" class="scr-grow" data-field="voiceText" maxlength="80" value="${esc(a.voiceText)}" placeholder="例如：{击杀者}残血反杀！"></div>` : ''}
                <div class="scr-muted">可用变量：${esc(vars)}　　语音需在「声音管理」开启语音播报。</div>
            </div>
            <div class="scr-block">
                <div class="scr-block-head"><strong>高级</strong></div>
                <label class="scr-check"><input type="checkbox" data-field="append"${r.append ? ' checked' : ''}> 追加播放：前面已有规则命中时，在主特效播完后再播这条</label>
                ${r.append ? `<div class="scr-row"><label class="scr-label">追加时长</label><input type="number" data-field="appendSec" min="0.5" max="10" step="0.5" value="${r.appendMs / 1000}"><span class="scr-unit">秒</span></div>` : ''}
                <div class="scr-row"><label class="scr-label">冷却</label><input type="number" data-field="cooldownSec" min="0" max="3600" step="1" value="${r.cooldownSec}"><span class="scr-unit">秒内不重复触发（0 = 不限制）</span></div>
            </div>
            <div class="scr-actions">
                <button type="button" class="fxm-btn primary" data-act="test" title="用示例数据在展示窗口播放一次（不看条件）">▶ 测试播放</button>
                ${r.preset ? '<button type="button" class="fxm-btn" data-act="restore">恢复默认</button>' : ''}
                <button type="button" class="fxm-btn" data-act="copy">复制</button>
                <button type="button" class="fxm-btn scr-danger" data-act="delete">删除</button>
            </div>`;
    }

    /* ---------- 编辑 ---------- */
    function changed(structural) {
        save();
        renderList();
        if (structural) renderEditor();
    }

    function onEditorInput(event) {
        const r = selected();
        const el = event.target;
        if (!r || !el) return;
        const R = S();
        const field = el.dataset.field;
        if (field) {
            const a = r.action;
            switch (field) {
                case 'name': r.name = el.value.slice(0, 40) || '未命名规则'; changed(false); return;
                case 'title': a.title = el.value.slice(0, 30); changed(false); return;
                case 'sub': a.sub = el.value.slice(0, 60); changed(false); return;
                case 'voiceText': a.voiceText = el.value.slice(0, 80); changed(false); return;
                case 'voicePrefix': a.voicePrefix = el.value.slice(0, 40); changed(false); return;
                case 'durationSec': a.durationMs = Math.max(0, Math.min(10000, Math.round((Number(el.value) || 0) * 1000))); changed(false); return;
                case 'appendSec': r.appendMs = Math.max(500, Math.min(10000, Math.round((Number(el.value) || 1.5) * 1000))); changed(false); return;
                case 'cooldownSec': r.cooldownSec = Math.max(0, Math.min(3600, Math.round(Number(el.value) || 0))); changed(false); return;
                default: break;
            }
            if (event.type !== 'change') return;
            if (field === 'trigger') {
                r.trigger = el.value;
                r.conditions = r.conditions.filter(c => R.FIELD_MAP[c.f].triggers.includes(r.trigger));
                if (r.trigger !== 'kill' && ['double', 'triple', 'first', 'shutdown', 'revenge'].includes(a.effect)) a.effect = 'custom';
            }
            else if (field === 'match') r.match = el.value === 'any' ? 'any' : 'all';
            else if (field === 'effect') a.effect = el.value;
            else if (field === 'style') a.style = el.value;
            else if (field === 'voice') {
                const val = String(el.value);
                if (val.startsWith('lib:')) {
                    // 下拉框里直接选了某条服务器音频
                    const item = tts.library.find(x => x.sha256 === val.slice(4));
                    a.voice = 'lib';
                    if (item) { a.voiceLib = item.sha256; a.voiceLibTitle = String(item.title || item.text || '').slice(0, 60); }
                }
                else if (val.startsWith('file:')) { a.voice = 'file'; a.voiceFile = val.slice(5); }
                else {
                    a.voice = val;
                    if (a.voice === 'file' && !a.voiceFile && customVoices.length) a.voiceFile = customVoices[0].id;
                    if (a.voice === 'lib' && !a.voiceLib && !a.voiceLibTitle && tts.library.length) {
                        a.voiceLib = tts.library[0].sha256;
                        a.voiceLibTitle = String(tts.library[0].title || tts.library[0].text || '').slice(0, 60);
                    }
                }
            }
            else if (field === 'voiceFile') a.voiceFile = el.value;
            else if (field === 'voiceTts') a.voiceTts = el.value;
            else if (field === 'append') r.append = el.checked;
            changed(true);
            return;
        }
        const index = Number(el.dataset.cond);
        const c = r.conditions[index];
        if (!c) return;
        const part = el.dataset.part;
        if (part === 'n') {
            c.n = Math.max(1, Math.min(99, Math.round(Number(el.value) || 1)));
            changed(false);
            return;
        }
        if (part === 'v') {
            const f = R.FIELD_MAP[c.f];
            c.v = f.type === 'team' ? el.value : Math.round(Number(el.value) || 0);
            changed(false);
            return;
        }
        if (event.type !== 'change') return;
        if (part === 'f') {
            const f = R.FIELD_MAP[el.value];
            r.conditions[index] = R.normalizeRule({ trigger: r.trigger, conditions: [{ f: f.key, v: defaultValue(f) }] }).conditions[0];
            changed(true);
        }
        else if (part === 'op') { c.op = el.value; changed(false); }
        else if (part === 'ref') { c.ref = el.value; changed(false); }
        else if (part === 'name') addPlayerName(c, el.value, el.dataset.side);
    }

    // side = 0 / 1：「两位选手是否同队」的第一位 / 第二位
    function addPlayerName(c, raw, side) {
        const name = String(raw || '').trim();
        if (!name) return;
        const pair = side === 0 || side === 1 || side === '0' || side === '1';
        if (pair && !(Array.isArray(c.v) && c.v.length === 2)) c.v = [[], []];
        const cur = pair ? c.v[Number(side)] : c.v;
        const list = Array.isArray(cur) ? cur.slice() : [];
        // 只存选手名：匹配时自动包含他的所有别名（别名以后在选手库里增删也会跟着生效）
        if (!list.includes(name)) list.push(name);
        if (pair) c.v[Number(side)] = list.slice(0, 40);
        else c.v = list.slice(0, 40);
        changed(true);
    }

    function defaultValue(f) {
        if (f.type === 'player' || f.type === 'alive') return [];
        if (f.type === 'pair') return [[], []];
        if (f.type === 'team') return 'red';
        if (f.type === 'bool') return true;
        if (f.type === 'chance') return 50;
        if (f.hp) return 30;
        return 2;
    }

    function onEditorClick(event) {
        const btn = event.target.closest('[data-act]');
        const r = selected();
        if (!btn || !r) return;
        const R = S();
        const act = btn.dataset.act;
        if (act === 'add-cond') {
            const f = R.FIELDS.find(x => x.triggers.includes(r.trigger) && x.type !== 'chance') || R.FIELDS.find(x => x.triggers.includes(r.trigger));
            if (!f) return;
            r.conditions.push(R.normalizeRule({ trigger: r.trigger, conditions: [{ f: f.key, v: defaultValue(f) }] }).conditions[0]);
            changed(true);
        }
        else if (act === 'del-cond') { r.conditions.splice(Number(btn.dataset.i), 1); changed(true); }
        else if (act === 'del-name') {
            const c = r.conditions[Number(btn.dataset.i)];
            const list = c && (btn.dataset.side != null ? (Array.isArray(c.v) ? c.v[Number(btn.dataset.side)] : null) : c.v);
            if (Array.isArray(list)) {
                // 移除选手时连同旧规则里存下的他的别名一起移除（界面上已折叠不显示）
                const R = window.SceneRules;
                const k = Number(btn.dataset.k);
                const name = list[k];
                const keep = list.filter((n, i) => i !== k && !(R && R.samePlayer && R.samePlayer(n, name)));
                list.splice(0, list.length, ...keep);
                changed(true);
            }
        }
        else if (act === 'voice-test') {
            if (r.action.voiceFile) window.chrome?.webview?.postMessage({ action: 'cmd_custom_voice_test', id: r.action.voiceFile });
        }
        else if (act === 'voice-import') importVoices();
        else if (act === 'lib-test') {
            const item = libItemFor(r.action);
            post({ action: 'cmd_voice_library_preview', sha256: item ? item.sha256 : r.action.voiceLib, title: r.action.voiceLibTitle });
            setStatus('试听：' + (item?.title || r.action.voiceLibTitle || '服务器音频'));
        }
        else if (act === 'test') {
            window.chrome?.webview?.postMessage({ action: 'cmd_scene_rules_test', rule: r });
            const L = currentLayout();
            setStatus(L.fxEnabled === 0 ? '「全部特效」已关闭，展示窗口不会播放' : '已发送到展示窗口（使用示例数据：90老王，剩余血量 23%）');
        }
        else if (act === 'restore') {
            const def = R.presetById(r.preset, currentLayout());
            if (!def) return;
            def.enabled = r.enabled;
            rules[rules.indexOf(r)] = def;
            selectedId = def.id;
            changed(true);
        }
        else if (act === 'copy') {
            const copy = R.normalizeRule(JSON.parse(JSON.stringify(r)));
            copy.id = R.uid();
            copy.preset = '';
            copy.name = (r.name + ' 副本').slice(0, 40);
            rules.splice(rules.indexOf(r) + 1, 0, copy);
            selectedId = copy.id;
            changed(true);
        }
        else if (act === 'delete') {
            const doDelete = () => {
                const i = rules.indexOf(r);
                rules.splice(i, 1);
                selectedId = (rules[i] || rules[i - 1] || {}).id || null;
                changed(true);
            };
            if (typeof showConfirm === 'function') {
                showConfirm(`删除规则「${esc(r.name)}」？${r.preset ? '<br><small>预设规则可通过「全部恢复默认」找回。</small>' : ''}`, ok => { if (ok) doDelete(); }, { okText: '删除', cancelText: '取消' });
            } else doDelete();
        }
    }

    function onListClick(event) {
        const item = event.target.closest('.scr-item');
        if (!item || !rules) return;
        const r = rules.find(x => x.id === item.dataset.id);
        if (!r) return;
        const act = event.target.closest('[data-act]')?.dataset.act;
        if (act === 'toggle') return; // change 事件处理
        if ((act === 'up' || act === 'down') && moveRule(r, act === 'up' ? -1 : 1)) save();
        const switched = selectedId !== r.id;
        selectedId = r.id;
        renderList();
        if (switched) { renderEditor(); const ed = $('scr-editor'); if (ed) ed.scrollTop = 0; }
    }

    function onListChange(event) {
        if (event.target.dataset.act !== 'toggle') return;
        const item = event.target.closest('.scr-item');
        const r = rules && rules.find(x => x.id === item?.dataset.id);
        if (!r) return;
        r.enabled = event.target.checked;
        mirrorToLayout(r);
        save();
        renderList();
    }

    function addRule() {
        if (!rules) return;
        const R = S();
        const r = R.normalizeRule({
            id: R.uid(), name: '新规则', trigger: 'kill', conditions: [{ f: 'killer', op: 'is', v: [] }],
            action: { effect: 'custom', style: 'big', title: '{击杀者}', sub: '精彩击杀', voice: 'default' }
        });
        rules.unshift(r);
        selectedId = r.id;
        changed(true);
        setStatus('新规则放在最上面（优先级最高），可用 ▲▼ 调整顺序');
    }

    function resetAll() {
        const run = () => {
            rules = S().presets(currentLayout());
            // 主播专属场景（马区 / 阿旺白给、连死 4 次……）不在通用预设里，原来只在第一次打开时加一次，
            // 恢复默认后就永久消失了；这里按最新内容一并加回
            BUNDLED_SCENES.forEach(scene => {
                if (rules.some(r => r.id === scene.id)) return;
                const r = S().normalizeRule(scene.make());
                if (r) rules.push(r);
            });
            selectedId = rules[0] ? rules[0].id : null;
            save(true);
            render();
            setStatus('已恢复默认规则（开关沿用「触发事件」勾选）');
        };
        if (typeof showConfirm === 'function') {
            showConfirm('将全部场景规则恢复为默认预设？<br><small>自己新建的规则会被删除；双杀 / 三杀等预设的开关沿用「特效管理 → 触发事件」。</small>', ok => { if (ok) run(); }, { okText: '恢复默认', cancelText: '取消' });
        } else run();
    }

    /* ---------- 自定义语音（本地音频） ---------- */
    function voiceName(id) {
        const v = customVoices.find(x => x.id === id);
        return v ? v.name : (id ? id.replace(/\.[^.]+$/, '') + '（已删除）' : '未选择');
    }
    function voiceFileOptions(value) {
        const list = customVoices.map(v => [v.id, v.name]);
        if (value && !customVoices.some(v => v.id === value)) list.unshift([value, voiceName(value)]);
        if (!list.length) return '<option value="">还没有导入音频，点右侧「导入…」</option>';
        return (value ? '' : '<option value="" selected>请选择</option>') + optionList(list, value);
    }
    function importVoices() {
        window.chrome?.webview?.postMessage({ action: 'cmd_custom_voice_import' });
        setVoiceStatus('请在弹出的窗口中选择音频文件（可多选）…');
    }
    function setVoiceStatus(text) {
        const el = $('cvl-status');
        if (el) el.textContent = text;
    }
    function formatSize(n) {
        n = Number(n) || 0;
        return n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB';
    }
    function usageCount(id) {
        return (rules || []).filter(r => r.action.voice === 'file' && r.action.voiceFile === id).length;
    }
    function renderVoiceLibrary() {
        const box = $('cvl-list');
        if (!box) return;
        box.innerHTML = customVoices.length ? customVoices.map(v => {
            const used = usageCount(v.id);
            return `<div class="cvl-item" data-id="${esc(v.id)}">
                <span class="cvl-icon" aria-hidden="true">♪</span>
                <div class="cvl-main"><strong>${esc(v.name)}</strong><small>${esc(v.id.split('.').pop().toUpperCase())} · ${formatSize(v.size)}${used ? ' · 用于 ' + used + ' 条规则' : ''}</small></div>
                <button type="button" class="fxm-btn scr-small" data-cvl="test">试听</button>
                <button type="button" class="fxm-btn scr-small scr-danger" data-cvl="delete">删除</button>
            </div>`;
        }).join('') : '<div class="scr-empty">还没有自定义语音。导入后到「场景规则」里，把任意规则的「语音」改成「自定义语音（本地音频）」即可使用，比如把双杀换成自己录的声音。</div>';
    }
    function receiveVoices(list) {
        const next = Array.isArray(list) ? list.filter(v => v && v.id) : [];
        const sig = next.map(v => v.id + ':' + v.size).join('|');
        if (sig === customVoicesSig) return;
        customVoicesSig = sig;
        customVoices = next;
        renderVoiceLibrary();
        renderServerLibrary();
        if (selected()?.action.voice === 'file') renderEditor();
        renderList();
    }
    function voiceResult(msg) {
        if (tts.generating) { tts.generating = false; renderTtsGen(); }
        setVoiceStatus(String(msg?.message || ''));
        setStatus(String(msg?.message || ''));
        const r = selected();
        if (msg?.ok && msg.id && r && r.action.voice === 'file' && !r.action.voiceFile) {
            r.action.voiceFile = msg.id;
            changed(true);
        }
    }
    function onLibraryClick(event) {
        const btn = event.target.closest('[data-cvl]');
        const id = btn?.closest('.cvl-item')?.dataset.id;
        if (!btn || !id) return;
        if (btn.dataset.cvl === 'test') {
            window.chrome?.webview?.postMessage({ action: 'cmd_custom_voice_test', id });
            setVoiceStatus('试听：' + voiceName(id));
        }
        else if (btn.dataset.cvl === 'delete') {
            const used = usageCount(id);
            const run = () => {
                window.chrome?.webview?.postMessage({ action: 'cmd_custom_voice_delete', id });
                setVoiceStatus('已删除：' + voiceName(id));
            };
            const msg = `删除自定义语音「${esc(voiceName(id))}」？` + (used ? `<br><small>有 ${used} 条场景规则在用它，删除后这些规则不再播放语音。</small>` : '');
            if (typeof showConfirm === 'function') showConfirm(msg, ok => { if (ok) run(); }, { okText: '删除', cancelText: '取消' });
            else run();
        }
    }

    /* ---------- 豆包音色生成 + 服务器音频库 ---------- */
    const looseTitle = s => String(s || '').toLowerCase().replace(/[\s\p{P}\p{S}]/gu, '');
    function editDistance(a, b) {
        a = [...a]; b = [...b];
        let prev = Array.from({ length: b.length + 1 }, (_, j) => j);
        for (let i = 1; i <= a.length; i++) {
            const cur = [i];
            for (let j = 1; j <= b.length; j++) cur[j] = Math.min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1));
            prev = cur;
        }
        return prev[b.length];
    }
    // 标题近似匹配（忽略标点；错一两个同音字也能对上，例如「阿旺 / 啊旺」）
    function fuzzyLibItem(lib, title) {
        const want = looseTitle(title);
        if ([...want].length < 2) return null;
        let best = null, bestScore = 0;
        lib.forEach(x => [x.title, x.text].forEach(s => {
            const cand = looseTitle(s);
            if (!cand) return;
            const len = Math.max([...cand].length, [...want].length);
            const score = cand.includes(want) || want.includes(cand) ? 0.8 + 0.2 * Math.min([...cand].length, [...want].length) / len
                : 1 - editDistance(cand, want) / len;
            if (score > bestScore) { bestScore = score; best = x; }
        }));
        return bestScore >= 0.75 ? best : null;
    }
    function libItemFor(a) {
        const lib = tts.library || [];
        const byTitle = looseTitle(a.voiceLibTitle);
        return lib.find(x => a.voiceLib && x.sha256 === a.voiceLib)
            || (byTitle ? lib.find(x => looseTitle(x.title) === byTitle) || lib.find(x => looseTitle(x.text) === byTitle) || fuzzyLibItem(lib, a.voiceLibTitle) : null)
            || null;
    }
    // 「语音」下拉框：常规选项 + 服务器音频库每一条 + 自定义语音每一条，直接就能选
    function voiceSelectOptions(a) {
        const R = S();
        let cur = a.voice;
        const libItem = a.voice === 'lib' ? libItemFor(a) : null;
        if (a.voice === 'lib' && (libItem || a.voiceLib || a.voiceLibTitle)) cur = 'lib:' + (libItem ? libItem.sha256 : (a.voiceLib || '?' + a.voiceLibTitle));
        if (a.voice === 'file' && a.voiceFile) cur = 'file:' + a.voiceFile;
        let html = optionList(R.VOICES, cur);
        const libOpts = (tts.library || []).map(x => ['lib:' + x.sha256, '☁ ' + (x.title || x.text || '音频') + (x.voiceLabel ? '（' + x.voiceLabel + '）' : '')]);
        if (cur.startsWith('lib:') && !libOpts.some(o => o[0] === cur)) {
            libOpts.unshift([cur, '☁ ' + (a.voiceLibTitle || '服务器音频') + (tts.library.length ? '（服务器上没找到）' : '（正在读取服务器音频库）')]);
        }
        if (libOpts.length) html += `<optgroup label="服务器音频库（管理员发布，不扣额度）">${optionList(libOpts, cur)}</optgroup>`;
        const fileOpts = customVoices.map(v => ['file:' + v.id, '♪ ' + v.name]);
        if (cur.startsWith('file:') && !fileOpts.some(o => o[0] === cur)) fileOpts.unshift([cur, '♪ ' + voiceName(a.voiceFile)]);
        if (fileOpts.length) html += `<optgroup label="自定义语音（本地导入 / 豆包生成）">${optionList(fileOpts, cur)}</optgroup>`;
        return html;
    }
    function libRowHtml(a) {
        const item = libItemFor(a);
        const desc = item ? [item.text && item.text !== item.title ? item.text : '', item.voiceLabel, formatSize(item.bytes)].filter(Boolean).join(' · ') || item.title
            : (a.voiceLibTitle ? `服务器音频库里暂时没找到「${a.voiceLibTitle}」${tts.library.length ? '，请在上面重新选择' : '（连接服务器后自动匹配）'}` : '请在上面的下拉框里选一条服务器音频');
        return `<div class="scr-row"><label class="scr-label">服务器音频</label><span class="scr-grow scr-lib-desc">${esc(desc)}</span>`
            + `<button type="button" class="fxm-btn scr-small" data-act="lib-test"${item || a.voiceLib ? '' : ' disabled'}>试听</button></div>`
            + `<div class="scr-row"><label class="scr-label">前面先念</label><input type="text" class="scr-grow" data-field="voicePrefix" maxlength="40" value="${esc(a.voicePrefix || '')}" placeholder="可留空；例如 {死者搞名} → 先念「马搞」再播这段"></div>`
            + '<div class="scr-muted">服务器音频试听和播放都不扣生成额度；第一次用到时自动下载到本地，之后离线也能播。「前面先念」用这段音频同样的音色合成，每个名字只生成一次（很短，几乎不占额度），之后走本地缓存。</div>';
    }
    const post = msg => window.chrome?.webview?.postMessage(msg);
    function ttsVoiceName(id) {
        if (!id) return '播报音色';
        const v = tts.voices.find(x => x.id === id);
        return v ? v.label : id;
    }
    function ttsVoiceOptions(value) {
        const list = [['', '跟随「特效管理」里选的播报音色'], ...tts.voices.map(v => [v.id, v.label])];
        if (value && !tts.voices.some(v => v.id === value)) list.push([value, value + (tts.loaded ? '（服务器已停用）' : '')]);
        return optionList(list, value || '');
    }
    function ttsRuleHint() {
        const max = Number(tts.maxTextChars) || 30;
        let s = `替换变量后的每一句第一次播放时向服务器生成（按字数扣本卡密的生成额度，最多 ${max} 字），之后本地缓存不再扣；`
            + '选手名不同就是不同的句子。未授权、额度用完、超过字数或网络失败时，自动改用 Windows 系统语音念。';
        if (!tts.ready) s = '当前未在线授权：会先用 Windows 系统语音念。' + s;
        return s;
    }
    function quotaText() {
        if (!tts.ready) return { text: '需要在线验证授权后才能生成', bad: true };
        if (!tts.loaded) return { text: '正在读取生成额度…', bad: false };
        if (!tts.enabled) return { text: '服务器暂未开放语音生成，或本卡密已被关闭', bad: true };
        const u = tts.usage || {};
        let text = `今日已用 ${u.dayUsed ?? 0} / ${u.dayLimit ?? '-'} 字 · 本月 ${u.monthUsed ?? 0} / ${u.monthLimit ?? '-'} 字`;
        if (u.bonusLeft) text += ` · 额外额度剩 ${u.bonusLeft} 字`;
        let bad = false;
        if (tts.budgetExhausted) { text += ' · 今天服务器预算已用完，明天恢复'; bad = true; }
        else if (u.dayLimit != null && u.dayUsed >= u.dayLimit && !u.bonusLeft) { text += ' · 今日额度已用完'; bad = true; }
        if (tts.message) { text += ' · ' + tts.message; bad = true; }
        return { text, bad };
    }
    function renderTtsGen() {
        const box = $('cvl-gen');
        const sel = $('cvl-tts-voice');
        if (!box || !sel) return;
        const sig = tts.voices.map(v => v.id + ':' + v.label).join('|');
        if (sig !== ttsVoicesSig || !sel.options.length) {
            const prev = sel.value;
            const fx = Number(currentLayout().fxVoice);
            sel.innerHTML = tts.voices.length ? optionList(tts.voices.map(v => [v.id, v.label]), prev)
                : '<option value="">（授权后显示可用音色）</option>';
            if (!tts.voices.some(v => v.id === prev)) {
                const pick = tts.voices.find(v => Number(v.index) === fx) || tts.voices[0];
                if (pick) sel.value = pick.id;
            }
        }
        const input = $('cvl-tts-text');
        if (input) input.maxLength = Number(tts.maxTextChars) || 30;
        const usable = tts.ready && tts.enabled && tts.voices.length > 0;
        box.classList.toggle('is-off', !usable);
        const btn = $('cvl-tts-generate');
        if (btn) { btn.disabled = !usable || !!tts.generating; btn.textContent = tts.generating ? '生成中…' : '生成'; }
        const q = quotaText();
        const quota = $('cvl-tts-quota');
        if (quota) { quota.textContent = q.text; quota.classList.toggle('cvl-quota-bad', q.bad); }
    }
    function downloadedAs(item) {
        const title = String(item.title || '').trim();
        return title ? customVoices.find(v => v.name === title || v.name.startsWith(title + ' (')) : null;
    }
    function renderServerLibrary() {
        const box = $('cvl-lib-list');
        if (!box) return;
        const status = $('cvl-lib-status');
        if (status) status.textContent = tts.libraryMessage || (tts.libraryLoading && !tts.library.length ? '正在读取…'
            : '管理员发布的音频，试听和下载都不扣额度；下载后在场景规则里使用');
        const busy = !!tts.libraryDownloading;
        box.innerHTML = tts.library.length ? tts.library.map(item => {
            const have = downloadedAs(item);
            return `<div class="cvl-item" data-sha="${esc(item.sha256)}" data-title="${esc(item.title)}">
                <span class="cvl-icon" aria-hidden="true">☁</span>
                <div class="cvl-main"><strong>${esc(item.title || item.text || '音频')}</strong><small>${esc([item.text && item.text !== item.title ? item.text : '', item.voiceLabel, formatSize(item.bytes)].filter(Boolean).join(' · '))}</small></div>
                ${have ? '<span class="cvl-tag">已下载</span>' : ''}
                <button type="button" class="fxm-btn scr-small" data-lib="preview">试听</button>
                <button type="button" class="fxm-btn scr-small" data-lib="download"${busy ? ' disabled' : ''}>${have ? '再下载' : '下载'}</button>
            </div>`;
        }).join('') : `<div class="scr-empty">${tts.ready || tts.library.length ? '服务器音频库里还没有发布的音频。' : '连接服务器后显示管理员发布的音频。'}</div>`;
    }
    function receiveTts(state) {
        if (!state || typeof state !== 'object') return;
        tts = {
            ...state,
            voices: Array.isArray(state.voices) ? state.voices.filter(v => v && v.id) : [],
            library: Array.isArray(state.library) ? state.library.filter(v => v && v.sha256) : []
        };
        const voicesSig = tts.voices.map(v => v.id + ':' + v.label).join('|') + '#' + tts.maxTextChars + '#' + tts.ready;
        const voicesChanged = voicesSig !== ttsVoicesSig;
        renderTtsGen();
        ttsVoicesSig = voicesSig;
        const libSig = JSON.stringify([tts.library, tts.libraryMessage, tts.libraryLoading, tts.libraryDownloading, tts.ready]);
        if (libSig !== ttsLibrarySig) {
            ttsLibrarySig = libSig;
            renderServerLibrary();
            // 只写了标题的规则（例如预置场景），读到服务器列表后补上 sha
            let filled = false;
            (rules || []).forEach(r => {
                if (r.action.voice !== 'lib' || r.action.voiceLib) return;
                const item = libItemFor(r.action);
                if (item) { r.action.voiceLib = item.sha256; filled = true; }
            });
            if (filled) save();
            renderList();
            if (selected()?.action.voice === 'lib' && !$('scr-editor')?.contains(document.activeElement)) renderEditor();
        }
        if (voicesChanged) {
            renderList();
            if (selected()?.action.voice === 'tts' && !$('scr-editor')?.contains(document.activeElement)) renderEditor();
        }
    }
    function generateTts() {
        const voice = $('cvl-tts-voice')?.value || '';
        const input = $('cvl-tts-text');
        const text = String(input?.value || '').trim();
        const max = Number(tts.maxTextChars) || 30;
        if (!voice) { setVoiceStatus('请先选择音色（需要在线授权）'); return; }
        if (!text) { setVoiceStatus('请输入要生成的文字'); input?.focus(); return; }
        if ([...text].length > max) { setVoiceStatus(`文字太长：最多 ${max} 字`); return; }
        post({ action: 'cmd_tts_generate', voice, text });
        tts.generating = true;
        renderTtsGen();
        setVoiceStatus(`正在用「${ttsVoiceName(voice)}」生成：${text}`);
    }
    function onServerLibraryClick(event) {
        const btn = event.target.closest('[data-lib]');
        const row = btn?.closest('.cvl-item');
        if (!btn || !row) return;
        const sha256 = row.dataset.sha, title = row.dataset.title || '';
        if (btn.dataset.lib === 'preview') {
            post({ action: 'cmd_voice_library_preview', sha256 });
            setVoiceStatus('试听：' + (title || '服务器音频'));
        }
        else {
            post({ action: 'cmd_voice_library_download', sha256, title });
            setVoiceStatus('正在下载：' + (title || '服务器音频'));
        }
    }

    function refreshPlayerList() {
        const list = $('scr-player-names');
        if (!list) return;
        const names = knownPlayers();
        const sig = names.join('|');
        if (list.dataset.sig === sig) return;
        list.dataset.sig = sig;
        list.innerHTML = names.map(n => `<option value="${esc(n)}"></option>`).join('');
    }

    function init() {
        const pane = $('streamer-pane-rules');
        if (!pane || !S()) return;
        $('scr-list')?.addEventListener('click', onListClick);
        $('scr-list')?.addEventListener('change', onListChange);
        const editor = $('scr-editor');
        editor?.addEventListener('input', onEditorInput);
        editor?.addEventListener('change', onEditorInput);
        editor?.addEventListener('click', onEditorClick);
        editor?.addEventListener('keydown', event => {
            const el = event.target;
            if (event.key !== 'Enter' || el.dataset.part !== 'name') return;
            event.preventDefault();
            const r = selected();
            const c = r && r.conditions[Number(el.dataset.cond)];
            if (c) addPlayerName(c, el.value, el.dataset.side);
        });
        editor?.addEventListener('focusin', event => { if (event.target.dataset.part === 'name') refreshPlayerList(); });
        $('scr-add')?.addEventListener('click', addRule);
        $('scr-reset-all')?.addEventListener('click', resetAll);
        $('cvl-import')?.addEventListener('click', importVoices);
        $('cvl-list')?.addEventListener('click', onLibraryClick);
        $('cvl-tts-generate')?.addEventListener('click', generateTts);
        $('cvl-tts-text')?.addEventListener('keydown', event => { if (event.key === 'Enter') { event.preventDefault(); generateTts(); } });
        $('cvl-lib-list')?.addEventListener('click', onServerLibraryClick);
        $('cvl-lib-refresh')?.addEventListener('click', () => { post({ action: 'cmd_tts_refresh' }); setVoiceStatus('正在刷新服务器音频库和生成额度…'); });
        render();
        renderVoiceLibrary();
        renderTtsGen();
        renderServerLibrary();
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
    else init();

    return { receive, receiveVoices, receiveTts, voiceResult, onEventToggle, syncFromLayout, render };
})();
window.SceneRulesUI = SceneRulesUI;
