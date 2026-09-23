#!/usr/bin/env node
'use strict';

const OSS = require('ali-oss');
const fs = require('fs');
const path = require('path');
const readline = require('readline');

const UPDATE_LIST_KEY = 'update_v2.txt';

let ossClient = null;
let cachedConfig = null;

function getRuntimeDir() {
    return process.pkg ? path.dirname(process.execPath) : __dirname;
}

function uniquePaths(paths) {
    const seen = new Set();
    const result = [];
    for (const value of paths) {
        if (!value) continue;
        const resolved = path.resolve(value);
        const key = resolved.toLowerCase();
        if (seen.has(key)) continue;
        seen.add(key);
        result.push(resolved);
    }
    return result;
}

function parentDirs(startPath) {
    const dirs = [];
    let current = path.resolve(startPath);
    while (true) {
        dirs.push(current);
        const parent = path.dirname(current);
        if (parent === current) break;
        current = parent;
    }
    return dirs;
}

function getSearchRoots() {
    return uniquePaths([
        process.cwd(),
        getRuntimeDir(),
        __dirname,
        ...parentDirs(process.cwd()),
        ...parentDirs(getRuntimeDir()),
        ...parentDirs(__dirname)
    ]);
}

function readTextNoBom(filePath) {
    return fs.readFileSync(filePath).toString('utf8').replace(/^\uFEFF/, '');
}

function formatBytes(bytes) {
    const value = Number(bytes) || 0;
    if (value >= 1024 * 1024 * 1024) return `${(value / 1024 / 1024 / 1024).toFixed(2)} GB`;
    if (value >= 1024 * 1024) return `${(value / 1024 / 1024).toFixed(2)} MB`;
    if (value >= 1024) return `${(value / 1024).toFixed(2)} KB`;
    return `${value} B`;
}

function formatDate(value) {
    if (!value) return '-';
    const date = value instanceof Date ? value : new Date(value);
    if (Number.isNaN(date.getTime())) return String(value);
    return date.toLocaleString('zh-CN', { hour12: false });
}

function maskKey(value) {
    const text = String(value || '');
    if (!text) return '(空)';
    if (text.length <= 8) return '****';
    return `${text.slice(0, 4)}****${text.slice(-4)}`;
}

function findDefaultUpdateFile() {
    const envPath = process.env.UPDATE_FILE || process.env.DNF_UPDATE_FILE;
    if (envPath && fs.existsSync(envPath)) return path.resolve(envPath);

    for (const root of getSearchRoots()) {
        const candidates = [
            path.join(root, UPDATE_LIST_KEY),
            path.join(root, 'x64', 'Release', UPDATE_LIST_KEY),
            path.join(root, '..', 'x64', 'Release', UPDATE_LIST_KEY)
        ];
        for (const candidate of candidates) {
            if (fs.existsSync(candidate)) return path.resolve(candidate);
        }
    }

    return '';
}

function findDefaultConfigPath() {
    const envPath = process.env.ADMIN_CONFIG_PATH;
    if (envPath && fs.existsSync(envPath)) return path.resolve(envPath);

    for (const root of getSearchRoots()) {
        const candidates = [
            path.join(root, 'admin.config.json'),
            path.join(root, '秘钥后台管理', 'admin.config.json')
        ];
        for (const candidate of candidates) {
            if (fs.existsSync(candidate)) return path.resolve(candidate);
        }
    }

    return path.join(getRuntimeDir(), 'admin.config.json');
}

function parseUpdateFile(updateFile) {
    if (!updateFile || !fs.existsSync(updateFile)) {
        throw new Error(`找不到更新清单：${updateFile || '(未指定)'}`);
    }

    const updateText = readTextNoBom(updateFile);
    const lines = updateText.split(/\r?\n/);
    const version = (lines[0] || '').trim();
    const packageUrl = (lines[1] || '').trim();

    if (!version) throw new Error('update_v2.txt 第 1 行版本号为空。');
    if (!packageUrl) throw new Error('update_v2.txt 第 2 行下载地址为空。');

    let parsedUrl;
    try {
        parsedUrl = new URL(packageUrl);
    } catch (_) {
        throw new Error(`update_v2.txt 第 2 行不是有效 URL：${packageUrl}`);
    }

    const zipName = path.basename(parsedUrl.pathname);
    if (!/^update_v.+\.zip$/i.test(zipName)) {
        throw new Error(`下载地址必须指向 update_v*.zip，当前为：${zipName}`);
    }

    const releaseDir = path.dirname(updateFile);
    const zipPath = path.join(releaseDir, zipName);
    if (!fs.existsSync(zipPath)) {
        throw new Error(`找不到更新包：${zipPath}`);
    }

    const stat = fs.statSync(zipPath);
    if (!stat.isFile() || stat.size <= 0) {
        throw new Error(`更新包为空或不是文件：${zipPath}`);
    }

    return {
        updateFile,
        releaseDir,
        updateText,
        version,
        packageUrl,
        zipName,
        zipPath,
        zipSize: stat.size
    };
}

