export function escapeAdminAttribute(value: string): string {
  return value.replace(/[&<>"']/g, character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[character]!);
}

export function buildAdminPage(csrfToken: string): string {
  return `<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="dnf-admin-csrf" content="${escapeAdminAttribute(csrfToken)}"><title>DNF 云端同步管理</title>
<link rel="stylesheet" href="/admin/style.css"></head><body>
<header class="topbar"><h1>DNF 云端同步管理</h1><span class="muted">管理首页</span></header>
<main class="hub"><nav class="hub-entries" aria-label="管理工作区">
<a class="hub-entry licenses" href="/admin/licenses" aria-label="管理密钥">管理密钥</a>
<a class="hub-entry broadcasters" href="/admin/broadcasters" aria-label="管理主播">管理主播</a>
<a class="hub-entry library" href="/admin/library" aria-label="管理共享库">管理共享库</a>
</nav></main></body></html>`;
}

export const ADMIN_PAGE_CSS = String.raw`
:root{color-scheme:dark;--bg:#111416;--surface:#191d20;--raised:#242a2e;--line:#374047;--text:#edf2f4;--muted:#a0adb5;--accent:#60c3dd;--danger:#ff9aa6;--green:#6ad4ae;--gold:#efd080}
*{box-sizing:border-box;letter-spacing:0}body{margin:0;min-width:0;min-height:100vh;background:var(--bg);color:var(--text);font:14px "Microsoft YaHei UI","Microsoft YaHei",sans-serif}h1,h2,p{margin:0}h1{font-size:22px}h2{font-size:17px}a{color:var(--accent);text-underline-offset:4px}button,input,select{font:inherit;min-width:0}button{min-height:36px;border:1px solid var(--line);border-radius:4px;padding:7px 12px;background:var(--raised);color:var(--text);cursor:pointer}button:hover{border-color:var(--accent)}button:disabled{opacity:.5;cursor:not-allowed}button.primary{background:#183d3a;border-color:var(--green);color:#d9fff2}input,select{width:100%;min-height:36px;border:1px solid var(--line);border-radius:4px;padding:7px 10px;background:var(--bg);color:var(--text)}a:focus-visible,button:focus-visible,input:focus-visible,select:focus-visible{outline:2px solid var(--accent);outline-offset:3px}[hidden]{display:none!important}.muted{color:var(--muted)}.topbar{min-height:68px;padding:18px 24px;border-bottom:1px solid var(--line);display:flex;align-items:center;justify-content:space-between;gap:16px;flex-wrap:wrap;background:var(--surface)}.topbar>div{display:flex;align-items:center;gap:20px;flex-wrap:wrap}.hub{max-width:960px;padding:36px 24px;margin:auto}.hub-entries{display:grid;border-top:1px solid var(--line)}.hub-entry{display:flex;align-items:center;min-height:96px;padding:24px;border-bottom:1px solid var(--line);border-left:3px solid var(--accent);font-size:20px;font-weight:700;text-decoration:none;transition:background .15s}.hub-entry:hover{background:var(--surface)}.hub-entry:after{content:"\2192";margin-left:auto;font-size:24px}.hub-entry.licenses{border-left-color:var(--gold);color:var(--gold)}.hub-entry.broadcasters{border-left-color:var(--accent)}.hub-entry.library{border-left-color:var(--green);color:var(--green)}
.license-workspace{max-width:1320px;margin:auto;padding:24px}.license-create{padding-bottom:24px;border-bottom:1px solid var(--line)}.license-form{display:grid;grid-template-columns:minmax(160px,1.5fr) minmax(140px,1fr) minmax(160px,1fr) auto;gap:14px;align-items:end;margin-top:16px}label{display:grid;gap:7px;color:var(--muted);font-size:13px}.issued-key{margin-top:16px;display:grid;grid-template-columns:minmax(0,1fr) auto;gap:10px;align-items:end}.issued-key input{font-family:Consolas,monospace;color:var(--green)}.license-heading{display:flex;align-items:center;justify-content:space-between;gap:16px;flex-wrap:wrap;padding:22px 0 14px}.license-heading label{width:280px;max-width:100%}.table-scroll{overflow:auto}table{width:100%;border-collapse:collapse;text-align:left;font-size:13px}th{color:var(--muted);font-weight:400;background:var(--surface)}th,td{padding:12px 14px;border-bottom:1px solid var(--line)}td{overflow-wrap:anywhere}td:nth-child(2){max-width:260px}td:nth-child(5){max-width:220px}td:last-child{width:86px}.license-status{color:var(--green)}.license-status.disabled{color:var(--danger)}.license-status.expired{color:var(--gold)}.message{margin-bottom:18px;padding:12px;border-left:3px solid var(--accent);background:var(--surface);overflow-wrap:anywhere}.message.error{border-color:var(--danger);color:var(--danger)}.empty{padding:24px;color:var(--muted)}
@media(max-width:900px){.license-form{grid-template-columns:1fr 1fr}.table-scroll table{min-width:720px}}
@media(max-width:600px){.topbar{padding:16px 12px}.topbar h1{font-size:20px}.hub{padding:24px 12px}.hub-entry{min-height:84px;padding:20px;font-size:18px}.license-workspace{padding:20px 12px}.license-form{grid-template-columns:minmax(0,1fr)}.license-heading{align-items:flex-start}.license-heading label{width:100%}.issued-key{grid-template-columns:minmax(0,1fr)}}
`;

// Retain the protected legacy asset URL without loading any management workspace.
export const ADMIN_PAGE_JS = "'use strict';";
