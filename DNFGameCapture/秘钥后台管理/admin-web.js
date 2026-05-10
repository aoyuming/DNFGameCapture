const OSS = require('ali-oss');
const express = require('express');
const fs = require('fs');
const path = require('path');

let client = null;

const PUBLIC_ALIAS_DB_KEY = 'shared-alias/public_alias_db.json';
const PENDING_ALIAS_PREFIX = 'alias-submissions/pending/';
const APPROVED_ALIAS_PREFIX = 'alias-submissions/approved/';
const REJECTED_ALIAS_PREFIX = 'alias-submissions/rejected/';
const MAX_NAME_LENGTH = 60;
const MAX_PUBLIC_LIST_LIMIT = 5000;

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
        throw new Error('缺少 OSS 凭证。请设置环境变量或填写 admin.config.json。');
    }

    return config;
}

function getClient() {
    if (!client) client = new OSS(loadOssConfig());
    return client;
}

function nowSec() {
    return Math.floor(Date.now() / 1000);
}

function cleanName(value) {
    return String(value || '')
        .replace(/[\r\n\t]/g, ' ')
        .replace(/[=|]/g, '')
        .trim()
        .slice(0, MAX_NAME_LENGTH);
}

function normalizeAliasArray(value) {
    const arr = Array.isArray(value)
        ? value
        : (typeof value === 'string' ? value.split(/[()（）、,，;；]/) : []);
    const out = [];
    const seen = new Set();
    for (const item of arr) {
        const alias = cleanName(item);
        if (!alias || seen.has(alias)) continue;
        seen.add(alias);
        out.push(alias);
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
            const cleanMain = cleanName(mainName);
            const cleanAliases = normalizeAliasArray(aliases).filter(alias => alias !== cleanMain);
            if (cleanMain && cleanAliases.length > 0) normalized.players[cleanMain] = cleanAliases;
        }
    }

    return normalized;
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

async function loadPublicAliasDb() {
    return normalizePublicDb(await getJsonObject(PUBLIC_ALIAS_DB_KEY, {
        version: 0,
        updatedAt: 0,
        players: {},
        audit: []
    }));
}

function getPublicAliasCount(publicDb) {
    return Object.values(publicDb.players).reduce((sum, aliases) => sum + aliases.length, 0);
}

function getPublicSummary(publicDb) {
    return {
        version: publicDb.version,
        updatedAt: publicDb.updatedAt,
        totalMainCount: Object.keys(publicDb.players).length,
        totalAliasCount: getPublicAliasCount(publicDb)
    };
}

function getPublicEntries(publicDb, search, limit) {
    return Object.entries(publicDb.players)
        .filter(([mainName, aliases]) => !search || mainName.includes(search) || aliases.some(alias => alias.includes(search)))
        .sort(([a], [b]) => a.localeCompare(b, 'zh-CN'))
        .slice(0, limit)
        .map(([mainName, aliases]) => ({ mainName, aliases }));
}

function normalizePublicEntryPayload(body) {
    const mainName = cleanName(body?.mainName);
    const aliases = normalizeAliasArray(body?.aliases).filter(alias => alias !== mainName);

    if (!mainName) {
        const err = new Error('主号不能为空');
        err.statusCode = 400;
        throw err;
    }

    if (aliases.length === 0) {
        const err = new Error('至少填写一个小号');
        err.statusCode = 400;
        throw err;
    }

    return { mainName, aliases };
}

function removeAliasesFromOtherPublicOwners(publicDb, aliases, expectedMainName) {
    const aliasSet = new Set(aliases);
    const affectedMains = [];
    let removed = 0;

    for (const [mainName, currentAliases] of Object.entries(publicDb.players)) {
        if (mainName === expectedMainName) continue;

        const nextAliases = currentAliases.filter(alias => !aliasSet.has(alias));
        if (nextAliases.length === currentAliases.length) continue;

        removed += currentAliases.length - nextAliases.length;
        affectedMains.push(mainName);

        if (nextAliases.length > 0) publicDb.players[mainName] = nextAliases;
        else delete publicDb.players[mainName];
    }

    return { removed, affectedMains };
}

async function savePublicAliasDb(publicDb, auditEntry) {
    const cleanDb = normalizePublicDb(publicDb);
    const changedAt = nowSec();

    cleanDb.version += 1;
    cleanDb.updatedAt = changedAt;
    cleanDb.audit.push({
        ...auditEntry,
        reviewedAt: changedAt
    });
    cleanDb.audit = cleanDb.audit.slice(-200);

    await putJsonObject(PUBLIC_ALIAS_DB_KEY, cleanDb);
    return cleanDb;
}

