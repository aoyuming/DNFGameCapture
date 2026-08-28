const express = require('express');
const OSS = require('ali-oss');
const crypto = require('crypto');

const app = express();
app.use(express.json({ limit: '1mb' }));

const OSS_REGION = 'oss-cn-beijing';
const OSS_BUCKET = 'dnf-capture-update';
const LICENSE_PREFIX = 'licenses/';
const PENDING_ALIAS_PREFIX = 'alias-submissions/pending/';
const REJECTED_BLOCK_PREFIX = 'alias-submissions/rejected-blocks/';
const PUBLIC_ALIAS_DB_KEY = 'shared-alias/public_alias_db.json';
const ADMIN_KEY_HASHES_KEY = 'shared-alias/admin_key_hashes.json';
const REJECTED_BLOCK_TTL_SEC = 14 * 24 * 60 * 60;
const MAX_ALIAS_SUBMISSION_MAINS = 300;
const MAX_ALIASES_PER_MAIN = 20;
const MAX_PUBLIC_ALIAS_MAINS = 5000;
const MAX_PUBLIC_ALIASES_PER_MAIN = 100;
const MAX_NAME_LENGTH = 60;
const CLOUD_MATCH_SERVER_URL = 'http://47.109.149.111:18880';

function createOssClient() {
    const accessKeyId = process.env.ALIBABA_CLOUD_ACCESS_KEY_ID;
    const accessKeySecret = process.env.ALIBABA_CLOUD_ACCESS_KEY_SECRET;
    const securityToken = process.env.ALIBABA_CLOUD_SECURITY_TOKEN;

    if (!accessKeyId || !accessKeySecret) {
        throw new Error('云端未获取到 OSS 权限(环境变量为空)');
    }

    return new OSS({
        region: OSS_REGION,
        accessKeyId,
        accessKeySecret,
        stsToken: securityToken,
        bucket: OSS_BUCKET
    });
}

function sha256(value) {
    return crypto.createHash('sha256').update(String(value || '')).digest('hex');
}

function nowSec() {
    return Math.floor(Date.now() / 1000);
}

function isValidNewLicenseKey(licenseKey) {
    return typeof licenseKey === 'string' && licenseKey.startsWith('CDK-');
}

function parseLicenseContent(content) {
    const parts = String(content || '').split('|');
    return {
        hwid: parts[0] || '',
        expireTime: parts.length > 1 ? parseInt(parts[1], 10) || 0 : 0
    };
}

async function getLicenseRecord(client, licenseKey) {
    try {
        const result = await client.get(`${LICENSE_PREFIX}${licenseKey}.txt`);
        return { exists: true, ...parseLicenseContent(result.content.toString()) };
    } catch (e) {
        if (e.name === 'NoSuchKeyError') return { exists: false };
        throw e;
    }
}

function validateBoundLicense(record, hwid) {
    if (!record.exists) return { ok: false, msg: '卡密尚未激活，请先完成授权验证' };
    if (record.hwid !== hwid) return { ok: false, msg: `该卡密已被设备(${record.hwid.substring(0, 4)}***)绑定` };
    if (record.expireTime === 1) return { ok: false, msg: '该卡密已被封停，禁止上传共享库' };
    if (record.expireTime > 0 && record.expireTime !== 0xFFFFFFFF && nowSec() > record.expireTime) {
        return { ok: false, msg: '该卡密已过期，请续费后再上传共享库' };
    }
    return { ok: true };
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
    const aliasName = cleanName(rawAlias);
    if (!aliasName) return 'none';

    const aliasHasJob = aliasHasDeclaredJob(aliasName);
    for (let i = 0; i < out.length; i++) {
        const existing = cleanName(out[i]);
        if (!existing) continue;
        const existingHasJob = aliasHasDeclaredJob(existing);

        if (sameAliasStorageEntry(existing, aliasName)) {
            if (!existingHasJob && aliasHasJob) {
                out[i] = aliasName;
                return 'upgraded';
            }
            return 'none';
        }

        if (sameAliasId(existing, aliasName)) {
            if (!existingHasJob && aliasHasJob) {
                out[i] = aliasName;
                return 'upgraded';
            }
            if (existingHasJob && !aliasHasJob) {
                return 'none';
            }
        }
    }

    out.push(aliasName);
    return 'added';
}