function loadOssConfig() {
    if (cachedConfig) return cachedConfig;

    const configPath = findDefaultConfigPath();
    let localConfig = {};
    if (fs.existsSync(configPath)) {
        localConfig = JSON.parse(fs.readFileSync(configPath, 'utf8'));
    }

    const config = {
        region: process.env.OSS_REGION || localConfig.region || 'oss-cn-beijing',
        bucket: process.env.OSS_BUCKET || localConfig.bucket || 'dnf-capture-update',
        accessKeyId: process.env.ALIBABA_CLOUD_ACCESS_KEY_ID || localConfig.accessKeyId || '',
        accessKeySecret: process.env.ALIBABA_CLOUD_ACCESS_KEY_SECRET || localConfig.accessKeySecret || '',
        stsToken: process.env.ALIBABA_CLOUD_SECURITY_TOKEN || localConfig.stsToken || undefined,
        configPath
    };

    if (!config.accessKeyId || !config.accessKeySecret) {
        throw new Error(`缺少 OSS 凭证。请设置环境变量，或把 admin.config.json 放到程序同目录。当前查找路径：${configPath}`);
    }

    cachedConfig = config;
    return config;
}

function getClient() {
    if (!ossClient) {
        const config = loadOssConfig();
        ossClient = new OSS({
            region: config.region,
            bucket: config.bucket,
            accessKeyId: config.accessKeyId,
            accessKeySecret: config.accessKeySecret,
            stsToken: config.stsToken
        });
    }
    return ossClient;
}

async function ask(rl, prompt) {
    return new Promise(resolve => rl.question(prompt, answer => resolve(String(answer || '').trim())));
}

async function pause(rl) {
    await ask(rl, '\n按回车返回菜单...');
}

async function resolveUpdateFileInteractive(rl) {
    const found = findDefaultUpdateFile();
    if (found) return found;

    const input = await ask(rl, '未自动找到 update_v2.txt，请输入完整路径：');
    if (!input) throw new Error('未提供 update_v2.txt 路径。');
    return path.resolve(input.replace(/^"|"$/g, ''));
}

function printTitle() {
    console.clear();
    console.log('========================================');
    console.log(' DNF 自动更新包 OSS 上传工具');
    console.log('========================================');
}

function printMenu() {
    console.log('\n请选择操作：');
    console.log('  1. 查看本地更新信息');
    console.log('  2. 上传当前更新包');
    console.log('  3. 查看远端文件列表');
    console.log('  4. 校验远端当前版本');
    console.log('  0. 退出');
}

function printConfigSummary(config) {
    console.log(`OSS 区域：${config.region}`);
    console.log(`OSS Bucket：${config.bucket}`);
    console.log(`配置文件：${config.configPath}`);
    console.log(`AccessKey：${maskKey(config.accessKeyId)}`);
}

function printLocalUpdate(update) {
    console.log('\n本地更新信息：');
    console.log(`版本号：${update.version}`);
    console.log(`更新清单：${update.updateFile}`);
    console.log(`下载地址：${update.packageUrl}`);
    console.log(`更新包：${update.zipPath}`);
    console.log(`更新包大小：${formatBytes(update.zipSize)} (${update.zipSize})`);
}

async function showLocalInfo(rl) {
    console.log('\n[1/2] 查找本地 update_v2.txt...');
    const updateFile = await resolveUpdateFileInteractive(rl);
    console.log('[2/2] 解析更新包信息...');
    const update = parseUpdateFile(updateFile);
    printLocalUpdate(update);
}

