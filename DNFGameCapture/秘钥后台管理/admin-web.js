const OSS = require('ali-oss');
const express = require('express');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

let client = null;

const PUBLIC_ALIAS_DB_KEY = 'shared-alias/public_alias_db.json';
const PENDING_ALIAS_PREFIX = 'alias-submissions/pending/';
const APPROVED_ALIAS_PREFIX = 'alias-submissions/approved/';
const REJECTED_ALIAS_PREFIX = 'alias-submissions/rejected/';
const REJECTED_BLOCK_PREFIX = 'alias-submissions/rejected-blocks/';
const REJECTED_BLOCK_TTL_SEC = 14 * 24 * 60 * 60;
const MAX_NAME_LENGTH = 60;
const MAX_PUBLIC_LIST_LIMIT = 5000;

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

function normalizeAliasArray(value) {
    const arr = Array.isArray(value)
        ? value
        : (typeof value === 'string' ? value.split(/[()（）、,，;；]/) : []);
    const out = [];
    const seen = new Set();
    for (const item of arr) {
        const alias = cleanName(item);
        const aliasId = getAliasDuplicateId(alias);
        const key = aliasId || alias;
        if (!alias || seen.has(key)) continue;
        seen.add(key);
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

function normalizeReviewDiff(diff) {
    const raw = diff && typeof diff === 'object' ? diff : {};
    return {
        addedAliases: normalizeAliasArray(raw.addedAliases),
        removedAliases: normalizeAliasArray(raw.removedAliases)
    };
}

function buildAliasReviewFingerprintPayload(mainName, aliases, operation, diff) {
    const cleanMain = cleanName(mainName);
    const normalizedDiff = normalizeReviewDiff(diff);
    return {
        schemaVersion: 1,
        mainName: cleanMain,
        operation: String(operation || (normalizeAliasArray(aliases).length === 0 ? 'delete' : 'replace')),
        aliases: normalizeAliasArray(aliases).filter(alias => alias !== cleanMain),
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

        const aliases = normalizeAliasArray(item.aliases).filter(alias => alias !== mainName);
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
    return getSubmissionEntries(row.data).length > 0;
}

function findDuplicateAliasOwners(publicDb, aliases, expectedMainName) {
    const hints = [];
    for (const aliasName of normalizeAliasArray(aliases)) {
        const aliasId = getAliasDuplicateId(aliasName);
        const owners = [];
        const matchedAliases = [];
        for (const [mainName, publicAliases] of Object.entries(publicDb.players || {})) {
            if (mainName === expectedMainName) continue;
            const matchedAlias = normalizeAliasArray(publicAliases).find(publicAlias => sameAliasId(publicAlias, aliasName));
            if (matchedAlias) {
                owners.push(mainName);
                matchedAliases.push(matchedAlias);
            }
        }
        if (owners.length > 0) hints.push({ aliasName, aliasId, matchedAliases, owners, requestedOwner: expectedMainName });
    }
    return hints;
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
        const keptAliases = normalizeAliasArray(oldAliases)
            .filter(oldAlias => !addedAliases.some(addedAlias => sameAliasId(oldAlias, addedAlias)));
        return normalizeAliasArray([...keptAliases, ...addedAliases]);
    }

    if (mode === 'removed') {
        const removedAliases = normalizeAliasArray(diff.removedAliases);
        return normalizeAliasArray(oldAliases)
            .filter(oldAlias => !removedAliases.some(removedAlias => sameAliasId(oldAlias, removedAlias)));
    }

    return targetAliases;
}

async function mergeSubmissionToPublicDb(submission, mode = 'full') {
    const publicDb = await loadPublicAliasDb();
    const result = { mode, replaced: 0, deleted: 0, unchanged: 0, pairCount: 0, duplicateHint: 0, duplicateHints: [] };
    const entries = getSubmissionEntries(submission);

    for (const item of entries) {
        const mainName = String(item.mainName || '').trim();
        const targetAliases = normalizeAliasArray(item.aliases);
        if (!mainName) continue;

        const oldAliases = normalizeAliasArray(publicDb.players[mainName]);
        const diff = diffAliasesForMerge(submission, item, oldAliases, targetAliases);
        const aliases = buildAliasesForReviewMode(mode, oldAliases, targetAliases, diff)
            .filter(alias => alias !== mainName);
        const duplicateCheckAliases = mode === 'removed' ? [] : (mode === 'added' ? diff.addedAliases : aliases);
        const hints = findDuplicateAliasOwners(publicDb, duplicateCheckAliases, mainName);
        result.duplicateHint += hints.length;
        result.duplicateHints.push(...hints);

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
    publicDb.updatedAt = nowSec();
    publicDb.audit.push({
        submissionId: submission.id,
        reviewedAt: publicDb.updatedAt,
        action: mode === 'added' ? 'review_added_aliases' : (mode === 'removed' ? 'review_removed_aliases' : 'review_replace_main'),
        reviewMode: mode,
        replaced: result.replaced,
        deleted: result.deleted,
        unchanged: result.unchanged,
        pairCount: result.pairCount,
        duplicateHint: result.duplicateHint
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
    if (status === 'rejected') {
        await putRejectedBlockMarkers(submission, submission.reviewedAt, submission.rejectReason || '');
    }
    await getClient().delete(row.key);
}

function buildPendingView(row) {
    const entries = getSubmissionEntries(row.data);
    const stats = getSubmissionStats(row.data);
    const duplicateHints = Array.isArray(row.data?.duplicateOwners)
        ? row.data.duplicateOwners
        : (Array.isArray(row.data?.conflicts) ? row.data.conflicts.map(c => ({
            aliasName: c.aliasName,
            owners: [c.currentOwner].filter(Boolean),
            requestedOwner: c.requestedOwner
        })) : []);
    const firstEntry = entries[0];
    const diff = row.data?.diff && typeof row.data.diff === 'object' ? {
        beforeAliases: normalizeAliasArray(row.data.diff.beforeAliases),
        afterAliases: normalizeAliasArray(row.data.diff.afterAliases),
        addedAliases: normalizeAliasArray(row.data.diff.addedAliases),
        removedAliases: normalizeAliasArray(row.data.diff.removedAliases)
    } : null;
    const sampleText = firstEntry
        ? `${firstEntry.mainName} -> ${firstEntry.aliases.length > 0 ? firstEntry.aliases.slice(0, 4).join(' / ') : '删除主号'}`
        : '无明细';

    return {
        key: row.key,
        id: row.data?.id || row.key,
        type: row.data?.type || 'legacy',
        operation: row.data?.operation || (stats.pairCount === 0 ? 'delete' : 'replace'),
        mainCount: stats.mainCount,
        pairCount: stats.pairCount,
        sourceCount: row.data?.sourceCount || row.data?.submitterKeyHashes?.length || 1,
        createdAt: row.data?.createdAt || 0,
        updatedAt: row.data?.updatedAt || row.data?.createdAt || 0,
        submitter: row.data?.submitterKeyHash || row.data?.submitterKeyHashes?.[0] || '',
        sample: sampleText,
        entries,
        diff,
        duplicateHints,
        duplicateHintCount: duplicateHints.length,
        conflictCount: duplicateHints.length,
        isEmpty: entries.length === 0
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
            duplicateHintCount: visiblePending.filter(row => row.duplicateHintCount > 0).length,
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
const externalWebDir = path.join(getRuntimeDir(), 'web-admin');
const bundledWebDir = path.join(__dirname, 'web-admin');
const webDir = fs.existsSync(externalWebDir) ? externalWebDir : bundledWebDir;
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

    publicDb.players[entry.mainName] = entry.aliases;

    const savedDb = await savePublicAliasDb(publicDb, {
        action: 'admin_public_create',
        mainName: entry.mainName,
        aliasCount: entry.aliases.length
    });

    res.json(buildPublicCrudResponse(savedDb, entry.mainName, '已新增公共库记录'));
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
    publicDb.players[entry.mainName] = entry.aliases;

    const savedDb = await savePublicAliasDb(publicDb, {
        action: 'admin_public_update',
        oldMainName,
        mainName: entry.mainName,
        aliasCount: entry.aliases.length
    });

    res.json(buildPublicCrudResponse(savedDb, entry.mainName, '已保存公共库记录', {
        oldMainName
    }));
}));

app.post('/api/public/batch-delete', asyncRoute(async (req, res) => {
    const requestedNames = Array.isArray(req.body?.mainNames) ? req.body.mainNames : [];
    const mainNames = [...new Set(requestedNames.map(cleanName).filter(Boolean))];
    if (mainNames.length === 0) return res.status(400).json({ error: '没有选择要删除的主号' });

    const publicDb = await loadPublicAliasDb();
    const deleted = [];
    const missing = [];
    let deletedAliasCount = 0;

    for (const mainName of mainNames) {
        const aliases = publicDb.players[mainName];
        if (!aliases) {
            missing.push(mainName);
            continue;
        }
        deletedAliasCount += normalizeAliasArray(aliases).length;
        delete publicDb.players[mainName];
        deleted.push(mainName);
    }

    if (deleted.length === 0) return res.status(404).json({ error: '所选主号都不存在或已删除' });

    const savedDb = await savePublicAliasDb(publicDb, {
        action: 'admin_public_batch_delete',
        mainNames: deleted,
        mainCount: deleted.length,
        aliasCount: deletedAliasCount,
        missing
    });

    res.json(buildPublicCrudResponse(savedDb, '', `已删除 ${deleted.length} 个公共库主号`, {
        deletedMainNames: deleted,
        deletedAliasCount,
        missing
    }));
}));

function parseImportText(text) {
    const entries = [];
    const errors = [];
    const seen = new Map();
    const lines = String(text || '').split(/\r?\n/);

    lines.forEach((rawLine, idx) => {
        const line = rawLine.trim();
        if (!line) return;

        const eq = line.indexOf('=');
        const fullEq = line.indexOf('＝');
        const eqPos = eq >= 0 && fullEq >= 0 ? Math.min(eq, fullEq) : Math.max(eq, fullEq);
        if (eqPos < 0) {
            errors.push({ line: idx + 1, text: line, error: '缺少等号' });
            return;
        }

        const mainName = cleanName(line.slice(0, eqPos));
        const aliases = normalizeAliasArray(line.slice(eqPos + 1)).filter(alias => alias !== mainName);
        if (!mainName) {
            errors.push({ line: idx + 1, text: line, error: '主号为空' });
            return;
        }
        if (aliases.length === 0) {
            errors.push({ line: idx + 1, text: line, error: '没有解析到小号' });
            return;
        }

        const current = seen.get(mainName) || [];
        seen.set(mainName, normalizeAliasArray([...current, ...aliases]));
    });

    for (const [mainName, aliases] of seen.entries()) entries.push({ mainName, aliases });
    return { entries, errors };
}

app.post('/api/public/import', asyncRoute(async (req, res) => {
    const { entries, errors } = parseImportText(req.body?.text);
    if (entries.length === 0) return res.status(400).json({ error: '没有可导入的有效记录', errors });

    const publicDb = await loadPublicAliasDb();
    let created = 0;
    let updated = 0;
    let addedAliasCount = 0;

    for (const entry of entries) {
        const before = normalizeAliasArray(publicDb.players[entry.mainName]);
        const merged = normalizeAliasArray([...before, ...entry.aliases]);
        if (before.length === 0) created++;
        else if (merged.length !== before.length) updated++;
        addedAliasCount += Math.max(0, merged.length - before.length);
        publicDb.players[entry.mainName] = merged;
    }

    const savedDb = await savePublicAliasDb(publicDb, {
        action: 'admin_public_import',
        entryCount: entries.length,
        created,
        updated,
        addedAliasCount,
        errorCount: errors.length
    });

    res.json({
        ...getPublicSummary(savedDb),
        message: `导入完成：新增 ${created} 个主号，更新 ${updated} 个主号，新增小号 ${addedAliasCount} 个`,
        imported: entries.length,
        created,
        updated,
        addedAliasCount,
        errors
    });
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
    const reviewMode = action === 'approve_added' ? 'added' : (action === 'approve_removed' ? 'removed' : 'full');

    if (keys.length === 0) return res.status(400).json({ error: '没有选择待审核记录' });
    if (!['approve', 'approve_added', 'approve_removed', 'reject'].includes(action)) return res.status(400).json({ error: '未知审核操作' });

    const rows = await loadPendingRows();
    const rowMap = new Map(rows.map(row => [row.key, row]));
    const result = { action, reviewMode, approved: 0, rejected: 0, failed: 0, replaced: 0, deleted: 0, unchanged: 0, pairCount: 0, duplicateHint: 0, failures: [] };

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
                const mergeResult = await mergeSubmissionToPublicDb(row.data, reviewMode);
                await archiveSubmission(row, 'approved', { mergeResult, reviewMode, batchReviewed: true });
                result.approved++;
                result.replaced += mergeResult.replaced;
                result.deleted += mergeResult.deleted;
                result.unchanged += mergeResult.unchanged;
                result.pairCount += mergeResult.pairCount;
                result.duplicateHint += mergeResult.duplicateHint;
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

const server = app.listen(port, host, () => {
    console.log(`DNF 共享库管理台已启动: http://${host}:${port}`);
});

server.on('error', (err) => {
    if (err.code === 'EADDRINUSE') {
        console.error(`端口已被占用: http://${host}:${port}`);
        console.error('请先关闭旧的管理台 node 进程，或用 ADMIN_WEB_PORT 指定另一个端口启动。');
        process.exit(1);
    }
    throw err;
});