function cloudAliasContainsNakedAlias(cloudAlias, localAlias) {
    const cloud = cleanName(cloudAlias);
    const local = cleanName(localAlias);
    if (!cloud || !local || aliasHasDeclaredJob(local)) return false;
    if (aliasHasDeclaredJob(cloud) && sameAliasStorageEntry(cloud, local)) return true;
    return cloud !== local && cloud.includes(local);
}

function parseAliasValue(value) {
    if (Array.isArray(value)) return value;
    if (typeof value === 'string') {
        const fromParens = value.split(/[()（）]/).map(s => s.trim()).filter(Boolean);
        return fromParens.length > 0 ? fromParens : value.split(/[,，;；]/);
    }
    return [];
}

function normalizeAliasArray(value) {
    const out = [];
    for (const raw of Array.isArray(value) ? value : parseAliasValue(value)) {
        mergeAliasIntoArray(out, raw);
    }
    return out;
}

function normalizeStringArray(value) {
    const out = [];
    const seen = new Set();
    for (const raw of Array.isArray(value) ? value : []) {
        const text = String(raw || '').trim();
        if (!text || seen.has(text)) continue;
        seen.add(text);
        out.push(text);
    }
    return out;
}

function normalizeMainNameArray(value, maxMains = MAX_PUBLIC_ALIAS_MAINS) {
    const out = [];
    const seen = new Set();
    for (const raw of Array.isArray(value) ? value : []) {
        if (out.length >= maxMains) break;
        const mainName = cleanName(raw);
        if (!mainName || seen.has(mainName)) continue;
        seen.add(mainName);
        out.push(mainName);
    }
    return out;
}

function normalizeAliasSubmission(rawAliasDb, maxMains = MAX_ALIAS_SUBMISSION_MAINS, maxAliasesPerMain = MAX_ALIASES_PER_MAIN, options = {}) {
    if (!rawAliasDb || typeof rawAliasDb !== 'object' || Array.isArray(rawAliasDb)) {
        return [];
    }

    const allowEmpty = !!options.allowEmpty;
    const entries = [];
    const seenMain = new Set();

    for (const [rawMainName, rawAliases] of Object.entries(rawAliasDb)) {
        if (entries.length >= maxMains) break;

        const mainName = cleanName(rawMainName);
        if (!mainName || seenMain.has(mainName)) continue;
        seenMain.add(mainName);

        const aliases = normalizeAliasArray(rawAliases)
            .filter(aliasName => aliasName !== mainName)
            .slice(0, maxAliasesPerMain);

        if (aliases.length > 0 || allowEmpty) {
            entries.push({ mainName, aliases });
        }
    }

    return entries;
}

function normalizePublicPlayersFromRaw(rawAliasDb, maxMains = MAX_PUBLIC_ALIAS_MAINS, maxAliasesPerMain = MAX_PUBLIC_ALIASES_PER_MAIN) {
    const players = {};
    if (!rawAliasDb || typeof rawAliasDb !== 'object' || Array.isArray(rawAliasDb)) {
        return players;
    }

    const seenMain = new Set();
    for (const [rawMainName, rawAliases] of Object.entries(rawAliasDb)) {
        if (seenMain.size >= maxMains) break;

        const mainName = cleanName(rawMainName);
        if (!mainName || seenMain.has(mainName)) continue;
        seenMain.add(mainName);

        const aliases = normalizeAliasArray(rawAliases)
            .filter(aliasName => aliasName !== mainName)
            .slice(0, maxAliasesPerMain);

        if (aliases.length > 0) {
            players[mainName] = aliases;
        }
    }

    return players;
}

function getPublicPairCount(players) {
    return Object.values(players || {}).reduce((sum, aliases) => sum + normalizeAliasArray(aliases).length, 0);
}

function sameAliasList(a, b) {
    const aa = normalizeAliasArray(a);
    const bb = normalizeAliasArray(b);
    if (aa.length !== bb.length) return false;
    return aa.every((item, idx) => item === bb[idx]);
}

function sameAliasSet(a, b) {
    const aa = normalizeAliasArray(a).slice().sort();
    const bb = normalizeAliasArray(b).slice().sort();
    if (aa.length !== bb.length) return false;
    return aa.every((item, idx) => item === bb[idx]);
}

