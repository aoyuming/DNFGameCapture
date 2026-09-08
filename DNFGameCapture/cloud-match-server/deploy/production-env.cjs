'use strict';
const { randomBytes } = require('node:crypto');
const { readFileSync, writeFileSync } = require('node:fs');
const { posix } = require('node:path');

const PUBLIC_URL = 'http://47.109.149.111:18880';
const DATA_DIR = '/var/lib/dnf-cloud-match';

function readEnvironment(text) {
  const lines = text.replace(/^\uFEFF/, '').replace(/\r\n/g, '\n').split('\n');
  const values = new Map();
  for (const line of lines) {
    if (/^\s*(?:[#;].*)?$/.test(line)) continue;
    const match = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=(.*)$/.exec(line);
    if (!match || values.has(match[1])) throw new Error('Ambiguous environment assignment; use unique single-line KEY=value entries.');
    let value = match[2].trim();
    // Reject continuation/expansion/compound quoting rather than reinterpret credentials or paths.
    if (value.startsWith('"') || value.startsWith("'")) {
      const quote = value[0];
      if (value.length < 2 || !value.endsWith(quote) || value.slice(1, -1).includes(quote)) {
        throw new Error('Unsupported environment quoting; use single-line values.');
      }
      value = value.slice(1, -1);
    } else if (/["']/.test(value)) throw new Error('Unsupported environment quoting.');
    if (/[\\\r\x00]/.test(value)) throw new Error('Unsupported environment escape or continuation.');
    values.set(match[1], value);
  }
  return { lines, values };
}

function prepareProductionEnvironment(text, dataDir = DATA_DIR, requireDatabasePath = false) {
  const { lines, values } = readEnvironment(text);
  if (requireDatabasePath && !values.has('DATABASE_PATH')) {
    throw new Error('Existing database location is unknown: DATABASE_PATH must be configured before upgrading.');
  }
  const databasePath = values.get('DATABASE_PATH') ?? `${dataDir}/cloud-match.sqlite`;
  if (!databasePath.startsWith(dataDir + '/') || posix.normalize(databasePath) !== databasePath ||
      !/^\/[A-Za-z0-9_./-]+$/.test(databasePath) || databasePath.endsWith('/')) {
    throw new Error('Unsafe database path: preserve a regular database inside the production data directory.');
  }
  const updates = new Map(Object.entries({ NODE_ENV: 'production', PORT: '18880', ADMIN_PORT: '18881', PUBLIC_URL }));
  for (const [key, value] of Object.entries({ HOST: '0.0.0.0', ADMIN_HOST: '127.0.0.1', DATABASE_PATH: databasePath,
    ALLOW_LEGACY_PERMANENT_KEYS: 'true' })) if (!values.has(key)) updates.set(key, value);
  if (!values.get('ADMIN_PASSWORD')?.trim()) updates.set('ADMIN_PASSWORD', randomBytes(24).toString('hex'));
  const remaining = new Map(updates);
  const output = lines.map(line => {
    const key = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=/.exec(line)?.[1];
    if (!key || !updates.has(key)) return line;
    remaining.delete(key);
    return `${key}=${updates.get(key)}`;
  });
  while (output.at(-1) === '') output.pop();
  for (const [key, value] of remaining) output.push(`${key}=${value}`);
  const allowLegacyPermanentKeys = /^(true|1)$/i.test((values.get('ALLOW_LEGACY_PERMANENT_KEYS') ?? 'true').trim());
  return { text: output.join('\n') + '\n', databasePath, allowLegacyPermanentKeys };
}

function checkHealth(health, expectedLegacy) {
  if (health?.ok !== true || health.protocolVersion !== 2 || health.cloudServerUrl !== PUBLIC_URL ||
      typeof health.allowLegacyPermanentKeys !== 'boolean' ||
      (expectedLegacy !== undefined && health.allowLegacyPermanentKeys !== expectedLegacy)) {
    throw new Error('Production v2 health/configuration mismatch; do not distribute the client.');
  }
  return health.allowLegacyPermanentKeys;
}

module.exports = { prepareProductionEnvironment, checkHealth, readEnvironment };
if (require.main === module) {
  try {
    const [command, input, output, dataDir, existing] = process.argv.slice(2);
    if (command === 'prepare' && input && output) {
      const prepared = prepareProductionEnvironment(readFileSync(input, 'utf8'), dataDir, existing === '1');
      writeFileSync(output, prepared.text, { flag: 'wx', mode: 0o600 });
      process.stdout.write(prepared.databasePath + '\n');
    } else if (command === 'check-health' && input) {
      const expected = output ? /^(true|1)$/i.test(readEnvironment(readFileSync(output, 'utf8')).values.get('ALLOW_LEGACY_PERMANENT_KEYS')?.trim() ?? '') : undefined;
      const enabled = checkHealth(JSON.parse(readFileSync(input, 'utf8')), expected);
      process.stdout.write(`Production protocol v2 ready; ALLOW_LEGACY_PERMANENT_KEYS=${enabled}\n`);
      if (!enabled) process.stderr.write('WARNING: explicit policy disables enrollment of unregistered old permanent cards.\n');
    } else throw new Error('Usage: production-env.cjs prepare input output [dataDir] [existing=1] | check-health json [env]');
  } catch (error) { process.stderr.write(error.message + '\n'); process.exitCode = 1; }
}
