const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '../web\u524d\u7aef');
const fixture = () => {
    const listeners = [];
    const group = { groupId: 'fixture-group', source: 'manual', names: ['王大枪', '老王', '旋律'],
        ids: ['广东1白羽#枪炮师', '上海1夜雨#气功师', '北京1测试游戏ID#剑魂'] };
    const solo = { name: '独立选手', ids: ['广东1测试#散打'] };
    const state = {
        blueScore: 1, redScore: 2, startupReady: true, isAuthValid: true, authText: '本地测试',
        playerLibrary: { ready: true, loaded: true, busy: false, storage: 'sqlite-v2', revision: 1, error: '' },
        playerIdentity: { revision: 1, groups: [group], entries: [...group.names.map(name => ({name, ids: group.ids})), solo], exactMatches: [], overlapSuggestions: [
            { leftName: '王大枪', rightName: '独立选手', commonIdCount: 1 },
            { leftName: '老王', rightName: '独立选手', commonIdCount: 1 },
            { leftName: '旋律', rightName: '独立选手', commonIdCount: 1 }
        ] },
        fullAliasDB: {},
        players: Array.from({ length: 8 }, (_, i) => ({ team: i < 4 ? 0 : 1,
            name: i === 0 ? '王大枪' : i === 4 ? '独立选手' : '', aliases: i === 0 ? group.ids : i === 4 ? solo.ids : [],
            kills: 0, deaths: 0, akCount: 0 })),
        cloudMatch: { shouldPrompt: false, broadcasterName: '本地预览', broadcasters: [] },
        recentEvents: []
    };
    for (const entry of state.playerIdentity.entries) state.fullAliasDB[entry.name] = entry.ids.map(id => `(${id})`).join('');
    const emit = data => listeners.forEach(listener => listener({ data }));
    window.chrome = window.chrome || {};
    window.chrome.webview = {
        addEventListener(type, listener) { if (type === 'message') listeners.push(listener); },
        postMessage(command) {
            if (command.action === 'page_ready' || command.action === 'cmd_identity_refresh') {
                setTimeout(() => emit({ action: 'sync_state', data: state }), 50);
            } else if (command.action.startsWith('cmd_identity_')) {
                console.info('fixture command', command);
                setTimeout(() => emit({ action: 'identity_error', requestId: command.requestId,
                    message: '预览测试不写入真实选手库' }), 80);
            }
        }
    };
    setTimeout(() => emit({ action: 'sync_state', data: state }), 500);
};

const types = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
    '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.svg': 'image/svg+xml' };
const server = http.createServer((req, res) => {
    try {
        const pathname = decodeURIComponent(new URL(req.url, 'http://127.0.0.1').pathname);
        res.setHeader('Cache-Control', 'no-store');
        if (pathname === '/fixture.js') {
            res.setHeader('Content-Type', types['.js']);
            return res.end(`(${fixture.toString()})();`);
        }
        const file = path.resolve(root, pathname === '/' ? 'index.html' : `.${pathname}`);
        if (!file.startsWith(root + path.sep)) { res.writeHead(403); return res.end(); }
        res.setHeader('Content-Type', types[path.extname(file)] || 'application/octet-stream');
        if (path.extname(file) === '.html') {
            const html = fs.readFileSync(file, 'utf8').replace('</head>', '<script src="/fixture.js"></script></head>');
            res.end(html);
        } else res.end(fs.readFileSync(file));
    } catch { res.writeHead(404); res.end('Not found'); }
});
server.listen(Number(process.env.PORT || 58634), '127.0.0.1', () => {
    console.log(`Read-only fixture: http://127.0.0.1:${server.address().port}/`);
});