function diffPublicPlayers(basePlayers, proposedPlayers) {
    const base = basePlayers || {};
    const proposed = proposedPlayers || {};
    const baseNames = new Set(Object.keys(base));
    const proposedNames = new Set(Object.keys(proposed));
    const addedMains = [];
    const removedMains = [];
    const changedMains = [];
    let unchangedMains = 0;

    for (const mainName of proposedNames) {
        if (!baseNames.has(mainName)) {
            addedMains.push(mainName);
        } else if (!sameAliasList(base[mainName], proposed[mainName])) {
            changedMains.push(mainName);
        } else {
            unchangedMains++;
        }
    }

    for (const mainName of baseNames) {
        if (!proposedNames.has(mainName)) removedMains.push(mainName);
    }

    return {
        addedMainCount: addedMains.length,
        changedMainCount: changedMains.length,
        removedMainCount: removedMains.length,
        unchangedMainCount: unchangedMains,
        addedMains: addedMains.slice(0, 50),
        changedMains: changedMains.slice(0, 50),
        removedMains: removedMains.slice(0, 50)
    };
}

function hasPublicPlayerChanges(diff) {
    return !!diff && (diff.addedMainCount > 0 || diff.changedMainCount > 0 || diff.removedMainCount > 0);
}

async function getJsonObject(client, key, fallbackValue) {
    try {
        const result = await client.get(key);
        return JSON.parse(result.content.toString());
    } catch (e) {
        if (e.name === 'NoSuchKeyError') return fallbackValue;
        throw e;
    }
}

async function putJsonObject(client, key, value) {
    await client.put(
        key,
        Buffer.from(JSON.stringify(value, null, 2), 'utf8'),
        { headers: { 'Content-Type': 'application/json; charset=utf-8' } }
    );
}

async function deleteObjectIfExists(client, key) {
    try {
        await client.delete(key);
        return true;
    } catch (e) {
        if (e.name === 'NoSuchKeyError') return false;
        throw e;
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
            const cleanAliases = normalizeAliasArray(aliases).filter(aliasName => aliasName !== cleanMain);
            if (cleanMain && cleanAliases.length > 0) normalized.players[cleanMain] = cleanAliases;
        }
    }

    return normalized;
}

async function loadPublicAliasDb(client) {
    return normalizePublicDb(await getJsonObject(client, PUBLIC_ALIAS_DB_KEY, {
        version: 0,
        updatedAt: 0,
        players: {}
    }));
}

function findDuplicateAliasOwners(publicDb, aliases, expectedMainName) {
    const warnings = [];
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
        if (owners.length > 0) warnings.push({ aliasName, aliasId, matchedAliases, owners, requestedOwner: expectedMainName });
    }
    return warnings;
}

function diffAliasArrays(beforeAliases, afterAliases) {
    const before = normalizeAliasArray(beforeAliases);
    const after = normalizeAliasArray(afterAliases);
    return {
        beforeAliases: before,
        afterAliases: after,
        addedAliases: after.filter(aliasName => !before.includes(aliasName)),
        removedAliases: before.filter(aliasName => !after.includes(aliasName))
    };
}

function filterContainedNakedAliases(publicAliases, targetAliases, mainName) {
    const publicList = normalizeAliasArray(publicAliases);
    const targetList = normalizeAliasArray(targetAliases).filter(aliasName => aliasName !== mainName);
    const kept = [];
    let containedNakedAliasCount = 0;

    for (const aliasName of targetList) {
        if (!aliasHasDeclaredJob(aliasName)) {
            const matchedPublicAliases = publicList.filter(publicAlias => cloudAliasContainsNakedAlias(publicAlias, aliasName));
            if (matchedPublicAliases.length > 0) {
                containedNakedAliasCount++;
                for (const publicAlias of matchedPublicAliases) {
                    mergeAliasIntoArray(kept, publicAlias);
                }
                continue;
            }
        }
        mergeAliasIntoArray(kept, aliasName);
    }

    return { aliases: kept, containedNakedAliasCount };
}

