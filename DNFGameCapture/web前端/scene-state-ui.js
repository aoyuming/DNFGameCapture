/* ================= 主播工具 → 情景状态 =================
 * 展示窗口（kill.js：击杀小窗 / 全屏特效 / OBS 浏览器源）每秒把场景规则用的比赛记录回报给 C++（/api/scene-state），
 * 本页打开时每秒通过 cmd_scene_state_get 读取并显示；修改通过 cmd_scene_edit 下发，所有展示窗口各执行一次。
 * ======================================================= */
const SceneStateUI = (() => {
    'use strict';
    const $ = id => document.getElementById(id);
    const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
    const post = msg => { try { window.chrome?.webview?.postMessage(msg); } catch (err) { /* ignore */ } };
    const TEAM = ['红队', '蓝队'];
    let reports = [];
    let chosenSrc = '';          // 空 = 自动选择
    let lastReceiveAt = 0;
    let statusTimer = null;

    const paneVisible = () => {
        const pane = $('streamer-pane-scene');
        // 不用 offsetParent：主播工具面板是 position:fixed 的浮层，offsetParent 可能恒为 null 导致从不请求数据
        const overlay = document.getElementById('fx-manager-overlay');
        return !!pane && !pane.hidden && (!overlay || overlay.classList.contains('active'));
    };
    const srcLabel = r => (r.data?.mode === 'fx' ? '全屏特效窗口' : '击杀小窗 / OBS') + ' · ' + String(r.src).split('-').pop()
        + (r.data?.visible ? '' : '（已隐藏）');

    // 自动选择：可见的全屏特效 > 可见的小窗 > 最新回报
    function current() {
        if (!reports.length) return null;
        if (chosenSrc) {
            const hit = reports.find(r => r.src === chosenSrc);
            if (hit) return hit;
        }
        const score = r => (r.data?.visible ? 4 : 0) + (r.data?.mode === 'fx' ? 2 : 0) - r.ageMs / 100000;
        return reports.slice().sort((a, b) => score(b) - score(a))[0];
    }

    function receive(data) {
        lastReceiveAt = Date.now();
        reports = Array.isArray(data?.reports) ? data.reports.filter(r => r && r.data) : [];
        // 正在输入时不重绘，避免打断编辑
        const active = document.activeElement;
        if (active && $('scs-body')?.contains(active) && (active.tagName === 'INPUT' || active.tagName === 'SELECT')) {
            renderSources();
            return;
        }
        render();
    }

    function edit(ops, hint) {
        post({ action: 'cmd_scene_edit', ops: Array.isArray(ops) ? ops : [ops] });
        setStatus(hint || '已发送修改，展示窗口约 1 秒内生效。');
        setTimeout(() => post({ action: 'cmd_scene_state_get' }), 400);
    }

    function setStatus(text) {
        const el = $('scs-status');
        if (!el) return;
        el.textContent = text;
        clearTimeout(statusTimer);
        statusTimer = setTimeout(() => { el.textContent = ''; }, 4000);
    }

    function renderSources() {
        const sel = $('scs-source');
        if (!sel) return;
        const sig = reports.map(r => r.src + (r.data?.visible ? 1 : 0)).join('|') + '#' + chosenSrc;
        if (sel.dataset.sig === sig) return;
        sel.dataset.sig = sig;
        sel.innerHTML = '<option value="">自动选择</option>'
            + reports.map(r => `<option value="${esc(r.src)}"${r.src === chosenSrc ? ' selected' : ''}>${esc(srcLabel(r))}</option>`).join('');
        sel.hidden = reports.length < 2 && !chosenSrc;
    }

    function render() {
        renderSources();
        const box = $('scs-body');
        if (!box) return;
        const r = current();
        if (!r) {
            box.innerHTML = `<div class="scs-empty">没有收到展示窗口的数据。<br>情景数据保存在击杀小窗 / 全屏特效窗口里，请先打开其中一个（OBS 浏览器源也算）。</div>`;
            return;
        }
        const d = r.data;
        const players = Array.isArray(d.players) ? d.players : [];
        const physText = d.physLeftTeam === 0 || d.physLeftTeam === 1 ? TEAM[d.physLeftTeam] + '（已推断）' : '未推断（按翻转：' + TEAM[d.flipped ? 1 : 0] + '）';
        const teamRows = [0, 1].map(t => {
            const list = players.filter(p => p.team === t);
            if (!list.length) return '';
            return `<div class="scs-team scs-team-${t}"><div class="scs-team-title">${TEAM[t]}</div>
                ${list.map(p => `<div class="scs-player${p.alive ? '' : ' is-dead'}">
                    <span class="scs-pname" title="${esc(p.gao && p.gao !== p.name ? '搞名：' + p.gao : '')}">${esc(p.name)}</span>
                    <label class="scs-alive"><input type="checkbox" data-op="alive" data-key="${esc(p.key)}"${p.alive ? ' checked' : ''}><span>${p.alive ? '存活' : '已阵亡'}</span></label>
                    <label class="scs-num" title="连续阵亡且没拿到人头的次数">没人头 <input type="number" min="0" max="99" step="1" data-op="drought" data-key="${esc(p.key)}" value="${Number(p.drought) || 0}"></label>
                    <span class="scs-streak" title="主程序记录的当前连杀">连杀 ${Number(p.streak) || 0}</span>
                </div>`).join('')}</div>`;
        }).join('');
        const log = (Array.isArray(d.killLog) ? d.killLog : []).slice().reverse();
        const logHtml = log.length ? log.map(k => `<div class="scs-kill">
                <span class="scs-kround">第${Number(k.round) + 1}局</span>
                <span class="scs-kteam-${k.killerTeam === 'blue' ? 1 : 0}">${esc(k.killer)}</span> → ${esc(k.victim || '（未对应）')}
                ${Number(k.streak) > 1 ? `<span class="scs-kstreak">${Number(k.streak)}连杀</span>` : ''}
                <button type="button" class="scs-x" data-op="removeKill" data-index="${Number(k.index)}" title="删除这条击杀记录">×</button>
            </div>`).join('') : '<div class="scs-muted">本场还没有击杀记录</div>';
        const cds = Array.isArray(d.cooldowns) ? d.cooldowns : [];
        const cdHtml = cds.length ? cds.map(c => `<div class="scs-cd"><span>${esc(c.name)}</span><span class="scs-muted">剩 ${Number(c.left)} 秒 / ${Number(c.total)} 秒</span>
                <button type="button" class="fxm-btn" data-op="clearCooldown" data-id="${esc(c.id)}">清除</button></div>`).join('')
            : '<div class="scs-muted">没有正在冷却的规则</div>';
        const rv = Array.isArray(d.revenge) ? d.revenge : [];
        const nameOfKey = key => (players.find(p => p.key === key) || {}).name || String(key).split('|').slice(1).join('|');
        const rvHtml = rv.length ? rv.map(x => {
            const [a, b] = String(x.pair).split('>');
            return `<div class="scs-kill">${esc(nameOfKey(a))} 击杀过 ${esc(nameOfKey(b))}<span class="scs-muted">（${esc(nameOfKey(b))} 本局反杀 = 复仇）</span>
                <button type="button" class="scs-x" data-op="revenge" data-round="${Number(x.round)}" data-pair="${esc(x.pair)}" title="删除">×</button></div>`;
        }).join('') : '<div class="scs-muted">上一局没有可复仇的记录</div>';

        box.innerHTML = `
            <div class="scs-summary">
                <div class="scs-card"><small>当前局</small><b>第 ${Number(d.round) + 1} 局</b></div>
                <div class="scs-card"><small>一血</small><b>${d.firstBlood ? '已出现' : '未出现'}</b>
                    <button type="button" class="fxm-btn" data-op="firstBlood" data-value="${d.firstBlood ? 0 : 1}">${d.firstBlood ? '改为未出现' : '改为已出现'}</button></div>
                <div class="scs-card"><small>游戏画面左侧</small><b>${esc(physText)}</b>
                    <select data-op="physLeft" aria-label="游戏画面左侧队伍">
                        <option value=""${d.physLeftTeam == null ? ' selected' : ''}>自动推断</option>
                        <option value="0"${d.physLeftTeam === 0 ? ' selected' : ''}>红队在左</option>
                        <option value="1"${d.physLeftTeam === 1 ? ' selected' : ''}>蓝队在左</option>
                    </select></div>
                <div class="scs-card"><small>数据来源</small><b>${esc(srcLabel(r))}</b>
                    <span class="scs-muted">${d.enabled ? '特效开启' : '特效关闭'} · ${d.rulesOn ? '场景规则已加载' : '内置逻辑'} · ${Math.round(r.ageMs / 1000)} 秒前</span></div>
            </div>
            <div class="scs-section-title">选手状态<span class="scs-muted">取消勾选 = 本局已阵亡（比分变化后自动全部复活）</span></div>
            <div class="scs-teams">${teamRows || '<div class="scs-muted">场上没有选手</div>'}</div>
            <div class="scs-grid">
                <section><div class="scs-section-title">本场击杀记录<span class="scs-muted">共 ${Number(d.killTotal) || 0} 条，显示最近 30 条</span></div><div class="scs-list">${logHtml}</div></section>
                <section><div class="scs-section-title">复仇候选</div><div class="scs-list scs-list-sm">${rvHtml}</div>
                    <div class="scs-section-title">规则冷却</div><div class="scs-list scs-list-sm">${cdHtml}</div></section>
            </div>
            <div class="scs-actions">
                <button type="button" class="fxm-btn" data-op="reviveAll">全部复活</button>
                <button type="button" class="fxm-btn" data-op="clearDrought">连续没人头清零</button>
                <button type="button" class="fxm-btn" data-op="clearHistory">清空击杀记录（含一血 / 复仇）</button>
                <button type="button" class="fxm-btn" data-op="clearCooldown">清除全部冷却</button>
                <button type="button" class="fxm-btn danger" data-op="full">完整重置情景数据</button>
            </div>`;
    }

    function onClick(event) {
        const b = event.target.closest('button[data-op]');
        if (!b) return;
        const op = b.dataset.op;
        if (op === 'removeKill') edit({ op, index: Number(b.dataset.index) }, '已删除一条击杀记录。');
        else if (op === 'revenge') edit({ op, round: Number(b.dataset.round), pair: b.dataset.pair }, '已删除一条复仇记录。');
        else if (op === 'firstBlood') edit({ op, value: b.dataset.value === '1' });
        else if (op === 'clearCooldown') edit(b.dataset.id ? { op, id: b.dataset.id } : { op }, b.dataset.id ? '已清除该规则冷却。' : '已清除全部冷却。');
        else if (op === 'full') {
            if (!window.confirm('完整重置情景数据？\n击杀记录、一血、复仇、存活、连续没人头、冷却都会清空（不影响比分和战绩）。')) return;
            edit({ op }, '情景数据已完整重置。');
        }
        else edit({ op });
    }

    function onChange(event) {
        const el = event.target;
        const op = el.dataset.op;
        if (!op) return;
        if (op === 'alive') edit({ op, key: el.dataset.key, value: el.checked });
        else if (op === 'drought') edit({ op, key: el.dataset.key, value: Math.max(0, Math.min(99, Math.round(Number(el.value) || 0))) });
        else if (op === 'physLeft') edit({ op, value: el.value === '' ? null : Number(el.value) });
        el.blur();
    }

    function tick() {
        if (!paneVisible()) return;
        post({ action: 'cmd_scene_state_get' });
        // 老版本主程序不回复时给出提示
        if (lastReceiveAt && Date.now() - lastReceiveAt > 5000) setStatus('主程序没有返回情景数据，请确认主程序已更新。');
    }

    function init() {
        const body = $('scs-body');
        if (!body) return;
        body.addEventListener('click', onClick);
        body.addEventListener('change', onChange);
        $('scs-source')?.addEventListener('change', e => { chosenSrc = e.target.value; render(); });
        render();
        setInterval(tick, 1000);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
    else init();

    return { receive, refresh: tick };
})();
// main.js 通过 window.SceneStateUI 调用；const 声明不会挂到 window 上
window.SceneStateUI = SceneStateUI;
