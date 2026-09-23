const assert = require('node:assert/strict');
const http = require('node:http');
const { spawn } = require('node:child_process');
const { once } = require('node:events');
const { Server } = require('../cloud-match-server/node_modules/socket.io');

async function scenario(executable, mode) {
    const server = http.createServer((req, res) => { res.writeHead(404); res.end(); });
    const io = new Server(server);
    const attempts = [];
    let joins = 0;
    io.use((socket, next) => {
        const auth = socket.handshake.auth;
        attempts.push(auth);
        let code = '';
        if (mode === 'invalid-device') code = 'authentication_failed';
        else if (mode === 'invalid-schema' || (mode === 'legacy' && 'licenseSessionToken' in auth)) code = 'invalid_auth';
        if (!code) return next();
        const error = new Error(code);
        error.data = { code };
        next(error);
    });
    io.on('connection', socket => {
        socket.on('broadcaster:join', (payload, ack) => {
            ++joins;
            ack({ ok: true, pool: 'unified', broadcasterName: payload.broadcasterName });
            if (joins === 1) setTimeout(() => socket.conn.close(), 25);
        });
    });
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const expected = mode === 'invalid-device' ? 'authentication_failed' : mode === 'invalid-schema' ? 'invalid_auth' : 'connected';
    try {
        const child = spawn(executable, ['--handshake', `http://127.0.0.1:${server.address().port}`, expected],
            { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
        let output = '';
        child.stdout.on('data', chunk => { output += chunk; });
        child.stderr.on('data', chunk => { output += chunk; });
        const timeout = setTimeout(() => child.kill(), 15000);
        let code;
        try { [code] = await once(child, 'exit'); } finally { clearTimeout(timeout); }
        assert.equal(code, 0, `${mode}: ${output}`);
        assert.equal(attempts.length, mode === 'legacy' ? 3 : mode === 'invalid-device' ? 1 : 2);
        for (const auth of attempts) {
            assert.equal(auth.deviceId, 'compat-device');
            assert.equal(auth.deviceToken, 'compat-device-token');
        }
        assert.equal(attempts[0].licenseSessionToken, 'compat-license-session');
        if (mode === 'modern') assert.equal(attempts[1].licenseSessionToken, 'compat-license-session');
        if (mode === 'legacy' || mode === 'invalid-schema') {
            for (const auth of attempts.slice(1)) assert.deepEqual(Object.keys(auth).sort(), ['deviceId', 'deviceToken', 'protocolVersion']);
        }
        console.log(`Native handshake integration passed: ${mode}`);
    } finally {
        await new Promise(resolve => io.close(resolve));
    }
}

(async () => {
    assert.ok(process.argv[2], 'Pass the native client test executable');
    for (const mode of ['legacy', 'modern', 'invalid-device', 'invalid-schema']) await scenario(process.argv[2], mode);
})().catch(error => { console.error(error); process.exitCode = 1; });
