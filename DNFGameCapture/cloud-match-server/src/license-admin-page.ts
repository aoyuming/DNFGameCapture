import { ADMIN_PAGE_CSS, escapeAdminAttribute } from './admin-page.js';

export function buildLicenseAdminPage(csrfToken: string): string {
  return `<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="dnf-admin-csrf" content="${escapeAdminAttribute(csrfToken)}"><title>DNF 密钥管理</title>
<link rel="stylesheet" href="/admin/licenses/style.css"></head><body class="license-admin">
<header class="topbar"><div><h1>DNF 密钥管理</h1><a href="/admin">管理首页</a></div><div><span id="refresh-status" class="muted" role="status">正在读取</span><button id="refresh-licenses" type="button">刷新</button></div></header>
<main class="license-workspace"><div id="message" class="message" role="status" hidden></div>
<div id="pending-notice" class="license-notice" hidden><span>上次操作结果未确认。重试将沿用原请求，请勿关闭或刷新此页面。</span><button id="retry-mutation" type="button">重试原操作</button></div>
<section class="license-create" aria-labelledby="create-heading"><h2 id="create-heading">生成密钥</h2>
<form id="license-form" class="license-form">
<label>有效时长<select id="license-preset" required disabled></select></label>
<label>数量<input id="license-quantity" type="number" min="1" max="200" step="1" value="1" required></label>
<label>备注<input id="license-label" maxlength="128" autocomplete="off"></label>
<label class="license-custom">自定义卡密（仅单张）<input id="license-key" maxlength="256" autocomplete="off" spellcheck="false"></label>
<button id="create-license" class="primary" type="submit" disabled>生成卡密</button></form>
<section id="issued-key" class="license-issued" aria-labelledby="issued-heading" hidden>
<div class="license-heading"><h2 id="issued-heading">本次生成 <span id="issued-count" class="muted"></span></h2><div class="license-actions"><button id="copy-all-keys" type="button">复制全部</button><button id="export-keys" type="button">导出文本</button><button id="hide-issued-keys" type="button">隐藏本批密钥</button></div></div>
<div id="issued-keys" class="license-key-list"></div></section></section>
<section aria-labelledby="licenses-heading"><div class="license-heading"><h2 id="licenses-heading">密钥列表 <span id="license-count" class="muted">0</span></h2><label>搜索<input id="license-search" type="search" placeholder="备注 / 设备 / 编号"></label></div>
  <div class="table-scroll" tabindex="0" role="region" aria-label="密钥列表"><table><thead><tr><th scope="col">编号 / 备注</th><th scope="col">状态</th><th scope="col">有效期</th><th scope="col">激活主播 / 绑定设备</th><th scope="col">卡密</th><th scope="col">管理</th></tr></thead><tbody id="license-list"></tbody></table></div>
<p id="license-empty" class="empty" hidden>暂无匹配密钥</p></section>
<div id="copy-fallback-wrap" class="license-copy-fallback" hidden><label>待复制密钥<textarea id="copy-fallback" readonly spellcheck="false" rows="4"></textarea></label></div>
</main>
<dialog id="license-dialog" aria-labelledby="dialog-title" aria-describedby="dialog-preview"><form id="dialog-form">
<h2 id="dialog-title"></h2><p id="dialog-subtitle" class="muted"></p>
<div id="dialog-error" class="message error" role="alert" hidden></div>
<label id="dialog-preset-field" hidden>延长时长<select id="dialog-preset"></select></label>
<label id="dialog-device-field" hidden>目标授权设备<select id="dialog-device"></select></label>
<label id="dialog-key-field" hidden>原始卡密<input id="dialog-key" maxlength="256" autocomplete="off" spellcheck="false"></label>
<p id="dialog-preview" class="license-preview"></p><div id="audit-list" hidden></div>
<div class="license-dialog-footer"><button id="dialog-cancel" type="button">取消</button><button id="dialog-retry" type="button" hidden>重试原操作</button><button id="dialog-submit" class="primary" type="submit">确认</button></div>
</form></dialog>
<script src="/admin/licenses/app.js" defer></script></body></html>`;
}

