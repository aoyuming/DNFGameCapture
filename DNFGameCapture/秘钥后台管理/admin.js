const OSS = require('ali-oss');
const readline = require('readline');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

let client = null;

function loadOssConfig() {
    const localConfigPath = path.join(__dirname, 'admin.config.json');
    let localConfig = {};

    if (fs.existsSync(localConfigPath)) {
        localConfig = JSON.parse(fs.readFileSync(localConfigPath, 'utf8'));
    }

    const config = {
        region: process.env.OSS_REGION || localConfig.region || 'oss-cn-beijing',
        accessKeyId: process.env.ALIBABA_CLOUD_ACCESS_KEY_ID || localConfig.accessKeyId || '',
        accessKeySecret: process.env.ALIBABA_CLOUD_ACCESS_KEY_SECRET || localConfig.accessKeySecret || '',
        stsToken: process.env.ALIBABA_CLOUD_SECURITY_TOKEN || localConfig.stsToken || undefined,
        bucket: process.env.OSS_BUCKET || localConfig.bucket || 'dnf-capture-update'
    };

    if (!config.accessKeyId || !config.accessKeySecret) {
        throw new Error('缺少 OSS 凭证。请设置环境变量 ALIBABA_CLOUD_ACCESS_KEY_ID / ALIBABA_CLOUD_ACCESS_KEY_SECRET，或创建 秘钥后台管理/admin.config.json。');
    }

    return config;
}

function getClient() {
    if (!client) client = new OSS(loadOssConfig());
    return client;
}

const PUBLIC_ALIAS_DB_KEY = 'shared-alias/public_alias_db.json';
const ADMIN_KEY_HASHES_KEY = 'shared-alias/admin_key_hashes.json';
const PENDING_ALIAS_PREFIX = 'alias-submissions/pending/';
const APPROVED_ALIAS_PREFIX = 'alias-submissions/approved/';
const REJECTED_ALIAS_PREFIX = 'alias-submissions/rejected/';

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});

const question = (query) => new Promise(resolve => rl.question(query, resolve));

function formatTime(sec) {
    if (sec === 0) return '未记录(旧版数据)';
    if (sec === 1) return '已封停';
    if (sec === 0xFFFFFFFF) return '永久有效';
    const d = new Date(sec * 1000);
    return d.toLocaleString('zh-CN', { hour12: false });
}

function formatUnixTime(sec) {
    if (!sec) return '未知时间';
    return new Date(sec * 1000).toLocaleString('zh-CN', { hour12: false });
}

function sha256(value) {
    return crypto.createHash('sha256').update(String(value || '')).digest('hex');
}

async function getLicense(key) {
    try {
        const result = await getClient().get(`licenses/${key}.txt`);
        const content = result.content.toString();
        const parts = content.split('|');
        return {
            exists: true,
            hwid: parts[0],
            expireTime: parts.length > 1 ? parseInt(parts[1], 10) : 0
        };
    } catch (e) {
        if (e.name === 'NoSuchKeyError') return { exists: false };
        throw e;
    }
}

async function saveLicense(key, hwid, expireTime) {
    const writeContent = `${hwid}|${expireTime}`;
    await getClient().put(`licenses/${key}.txt`, Buffer.from(writeContent));
}

async function getJsonObject(key, fallbackValue) {
    try {
        const result = await getClient().get(key);
        return JSON.parse(result.content.toString());
    } catch (e) {
        if (e.name === 'NoSuchKeyError') return fallbackValue;
        throw e;
    }
}

async function putJsonObject(key, value) {
    await getClient().put(
        key,
        Buffer.from(JSON.stringify(value, null, 2), 'utf8'),
        { headers: { 'Content-Type': 'application/json; charset=utf-8' } }
    );
}

function normalizeAliasArray(value) {
    const arr = Array.isArray(value)
        ? value
        : (typeof value === 'string' ? value.split(/[()（）、,，;；]/) : []);
    const out = [];
    const seen = new Set();
    for (const item of arr) {
        const alias = String(item || '').trim();
        if (!alias || seen.has(alias)) continue;
        seen.add(alias);
        out.push(alias);
    }
    return out;
}

