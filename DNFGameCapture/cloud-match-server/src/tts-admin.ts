// 后台「语音生成额度」：/admin/tts 页面 + /admin/api/tts 接口。
import express, { type NextFunction, type Request, type Response } from 'express';
import { ADMIN_PAGE_CSS, escapeAdminAttribute } from './admin-page.js';
import { TtsQuotaError, type TtsGenerator } from './tts-generate.js';

function idOf(value: unknown): number {
  if (typeof value !== 'string' || !/^[1-9][0-9]*$/.test(value) || !Number.isSafeInteger(Number(value))) {
    throw new TtsQuotaError(400, 'invalid_request');
  }
  return Number(value);
}

export function createTtsAdminApi(tts: TtsGenerator) {
  const router = express.Router();
  router.use(express.json({ limit: '16kb' }));
  router.get('/', (_request, response) => response.json(tts.adminState()));
  router.put('/settings', (request, response) => response.json({ ok: true, settings: tts.updateSettings(request.body) }));
  router.put('/licenses/:id', (request, response) => response.json({ ok: true, quota: tts.setLicenseQuota(idOf(request.params.id), request.body) }));
  router.post('/licenses/:id/bonus', (request, response) => response.json({ ok: true, quota: tts.addBonus(idOf(request.params.id), request.body) }));
  router.post('/licenses/:id/reset', (request, response) => response.json({ ok: true, usage: tts.resetUsage(idOf(request.params.id), request.body) }));
  router.get('/licenses/:id/audit', (request, response) => response.json({ ok: true, events: tts.quotaAudit(idOf(request.params.id)) }));
  router.get('/audit', (_request, response) => response.json({ ok: true, events: tts.quotaAudit(null) }));
  router.use((error: unknown, _request: Request, response: Response, _next: NextFunction) => {
    if (error instanceof TtsQuotaError) response.status(error.status).json({ ok: false, code: error.code });
    else response.status(500).json({ ok: false, code: 'internal_error' });
  });
  return router;
}