export const LICENSE_ADMIN_CSS = ADMIN_PAGE_CSS + String.raw`
.license-admin .license-workspace{max-width:1500px}
.license-admin .license-form{grid-template-columns:minmax(120px,.8fr) 90px minmax(140px,1fr) minmax(200px,1.5fr) auto}
.license-admin .license-notice{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:12px 0 20px;color:var(--gold);overflow-wrap:anywhere}
.license-admin .license-actions{display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.license-admin .license-actions button{padding:6px 9px;white-space:nowrap}
.license-admin .license-issued{margin-top:20px;border-top:1px solid var(--line)}
.license-admin .license-issued .license-heading{padding-top:16px}
.license-admin .license-key-list{max-height:280px;overflow:auto;scrollbar-gutter:stable}
.license-admin .license-key-row{display:grid;grid-template-columns:50px minmax(0,1fr) auto;gap:10px;align-items:center;padding:6px 0}
.license-admin .license-key-row input,.license-admin .license-key-cell input{font-family:Consolas,monospace;color:var(--green)}
.license-admin th,.license-admin td{padding:12px 10px;vertical-align:top}
.license-admin td:nth-child(1){width:20%;min-width:130px;max-width:290px}
.license-admin td:nth-child(2){width:80px;min-width:70px}
.license-admin td:nth-child(3){width:160px;min-width:130px}
.license-admin td:nth-child(4){width:17%;min-width:120px;max-width:220px}
.license-admin td:nth-child(5){width:24%;min-width:210px;max-width:360px}
.license-admin td:last-child{width:190px;min-width:175px}
.license-admin td .muted{display:block;margin-top:5px;font-size:12px}
.license-admin .license-status.pending{color:var(--gold)}
.license-admin .license-key-cell input{margin-bottom:8px;font-size:12px}
.license-admin button.danger{color:var(--danger)}
.license-admin .license-copy-fallback{margin-top:20px}
.license-admin textarea{box-sizing:border-box;width:100%;resize:vertical;padding:10px;border:1px solid var(--line);border-radius:4px;background:var(--bg);color:var(--green);font:13px Consolas,monospace}
.license-admin textarea:focus-visible{outline:2px solid var(--accent);outline-offset:3px}
.license-admin dialog{width:min(600px,calc(100% - 24px));max-height:calc(100dvh - 32px);overflow:auto;border:1px solid var(--line);border-radius:6px;padding:24px;background:var(--surface);color:var(--text)}
.license-admin dialog::backdrop{background:rgb(0 0 0 / .65)}
.license-admin dialog form{display:grid;gap:18px;min-width:0}
.license-admin dialog h2{font-size:18px}
.license-admin dialog p{line-height:1.7;overflow-wrap:anywhere}
.license-admin dialog .message{margin:0}
.license-admin .license-preview{white-space:pre-line;padding:12px 0;border-top:1px solid var(--line);border-bottom:1px solid var(--line)}
.license-admin .license-dialog-footer{display:flex;justify-content:flex-end;gap:10px;flex-wrap:wrap}
.license-admin .license-audit-event{border-bottom:1px solid var(--line);padding:12px 0}
.license-admin .license-audit-event h3{font-size:14px;margin:0 0 6px}
.license-admin .license-audit-event p{font-size:13px;white-space:pre-wrap}
.license-admin #audit-list{max-height:360px;overflow:auto;overflow-wrap:anywhere}
@media(max-width:1100px){.license-admin .license-form{grid-template-columns:minmax(0,1fr) 100px minmax(0,1fr)}.license-admin .license-custom{grid-column:1 / 3}.license-admin table{min-width:940px}}
@media(max-width:600px){.license-admin .license-form{grid-template-columns:minmax(0,1fr) 90px}.license-admin .license-form>label:nth-child(3),.license-admin .license-custom,.license-admin #create-license{grid-column:1 / -1}.license-admin .license-notice{align-items:flex-start;flex-direction:column}.license-admin .license-key-row{grid-template-columns:36px minmax(0,1fr) auto;gap:6px}.license-admin .license-key-row input{font-size:12px}.license-admin dialog{padding:18px}.license-admin .license-issued .license-heading{gap:10px}}
`;