function normalizeStringArray(value) {
    const arr = Array.isArray(value) ? value : [];
    const out = [];
    const seen = new Set();
    for (const item of arr) {
        const text = String(item || '').trim();
        if (!text || seen.has(text)) continue;
        seen.add(text);
        out.push(text);
    }
    return out;
}

function getSubmissionEntries(submission) {
    if (Array.isArray(submission?.entries)) {
        return submission.entries
            .map(item => ({
                mainName: String(item.mainName || '').trim(),
                aliases: normalizeAliasArray(item.aliases)
            }))
            .filter(item => item.mainName && item.aliases.length > 0);
    }

    if (submission?.mainName) {
        const mainName = String(submission.mainName || '').trim();
        const aliases = normalizeAliasArray(submission.aliases);
        return mainName && aliases.length > 0 ? [{ mainName, aliases }] : [];
    }

    return [];
}

function getSubmissionStats(submission) {
    const entries = getSubmissionEntries(submission);
    return {
        mainCount: entries.length,
        pairCount: entries.reduce((sum, item) => sum + item.aliases.length, 0)
    };
}

function normalizePublicDb(db) {
    const normalized = {
        version: Number(db?.version || 0),
        updatedAt: Number(db?.updatedAt || 0),
        players: {},
        audit: Array.isArray(db?.audit) ? db.audit : []
    };

    if (db?.players && typeof db.players === 'object') {
        for (const [mainName, aliases] of Object.entries(db.players)) {
            const cleanMain = String(mainName || '').trim();
            const cleanAliases = normalizeAliasArray(aliases);
            if (cleanMain && cleanAliases.length > 0) normalized.players[cleanMain] = cleanAliases;
        }
    }

    return normalized;
}

async function loadPublicAliasDb() {
    return normalizePublicDb(await getJsonObject(PUBLIC_ALIAS_DB_KEY, {
        version: 0,
        updatedAt: 0,
        players: {},
        audit: []
    }));
}

function normalizeAdminKeyConfig(config) {
    return {
        updatedAt: Number(config?.updatedAt || 0),
        keyHashes: normalizeStringArray(config?.keyHashes)
            .map(item => item.toLowerCase())
            .filter(item => /^[0-9a-f]{64}$/.test(item))
    };
}

async function loadAdminKeyConfig() {
    return normalizeAdminKeyConfig(await getJsonObject(ADMIN_KEY_HASHES_KEY, {
        updatedAt: 0,
        keyHashes: []
    }));
}

async function saveAdminKeyConfig(config) {
    await putJsonObject(ADMIN_KEY_HASHES_KEY, {
        updatedAt: Math.floor(Date.now() / 1000),
        keyHashes: normalizeAdminKeyConfig(config).keyHashes
    });
}

function findAliasOwner(publicDb, aliasName, expectedMainName) {
    for (const [mainName, aliases] of Object.entries(publicDb.players)) {
        if (mainName !== expectedMainName && aliases.includes(aliasName)) {
            return mainName;
        }
    }
    return '';
}

function removeAliasFromOtherOwners(publicDb, aliasName, expectedMainName) {
    for (const [mainName, aliases] of Object.entries(publicDb.players)) {
        if (mainName === expectedMainName) continue;
        publicDb.players[mainName] = aliases.filter(a => a !== aliasName);
        if (publicDb.players[mainName].length === 0) delete publicDb.players[mainName];
    }
}

async function listPendingAliasSubmissions() {
    const all = [];
    let marker = undefined;

    do {
        const params = { prefix: PENDING_ALIAS_PREFIX, 'max-keys': 100 };
        if (marker) params.marker = marker;
        const result = await getClient().list(params);
        const objects = (result.objects || []).filter(o => o.name.endsWith('.json'));
        all.push(...objects);
        marker = result.nextMarker;
    } while (marker && all.length < 1000);

    return all.sort((a, b) => a.name.localeCompare(b.name));
}

async function loadPendingSubmissionList() {
    const objects = await listPendingAliasSubmissions();
    const rows = [];

    for (const obj of objects.slice(0, 50)) {
        try {
            const data = await getJsonObject(obj.name, null);
            if (data) rows.push({ key: obj.name, data });
        } catch (e) {
            rows.push({ key: obj.name, data: { id: obj.name, error: e.message } });
        }
    }

    return rows;
}

function hasReviewableContent(row) {
    return getSubmissionStats(row.data).pairCount > 0;
}