function buildAliasReviewFingerprintPayload(mainName, aliases, operation, diff) {
    const cleanMain = cleanName(mainName);
    const normalizedDiff = diff && typeof diff === 'object' ? diff : {};
    return {
        schemaVersion: 1,
        mainName: cleanMain,
        operation: String(operation || (normalizeAliasArray(aliases).length === 0 ? 'delete' : 'replace')),
        aliases: normalizeAliasArray(aliases).filter(aliasName => aliasName !== cleanMain),
        addedAliases: normalizeAliasArray(normalizedDiff.addedAliases),
        removedAliases: normalizeAliasArray(normalizedDiff.removedAliases)
    };
}

function getAliasReviewFingerprint(mainName, aliases, operation, diff) {
    return sha256(JSON.stringify(buildAliasReviewFingerprintPayload(mainName, aliases, operation, diff)));
}

function normalizeAdminKeyHashes(value) {
    const rawList = Array.isArray(value)
        ? value
        : (Array.isArray(value?.keyHashes) ? value.keyHashes : []);

    return normalizeStringArray(rawList)
        .map(item => item.toLowerCase())
        .filter(item => /^[0-9a-f]{64}$/.test(item));
}

async function validateAdminLicense(client, licenseKey) {
    const adminConfig = await getJsonObject(client, ADMIN_KEY_HASHES_KEY, { keyHashes: [] });
    const allowedHashes = normalizeAdminKeyHashes(adminConfig);
    return allowedHashes.includes(sha256(licenseKey).toLowerCase());
}

function getPendingMainKey(mainName) {
    return `${PENDING_ALIAS_PREFIX}main-${sha256(mainName).slice(0, 16)}.json`;
}

function getRejectedBlockKey(mainName, fingerprint) {
    return `${REJECTED_BLOCK_PREFIX}main-${sha256(mainName).slice(0, 16)}/${fingerprint}.json`;
}

async function getActiveRejectedBlock(client, mainName, fingerprint, now) {
    const marker = await getJsonObject(client, getRejectedBlockKey(mainName, fingerprint), null);
    if (!marker || typeof marker !== 'object') return null;
    const expireAt = Number(marker.expireAt || 0);
    const rejectedAt = Number(marker.rejectedAt || 0);
    const maxExpireAt = rejectedAt > 0 ? rejectedAt + REJECTED_BLOCK_TTL_SEC : expireAt;
    return expireAt > now && maxExpireAt > now ? marker : null;
}

function createPendingAggregate(mainName, now) {
    return {
        id: `main-${sha256(mainName).slice(0, 16)}`,
        type: 'alias-main-aggregate',
        status: 'pending',
        mainName,
        aliases: [],
        operation: 'replace',
        diff: {
            beforeAliases: [],
            afterAliases: [],
            addedAliases: [],
            removedAliases: []
        },
        conflicts: [],
        duplicateOwners: [],
        submitterKeyHashes: [],
        hwidHashes: [],
        sourceCount: 0,
        createdAt: now,
        updatedAt: now,
        entryCount: 1,
        pairCount: 0
    };
}

function normalizePendingAggregate(raw, mainName, now) {
    const aggregate = raw && typeof raw === 'object'
        ? { ...createPendingAggregate(mainName, now), ...raw, mainName }
        : createPendingAggregate(mainName, now);

    aggregate.aliases = normalizeAliasArray(aggregate.aliases).filter(aliasName => aliasName !== mainName);
    aggregate.diff = aggregate.diff && typeof aggregate.diff === 'object' ? aggregate.diff : {};
    aggregate.diff.beforeAliases = normalizeAliasArray(aggregate.diff.beforeAliases);
    aggregate.diff.afterAliases = normalizeAliasArray(aggregate.diff.afterAliases);
    aggregate.diff.addedAliases = normalizeAliasArray(aggregate.diff.addedAliases);
    aggregate.diff.removedAliases = normalizeAliasArray(aggregate.diff.removedAliases);
    aggregate.conflicts = Array.isArray(aggregate.conflicts) ? aggregate.conflicts : [];
    aggregate.duplicateOwners = Array.isArray(aggregate.duplicateOwners) ? aggregate.duplicateOwners : [];
    aggregate.operation = aggregate.aliases.length === 0 ? 'delete' : String(aggregate.operation || 'replace');
    aggregate.submitterKeyHashes = normalizeStringArray(aggregate.submitterKeyHashes);
    aggregate.hwidHashes = normalizeStringArray(aggregate.hwidHashes);
    aggregate.sourceCount = Number(aggregate.sourceCount || 0);
    aggregate.entryCount = 1;
    aggregate.pairCount = aggregate.aliases.length;
    aggregate.updatedAt = now;
    return aggregate;
}

