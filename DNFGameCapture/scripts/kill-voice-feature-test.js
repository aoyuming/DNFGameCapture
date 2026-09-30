// Offline regression: no WebView, sound playback, network, or paid API.
const fs = require('fs'), path = require('path'), assert = require('assert'), vm = require('vm');
const root = path.resolve(__dirname, '..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8');
const cpp = read('DNFGameCaptureDlg.cpp'), main = read('web前端/main.js'), kill = read('web前端/kill.js');
const generator = read('scripts/generate-kill-voices.ps1');
for (const id of ['zh_female_wuzetian_mars_bigtts', 'zh_female_gufengshaoyu_uranus_bigtts', 'zh_female_gufengshaoyu_mars_bigtts', 'zh_female_jiaochuannv_uranus_bigtts']) {
    assert(cpp.includes(id) && generator.includes(id), id);
}
for (const label of ['魅惑女声', '古风雅韵 2.0', '古风雅韵 1.0', '武则天 1.0']) assert(cpp.includes(label) && read('cloud-match-server/src/voices.ts').includes(label));
assert(cpp.includes('g_voiceCloudCache.List()'));
assert(read('VoiceCloudCache.h').includes('If-None-Match'));
assert(main.includes('state.killVoiceSync'));
assert(!main.includes("id: 'doubao-gufeng2'"));
assert(/"lol-announcer",\s*L"默认（LOL音效播报）", true, nullptr/.test(cpp));
assert(/"fxVoiceOn",\s*0,/.test(cpp));
assert(/fxVoiceOn:\s*0/.test(main) && /fxVoiceOn:\s*0/.test(kill));
assert(cpp.includes('if (!g_killVoicePlaybackEnabled.load() || epoch != g_killVoicePlaybackEpoch.load()) return;'));
assert(cpp.includes('if (!enabled) ::PlaySoundW(nullptr, nullptr, 0);'));
assert(main.includes('voiceTest.disabled = !master || layout.fxVoiceOn !== 1;'));
assert(main.includes('if (layout.fxVoiceOn !== 1 || layout.fxEnabled === 0) return;'));
assert(!cpp.includes('openspeech.bytedance.com')); // runtime never calls the paid synthesis endpoint
assert(read('scripts/package-production-client.ps1').includes('$files += $relative'));
assert(read('scripts/installer/DNFGameCapture-setup.iss').includes('voice\\*.wav'));
const start = kill.indexOf('    const VOICE_EVENT =');
const end = kill.indexOf('    function playBig(', start);
assert(start > 0 && end > start);
let calls = [], timers = new Map(), next = 0;
const c = {
    enabled: true, KILL_FX_FULLSCREEN: false, KILL_VOICE_URL: '/api/voice/play',
    document: { hidden: false }, window: { chrome: { webview: {} } }, killDisplaySettings: {layout:{}},
    EVENT_KEY: {2:'fxEvtDouble', 3:'fxEvtTriple', first:'fxEvtFirst', shutdown:'fxEvtShutdown', revenge:'fxEvtRevenge', ak:'fxEvtAk'},
    setTimeout: fn => { timers.set(++next, fn); return next; }, clearTimeout: id => timers.delete(id),
    fetch: (url, options) => { calls.push({url, options}); return Promise.resolve(); }
};
vm.createContext(c); vm.runInContext(kill.slice(start, end), c);
const defaults = () => ({fxEnabled:1, fxVoiceOn:1, fxVoice:0, skin:3, fxFullscreen:0});
const flush = () => { const fns = [...timers.values()]; timers.clear(); fns.forEach(fn => fn()); };
const reset = () => { c.killDisplaySettings.layout = defaults(); c.enabled = true; c.KILL_FX_FULLSCREEN = false; c.window = {chrome:{webview:{}}}; calls = []; c.syncVoiceSettings(); };
reset(); c.scheduleKillVoice('double', 'ink', 0, 100); flush(); assert.equal(calls.length, 1); assert.equal(calls[0].options.method, 'POST');
reset(); c.scheduleKillVoice('double', 'ink', 0, 100); c.killDisplaySettings.layout.fxVoiceOn = 0; flush(); assert.equal(calls.length, 0);
reset(); c.scheduleKillVoice('double', 'ink', 0, 100); c.killDisplaySettings.layout.fxVoiceOn = 0; c.syncVoiceSettings(); c.killDisplaySettings.layout.fxVoiceOn = 1; c.syncVoiceSettings(); flush(); assert.equal(calls.length, 0);
for (const [key,value] of [['fxEnabled',0],['fxVoice',4],['skin',1],['fxFullscreen',1],['fxEvtDouble',0]]) {
    reset(); c.scheduleKillVoice('double','ink',0,100); c.killDisplaySettings.layout[key] = value; c.syncVoiceSettings(); flush(); assert.equal(calls.length,0,key);
}
reset(); c.window = {}; c.requestKillVoice('double','ink',0); assert.equal(calls.length,0,'OBS must not speak');
reset(); c.KILL_FX_FULLSCREEN = true; c.requestKillVoice('double','ink',0); assert.equal(calls.length,0,'hidden fullscreen window');
c.killDisplaySettings.layout.fxFullscreen = 1; c.requestKillVoice('double','ink',0); assert.equal(calls.length,1);
reset(); c.scheduleKillVoice('double','ink',0,100); c.cancelVoiceRequests(); flush(); assert.equal(calls.length,0);
console.log('PASS: voice catalog/defaults, muted preview/backend guards, delayed cancellation, fullscreen ownership, OBS silence, offline packaging.');