async function verifyRemoteUpdate(update) {
    const client = getClient();
    console.log('[1/3] 读取远端 update_v2.txt...');
    const remoteUpdate = await client.get(UPDATE_LIST_KEY);
    const remoteBuffer = Buffer.isBuffer(remoteUpdate.content)
        ? remoteUpdate.content
        : Buffer.from(remoteUpdate.content);
    const firstBytes = Array.from(remoteBuffer.slice(0, 3))
        .map(byte => byte.toString(16).padStart(2, '0'))
        .join(' ');
    if (firstBytes.toLowerCase() === 'ef bb bf') {
        throw new Error('远端 update_v2.txt 仍带 UTF-8 BOM。');
    }

    const remoteText = remoteBuffer.toString('utf8').replace(/^\uFEFF/, '');
    const remoteLines = remoteText.split(/\r?\n/);
    const remoteVersion = (remoteLines[0] || '').trim();
    const remoteUrl = (remoteLines[1] || '').trim();

    console.log('[2/3] 对比远端清单...');
    if (remoteVersion !== update.version) {
        throw new Error(`远端版本不一致：${remoteVersion} != ${update.version}`);
    }
    if (remoteUrl !== update.packageUrl) {
        throw new Error(`远端下载地址不一致：${remoteUrl} != ${update.packageUrl}`);
    }

    console.log('[3/3] 检查远端更新包大小...');
    const remoteHead = await client.head(update.zipName);
    const remoteSize = Number(remoteHead.res.headers['content-length'] || 0);
    if (remoteSize !== update.zipSize) {
        throw new Error(`远端更新包大小不一致：${remoteSize} != ${update.zipSize}`);
    }

    console.log('\n远端校验通过：');
    console.log(`版本号：${remoteVersion}`);
    console.log(`下载地址：${remoteUrl}`);
    console.log(`远端更新包：${update.zipName} (${formatBytes(remoteSize)})`);
}

async function uploadCurrentUpdate(rl) {
    console.log('\n[1/7] 读取 OSS 配置...');
    const config = loadOssConfig();
    printConfigSummary(config);

    console.log('\n[2/7] 查找本地 update_v2.txt...');
    const updateFile = await resolveUpdateFileInteractive(rl);

    console.log('[3/7] 解析并检查本地更新包...');
    const update = parseUpdateFile(updateFile);
    printLocalUpdate(update);

    const client = getClient();

    console.log(`\n[4/7] 上传更新包 ${update.zipName}...`);
    await client.put(update.zipName, update.zipPath);

    console.log(`[5/7] 上传 ${UPDATE_LIST_KEY}...`);
    await client.put(UPDATE_LIST_KEY, Buffer.from(update.updateText, 'utf8'));

    console.log('[6/7] 读取远端并校验...');
    await verifyRemoteUpdate(update);

    console.log('\n[7/7] 上传完成。');
}

async function listRemoteFiles(rl) {
    console.log('\n[1/2] 读取 OSS 配置...');
    const config = loadOssConfig();
    printConfigSummary(config);

    const prefix = await ask(rl, '\n请输入要查看的 prefix（直接回车列根目录）：');
    const client = getClient();
    const objects = [];
    let marker = undefined;

    console.log('\n[2/2] 正在读取远端文件列表...');
    do {
        const result = await client.list({
            prefix,
            marker,
            'max-keys': 100
        });
        if (Array.isArray(result.objects)) objects.push(...result.objects);
        marker = result.nextMarker;
    } while (marker);

    if (objects.length === 0) {
        console.log('没有找到匹配的远端文件。');
        return;
    }

    console.log(`\n远端文件列表（prefix="${prefix || '(根目录)'}"，共 ${objects.length} 个）：`);
    console.log('------------------------------------------------------------');
    for (const item of objects) {
        const size = String(formatBytes(item.size)).padStart(10, ' ');
        console.log(`${size}  ${formatDate(item.lastModified)}  ${item.name}`);
    }
}

async function verifyRemoteCurrent(rl) {
    console.log('\n[1/4] 查找本地 update_v2.txt...');
    const updateFile = await resolveUpdateFileInteractive(rl);

    console.log('[2/4] 解析本地版本...');
    const update = parseUpdateFile(updateFile);

    console.log('[3/4] 读取 OSS 配置...');
    const config = loadOssConfig();
    printConfigSummary(config);

    console.log('\n[4/4] 校验远端当前版本...');
    await verifyRemoteUpdate(update);
}

async function runAction(rl, action) {
    try {
        await action(rl);
    } catch (error) {
        console.log('\n操作失败：');
        console.log(error && error.message ? error.message : String(error));
    }
    await pause(rl);
}

async function main() {
    if (process.platform === 'win32') {
        process.title = 'DNF 更新包 OSS 上传工具';
    }

    const rl = readline.createInterface({
        input: process.stdin,
        output: process.stdout
    });

    while (true) {
        printTitle();
        printMenu();
        const choice = await ask(rl, '\n请输入序号：');

        if (choice === '0') break;
        if (choice === '1') await runAction(rl, showLocalInfo);
        else if (choice === '2') await runAction(rl, uploadCurrentUpdate);
        else if (choice === '3') await runAction(rl, listRemoteFiles);
        else if (choice === '4') await runAction(rl, verifyRemoteCurrent);
        else {
            console.log('无效选择，请重新输入。');
            await pause(rl);
        }
    }

    rl.close();
    console.log('已退出。');
}

main().catch(error => {
    console.log('\n程序异常：');
    console.log(error && error.message ? error.message : String(error));
    console.log('\n请按回车退出...');
    process.stdin.resume();
    process.stdin.once('data', () => process.exit(1));
});