async function handleVerifyLicense(client, data, res) {
    const licenseKey = data.key;
    const hwid = data.hwid;
    const duration = data.duration || 0;

    if (!licenseKey || !hwid) {
        return res.json({ status: 'error', msg: '缺少参数' });
    }

    if (!isValidNewLicenseKey(licenseKey)) {
        return res.json({ status: 'error', msg: '此旧版卡密已停用，请下载最新版软件并联系管理员更换 CDK！' });
    }

    const record = await getLicenseRecord(client, licenseKey);

    if (record.exists) {
        const valid = validateBoundLicense(record, hwid);
        if (!valid.ok) return res.json({ status: 'error', msg: valid.msg });
        return res.json({
            status: 'ok',
            msg: '验证通过',
            expireTime: record.expireTime,
            cloudServerUrl: CLOUD_MATCH_SERVER_URL
        });
    }

    let expireTime = 0;
    if (duration > 0) {
        expireTime = duration === 0xFFFFFFFF ? 0xFFFFFFFF : nowSec() + duration;
    }

    const writeContent = expireTime > 0 ? `${hwid}|${expireTime}` : hwid;
    try {
        await client.put(`${LICENSE_PREFIX}${licenseKey}.txt`, Buffer.from(writeContent), {
            headers: { 'x-oss-forbid-overwrite': 'true' }
        });
        return res.json({
            status: 'ok',
            msg: '首次激活，绑定成功！',
            expireTime,
            cloudServerUrl: CLOUD_MATCH_SERVER_URL
        });
    } catch (putErr) {
        return res.json({ status: 'error', msg: '激活冲突，请重试！' });
    }
}

async function handleGetPublicAliasDb(client, data, res) {
    const licenseKey = data.key;
    const hwid = data.hwid;

    if (!licenseKey || !hwid) {
        return res.json({ status: 'error', msg: '缺少授权参数，无法同步云端库' });
    }

    if (!isValidNewLicenseKey(licenseKey)) {
        return res.json({ status: 'error', msg: '旧版卡密不允许同步云端库，请更换 CDK' });
    }

    const record = await getLicenseRecord(client, licenseKey);
    const valid = validateBoundLicense(record, hwid);
    if (!valid.ok) return res.json({ status: 'error', msg: valid.msg });

    const publicDb = await loadPublicAliasDb(client);
    const pairCount = Object.values(publicDb.players).reduce((sum, aliases) => sum + aliases.length, 0);
    return res.json({
        status: 'ok',
        msg: `已获取公共小号库：${Object.keys(publicDb.players).length} 个主号、${pairCount} 个小号`,
        publicAliasDB: publicDb
    });
}

function entriesToPublicPlayers(entries, options = {}) {
    const allowEmpty = !!options.allowEmpty;
    const players = {};
    for (const entry of entries) {
        const mainName = cleanName(entry.mainName);
        const aliases = normalizeAliasArray(entry.aliases)
            .filter(aliasName => aliasName && aliasName !== mainName)
            .slice(0, MAX_ALIASES_PER_MAIN);
        if (mainName && (aliases.length > 0 || allowEmpty)) players[mainName] = aliases;
    }
    return players;
}

