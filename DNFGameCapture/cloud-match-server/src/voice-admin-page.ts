import { ADMIN_PAGE_CSS, escapeAdminAttribute } from './admin-page.js';
export function buildVoiceAdminPage(csrfToken:string):string {
  return `<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="dnf-admin-csrf" content="${escapeAdminAttribute(csrfToken)}"><title>声音管理 · DNF</title><link rel="stylesheet" href="/admin/voices/style.css"></head><body>
  <header class="topbar"><div><a href="/admin">管理首页</a><h1>声音管理</h1></div><button id="refresh">刷新</button></header>
  <main class="workspace"><p class="notice">客户端仅下载音频，绝不触发生成。音频更新后客户端自动校验并更新缓存；已生成内容默认复用。支持上传 30 秒以内、8MB 以内的 PCM WAV。</p>
  <p id="message" class="message" role="status">正在加载…</p><div class="grid"><aside><div class="heading"><h2>音色列表</h2><button id="new">＋ 新音色</button></div><div id="voices"></div></aside>
  <section><h2 id="heading">添加音色</h2><form id="voice-form"><div class="fields"><label>内部标识（创建后不可改）<input id="id" required pattern="[a-z0-9][a-z0-9-]{0,63}" maxlength="64" placeholder="my-voice"></label><label>显示名称<input id="label" required maxlength="80"></label><label>类型<select id="kind"><option value="tts">豆包 TTS</option><option value="custom">自定义音频</option></select></label><label>音色 ID<input id="speaker" maxlength="128" placeholder="zh_female_…_bigtts"></label><label>模型资源<select id="resource"><option>seed-tts-2.0</option><option>seed-tts-1.0</option></select></label><label class="check"><input id="enabled" type="checkbox" checked>客户端可见</label></div><button class="primary" type="submit">保存音色</button></form>
  <div class="heading"><h2>固定播报音频</h2><button id="generate">生成缺失音频（收费）</button></div><p class="muted">替换单句会保留旧版本；“强制重生成”需要额外确认，不会自动重试失败的请求。</p><div class="table-scroll"><table><thead><tr><th>台词</th><th>版本 / 来源</th><th>管理</th></tr></thead><tbody id="clips"></tbody></table></div></section></div>
  <section class="library"><div class="heading"><h2>自定义音频库（发布给客户端）</h2></div><p class="muted">用任意音色把一句话生成音频，或上传 WAV。发布后，客户端在「声音管理 → 自定义语音 → 服务器音频库」里可以看到、试听、下载，下载后可在场景规则里使用。管理员生成会调用付费 TTS，但<strong>不计入主播额度和每日 5 元预算</strong>。</p>
  <form id="lib-form" class="lib-form"><label>音色<select id="lib-voice" required></select></label><label class="lib-text">文字（最多 200 字）<textarea id="lib-text" maxlength="200" rows="2" required placeholder="例如：欢迎来到今晚的 DNF 决斗赛！"></textarea></label><label>名称（客户端显示，留空 = 文字）<input id="lib-title" maxlength="30"></label><button class="primary" id="lib-generate" type="submit">生成（收费）</button></form>
  <div class="lib-upload"><label>或上传 WAV（PCM，30 秒 / 8MB 以内）<input id="lib-file" type="file" accept=".wav,audio/wav"></label><button id="lib-upload" type="button">上传</button><span id="lib-stats" class="muted"></span></div>
  <p id="lib-msg" class="message" role="status" hidden></p>
  <div class="table-scroll"><table><thead><tr><th>名称</th><th>文字 / 音色</th><th>大小</th><th>状态</th><th>管理</th></tr></thead><tbody id="lib-items"></tbody></table></div></section>
  <section class="jobs"><h2>最近生成任务</h2><p class="muted">API 密钥只从服务器环境变量读取，不发给浏览器。中断任务不会自动再次扣费。</p><div id="jobs"></div></section></main><script src="/admin/voices/app.js" defer></script></body></html>`;
}
export const VOICE_ADMIN_CSS=ADMIN_PAGE_CSS+String.raw`
.workspace{max-width:1400px;margin:auto;padding:24px}.notice{padding:16px 0;color:var(--muted);line-height:1.8}.grid{display:grid;grid-template-columns:260px minmax(0,1fr);gap:28px}.heading{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:18px 0}.fields{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin:18px 0}.check{display:flex;align-items:center}.check input{width:auto}.voice{display:block;width:100%;text-align:left;margin:8px 0;line-height:1.6}.voice.active{border-color:var(--green);background:#183d3a}.actions{display:flex;gap:8px;flex-wrap:wrap}.actions input{max-width:210px}.jobs{margin-top:32px}.job{padding:10px;border-bottom:1px solid var(--line)}.muted{line-height:1.7}td:last-child{width:auto}@media(max-width:900px){.grid{grid-template-columns:1fr}.fields{grid-template-columns:1fr}.workspace{padding:12px}}
.library{margin-top:32px;padding-top:8px;border-top:1px solid var(--line)}.lib-form{display:grid;grid-template-columns:minmax(140px,.8fr) minmax(260px,2fr) minmax(160px,1fr) auto;gap:14px;align-items:end;margin:14px 0}
.lib-form textarea{width:100%;min-height:36px;border:1px solid var(--line);border-radius:4px;padding:7px 10px;background:var(--bg);color:var(--text);font:inherit;resize:vertical}
.lib-upload{display:flex;gap:12px;align-items:end;flex-wrap:wrap;margin-bottom:12px}.lib-upload label{width:320px;max-width:100%}.lib-upload input[type=file]{padding:5px}
#lib-items td .actions button{padding:5px 9px;min-height:30px}.lib-off{color:var(--gold)}.lib-on{color:var(--green)}
@media(max-width:900px){.lib-form{grid-template-columns:1fr}}
`;
export const VOICE_ADMIN_JS=String.raw`
'use strict';
const $=id=>document.getElementById(id), csrf=document.querySelector('meta[name="dnf-admin-csrf"]').content;
let state=null, selected='', pending=false, audio=null;
function requestId(){const b=crypto.getRandomValues(new Uint8Array(16));b[6]=(b[6]&15)|64;b[8]=(b[8]&63)|128;const h=Array.from(b,x=>x.toString(16).padStart(2,'0')).join('');return h.slice(0,8)+'-'+h.slice(8,12)+'-'+h.slice(12,16)+'-'+h.slice(16,20)+'-'+h.slice(20);}
function message(text,error=false){$('message').textContent=text;$('message').classList.toggle('error',error);}
async function api(path,method='GET',body){const raw=body instanceof File;const response=await fetch('/admin/api/voices'+path,{method,headers:{'x-dnf-admin-csrf':csrf,...(body?{'Content-Type':raw?'audio/wav':'application/json'}:{})},body:body?(raw?body:JSON.stringify(body)):undefined});const data=await response.json();if(!response.ok)throw Error(data.code||'request_failed');return data;}
function el(tag,text){const e=document.createElement(tag);if(text!==undefined)e.textContent=text;return e;}
function button(text,action){const b=el('button',text);b.type='button';b.onclick=()=>run(action);return b;}
async function run(fn){if(pending)return;pending=true;try{await fn();}catch(e){message(e.message,true);}finally{pending=false;}}
function fill(id){selected=id;const v=state?.voices.find(v=>v.id===id);$('heading').textContent=v?'编辑音色 · '+v.label:'添加音色';for(const key of ['id','label','speaker'])$(key).value=v?.[key]||'';$('id').readOnly=!!v;$('kind').value=v?.kind||'tts';$('resource').value=v?.resource||'seed-tts-2.0';$('enabled').checked=v?v.enabled:true;render();}
function render(){if(!state)return;$('voices').replaceChildren();for(const v of state.voices){const b=button(v.label+' · '+v.clips.length+'/'+Object.keys(state.phrases).length+(v.enabled?'':' · 已停用'),async()=>fill(v.id));b.className='voice'+(selected===v.id?' active':'');$('voices').append(b);}const voice=state.voices.find(v=>v.id===selected);$('generate').disabled=!voice||voice.kind!=='tts'||!state.ttsConfigured;$('clips').replaceChildren();for(const [key,text] of Object.entries(state.phrases)){const row=el('tr'),clip=voice?.clips.find(c=>c.key===key),actions=el('div');actions.className='actions';row.append(el('td',text));row.append(el('td',clip?clip.sha256.slice(0,12)+' · '+clip.source:'未配置'));if(voice){if(clip)actions.append(button('试听',async()=>{if(audio)audio.pause();audio=new Audio('/admin/api/voices/audio/'+clip.sha256+'.wav');await audio.play();}));const file=el('input');file.type='file';file.accept='.wav,audio/wav';file.setAttribute('aria-label','上传 '+text);file.onchange=()=>run(async()=>{if(!file.files.length)return;const f=file.files[0];if(f.size>8388608)throw Error('音频不能超过 8MB');if(clip&&!confirm('替换这句音频？客户端将自动更新，旧版本仍保留。'))return;await api('/'+voice.id+'/clips/'+key,'PUT',f);await reload();message('音频已发布，客户端将在下次同步时更新。');});actions.append(file);if(voice.kind==='tts')actions.append(button(clip?'强制重生成':'生成',async()=>generate([key],!!clip)));}const cell=el('td');cell.append(actions);row.append(cell);$('clips').append(row);}$('jobs').replaceChildren();for(const j of state.jobs.slice(0,60))$('jobs').append(el('div',j.voiceId+' / '+j.key+' — '+j.state+(j.error?' · '+j.error:'')));}
async function reload(){state=await api('/state');render();}
async function generate(keys,force){if(!confirm((force?'将重新生成并覆盖当前版本；旧音频会保留。':'仅生成缺失音频，已有内容直接复用。')+'这会调用付费 TTS API，确认继续？'))return;await api('/'+selected+'/generate','POST',{keys,force,confirmPaid:true,requestId:requestId()});await reload();message('任务已提交。失败不会自动重试，请查看任务状态。');}
$('voice-form').onsubmit=e=>{e.preventDefault();run(async()=>{const v={id:$('id').value,label:$('label').value,kind:$('kind').value,speaker:$('speaker').value,resource:$('resource').value,enabled:$('enabled').checked};await api(selected?'/'+selected:'',selected?'PUT':'POST',v);await reload();fill(v.id);message('音色已保存，客户端列表将自动同步。');});};
$('new').onclick=()=>fill('');$('refresh').onclick=()=>run(reload);$('generate').onclick=()=>run(()=>generate(Object.keys(state.phrases),false));
run(async()=>{await reload();fill(state.voices[0]?.id||'');message(state.ttsConfigured?'声音管理已就绪。':'可上传自定义音频；付费生成尚未配置服务器 TTS 环境变量。');});
setInterval(()=>{if(!pending)run(reload);},5000);
// ---------- 自定义音频库（发布给客户端；不计入主播额度） ----------
(()=>{
const $=id=>document.getElementById(id), csrf=document.querySelector('meta[name="dnf-admin-csrf"]').content;
const MSG={tts_not_configured:'服务器没有配置 TTS 密钥，无法生成（仍可上传 WAV）。',voice_not_tts:'这个音色不是豆包 TTS 音色。',text_too_long:'文字超过 200 字。',text_invalid_chars:'文字里有不支持的字符（例如表情）。',empty_text:'请输入文字。',library_full:'音频库已满（500 条），请先删除一些。',invalid_title:'名称无效。',invalid_wav:'不是有效的 WAV 文件。',pcm_wav_required:'只支持 PCM WAV。',wav_max_30_seconds:'WAV 不能超过 30 秒。',voice_storage_limit:'服务器音频存储已满。',tts_provider_error:'TTS 服务返回错误。',tts_request_failed:'TTS 请求失败。',invalid_csrf:'安全令牌失效，请刷新页面。'};
let lib=null,busy=false,audio=null,voiceSig='';
const esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'})[c]);
function say(text,error){const e=$('lib-msg');e.hidden=!text;e.textContent=text||'';e.classList.toggle('error',!!error);}
function uuid(){const b=crypto.getRandomValues(new Uint8Array(16));b[6]=(b[6]&15)|64;b[8]=(b[8]&63)|128;const h=Array.from(b,x=>x.toString(16).padStart(2,'0')).join('');return h.slice(0,8)+'-'+h.slice(8,12)+'-'+h.slice(12,16)+'-'+h.slice(16,20)+'-'+h.slice(20);}
async function call(path,method='GET',body,raw=false){const r=await fetch('/admin/api/voice-library'+path,{method,headers:{'x-dnf-admin-csrf':csrf,...(body!==undefined?{'Content-Type':raw?'audio/wav':'application/json'}:{})},body:body===undefined?undefined:(raw?body:JSON.stringify(body))});const d=await r.json().catch(()=>({}));if(!r.ok)throw Error(MSG[d.code]||('操作失败：'+(d.code||r.status)));return d;}
async function guard(fn){if(busy)return;busy=true;try{await fn();}catch(e){say(e.message,true);}finally{busy=false;}}
async function load(){
  lib=await call('');
  const s=await (await fetch('/admin/api/voices/state',{headers:{'x-dnf-admin-csrf':csrf}})).json();
  const voices=(s.voices||[]).filter(v=>v.kind==='tts'&&v.enabled), sig=voices.map(v=>v.id+v.label).join('|');
  if(sig!==voiceSig){voiceSig=sig;const cur=$('lib-voice').value;$('lib-voice').innerHTML=voices.map(v=>'<option value="'+esc(v.id)+'">'+esc(v.label)+'</option>').join('')||'<option value="">没有可用的 TTS 音色</option>';if(voices.some(v=>v.id===cur))$('lib-voice').value=cur;}
  render();
}
function render(){
  const st=lib.stats;
  $('lib-stats').textContent='共 '+st.items+' 条，已发布 '+st.published+' 条；管理员累计生成 '+st.chars+' 字（不计入主播额度）'+(st.configured?'':' · 未配置 TTS 密钥');
  $('lib-generate').disabled=!st.configured;
  $('lib-items').innerHTML=lib.items.map(i=>'<tr data-id="'+esc(i.id)+'"><td><strong>'+esc(i.title)+'</strong></td><td>'+(i.text?esc(i.text):'<span class="muted">（上传的音频）</span>')+'<br><small class="muted">'+esc(i.voiceLabel)+'</small></td><td>'+Math.max(1,Math.round(i.bytes/1024))+' KB</td><td>'+(i.enabled?'<span class="lib-on">已发布</span>':'<span class="lib-off">未发布</span>')+'</td><td><div class="actions"><button type="button" data-lib="play">试听</button><button type="button" data-lib="rename">改名</button><button type="button" data-lib="toggle">'+(i.enabled?'取消发布':'发布')+'</button><button type="button" data-lib="delete">删除</button></div></td></tr>').join('')||'<tr><td colspan="5" class="muted">还没有自定义音频</td></tr>';
}
$('lib-items').addEventListener('click',e=>{const b=e.target.closest('button[data-lib]');if(!b)return;const id=b.closest('tr').dataset.id,item=lib.items.find(i=>i.id===id);if(!item)return;const act=b.dataset.lib;
  if(act==='play'){if(audio)audio.pause();audio=new Audio('/admin/api/voices/audio/'+item.sha256+'.wav');audio.play().catch(()=>say('浏览器无法播放',true));return;}
  guard(async()=>{
    if(act==='rename'){const t=prompt('新的名称（客户端显示）',item.title);if(t===null)return;await call('/'+id,'PUT',{title:t.trim()});say('已改名');}
    else if(act==='toggle'){await call('/'+id,'PUT',{enabled:!item.enabled});say(item.enabled?'已取消发布，客户端将不再显示':'已发布，客户端刷新后可见');}
    else if(act==='delete'){if(!confirm('删除「'+item.title+'」？已下载到主播电脑上的副本不受影响。'))return;await call('/'+id,'DELETE');say('已删除');}
    await load();
  });
});
$('lib-form').addEventListener('submit',e=>{e.preventDefault();guard(async()=>{
  const text=$('lib-text').value.trim();if(!text)throw Error('请输入文字');
  if(!confirm('将调用付费 TTS 生成这句话（'+[...text].length+' 字），不计入主播额度。确认？'))return;
  say('正在生成…');
  const r=await call('/generate','POST',{voiceId:$('lib-voice').value,text,title:$('lib-title').value.trim()||undefined,requestId:uuid()});
  $('lib-text').value='';$('lib-title').value='';say('已生成并发布：'+r.item.title);await load();
});});
$('lib-upload').addEventListener('click',()=>guard(async()=>{
  const f=$('lib-file').files[0];if(!f)throw Error('请先选择 WAV 文件');
  const title=($('lib-title').value.trim()||f.name.replace(/\.wav$/i,'')).slice(0,30);
  const r=await call('/upload?title='+encodeURIComponent(title),'PUT',f,true);$('lib-file').value='';say('已上传并发布：'+r.item.title);await load();
}));
guard(load);setInterval(()=>{if(!busy)guard(load);},15000);
})();
`;
