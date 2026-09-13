import { escapeAdminAttribute } from './admin-page.js';

export function buildBroadcasterAdminPage(csrfToken: string): string {
  return `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="dnf-admin-csrf" content="${escapeAdminAttribute(csrfToken)}">
  <title>DNF 主播管理</title>
  <link rel="stylesheet" href="/admin/broadcasters/style.css">
</head>
<body>
  <header class="topbar">
    <div><strong>DNF 主播管理</strong><a class="library-link" href="/admin">管理首页</a></div>
    <div class="health"><i></i><span id="refresh-status" role="status">正在读取</span><button id="refresh-broadcasters">刷新</button></div>
  </header>
  <main class="workspace">
    <aside class="directory">
      <div class="section-heading"><span>主播大厅</span><b id="broadcaster-count">0</b></div>
      <label class="search"><span>搜索</span><input id="search-input" type="search" placeholder="主播 / IP / 地区 / 密钥备注" autocomplete="off"></label>
      <div id="broadcaster-list" class="broadcaster-list"></div>
    </aside>
    <section class="detail">
      <div id="empty-state" class="empty-state">暂无选中主播</div>
      <div id="detail-content" hidden>
        <header class="detail-header">
          <div><h1 id="detail-name"></h1><p id="detail-meta"></p></div>
          <div class="actions"><button id="disconnect-button">强制断开</button><button class="danger" id="delete-button">删除离线数据</button></div>
        </header>
        <section class="attribution-panel" aria-label="主播网络与授权">
          <div class="attribution-grid">
            <div><span>当前 IP</span><strong id="current-ip">暂无</strong></div>
            <div><span>最后 IP</span><strong id="last-ip">暂无</strong></div>
            <div><span>IP 地区</span><strong id="ip-region">未知地区</strong></div>
          </div>
          <div class="license-binding">
            <label><span>对应密钥</span><select id="license-select" aria-label="选择主播对应密钥"></select></label>
            <button id="bind-license" type="button">绑定 / 换绑</button>
          </div>
          <p id="license-meta" class="license-meta">未关联密钥</p>
          <label class="license-key"><span>完整卡密</span><input id="license-key-value" readonly autocomplete="off" spellcheck="false" value="未关联密钥"></label>
        </section>
        <div id="score-line" class="score-line"></div>
        <div id="teams" class="teams"></div>
      </div>
    </section>
    <aside class="activity">
      <section><div class="section-heading"><span>实时关系</span><b id="relation-count">0</b></div><div id="relation-list" class="activity-list"></div></section>
      <section><div class="section-heading"><span>24 小时同步记录</span><b id="history-count">0</b></div><div id="history-list" class="activity-list"></div></section>
      <section class="maintenance">
        <div class="section-heading"><span>维护</span></div>
        <button id="cleanup-expired">清理过期记录</button>
        <button class="danger" id="cleanup-offline">清空离线/测试数据</button>
      </section>
      <section class="management">
        <div class="section-heading"><span>主播 OCR 管控</span></div>
        <div class="management-form"><label>设备 ID<input id="ocr-device-id" maxlength="128" placeholder="设备 ID"></label><label>禁用截止 Unix 秒<input id="ocr-until" type="number" min="0" placeholder="禁用截止 Unix 秒"></label><button id="set-ocr-policy">应用策略</button><button id="clear-ocr-policy">解除禁用</button></div>
      </section>
    </aside>
  </main>
  <div id="toast" class="toast" role="status"></div>
  <script src="/admin/broadcasters/app.js" defer></script>
</body>
</html>`;
}