async function handleDirectSyncAliasDb(client, data, res) {
    const licenseKey = data.key;
    const hwid = data.hwid;

    if (!licenseKey || !hwid) {
        return res.json({ status: 'error', msg: '缺少授权参数，无法直写公共库' });
    }

    if (!isValidNewLicenseKey(licenseKey)) {
        return res.json({ status: 'error', msg: '旧版卡密不允许直写公共库，请更换 CDK' });
    }

    const record = await getLicenseRecord(client, licenseKey);
    const valid = validateBoundLicense(record, hwid);
    if (!valid.ok) return res.json({ status: 'error', msg: valid.msg });

    if (!(await validateAdminLicense(client, licenseKey))) {
        return res.json({ status: 'error', msg: '当前卡密不在管理员直写白名单中' });
    }

    const entries = normalizeAliasSubmission(data.aliasDB || data.fullAliasDB, MAX_PUBLIC_ALIAS_MAINS, MAX_PUBLIC_ALIASES_PER_MAIN, { allowEmpty: true });
    if (entries.length === 0) {
        return res.json({ status: 'error', msg: '没有可直写的公共库数据' });
    }

    const oldDb = await loadPublicAliasDb(client);
    const players = entriesToPublicPlayers(entries);
    const publicDb = {
        version: oldDb.version + 1,
        updatedAt: nowSec(),
        players,
        audit: [
            ...oldDb.audit,
            {
                action: 'admin_direct_sync',
                reviewedAt: nowSec(),
                submitterKeyHash: sha256(licenseKey),
                mainCount: Object.keys(players).length,
                pairCount: Object.values(players).reduce((sum, aliases) => sum + aliases.length, 0)
            }
        ].slice(-200)
    };

    await putJsonObject(client, PUBLIC_ALIAS_DB_KEY, publicDb);

    const mainCount = Object.keys(players).length;
    const pairCount = Object.values(players).reduce((sum, aliases) => sum + aliases.length, 0);
        return res.json({
            status: 'ok',
            action: 'direct_sync_alias_db',
            directSync: true,
            msg: `管理员直写完成：公共库已更新为 ${mainCount} 个主号、${pairCount} 个小号`,
            version: publicDb.version,
            mainCount,
        pairCount
    });
}

