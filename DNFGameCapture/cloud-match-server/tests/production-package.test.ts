import { existsSync, readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { expect, test } from 'vitest';

const read = (path: string) => readFileSync(new URL(path, import.meta.url), 'utf8');
const script = new URL('../../scripts/package-production-5.2.0.ps1', import.meta.url);

test('production package has its own clean compilation, whitelist, checksums and validation-only mode', () => {
  expect(existsSync(script)).toBe(true);
  const source = readFileSync(script, 'utf8');
  for (const marker of ['ValidateOnly', 'tsconfig.json', '--outDir', 'production-env.cjs', 'preflight.sh',
    'README-production-5.2.0.md', 'SHA256SUMS.txt', 'serverFirstRequired', 'remove-adventure-identifiers.js']) expect(source).toContain(marker);
  expect(source).not.toContain('package-test-identity-admin.ps1');
});

test('production template contains no deployment password or test routing', () => {
  const source = read('../deploy/server.env');
  const require = createRequire(import.meta.url);
  const values: Map<string, string> = require(fileURLToPath(new URL('../deploy/production-env.cjs', import.meta.url))).readEnvironment(source).values;
  expect(Object.fromEntries(values)).toEqual({ NODE_ENV: 'production', HOST: '0.0.0.0', PORT: '18880',
    PUBLIC_URL: 'http://47.109.149.111:18880', ADMIN_HOST: '127.0.0.1', ADMIN_PORT: '18881', ADMIN_PASSWORD: '',
    DATABASE_PATH: '/var/lib/dnf-cloud-match/cloud-match.sqlite', ALLOW_LEGACY_PERMANENT_KEYS: 'true' });
  const unit = read('../deploy/dnf-cloud-match.service');
  expect(unit).toContain('EnvironmentFile=/etc/default/dnf-cloud-match');
  expect(unit).toContain('ReadWritePaths=/var/lib/dnf-cloud-match');
  expect(unit).not.toContain('dnf-cloud-match-test');
});

test('production README blocks client rollout until server v2 is ready and documents data recovery', () => {
  const path = new URL('../README-production-5.2.0.md', import.meta.url);
  expect(existsSync(path)).toBe(true);
  const source = readFileSync(path, 'utf8');
  for (const marker of ['cloud-server-prod.json', 'server-first', '401', '404', '18880', '18881',
    '.license-key', 'backup.complete', 'ALLOW_LEGACY_PERMANENT_KEYS=false', 'first-use', 'test database',
    'approved', 'preflight.sh']) expect(source).toContain(marker);
});
