const fs=require('fs'),path=require('path'),assert=require('assert/strict'),vm=require('vm');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8'),main=read('web前端/main.js'),kill=read('web前端/kill.js'),cpp=read('DNFGameCaptureDlg.cpp');
for(const [source,name] of [[main,'main'],[kill,'kill']]){
 const start=source.indexOf('const KILL_DISPLAY_LAYOUT_DEFAULTS ='),end=source.indexOf('\n};',start)+3;
 const ctx={KILL_DISPLAY_DEFAULT_SKIN:3};vm.createContext(ctx);vm.runInContext(source.slice(start,end)+'\nthis.defaults=KILL_DISPLAY_LAYOUT_DEFAULTS;',ctx);
 assert.equal(ctx.defaults.fxVoice,0,name);assert.equal(ctx.defaults.fxVoiceOn,0,name);assert.equal(ctx.defaults.fxFullscreen,0,name);
}
assert(/\{ "fxFullscreen",\s+0,/.test(cpp));assert(/\{ "fxVoiceOn",\s+0,/.test(cpp));assert(cpp.includes('L"默认（LOL音效播报）"'));
assert(read('VoiceCloudCache.h').includes('index == 0 ? "lol-announcer"'));assert(main.includes('默认（LOL音效播报）'));
const keys=main.slice(main.indexOf('const FX_MANAGER_KEYS ='),main.indexOf('// 云端音色列表'));
for(const key of ['fxVoice','fxVoiceOn','fxFullscreen'])assert(keys.includes("'"+key+"'"));
const meta=JSON.parse(read('docs/lol-clean-edits.json')),crypto=require('crypto');
for(const [key,v]of Object.entries(meta.mapping)){const bytes=fs.readFileSync(path.join(root,'web前端/voice/lol-announcer',key+'.wav'));assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'),v.clean);}
assert.equal(Object.keys(meta.mapping).length,28);assert.equal(new Set(Object.values(meta.mapping).map(v=>v.clean)).size,7);
console.log('PASS: stable LOL default, fresh voice/fullscreen OFF, reset disables both, 28 cleaned slots / 7 exact hashes.');