export function buildTtsAdminPage(csrfToken: string): string {
  return `<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="dnf-admin-csrf" content="${escapeAdminAttribute(csrfToken)}"><title>语音生成额度</title>
<link rel="stylesheet" href="/admin/tts/style.css"></head><body class="tts-admin">
<header class="topbar"><div><h1>语音生成额度</h1><a href="/admin">管理首页</a><a href="/admin/voices">声音管理</a></div><div><span id="refresh-status" class="muted" role="status">正在读取</span><button id="refresh" type="button">刷新</button></div></header>
<main class="license-workspace"><div id="message" class="message" role="status" hidden></div>
<section aria-labelledby="global-heading"><h2 id="global-heading">全局</h2>
<div class="tts-cards">
<div class="tts-card"><span class="muted">今日预估花费</span><strong id="today-cost">-</strong><div class="tts-bar"><i id="today-bar"></i></div><small id="today-chars" class="muted"></small></div>
<div class="tts-card"><span class="muted">全局缓存</span><strong id="cache-info">-</strong><small class="muted">同一音色同一句话只花一次钱，之后所有主播免费复用</small></div>
<div class="tts-card"><span class="muted">火山引擎密钥</span><strong id="configured">-</strong><small class="muted">服务器环境变量 DOUBAO_TTS_API_KEY / DOUBAO_TTS_APP_KEY</small></div>
</div>
<form id="settings-form" class="tts-settings">
<label class="tts-check"><input id="set-enabled" type="checkbox"> 允许主播生成语音（总开关）</label>
<label>每日总预算（元，最多 5 元）<input id="set-budget" type="number" min="0" max="5" step="0.1" required></label>
<label>每个卡密默认每日字数<input id="set-daily" type="number" min="0" max="1000000" step="1" required></label>
<label>每个卡密默认每月字数<input id="set-monthly" type="number" min="0" max="10000000" step="1" required></label>
<button class="primary" type="submit">保存全局设置</button>
</form>
<p class="muted tts-note">单句最多 <span id="max-chars">30</span> 字，每个卡密每分钟最多真正合成 <span id="per-minute">4</span> 句；缓存命中不扣额度。额外额度在日 / 月额度用完后才扣，但任何情况下都不会突破每日总预算。</p>
<details><summary>最近 14 天</summary><div class="table-scroll"><table><thead><tr><th>日期</th><th>合成字数</th><th>预估花费</th><th>合成次数</th></tr></thead><tbody id="recent-body"></tbody></table></div></details>
</section>
<section aria-labelledby="licenses-heading"><div class="license-heading"><h2 id="licenses-heading">卡密 / 主播额度 <span id="license-count" class="muted">0</span></h2>
<div class="tts-filters"><label class="tts-check"><input id="only-used" type="checkbox"> 只看本月用过的 / 单独设置过的</label><label>搜索<input id="search" type="search" placeholder="主播 / 备注 / 设备 / 编号"></label></div></div>
<div class="table-scroll" tabindex="0" role="region" aria-label="卡密额度列表"><table><thead><tr><th>编号 / 备注</th><th>主播 / 设备</th><th>今日</th><th>本月</th><th>额外额度</th><th>状态</th><th>管理</th></tr></thead><tbody id="license-body"></tbody></table></div>
<p id="license-empty" class="empty" hidden>暂无匹配卡密</p></section>
</main>
<dialog id="dlg"><form id="dlg-form">
<h2 id="dlg-title"></h2><p id="dlg-sub" class="muted"></p>
<div id="dlg-error" class="message error" role="alert" hidden></div>
<div id="dlg-quota" hidden>
<label><span>每日字数（留空 = 跟随全局默认 <b class="dflt-day"></b>）</span><input id="q-daily" type="number" min="0" max="1000000" step="1"></label>
<label><span>每月字数（留空 = 跟随全局默认 <b class="dflt-month"></b>）</span><input id="q-monthly" type="number" min="0" max="10000000" step="1"></label>
<label class="tts-check"><input id="q-disabled" type="checkbox"> 禁止该卡密生成语音（缓存里的也不能用）</label>
<label>备注<input id="q-note" maxlength="200" autocomplete="off"></label>
</div>
<div id="dlg-bonus" hidden>
<div class="tts-quick"><button type="button" data-add="500">+500</button><button type="button" data-add="2000">+2000</button><button type="button" data-add="10000">+10000</button></div>
<label>增加额外额度（字，负数 = 扣减）<input id="b-delta" type="number" min="-10000000" max="10000000" step="1"></label>
<label>备注（例如：赞助 / 活动补偿）<input id="b-note" maxlength="200" autocomplete="off"></label>
</div>
<div id="dlg-audit" hidden></div>
<div class="license-dialog-footer"><button id="dlg-cancel" type="button">关闭</button><button id="dlg-submit" class="primary" type="submit">确认</button></div>
</form></dialog>
<script src="/admin/tts/app.js" defer></script></body></html>`;
}

