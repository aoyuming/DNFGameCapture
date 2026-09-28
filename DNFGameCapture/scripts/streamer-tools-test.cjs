// Offline contracts and pure preview selector. No app launch, audio, API calls or config writes.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const js=read('web前端/main.js'),html=read('web前端/index.html'),cpp=read('DNFGameCaptureDlg.cpp');
for(const id of ['btn-fx-manager','btn-kill-display-toggle','output-seat-label-toggle','fxm-preview-skin','fxm-preview-voice','fxm-preview-event'])assert.equal(html.split('id="'+id+'"').length-1,1,id);
assert(html.includes('>主播工具</button>'));assert(html.includes('>txt输出选人顺序</strong>'));
assert(html.indexOf('id="output-seat-label-toggle"')>html.indexOf('id="streamer-pane-display"'));
assert(html.includes('data-streamer-pane="effects" hidden'));assert(html.includes('data-streamer-pane="voice" hidden'));
assert(js.includes("action: 'cmd_set_kill_display_visible', visible: this.checked"));assert(cpp.includes('if (j.value("visible", false)) OpenKillDisplayWindow();'));
assert(js.includes('btn.checked = isKillDisplayWindowVisible;'));
const fields={'fxm-preview-skin':{value:'3'},'fxm-preview-voice':{value:'6'},'fxm-preview-event':{value:'victory'}};
const c={document:{getElementById:id=>fields[id]},killVoices:[{index:0},{index:2},{index:6}]};vm.createContext(c);
const a=js.indexOf('function getVoicePreviewSelection('),b=js.indexOf('function formatFxManagerValue(',a);vm.runInContext(js.slice(a,b),c);
const layout={skin:8,fxVoice:2,fxVoiceOn:1};const before=JSON.stringify(layout);let result=c.getVoicePreviewSelection(layout);
assert.equal(result.skin,3);assert.equal(result.voice,6);assert.equal(result.event,'victory');assert.equal(JSON.stringify(layout),before);
for(let skin=0;skin<9;skin++){fields['fxm-preview-skin'].value=String(skin);assert.equal(c.getVoicePreviewSelection(layout).skin,skin);}
fields['fxm-preview-skin'].value='-1';fields['fxm-preview-voice'].value='-1';result=c.getVoicePreviewSelection(layout);assert.equal(result.skin,8);assert.equal(result.voice,2);
fields['fxm-preview-skin'].value='999';fields['fxm-preview-voice'].value='999';fields['fxm-preview-event'].value='invalid';result=c.getVoicePreviewSelection(layout);assert.equal(result.skin,8);assert.equal(result.voice,2);assert.equal(result.event,'victory');
const preview=cpp.slice(cpp.indexOf('else if (action == "cmd_kill_voice_test")'),cpp.indexOf('else if (action == "cmd_toggle_kill_display")'));
assert(preview.includes('++g_killVoicePlaybackEpoch;'));assert(preview.includes('cmd_stop_kill_voice_test'));assert(!preview.includes('WritePrivateProfile'));assert(!preview.includes('SetPolicy('));
assert(read('DNFGameCaptureDlg.h').includes('#define CURRENT_VERSION L"5.5.3"'));
const rc=fs.readFileSync(path.join(root,'DNFGameCapture.rc'));assert.equal(rc[0],255);assert.equal(rc[1],254);const text=rc.toString('utf16le');assert(text.includes('FILEVERSION 5,5,3,0'));assert(text.includes('PRODUCTVERSION 5,5,3,0'));
assert(read('scripts/installer/DNFGameCapture-setup.iss').includes('#define AppVersion "5.5.3"'));assert(read('scripts/package-production-client.ps1').includes('5.5.3.0'));
const webVersion=js.match(/const WEB_LAYOUT_VERSION = '([^']+)'/)[1];assert(webVersion.startsWith('20260929-5.5.3-'));assert(html.includes('content="'+webVersion+'"'));assert(html.includes('main.js?v='+webVersion));
console.log('PASS: unified tools/TXT placement, explicit show/hide, independent preview across nine skins, fallback, audio cancellation, no live-setting mutation, consistent 5.5.3 version.');