async function cleanupEmptyPendingSubmissions() {
    const rows = await loadPendingSubmissionList();
    const emptyRows = rows.filter(row => !hasReviewableContent(row));

    if (emptyRows.length === 0) {
        console.log('✅ 没有空的待审核记录。');
        return;
    }

    console.log(`发现 ${emptyRows.length} 条空待审核记录。`);
    const confirm = await question('确认清理这些空记录？(y/n): ');
    if (confirm.toLowerCase() !== 'y') {
        console.log('已取消。');
        return;
    }

    for (let i = 0; i < emptyRows.length; i++) {
        await archiveSubmission(emptyRows[i], 'rejected', { rejectReason: '空投稿自动清理', emptyCleanup: true });
        process.stdout.write(`\r清理进度: ${i + 1}/${emptyRows.length}`);
    }
    console.log('\n✅ 空记录已清理完成。');
}

function printSubmissionSummary(row, index) {
    const s = row.data;
    const entries = getSubmissionEntries(s);
    const stats = getSubmissionStats(s);
    const sample = entries.length > 0
        ? `${entries[0].mainName} -> ${entries[0].aliases.slice(0, 3).join(', ')}`
        : '无明细';

    console.log(`${index}. ${s.id || row.key}`);
    console.log(`   时间: ${formatUnixTime(s.updatedAt || s.createdAt)} | 主号: ${stats.mainCount} | 小号: ${stats.pairCount} | 投稿人数: ${s.sourceCount || s.submitterKeyHashes?.length || 1}`);
    console.log(`   提交者: ${(s.submitterKeyHash || s.submitterKeyHashes?.[0] || '').slice(0, 12)}... | 样例: ${sample}`);
    if (Array.isArray(s.conflicts) && s.conflicts.length > 0) {
        console.log(`   ⚠️ 冲突: ${s.conflicts.length} 条`);
    }
}

function printSubmissionDetail(submission) {
    console.log('\n========== 投稿明细 ==========');
    console.log(`ID: ${submission.id}`);
    const stats = getSubmissionStats(submission);
    console.log(`提交时间: ${formatUnixTime(submission.createdAt)}`);
    console.log(`更新时间: ${formatUnixTime(submission.updatedAt || submission.createdAt)}`);
    console.log(`提交者哈希: ${submission.submitterKeyHash || (submission.submitterKeyHashes || []).join(', ')}`);
    console.log(`设备哈希: ${submission.hwidHash || (submission.hwidHashes || []).join(', ')}`);
    console.log(`数量: ${stats.mainCount} 个主号 / ${stats.pairCount} 个小号`);
    console.log('------------------------------');

    const entries = getSubmissionEntries(submission);
    entries.slice(0, 80).forEach((item, idx) => {
        console.log(`${idx + 1}. ${item.mainName} = ${normalizeAliasArray(item.aliases).join(' / ')}`);
    });

    if (entries.length > 80) {
        console.log(`...还有 ${entries.length - 80} 条未显示`);
    }

    if (Array.isArray(submission.conflicts) && submission.conflicts.length > 0) {
        console.log('------------------------------');
        console.log('冲突项：');
        submission.conflicts.slice(0, 30).forEach(c => {
            console.log(` - ${c.aliasName}: 当前属于 ${c.currentOwner}, 投稿要求属于 ${c.requestedOwner}`);
        });
    }
}