export const LICENSE_ADMIN_JS = String.raw`
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const csrf = document.querySelector('meta[name="dnf-admin-csrf"]').content;
  const revealed = new Map();
  let licenses = [], presets = [], devices = [], issued = [], busy = false, loaded = false;
  let pending = null, dialog = null, issuedHidden = false;
  const errors = {
    invalid_csrf: '安全令牌已失效，请重新打开管理页。', invalid_request: '请求内容无效，请检查输入。',
    invalid_preset: '有效时长选项已变化，请刷新。', invalid_license_format: '卡密格式无效，请输入原始客户端兼容卡密。',
    license_already_exists: '该卡密已存在。', license_not_found: '密钥不存在，请刷新。',
    stale_license: '此密钥已被其他操作更新。请关闭弹窗后重新操作。',
    request_id_conflict: '请求编号冲突，请刷新后核对密钥记录。', key_unavailable: '该旧卡尚无可显示密钥，请补录原始卡密。',
    key_mismatch: '原始卡密与此记录不匹配。', unknown_device: '设备已不存在，请刷新后重新选择。',
    vault_unavailable: '密钥库暂不可用，请稍后重试。'
  };
  const time = value => value === null ? '永久' : new Date(value * 1000).toLocaleString('zh-CN', { hour12: false });
  const unactivated = item => item.activationMode === 'first_use' && item.activatedAt === null;
  function duration(value) {
    if (value === null) return '永久';
    for (const [unit, label] of [[86400, '天'], [3600, '小时'], [60, '分钟'], [1, '秒']]) {
      if (value % unit === 0) return value / unit + label + '时长';
    }
    return '时长未知';
  }
  const validity = item => unactivated(item) ? duration(item.durationSeconds) : time(item.expiresAt);
  function node(tag, text, className) {
    const element = document.createElement(tag);
    if (text !== undefined) element.textContent = text;
    if (className) element.className = className;
    return element;
  }
  function button(label, handler, className) {
    const element = node('button', label, className); element.type = 'button';
    element.addEventListener('click', handler); return element;
  }
  function keyInput(key, label) {
    const input = node('input'); input.readOnly = true; input.value = key;
    input.autocomplete = 'off'; input.spellcheck = false; input.setAttribute('aria-label', label); return input;
  }
  function message(value, error = false) {
    $('message').textContent = value; $('message').className = 'message' + (error ? ' error' : ''); $('message').hidden = false;
  }
  function controls() {
    const locked = busy || !!pending;
    document.querySelectorAll('.license-admin button,.license-admin input:not([readonly]),.license-admin select').forEach(element => { element.disabled = locked; });
    $('license-search').disabled = false;
    $('refresh-licenses').disabled = busy;
    $('dialog-cancel').disabled = busy;
    $('retry-mutation').disabled = busy || !pending;
    $('dialog-retry').disabled = busy || !pending;
    $('pending-notice').hidden = !pending;
    $('dialog-retry').hidden = !pending;
    $('dialog-submit').hidden = !!pending || dialog?.action === 'audit';
    $('dialog-submit').disabled = locked || !!dialog?.stale;
    $('license-key').disabled = locked || Number($('license-quantity').value) !== 1;
    $('license-preset').disabled = locked || !loaded;
    $('create-license').disabled = locked || !loaded;
    $('refresh-licenses').textContent = busy ? '处理中...' : '刷新';
  }
  async function api(path = '', body) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 30000);
    let response, result;
    try {
      try {
        response = await fetch('/admin/api/licenses' + path, { method: body === undefined ? 'GET' : 'POST', credentials: 'same-origin', signal: controller.signal,
        headers: { 'content-type': 'application/json', 'x-dnf-admin-csrf': csrf }, body: body === undefined ? undefined : JSON.stringify(body) });
      } catch { throw Object.assign(new Error(controller.signal.aborted ? '请求超时，操作结果尚未确认。' : '网络连接中断，操作结果尚未确认。'), { uncertain: true }); }
      if (response.status === 401) throw Object.assign(new Error('登录已失效，请重新打开管理页。'), { auth: true });
      try { result = await response.json(); }
      catch { throw Object.assign(new Error(controller.signal.aborted ? '请求超时，操作结果尚未确认。' : '服务器响应无效，请稍后重试。'), { uncertain: true }); }
      if (!response.ok || result?.ok === false) {
        throw Object.assign(new Error(errors[result?.code] || '请求失败，请稍后重试。'), { code: result?.code, uncertain: response.status >= 500 });
      }
      if (!result || typeof result !== 'object') throw Object.assign(new Error('服务器响应无效。'), { uncertain: true });
      return result;
    } finally { clearTimeout(timeout); }
  }
  async function run(task) {
    if (busy) return;
    busy = true; document.body.setAttribute('aria-busy', 'true'); controls();
    try { await task(); }
    catch (error) {
      message(error.message || '操作失败，请重试。', true);
      if ($('license-dialog').open) { $('dialog-error').textContent = error.message || '操作失败，请重试。'; $('dialog-error').hidden = false; }
    } finally { busy = false; document.body.removeAttribute('aria-busy'); controls(); }
  }
  function presetOptions(select) {
    const selected = select.value; select.replaceChildren();
    for (const preset of presets) { const option = node('option', preset.label); option.value = preset.id; select.append(option); }
    if (presets.some(preset => preset.id === selected)) select.value = selected;
  }
  function render() {
    const query = $('license-search').value.trim().toLocaleLowerCase();
    const visible = licenses.filter(item => [item.id, item.label, item.boundDeviceId, devices.find(device => device.deviceId === item.boundDeviceId)?.broadcasterName]
      .some(value => String(value ?? '').toLocaleLowerCase().includes(query)));
    $('license-list').replaceChildren(); $('license-count').textContent = visible.length + ' / ' + licenses.length;
    $('license-empty').hidden = !!visible.length;
    for (const item of visible) {
      const row = node('tr'); row.dataset.licenseId = item.id;
      const identity = node('td'); identity.append(node('strong', '#' + item.id), node('span', item.label || '未备注', 'muted')); row.append(identity);
      const disabled = item.disabledAt !== null, expired = !unactivated(item) && item.expiresAt !== null && item.expiresAt <= Date.now() / 1000;
      row.append(node('td', disabled ? '已禁用' : unactivated(item) ? '未激活' : expired ? '已过期' : '可用',
        'license-status' + (disabled ? ' disabled' : expired ? ' expired' : unactivated(item) ? ' pending' : '')));
      const expiry = node('td', validity(item));
      if (unactivated(item) && item.durationSeconds !== null) expiry.append(node('span', '首次使用起算', 'muted'));
      row.append(expiry);
      const device = devices.find(device => device.deviceId === item.boundDeviceId);
      const binding = node('td', device?.broadcasterName ? '主播：' + device.broadcasterName : item.boundDeviceId || '未绑定');
      if (device?.broadcasterName) binding.append(node('span', '授权设备：' + item.boundDeviceId, 'muted')); row.append(binding);
      const keyCell = node('td', undefined, 'license-key-cell'), keyActions = node('div', undefined, 'license-actions');
      if (revealed.has(item.id)) {
        keyCell.append(keyInput(revealed.get(item.id), '密钥 ' + item.id));
        keyActions.append(button('复制密钥', () => run(() => copy(revealed.get(item.id)))), button('隐藏密钥', () => { revealed.delete(item.id); render(); }));
      } else if (item.hasKey) {
        keyActions.append(button('显示密钥', () => run(async () => {
          const result = await api('/' + item.id + '/reveal', {});
          if (typeof result.key !== 'string' || !result.key) throw new Error('服务器未返回密钥。');
          revealed.set(item.id, result.key); render();
        })));
      } else keyActions.append(button('补录密钥', () => openDialog('key', item)));
      keyCell.append(keyActions); row.append(keyCell);
      const management = node('td'), actions = node('div', undefined, 'license-actions');
      actions.append(button('延长', () => openDialog('extend', item)), button('换绑', () => openDialog('rebind', item)),
        button(disabled ? '启用' : '禁用', () => openDialog('disable', item), disabled ? '' : 'danger'),
        button('记录', () => openAudit(item)));
      management.append(actions); row.append(management); $('license-list').append(row);
    }
    controls();
  }
  async function refresh() {
    $('refresh-status').textContent = '正在读取';
    try {
      const result = await api();
      if (!Array.isArray(result.licenses) || !Array.isArray(result.presets) || !result.presets.length || !Array.isArray(result.devices)) {
        throw new Error('密钥管理接口尚未就绪，请稍后刷新。');
      }
      licenses = result.licenses; presets = result.presets; devices = result.devices; loaded = true;
      $('license-quantity').max = String(Math.min(200, Number.isInteger(result.maxBatch) ? Math.max(1, result.maxBatch) : 200));
      presetOptions($('license-preset')); render();
      $('refresh-status').textContent = '已更新 ' + new Date().toLocaleTimeString('zh-CN', { hour12: false });
    } catch (error) { $('refresh-status').textContent = '读取失败'; throw error; }
  }
  function renderIssued() {
    $('issued-key').hidden = !issued.length; $('issued-keys').hidden = issuedHidden;
    $('hide-issued-keys').textContent = issuedHidden ? '显示本批密钥' : '隐藏本批密钥';
    $('issued-count').textContent = issued.length + ' 张'; $('issued-keys').replaceChildren();
    if (issuedHidden) return;
    for (const [index, item] of issued.entries()) {
      const row = node('div', undefined, 'license-key-row'), input = keyInput(item.key, '本次生成的密钥 ' + item.id);
      const copyButton = button('复制', () => run(() => copy(item.key)));
      if (index === 0) { input.id = 'created-license-key'; copyButton.id = 'copy-license-key'; }
      row.append(node('span', '#' + item.id, 'muted'), input, copyButton); $('issued-keys').append(row);
    }
  }
  async function copy(value) {
    try {
      if (!navigator.clipboard?.writeText) throw new Error();
      await navigator.clipboard.writeText(value); message('密钥已复制');
    } catch {
      $('copy-fallback-wrap').hidden = false; const area = $('copy-fallback'); area.value = value; area.focus(); area.select();
      let copied = false;
      try { copied = document.execCommand('copy'); } catch { /* HTTP may disable both clipboard APIs. */ }
      if (copied) $('copy-fallback-wrap').hidden = true;
      message(copied ? '密钥已复制' : '无法访问剪贴板，密钥已选中，请手动复制。', !copied);
    }
  }
  function requestId() {
    // getRandomValues is also available on non-secure HTTP origins.
    return Array.from(crypto.getRandomValues(new Uint8Array(16)), byte => byte.toString(16).padStart(2, '0')).join('');
  }
  function mergeLicense(item) {
    const index = licenses.findIndex(license => license.id === item.id);
    const { key, ...record } = item;
    if (index < 0) licenses.unshift(record); else licenses[index] = record;
  }
  async function sendPending() {
    const operation = pending;
    if (!operation) return;
    let result;
    try {
      result = await api(operation.path, operation.body);
      if (operation.path === '' ? !Array.isArray(result.licenses) || result.licenses.length !== operation.body.count || result.licenses.some(item => !item.id || typeof item.key !== 'string') : !result.license?.id) {
        throw Object.assign(new Error('服务器响应不完整，请重试原操作。'), { uncertain: true });
      }
    } catch (error) {
      if (error.uncertain) operation.uncertain = true;
      // A rejected retry cannot establish whether the earlier request committed.
      if (!error.uncertain && !(operation.uncertain && (error.auth || error.code === 'invalid_csrf'))) pending = null;
      if (error.code === 'stale_license') {
        if (dialog) dialog.stale = true;
        try { await refresh(); } catch { /* Keep the mutation conflict visible. */ }
      }
      throw error;
    }
    // The write is complete before any follow-up GET; a failed refresh must never repeat it.
    pending = null;
    if (operation.path === '') {
      issued = result.licenses; issuedHidden = false; issued.forEach(mergeLicense); renderIssued();
      $('license-key').value = '';
    } else {
      mergeLicense(result.license);
      if (operation.path.endsWith('/key')) revealed.delete(result.license.id);
      closeDialog();
    }
    render();
    const success = operation.path === '' ? (result.replayed ? '已恢复本批密钥，共 ' : '已生成 ') + result.licenses.length + ' 张卡密。' : '密钥已更新。';
    message(success);
    try { await refresh(); }
    catch (error) { message(success + '列表刷新失败，可手动刷新。' + error.message, true); }
  }
  function mutate(path, body) {
    if (busy || pending) return;
    run(async () => { pending = { path, body: { ...body, requestId: requestId() } }; controls(); await sendPending(); });
  }
  function closeDialog() {
    $('license-dialog').close(); dialog = null; $('dialog-key').value = ''; $('audit-list').replaceChildren();
  }
  function openDialog(action, item) {
    if (busy || pending) return;
    dialog = { action, item: { ...item }, stale: false };
    const titles = { extend: '延长有效期', rebind: '更换绑定设备', key: '补录原始密钥', disable: item.disabledAt !== null ? '启用密钥' : '禁用密钥', audit: '操作记录' };
    $('dialog-title').textContent = titles[action]; $('dialog-subtitle').textContent = '#' + item.id + ' · ' + (item.label || '未备注');
    $('dialog-error').hidden = true; $('dialog-preview').hidden = false;
    $('dialog-cancel').textContent = action === 'audit' ? '关闭' : '取消';
    $('dialog-submit').textContent = '确认' + (action === 'extend' ? '延长' : action === 'rebind' ? '换绑' : action === 'key' ? '补录' : item.disabledAt !== null ? '启用' : '禁用');
    $('dialog-preset-field').hidden = action !== 'extend'; $('dialog-device-field').hidden = action !== 'rebind'; $('dialog-key-field').hidden = action !== 'key';
    $('dialog-key').required = action === 'key'; $('dialog-key').value = '';
    $('audit-list').hidden = action !== 'audit'; $('audit-list').replaceChildren();
    presetOptions($('dialog-preset')); $('dialog-preset').selectedIndex = 0;
    $('dialog-device').replaceChildren(); const unbind = node('option', '解除绑定（下次使用时绑定）'); unbind.value = ''; $('dialog-device').append(unbind);
    for (const device of devices) { const option = node('option', (device.broadcasterName ? '主播 ' + device.broadcasterName : '授权设备') + ' · ' + device.deviceId); option.value = device.deviceId; $('dialog-device').append(option); }
    if (devices.some(device => device.deviceId === item.boundDeviceId)) $('dialog-device').value = item.boundDeviceId;
    preview(); controls(); $('license-dialog').showModal();
  }
  function preview() {
    if (!dialog) return;
    const { action, item } = dialog;
    let text = '';
    if (action === 'extend') {
      const preset = presets.find(preset => preset.id === $('dialog-preset').value);
      if (!preset) return;
      const permanent = unactivated(item) ? item.durationSeconds === null : item.expiresAt === null;
      text = '当前：' + validity(item) + '\n';
      if (permanent) text += '延长后：永久，有效期不变。';
      else if (preset.durationSeconds === null) text += '延长后：永久。';
      else if (unactivated(item)) text += '延长后：' + duration(item.durationSeconds + preset.durationSeconds) + '，仍为未激活。';
      else {
        const base = Math.max(item.expiresAt, Math.floor(Date.now() / 1000));
        text += '预计到期：' + time(base + preset.durationSeconds) + '\n以' + (item.expiresAt > Date.now() / 1000 ? '原到期时间' : '当前时间') + '起算，最终以服务器处理时间为准。';
      }
    } else if (action === 'rebind') {
      const device = devices.find(device => device.deviceId === $('dialog-device').value);
      text = device ? '确认绑定至：' + (device.broadcasterName || device.deviceId) + '\n' + device.deviceId : '确认解除绑定，下次使用时绑定设备。';
      text += '\n有效期不变，不会重新计时。旧服务端授权会话将撤销，已签发的离线授权不会被远程清除。';
    } else if (action === 'key') text = '仅补录与当前记录匹配的原始卡密，不会更换卡密或改变有效期。';
    else if (action === 'disable') text = item.disabledAt !== null ? '确认恢复此密钥使用资格？原有效期和绑定设备保持不变。' : '确认禁用此密钥？已签发的离线授权不会被远程清除。';
    else { text = '正在读取操作记录'; }
    $('dialog-preview').textContent = text;
  }
  const auditActions = { create: '生成', batch_create: '批量生成', generate: '生成', enroll_legacy: '旧永久卡登记', activate: '激活', activation: '激活', extend: '延长', rebind: '换绑', disable: '禁用', enable: '启用', key: '补录密钥', recover_key: '补录密钥', key_recovery: '补录密钥', key_backfill: '补录密钥', reveal: '查看密钥' };
  const detailNames = { preset: '时长选项', count: '数量', batchCount: '批次数量', label: '备注', revision: '版本', deviceId: '授权设备', boundDeviceId: '绑定设备', beforeDeviceId: '原授权设备', previousDeviceId: '原授权设备', expiresAt: '到期时间', beforeExpiresAt: '原到期时间', previousExpiresAt: '原到期时间', activatedAt: '激活时间', durationSeconds: '有效时长', beforeDurationSeconds: '原有效时长', firstActivation: '首次激活', disabled: '禁用', activationMode: '计时方式', before: '变更前', after: '变更后' };
  function auditDetails(value, field = '') {
    if (value === null) return field === 'durationSeconds' || field === 'beforeDurationSeconds' ? '永久' : '无';
    if (Array.isArray(value)) return value.map(item => auditDetails(item, field)).join('、');
    if (typeof value === 'object') return Object.entries(value).map(([key, item]) => (detailNames[key] || '附加信息') + '：' + auditDetails(item, key)).join('\n');
    if (typeof value === 'boolean') return value ? '是' : '否';
    if (field === 'preset') return presets.find(preset => preset.id === value)?.label || '未知时长';
    if (field === 'durationSeconds' || field === 'beforeDurationSeconds') return duration(value);
    if (field.endsWith('At') && typeof value === 'number') return time(value);
    if (field === 'activationMode') return value === 'first_use' ? '首次使用起算' : '固定到期';
    return String(value);
  }
  function openAudit(item) {
    if (busy || pending) return;
    openDialog('audit', item);
    run(async () => {
      const result = await api('/' + item.id + '/audit');
      $('dialog-preview').textContent = result.events.length ? '共 ' + result.events.length + ' 条记录' : '暂无操作记录';
      for (const event of result.events) {
        const entry = node('div', undefined, 'license-audit-event');
        entry.append(node('h3', (auditActions[event.action] || '其他操作') + ' · ' + time(event.createdAt)), node('p', auditDetails(event.details)));
        $('audit-list').append(entry);
      }
    });
  }
  $('license-form').addEventListener('submit', event => {
    event.preventDefault(); if (busy || pending || !loaded || !$('license-form').reportValidity()) return;
    const count = Number($('license-quantity').value), key = $('license-key').value.trim();
    if (!Number.isInteger(count) || count < 1 || count > Number($('license-quantity').max)) return;
    mutate('', { preset: $('license-preset').value, count, label: $('license-label').value.trim(), ...(count === 1 && key ? { key } : {}) });
  });
  $('license-quantity').addEventListener('input', controls);
  $('license-search').addEventListener('input', render);
  $('refresh-licenses').addEventListener('click', () => run(async () => { await refresh(); if (!pending) $('message').hidden = true; }));
  $('retry-mutation').addEventListener('click', () => run(sendPending));
  $('dialog-retry').addEventListener('click', () => run(sendPending));
  $('dialog-cancel').addEventListener('click', () => { if (!busy) closeDialog(); });
  $('license-dialog').addEventListener('cancel', event => { event.preventDefault(); if (!busy) closeDialog(); });
  $('dialog-preset').addEventListener('change', preview); $('dialog-device').addEventListener('change', preview);
  $('dialog-form').addEventListener('submit', event => {
    event.preventDefault(); if (!dialog || dialog.stale || busy || pending || !$('dialog-form').reportValidity()) return;
    const { action, item } = dialog; let body;
    if (action === 'extend') body = { preset: $('dialog-preset').value };
    else if (action === 'rebind') body = { deviceId: $('dialog-device').value || null };
    else if (action === 'disable') body = { disabled: item.disabledAt === null };
    else if (action === 'key') body = { key: $('dialog-key').value.trim() };
    else return;
    $('dialog-error').hidden = true; mutate('/' + item.id + '/' + action, { ...body, revision: item.revision });
  });
  $('copy-all-keys').addEventListener('click', () => run(() => copy(issued.map(item => item.key).join('\r\n'))));
  $('hide-issued-keys').addEventListener('click', () => { issuedHidden = !issuedHidden; renderIssued(); controls(); });
  $('export-keys').addEventListener('click', () => {
    const url = URL.createObjectURL(new Blob(['\uFEFF' + issued.map(item => item.key).join('\r\n')], { type: 'text/plain;charset=utf-8' }));
    const link = node('a'); link.href = url; link.download = '卡密-' + issued.length + '张.txt'; document.body.append(link); link.click(); link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  });
  run(refresh);
})();
`;