export const BROADCASTER_ADMIN_CSS = `
:root{color-scheme:dark;--bg:#0b1015;--surface:#111820;--raised:#17212a;--line:#2b3945;--text:#e8f0f4;--muted:#91a2ae;--accent:#60c3dd;--red:#ff6e7f;--blue:#62a8ff;--warn:#f3c96c}*{box-sizing:border-box}html,body{height:100%;margin:0;background:var(--bg);color:var(--text);font-family:"Microsoft YaHei UI","Microsoft YaHei",sans-serif;font-size:14px}button,input,select{font:inherit}.topbar{height:58px;display:flex;align-items:center;justify-content:space-between;padding:0 22px;border-bottom:1px solid var(--line);background:#0e151c}.topbar strong{font-size:18px}.topbar span{margin-left:10px;color:var(--muted);font-size:12px}.health{display:flex;align-items:center}.health i{width:8px;height:8px;border-radius:50%;background:#48c78e;box-shadow:0 0 12px #48c78e}.workspace{height:calc(100% - 58px);display:grid;grid-template-columns:280px minmax(520px,1fr) 350px;min-width:1040px}.directory,.activity{min-height:0;overflow:auto;background:var(--surface)}.directory{border-right:1px solid var(--line)}.activity{border-left:1px solid var(--line)}.section-heading{height:45px;display:flex;align-items:center;justify-content:space-between;padding:0 14px;border-bottom:1px solid var(--line);font-weight:800}.section-heading b{min-width:24px;padding:2px 7px;border-radius:10px;text-align:center;color:var(--accent);background:#20313b;font-size:11px}.search{display:grid;gap:5px;padding:11px 14px;border-bottom:1px solid var(--line);color:var(--muted);font-size:11px}.search input{width:100%;height:34px;padding:0 10px;border:1px solid var(--line);border-radius:4px;outline:0;color:var(--text);background:var(--bg)}.search input:focus{border-color:var(--accent)}.broadcaster-list{display:grid}.broadcaster-row{width:100%;display:grid;grid-template-columns:9px minmax(0,1fr) auto;gap:10px;align-items:center;padding:12px 14px;border:0;border-bottom:1px solid #1e2932;text-align:left;color:var(--text);background:transparent;cursor:pointer}.broadcaster-row:hover,.broadcaster-row.active{background:var(--raised)}.broadcaster-row.offline{color:#778690}.presence{width:8px;height:8px;border-radius:50%;background:#58646d}.online .presence{background:#48c78e}.broadcaster-copy{min-width:0}.broadcaster-copy strong,.broadcaster-copy small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.broadcaster-copy strong{font-size:14px}.broadcaster-copy small{margin-top:4px;color:var(--muted);font-size:11px}.revision{color:var(--muted);font:11px Consolas,monospace}.detail{min-width:0;overflow:auto;padding:20px 22px}.empty-state{display:grid;place-items:center;height:100%;color:var(--muted)}.empty-state[hidden],#detail-content[hidden]{display:none!important}.detail-header{display:flex;align-items:flex-start;justify-content:space-between;gap:20px;padding-bottom:15px;border-bottom:1px solid var(--line)}h1{margin:0;font-size:24px;letter-spacing:0}.detail-header p{margin:6px 0 0;color:var(--muted);font-size:12px}.actions{display:flex;gap:8px}button{min-height:34px;padding:6px 11px;border:1px solid var(--line);border-radius:4px;color:var(--text);background:var(--raised);cursor:pointer}button:hover{border-color:var(--accent)}button:disabled{opacity:.45;cursor:not-allowed}.danger{border-color:#713441;color:#ff9aa6}.attribution-panel{margin:16px 0;border-block:1px solid var(--line);padding:14px 0}.attribution-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}.attribution-grid>div{min-width:0;padding:10px 12px;background:var(--surface)}.attribution-grid span,.license-binding span,.license-key span{display:block;margin-bottom:6px;color:var(--muted);font-size:11px}.attribution-grid strong{display:block;overflow-wrap:anywhere;font-size:13px}.license-binding{display:grid;grid-template-columns:minmax(0,1fr) auto;align-items:end;gap:10px;margin-top:10px}.license-binding label{min-width:0}.license-binding select,.license-key input{width:100%;min-height:38px;padding:7px 9px;border:1px solid var(--line);border-radius:4px;color:var(--text);background:var(--bg);outline:0}.license-binding select:focus,.license-key input:focus{border-color:var(--accent)}.license-binding button{min-height:38px}.license-meta{margin:9px 0;color:var(--muted);font-size:12px;overflow-wrap:anywhere}.license-key input{font:12px Consolas,monospace;color:#8be0ba}.score-line{margin:16px 0;padding:14px 16px;border-left:4px solid var(--accent);background:var(--surface);font-size:18px;font-weight:900}.teams{display:grid;grid-template-columns:1fr 1fr;gap:14px}.team{border:1px solid var(--line);background:var(--surface)}.team.red{border-top:3px solid var(--red)}.team.blue{border-top:3px solid var(--blue)}.team h2{margin:0;padding:10px 12px;border-bottom:1px solid var(--line);font-size:15px}.player{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:10px;align-items:center;min-height:58px;padding:9px 12px;border-bottom:1px solid #202b34}.player:last-child{border-bottom:0}.player strong,.player small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.player strong{font-size:16px}.player small{margin-top:4px;color:var(--muted);font-size:11px}.stats{color:var(--accent);font:11px Consolas,monospace;white-space:nowrap}.activity section{border-bottom:1px solid var(--line)}.activity-list{max-height:250px;overflow:auto}.activity-item{padding:10px 14px;border-bottom:1px solid #202b34;color:var(--muted);font-size:12px;line-height:1.5}.activity-item.bidirectional{border-left:3px solid var(--warn);background:#30291a}.activity-item strong{display:block;color:var(--text)}.activity-item button{width:100%;margin-top:7px}.maintenance{padding-bottom:12px}.maintenance>button{width:calc(100% - 28px);margin:10px 14px 0}.toast{position:fixed;right:18px;bottom:18px;max-width:420px;padding:10px 14px;border:1px solid var(--line);border-radius:4px;color:var(--text);background:#1a2730;box-shadow:0 12px 30px #0008;opacity:0;pointer-events:none;transform:translateY(8px);transition:.18s}.toast.show{opacity:0;transform:none}@media(max-width:1200px){.workspace{grid-template-columns:250px minmax(480px,1fr) 310px}.teams{grid-template-columns:1fr}.player{grid-template-columns:1fr}.stats{white-space:normal}}.management{border-top:1px solid var(--line)}.management-form{display:grid;gap:7px;padding:10px 14px}.management-form input{width:100%;height:32px;padding:0 9px;border:1px solid var(--line);border-radius:4px;color:var(--text);background:var(--bg);outline:0}.management-form input:focus{border-color:var(--accent)}.management-form button{width:100%;font-size:12px}.compact-list{max-height:190px;overflow:auto}.compact-item{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:8px 14px;border-bottom:1px solid #202b34;color:var(--muted);font-size:11px}.compact-item strong{display:block;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:var(--text)}.compact-item button{min-height:27px;padding:3px 7px;font-size:11px;white-space:nowrap}.library-summary{padding:9px 14px;color:var(--muted);font-size:11px;line-height:1.45}
.toast.show{opacity:1}
.library-link{margin-left:18px;color:var(--accent);font-size:13px;text-underline-offset:4px}

*{letter-spacing:0}body{min-width:0}button,input{min-width:0}a,button,input{touch-action:manipulation}a:focus-visible,button:focus-visible,input:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.topbar{min-height:58px;height:auto;gap:12px;padding:14px 22px;flex-wrap:wrap}.topbar>div{min-width:0}.workspace{min-width:0;grid-template-columns:minmax(220px,.8fr) minmax(0,1.8fr) minmax(260px,1fr)}.detail-header{flex-wrap:wrap}.detail-header>div{min-width:0}.detail-header h1{font-size:22px}.detail-header p,.score-line,.activity-item{overflow-wrap:anywhere}.actions{flex-wrap:wrap}.management-form label{color:var(--muted);font-size:12px;display:grid;gap:6px}.toast{max-width:min(420px,calc(100vw - 36px));overflow-wrap:anywhere}.player>div{min-width:0}
@media(max-width:1100px){.workspace{height:auto;grid-template-columns:240px minmax(0,1fr)}.directory{max-height:680px}.detail{min-height:420px}.activity{grid-column:1/-1;display:grid;grid-template-columns:1fr 1fr;border-top:1px solid var(--line)}.teams{grid-template-columns:1fr}}
@media(max-width:680px){.topbar{padding:14px 12px}.topbar strong{display:block;margin-bottom:8px}.library-link{margin-left:0}.workspace{grid-template-columns:minmax(0,1fr)}.directory{max-height:320px;border-bottom:1px solid var(--line)}.detail{padding:18px 12px}.activity{grid-template-columns:minmax(0,1fr)}.detail-header{gap:12px}.actions button{flex:1}.attribution-grid{grid-template-columns:minmax(0,1fr)}.license-binding{grid-template-columns:minmax(0,1fr)}.license-binding button{width:100%;min-height:44px}.score-line{font-size:16px}.health i{box-shadow:none}}
`;