async function mergeSubmissionToPublicDb(submission, forceConflicts) {
    const publicDb = await loadPublicAliasDb();
    const result = {
        added: 0,
        duplicate: 0,
        conflict: 0,
        conflicts: []
    };

    const entries = getSubmissionEntries(submission);

    for (const item of entries) {
        const mainName = String(item.mainName || '').trim();
        const aliases = normalizeAliasArray(item.aliases);
        if (!mainName || aliases.length === 0) continue;

        if (!publicDb.players[mainName]) publicDb.players[mainName] = [];

        for (const aliasName of aliases) {
            const owner = findAliasOwner(publicDb, aliasName, mainName);
            if (owner && !forceConflicts) {
                result.conflict++;
                result.conflicts.push({ aliasName, currentOwner: owner, requestedOwner: mainName });
                continue;
            }

            if (owner && forceConflicts) {
                removeAliasFromOtherOwners(publicDb, aliasName, mainName);
            }

            if (publicDb.players[mainName].includes(aliasName)) {
                result.duplicate++;
                continue;
            }

            publicDb.players[mainName].push(aliasName);
            result.added++;
        }

        publicDb.players[mainName] = normalizeAliasArray(publicDb.players[mainName]);
        if (publicDb.players[mainName].length === 0) delete publicDb.players[mainName];
    }

    publicDb.version += 1;
    publicDb.updatedAt = Math.floor(Date.now() / 1000);
    publicDb.audit.push({
        submissionId: submission.id,
        reviewedAt: publicDb.updatedAt,
        forceConflicts,
        added: result.added,
        duplicate: result.duplicate,
        conflict: result.conflict
    });
    publicDb.audit = publicDb.audit.slice(-200);

    await putJsonObject(PUBLIC_ALIAS_DB_KEY, publicDb);
    return result;
}

function addMergeTotals(totals, mergeResult) {
    totals.approved++;
    totals.added += mergeResult.added;
    totals.duplicate += mergeResult.duplicate;
    totals.conflict += mergeResult.conflict;
}

async function archiveSubmission(row, status, extra = {}) {
    const submission = {
        ...row.data,
        status,
        reviewedAt: Math.floor(Date.now() / 1000),
        ...extra
    };
    const prefix = status === 'approved' ? APPROVED_ALIAS_PREFIX : REJECTED_ALIAS_PREFIX;
    const archiveKey = `${prefix}${submission.id || Date.now()}.json`;
    await putJsonObject(archiveKey, submission);
    await getClient().delete(row.key);
}

function rowHasDeclaredConflicts(row) {
    return Array.isArray(row.data?.conflicts) && row.data.conflicts.length > 0;
}

async function approveSubmissionRow(row, forceConflicts) {
    const mergeResult = await mergeSubmissionToPublicDb(row.data, forceConflicts);
    await archiveSubmission(row, 'approved', { mergeResult, batchReviewed: true });
    return mergeResult;
}

async function showPendingAliasSubmissions() {
    console.log('⏳ 正在读取待审核投稿...');
    const rows = await loadPendingSubmissionList();
    if (rows.length === 0) {
        console.log('✅ 当前没有待审核小号投稿。');
        return;
    }

    console.log(`\n待审核投稿共 ${rows.length} 条（最多显示前 50 条）：`);
    const visibleRows = rows.filter(hasReviewableContent);
    const emptyCount = rows.length - visibleRows.length;
    if (emptyCount > 0) {
        console.log(`已隐藏 ${emptyCount} 条空记录，可用菜单 11 清理。`);
    }
    visibleRows.forEach((row, idx) => printSubmissionSummary(row, idx + 1));
}

async function reviewAliasSubmission() {
    const rows = (await loadPendingSubmissionList()).filter(hasReviewableContent);
    if (rows.length === 0) {
        console.log('✅ 当前没有待审核小号投稿。');
        return;
    }

    console.log('\n请选择要审核的投稿：');
    rows.forEach((row, idx) => printSubmissionSummary(row, idx + 1));

    const idxText = await question('请输入序号: ');
    const idx = parseInt(idxText, 10);
    if (!idx || idx < 1 || idx > rows.length) {
        console.log('❌ 序号无效。');
        return;
    }

    const row = rows[idx - 1];
    const submission = row.data;
    printSubmissionDetail(submission);

    console.log('\n操作：');
    console.log(' a. 通过（冲突项跳过）');
    console.log(' f. 强制通过（冲突小号会改绑到本投稿主号）');
    console.log(' r. 驳回');
    console.log(' s. 跳过');

    const decision = (await question('请输入操作: ')).trim().toLowerCase();

    if (decision === 's') return;

    if (decision === 'r') {
        const reason = await question('请输入驳回原因: ');
        await archiveSubmission(row, 'rejected', { rejectReason: reason || '管理员驳回' });
        console.log('🗑️ 已驳回并归档该投稿。');
        return;
    }

    if (decision !== 'a' && decision !== 'f') {
        console.log('❌ 无效操作。');
        return;
    }

    const forceConflicts = decision === 'f';
    const mergeResult = await mergeSubmissionToPublicDb(submission, forceConflicts);
    await archiveSubmission(row, 'approved', { mergeResult });

    console.log('✅ 已审核通过并合并到公共小号库。');
    console.log(` - 新增: ${mergeResult.added}`);
    console.log(` - 已存在: ${mergeResult.duplicate}`);
    console.log(` - 冲突跳过: ${mergeResult.conflict}`);

    if (mergeResult.conflicts.length > 0) {
        console.log('冲突样例：');
        mergeResult.conflicts.slice(0, 20).forEach(c => {
            console.log(` - ${c.aliasName}: 当前属于 ${c.currentOwner}, 投稿要求属于 ${c.requestedOwner}`);
        });
    }
}

