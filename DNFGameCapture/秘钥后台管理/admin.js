const OSS = require('ali-oss');
const readline = require('readline');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

let client = null;

function getRuntimeDir() {
    return process.pkg ? path.dirname(process.execPath) : __dirname;
}

function resolveAdminConfigPath() {
    if (process.env.ADMIN_CONFIG_PATH) return process.env.ADMIN_CONFIG_PATH;

    const runtimeConfigPath = path.join(getRuntimeDir(), 'admin.config.json');
    if (fs.existsSync(runtimeConfigPath)) return runtimeConfigPath;

    return path.join(__dirname, 'admin.config.json');
}

function loadOssConfig() {
    const localConfigPath = resolveAdminConfigPath();
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
        throw new Error(`缺少 OSS 凭证。请设置环境变量，或在程序目录创建 admin.config.json。当前查找路径: ${localConfigPath}`);
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
const REJECTED_BLOCK_PREFIX = 'alias-submissions/rejected-blocks/';
const REJECTED_BLOCK_TTL_SEC = 14 * 24 * 60 * 60;
const MAX_NAME_LENGTH = 60;

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

function cleanName(value) {
    return String(value || '')
        .replace(/[\r\n\t]/g, ' ')
        .replace(/[=|]/g, '')
        .trim()
        .slice(0, MAX_NAME_LENGTH);
}

function getAliasDuplicateId(value) {
    const clean = cleanName(value);
    if (!clean) return '';
    const halfSharp = clean.indexOf('#');
    const fullSharp = clean.indexOf('＃');
    let sharp = -1;
    if (halfSharp >= 0 && fullSharp >= 0) sharp = Math.min(halfSharp, fullSharp);
    else sharp = halfSharp >= 0 ? halfSharp : fullSharp;
    return (sharp >= 0 ? clean.slice(0, sharp) : clean).trim();
}

function sameAliasId(a, b) {
    const aa = getAliasDuplicateId(a);
    const bb = getAliasDuplicateId(b);
    return !!aa && !!bb && aa === bb;
}

function getAliasJobKey(value) {
    const clean = cleanName(value);
    if (!clean) return '';
    const halfSharp = clean.indexOf('#');
    const fullSharp = clean.indexOf('＃');
    let sharp = -1;
    if (halfSharp >= 0 && fullSharp >= 0) sharp = Math.min(halfSharp, fullSharp);
    else sharp = halfSharp >= 0 ? halfSharp : fullSharp;
    return sharp >= 0 ? clean.slice(sharp + 1).trim() : '';
}

function aliasHasDeclaredJob(value) {
    return !!getAliasJobKey(value);
}

function sameAliasStorageEntry(a, b) {
    const aa = cleanName(a);
    const bb = cleanName(b);
    if (!aa || !bb) return false;
    if (aa === bb) return true;
    if (!sameAliasId(aa, bb)) return false;
    const aj = getAliasJobKey(aa);
    const bj = getAliasJobKey(bb);
    if (aj || bj) return !!aj && !!bj && aj === bj;
    return true;
}

function mergeAliasIntoArray(out, rawAlias) {
    const alias = cleanName(rawAlias);
    if (!alias) return;
    const aliasHasJob = aliasHasDeclaredJob(alias);
    for (let i = 0; i < out.length; i++) {
        const existing = cleanName(out[i]);
        const existingHasJob = aliasHasDeclaredJob(existing);
        if (sameAliasStorageEntry(existing, alias)) {
            if (!existingHasJob && aliasHasJob) out[i] = alias;
            return;
        }
        if (sameAliasId(existing, alias)) {
            if (!existingHasJob && aliasHasJob) {
                out[i] = alias;
                return;
            }
            if (existingHasJob && !aliasHasJob) return;
        }
    }
    out.push(alias);
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
    for (const item of arr) {
        mergeAliasIntoArray(out, item);
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
            .filter(item => item.mainName);
    }

    if (submission?.mainName) {
        const mainName = String(submission.mainName || '').trim();
        const aliases = normalizeAliasArray(submission.aliases);
        return mainName ? [{ mainName, aliases }] : [];
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

function sameAliasSet(a, b) {
    const aa = normalizeAliasArray(a).slice().sort();
    const bb = normalizeAliasArray(b).slice().sort();
    if (aa.length !== bb.length) return false;
    return aa.every((item, idx) => item === bb[idx]);
}

function isSubmissionUnchangedAgainstPublic(row, publicDb) {
    const entries = getSubmissionEntries(row.data);
    if (entries.length === 0) return false;

    return entries.every(item => {
        const mainName = cleanName(item.mainName);
        if (!mainName) return true;
        return sameAliasSet(publicDb.players[mainName] || [], item.aliases);
    });
}

function normalizeReviewAliasArray(value) {
    const out = [];
    const seen = new Set();
    for (const raw of normalizeAliasArray(value)) {
        const alias = cleanName(raw);
        if (!alias || seen.has(alias)) continue;
        seen.add(alias);
        out.push(alias);
    }
    return out;
}

function normalizeReviewDiff(diff) {
    const raw = diff && typeof diff === 'object' ? diff : {};
    return {
        addedAliases: normalizeReviewAliasArray(raw.addedAliases),
        removedAliases: normalizeReviewAliasArray(raw.removedAliases)
    };
}

function buildAliasReviewFingerprintPayload(mainName, aliases, operation, diff) {
    const cleanMain = cleanName(mainName);
    const normalizedAliases = normalizeReviewAliasArray(aliases).filter(alias => alias !== cleanMain);
    const normalizedDiff = normalizeReviewDiff(diff);
    return {
        schemaVersion: 1,
        mainName: cleanMain,
        operation: String(operation || (normalizedAliases.length === 0 ? 'delete' : 'replace')),
        aliases: normalizedAliases,
        addedAliases: normalizedDiff.addedAliases,
        removedAliases: normalizedDiff.removedAliases
    };
}

function getAliasReviewFingerprint(mainName, aliases, operation, diff) {
    return sha256(JSON.stringify(buildAliasReviewFingerprintPayload(mainName, aliases, operation, diff)));
}

function getRejectedBlockKey(mainName, fingerprint) {
    return `${REJECTED_BLOCK_PREFIX}main-${sha256(mainName).slice(0, 16)}/${fingerprint}.json`;
}

async function putRejectedBlockMarkers(submission, rejectedAt, rejectReason) {
    const entries = getSubmissionEntries(submission);
    const reason = String(rejectReason || '');

    for (const item of entries) {
        const mainName = cleanName(item.mainName);
        if (!mainName) continue;

        const aliases = normalizeReviewAliasArray(item.aliases).filter(alias => alias !== mainName);
        const operation = aliases.length === 0 ? 'delete' : 'replace';
        const diff = normalizeReviewDiff(submission.diff);
        const fingerprint = getAliasReviewFingerprint(mainName, aliases, operation, diff);

        await putJsonObject(getRejectedBlockKey(mainName, fingerprint), {
            type: 'alias-rejected-block',
            schemaVersion: 1,
            mainName,
            fingerprint,
            rejectedAt,
            expireAt: rejectedAt + REJECTED_BLOCK_TTL_SEC,
            rejectReason: reason,
            submissionId: String(submission.id || ''),
            operation,
            aliases,
            diff
        });
    }
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

async function loadReviewablePendingSubmissionList() {
    const [rows, publicDb] = await Promise.all([loadPendingSubmissionList(), loadPublicAliasDb()]);
    const visibleRows = rows.filter(row => hasReviewableContent(row) && !isSubmissionUnchangedAgainstPublic(row, publicDb));
    return { rows, publicDb, visibleRows, hiddenCount: rows.length - visibleRows.length };
}

function hasReviewableContent(row) {
    return getSubmissionEntries(row.data).length > 0;
}

async function cleanupEmptyPendingSubmissions() {
    const { rows, publicDb } = await loadReviewablePendingSubmissionList();
    const emptyRows = rows.filter(row => !hasReviewableContent(row) || isSubmissionUnchangedAgainstPublic(row, publicDb));

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
        ? `${entries[0].mainName} -> ${entries[0].aliases.length > 0 ? entries[0].aliases.slice(0, 3).join(', ') : '删除主号'}`
        : '无明细';

    console.log(`${index}. ${s.id || row.key}`);
    console.log(`   时间: ${formatUnixTime(s.updatedAt || s.createdAt)} | 主号: ${stats.mainCount} | 小号: ${stats.pairCount} | 投稿人数: ${s.sourceCount || s.submitterKeyHashes?.length || 1}`);
    console.log(`   提交者: ${(s.submitterKeyHash || s.submitterKeyHashes?.[0] || '').slice(0, 12)}... | 样例: ${sample}`);
    if (Array.isArray(s.duplicateOwners) && s.duplicateOwners.length > 0) {
        console.log(`   ⚠️ 重复提示: ${s.duplicateOwners.length} 条`);
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
        const aliases = normalizeAliasArray(item.aliases);
        console.log(`${idx + 1}. ${item.mainName} = ${aliases.length > 0 ? aliases.join(' / ') : '删除主号'}`);
    });

    if (entries.length > 80) {
        console.log(`...还有 ${entries.length - 80} 条未显示`);
    }

    if (submission.diff && typeof submission.diff === 'object') {
        const added = normalizeAliasArray(submission.diff.addedAliases);
        const removed = normalizeAliasArray(submission.diff.removedAliases);
        console.log('------------------------------');
        console.log(`差异：新增 ${added.length} 个 / 删除 ${removed.length} 个`);
        if (added.length > 0) console.log(` + ${added.slice(0, 30).join(' / ')}`);
        if (removed.length > 0) console.log(` - ${removed.slice(0, 30).join(' / ')}`);
    }

    if (Array.isArray(submission.duplicateOwners) && submission.duplicateOwners.length > 0) {
        console.log('------------------------------');
        console.log('重复小号提示：');
        submission.duplicateOwners.slice(0, 30).forEach(c => {
            console.log(` - ${c.aliasName}: 公共库也属于 ${(c.owners || []).join(', ')}, 本次提交给 ${c.requestedOwner}`);
        });
    }
}

function diffAliasesForMerge(submission, item, oldAliases, targetAliases) {
    const diff = submission?.diff && typeof submission.diff === 'object' ? submission.diff : {};
    let addedAliases = normalizeAliasArray(diff.addedAliases);
    let removedAliases = normalizeAliasArray(diff.removedAliases);

    if (addedAliases.length === 0 && removedAliases.length === 0) {
        addedAliases = targetAliases.filter(alias => !oldAliases.includes(alias));
        removedAliases = oldAliases.filter(alias => !targetAliases.includes(alias));
    }

    const mainName = String(item.mainName || '').trim();
    return {
        addedAliases: addedAliases.filter(alias => alias !== mainName),
        removedAliases: removedAliases.filter(alias => alias !== mainName)
    };
}

function buildAliasesForReviewMode(mode, oldAliases, targetAliases, diff) {
    if (mode === 'added') {
        const addedAliases = normalizeAliasArray(diff.addedAliases);
        return normalizeAliasArray([...normalizeAliasArray(oldAliases), ...addedAliases]);
    }

    if (mode === 'removed') {
        const removedAliases = normalizeAliasArray(diff.removedAliases);
        return normalizeAliasArray(oldAliases)
            .filter(oldAlias => !removedAliases.some(removedAlias => sameAliasStorageEntry(oldAlias, removedAlias)));
    }

    return targetAliases;
}

async function mergeSubmissionToPublicDb(submission, mode = 'full') {
    const publicDb = await loadPublicAliasDb();
    const result = {
        mode,
        replaced: 0,
        deleted: 0,
        unchanged: 0,
        pairCount: 0
    };

    const entries = getSubmissionEntries(submission);

    for (const item of entries) {
        const mainName = String(item.mainName || '').trim();
        const targetAliases = normalizeAliasArray(item.aliases);
        if (!mainName) continue;

        const oldAliases = normalizeAliasArray(publicDb.players[mainName]);
        const diff = diffAliasesForMerge(submission, item, oldAliases, targetAliases);
        const aliases = buildAliasesForReviewMode(mode, oldAliases, targetAliases, diff)
            .filter(alias => alias !== mainName);

        if (aliases.length === 0) {
            if (publicDb.players[mainName]) {
                delete publicDb.players[mainName];
                result.deleted++;
            } else {
                result.unchanged++;
            }
            continue;
        }

        if (oldAliases.length === aliases.length && oldAliases.every((alias, idx) => alias === aliases[idx])) {
            result.unchanged++;
        } else {
            publicDb.players[mainName] = aliases;
            result.replaced++;
        }
        result.pairCount += aliases.length;
    }

    publicDb.version += 1;
    publicDb.updatedAt = Math.floor(Date.now() / 1000);
    publicDb.audit.push({
        submissionId: submission.id,
        reviewedAt: publicDb.updatedAt,
        action: mode === 'added' ? 'review_added_aliases' : (mode === 'removed' ? 'review_removed_aliases' : 'review_replace_main'),
        reviewMode: mode,
        replaced: result.replaced,
        deleted: result.deleted,
        unchanged: result.unchanged,
        pairCount: result.pairCount
    });
    publicDb.audit = publicDb.audit.slice(-200);

    await putJsonObject(PUBLIC_ALIAS_DB_KEY, publicDb);
    return result;
}

function addMergeTotals(totals, mergeResult) {
    totals.approved++;
    totals.replaced += mergeResult.replaced;
    totals.deleted += mergeResult.deleted;
    totals.unchanged += mergeResult.unchanged;
    totals.pairCount += mergeResult.pairCount;
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
    if (status === 'rejected') {
        await putRejectedBlockMarkers(submission, submission.reviewedAt, submission.rejectReason || '');
    }
    await getClient().delete(row.key);
}

function rowHasDeclaredConflicts(row) {
    return Array.isArray(row.data?.duplicateOwners) && row.data.duplicateOwners.length > 0;
}

async function approveSubmissionRow(row, mode = 'full') {
    const mergeResult = await mergeSubmissionToPublicDb(row.data, mode);
    await archiveSubmission(row, 'approved', { mergeResult, reviewMode: mode, batchReviewed: true });
    return mergeResult;
}

async function showPendingAliasSubmissions() {
    console.log('⏳ 正在读取待审核投稿...');
    const { rows, visibleRows, hiddenCount } = await loadReviewablePendingSubmissionList();
    if (rows.length === 0) {
        console.log('✅ 当前没有待审核小号投稿。');
        return;
    }

    console.log(`\n待审核投稿共 ${rows.length} 条（最多显示前 50 条）：`);
    const emptyCount = hiddenCount;
    if (emptyCount > 0) {
        console.log(`已隐藏 ${emptyCount} 条空记录，可用菜单 11 清理。`);
    }
    visibleRows.forEach((row, idx) => printSubmissionSummary(row, idx + 1));
}

async function reviewAliasSubmission() {
    const { visibleRows: rows } = await loadReviewablePendingSubmissionList();
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
    console.log(' a. 通过（按投稿目标状态替换/删除主号）');
    console.log(' n. 只通过新增小号');
    console.log(' d. 只通过删除小号');
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

    if (!['a', 'n', 'd'].includes(decision)) {
        console.log('❌ 无效操作。');
        return;
    }

    const reviewMode = decision === 'n' ? 'added' : (decision === 'd' ? 'removed' : 'full');
    const mergeResult = await mergeSubmissionToPublicDb(submission, reviewMode);
    await archiveSubmission(row, 'approved', { mergeResult, reviewMode });

    console.log('✅ 已审核通过并合并到公共小号库。');
    console.log(` - 模式: ${reviewMode === 'added' ? '只通过新增' : (reviewMode === 'removed' ? '只通过删除' : '完整通过')}`);
    console.log(` - 替换主号: ${mergeResult.replaced}`);
    console.log(` - 删除主号: ${mergeResult.deleted}`);
    console.log(` - 未变化: ${mergeResult.unchanged}`);
}

async function batchReviewAliasSubmissions() {
    const { visibleRows: rows } = await loadReviewablePendingSubmissionList();
    if (rows.length === 0) {
        console.log('✅ 当前没有待审核小号投稿。');
        return;
    }

    const noHintRows = rows.filter(row => !rowHasDeclaredConflicts(row));
    const hintRows = rows.filter(row => rowHasDeclaredConflicts(row));
    const allStats = rows.reduce((acc, row) => {
        const stats = getSubmissionStats(row.data);
        acc.main += stats.mainCount;
        acc.alias += stats.pairCount;
        return acc;
    }, { main: 0, alias: 0 });

    console.log('\n========== 批量审核 ==========');
    console.log(`待审核记录: ${rows.length} 条`);
    console.log(`无重复提示记录: ${noHintRows.length} 条`);
    console.log(`有重复提示记录: ${hintRows.length} 条`);
    console.log(`合计内容: ${allStats.main} 个主号 / ${allStats.alias} 个小号`);
    console.log('------------------------------');
    console.log(' 1. 一键通过全部无重复提示投稿（推荐）');
    console.log(' 2. 通过前 N 条无重复提示投稿');
    console.log(' 3. 驳回前 N 条无重复提示投稿');
    console.log(' 4. 通过全部投稿（包含重复提示）');
    console.log(' 0. 返回');

    const choice = (await question('请选择批量操作: ')).trim();
    if (choice === '0') return;

    let targetRows = [];
    let rejectMode = false;

    if (choice === '1') {
        targetRows = noHintRows;
    }
    else if (choice === '2' || choice === '3') {
        const nText = await question(`请输入 N (1-${noHintRows.length}): `);
        const n = parseInt(nText, 10);
        if (!n || n < 1) {
            console.log('❌ 数量无效。');
            return;
        }
        targetRows = noHintRows.slice(0, n);
        rejectMode = choice === '3';
    }
    else if (choice === '4') {
        targetRows = rows;
    }
    else {
        console.log('❌ 无效操作。');
        return;
    }

    if (targetRows.length === 0) {
        console.log('没有可处理的记录。');
        return;
    }

    const actionText = rejectMode ? '驳回' : '通过';
    const confirm = await question(`确认${actionText} ${targetRows.length} 条投稿？(y/n): `);
    if (confirm.toLowerCase() !== 'y') {
        console.log('已取消。');
        return;
    }

    const totals = { approved: 0, rejected: 0, failed: 0, replaced: 0, deleted: 0, unchanged: 0, pairCount: 0 };

    for (let i = 0; i < targetRows.length; i++) {
        const row = targetRows[i];
        try {
            if (rejectMode) {
                await archiveSubmission(row, 'rejected', { rejectReason: '批量驳回无重复提示投稿', batchReviewed: true });
                totals.rejected++;
            }
            else {
                const mergeResult = await approveSubmissionRow(row);
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
    console.log(`替换主号: ${totals.replaced}`);
    console.log(`删除主号: ${totals.deleted}`);
    console.log(`未变化: ${totals.unchanged}`);
    console.log(`目标小号数: ${totals.pairCount}`);
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
