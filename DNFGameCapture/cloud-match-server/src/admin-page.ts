function escapeHtmlAttribute(value: string): string {
  return value
    .replaceAll('&', '&amp;')
    .replaceAll('"', '&quot;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;');
}

export function buildAdminPage(csrfToken: string): string {
  return `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="dnf-admin-csrf" content="${escapeHtmlAttribute(csrfToken)}">
  <title>DNF 云端同步管理</title>
  <link rel="stylesheet" href="/admin/style.css">
</head>
<body>
  <header class="topbar">
    <div><strong>DNF 云端同步管理</strong><span>本机 SSH 隧道</span></div>
    <div class="health"><i></i><span id="refresh-status">正在读取</span></div>
  </header>
  <main class="workspace">
    <aside class="directory">
      <div class="section-heading"><span>主播大厅</span><b id="broadcaster-count">0</b></div>
      <label class="search"><span>搜索</span><input id="search-input" type="search" placeholder="主播名称或设备尾号" autocomplete="off"></label>
      <div id="broadcaster-list" class="broadcaster-list"></div>
    </aside>
    <section class="detail">
      <div id="empty-state" class="empty-state">从左侧选择主播查看比赛快照</div>
      <div id="detail-content" hidden>
        <header class="detail-header">
          <div><h1 id="detail-name"></h1><p id="detail-meta"></p></div>
          <div class="actions"><button id="disconnect-button">强制断开</button><button class="danger" id="delete-button">删除离线数据</button></div>
        </header>
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
    </aside>
  </main>
  <div id="toast" class="toast" role="status"></div>
  <script src="/admin/app.js" defer></script>
</body>
</html>`;
}

export const ADMIN_PAGE_CSS = `
:root{color-scheme:dark;--bg:#0b1015;--surface:#111820;--raised:#17212a;--line:#2b3945;--text:#e8f0f4;--muted:#91a2ae;--accent:#60c3dd;--red:#ff6e7f;--blue:#62a8ff;--warn:#f3c96c}*{box-sizing:border-box}html,body{height:100%;margin:0;background:var(--bg);color:var(--text);font-family:"Microsoft YaHei UI","Microsoft YaHei",sans-serif;font-size:14px}button,input{font:inherit}.topbar{height:58px;display:flex;align-items:center;justify-content:space-between;padding:0 22px;border-bottom:1px solid var(--line);background:#0e151c}.topbar strong{font-size:18px}.topbar span{margin-left:10px;color:var(--muted);font-size:12px}.health{display:flex;align-items:center}.health i{width:8px;height:8px;border-radius:50%;background:#48c78e;box-shadow:0 0 12px #48c78e}.workspace{height:calc(100% - 58px);display:grid;grid-template-columns:280px minmax(520px,1fr) 350px;min-width:1040px}.directory,.activity{min-height:0;overflow:auto;background:var(--surface)}.directory{border-right:1px solid var(--line)}.activity{border-left:1px solid var(--line)}.section-heading{height:45px;display:flex;align-items:center;justify-content:space-between;padding:0 14px;border-bottom:1px solid var(--line);font-weight:800}.section-heading b{min-width:24px;padding:2px 7px;border-radius:10px;text-align:center;color:var(--accent);background:#20313b;font-size:11px}.search{display:grid;gap:5px;padding:11px 14px;border-bottom:1px solid var(--line);color:var(--muted);font-size:11px}.search input{width:100%;height:34px;padding:0 10px;border:1px solid var(--line);border-radius:4px;outline:0;color:var(--text);background:var(--bg)}.search input:focus{border-color:var(--accent)}.broadcaster-list{display:grid}.broadcaster-row{width:100%;display:grid;grid-template-columns:9px minmax(0,1fr) auto;gap:10px;align-items:center;padding:12px 14px;border:0;border-bottom:1px solid #1e2932;text-align:left;color:var(--text);background:transparent;cursor:pointer}.broadcaster-row:hover,.broadcaster-row.active{background:var(--raised)}.broadcaster-row.offline{color:#778690}.presence{width:8px;height:8px;border-radius:50%;background:#58646d}.online .presence{background:#48c78e}.broadcaster-copy{min-width:0}.broadcaster-copy strong,.broadcaster-copy small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.broadcaster-copy strong{font-size:14px}.broadcaster-copy small{margin-top:4px;color:var(--muted);font-size:11px}.revision{color:var(--muted);font:11px Consolas,monospace}.detail{min-width:0;overflow:auto;padding:20px 22px}.empty-state{display:grid;place-items:center;height:100%;color:var(--muted)}.empty-state[hidden],#detail-content[hidden]{display:none!important}.detail-header{display:flex;align-items:flex-start;justify-content:space-between;gap:20px;padding-bottom:15px;border-bottom:1px solid var(--line)}h1{margin:0;font-size:24px;letter-spacing:0}.detail-header p{margin:6px 0 0;color:var(--muted);font-size:12px}.actions{display:flex;gap:8px}button{min-height:34px;padding:6px 11px;border:1px solid var(--line);border-radius:4px;color:var(--text);background:var(--raised);cursor:pointer}button:hover{border-color:var(--accent)}button:disabled{opacity:.45;cursor:not-allowed}.danger{border-color:#713441;color:#ff9aa6}.score-line{margin:16px 0;padding:14px 16px;border-left:4px solid var(--accent);background:var(--surface);font-size:18px;font-weight:900}.teams{display:grid;grid-template-columns:1fr 1fr;gap:14px}.team{border:1px solid var(--line);background:var(--surface)}.team.red{border-top:3px solid var(--red)}.team.blue{border-top:3px solid var(--blue)}.team h2{margin:0;padding:10px 12px;border-bottom:1px solid var(--line);font-size:15px}.player{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:10px;align-items:center;min-height:58px;padding:9px 12px;border-bottom:1px solid #202b34}.player:last-child{border-bottom:0}.player strong,.player small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.player strong{font-size:16px}.player small{margin-top:4px;color:var(--muted);font-size:11px}.stats{color:var(--accent);font:11px Consolas,monospace;white-space:nowrap}.activity section{border-bottom:1px solid var(--line)}.activity-list{max-height:250px;overflow:auto}.activity-item{padding:10px 14px;border-bottom:1px solid #202b34;color:var(--muted);font-size:12px;line-height:1.5}.activity-item.bidirectional{border-left:3px solid var(--warn);background:#30291a}.activity-item strong{display:block;color:var(--text)}.activity-item button{width:100%;margin-top:7px}.maintenance{padding-bottom:12px}.maintenance>button{width:calc(100% - 28px);margin:10px 14px 0}.toast{position:fixed;right:18px;bottom:18px;max-width:420px;padding:10px 14px;border:1px solid var(--line);border-radius:4px;color:var(--text);background:#1a2730;box-shadow:0 12px 30px #0008;opacity:0;pointer-events:none;transform:translateY(8px);transition:.18s}.toast.show{opacity:1;transform:none}@media(max-width:1200px){.workspace{grid-template-columns:250px minmax(480px,1fr) 310px}.teams{grid-template-columns:1fr}.player{grid-template-columns:1fr}.stats{white-space:normal}}
`;

