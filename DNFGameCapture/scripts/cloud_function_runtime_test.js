'use strict';

const assert = require('assert');
const http = require('http');
const path = require('path');
const { spawn } = require('child_process');

const projectRoot = path.resolve(__dirname, '..');
const cloudFunction = path.join(projectRoot, '云函数', 'index.js');

function reservePort() {
    return new Promise((resolve, reject) => {
        const server = http.createServer();
        server.once('error', reject);
        server.listen(0, '127.0.0.1', () => {
            const port = server.address().port;
            server.close(() => resolve(port));
        });
    });
}

function requestHealth(port) {
    return new Promise((resolve, reject) => {
        const request = http.get({ hostname: '127.0.0.1', port, path: '/health' }, response => {
            let body = '';
            response.setEncoding('utf8');
            response.on('data', chunk => { body += chunk; });
            response.on('end', () => resolve({ statusCode: response.statusCode, body }));
        });
        request.setTimeout(3000, () => request.destroy(new Error('health request timeout')));
        request.on('error', reject);
    });
}

async function waitForHealth(child, port) {
    const deadline = Date.now() + 5000;
    let lastError;
    while (Date.now() < deadline) {
        if (child.exitCode !== null) {
            throw new Error(`cloud function exited before health check: ${child.exitCode}`);
        }
        try {
            return await requestHealth(port);
        } catch (error) {
            lastError = error;
            await new Promise(resolve => setTimeout(resolve, 100));
        }
    }
    throw lastError || new Error('health check timeout');
}

async function main() {
    const port = await reservePort();
    const child = spawn(process.execPath, [cloudFunction], {
        cwd: projectRoot,
        env: { ...process.env, PORT: String(port) },
        stdio: ['ignore', 'pipe', 'pipe']
    });
    let output = '';
    child.stdout.on('data', chunk => { output += chunk; });
    child.stderr.on('data', chunk => { output += chunk; });

    try {
        const health = await waitForHealth(child, port);
        assert.strictEqual(health.statusCode, 200);
        assert.deepStrictEqual(JSON.parse(health.body), { ok: true });
    } finally {
        child.kill();
    }

    const entryProbe = spawn(process.execPath, ['-e',
        `const runtime = require(${JSON.stringify(cloudFunction)}); ` +
        `process.exit(typeof runtime.handler === 'function' ? 0 : 2);`
    ], { cwd: projectRoot, stdio: 'pipe' });
    const entryExitCode = await new Promise(resolve => entryProbe.once('close', resolve));
    assert.strictEqual(entryExitCode, 0, `missing handler export\n${output}`);
}

main().then(() => {
    console.log('Cloud function runtime contract passed.');
}).catch(error => {
    console.error(error.stack || error);
    process.exitCode = 1;
});