export const BROADCASTER_ADMIN_JS = `
(() => {
  'use strict';
  const csrf = document.querySelector('meta[name="dnf-admin-csrf"]')?.content || '';
  const byId = id => document.getElementById(id);
  let state = { broadcasters: [], relations: [], history: [] };
  let licenses = [];
  const revealedKeys = new Map();
  const keyErrors = new Map();
  const revealingKeys = new Set();
  let selectedId = '';
  let query = '';
  let refreshPending = false;
  const text = (node, value) => { if (node) node.textContent = String(value ?? ''); };
  const formatTime = seconds => seconds ? new Date(seconds * 1000).toLocaleString('zh-CN', { hour12: false }) : '暂无';
  const toast = message => { const node = byId('toast'); text(node, message); node?.classList.add('show'); setTimeout(() => node?.classList.remove('show'), 2200); };
  const request = async (url, options = {}) => {
    const response = await fetch(url, { ...options, headers: { 'content-type': 'application/json', 'x-dnf-admin-csrf': csrf, ...(options.headers || {}) } });
    const payload = await response.json().catch(() => ({ ok: false, code: 'invalid_response' }));
    if (!response.ok || !payload.ok) throw new Error(payload.code || 'request_failed');
    return payload;
  };
  const confirmDanger = (first, second) => window.confirm(first) && window.prompt(second + '\\n请输入“确认”继续：') === '确认';
  function broadcasterById(id) { return state.broadcasters.find(item => item.deviceId === id); }
  function renderDirectory() {
    const list = byId('broadcaster-list'); if (!list) return; list.replaceChildren();
    text(byId('broadcaster-count'), state.broadcasters.length);
    for (const item of state.broadcasters) {
      const button = document.createElement('button'); button.className = 'broadcaster-row ' + (item.online ? 'online' : 'offline') + (item.deviceId === selectedId ? ' active' : '');
      const dot = document.createElement('i'); dot.className = 'presence';
      const copy = document.createElement('span'); copy.className = 'broadcaster-copy';
      const name = document.createElement('strong'); text(name, item.broadcasterName);
      const meta = document.createElement('small'); text(meta, (item.online ? '在线' : '离线') + ' · ' + (item.currentIp || item.lastIp || 'IP 未知') + ' · ' + (item.region || '未知地区')); copy.append(name, meta);
      const revision = document.createElement('span'); revision.className = 'revision'; text(revision, 'r' + (item.snapshotRevision || 0));
      button.append(dot, copy, revision); button.addEventListener('click', () => { selectedId = item.deviceId; byId('ocr-device-id').value = selectedId; render(); }); list.append(button);
    }
  }
  function playerRow(player) {
    const row = document.createElement('div'); row.className = 'player'; const identity = document.createElement('div');
    const main = document.createElement('strong'); text(main, player?.mainName || '未设置');
    const aliases = document.createElement('small'); text(aliases, (player?.aliases || []).join(' / ') || '无游戏ID'); identity.append(main, aliases);
    const stats = document.createElement('span'); stats.className = 'stats'; text(stats, '杀 ' + (player?.kills || 0) + ' · 死 ' + (player?.deaths || 0) + ' · AK ' + (player?.ak || 0) + ' · 连杀 ' + (player?.streak || 0)); row.append(identity, stats); return row;
  }
  function team(title, players, side) { const section = document.createElement('section'); section.className = 'team ' + side; const heading = document.createElement('h2'); text(heading, title); section.append(heading); players.forEach(item => section.append(playerRow(item))); return section; }
  function renderAttribution(item) {
    text(byId('current-ip'), item.currentIp || (item.online ? '未知' : '当前离线'));
    text(byId('last-ip'), item.lastIp || '暂无');
    text(byId('ip-region'), item.region || '未知地区');
    const select = byId('license-select'); select.replaceChildren();
    const available = licenses.filter(license => license.activatedAt !== null && license.boundDeviceId);
    if (!available.length) {
      const option = document.createElement('option'); option.value = ''; text(option, '暂无已激活密钥'); select.append(option);
    } else {
      for (const license of available) {
        const option = document.createElement('option'); option.value = String(license.id);
        text(option, '#' + license.id + ' · ' + (license.label || '未备注') + ' · ' + license.boundDeviceId);
        select.append(option);
      }
    }
    if (item.license && available.some(license => license.id === item.license.id)) select.value = String(item.license.id);
    text(byId('license-meta'), item.license
      ? (item.license.source === 'manual' ? '人工绑定' : '自动关联') + ' · #' + item.license.id + ' · ' + (item.license.label || '未备注') + ' · 授权设备 ' + item.license.deviceId
      : '未关联密钥');
    const key = byId('license-key-value');
    key.value = !item.license ? '未关联密钥'
      : !item.license.hasKey ? '该旧卡暂无可显示密钥'
      : revealedKeys.get(item.license.id) || keyErrors.get(item.license.id) || '正在读取完整卡密...';
    select.disabled = !available.length;
    byId('bind-license').disabled = !available.length;
  }
  async function revealSelectedKey(item) {
    const licenseId = item?.license?.id;
    if (!licenseId || !item.license.hasKey || revealedKeys.has(licenseId) || revealingKeys.has(licenseId)) return;
    revealingKeys.add(licenseId); keyErrors.delete(licenseId);
    try {
      const result = await request('/admin/api/licenses/' + licenseId + '/reveal', { method: 'POST', body: '{}' });
      if (typeof result.key !== 'string' || !result.key) throw new Error('服务器未返回卡密');
      revealedKeys.set(licenseId, result.key);
    } catch (error) {
      keyErrors.set(licenseId, '读取失败：' + error.message);
    } finally {
      revealingKeys.delete(licenseId);
      const current = broadcasterById(selectedId);
      if (current?.license?.id === licenseId) renderAttribution(current);
    }
  }
  function renderDetail() {
    const item = broadcasterById(selectedId); byId('empty-state').hidden = !!item; byId('detail-content').hidden = !item; if (!item) return;
    text(byId('detail-name'), item.broadcasterName); text(byId('detail-meta'), (item.online ? '在线' : '离线') + ' · ' + item.deviceId + ' · 快照 ' + formatTime(item.receivedAt));
    renderAttribution(item); void revealSelectedKey(item);
    const snap = item.snapshot; text(byId('score-line'), snap ? '比分 ' + snap.redScore + ' : ' + snap.blueScore + ' · 红方' + (snap.redPickFirst ? '先手' : '后手') + '' : '暂无有效比赛快照');
    const teams = byId('teams'); teams.replaceChildren(); if (snap) teams.append(team('红队', snap.redPlayers || [], 'red'), team('蓝队', snap.bluePlayers || [], 'blue'));
    byId('disconnect-button').disabled = !item.online; byId('delete-button').disabled = item.online;
  }
  function renderActivity() {
    const relations = byId('relation-list'); relations.replaceChildren(); text(byId('relation-count'), state.relations.length);
    for (const item of state.relations) { const row = document.createElement('div'); row.className = 'activity-item'; const bidirectional = state.relations.some(other => other.viewerDeviceId === item.targetDeviceId && other.targetDeviceId === item.viewerDeviceId); if (bidirectional) row.classList.add('bidirectional'); const label = document.createElement('strong'); text(label, item.viewerName + ' → ' + item.targetName + (bidirectional ? ' · 双向冲突' : '')); const time = document.createElement('span'); text(time, '开始 ' + formatTime(item.startedAt)); const stop = document.createElement('button'); text(stop, '强制停止'); stop.addEventListener('click', () => mutate('/admin/api/realtime/' + encodeURIComponent(item.viewerDeviceId) + '/stop', 'POST')); row.append(label, time, stop); relations.append(row); }
    if (!state.relations.length) { const row = document.createElement('div'); row.className = 'activity-item'; text(row, '暂无实时同步关系'); relations.append(row); }
    const history = byId('history-list'); history.replaceChildren(); text(byId('history-count'), state.history.length);
    for (const item of state.history) { const row = document.createElement('div'); row.className = 'activity-item'; const label = document.createElement('strong'); text(label, item.targetName + ' 同步了 ' + item.sourceName); const meta = document.createElement('span'); text(meta, formatTime(item.createdAt) + ' · ' + (item.syncType === 'realtime' ? '实时' : '一次') + (item.merged ? ' · 合并' : '')); row.append(label, meta); history.append(row); }
    if (!state.history.length) { const row = document.createElement('div'); row.className = 'activity-item'; text(row, '24 小时内暂无成功同步'); history.append(row); }
  }
  function render() { renderDirectory(); renderDetail(); renderActivity(); }
  async function refresh() {
    if (refreshPending) return;
    refreshPending = true; const requestedQuery = query;
    byId('refresh-broadcasters').disabled = true; text(byId('refresh-status'), '正在读取');
    document.body.setAttribute('aria-busy', 'true');
    try {
      const suffix = requestedQuery ? '?q=' + encodeURIComponent(requestedQuery) : '';
      const result = await Promise.all([
        request('/admin/api/broadcasters/state' + suffix),
        request('/admin/api/licenses'),
      ]);
      state = result[0]; licenses = Array.isArray(result[1].licenses) ? result[1].licenses : [];
      if (selectedId && !broadcasterById(selectedId)) selectedId = '';
      text(byId('refresh-status'), '已更新 ' + new Date().toLocaleTimeString('zh-CN', { hour12: false })); render();
    } catch (error) { text(byId('refresh-status'), '读取失败'); toast('后台读取失败：' + error.message); }
    finally {
      refreshPending = false; byId('refresh-broadcasters').disabled = false; document.body.removeAttribute('aria-busy');
      if (requestedQuery !== query) refresh();
    }
  }
  async function mutate(url, method = 'POST', body = {}) { try { await request(url, { method, body: JSON.stringify(body) }); toast('操作完成'); await refresh(); } catch (error) { toast('操作失败：' + error.message); } }
  byId('search-input')?.addEventListener('input', event => { query = event.target.value.trim(); refresh(); });
  byId('refresh-broadcasters').addEventListener('click', refresh);
  byId('disconnect-button')?.addEventListener('click', () => { const item = broadcasterById(selectedId); if (item && window.confirm('确认强制断开 ' + item.broadcasterName + '？')) mutate('/admin/api/broadcasters/' + encodeURIComponent(item.deviceId) + '/disconnect'); });
  byId('delete-button')?.addEventListener('click', () => { const item = broadcasterById(selectedId); if (item && confirmDanger('将删除 ' + item.broadcasterName + ' 的离线大厅数据。', '此操作保留设备身份，但比赛快照和同步记录无法恢复。')) mutate('/admin/api/broadcasters/' + encodeURIComponent(item.deviceId) + '/data', 'DELETE'); });
  byId('bind-license')?.addEventListener('click', () => {
    const item = broadcasterById(selectedId), licenseId = Number(byId('license-select')?.value || 0);
    if (!item || !Number.isSafeInteger(licenseId) || licenseId <= 0) { toast('请选择已激活密钥'); return; }
    if (window.confirm('确认将 #' + licenseId + ' 绑定给 ' + item.broadcasterName + '？')) {
      mutate('/admin/api/broadcasters/' + encodeURIComponent(item.deviceId) + '/license', 'PUT', { licenseId });
    }
  });
  byId('cleanup-expired')?.addEventListener('click', () => { if (window.confirm('立即清理已过期记录？')) mutate('/admin/api/cleanup/expired'); });
  byId('cleanup-offline')?.addEventListener('click', () => { if (confirmDanger('将清空全部离线主播和临时多开测试数据。', '在线正式主播不会被删除。')) mutate('/admin/api/cleanup/offline'); });
  byId('set-ocr-policy')?.addEventListener('click', () => { const deviceId = byId('ocr-device-id')?.value.trim() || ''; const rawUntil = byId('ocr-until')?.value.trim() || ''; if (!deviceId || !rawUntil) { toast('请填写设备 ID 和截止时间'); return; } mutate('/admin/api/broadcasters/' + encodeURIComponent(deviceId) + '/ocr-policy', 'PUT', { disabledUntil: Number(rawUntil) }); });
  byId('clear-ocr-policy')?.addEventListener('click', () => { const deviceId = byId('ocr-device-id').value.trim(); if (!deviceId) { toast('请填写设备 ID'); return; } mutate('/admin/api/broadcasters/' + encodeURIComponent(deviceId) + '/ocr-policy', 'PUT', { disabledUntil: null }); });
  refresh(); setInterval(refresh, 3000);
})();
`;
