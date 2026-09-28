// Pure state-machine/contract regression. No network, paid synthesis or audio playback.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const source=read('web前端/kill.js'),start=source.indexOf('function createLeftVictoryTracker()'),end=source.indexOf('const KillFxTracker =',start);
const c={};vm.createContext(c);vm.runInContext(source.slice(start,end),c);const fresh=()=>c.createLeftVictoryTracker();
const state=(red,blue,flip=false)=>({redScore:red,blueScore:blue,isFlipped:flip});
let t=fresh();assert.equal(t.ingest(state(6,2)),null);assert.equal(t.ingest(state(7,2)).team,'red');assert.equal(t.ingest(state(8,2)),null);
t=fresh();t.ingest(state(1,6,true));assert.equal(t.ingest(state(1,7,true)).team,'blue');
t=fresh();t.ingest(state(6,6));assert.equal(t.ingest(state(6,7)),null);assert.equal(t.ingest(state(6,7,true)),null,'flip must not create a score edge');
t=fresh();assert.equal(t.ingest(state(7,1)),null);t.resync();assert.equal(t.ingest(state(7,1)),null,'reopen must not replay');
t=fresh();t.ingest(state(6,1));assert.equal(t.ingest(state(8,1)).team,'red','batched updates may jump over seven');assert.equal(t.ingest(state(6,1)),null);assert.equal(t.ingest(state(7,1)),null,'undo/redo must not duplicate victory');
t.ingest(state(0,0));t.ingest(state(6,2));assert.equal(t.ingest(state(7,2)).team,'red','reset re-arms next match');
t=fresh();t.ingest(state(6,2));assert.equal(t.ingest(state(7,2),false),null);assert.equal(t.ingest(state(7,2),true),null,'disabled/hidden crossing consumed');
t=fresh();t.ingest(state(6,2));assert.equal(t.ingest({}),null);assert.equal(t.ingest(state(NaN,2)),null);assert.equal(t.ingest(state(7,2)).team,'red');
t=fresh();t.ingest(state(5,6));assert.equal(t.ingest(state(5,7,true)).team,'blue','simultaneous flip + genuine score increment uses current left');
t=fresh();t.ingest(state(6,7));assert.equal(t.ingest(state(7,7)).team,'red','initial right-side seven must not consume left-side victory');
t=fresh();t.ingest(state(7,6,true));assert.equal(t.ingest(state(7,7,true)).team,'blue');
// Exercise the real queue integration, not just the score factory.
for (const victoryOn of [0,1]) {
 const jobs=[],played=[],sandbox={document:{hidden:false,querySelectorAll:()=>[]},killDisplaySettings:{layout:{fxEvtVictory:victoryOn}},KillFx:{play:e=>played.push(e),setEnabled:()=>{}},setTimeout:fn=>{jobs.push(fn);return jobs.length;},clearTimeout:()=>{}};
 vm.createContext(sandbox);vm.runInContext(source.slice(start,source.indexOf('/* ---------- 与记分板',start))+'\nthis.tracker=KillFxTracker;',sandbox);
 const players=(kills,ak,deaths)=>[{name:'Alpha',team:0,kills,akCount:ak,deaths:0,currentStreak:kills},{name:'Beta',team:1,kills:0,akCount:0,deaths,currentStreak:0}];
 sandbox.tracker.ingest({...state(6,2),players:players(0,0,0)});
 sandbox.tracker.ingest({...state(7,2),players:players(3,1,3)});
 while(jobs.length)jobs.shift()();
 assert(played.length>0,'final event should not be lost');
 assert(played.some(e=>e.level===(victoryOn?'victory':'ak')),'disabled victory must not swallow final AK');
 if(!victoryOn)assert(!played.some(e=>e.level==='victory'));
 if(victoryOn)assert.equal(played.length,1,'victory owns the final batch');
}

const cpp=read('DNFGameCaptureDlg.cpp'),dlg=read('KillFxDlg.cpp');
assert(cpp.includes('g_killFxClosedByUser.store(true)'));assert(cpp.includes('L"fxFullscreen", L"0", m_iniPath'));assert(cpp.includes('SetOnUserClose([this]()'));
assert(dlg.includes('if (m_onUserClose) m_onUserClose();'));assert(/void CKillFxDlg::OnCancel\(\)\s*\{\s*OnClose\(\);/.test(dlg));
assert(source.includes("if (level === 'victory') { victory(team); return; }"));assert(source.includes('victoryTracker.ingest(data, enabled && !document.hidden && killDisplaySettings?.layout?.fxEvtVictory !== 0)'));assert(source.includes('document.hidden || !window.chrome?.webview'));
assert(read('web前端/index.html').includes('fxEvtVictory'));assert(read('cloud-match-server/src/voices.ts').includes('LOL音效播报'));
console.log('PASS: physical-left red/blue, flip/reopen guards, 6→7/6→8, undo dedupe, reset, hidden/disabled, victory rendering and close-toggle contract.');
