const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),html=fs.readFileSync(path.join(root,'web前端/index.html'),'utf8'),js=fs.readFileSync(path.join(root,'web前端/main.js'),'utf8'),css=fs.readFileSync(path.join(root,'web前端/style.css'),'utf8');
assert.equal((html.match(/id="btn-random-teams"/g)||[]).length,1);
const row=html.slice(html.indexOf('<div class="control-row control-row-match">'),html.indexOf('<div class="control-row control-row-aux">'));
assert(row.includes('id="btn-random-teams"'));assert(row.indexOf('id="btn-monitor"')<row.indexOf('id="btn-random-teams"'));assert(row.indexOf('id="btn-random-teams"')>row.indexOf('id="btn-pro"'));
assert(html.includes('aria-controls="random-tool-overlay" aria-expanded="false"'));assert(css.includes('#btn-random-teams.main-random-action'));assert(css.includes('container-name: match-workspace'));assert(css.includes('display: flex; flex-direction: row; flex-wrap: nowrap;'));assert(!css.includes('grid-template-columns: 1.3fr 1.45fr repeat(4, 1fr)'));assert(css.includes('margin-left: auto; box-shadow: 0 3px 9px'));
assert(js.includes("document.getElementById('btn-random-teams')?.addEventListener('click', openRandomTool)"));
const version=js.match(/const WEB_LAYOUT_VERSION = '([^']+)'/)[1];assert.equal(version,'20260930-5.5.4-2');for(const value of ['content="'+version+'"','main.js?v='+version])assert(html.includes(value));
function element(){return {attrs:{},active:false,querySelector(){return null},setAttribute(k,v){this.attrs[k]=v},classList:{add(){},remove(){}}};}
const overlay=element(),button=element(),suggestions=element(),calls=[];overlay.classList={add(v){overlay.active=v==='active'},remove(){overlay.active=false}};
const ctx={isMonitoring:false,document:{getElementById:id=>({'random-tool-overlay':overlay,'btn-random-teams':button,'random-roster-suggestions':suggestions}[id])},showAlert:m=>calls.push(['alert',m]),resetRandomGroupTransientUi:()=>calls.push(['reset-ui']),resetRandomToolToInitialState:o=>calls.push(['reset-roster',o]),updateRandomRosterSuggestions:()=>calls.push(['suggestions']),setMoreControlsOpen:v=>calls.push(['more-menu',v])};
vm.createContext(ctx);vm.runInContext(js.slice(js.indexOf('function openRandomTool()'),js.indexOf('function shouldPreserveNoAliasInput')),ctx);
ctx.openRandomTool();assert(overlay.active);assert.equal(overlay.attrs['aria-hidden'],'false');assert.equal(button.attrs['aria-expanded'],'true');assert(calls.some(c=>c[0]==='reset-roster'&&c[1].notify===false));assert(calls.some(c=>c[0]==='more-menu'&&c[1]===false));
assert(!calls.some(c=>c[0]==='suggestions'));ctx.closeRandomTool();assert(!overlay.active);assert.equal(overlay.attrs['aria-hidden'],'true');assert.equal(button.attrs['aria-expanded'],'false');
calls.length=0;ctx.isMonitoring=true;ctx.openRandomTool();assert(!overlay.active);assert.equal(calls.length,1);assert(calls[0][0]==='alert'&&calls[0][1].includes('停止监控'));
console.log('PASS: single main-screen grouping/draw entry, toolbar order, accessible dialog state, existing click binding and monitoring/roster behavior, consistent revision.');
