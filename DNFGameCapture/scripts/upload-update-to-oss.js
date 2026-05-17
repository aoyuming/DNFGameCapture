#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');

function usage() {
    return [
        'Usage:',
        '  node scripts/upload-update-to-oss.js [--release-dir DIR] [--update-file FILE] [--config-path FILE] [--dry-run]',
        '',
        'Defaults:',
        '  release dir: ../x64/Release relative to repo root',
        '  update file: update_v2.txt inside release dir',
        '  config: env vars first, then a local admin.config.json discovered under the repo'
    ].join('\n');
}

function fail(message) {
    console.error(`ERROR: ${message}`);
    process.exit(1);
}

function parseArgs(argv) {
    const args = {
        releaseDir: '',
        updateFile: '',
        configPath: '',
        dryRun: false
    };

    for (let i = 0; i < argv.length; i += 1) {
        const arg = argv[i];
        if (arg === '--dry-run') {
            args.dryRun = true;
        } else if (arg === '--release-dir') {
            args.releaseDir = argv[++i] || '';
        } else if (arg === '--update-file') {
            args.updateFile = argv[++i] || '';
        } else if (arg === '--config-path') {
            args.configPath = argv[++i] || '';
        } else if (arg === '--help' || arg === '-h') {
            console.log(usage());
            process.exit(0);
        } else {
            fail(`Unknown argument: ${arg}\n${usage()}`);
        }
    }

    return args;
}

function readTextNoBom(filePath) {
    const buffer = fs.readFileSync(filePath);
    return buffer.toString('utf8').replace(/^\uFEFF/, '');
}

function getImmediateDirs(rootDir) {
    if (!fs.existsSync(rootDir)) return [];
    return fs.readdirSync(rootDir, { withFileTypes: true })
        .filter(entry => entry.isDirectory())
        .map(entry => path.join(rootDir, entry.name));
}

function discoverAdminBase(repoRoot, explicitConfigPath) {
    const candidates = [];

    if (explicitConfigPath) {
        candidates.push(path.dirname(path.resolve(explicitConfigPath)));
    }

    if (process.env.ADMIN_CONFIG_PATH) {
        candidates.push(path.dirname(path.resolve(process.env.ADMIN_CONFIG_PATH)));
    }

    candidates.push(repoRoot);
    candidates.push(...getImmediateDirs(repoRoot));

    const seen = new Set();
    for (const candidate of candidates) {
        const resolved = path.resolve(candidate);
        if (seen.has(resolved)) continue;
        seen.add(resolved);

        const configPath = path.join(resolved, 'admin.config.json');
        const modulePath = path.join(resolved, 'node_modules', 'ali-oss');
        if (fs.existsSync(configPath) || fs.existsSync(modulePath)) {
            return resolved;
        }
    }

    return repoRoot;
}

function resolveConfigPath(repoRoot, adminBase, explicitConfigPath) {
    if (explicitConfigPath) return path.resolve(explicitConfigPath);
    if (process.env.ADMIN_CONFIG_PATH) return path.resolve(process.env.ADMIN_CONFIG_PATH);

    const direct = path.join(adminBase, 'admin.config.json');
    if (fs.existsSync(direct)) return direct;

    for (const dir of getImmediateDirs(repoRoot)) {
        const candidate = path.join(dir, 'admin.config.json');
        if (fs.existsSync(candidate)) return candidate;
    }

    return direct;
}

function loadOssModule(adminBase) {
    try {
        return require('ali-oss');
    } catch (_) {
        const localModule = path.join(adminBase, 'node_modules', 'ali-oss');
        if (fs.existsSync(localModule)) return require(localModule);
    }

    for (const dir of getImmediateDirs(path.dirname(adminBase))) {
        const localModule = path.join(dir, 'node_modules', 'ali-oss');
        if (fs.existsSync(localModule)) return require(localModule);
    }

    fail('Cannot find ali-oss. Run npm install in the admin tool directory first.');
}

function loadOssConfig(configPath) {
    let localConfig = {};
    if (fs.existsSync(configPath)) {
        localConfig = JSON.parse(fs.readFileSync(configPath, 'utf8'));
    }

    const config = {
        region: process.env.OSS_REGION || localConfig.region || 'oss-cn-beijing',
        accessKeyId: process.env.ALIBABA_CLOUD_ACCESS_KEY_ID || localConfig.accessKeyId || '',
        accessKeySecret: process.env.ALIBABA_CLOUD_ACCESS_KEY_SECRET || localConfig.accessKeySecret || '',
        stsToken: process.env.ALIBABA_CLOUD_SECURITY_TOKEN || localConfig.stsToken || undefined,
        bucket: process.env.OSS_BUCKET || localConfig.bucket || 'dnf-capture-update'
    };

    if (!config.accessKeyId || !config.accessKeySecret) {
        fail(`Missing OSS credentials. Set environment variables or create admin.config.json. Checked: ${configPath}`);
    }

    return config;
}