async function batchReviewAliasSubmissions() {
    const rows = (await loadPendingSubmissionList()).filter(hasReviewableContent);
    if (rows.length === 0) {
        console.log('✅ 当前没有待审核小号投稿。');
        return;
    }

    const noConflictRows = rows.filter(row => !rowHasDeclaredConflicts(row));
    const conflictRows = rows.filter(row => rowHasDeclaredConflicts(row));
    const allStats = rows.reduce((acc, row) => {
        const stats = getSubmissionStats(row.data);
        acc.main += stats.mainCount;
        acc.alias += stats.pairCount;
        return acc;
    }, { main: 0, alias: 0 });

    console.log('\n========== 批量审核 ==========');
    console.log(`待审核记录: ${rows.length} 条`);
    console.log(`无冲突记录: ${noConflictRows.length} 条`);
    console.log(`有冲突记录: ${conflictRows.length} 条`);
    console.log(`合计内容: ${allStats.main} 个主号 / ${allStats.alias} 个小号`);
    console.log('------------------------------');
    console.log(' 1. 一键通过全部无冲突投稿（推荐）');
    console.log(' 2. 通过前 N 条无冲突投稿');
    console.log(' 3. 驳回前 N 条无冲突投稿');
    console.log(' 4. 强制通过全部投稿（包含冲突，会改绑小号，谨慎）');
    console.log(' 0. 返回');

    const choice = (await question('请选择批量操作: ')).trim();
    if (choice === '0') return;

    let targetRows = [];
    let forceConflicts = false;
    let rejectMode = false;

    if (choice === '1') {
        targetRows = noConflictRows;
    }
    else if (choice === '2' || choice === '3') {
        const nText = await question(`请输入 N (1-${noConflictRows.length}): `);
        const n = parseInt(nText, 10);
        if (!n || n < 1) {
            console.log('❌ 数量无效。');
            return;
        }
        targetRows = noConflictRows.slice(0, n);
        rejectMode = choice === '3';
    }
    else if (choice === '4') {
        const confirm = await question('⚠️ 强制通过会处理冲突并改绑小号，输入 YES 确认: ');
        if (confirm !== 'YES') {
            console.log('已取消。');
            return;
        }
        targetRows = rows;
        forceConflicts = true;
    }
    else {
        console.log('❌ 无效操作。');
        return;
    }

    if (targetRows.length === 0) {
        console.log('没有可处理的记录。');
        return;
    }

    const actionText = rejectMode ? '驳回' : (forceConflicts ? '强制通过' : '通过');
    const confirm = await question(`确认${actionText} ${targetRows.length} 条投稿？(y/n): `);
    if (confirm.toLowerCase() !== 'y') {
        console.log('已取消。');
        return;
    }

    const totals = { approved: 0, rejected: 0, failed: 0, added: 0, duplicate: 0, conflict: 0 };

    for (let i = 0; i < targetRows.length; i++) {
        const row = targetRows[i];
        try {
            if (rejectMode) {
                await archiveSubmission(row, 'rejected', { rejectReason: '批量驳回无冲突投稿', batchReviewed: true });
                totals.rejected++;
            }
            else {
                const mergeResult = await approveSubmissionRow(row, forceConflicts);
                addMergeTotals(totals, mergeResult);
            }
            process.stdout.write(`\r进度: ${i + 1}/${targetRows.length}`);
        } catch (err) {
            totals.failed++;
            console.log(`\n❌ 处理失败: ${row.data?.id || row.key} - ${err.message}`);
        }
    }

    console.log('\n========== 批量处理完成 ==========');
    console.log(`通过: ${totals.approved}`);
    console.log(`驳回: ${totals.rejected}`);
    console.log(`失败: ${totals.failed}`);
    console.log(`新增小号: ${totals.added}`);
    console.log(`已存在跳过: ${totals.duplicate}`);
    console.log(`冲突跳过: ${totals.conflict}`);
    if (conflictRows.length > 0 && !forceConflicts) {
        console.log(`仍保留 ${conflictRows.length} 条冲突投稿，请用单条审核查看。`);
    }
}