function buildPublicCrudResponse(publicDb, mainName, message, extra = {}) {
    return {
        ...getPublicSummary(publicDb),
        message,
        entry: mainName && publicDb.players[mainName]
            ? { mainName, aliases: publicDb.players[mainName] }
            : null,
        ...extra
    };
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

async function loadPendingRows() {
    const objects = await listPendingAliasSubmissions();
    const rows = [];

    for (const obj of objects) {
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

function findAliasOwner(publicDb, aliasName, expectedMainName) {
    for (const [mainName, aliases] of Object.entries(publicDb.players)) {
        if (mainName !== expectedMainName && aliases.includes(aliasName)) return mainName;
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

async function mergeSubmissionToPublicDb(submission, forceConflicts) {
    const publicDb = await loadPublicAliasDb();
    const result = { added: 0, duplicate: 0, conflict: 0, conflicts: [] };
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

            if (owner && forceConflicts) removeAliasFromOtherOwners(publicDb, aliasName, mainName);

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
    publicDb.updatedAt = nowSec();
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

async function archiveSubmission(row, status, extra = {}) {
    const submission = {
        ...row.data,
        status,
        reviewedAt: nowSec(),
        ...extra
    };
    const prefix = status === 'approved' ? APPROVED_ALIAS_PREFIX : REJECTED_ALIAS_PREFIX;
    const archiveKey = `${prefix}${submission.id || Date.now()}.json`;
    await putJsonObject(archiveKey, submission);
    await getClient().delete(row.key);
}

function buildPendingView(row) {
    const entries = getSubmissionEntries(row.data);
    const stats = getSubmissionStats(row.data);
    const conflicts = Array.isArray(row.data?.conflicts) ? row.data.conflicts : [];
    const firstEntry = entries[0];

    return {
        key: row.key,
        id: row.data?.id || row.key,
        type: row.data?.type || 'legacy',
        mainCount: stats.mainCount,
        pairCount: stats.pairCount,
        sourceCount: row.data?.sourceCount || row.data?.submitterKeyHashes?.length || 1,
        createdAt: row.data?.createdAt || 0,
        updatedAt: row.data?.updatedAt || row.data?.createdAt || 0,
        submitter: row.data?.submitterKeyHash || row.data?.submitterKeyHashes?.[0] || '',
        sample: firstEntry ? `${firstEntry.mainName} -> ${firstEntry.aliases.slice(0, 4).join(' / ')}` : '无明细',
        entries,
        conflicts,
        conflictCount: conflicts.length,
        isEmpty: stats.pairCount === 0
    };
}

async function getDashboardData() {
    const [rows, publicDb] = await Promise.all([loadPendingRows(), loadPublicAliasDb()]);
    const pendingViews = rows.map(buildPendingView);
    const visiblePending = pendingViews.filter(row => !row.isEmpty);
    const publicMainCount = Object.keys(publicDb.players).length;
    const publicAliasCount = Object.values(publicDb.players).reduce((sum, aliases) => sum + aliases.length, 0);

    return {
        pending: visiblePending,
        emptyCount: pendingViews.length - visiblePending.length,
        stats: {
            pendingCount: visiblePending.length,
            conflictCount: visiblePending.filter(row => row.conflictCount > 0).length,
            pendingAliasCount: visiblePending.reduce((sum, row) => sum + row.pairCount, 0),
            publicMainCount,
            publicAliasCount,
            publicVersion: publicDb.version,
            publicUpdatedAt: publicDb.updatedAt
        }
    };
}

function asyncRoute(fn) {
    return (req, res, next) => Promise.resolve(fn(req, res, next)).catch(next);
}

const app = express();
const webDir = path.join(__dirname, 'web-admin');
const host = process.env.ADMIN_WEB_HOST || '127.0.0.1';
const port = Number(process.env.ADMIN_WEB_PORT || 8899);

app.use(express.json({ limit: '1mb' }));
app.use(express.static(webDir));

app.get('/api/dashboard', asyncRoute(async (req, res) => {
    res.json(await getDashboardData());
}));

app.get('/api/public', asyncRoute(async (req, res) => {
    const publicDb = await loadPublicAliasDb();
    const search = String(req.query.search || '').trim();
    const limit = Math.max(1, Math.min(MAX_PUBLIC_LIST_LIMIT, parseInt(req.query.limit, 10) || MAX_PUBLIC_LIST_LIMIT));
    const entries = getPublicEntries(publicDb, search, limit);

    res.json({
        ...getPublicSummary(publicDb),
        entries
    });
}));

app.get('/api/public/:mainName', asyncRoute(async (req, res) => {
    const publicDb = await loadPublicAliasDb();
    const mainName = cleanName(req.params.mainName);
    const aliases = publicDb.players[mainName];

    if (!mainName || !aliases) {
        return res.status(404).json({ error: '公共库记录不存在' });
    }

    res.json({
        ...getPublicSummary(publicDb),
        entry: { mainName, aliases }
    });
}));

app.post('/api/public', asyncRoute(async (req, res) => {
    const entry = normalizePublicEntryPayload(req.body);
    const publicDb = await loadPublicAliasDb();

    if (publicDb.players[entry.mainName]) {
        return res.status(409).json({ error: '该主号已存在，请使用保存修改' });
    }

    const moved = removeAliasesFromOtherPublicOwners(publicDb, entry.aliases, entry.mainName);
    publicDb.players[entry.mainName] = entry.aliases;

    const savedDb = await savePublicAliasDb(publicDb, {
        action: 'admin_public_create',
        mainName: entry.mainName,
        aliasCount: entry.aliases.length,
        movedAliasCount: moved.removed,
        affectedMains: moved.affectedMains
    });

    res.json(buildPublicCrudResponse(savedDb, entry.mainName, '已新增公共库记录', {
        movedAliasCount: moved.removed
    }));
}));

app.put('/api/public/:mainName', asyncRoute(async (req, res) => {
    const oldMainName = cleanName(req.params.mainName);
    const entry = normalizePublicEntryPayload(req.body);
    const publicDb = await loadPublicAliasDb();

    if (!oldMainName || !publicDb.players[oldMainName]) {
        return res.status(404).json({ error: '要修改的公共库记录不存在' });
    }

    if (entry.mainName !== oldMainName && publicDb.players[entry.mainName]) {
        return res.status(409).json({ error: '新的主号名称已存在' });
    }

    delete publicDb.players[oldMainName];
    const moved = removeAliasesFromOtherPublicOwners(publicDb, entry.aliases, entry.mainName);
    publicDb.players[entry.mainName] = entry.aliases;

    const savedDb = await savePublicAliasDb(publicDb, {
        action: 'admin_public_update',
        oldMainName,
        mainName: entry.mainName,
        aliasCount: entry.aliases.length,
        movedAliasCount: moved.removed,
        affectedMains: moved.affectedMains
    });

    res.json(buildPublicCrudResponse(savedDb, entry.mainName, '已保存公共库记录', {
        oldMainName,
        movedAliasCount: moved.removed
    }));
}));

app.delete('/api/public/:mainName', asyncRoute(async (req, res) => {
    const mainName = cleanName(req.params.mainName);
    const publicDb = await loadPublicAliasDb();
    const aliases = publicDb.players[mainName];

    if (!mainName || !aliases) {
        return res.status(404).json({ error: '要删除的公共库记录不存在' });
    }

    delete publicDb.players[mainName];

    const savedDb = await savePublicAliasDb(publicDb, {
        action: 'admin_public_delete',
        mainName,
        aliasCount: aliases.length
    });

    res.json(buildPublicCrudResponse(savedDb, '', '已删除公共库记录', {
        deletedMainName: mainName,
        deletedAliasCount: aliases.length
    }));
}));

app.post('/api/review', asyncRoute(async (req, res) => {
    const keys = Array.isArray(req.body.keys) ? req.body.keys : [];
    const action = String(req.body.action || '');
    const reason = String(req.body.reason || '');

    if (keys.length === 0) return res.status(400).json({ error: '没有选择待审核记录' });
    if (!['approve', 'forceApprove', 'reject'].includes(action)) return res.status(400).json({ error: '未知审核操作' });

    const rows = await loadPendingRows();
    const rowMap = new Map(rows.map(row => [row.key, row]));
    const result = { approved: 0, rejected: 0, failed: 0, added: 0, duplicate: 0, conflict: 0, failures: [] };

    for (const key of keys) {
        const row = rowMap.get(key);
        if (!row) {
            result.failed++;
            result.failures.push({ key, error: '记录不存在或已经处理' });
            continue;
        }

        try {
            if (action === 'reject') {
                await archiveSubmission(row, 'rejected', { rejectReason: reason || '网页批量驳回', batchReviewed: true });
                result.rejected++;
            } else {
                const mergeResult = await mergeSubmissionToPublicDb(row.data, action === 'forceApprove');
                await archiveSubmission(row, 'approved', { mergeResult, batchReviewed: true });
                result.approved++;
                result.added += mergeResult.added;
                result.duplicate += mergeResult.duplicate;
                result.conflict += mergeResult.conflict;
            }
        } catch (err) {
            result.failed++;
            result.failures.push({ key, error: err.message });
        }
    }

    res.json(result);
}));

app.post('/api/cleanup-empty', asyncRoute(async (req, res) => {
    const rows = await loadPendingRows();
    const emptyRows = rows.filter(row => !hasReviewableContent(row));
    let cleaned = 0;

    for (const row of emptyRows) {
        await archiveSubmission(row, 'rejected', { rejectReason: '网页清理空投稿', emptyCleanup: true });
        cleaned++;
    }

    res.json({ cleaned });
}));

app.use((err, req, res, next) => {
    console.error(err);
    res.status(err.statusCode || 500).json({ error: err.message || '服务器异常' });
});

app.listen(port, host, () => {
    console.log(`DNF 共享库管理台已启动: http://${host}:${port}`);
});
