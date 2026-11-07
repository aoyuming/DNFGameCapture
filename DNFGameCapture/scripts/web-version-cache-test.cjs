const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const read = (name) => fs.readFileSync(path.join(root, name), 'utf8');
const header = read('DNFGameCaptureDlg.h');
const main = read('web前端/main.js');
const html = read('web前端/index.html');
const webHost = read('WebScoreDlg.cpp');
const resource = fs.readFileSync(path.join(root, 'DNFGameCapture.rc')).toString('utf16le');

const nativeVersion = header.match(/#define CURRENT_VERSION L"([^"]+)"/)?.[1];
const webVersion = main.match(/const WEB_LAYOUT_VERSION = '.*?-(\d+\.\d+\.\d+)-/)?.[1];
const fullWebVersion = main.match(/const WEB_LAYOUT_VERSION = '([^']+)'/)?.[1];
assert.ok(nativeVersion, 'native client version must be defined');
assert.equal(webVersion, nativeVersion, 'web top-bar version must match the native client version');
assert.ok(/pageUri \+= L"\?appVersion=";[\s\S]{0,100}pageUri \+= CURRENT_VERSION;/.test(webHost),
  'WebView main document URL must change when the client version changes');
assert.ok(fullWebVersion, 'web cache version must be defined');
assert.ok(html.includes(`<meta name="dnf-web-layout-version" content="${fullWebVersion}">`),
  'HTML cache version must match the JavaScript cache version');
assert.ok(resource.includes(`DNFGameCapture，版本 ${nativeVersion}`), 'About dialog must show the current client version');