async function showPublicAliasDbStats() {
    const publicDb = await loadPublicAliasDb();
    const mainCount = Object.keys(publicDb.players).length;
    const aliasCount = Object.values(publicDb.players).reduce((sum, arr) => sum + arr.length, 0);
    console.log('\n========== 公共小号库 ==========');
    console.log(`版本: ${publicDb.version}`);
    console.log(`更新时间: ${formatUnixTime(publicDb.updatedAt)}`);
    console.log(`主号数: ${mainCount}`);
    console.log(`小号数: ${aliasCount}`);
}

async function previewPublicAliasDb() {
    const publicDb = await loadPublicAliasDb();
    const entries = Object.entries(publicDb.players);
    const mainCount = entries.length;
    const aliasCount = entries.reduce((sum, [, aliases]) => sum + aliases.length, 0);

    console.log('\n========== 公共小号库格式预览 ==========');
    console.log(`OSS Key: ${PUBLIC_ALIAS_DB_KEY}`);
    console.log(`版本: ${publicDb.version}`);
    console.log(`更新时间: ${formatUnixTime(publicDb.updatedAt)}`);
    console.log(`主号数: ${mainCount} | 小号数: ${aliasCount}`);
    console.log('\nJSON 格式：');
    console.log(JSON.stringify({
        version: publicDb.version,
        updatedAt: publicDb.updatedAt,
        players: {
            '主号示例': ['小号示例1', '小号示例2']
        },
        audit: [
            { action: 'admin_direct_sync 或审核来源', reviewedAt: Math.floor(Date.now() / 1000) }
        ]
    }, null, 2));

    const limitText = await question('\n预览多少条实际内容？默认 30: ');
    const limit = Math.max(1, parseInt(limitText, 10) || 30);

    console.log('\n实际内容预览（alias_db.ini 风格）：');
    entries.slice(0, limit).forEach(([mainName, aliases], idx) => {
        console.log(`${idx + 1}. ${mainName}=${aliases.map(a => `(${a})`).join('')}`);
    });

    if (entries.length > limit) {
        console.log(`...还有 ${entries.length - limit} 个主号未显示`);
    }
}

async function manageAdminDirectSyncKeys() {
    const config = await loadAdminKeyConfig();
    console.log('\n========== 直写管理员白名单 ==========');
    console.log(`白名单数量: ${config.keyHashes.length}`);
    config.keyHashes.forEach((hash, idx) => {
        console.log(`${idx + 1}. ${hash}`);
    });

    console.log('\n操作：');
    console.log(' 1. 添加 CDK 到直写白名单');
    console.log(' 2. 删除白名单 Hash');
    console.log(' 0. 返回');

    const choice = (await question('请选择操作: ')).trim();
    if (choice === '0') return;

    if (choice === '1') {
        const key = (await question('请输入允许直写的 CDK: ')).trim();
        if (!key.startsWith('CDK-')) {
            console.log('❌ 只允许添加 CDK- 开头的新卡密。');
            return;
        }

        const hash = sha256(key).toLowerCase();
        if (!config.keyHashes.includes(hash)) {
            config.keyHashes.push(hash);
            await saveAdminKeyConfig(config);
            console.log(`✅ 已加入直写白名单: ${hash}`);
        } else {
            console.log('该 CDK 已在白名单中。');
        }
        return;
    }

    if (choice === '2') {
        const hash = (await question('请输入要删除的完整 Hash: ')).trim().toLowerCase();
        const before = config.keyHashes.length;
        config.keyHashes = config.keyHashes.filter(item => item !== hash);
        if (config.keyHashes.length === before) {
            console.log('未找到该 Hash。');
            return;
        }
        await saveAdminKeyConfig(config);
        console.log('✅ 已删除。');
        return;
    }

    console.log('❌ 无效操作。');
}

