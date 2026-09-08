'use strict';

// Function Compute may dispatch multiple HTTP requests to the same Node
// runtime at once.  Keep the read/merge/write part of append-only submissions
// serialized so a later request cannot write a snapshot based on stale pending
// data and erase an earlier append.
let aliasAppendQueue = Promise.resolve();

function withAliasAppendLock(work) {
    if (typeof work !== 'function') {
        return Promise.reject(new TypeError('append lock requires a function'));
    }

    const previous = aliasAppendQueue;
    let release;
    aliasAppendQueue = new Promise(resolve => {
        release = resolve;
    });

    return previous.then(() => work()).finally(() => {
        release();
    });
}

// The automatic path is intentionally append-only.  Keeping this small,
// dependency-free module separate makes the no-delete guarantee easy to test
// without constructing an OSS client.
function cleanAppendName(value) {
    return String(value || '')
        .replace(/[\r\n\t]/g, ' ')
        .replace(/[=|]/g, '')
        .trim()
        .slice(0, 60);
}

function appendAliasId(value) {
    const clean = cleanAppendName(value);
    if (!clean) return '';
    const halfSharp = clean.indexOf('#');
    const fullSharp = clean.indexOf('＃');
    let sharp = -1;
    if (halfSharp >= 0 && fullSharp >= 0) sharp = Math.min(halfSharp, fullSharp);
    else sharp = halfSharp >= 0 ? halfSharp : fullSharp;
    return (sharp >= 0 ? clean.slice(0, sharp) : clean).trim();
}

function appendAliasJob(value) {
    const clean = cleanAppendName(value);
    if (!clean) return '';
    const halfSharp = clean.indexOf('#');
    const fullSharp = clean.indexOf('＃');
    let sharp = -1;
    if (halfSharp >= 0 && fullSharp >= 0) sharp = Math.min(halfSharp, fullSharp);
    else sharp = halfSharp >= 0 ? halfSharp : fullSharp;
    return sharp >= 0 ? clean.slice(sharp + 1).trim() : '';
}

function appendSameAliasId(a, b) {
    const aa = appendAliasId(a);
    const bb = appendAliasId(b);
    return !!aa && !!bb && aa === bb;
}

function appendSameStorageEntry(a, b) {
    const aa = cleanAppendName(a);
    const bb = cleanAppendName(b);
    if (!aa || !bb) return false;
    if (aa === bb) return true;
    if (!appendSameAliasId(aa, bb)) return false;
    const aj = appendAliasJob(aa);
    const bj = appendAliasJob(bb);
    return !(aj || bj) || (!!aj && !!bj && aj === bj);
}

function mergeAppendAlias(out, rawAlias) {
    const alias = cleanAppendName(rawAlias);
    if (!alias) return false;
    const aliasJob = appendAliasJob(alias);
    for (let i = 0; i < out.length; i++) {
        const existing = cleanAppendName(out[i]);
        if (!existing) continue;
        const existingJob = appendAliasJob(existing);
        if (appendSameStorageEntry(existing, alias)) {
            if (!existingJob && aliasJob) {
                out[i] = alias;
                return true;
            }
            return false;
        }
        if (appendSameAliasId(existing, alias)) {
            if (!existingJob && aliasJob) {
                out[i] = alias;
                return true;
            }
            if (existingJob && !aliasJob) return false;
        }
    }
    out.push(alias);
    return true;
}

function parseAppendAliasValue(value) {
    if (Array.isArray(value)) return value;
    if (typeof value === 'string') {
        const parts = value.split(/[()（）]/).map(item => item.trim()).filter(Boolean);
        return parts.length > 0 ? parts : value.split(/[,，;；]/);
    }
    return [];
}

function normalizeAppendAliases(value, mainName = '') {
    const aliases = [];
    for (const rawAlias of parseAppendAliasValue(value)) {
        const alias = cleanAppendName(rawAlias);
        if (!alias || alias === mainName) continue;
        mergeAppendAlias(aliases, alias);
    }
    return aliases;
}

function normalizeAppendOnlyAliasDb(rawAliasDb, maxMains = 300, maxAliasesPerMain = 20) {
    if (!rawAliasDb || typeof rawAliasDb !== 'object' || Array.isArray(rawAliasDb)) {
        return [];
    }

    const entries = [];
    const seenMains = new Set();
    for (const [rawMainName, rawAliases] of Object.entries(rawAliasDb)) {
        if (entries.length >= maxMains) break;
        const mainName = cleanAppendName(rawMainName);
        if (!mainName || seenMains.has(mainName)) continue;
        seenMains.add(mainName);
        const aliases = normalizeAppendAliases(rawAliases, mainName)
            .slice(0, maxAliasesPerMain);
        // Empty values are deletion requests in the legacy protocol and are
        // deliberately ignored by the automatic append-only path.
        if (aliases.length > 0) entries.push({ mainName, aliases });
    }
    return entries;
}

function normalizeAppendPlayers(rawPlayers) {
    const players = {};
    if (!rawPlayers || typeof rawPlayers !== 'object' || Array.isArray(rawPlayers)) {
        return players;
    }
    for (const [rawMainName, rawAliases] of Object.entries(rawPlayers)) {
        const mainName = cleanAppendName(rawMainName);
        if (!mainName) continue;
        const aliases = normalizeAppendAliases(rawAliases, mainName);
        if (aliases.length > 0) players[mainName] = aliases;
    }
    return players;
}

function mergeAppendOnlyAliasDb(publicPlayers, pendingPlayers, rawAliasDb) {
    const players = {};
    const addPlayerAliases = (mainName, aliases) => {
        if (!players[mainName]) players[mainName] = [];
        for (const alias of aliases) mergeAppendAlias(players[mainName], alias);
        if (players[mainName].length === 0) delete players[mainName];
    };

    for (const [mainName, aliases] of Object.entries(normalizeAppendPlayers(publicPlayers))) {
        addPlayerAliases(mainName, aliases);
    }
    for (const [mainName, aliases] of Object.entries(normalizeAppendPlayers(pendingPlayers))) {
        addPlayerAliases(mainName, aliases);
    }
    for (const entry of normalizeAppendOnlyAliasDb(rawAliasDb)) {
        addPlayerAliases(entry.mainName, entry.aliases);
    }

    const changedMainNames = [];
    const before = normalizeAppendPlayers(publicPlayers);
    const pending = normalizeAppendPlayers(pendingPlayers);
    for (const mainName of Object.keys(players)) {
        const beforeAliases = [ ...(before[mainName] || []), ...(pending[mainName] || []) ];
        const candidate = players[mainName];
        if (beforeAliases.length !== candidate.length ||
            candidate.some(alias => !beforeAliases.includes(alias))) {
            changedMainNames.push(mainName);
        }
    }

    return {
        players,
        changedMainNames,
        hasDeletion: false
    };
}

module.exports = {
    mergeAppendAlias,
    mergeAppendOnlyAliasDb,
    normalizeAppendOnlyAliasDb,
    withAliasAppendLock
};
