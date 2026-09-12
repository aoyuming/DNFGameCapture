// Structure and palette adapted from the legacy web-admin console; data access is SQLite-only.
export function buildLibraryAdminPage(csrfToken: string): string {
  const token = csrfToken.replace(/[&<>"']/g, value => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[value]!);
  return `<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="dnf-admin-csrf" content="${token}"><title>DNF 共享库管理台</title>
<link rel="stylesheet" href="/admin/library/style.css"></head><body>
<header class="topbar"><div><h1>DNF 共享库管理台</h1><div class="subline" id="status-text">正在读取公共库...</div></div>
<div class="top-actions"><a href="/admin">管理首页</a><button id="btn-refresh">刷新</button></div></header>
<section class="stats-grid" aria-label="公共库统计">
<div class="stat-card"><span>待审核</span><b id="stat-pending">0</b></div>
<div class="stat-card warn"><span>名称归属冲突</span><b id="stat-conflict">0</b></div>
<div class="stat-card"><span>待审游戏ID</span><b id="stat-pending-alias">0</b></div>
<div class="stat-card"><span>公共库 · 选手 / 游戏ID</span><b id="stat-public">0 / 0</b></div></section>
<div id="message" role="status" aria-live="polite" hidden></div>
<main class="workspace">
<section class="panel pending-panel" aria-labelledby="pending-heading">
<div class="panel-head"><div><h2 id="pending-heading">待审核投稿</h2><span id="pending-meta">读取中...</span></div>
<label class="checkbox-line"><input type="checkbox" id="check-all">全选</label></div>
<div class="filters"><input id="pending-search" aria-label="搜索待审核投稿" placeholder="搜索名称 / ID / 提交者">
<select id="conflict-filter" aria-label="名称归属冲突筛选"><option value="all">全部</option><option value="clean">无名称冲突</option><option value="conflict">有名称冲突</option></select></div>
<div class="pending-list-box list-box"><span>待审核列表</span><div id="pending-list" class="pending-list"></div></div></section>
<section class="panel detail-panel" aria-labelledby="detail-heading">
<div class="panel-head"><div><h2 id="detail-heading">投稿详情</h2><span id="detail-meta">未选择投稿</span></div><button id="btn-detail-reload" class="quiet" disabled>重新载入</button></div>
<div id="detail-view" class="detail-content"><p class="empty">暂无选中投稿</p></div></section>
<section class="panel public-panel" aria-labelledby="public-heading">
<div class="panel-head"><div><h2 id="public-heading">公共库管理</h2><span id="public-meta">读取中...</span></div></div>
<div class="filters"><input id="public-search" aria-label="搜索公共库" placeholder="搜索公共库"><button id="btn-public-search" class="quiet">搜索</button></div>
<div class="public-crud"><label class="list-box"><span>选手</span><select id="public-main-list" size="8" aria-label="选手实体"></select></label>
<label class="list-box"><span>游戏ID</span><select id="public-alias-list" size="8" aria-label="实体标识列表"></select></label></div>
<div class="public-editor" id="public-editor">
<div class="editor-heading"><span id="public-edit-state">新建选手</span><button id="btn-public-reload" class="quiet" disabled>重新载入</button></div>
<label><span>并列名称 / 别名</span><textarea id="public-names" rows="2" aria-label="公共库并列名称 / 别名"></textarea></label>
<label><span>游戏ID</span><textarea id="public-game-ids" rows="4" aria-label="公共库游戏ID"></textarea></label>
<div class="public-actions"><button id="btn-public-new" class="quiet">新建</button><button id="btn-public-add" class="primary">新增</button><button id="btn-public-save">保存修改</button><button id="btn-public-delete" class="danger">删除</button></div>
<button id="btn-public-merge">合并公共选手...</button>
<label><span>一键导入 · 旧版文本 / v2 JSON</span><textarea id="public-import-text" rows="4" aria-label="导入内容" placeholder="选手=(游戏ID一)(游戏ID二)"></textarea></label>
<button id="btn-public-import" class="primary">检查并导入</button><div id="import-result" role="status"></div></div></section></main>
<footer class="batchbar"><div>已选 <b id="selected-count">0</b> 条 <span id="selected-info"></span></div>
<div class="batch-actions"><button id="btn-resolve-by-name" class="primary">按名称一键合并</button><button id="btn-approve" class="primary">通过当前/选中</button><button id="btn-approve-added" class="primary">一键通过新增</button><button id="btn-reject" class="quiet">驳回当前/选中</button><button id="btn-clear-selected" class="quiet">取消选择</button></div></footer>
<dialog id="merge-dialog" aria-labelledby="merge-heading"><h2 id="merge-heading">合并公共选手</h2>
<div id="merge-options" class="merge-options"></div>
<label class="merge-target-label">保留选手ID<select id="merge-target" aria-label="保留选手ID"></select></label>
<h3>合并结果</h3><div id="merge-preview" class="merge-preview"></div>
<p id="merge-error" class="review-note" role="status" hidden></p>
<div class="review-actions"><button id="btn-merge-cancel">取消</button><button id="btn-merge-confirm" class="danger">确认合并</button></div></dialog>
<dialog id="conflict-batch-dialog" aria-labelledby="conflict-batch-heading">
<h2 id="conflict-batch-heading">批量冲突合并预览</h2>
<p id="conflict-batch-summary" aria-live="polite"></p>
<div class="conflict-batch-tools" role="group" aria-label="冲突组选择操作"><button id="btn-conflict-batch-safe-all" class="quiet">选择安全项</button><button id="btn-conflict-batch-clear" class="quiet">清空选择</button></div>
<div id="conflict-batch-list" class="conflict-batch-list" role="list" aria-label="冲突处理组"></div>
<p id="conflict-batch-error" class="review-note" role="status" hidden></p>
<div class="review-actions conflict-batch-actions"><button id="btn-conflict-batch-cancel">取消</button><button id="btn-conflict-batch-confirm" class="danger">确认合并</button></div></dialog>
<script src="/admin/library/app.js"></script></body></html>`;
}

export const LIBRARY_ADMIN_CSS = String.raw`
.conflict-merge{display:block;max-width:100%;white-space:normal;overflow-wrap:anywhere;text-align:left;margin:8px 0}
#merge-dialog{width:min(640px,calc(100vw - 24px));max-height:calc(100dvh - 32px);overflow:auto;background:var(--panel);color:var(--text);border:1px solid var(--line);border-radius:8px;padding:20px}#merge-dialog::backdrop{background:#000a}.merge-options{max-height:220px;overflow:auto;border-block:1px solid var(--line);margin:14px 0;padding:8px 0}.merge-options label{display:flex;align-items:start;gap:8px;padding:7px 0;overflow-wrap:anywhere}.merge-options input{width:16px;height:16px;flex-shrink:0}.merge-target-label{display:block;margin-bottom:16px}.merge-target-label select{margin-top:6px}.merge-preview{font-size:12px;line-height:1.7;overflow-wrap:anywhere}.merge-preview p{margin:8px 0}
#conflict-batch-dialog{width:min(860px,calc(100vw - 24px));height:min(720px,calc(100dvh - 24px));max-width:none;max-height:none;overflow:hidden;background:var(--panel);color:var(--text);border:1px solid var(--line);border-radius:8px;padding:18px}#conflict-batch-dialog[open]{display:grid;grid-template-rows:auto auto auto minmax(120px,1fr) auto auto;gap:12px}#conflict-batch-dialog::backdrop{background:#000a}#conflict-batch-summary{margin:0;color:var(--muted);font-size:12px;line-height:1.7;overflow-wrap:anywhere}.conflict-batch-tools{display:flex;gap:8px}.conflict-batch-tools button,.conflict-batch-actions button{min-height:44px}.conflict-batch-list{min-width:0;min-height:0;overflow:auto;overscroll-behavior:contain;border-block:1px solid var(--line)}.conflict-batch-row{display:grid;grid-template-columns:44px minmax(0,1fr) 240px;gap:10px;padding:12px 0;border-bottom:1px solid #ffffff14;min-width:0}.conflict-batch-row:last-child{border-bottom:0}.conflict-batch-check{display:flex;align-items:flex-start;justify-content:center;min-width:44px;min-height:44px;padding-top:12px}.conflict-batch-check input{width:20px;height:20px;margin:0;accent-color:var(--accent)}.conflict-batch-copy,.conflict-batch-target{min-width:0;overflow-wrap:anywhere}.conflict-batch-kind{display:block;color:var(--accent);font-size:13px;font-weight:700;line-height:1.5}.conflict-batch-row[data-kind="ambiguous"] .conflict-batch-kind{color:var(--warn)}.conflict-batch-line{margin:5px 0 0;color:var(--muted);font-size:12px;line-height:1.65;overflow-wrap:anywhere}.conflict-batch-line strong{color:var(--text)}.conflict-batch-target{align-self:start}.conflict-batch-target>span{display:block;color:var(--muted);font-size:12px;margin-bottom:6px}.conflict-batch-target select{min-height:44px}.conflict-batch-fixed{color:var(--text);font-size:12px;line-height:1.65}.conflict-batch-reason{color:var(--warn);font-size:12px;line-height:1.65}.conflict-batch-actions{justify-content:flex-end;margin-top:0}#conflict-batch-error{margin:0}
:root{color-scheme:dark;--bg:#10141c;--panel:#171d27;--panel-2:#1f2733;--line:#2d3748;--text:#edf2f7;--muted:#91a0b5;--accent:#4fd1c5;--danger:#fc8181;--warn:#f6c66f}
*{box-sizing:border-box;letter-spacing:0}body{margin:0;min-height:100vh;background:var(--bg);color:var(--text);font:14px "Microsoft YaHei","Segoe UI",sans-serif;padding-bottom:76px}button,input,select,textarea{font:inherit;min-width:0}button{border:1px solid var(--line);background:var(--panel-2);color:var(--text);border-radius:6px;padding:8px 10px;cursor:pointer;font-weight:700;min-height:36px}button:hover{border-color:var(--accent);color:var(--accent)}button.primary{background:#123d3b;border-color:var(--accent);color:#d9fffb}button.danger{background:#452028;border-color:var(--danger);color:#ffe5e5}button.quiet{background:transparent}button:disabled{opacity:.45;cursor:not-allowed}a{color:var(--accent);text-underline-offset:4px}input,select,textarea{width:100%;border:1px solid var(--line);background:#0d1118;color:var(--text);border-radius:6px;padding:8px 10px}textarea{resize:vertical;line-height:1.5;min-height:64px;white-space:pre-wrap;overflow-wrap:anywhere}button:focus-visible,a:focus-visible,input:focus-visible,select:focus-visible,textarea:focus-visible{outline:2px solid var(--accent);outline-offset:2px}input:focus,select:focus,textarea:focus{border-color:var(--accent)}[hidden]{display:none!important}
.topbar{display:flex;justify-content:space-between;align-items:center;gap:18px;padding:18px 22px 12px;border-bottom:1px solid var(--line);background:#111720}h1,h2,h3{margin:0}h1{font-size:22px}h2{font-size:15px}h3{font-size:14px}.subline,.panel-head span{color:var(--muted);font-size:12px;margin-top:5px;display:block;overflow-wrap:anywhere}.top-actions{display:flex;gap:12px;align-items:center;flex-shrink:0}.stats-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;padding:14px 22px}.stat-card{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px 14px}.stat-card span{color:var(--muted);font-size:12px}.stat-card b{display:block;margin-top:6px;font-size:24px;color:var(--accent);overflow-wrap:anywhere}.stat-card.warn b{color:var(--warn)}
.workspace{display:grid;grid-template-columns:minmax(250px,.95fr) minmax(320px,1.15fr) minmax(340px,.9fr);gap:12px;padding:0 22px 18px;height:calc(100vh - 236px);min-height:520px}.panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;min-width:0;min-height:0;overflow:hidden;display:flex;flex-direction:column}.panel-head{min-height:58px;display:flex;align-items:center;justify-content:space-between;gap:12px;padding:12px 14px;border-bottom:1px solid var(--line)}.panel-head>div{min-width:0}.panel-head button{font-size:12px;flex-shrink:0}.checkbox-line{display:inline-flex;align-items:center;gap:6px;color:var(--muted);font-size:13px;white-space:nowrap}.checkbox-line input{width:16px;height:16px;accent-color:var(--accent)}.filters{display:grid;grid-template-columns:minmax(0,1fr) 100px;gap:8px;padding:10px 12px;border-bottom:1px solid var(--line)}.pending-list,#detail-view{overflow:auto;min-height:0;overscroll-behavior:contain}.pending-list-box{flex:1;min-height:0;padding:12px;display:flex;flex-direction:column}.pending-list{flex:1;border:1px solid var(--line);border-radius:6px;background:#0d1118}.pending-item{display:grid;grid-template-columns:20px minmax(0,1fr);gap:8px;padding:10px;border-bottom:1px solid #ffffff0f}.pending-item.active{background:#4fd1c514}.pending-item.conflict{box-shadow:inset 3px 0 0 var(--warn)}.pending-item input{width:16px;height:16px;accent-color:var(--accent)}.pending-open{display:block;text-align:left;border:0;background:transparent;padding:0;min-width:0;font-weight:400}.pending-title{display:block;font-weight:700;overflow-wrap:anywhere}.pending-sub{display:block;color:var(--muted);font-size:12px;margin-top:5px;overflow-wrap:anywhere}.pending-badge{color:var(--warn);font-size:12px;display:block;margin-top:5px}
.empty{color:var(--muted);margin:0;padding:16px;line-height:1.6}.detail-content{flex:1;padding:14px}.detail-content>.empty{padding:4px}.detail-summary{color:var(--muted);font-size:12px;line-height:1.7;overflow-wrap:anywhere}.section-title{color:var(--accent);font-size:13px;font-weight:700;margin:14px 0 8px}.conflict-row{color:#fff0d2;border-left:3px solid var(--warn);background:#f6c66f14;padding:8px 10px;margin:8px 0;font-size:12px;line-height:1.6;overflow-wrap:anywhere}.review-entity{border:0;border-top:1px solid var(--line);padding:12px 0;margin:12px 0 0;min-width:0}.review-entity legend{color:var(--accent);font-size:13px}.review-entity label{display:block;margin-bottom:9px}.review-entity label span,.public-editor label span,.list-box>span{display:block;color:var(--muted);font-size:12px;margin-bottom:6px}.review-entity textarea{min-height:64px}.review-actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}.review-entity .danger{font-size:12px}.review-note{color:var(--warn);font-size:12px;margin:8px 0;overflow-wrap:anywhere}
.public-crud{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,.95fr);gap:10px;padding:12px;border-bottom:1px solid var(--line)}.list-box{min-width:0}.list-box select{height:180px;padding:6px;border-radius:4px}.list-box option{padding:4px 6px}.public-editor{display:grid;gap:10px;padding:12px;overflow:auto;min-height:0;overscroll-behavior:contain}.public-editor>label{min-width:0}.public-actions{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px}.public-actions button{font-size:12px;padding:8px 4px}.editor-heading{display:flex;justify-content:space-between;gap:8px;align-items:center;color:var(--muted);font-size:12px;overflow-wrap:anywhere}.editor-heading span{min-width:0}.editor-heading button{font-size:12px;flex-shrink:0}#import-result{color:var(--warn);font-size:12px;line-height:1.6;overflow-wrap:anywhere}
.batchbar{position:fixed;left:0;right:0;bottom:0;min-height:64px;display:flex;align-items:center;justify-content:space-between;gap:14px;padding:10px 22px;background:#0c1119fa;border-top:1px solid var(--line);z-index:10}.batch-actions{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}.batch-actions button{white-space:nowrap}#selected-info{color:var(--muted);font-size:12px}#message{position:fixed;right:22px;top:82px;max-width:min(560px,calc(100vw - 44px));max-height:190px;overflow:auto;padding:12px 16px;border:1px solid var(--accent);background:#0d1118;z-index:20;font-size:13px;line-height:1.6;overflow-wrap:anywhere;box-shadow:0 8px 24px #0008;cursor:pointer}#message.error{border-color:var(--danger);color:#ffe5e5}
.pending-title{font-size:14px;line-height:20px;white-space:nowrap}.pending-device{display:block;margin-top:4px;color:var(--muted);font-size:11px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.pending-sub{line-height:1.55}.submission-summary{padding:10px 0;border-bottom:1px solid var(--line);font-size:12px;line-height:1.7;overflow-wrap:anywhere}.submission-summary p{margin:4px 0}.submission-summary p:nth-child(2){color:var(--accent)}.reconciliation-list,.unchanged-entities{margin:12px 0;border-top:1px solid var(--line);font-size:12px;overflow-wrap:anywhere}summary{cursor:pointer;padding:10px 0;color:var(--muted)}summary:focus-visible{outline:2px solid var(--accent);outline-offset:2px}.reconciliation-rows{max-height:180px;overflow:auto}.reconciliation-rows p{margin:0 0 8px;line-height:1.6}.entity-delta{font-size:12px;line-height:1.6;color:var(--accent);overflow-wrap:anywhere;margin-bottom:12px}.entity-delta p{margin:4px 0}.unchanged-entities .entity-delta{color:var(--muted)}
@media(max-width:1080px){body{padding-bottom:0}.workspace{grid-template-columns:1fr 1fr;height:auto;min-height:0}.pending-panel,.detail-panel{height:580px}.public-panel{grid-column:1/-1;max-height:900px}.public-editor{max-height:530px}.stats-grid{grid-template-columns:repeat(4,minmax(0,1fr))}.batchbar{position:sticky;flex-wrap:wrap;bottom:0}.batch-actions{justify-content:flex-start}}@media(max-width:680px){.topbar{align-items:flex-start;flex-direction:column;padding:14px 12px;gap:10px}h1{font-size:20px}.top-actions{width:100%;justify-content:space-between}.stats-grid{grid-template-columns:repeat(2,minmax(0,1fr));padding:12px;gap:8px}.stat-card{padding:10px}.stat-card b{font-size:22px}.workspace{grid-template-columns:minmax(0,1fr);padding:0 12px 12px}.pending-panel{height:380px}.detail-panel{height:650px}.public-panel{grid-column:auto;max-height:none}.public-editor{max-height:660px}.batchbar{padding:10px 12px;gap:8px}.batch-actions{display:grid;grid-template-columns:1fr 1fr;width:100%}.batch-actions button{font-size:12px;white-space:normal;padding:8px 5px}.public-actions{grid-template-columns:repeat(2,minmax(0,1fr))}#message{right:12px;top:12px;max-width:calc(100vw - 24px)}#conflict-batch-dialog{width:calc(100vw - 16px);height:calc(100dvh - 16px);padding:12px}.conflict-batch-row{grid-template-columns:44px minmax(0,1fr);gap:8px}.conflict-batch-target{grid-column:2}.conflict-batch-tools,.conflict-batch-actions{display:grid;grid-template-columns:minmax(0,1fr)}.conflict-batch-tools button,.conflict-batch-actions button{width:100%}}
`;

export const LIBRARY_ADMIN_JS = String.raw`
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const csrf = document.querySelector('meta[name="dnf-admin-csrf"]').content;
  const fields = [['names', '并列名称 / 别名'], ['gameIds', '游戏ID']];
  let state = { revision: 0, entities: [], submissions: [], conflictResolutionGroups: [], conflictResolutionStats: {}, stats: {} };
  let loaded = false, busy = false, active = null, reviewDirty = false, reviewRevision = 0;
  let publicId = null, publicDirty = false, publicRevision = 0;
  const selected = new Map();
  const conflictBatchSelections = new Map();
  let mergeRevision = 0, conflictBatchRevision = 0;
  const refreshInterval = 5000;
  let refreshTimer = null, refreshFlight = null, refreshRequested = false;
  let refreshGeneration = 0, latestRevision = 0, composing = false, authExpired = false;
  const errors = { stale_revision: '公共库版本已变化。草稿已保留，请重新载入后核对再提交。', stale_submission: '投稿已被修改。草稿已保留，请重新载入后核对。',
    stale_conflict_group: '冲突数据已变化，请刷新后重新确认。',
    submission_not_pending: '投稿已被其他管理员处理，列表将自动更新。', ownership_conflict: '存在归属冲突，未写入任何数据。',
    reconciliation_confirmation_required: '保存需要确认当前选手关联。草稿已保留，请重新载入并核对关联后再保存。',
    retired_entity_id: '此选手ID已合并停用，请使用保留的公共选手ID。', entity_has_redirects: '此选手承接历史ID，不能删除。可将其合并到另一公共选手。',
    invalid_request: '请求无效，请检查必填名称、ID长度和数量。', invalid_library: '内容格式无效，请检查名称、ID长度和数量。',
    duplicate_entity_id: '投稿内实体ID重复，请明确整理为一个实体后保存。', invalid_json: 'JSON格式错误。',
    library_too_large: '内容或合并后的公共库超过256 KiB限制。', invalid_csrf: '安全令牌已失效，请重新打开管理台。',
    entity_already_exists: '实体已存在。', entity_not_found: '实体已被删除，草稿已保留。', submission_not_found: '投稿不存在。', internal_error: '服务器处理失败，未完成操作。' };
  function node(tag, className, text) { const el = document.createElement(tag); if (className) el.className = className; if (text !== undefined) el.textContent = text; return el; }
  function message(text, error = false) {
    $('message').textContent = text; $('message').className = error ? 'error' : ''; $('message').hidden = false;
    if ($('merge-dialog').open) { $('merge-error').textContent = text; $('merge-error').hidden = false; }
    if ($('conflict-batch-dialog').open) { $('conflict-batch-error').textContent = text; $('conflict-batch-error').hidden = false; }
  }
  $('message').onclick = () => { $('message').hidden = true; };
  function conflictText(conflict) { return (fields.find(field => field[0] === conflict.kind)?.[1] || conflict.kind) + '「' + conflict.value + '」：' + conflict.entityIds.join(' / '); }
  async function api(path, body, method = 'POST', signal) {
    const response = await fetch('/admin/api/library' + path, { method: body === undefined ? 'GET' : method,
      cache: 'no-store', signal, headers: { 'Content-Type': 'application/json', 'x-dnf-admin-csrf': csrf }, body: body === undefined ? undefined : JSON.stringify(body) });
    if (response.status === 401 || response.status === 403) {
      authExpired = true;
      throw new Error('登录或安全令牌已失效，请重新打开管理台。草稿已保留。');
    }
    let data; try { data = await response.json(); } catch { throw new Error('服务器返回了无效响应。'); }
    if (!response.ok || !data.ok) {
      const detail = data.conflicts?.slice(0, 8).map(conflictText).join('；');
      const error = new Error((errors[data.code] || (data.code?.startsWith('invalid_import_line_') ? '导入第 ' + data.code.split('_').pop() + ' 行格式无效，未导入。' : data.code || '请求失败')) + (detail ? ' ' + detail : ''));
      error.code = data.code; throw error;
    }
    latestRevision = Math.max(latestRevision, Number(data.revision) || 0);
    authExpired = false;
    return data;
  }
  async function run(task) {
    if (busy) return;
    ++refreshGeneration;
    busy = true; controls(); document.body.setAttribute('aria-busy', 'true');
    try {
      // A foreground action owns the UI before waiting, invalidating any older poll.
      if (refreshFlight) await refreshFlight.catch(() => {});
      await task();
    } catch (error) { message(error.message || '网络连接失败，请重试。', true); }
    finally { busy = false; document.body.removeAttribute('aria-busy'); controls(); scheduleRefresh(refreshRequested ? 0 : refreshInterval); }
  }
  function scheduleRefresh(delay = refreshInterval) {
    clearTimeout(refreshTimer); refreshTimer = null;
    if (document.hidden || authExpired) return;
    refreshTimer = setTimeout(autoRefresh, delay);
  }
  async function autoRefresh() {
    clearTimeout(refreshTimer); refreshTimer = null;
    if (document.hidden || authExpired) return;
    if (busy || composing || $('merge-dialog').open || $('conflict-batch-dialog').open) { refreshRequested = true; return; }
    if (refreshFlight) return;
    refreshRequested = false;
    try { await refresh(true); }
    catch (error) { if (authExpired) message(error.message, true); }
    finally { scheduleRefresh(); }
  }
  function controls() {
    document.querySelectorAll('button,input,select,textarea').forEach(el => { el.disabled = busy || !loaded; });
    $('btn-refresh').disabled = busy;
    $('btn-refresh').textContent = busy ? '处理中...' : '刷新';
    $('btn-public-save').disabled = busy || !loaded || !publicId || !publicDirty;
    $('btn-public-delete').disabled = busy || !publicId;
    $('btn-public-add').disabled = busy || !loaded || !!publicId;
    $('btn-public-reload').disabled = busy || !publicId;
    $('btn-public-merge').disabled = busy || !loaded || state.entities.length < 2;
    $('btn-merge-confirm').disabled = busy || !loaded || mergeIds().length < 2 || mergeIds().length > 100 || !$('merge-target').value;
    const conflictGroups = Array.isArray(state.conflictResolutionGroups) ? state.conflictResolutionGroups : [];
    $('btn-resolve-by-name').disabled = busy || !loaded || !conflictGroups.length;
    const conflictGroupsByToken = new Map(conflictGroups.map(group => [group.token, group]));
    for (const row of $('conflict-batch-list').querySelectorAll('.conflict-batch-row')) {
      const group = conflictGroupsByToken.get(row.dataset.token), check = row.querySelector('input[type="checkbox"]');
      const select = row.querySelector('select');
      if (select) select.disabled = busy || !loaded || !group || group.kind !== 'public_merge';
      const hasPublicTarget = group?.kind === 'public_merge' && group.publicEntityIds.includes(select?.value);
      check.disabled = busy || !loaded || !group || group.kind === 'ambiguous' || (group.kind === 'public_merge' && !hasPublicTarget);
    }
    const conflictChoices = resolutionChoices();
    const checkedConflictCount = $('conflict-batch-list').querySelectorAll('input[type="checkbox"]:checked').length;
    $('btn-conflict-batch-safe-all').disabled = busy || !loaded || !conflictGroups.some(group => group.kind === 'unique_name_target' ||
      (group.kind === 'public_merge' && conflictBatchSelections.get(group.token)?.targetEntityId));
    $('btn-conflict-batch-clear').disabled = busy || !checkedConflictCount;
    $('btn-conflict-batch-cancel').disabled = busy;
    $('btn-conflict-batch-confirm').disabled = busy || !loaded || !conflictChoices.length || conflictChoices.length !== checkedConflictCount;
    $('btn-conflict-batch-confirm').textContent = busy && $('conflict-batch-dialog').open ? '合并中...' : '确认合并';
    if (busy && $('conflict-batch-dialog').open) $('conflict-batch-dialog').setAttribute('aria-busy', 'true');
    else $('conflict-batch-dialog').removeAttribute('aria-busy');
    $('btn-detail-reload').disabled = busy || !active;
    const targets = selected.size || (active?.status === 'pending' ? 1 : 0);
    const approvalTargets = selected.size ? state.submissions.filter(item => selected.has(item.id)) : active ? [active] : [];
    $('btn-approve').disabled = busy || !targets || approvalTargets.some(item => !item.valid);
    $('btn-reject').disabled = busy || !targets;
    $('btn-approve-added').disabled = busy || !state.submissions.some(isAddition);
    $('btn-clear-selected').disabled = busy || !selected.size;
    if ($('btn-review-save')) $('btn-review-save').disabled = busy || !reviewDirty || active?.status !== 'pending';
    if (active?.status !== 'pending') $('detail-view').querySelectorAll('input,textarea,select,button').forEach(el => { el.disabled = true; });
    $('selected-count').textContent = selected.size;
    $('selected-info').textContent = active ? '当前 #' + active.id : '';
    $('public-edit-state').textContent = (publicId || '新建选手') + (publicDirty ? ' · 未保存' : '') + (publicDirty && publicRevision !== state.revision ? ' · 版本已变化' : '');
    if ($('review-edit-state')) $('review-edit-state').textContent = (reviewDirty ? '未保存的审核修改' : '已载入审核内容') + (reviewRevision !== state.revision ? ' · 公共库版本已变化' : '');
  }
  const normalize = value => value.normalize('NFC').trim().toLocaleLowerCase();
  const matches = (entity, q) => [entity.entityId, ...entity.names, ...entity.gameIds].some(value => normalize(value).includes(q));
  function isAddition(item) {
    return item.valid && !item.conflicts.length && (item.addedEntityCount === undefined || item.addedEntityCount + item.updatedEntityCount > 0);
  }
  function changeSummary(item) {
    return item.addedEntityCount === undefined ? item.entities.length + ' 个实体' :
      '新增 ' + item.addedEntityCount + ' · 补充 ' + item.updatedEntityCount + ' · 无变化 ' + item.unchangedCount;
  }
  function additionsSummary(item) {
    return item.additions ? '名称 +' + item.additions.names + ' · 游戏ID +' + item.additions.gameIds : '';
  }
  function visiblePending() {
    const q = normalize($('pending-search').value), filter = $('conflict-filter').value;
    return state.submissions.filter(item => (!q || normalize(item.deviceId).includes(q) || item.entities.some(entity => matches(entity, q))) &&
      (filter === 'conflict' ? !item.valid || item.conflicts.length : filter === 'clean' ? item.valid && !item.conflicts.length : true));
  }
  function renderPending() {
    const list = $('pending-list'), scroll = list.scrollTop, visible = visiblePending(); list.replaceChildren();
    $('pending-meta').textContent = visible.length + ' / ' + state.submissions.length + ' 条 · 增量投稿';
    if (!visible.length) list.append(node('p', 'empty', '没有符合条件的待审核记录。'));
    for (const item of visible) {
      const row = node('div', 'pending-item' + (item.id === active?.id ? ' active' : '') + (!item.valid || item.conflicts.length ? ' conflict' : ''));
      const check = node('input'); check.type = 'checkbox'; check.checked = selected.has(item.id); check.setAttribute('aria-label', '选择投稿 ' + item.id);
      check.onchange = () => { ++refreshGeneration; if (check.checked) selected.set(item.id, { submissionRevision: item.submissionRevision, revision: state.revision }); else selected.delete(item.id); renderPending(); controls(); };
      const open = node('button', 'pending-open'); open.dataset.submissionId = item.id; open.setAttribute('aria-label', '查看投稿 ' + item.id);
      open.append(node('span', 'pending-title', '投稿 #' + item.id));
      const device = node('span', 'pending-device', item.deviceId); device.title = item.deviceId; open.append(device);
      open.append(node('span', 'pending-sub', new Date(item.createdAt * 1000).toLocaleString() + ' · ' + item.entities.length + ' 个实体'));
      open.append(node('span', 'pending-sub', changeSummary(item)));
      if (item.additions) open.append(node('span', 'pending-sub', additionsSummary(item)));
      if (!item.valid || item.conflicts.length) open.append(node('span', 'pending-badge', !item.valid ? '内容格式无效' : item.conflicts.length + ' 项归属冲突'));
      const guard = selected.get(item.id);
      if (guard && (guard.submissionRevision !== item.submissionRevision || guard.revision !== state.revision)) open.append(node('span', 'pending-badge', '已选版本过期，请重新选择'));
      open.onclick = () => run(() => openSubmission(item.id)); row.append(check, open); list.append(row);
    }
    list.scrollTop = scroll;
    const count = visible.filter(item => selected.has(item.id)).length;
    $('check-all').checked = visible.length > 0 && count === visible.length;
    $('check-all').indeterminate = count > 0 && count < visible.length;
  }
  function updateOptions(select, items, value) {
    const scroll = select.scrollTop, existing = new Map([...select.options].map(option => [option.value, option]));
    const keys = new Set(items.map(item => item.value));
    for (const [index, item] of items.entries()) {
      const option = existing.get(item.value) || node('option');
      if (option.value !== item.value) option.value = item.value;
      if (option.textContent !== item.label) option.textContent = item.label;
      option.title = item.label;
      if (select.options[index] !== option) select.insertBefore(option, select.options[index] || null);
    }
    for (const option of [...select.options]) if (!keys.has(option.value)) option.remove();
    if (select.value !== value) select.value = value;
    select.scrollTop = scroll;
  }
  function renderPublicList() {
    const q = normalize($('public-search').value);
    updateOptions($('public-main-list'), state.entities.filter(entity => matches(entity, q)).map(entity => ({
      value: entity.entityId, label: entity.names.join(' / ') + ' (' + entity.gameIds.length + ')'
    })), publicId || '');
    const entity = state.entities.find(entity => entity.entityId === publicId);
    const aliases = entity ? fields.slice(1).flatMap(([field, label]) => entity[field].map(value => ({ value: label + ' · ' + value, label: label + ' · ' + value }))) : [];
    updateOptions($('public-alias-list'), aliases, $('public-alias-list').value);
    $('public-meta').textContent = '版本 ' + state.revision + ' · ' + state.entities.length + ' 位选手 / ' + (state.stats.gameIds || 0) + ' 游戏ID';
  }
  function fillPublic(entity) {
    publicId = entity?.entityId || null; publicRevision = state.revision; publicDirty = false;
    $('public-names').value = entity?.names.join('\n') || '';
    $('public-game-ids').value = entity?.gameIds.join('\n') || '';
    renderPublicList(); controls();
  }
  function preserveView() {
    const focused = document.activeElement;
    const selector = focused?.id ? '#' + CSS.escape(focused.id) : focused?.hasAttribute('aria-label') ? '[aria-label="' + CSS.escape(focused.getAttribute('aria-label')) + '"]' : null;
    const selection = typeof focused?.selectionStart === 'number' ? [focused.selectionStart, focused.selectionEnd, focused.selectionDirection] : null;
    const scrolls = ['pending-list', 'detail-view', 'public-main-list', 'public-alias-list', 'public-editor', 'conflict-batch-list']
      .map(id => [$(id), $(id).scrollTop, $(id).scrollLeft]);
    const inputScroll = focused ? [focused.scrollTop, focused.scrollLeft] : [0, 0];
    const alias = $('public-alias-list').value;
    const expanded = [...$('detail-view').querySelectorAll('details')].map(el => [el.className, el.open]);
    const windowScroll = [scrollX, scrollY];
    return () => {
      for (const [className, open] of expanded) {
        const el = $('detail-view').getElementsByClassName(className)[0]; if (el) el.open = open;
      }
      if ($('public-alias-list').value !== alias && [...$('public-alias-list').options].some(option => option.value === alias)) $('public-alias-list').value = alias;
      const target = focused?.isConnected ? focused : selector ? document.querySelector(selector) : null;
      if (target && !target.disabled) {
        if (document.activeElement !== target) target.focus({ preventScroll: true });
        if (selection && typeof target.setSelectionRange === 'function') target.setSelectionRange(...selection);
        target.scrollTop = inputScroll[0]; target.scrollLeft = inputScroll[1];
      }
      for (const [el, top, left] of scrolls) { el.scrollTop = top; el.scrollLeft = left; }
      window.scrollTo(...windowScroll);
    };
  }
  function refresh(silent = false) {
    if (refreshFlight) return refreshFlight;
    const generation = refreshGeneration, controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 15000);
    const obsolete = () => silent && (generation !== refreshGeneration || busy || composing || document.hidden || $('merge-dialog').open || $('conflict-batch-dialog').open);
    refreshFlight = (async () => {
      const next = await api('/state', undefined, 'GET', controller.signal);
      if (obsolete() || next.revision < latestRevision) return;
      // Pending payloads and statuses can change without a public-library revision bump.
      if (loaded && JSON.stringify(next) === JSON.stringify(state)) return;
      let nextActive = active;
      if (active && !reviewDirty) {
        nextActive = next.submissions.find(item => item.id === active.id);
        if (!nextActive) {
          try { nextActive = (await api('/submissions/' + active.id, undefined, 'GET', controller.signal)).submission; }
          catch (error) { if (error.code !== 'submission_not_found') throw error; nextActive = null; }
        }
      }
      if (obsolete() || next.revision < latestRevision) return;
      const restore = preserveView();
      state = next; loaded = true;
      for (const id of selected.keys()) if (!state.submissions.some(item => item.id === id)) selected.delete(id);
      if (!publicDirty) {
        const entity = state.entities.find(entity => entity.entityId === publicId);
        if (publicId && !entity && !silent) message('正在查看的实体已被删除。', true);
        fillPublic(entity);
      }
      if (active && !reviewDirty) {
        const changed = JSON.stringify(active) !== JSON.stringify(nextActive) || reviewRevision !== state.revision;
        active = nextActive; reviewRevision = state.revision;
        if (changed) renderDetail();
      }
      $('status-text').textContent = '公共库版本 ' + state.revision + '，更新于 ' + new Date().toLocaleString();
      $('stat-pending').textContent = state.stats.pending; $('stat-conflict').textContent = state.stats.conflicts;
      $('stat-pending-alias').textContent = state.stats.pendingGameIds; $('stat-public').textContent = state.stats.entities + ' / ' + state.stats.gameIds;
      renderPending(); renderPublicList();
      if ($('conflict-batch-dialog').open) renderConflictBatch();
      controls(); restore();
    })().finally(() => { clearTimeout(timeout); refreshFlight = null; });
    return refreshFlight;
  }
  async function openSubmission(id) {
    if (reviewDirty && !confirm('放弃当前投稿尚未保存的审核修改？')) return;
    const result = await api('/submissions/' + id); active = result.submission; reviewRevision = result.revision; reviewDirty = false;
    renderDetail(); renderPending(); controls();
  }
  function renderDetail() {
    const container = $('detail-view'), scroll = container.scrollTop; container.replaceChildren();
    if (!active) { $('detail-meta').textContent = '未选择投稿'; container.append(node('p', 'empty', '暂无选中投稿')); return; }
    $('detail-meta').textContent = '#' + active.id + ' · ' + ({ pending: '待审核', approved: '已通过', rejected: '已驳回' }[active.status] || active.status);
    container.append(node('div', 'detail-summary', active.deviceId + ' · ' + new Date(active.createdAt * 1000).toLocaleString()));
    const summary = node('div', 'submission-summary');
    summary.append(node('p', '', '原始 ' + (active.rawEntityCount ?? active.entities.length) + ' · 归并后 ' + active.entities.length + ' 个实体' +
      (active.matchedEntityCount === undefined ? '' : ' · 匹配公共实体 ' + active.matchedEntityCount)));
    summary.append(node('p', '', changeSummary(active)));
    if (active.additions) summary.append(node('p', '', additionsSummary(active)));
    container.append(summary);
    if (active.reconciliations?.length) {
      const details = node('details', 'reconciliation-list'); details.append(node('summary', '', '身份归并 · ' + active.reconciliations.length));
      const reasons = { entity_redirect: '已确认的公共实体合并' };
      const rows = node('div', 'reconciliation-rows');
      for (const item of active.reconciliations) rows.append(node('p', '', item.submittedEntityId + ' → ' + item.entityId + ' · ' + (reasons[item.reason] || item.reason)));
      details.append(rows); container.append(details);
    }
    if (active.automaticGroups?.length) {
      const details = node('details', 'automatic-group-list'); details.open = true;
      details.append(node('summary', '', '审核通过后自动归并 · ' + active.automaticGroups.length + ' 组'));
      const entities = new Map([...state.entities, ...active.entities].map(entity => [entity.entityId, entity]));
      for (const ids of active.automaticGroups) details.append(node('p', '',
        ids.map(id => (entities.get(id)?.names.join(' / ') || id) + ' [' + id + ']').join(' + ')));
      container.append(details);
    }
    const status = node('div', 'review-note'); status.id = 'review-edit-state'; container.append(status);
    container.append(node('h3', 'section-title', '名称归属冲突 · ' + active.conflicts.length));
    if (!active.valid) container.append(node('div', 'conflict-row', '投稿格式无效，可驳回；不可直接通过。'));
    else if (!active.conflicts.length) container.append(node('div', 'detail-summary', '无名称归属冲突'));
    for (const conflict of active.conflicts) {
      const row = node('div', 'conflict-row', conflictText(conflict));
      for (const id of conflict.entityIds) {
        const owner = state.entities.find(entity => entity.entityId === id);
        if (owner) row.append(node('div', '', '公共库：' + owner.names.join(' / ') + ' [' + id + ']'));
      }
      container.append(row);
    }
    if (active.conflicts.length && state.entities.length > 1) {
      for (const [index, ids] of conflictGroups(active.conflicts).entries()) {
        const owners = state.entities.filter(entity => ids.includes(entity.entityId));
        if (owners.length < 2) continue;
        const merge = node('button', 'conflict-merge', '确认是同一人后合并：' + owners.map(entity => entity.names.join(' / ')).join(' + '));
        merge.id = 'btn-conflict-merge' + (index ? '-' + index : ''); merge.dataset.mergeIds = owners.map(entity => entity.entityId).sort().join(',');
        merge.onclick = () => run(() => openMerge(ids, reviewRevision)); container.append(merge);
      }
    }
    const changes = new Map((active.entityChanges || []).map(item => [item.entityId, item]));
    const unchanged = node('details', 'unchanged-entities');
    unchanged.append(node('summary', '', '无变化实体 · ' + (active.unchangedCount || 0)));
    active.entities.forEach((entity, index) => {
      const change = changes.get(entity.entityId);
      const fieldset = node('fieldset', 'review-entity'); fieldset.dataset.index = index;
      fieldset.append(node('legend', '', '选手 ' + (index + 1)));
      if (change) {
        const delta = node('div', 'entity-delta');
        delta.append(node('strong', '', { added: '新增选手', updated: '补充公共选手', unchanged: '无变化' }[change.status]));
        for (const [field, label] of fields) if (change.additions[field].length) {
          delta.append(node('p', '', label + ' +' + change.additions[field].length + '：' + change.additions[field].join(' / ')));
        }
        fieldset.append(delta);
      }
      const targetLabel = node('label'); targetLabel.append(node('span', '', '目标选手ID'));
      const id = node('input'); id.value = entity.entityId; id.dataset.field = 'entityId'; id.maxLength = 128; id.setAttribute('aria-label', '审核实体 ' + (index + 1) + ' ID'); targetLabel.append(id); fieldset.append(targetLabel);
      const ownerLabel = node('label'); ownerLabel.append(node('span', '', '关联公共选手'));
      const owner = node('select'); owner.setAttribute('aria-label', '关联公共实体 ' + (index + 1)); const placeholder = node('option', '', '保留当前目标'); placeholder.value = ''; owner.append(placeholder);
      for (const item of state.entities) { const option = node('option', '', item.names.join(' / ') + ' [' + item.entityId + ']'); option.value = item.entityId; owner.append(option); }
      owner.onchange = () => { if (owner.value) { id.value = owner.value; reviewDirty = true; controls(); } }; ownerLabel.append(owner); fieldset.append(ownerLabel);
      for (const [field, label] of fields) {
        const wrap = node('label'); wrap.append(node('span', '', label)); const input = node('textarea'); input.rows = field === 'gameIds' ? 3 : 2;
        input.value = entity[field].join('\n'); input.dataset.field = field; input.setAttribute('aria-label', '审核实体 ' + (index + 1) + ' ' + label); wrap.append(input); fieldset.append(wrap);
      }
      const remove = node('button', 'danger', '移除此选手'); remove.onclick = () => {
        if (!confirm('从待审内容中移除此选手？公共库不会被删除。')) return;
        fieldset.remove(); reviewDirty = true; controls();
      }; fieldset.append(remove);
      const hasConflict = active.conflicts.some(conflict => conflict.entityIds.includes(entity.entityId));
      (change?.status === 'unchanged' && !hasConflict ? unchanged : container).append(fieldset);
    });
    if (unchanged.querySelector('.review-entity')) container.append(unchanged);
    if (active.valid && active.status === 'pending') {
      const actions = node('div', 'review-actions'); const save = node('button', 'primary', '保存审核修改'); save.id = 'btn-review-save';
      save.onclick = () => run(async () => {
        const entities = readReview();
        const inferred = !!active.reconciliations?.length;
        if (!confirm(inferred ? '保存将确认当前显示的选手关联；原始投稿会保留，公共库仍需审核通过后更新。是否继续？' : '保存这份审核修改？公共库将在通过审核后增量更新。')) return;
        const result = await api('/submissions/' + active.id, { revision: reviewRevision, submissionRevision: active.submissionRevision, entities,
          ...(inferred ? { confirmReconciliations: true } : {}) }, 'PUT');
        active = result.submission; reviewDirty = false; reviewRevision = result.revision;
        if (selected.has(active.id)) selected.set(active.id, { submissionRevision: active.submissionRevision, revision: result.revision });
        renderDetail(); await refresh(); message('审核修改已保存。归属冲突已重新检查。');
      }); actions.append(save); container.append(actions);
    }
    container.scrollTop = scroll; controls();
  }
  function lines(value) { return value.split(/\r?\n/).map(value => value.trim()).filter(Boolean); }
  function readReview() {
    return [...$('detail-view').querySelectorAll('.review-entity')].sort((a, b) => Number(a.dataset.index) - Number(b.dataset.index)).map(row => {
      const entity = { entityId: row.querySelector('[data-field="entityId"]').value.trim() };
      for (const [field] of fields) entity[field] = lines(row.querySelector('[data-field="' + field + '"]').value);
      return entity;
    });
  }
  function publicPayload() {
    return { ...(publicId ? { entityId: publicId } : {}), names: lines($('public-names').value), gameIds: lines($('public-game-ids').value) };
  }
  async function writePublic() {
    if (publicId && !confirm('保存此选手的名称和ID？移除ID只解除此选手的关联，不影响其他选手。达到5个重叠游戏ID的选手将自动归并。')) return;
    const result = await api(publicId ? '/entities/' + encodeURIComponent(publicId) : '/entities', { revision: publicRevision, entity: publicPayload() }, publicId ? 'PUT' : 'POST');
    publicId = result.entity.entityId; publicDirty = false; await refresh(); message('公共库已保存。');
  }
  function currentConflictGroups() {
    return Array.isArray(state.conflictResolutionGroups) ? state.conflictResolutionGroups : [];
  }
  function publicEntityLabel(entityId) {
    const entity = state.entities.find(item => item.entityId === entityId);
    return entity ? entity.names.join(' / ') + ' [' + entity.entityId + ']' : entityId;
  }
  function appendConflictBatchLine(container, label, value) {
    const line = node('p', 'conflict-batch-line');
    line.append(node('strong', '', label + '：'), document.createTextNode(value || '无'));
    container.append(line);
  }
  function rememberConflictBatchRow(row) {
    const check = row.querySelector('input[type="checkbox"]'), select = row.querySelector('select');
    conflictBatchSelections.set(row.dataset.token, {
      checked: check.checked,
      targetEntityId: select ? select.value : row.dataset.targetEntityId || '',
    });
  }
  function resolutionChoices() {
    const authorized = new Map(currentConflictGroups().map(group => [group.token, group]));
    const choices = [];
    for (const row of $('conflict-batch-list').querySelectorAll('.conflict-batch-row')) {
      const check = row.querySelector('input[type="checkbox"]');
      if (!check.checked) continue;
      const group = authorized.get(row.dataset.token);
      if (!group || group.kind === 'ambiguous') continue;
      const targetEntityId = group.kind === 'unique_name_target' ? row.dataset.targetEntityId : row.querySelector('select')?.value;
      const authorizedTarget = group.kind === 'unique_name_target'
        ? targetEntityId === group.suggestedTargetEntityId
        : group.publicEntityIds.includes(targetEntityId);
      if (authorizedTarget) choices.push({ token: group.token, targetEntityId });
    }
    return choices;
  }
  function conflictBatchPreview(choices = resolutionChoices()) {
    const groups = new Map(currentConflictGroups().map(group => [group.token, group]));
    const submissionIds = new Set(), redirectedIds = new Set(), mergedPublicIds = new Set();
    const existingRedirects = new Set((state.entityRedirects || []).map(redirect => redirect.fromEntityId + '\0' + redirect.toEntityId));
    let sourceCount = 0;
    for (const choice of choices) {
      const group = groups.get(choice.token);
      if (!group) continue;
      sourceCount += group.sources.length;
      for (const source of group.sources) submissionIds.add(source.submissionId);
      for (const entityId of group.publicEntityIds) if (entityId !== choice.targetEntityId) mergedPublicIds.add(entityId);
      for (const entityId of [...group.publicEntityIds, ...group.sources.map(source => source.entityId)]) {
        if (entityId !== choice.targetEntityId && !existingRedirects.has(entityId + '\0' + choice.targetEntityId)) redirectedIds.add(entityId);
      }
    }
    return {
      selectedGroupCount: choices.length,
      submissionCount: submissionIds.size,
      sourceCount,
      mergedPublicEntityCount: mergedPublicIds.size,
      redirectCount: redirectedIds.size,
    };
  }
  function renderConflictBatchSummary() {
    const groups = currentConflictGroups();
    const provided = state.conflictResolutionStats || {};
    const uniqueCount = Number(provided.uniqueNameTargets) || groups.filter(group => group.kind === 'unique_name_target').length;
    const publicMergeCount = Number(provided.publicMerges) || groups.filter(group => group.kind === 'public_merge').length;
    const ambiguousCount = Number(provided.ambiguous) || groups.filter(group => group.kind === 'ambiguous').length;
    const preview = conflictBatchPreview();
    $('conflict-batch-summary').textContent = '按名称可合并 ' + uniqueCount + ' · 已跳过 ' + (publicMergeCount + ambiguousCount) +
      '（公共实体合并 ' + publicMergeCount + '，需要人工整理 ' + ambiguousCount + '）。当前选择 ' + preview.selectedGroupCount +
      ' 组 · 关联待审投稿 ' + preview.submissionCount + ' 条 / 实体 ' + preview.sourceCount + ' 个 · 合并公共实体 ' +
      preview.mergedPublicEntityCount + ' 个 · 预计重定向 ' + preview.redirectCount + ' 条。';
  }
  function renderConflictBatch() {
    const list = $('conflict-batch-list'), scroll = list.scrollTop;
    list.replaceChildren();
    const kindLabels = { unique_name_target: '唯一名称目标', public_merge: '公共实体合并', ambiguous: '无法自动处理' };
    for (const group of currentConflictGroups()) {
      const row = node('div', 'conflict-batch-row');
      row.setAttribute('role', 'listitem');
      row.dataset.token = group.token; row.dataset.kind = group.kind;
      const defaultTarget = group.kind === 'unique_name_target' ? group.suggestedTargetEntityId || '' : '';
      const saved = conflictBatchSelections.get(group.token) || { checked: false, targetEntityId: defaultTarget };
      const targetEntityId = group.kind === 'unique_name_target' ? defaultTarget :
        group.publicEntityIds.includes(saved.targetEntityId) ? saved.targetEntityId : '';
      row.dataset.targetEntityId = targetEntityId;

      const checkLabel = node('label', 'conflict-batch-check');
      const check = node('input'); check.type = 'checkbox'; check.dataset.role = 'conflict-choice';
      check.checked = group.kind !== 'ambiguous' && !!saved.checked &&
        (group.kind !== 'public_merge' || !!targetEntityId);
      check.setAttribute('aria-label', '选择冲突组 ' + (group.conflictNames.join(' / ') || group.token));
      check.onchange = () => { rememberConflictBatchRow(row); renderConflictBatchSummary(); controls(); };
      checkLabel.append(check);

      const copy = node('div', 'conflict-batch-copy');
      copy.append(node('span', 'conflict-batch-kind', kindLabels[group.kind] || group.kind));
      appendConflictBatchLine(copy, '冲突名称', group.conflictNames.join(' / '));
      appendConflictBatchLine(copy, '来源ID', group.sources.map(source => source.entityId + '（投稿 #' + source.submissionId + '）').join(' / '));
      const sourceSubmissionIds = new Set(group.sources.map(source => source.submissionId));
      const devices = [...new Set(state.submissions.filter(submission => sourceSubmissionIds.has(submission.id)).map(submission => submission.deviceId))];
      appendConflictBatchLine(copy, '来源摘要', '待审投稿 ' + sourceSubmissionIds.size + ' 条 · 来源实体 ' + group.sources.length + ' 个' +
        (devices.length ? ' · 设备 ' + devices.length + ' 个：' + devices.join(' / ') : ''));
      const members = [...group.publicEntityIds.map(entityId => state.entities.find(entity => entity.entityId === entityId)).filter(Boolean), ...group.sources];
      for (const [field, label] of fields) {
        const values = new Set();
        for (const member of members) for (const value of member[field]) values.add(normalize(value));
        appendConflictBatchLine(copy, '合并后' + label, values.size + ' 项');
      }

      let target;
      if (group.kind === 'unique_name_target') {
        target = node('div', 'conflict-batch-target'); target.append(node('span', '', '保留公共选手'));
        target.append(node('div', 'conflict-batch-fixed', publicEntityLabel(targetEntityId)));
      } else if (group.kind === 'public_merge') {
        target = node('label', 'conflict-batch-target'); target.append(node('span', '', '保留公共选手'));
        const select = node('select'); select.setAttribute('aria-label', '冲突组保留公共选手 ' + (group.conflictNames.join(' / ') || group.token));
        const placeholder = node('option', '', '请选择保留选手'); placeholder.value = ''; select.append(placeholder);
        for (const entityId of group.publicEntityIds) {
          const option = node('option', '', publicEntityLabel(entityId)); option.value = entityId; select.append(option);
        }
        select.value = targetEntityId;
        select.onchange = () => {
          row.dataset.targetEntityId = select.value;
          if (!select.value) check.checked = false;
          rememberConflictBatchRow(row); renderConflictBatchSummary(); controls();
        };
        target.append(select);
      } else {
        target = node('div', 'conflict-batch-target'); target.append(node('span', '', '处理说明'));
        const publicLabels = group.publicEntityIds.map(publicEntityLabel);
        target.append(node('div', 'conflict-batch-reason', publicLabels.length
          ? '目标不唯一或冲突链不完整：' + publicLabels.join(' / ') + '。需要人工整理后再确认。'
          : '没有唯一公共目标，需要人工整理后再确认。'));
      }
      row.append(checkLabel, copy, target); list.append(row);
      rememberConflictBatchRow(row);
    }
    if (!list.children.length) list.append(node('p', 'empty', '当前没有可预览的冲突组。'));
    list.scrollTop = scroll;
    renderConflictBatchSummary(); controls();
  }
  function openConflictBatch() {
    if (publicDirty || reviewDirty) throw new Error('有未保存的修改，请先保存或重新载入后再批量处理冲突。');
    const groups = currentConflictGroups();
    if (!groups.length) throw new Error('当前没有可处理的冲突组。');
    conflictBatchRevision = state.revision; conflictBatchSelections.clear();
    for (const group of groups) conflictBatchSelections.set(group.token, {
      checked: group.kind === 'unique_name_target',
      targetEntityId: group.kind === 'unique_name_target' ? group.suggestedTargetEntityId || '' : '',
    });
    $('conflict-batch-error').hidden = true; $('conflict-batch-error').textContent = '';
    renderConflictBatch(); $('conflict-batch-dialog').showModal(); controls();
  }
  function mergeIds() { return [...$('merge-options').querySelectorAll('input:checked')].map(input => input.value); }
  function conflictGroups(conflicts) {
    const neighbors = new Map();
    for (const conflict of conflicts) for (const id of conflict.entityIds) {
      const edges = neighbors.get(id) || new Set();
      // A star connects the entire ownership collision without quadratic edges.
      edges.add(conflict.entityIds[0]); neighbors.set(id, edges);
      const first = neighbors.get(conflict.entityIds[0]) || new Set(); first.add(id); neighbors.set(conflict.entityIds[0], first);
    }
    const seen = new Set(), groups = [];
    for (const id of neighbors.keys()) {
      if (seen.has(id)) continue;
      const group = [], pending = [id]; seen.add(id);
      while (pending.length) {
        const current = pending.pop(); group.push(current);
        for (const next of neighbors.get(current)) if (!seen.has(next)) { seen.add(next); pending.push(next); }
      }
      groups.push(group);
    }
    return groups;
  }
  function renderMergePreview() {
    const ids = new Set(mergeIds()), target = $('merge-target').value;
    const entities = state.entities.filter(entity => ids.has(entity.entityId));
    $('merge-target').replaceChildren();
    for (const entity of entities) {
      const option = node('option', '', entity.names.join(' / ') + ' [' + entity.entityId + ']'); option.value = entity.entityId; $('merge-target').append(option);
    }
    if (ids.has(target)) $('merge-target').value = target;
    const targetEntityId = $('merge-target').value;
    const ordered = [...entities.filter(entity => entity.entityId === targetEntityId), ...entities.filter(entity => entity.entityId !== targetEntityId)];
    const preview = $('merge-preview'); preview.replaceChildren();
    preview.append(node('p', '', '公共库版本 ' + mergeRevision + ' · ' + entities.length + ' 位选手'));
    preview.append(node('p', '', '保留ID：' + (targetEntityId || '未选择')));
    preview.append(node('p', '', '停用ID：' + entities.filter(entity => entity.entityId !== targetEntityId).map(entity => entity.entityId).join(' / ')));
    for (const [field, label] of fields) {
      const union = new Map();
      for (const entity of ordered) for (const value of entity[field]) if (!union.has(normalize(value))) union.set(normalize(value), value);
      preview.append(node('p', '', label + ' (' + union.size + ')：' + [...union.values()].join(' / ')));
    }
    controls();
  }
  function openMerge(ids, revision = state.revision) {
    if (publicDirty || reviewDirty) throw new Error('有未保存的修改，请先保存或重新载入后再合并公共选手。');
    mergeRevision = revision; const selectedIds = new Set(ids);
    $('merge-error').hidden = true; $('merge-error').textContent = '';
    $('merge-options').replaceChildren(); $('merge-target').replaceChildren();
    for (const entity of state.entities) {
      const label = node('label'); const input = node('input'); input.type = 'checkbox'; input.value = entity.entityId;
      input.checked = selectedIds.has(entity.entityId); input.setAttribute('aria-label', '合并 ' + entity.names.join(' / ') + ' [' + entity.entityId + ']');
      input.onchange = renderMergePreview;
      label.append(input, node('span', '', entity.names.join(' / ') + ' [' + entity.entityId + ']')); $('merge-options').append(label);
    }
    renderMergePreview(); $('merge-dialog').showModal();
  }
  async function review(action, allClean = false) {
    const refs = allClean ? state.submissions.filter(isAddition).map(item => ({ id: item.id, submissionRevision: item.submissionRevision }))
      : selected.size ? [...selected].map(([id, guard]) => ({ id, submissionRevision: guard.submissionRevision })) : active ? [{ id: active.id, submissionRevision: active.submissionRevision }] : [];
    if (!refs.length) return;
    if (refs.length > 100) throw new Error('单次最多审核100条，请缩小选择范围。');
    if (reviewDirty && refs.some(ref => ref.id === active?.id) && action === 'approve') throw new Error('当前投稿有未保存的审核修改，请先保存。');
    if (!confirm((action === 'approve' ? '审核通过' : '驳回') + '这 ' + refs.length + ' 条投稿？' + (action === 'approve' ? '名称冲突将按整位选手跳过并保留待审核，原始投稿留存。游戏ID允许共用；重叠游戏ID达到5个才自动归并。' : '此操作不可撤销。'))) return;
    // Any stale selection must reject the whole batch, including newer selections.
    const revision = allClean ? state.revision : selected.size ? Math.min(...[...selected.values()].map(guard => guard.revision)) : reviewRevision;
    const result = await api('/review', { revision, action, submissions: refs, ...(action === 'approve' ? { skipConflicts: true } : {}) });
    for (const ref of refs) selected.delete(ref.id);
    if (refs.some(ref => ref.id === active?.id)) reviewDirty = false;
    await refresh(); message(action === 'approve' ? '审核完成：接受 ' + result.acceptedEntityCount + ' 位选手，跳过 ' + result.skippedEntityCount + ' 位选手，待审核 ' + result.pendingSubmissionCount + ' 条。' : '已驳回 ' + refs.length + ' 条投稿。');
  }
  $('btn-resolve-by-name').onclick = () => run(openConflictBatch);
  $('btn-conflict-batch-safe-all').onclick = () => {
    const authorized = new Map(currentConflictGroups().map(group => [group.token, group]));
    for (const row of $('conflict-batch-list').querySelectorAll('.conflict-batch-row')) {
      const group = authorized.get(row.dataset.token), check = row.querySelector('input[type="checkbox"]'), select = row.querySelector('select');
      check.checked = group?.kind === 'unique_name_target' ||
        (group?.kind === 'public_merge' && group.publicEntityIds.includes(select?.value));
      rememberConflictBatchRow(row);
    }
    renderConflictBatchSummary(); controls();
  };
  $('btn-conflict-batch-clear').onclick = () => {
    for (const row of $('conflict-batch-list').querySelectorAll('.conflict-batch-row')) {
      row.querySelector('input[type="checkbox"]').checked = false; rememberConflictBatchRow(row);
    }
    renderConflictBatchSummary(); controls();
  };
  $('btn-conflict-batch-cancel').onclick = () => $('conflict-batch-dialog').close();
  $('conflict-batch-dialog').addEventListener('cancel', event => { if (busy) event.preventDefault(); });
  $('btn-conflict-batch-confirm').onclick = () => {
    const choices = resolutionChoices();
    if (!choices.length) return;
    run(async () => {
      const preview = conflictBatchPreview(choices);
      if (!confirm('确认处理 ' + preview.selectedGroupCount + ' 组冲突？关联待审投稿 ' + preview.submissionCount + ' 条 / 实体 ' +
        preview.sourceCount + ' 个；合并公共实体 ' + preview.mergedPublicEntityCount + ' 个；预计重定向 ' + preview.redirectCount +
        ' 条。全部名称和游戏ID将并入所选保留实体。')) return;
      let result;
      try {
        result = await api('/conflicts/resolve', { revision: conflictBatchRevision, groups: choices, confirm: true });
      } catch (error) {
        if (error.code !== 'stale_conflict_group') throw error;
        const previousSelections = new Map(conflictBatchSelections);
        conflictBatchSelections.clear();
        try { await refresh(); }
        catch (refreshError) {
          conflictBatchSelections.clear();
          for (const [token, selection] of previousSelections) conflictBatchSelections.set(token, selection);
          renderConflictBatch(); throw refreshError;
        }
        conflictBatchRevision = state.revision; conflictBatchSelections.clear();
        for (const group of currentConflictGroups()) conflictBatchSelections.set(group.token, {
          checked: false,
          targetEntityId: group.kind === 'unique_name_target' ? group.suggestedTargetEntityId || '' : '',
        });
        renderConflictBatch(); message(errors.stale_conflict_group, true); return;
      }
      $('conflict-batch-dialog').close();
      await refresh();
      message('冲突合并完成：已接纳 ' + result.associatedEntityCount + ' 个待审实体，剩余待审核 ' + result.pendingSubmissionCount +
        ' 条，公共库版本 ' + result.revision + '。');
    });
  };
  $('btn-public-merge').onclick = () => run(() => openMerge(publicId ? [publicId] : []));
  $('merge-target').onchange = renderMergePreview;
  $('btn-merge-cancel').onclick = () => $('merge-dialog').close();
  $('merge-dialog').addEventListener('cancel', event => { if (busy) event.preventDefault(); });
  $('btn-merge-confirm').onclick = () => run(async () => {
    const entityIds = mergeIds(), targetEntityId = $('merge-target').value;
    if (!confirm('确认将 ' + entityIds.join(' / ') + ' 合并为 ' + targetEntityId + '？全部名称和游戏ID将保留，其他选手ID永久停用。')) return;
    const result = await api('/entities/merge', { revision: mergeRevision, targetEntityId, entityIds, confirm: true });
    $('merge-dialog').close(); publicId = result.entity.entityId;
    await refresh(); message('公共选手已合并。待审核内容已重新检查。');
  });
  $('btn-refresh').onclick = () => run(refresh);
  $('pending-search').oninput = $('conflict-filter').onchange = () => { renderPending(); controls(); };
  $('public-search').oninput = $('btn-public-search').onclick = renderPublicList;
  $('check-all').onchange = () => { ++refreshGeneration; for (const item of visiblePending()) if ($('check-all').checked) selected.set(item.id, { submissionRevision: item.submissionRevision, revision: state.revision }); else selected.delete(item.id); renderPending(); controls(); };
  $('btn-clear-selected').onclick = () => { ++refreshGeneration; selected.clear(); renderPending(); controls(); };
  $('public-main-list').onchange = () => {
    const id = $('public-main-list').value;
    if (id === publicId) return;
    if (publicDirty && !confirm('放弃公共库中尚未保存的修改？')) { $('public-main-list').value = publicId || ''; return; }
    ++refreshGeneration;
    fillPublic(state.entities.find(entity => entity.entityId === id));
  };
  $('btn-public-new').onclick = () => { if (publicDirty && !confirm('放弃尚未保存的公共库修改？')) return; ++refreshGeneration; fillPublic(null); $('public-names').focus(); };
  $('btn-public-reload').onclick = () => run(async () => {
    if (publicDirty && !confirm('放弃草稿并载入公共库最新内容？')) return;
    const result = await api('/entities/' + encodeURIComponent(publicId));
    publicDirty = false; await refresh(); fillPublic(result.entity); publicRevision = result.revision;
  });
  $('btn-detail-reload').onclick = () => run(() => openSubmission(active.id));
  $('public-editor').addEventListener('input', event => { ++refreshGeneration; if (event.target.id !== 'public-import-text') { publicDirty = true; controls(); } });
  $('detail-view').addEventListener('input', () => { ++refreshGeneration; reviewDirty = true; controls(); });
  $('btn-public-add').onclick = $('btn-public-save').onclick = () => run(writePublic);
  $('btn-public-delete').onclick = () => run(async () => {
    if (!confirm('永久删除此公共实体及其全部名称和游戏ID？此操作不可撤销。')) return;
    await api('/entities/' + encodeURIComponent(publicId), { revision: publicRevision }, 'DELETE');
    publicDirty = false; publicId = null; await refresh(); message('公共实体已删除。');
  });
  $('btn-public-import').onclick = () => run(async () => {
    const text = $('public-import-text').value;
    if (new TextEncoder().encode(text).length > 262144) throw new Error(errors.library_too_large);
    const revision = state.revision; const result = await api('/import/preview', { revision, text });
    $('import-result').textContent = result.entities.length + ' 个实体；' + result.conflicts.length + ' 项归属冲突。' + result.conflicts.map(conflictText).join('；');
    if (result.conflicts.length) throw new Error('导入存在归属冲突，请修改导入内容后重试。公共库未改变。');
    if (!confirm('增量导入 ' + result.entities.length + ' 个实体？')) return;
    await api('/import', { revision, text }); await refresh(); $('import-result').textContent = '已导入 ' + result.entities.length + ' 个实体。'; message('导入完成。');
  });
  $('btn-approve').onclick = () => run(() => review('approve'));
  $('btn-approve-added').onclick = () => run(() => review('approve', true));
  $('btn-reject').onclick = () => run(() => review('reject'));
  window.addEventListener('beforeunload', event => { if (publicDirty || reviewDirty) { event.preventDefault(); event.returnValue = ''; } });
  window.addEventListener('focus', autoRefresh);
  document.addEventListener('visibilitychange', () => {
    ++refreshGeneration;
    if (document.hidden) { clearTimeout(refreshTimer); refreshTimer = null; }
    else autoRefresh();
  });
  document.addEventListener('compositionstart', () => { composing = true; ++refreshGeneration; clearTimeout(refreshTimer); refreshTimer = null; });
  document.addEventListener('compositionend', () => { composing = false; autoRefresh(); });
  $('merge-dialog').addEventListener('close', autoRefresh);
  $('conflict-batch-dialog').addEventListener('close', autoRefresh);
  run(refresh);
})();
`;