async function handleLicenseMenu(choice) {
    const key = await question('🔑 请输入目标卡密 (如 CDK-XXX...): ');
    if (!key.startsWith('CDK-') && !key.startsWith('DNF-')) {
        console.log('❌ 卡密格式错误！');
        return;
    }

    console.log('⏳ 正在查询云端数据...');
    const data = await getLicense(key);

    if (!data.exists) {
        console.log('❌ 找不到该卡密！可能是新卡密还未被用户激活过。');
        return;
    }

    console.log('\n✅ 当前卡密信息：');
    console.log(` - 绑定机器码: ${data.hwid}`);
    console.log(` - 到期时间: ${formatTime(data.expireTime)} (时间戳: ${data.expireTime})`);

    switch (choice) {
        case '1':
            break;

        case '2': {
            const confirmBlock = await question('⚠️ 确定要封停该卡密吗？(y/n): ');
            if (confirmBlock.toLowerCase() === 'y') {
                await saveLicense(key, data.hwid, 1);
                console.log('💀 封停成功！该用户下次打开软件将被立刻拦截！');
            }
            break;
        }

        case '3': {
            const daysStr = await question('⏱️ 请输入要增加的天数 (支持负数扣除): ');
            const days = parseFloat(daysStr);
            if (!isNaN(days)) {
                if (data.expireTime === 1) {
                    console.log('⚠️ 该卡密已被封停，请先解除封停状态。');
                } else if (data.expireTime === 0xFFFFFFFF) {
                    console.log('🌟 该卡密已经是永久卡，无需加时间！');
                } else {
                    const baseTime = data.expireTime > 0 ? data.expireTime : Math.floor(Date.now() / 1000);
                    const newExpireTime = baseTime + Math.floor(days * 24 * 60 * 60);
                    await saveLicense(key, data.hwid, newExpireTime);
                    console.log(`🎉 操作成功！最新到期时间更新为: ${formatTime(newExpireTime)}`);
                }
            }
            break;
        }

        case '4': {
            const newHwid = await question('💻 请输入新的机器码 (输入 RESET 将重置为首登绑定): ');
            const finalHwid = newHwid.toUpperCase() === 'RESET' ? 'UNBOUND_WAITING_FOR_LOGIN' : newHwid;
            await saveLicense(key, finalHwid, data.expireTime);
            console.log(`🔄 换绑成功！目标机器码已变更为: ${finalHwid}`);
            break;
        }
    }
}

async function mainMenu() {
    console.log('\n==================================');
    console.log('    DNF 授权与共享库管理控制台');
    console.log('==================================');
    console.log(' 1. 查询卡密状态');
    console.log(' 2. 封停卡密 (拉黑)');
    console.log(' 3. 增加/减少时长 (补偿/续费)');
    console.log(' 4. 解除设备绑定 (换绑电脑)');
    console.log(' 5. 查看待审核小号投稿');
    console.log(' 6. 审核小号投稿');
    console.log(' 7. 批量审核小号投稿');
    console.log(' 8. 查看公共小号库统计');
    console.log(' 9. 预览公共小号库格式');
    console.log('10. 管理直写管理员白名单');
    console.log('11. 清理空的待审核记录');
    console.log(' 0. 退出程序');
    console.log('==================================');

    const choice = await question('请输入操作序号 (0-11): ');

    try {
        if (choice === '0') {
            console.log('退出管理系统...');
            rl.close();
            return;
        }

        if (['1', '2', '3', '4'].includes(choice)) {
            await handleLicenseMenu(choice);
        } else if (choice === '5') {
            await showPendingAliasSubmissions();
        } else if (choice === '6') {
            await reviewAliasSubmission();
        } else if (choice === '7') {
            await batchReviewAliasSubmissions();
        } else if (choice === '8') {
            await showPublicAliasDbStats();
        } else if (choice === '9') {
            await previewPublicAliasDb();
        } else if (choice === '10') {
            await manageAdminDirectSyncKeys();
        } else if (choice === '11') {
            await cleanupEmptyPendingSubmissions();
        } else {
            console.log('❌ 无效的序号！');
        }
    } catch (err) {
        console.log('❌ 操作过程中发生云端异常: ', err.message);
    }

    setTimeout(mainMenu, 1000);
}

mainMenu();