export const TTS_ADMIN_CSS = ADMIN_PAGE_CSS + String.raw`
.tts-admin .license-workspace{max-width:1500px}
.tts-admin section{margin-bottom:28px}
.tts-admin h2{margin-bottom:14px}
.tts-cards{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px;margin-bottom:18px}
.tts-card{display:grid;gap:6px;padding:14px 16px;background:var(--surface);border:1px solid var(--line);border-radius:6px}
.tts-card strong{font-size:20px}
.tts-bar{height:6px;background:var(--raised);border-radius:3px;overflow:hidden}.tts-bar i{display:block;height:100%;width:0;background:var(--green)}
.tts-bar i.warn{background:var(--gold)}.tts-bar i.full{background:var(--danger)}
.tts-settings{display:grid;grid-template-columns:minmax(200px,1.2fr) repeat(3,minmax(140px,1fr)) auto;gap:14px;align-items:end}
.tts-check{display:flex;align-items:center;gap:8px;color:var(--text);font-size:14px}.tts-check input{width:auto;min-height:0}
.tts-note{margin:12px 0}
.tts-filters{display:flex;gap:16px;align-items:end;flex-wrap:wrap}.tts-filters label:last-child{width:260px}
.tts-admin td:last-child{width:auto;min-width:330px}.tts-admin td .actions{display:flex;gap:6px;flex-wrap:nowrap}.tts-admin td .actions button{padding:5px 9px;min-height:30px;white-space:nowrap}
.tts-admin .over{color:var(--danger)}.tts-admin .custom{color:var(--gold)}.tts-admin .ok{color:var(--green)}
.tts-admin dialog{width:min(520px,94vw);background:var(--surface);color:var(--text);border:1px solid var(--line);border-radius:8px;padding:22px}
.tts-admin dialog::backdrop{background:rgba(0,0,0,.6)}
.tts-admin dialog form>div,.tts-admin dialog form>label{margin-top:12px}
.tts-admin #dlg-quota,.tts-admin #dlg-bonus{display:grid;gap:12px}
.tts-quick{display:flex;gap:8px}
.tts-admin #dlg-audit{max-height:300px;overflow:auto;font-size:13px}
.tts-admin #dlg-audit div{padding:6px 0;border-bottom:1px solid var(--line)}
.license-dialog-footer{display:flex;justify-content:flex-end;gap:10px;margin-top:18px}
@media(max-width:900px){.tts-cards{grid-template-columns:1fr}.tts-settings{grid-template-columns:1fr 1fr}}
`;

