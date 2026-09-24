'use strict';
// Compile the real native sizing/setter bodies against a small window adapter.
// Does not start the application or access cloud services / personal data.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const cpp = fs.readFileSync(path.join(root, 'WebScoreDlg.cpp'), 'utf8');
const host = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.cpp'), 'utf8');
const header = fs.readFileSync(path.join(root, 'WebScoreDlg.h'), 'utf8');
function method(name) {
    const start = cpp.indexOf(`void CWebScoreDlg::${name}(`);
    assert.ok(start >= 0, name);
    const open = cpp.indexOf('{', start);
    let depth = 1, end = open + 1;
    while (depth && end < cpp.length) {
        if (cpp[end] === '{') ++depth;
        if (cpp[end] === '}') --depth;
        ++end;
    }
    assert.equal(depth, 0);
    return cpp.slice(start, end);
}
const constants = ['kReferenceClientHeight','kAppearanceExtraClientHeight','kBroadcasterPreviewClientHeight','kPlayerIdentityClientHeight','kRealtimeSyncExtraClientHeight','kTargetVisualScale']
    .map(name => {
        const m = cpp.match(new RegExp(`constexpr (?:int|double) ${name} = [^;]+;`));
        assert.ok(m, name); return m[0];
    }).join('\n');
assert.ok(header.includes('bool m_realtimeSyncExpanded = false;'));
const broadcast = host.slice(host.indexOf('void CDNFGameCaptureDlg::BroadcastStateToWeb()'));
assert.ok(broadcast.indexOf('if (m_pWebDlg == nullptr) return;') < broadcast.indexOf('SetRealtimeSyncExpanded(m_cloudRealtimeFollowing)'));
assert.ok(broadcast.indexOf('SetRealtimeSyncExpanded(m_cloudRealtimeFollowing)') < broadcast.indexOf('json webMessage;'));
assert.ok(cpp.includes('if (targetWindowH > maxWindowH) targetWindowH = maxWindowH;'));
const source = `#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using std::max;
${constants}
int ScaleCssSizeToNativePixels(int value, double scale) { return static_cast<int>(std::lround(value * scale)); }
class CWebScoreDlg {
public:
 bool m_aliasPopoverExpanded=false, m_appearanceExpanded=false, m_broadcasterPreviewExpanded=false;
 bool m_playerIdentityExpanded=false, m_consolePanelExpanded=false, m_realtimeSyncExpanded=false;
 int width=0, height=0, calls=0;
 int GetReferenceClientWidth() const { return m_consolePanelExpanded ? 1400 : 1140; }
 void ResizeWindowForClientSize(int w,int h) { width=w; height=h; ++calls; }
 void WriteWebHostDiagnostics(const wchar_t*) {}
 void ApplyExpandedWindowSize();
 void SetRealtimeSyncExpanded(bool expanded);
};
${method('ApplyExpandedWindowSize')}
${method('SetRealtimeSyncExpanded')}
void check(bool value) { if (!value) { std::fputs("Realtime height check failed\\n", stderr); std::exit(1); } }
int main() {
 for (int mask=0; mask<32; ++mask) {
  CWebScoreDlg w;
  w.m_aliasPopoverExpanded=(mask&1)!=0; w.m_appearanceExpanded=(mask&2)!=0;
  w.m_broadcasterPreviewExpanded=(mask&4)!=0; w.m_playerIdentityExpanded=(mask&8)!=0;
  w.m_consolePanelExpanded=(mask&16)!=0;
  w.ApplyExpandedWindowSize();
  const int base=w.height, width=w.width, calls=w.calls;
  check(base>=600);
  w.SetRealtimeSyncExpanded(true); check(w.height==base+80 && w.width==width && w.calls==calls+1);
  for(int n=0;n<100;++n) w.SetRealtimeSyncExpanded(true);
  check(w.calls==calls+1);
  w.ApplyExpandedWindowSize(); check(w.height==base+80 && w.width==width);
  w.SetRealtimeSyncExpanded(false); check(w.height==base && w.width==width);
  const int stopped=w.calls; w.SetRealtimeSyncExpanded(false); check(w.calls==stopped);
 }
 CWebScoreDlg w; w.SetRealtimeSyncExpanded(true); check(w.height==680 && w.width==1425);
 w.m_appearanceExpanded=true; w.ApplyExpandedWindowSize(); check(w.height==1055);
 w.m_appearanceExpanded=false; w.ApplyExpandedWindowSize(); check(w.height==680);
 w.m_broadcasterPreviewExpanded=true; w.ApplyExpandedWindowSize(); check(w.height==1030);
 w.m_broadcasterPreviewExpanded=false; w.ApplyExpandedWindowSize(); check(w.height==680);
 w.SetRealtimeSyncExpanded(false); check(w.height==600);
 std::puts("Realtime height: 32 panel combinations, repeated states and panel transitions passed.");
}
`;
const out = path.join(root, 'build', 'realtime-height-test');
fs.mkdirSync(out, { recursive: true });
fs.writeFileSync(path.join(out, 'test.cpp'), source);
const compile = process.platform === 'win32'
    ? spawnSync('cl', ['/nologo','/std:c++17','/EHsc','/W4','/WX','test.cpp','/Fe:test.exe','/Fo:test.obj'], { cwd: out, encoding: 'utf8' })
    : spawnSync('g++', ['-std=c++17','-Wall','-Wextra','-Werror','test.cpp','-o','test'], { cwd: out, encoding: 'utf8' });
process.stdout.write(compile.stdout || ''); process.stderr.write(compile.stderr || '');
assert.equal(compile.status, 0, String(compile.error || 'compile failed'));
const run = spawnSync(path.join(out, process.platform === 'win32' ? 'test.exe' : 'test'), [], { cwd: out, encoding: 'utf8' });
process.stdout.write(run.stdout || ''); process.stderr.write(run.stderr || '');
assert.equal(run.status, 0, String(run.error || 'test failed'));