async function handleSubmitAliasDb(client, data, res) {
    const licenseKey = data.key;
    const hwid = data.hwid;

    if (!licenseKey || !hwid) {
        return res.json({ status: 'error', msg: '缺少授权参数，无法上传共享库' });
    }

    if (!isValidNewLicenseKey(licenseKey)) {
        return res.json({ status: 'error', msg: '旧版卡密不允许上传共享库，请更换 CDK' });
    }

    const record = await getLicenseRecord(client, licenseKey);
    const valid = validateBoundLicense(record, hwid);
    if (!valid.ok) return res.json({ status: 'error', msg: valid.msg });

    const submittedEntries = normalizeAliasSubmission(data.aliasDB || data.fullAliasDB, MAX_PUBLIC_ALIAS_MAINS, MAX_PUBLIC_ALIASES_PER_MAIN, { allowEmpty: true });
    const publicDb = await loadPublicAliasDb(client);
    const submittedMap = new Map(submittedEntries.map(entry => [entry.mainName, entry.aliases]));
    const deleteScopeMainNames = normalizeMainNameArray(data.deleteScopeMainNames, MAX_PUBLIC_ALIAS_MAINS);
    const candidateMainNames = new Set(submittedMap.keys());
    for (const mainName of deleteScopeMainNames) {
        if (!submittedMap.has(mainName) && publicDb.players[mainName]) {
            candidateMainNames.add(mainName);
        }
    }
    const entries = Array.from(candidateMainNames).slice(0, MAX_PUBLIC_ALIAS_MAINS).map(mainName => ({
        mainName,
        aliases: submittedMap.get(mainName) || []
    }));

    if (entries.length === 0) {
        return res.json({ status: 'error', msg: '没有可上传的小号数据' });
    }

    const createdAt = nowSec();
    const submitterKeyHash = sha256(licenseKey);
    const hwidHash = sha256(hwid);
    const clientVersion = cleanName(data.clientVersion || '');

    let touchedMainCount = 0;
    let targetPairCount = 0;
    let unchangedMainCount = 0;
    let deleteMainCount = 0;
    let duplicateHintCount = 0;
    let rejectedDuplicateCount = 0;
    let containedNakedAliasCount = 0;
    let stalePendingClearedCount = 0;

    for (const entry of entries) {
        const mainName = entry.mainName;
        const publicAliases = publicDb.players[mainName] || [];
        const key = getPendingMainKey(mainName);
        const existing = await getJsonObject(client, key, null);
        const aggregate = normalizePendingAggregate(existing, mainName, createdAt);

        const filterResult = filterContainedNakedAliases(publicAliases, entry.aliases, mainName);
        const targetAliases = filterResult.aliases;
        containedNakedAliasCount += filterResult.containedNakedAliasCount;
        if ((publicAliases.length === 0 && targetAliases.length === 0) || sameAliasSet(publicAliases, targetAliases)) {
            unchangedMainCount++;
            if (existing) {
                if (await deleteObjectIfExists(client, key)) stalePendingClearedCount++;
            }
            continue;
        }

        const operation = targetAliases.length === 0 ? 'delete' : 'replace';
        const diff = diffAliasArrays(publicAliases, targetAliases);
        const fingerprint = getAliasReviewFingerprint(mainName, targetAliases, operation, diff);
        const rejectedBlock = await getActiveRejectedBlock(client, mainName, fingerprint, createdAt);
        if (rejectedBlock) {
            rejectedDuplicateCount++;
            continue;
        }

        aggregate.aliases = targetAliases;
        aggregate.operation = operation;
        aggregate.diff = diff;
        aggregate.conflicts = [];
        aggregate.duplicateOwners = findDuplicateAliasOwners(publicDb, targetAliases, mainName);
        duplicateHintCount += aggregate.duplicateOwners.length;

        if (!aggregate.submitterKeyHashes.includes(submitterKeyHash)) aggregate.submitterKeyHashes.push(submitterKeyHash);
        if (!aggregate.hwidHashes.includes(hwidHash)) aggregate.hwidHashes.push(hwidHash);

        aggregate.sourceCount = aggregate.submitterKeyHashes.length;
        aggregate.clientVersions = normalizeStringArray([...(aggregate.clientVersions || []), clientVersion].filter(Boolean));
        aggregate.pairCount = aggregate.aliases.length;
        aggregate.updatedAt = createdAt;

        await client.put(
            key,
            Buffer.from(JSON.stringify(aggregate, null, 2), 'utf8'),
            { headers: { 'Content-Type': 'application/json; charset=utf-8' } }
        );
        touchedMainCount++;
        targetPairCount += targetAliases.length;
        if (targetAliases.length === 0) deleteMainCount++;
    }

    if (touchedMainCount === 0) {
        const msgParts = [];
        if (rejectedDuplicateCount > 0) {
            msgParts.push(`14 天内已驳回过相同投稿 ${rejectedDuplicateCount} 个，已自动过滤`);
        }
        if (containedNakedAliasCount > 0) {
            msgParts.push(`云端已有包含该裸 ID 的小号 ${containedNakedAliasCount} 个，已自动过滤`);
        }
        if (unchangedMainCount > 0) {
            msgParts.push(`未变化主号 ${unchangedMainCount} 个`);
        }

        return res.json({
            status: 'ok',
            action: 'submit_alias_db',
            aliasSubmit: true,
            msg: msgParts.length > 0 ? msgParts.join('；') : '没有发现需要审核的共享库变更',
            entryCount: 0,
            pairCount: 0,
            unchangedMainCount,
            rejectedDuplicateCount,
            containedNakedAliasCount,
            stalePendingClearedCount
        });
    }

    const rejectedDuplicateMsg = rejectedDuplicateCount > 0 ? `、过滤重复驳回 ${rejectedDuplicateCount} 个` : '';
    const containedNakedAliasMsg = containedNakedAliasCount > 0 ? `、过滤裸ID ${containedNakedAliasCount} 个` : '';
    return res.json({
        status: 'ok',
        action: 'submit_alias_db',
        aliasSubmit: true,
        msg: `已提交到待审核区：${touchedMainCount} 个主号、目标小号 ${targetPairCount} 个、删除主号 ${deleteMainCount} 个${rejectedDuplicateMsg}${containedNakedAliasMsg}`,
        entryCount: touchedMainCount,
        pairCount: targetPairCount,
        deleteMainCount,
        duplicateHintCount,
        rejectedDuplicateCount,
        containedNakedAliasCount,
        stalePendingClearedCount
    });
}

app.post(/.*/, async (req, res) => {
    try {
        const data = req.body || {};
        const client = createOssClient();

        if (data.action === 'submit_alias_db') {
            return await handleSubmitAliasDb(client, data, res);
        }

        if (data.action === 'get_public_alias_db') {
            return await handleGetPublicAliasDb(client, data, res);
        }

        if (data.action === 'direct_sync_alias_db') {
            return await handleDirectSyncAliasDb(client, data, res);
        }

        if (data.action) {
            return res.json({ status: 'error', msg: `未知云端动作: ${data.action}` });
        }

        return await handleVerifyLicense(client, data, res);
    } catch (err) {
        return res.json({ status: 'error', msg: '服务器内部错误: ' + err.message });
    }
});

app.listen(9000, '0.0.0.0', () => {
    console.log('Server is running on port 9000');
});