function parseUpdateFile(updateFile, releaseDir) {
    if (!fs.existsSync(updateFile)) {
        fail(`Missing update file: ${updateFile}`);
    }

    const updateText = readTextNoBom(updateFile);
    const lines = updateText.split(/\r?\n/);
    const version = (lines[0] || '').trim();
    const packageUrl = (lines[1] || '').trim();

    if (!version) fail('The first line of update_v2.txt is empty.');
    if (!packageUrl) fail('The second line of update_v2.txt is empty.');

    let parsedUrl;
    try {
        parsedUrl = new URL(packageUrl);
    } catch (_) {
        fail(`The second line of update_v2.txt is not a valid URL: ${packageUrl}`);
    }

    const zipName = path.basename(parsedUrl.pathname);
    if (!/^update_v.+\.zip$/i.test(zipName)) {
        fail(`Package URL must point to update_v*.zip, got: ${zipName}`);
    }

    const zipPath = path.join(releaseDir, zipName);
    if (!fs.existsSync(zipPath)) {
        fail(`Missing package file: ${zipPath}`);
    }

    const zipStat = fs.statSync(zipPath);
    if (!zipStat.isFile() || zipStat.size <= 0) {
        fail(`Package file is empty or invalid: ${zipPath}`);
    }

    return {
        version,
        packageUrl,
        zipName,
        zipPath,
        zipSize: zipStat.size,
        updateText
    };
}

function maskKey(value) {
    if (!value) return '(empty)';
    if (value.length <= 8) return '****';
    return `${value.slice(0, 4)}****${value.slice(-4)}`;
}

async function main() {
    const args = parseArgs(process.argv.slice(2));
    const repoRoot = path.resolve(__dirname, '..');
    const releaseDir = path.resolve(args.releaseDir || path.join(repoRoot, '..', 'x64', 'Release'));
    const updateFile = path.resolve(args.updateFile || path.join(releaseDir, 'update_v2.txt'));
    const adminBase = discoverAdminBase(repoRoot, args.configPath);
    const configPath = resolveConfigPath(repoRoot, adminBase, args.configPath);
    const OSS = loadOssModule(adminBase);
    const config = loadOssConfig(configPath);
    const update = parseUpdateFile(updateFile, releaseDir);

    console.log('DNF update upload');
    console.log(`Version      : ${update.version}`);
    console.log(`Package      : ${update.zipName}`);
    console.log(`Package size : ${update.zipSize}`);
    console.log(`Update file  : ${updateFile}`);
    console.log(`Release dir  : ${releaseDir}`);
    console.log(`OSS bucket   : ${config.bucket}`);
    console.log(`OSS region   : ${config.region}`);
    console.log(`Access key   : ${maskKey(config.accessKeyId)}`);

    if (args.dryRun) {
        console.log('Dry run      : no files uploaded');
        return;
    }

    const client = new OSS(config);

    console.log(`Uploading    : ${update.zipName}`);
    await client.put(update.zipName, update.zipPath);

    console.log('Uploading    : update_v2.txt');
    await client.put('update_v2.txt', Buffer.from(update.updateText, 'utf8'));

    const remoteUpdate = await client.get('update_v2.txt');
    const remoteBuffer = Buffer.isBuffer(remoteUpdate.content)
        ? remoteUpdate.content
        : Buffer.from(remoteUpdate.content);
    const remoteText = remoteBuffer.toString('utf8').replace(/^\uFEFF/, '');
    const remoteLines = remoteText.split(/\r?\n/);
    const remoteVersion = (remoteLines[0] || '').trim();
    const remoteUrl = (remoteLines[1] || '').trim();
    const firstBytes = Array.from(remoteBuffer.slice(0, 3))
        .map(byte => byte.toString(16).padStart(2, '0'))
        .join(' ');

    if (firstBytes.toLowerCase() === 'ef bb bf') {
        fail('Remote update_v2.txt still has a UTF-8 BOM.');
    }
    if (remoteVersion !== update.version) {
        fail(`Remote version mismatch: ${remoteVersion} != ${update.version}`);
    }
    if (remoteUrl !== update.packageUrl) {
        fail(`Remote package URL mismatch: ${remoteUrl} != ${update.packageUrl}`);
    }

    const remoteHead = await client.head(update.zipName);
    const remoteSize = Number(remoteHead.res.headers['content-length'] || 0);
    if (remoteSize !== update.zipSize) {
        fail(`Remote package size mismatch: ${remoteSize} != ${update.zipSize}`);
    }

    console.log('Verified     : update_v2.txt');
    console.log(`Remote line1 : ${remoteVersion}`);
    console.log(`Remote line2 : ${remoteUrl}`);
    console.log(`Remote zip   : ${update.zipName} (${remoteSize})`);
}

main().catch(error => {
    fail(error && error.message ? error.message : String(error));
});