export const TTS_ADMIN_JS = String.raw`'use strict';
(() => {
  const csrf = document.querySelector('meta[name="dnf-admin-csrf"]').content;
  const $ = id => document.getElementById(id);
  const MESSAGES = {
    invalid_csrf: '安全令牌已失效，请重新打开页面。', invalid_request: '输入无效，请检查。', license_not_found: '卡密不存在。',
    internal_error: '服务器内部错误。'
  };
  let state = null, current = null, mode = '';
  const esc = v => String(v ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
  const fmt = n => Number(n || 0).toLocaleString('zh-CN');
  const time = s => s ? new Date(s * 1000).toLocaleString('zh-CN', { hour12: false }) : '';
  function message(text, error) { const el = $('message'); el.hidden = !text; el.textContent = text || ''; el.classList.toggle('error', !!error); }
  async function api(path, method = 'GET', body) {
    const response = await fetch('/admin/api/tts' + path, { method, credentials: 'same-origin',
      headers: { 'content-type': 'application/json', 'x-dnf-admin-csrf': csrf }, body: body === undefined ? undefined : JSON.stringify(body) });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) throw new Error(MESSAGES[data.code] || ('操作失败：' + (data.code || response.status)));
    return data;
  }
  async function load() {
    $('refresh-status').textContent = '正在读取';
    try { state = await api(''); render(); $('refresh-status').textContent = '已更新 ' + new Date().toLocaleTimeString('zh-CN', { hour12: false }); }
    catch (err) { $('refresh-status').textContent = '读取失败'; message(err.message, true); }
  }
  function render() {
    const s = state.settings, t = state.today;
    $('today-cost').textContent = t.costYuan.toFixed(3) + ' / ' + t.budgetYuan.toFixed(2) + ' 元';
    const pct = t.budgetYuan > 0 ? Math.min(100, t.costYuan / t.budgetYuan * 100) : 100;
    const bar = $('today-bar'); bar.style.width = pct + '%'; bar.className = pct >= 100 ? 'full' : pct >= 80 ? 'warn' : '';
    $('today-chars').textContent = state.day + '（北京时间）· 已合成 ' + fmt(t.chars) + ' 字';
    $('cache-info').textContent = fmt(state.cache.entries) + ' 句 · ' + state.cache.megabytes + ' MB · 复用 ' + fmt(state.cache.hits) + ' 次';
    $('configured').textContent = state.configured ? '已配置' : '未配置（无法生成）';
    $('configured').className = state.configured ? 'ok' : 'over';
    $('set-budget').max = state.hardDailyBudgetYuan;
    if (document.activeElement?.closest('#settings-form') == null) {
      $('set-enabled').checked = s.enabled; $('set-budget').value = s.dailyBudgetYuan;
      $('set-daily').value = s.licenseDailyChars; $('set-monthly').value = s.licenseMonthlyChars;
    }
    $('max-chars').textContent = s.maxTextChars; $('per-minute').textContent = s.licenseGeneratePerMinute;
    $('recent-body').innerHTML = state.recent.map(r => '<tr><td>' + esc(r.day) + '</td><td>' + fmt(r.chars) + '</td><td>' + r.costYuan.toFixed(3) + ' 元</td><td>' + fmt(r.requests) + '</td></tr>').join('')
      || '<tr><td colspan="4" class="muted">还没有生成记录</td></tr>';
    renderLicenses();
  }
  function limitCell(used, bonusUsed, limit, custom) {
    const cls = used >= limit ? 'over' : custom ? 'custom' : '';
    return '<span class="' + cls + '">' + fmt(used) + ' / ' + fmt(limit) + (custom ? '（单独）' : '') + '</span>' + (bonusUsed ? '<br><small class="muted">额外 ' + fmt(bonusUsed) + '</small>' : '');
  }
  function renderLicenses() {
    const q = $('search').value.trim().toLowerCase(), onlyUsed = $('only-used').checked, s = state.settings;
    const rows = state.licenses.filter(l => {
      if (onlyUsed && !(l.monthUsed || l.monthBonusUsed || l.dailyLimit != null || l.monthlyLimit != null || l.bonusChars || l.ttsDisabled)) return false;
      return !q || [l.id, l.label, l.broadcasterName, l.boundDeviceId, l.note].some(v => String(v ?? '').toLowerCase().includes(q));
    });
    $('license-count').textContent = rows.length;
    $('license-empty').hidden = rows.length > 0;
    $('license-body').innerHTML = rows.slice(0, 500).map(l => {
      const day = l.dailyLimit ?? s.licenseDailyChars, month = l.monthlyLimit ?? s.licenseMonthlyChars;
      const status = l.ttsDisabled ? '<span class="over">已禁止生成</span>' : l.disabledAt ? '<span class="over">卡密已停用</span>' : '<span class="ok">正常</span>';
      return '<tr data-id="' + l.id + '"><td>#' + l.id + (l.label ? ' ' + esc(l.label) : '') + (l.note ? '<br><small class="muted">' + esc(l.note) + '</small>' : '') + '</td>'
        + '<td>' + (l.broadcasterName ? esc(l.broadcasterName) : '<span class="muted">-</span>') + '<br><small class="muted">' + esc(l.boundDeviceId || '未绑定') + '</small></td>'
        + '<td>' + limitCell(l.dayUsed, l.dayBonusUsed, day, l.dailyLimit != null) + '</td>'
        + '<td>' + limitCell(l.monthUsed, l.monthBonusUsed, month, l.monthlyLimit != null) + '</td>'
        + '<td>' + (l.bonusChars ? '<span class="custom">' + fmt(l.bonusChars) + '</span>' : '<span class="muted">0</span>') + '</td>'
        + '<td>' + status + '</td>'
        + '<td><div class="actions"><button type="button" data-act="bonus">加额度</button><button type="button" data-act="quota">设置额度</button>'
        + '<button type="button" data-act="reset-day">清零今日</button><button type="button" data-act="audit">记录</button></div></td></tr>';
    }).join('');
  }
  function openDialog(kind, license) {
    mode = kind; current = license;
    $('dlg-error').hidden = true;
    $('dlg-quota').hidden = kind !== 'quota'; $('dlg-bonus').hidden = kind !== 'bonus'; $('dlg-audit').hidden = kind !== 'audit';
    $('dlg-submit').hidden = kind === 'audit';
    const who = '#' + license.id + (license.label ? ' ' + license.label : '') + (license.broadcasterName ? ' · 主播 ' + license.broadcasterName : '');
    $('dlg-title').textContent = { quota: '设置额度', bonus: '增加额外额度', audit: '额度记录' }[kind];
    $('dlg-sub').textContent = who + (kind === 'bonus' ? '（当前额外额度 ' + fmt(license.bonusChars) + ' 字）' : '');
    if (kind === 'quota') {
      document.querySelectorAll('.dflt-day').forEach(e => e.textContent = fmt(state.settings.licenseDailyChars));
      document.querySelectorAll('.dflt-month').forEach(e => e.textContent = fmt(state.settings.licenseMonthlyChars));
      $('q-daily').value = license.dailyLimit ?? ''; $('q-monthly').value = license.monthlyLimit ?? '';
      $('q-disabled').checked = !!license.ttsDisabled; $('q-note').value = license.note || '';
    }
    if (kind === 'bonus') { $('b-delta').value = ''; $('b-note').value = ''; }
    if (kind === 'audit') {
      $('dlg-audit').innerHTML = '正在读取…';
      api('/licenses/' + license.id + '/audit').then(r => {
        const label = { quota: '设置额度', bonus: '额外额度', reset: '清零用量', settings: '全局设置' };
        $('dlg-audit').innerHTML = r.events.map(e => '<div><b>' + esc(label[e.action] || e.action) + '</b> <small class="muted">' + esc(time(e.createdAt)) + '</small><br><small>' + esc(JSON.stringify(e.details)) + '</small></div>').join('') || '<p class="muted">没有记录</p>';
      }).catch(err => { $('dlg-audit').textContent = err.message; });
    }
    $('dlg').showModal();
  }
  const numOrNull = v => v.trim() === '' ? null : Math.max(0, Math.round(Number(v)));
  $('dlg-form').addEventListener('submit', async event => {
    event.preventDefault();
    try {
      if (mode === 'quota') {
        await api('/licenses/' + current.id, 'PUT', { dailyLimit: numOrNull($('q-daily').value), monthlyLimit: numOrNull($('q-monthly').value),
          disabled: $('q-disabled').checked, note: $('q-note').value });
        message('已更新 #' + current.id + ' 的额度');
      } else if (mode === 'bonus') {
        const delta = Math.round(Number($('b-delta').value));
        if (!delta) throw new Error('请输入要增加（或扣减）的字数');
        const r = await api('/licenses/' + current.id + '/bonus', 'POST', { delta, note: $('b-note').value });
        message('#' + current.id + ' 额外额度现在是 ' + fmt(r.quota.bonusChars) + ' 字');
      }
      $('dlg').close(); load();
    } catch (err) { $('dlg-error').hidden = false; $('dlg-error').textContent = err.message; }
  });
  document.querySelectorAll('[data-add]').forEach(b => b.addEventListener('click', () => {
    $('b-delta').value = (Math.round(Number($('b-delta').value)) || 0) + Number(b.dataset.add);
  }));
  $('dlg-cancel').addEventListener('click', () => $('dlg').close());
  $('license-body').addEventListener('click', async event => {
    const btn = event.target.closest('button[data-act]'); if (!btn) return;
    const id = Number(btn.closest('tr').dataset.id), license = state.licenses.find(l => l.id === id); if (!license) return;
    const act = btn.dataset.act;
    if (act === 'reset-day') {
      if (!confirm('把 #' + id + ' 今天已用的 ' + fmt(license.dayUsed) + ' 字清零？（已花掉的额外额度不退）')) return;
      try { await api('/licenses/' + id + '/reset', 'POST', { scope: 'day' }); message('已清零 #' + id + ' 今日用量'); load(); }
      catch (err) { message(err.message, true); }
      return;
    }
    openDialog(act, license);
  });
  $('settings-form').addEventListener('submit', async event => {
    event.preventDefault();
    try {
      await api('/settings', 'PUT', { enabled: $('set-enabled').checked, dailyBudgetYuan: Number($('set-budget').value),
        licenseDailyChars: Math.round(Number($('set-daily').value)), licenseMonthlyChars: Math.round(Number($('set-monthly').value)) });
      message('全局设置已保存'); document.activeElement?.blur(); load();
    } catch (err) { message(err.message, true); }
  });
  $('search').addEventListener('input', () => state && renderLicenses());
  $('only-used').addEventListener('change', () => state && renderLicenses());
  $('refresh').addEventListener('click', load);
  load();
  setInterval(() => { if (!$('dlg').open) load(); }, 30000);
})();
`;