export const ADMIN_PAGE_JS = `
(() => {
  'use strict';
  const csrf = document.querySelector('meta[name="dnf-admin-csrf"]')?.content || '';
  const byId = id => document.getElementById(id);
  let state = { broadcasters: [], relations: [], history: [] };
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
      const meta = document.createElement('small'); text(meta, item.online ? '在线' : '离线 · 保留至 ' + formatTime(item.offlineExpiresAt)); copy.append(name, meta);
      const revision = document.createElement('span'); revision.className = 'revision'; text(revision, 'r' + (item.snapshotRevision || 0));
      button.append(dot, copy, revision); button.addEventListener('click', () => { selectedId = item.deviceId; render(); }); list.append(button);
    }
  }
  function playerRow(player) {
    const row = document.createElement('div'); row.className = 'player'; const identity = document.createElement('div');
    const main = document.createElement('strong'); text(main, player?.mainName || '未设置');
    const aliases = document.createElement('small'); text(aliases, (player?.aliases || []).join(' / ') || '无小号'); identity.append(main, aliases);
    const stats = document.createElement('span'); stats.className = 'stats'; text(stats, '杀 ' + (player?.kills || 0) + ' · 死 ' + (player?.deaths || 0) + ' · AK ' + (player?.ak || 0) + ' · 连杀 ' + (player?.streak || 0)); row.append(identity, stats); return row;
  }
  function team(title, players, side) { const section = document.createElement('section'); section.className = 'team ' + side; const heading = document.createElement('h2'); text(heading, title); section.append(heading); players.forEach(item => section.append(playerRow(item))); return section; }
  function renderDetail() {
    const item = broadcasterById(selectedId); byId('empty-state').hidden = !!item; byId('detail-content').hidden = !item; if (!item) return;
    text(byId('detail-name'), item.broadcasterName); text(byId('detail-meta'), (item.online ? '在线' : '离线') + ' · ' + item.deviceId + ' · 快照 ' + formatTime(item.receivedAt));
    const snap = item.snapshot; text(byId('score-line'), snap ? '比分 ' + snap.redScore + ' : ' + snap.blueScore + ' · 红方' + (snap.redPickFirst ? '先手' : '后手') + ' · 显示设置由主播本地管理' : '暂无有效比赛快照');
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
  async function refresh() { if (refreshPending) return; refreshPending = true; try { const suffix = query ? '?q=' + encodeURIComponent(query) : ''; state = await request('/admin/api/state' + suffix); if (selectedId && !broadcasterById(selectedId)) selectedId = ''; text(byId('refresh-status'), '已更新 ' + new Date().toLocaleTimeString('zh-CN', { hour12: false })); render(); } catch (error) { text(byId('refresh-status'), '读取失败'); toast('后台读取失败：' + error.message); } finally { refreshPending = false; } }
  async function mutate(url, method = 'POST') { try { await request(url, { method, body: '{}' }); toast('操作完成'); await refresh(); } catch (error) { toast('操作失败：' + error.message); } }
  byId('search-input')?.addEventListener('input', event => { query = event.target.value.trim(); refresh(); });
  byId('disconnect-button')?.addEventListener('click', () => { const item = broadcasterById(selectedId); if (item && window.confirm('确认强制断开 ' + item.broadcasterName + '？')) mutate('/admin/api/broadcasters/' + encodeURIComponent(item.deviceId) + '/disconnect'); });
  byId('delete-button')?.addEventListener('click', () => { const item = broadcasterById(selectedId); if (item && confirmDanger('将删除 ' + item.broadcasterName + ' 的离线大厅数据。', '此操作保留设备身份，但比赛快照和同步记录无法恢复。')) mutate('/admin/api/broadcasters/' + encodeURIComponent(item.deviceId) + '/data', 'DELETE'); });
  byId('cleanup-expired')?.addEventListener('click', () => { if (window.confirm('立即清理已过期记录？')) mutate('/admin/api/cleanup/expired'); });
  byId('cleanup-offline')?.addEventListener('click', () => { if (confirmDanger('将清空全部离线主播和临时多开测试数据。', '在线正式主播不会被删除。')) mutate('/admin/api/cleanup/offline'); });
  refresh(); setInterval(refresh, 3000);
})();
`;
