const assert = require('node:assert/strict');
const { readFileSync, existsSync } = require('node:fs');
const { spawnSync } = require('node:child_process');
const path = require('node:path');
const { test } = require('node:test');

const root = path.resolve(__dirname, '..');
const installer = readFileSync(path.join(root, 'cloud-match-server/deploy/install-test.sh'), 'utf8').replaceAll('\r\n', '\n');
const expected = 'Aym724794';
const bash = process.platform === 'win32' ? 'C:/Program Files/Git/bin/bash.exe' : 'bash';
const start = installer.indexOf('ADMIN_PASSWORD="$(sed');
const end = installer.indexOf('\nfi', start);
assert(start >= 0 && end > start, 'Installer password initialization block must exist');
const initialize = installer.slice(start, end + 3);

function run(initial) {
  if (process.platform === 'win32') assert(existsSync(bash), 'Git Bash is required for installer checks');
  // Exercise only the real password-selection block; never run installation or touch /etc.
  const result = spawnSync(bash, ['--noprofile', '--norc', '-c', `set -euo pipefail
ENV_FILE=/unused-fixture
sed() { printf '%s\\n' "$INITIAL_PASSWORD"; }
set_env_value() { printf 'WRITE:%s=%s\\n' "$1" "$2"; }
${initialize}
printf 'RESULT:%s\\n' "$ADMIN_PASSWORD"
`], { encoding: 'utf8', env: { ...process.env, INITIAL_PASSWORD: initial } });
  assert.equal(result.status, 0, result.stderr || result.error?.message);
  return result.stdout.trim().split(/\r?\n/);
}

test('test installation template declares the requested default', () => {
  const template = readFileSync(path.join(root, 'cloud-match-server/deploy/test-server.env'), 'utf8');
  assert.match(template, new RegExp(`^ADMIN_PASSWORD=${expected}$`, 'm'));
  assert.match(template, /^PORT=28880$/m);
});

test('test installation template enables legacy permanent card compatibility', () => {
  const template = readFileSync(path.join(root, 'cloud-match-server/deploy/test-server.env'), 'utf8');
  assert.match(template, /^ALLOW_LEGACY_PERMANENT_KEYS=true$/m);
});

test('test installer enables missing legacy compatibility but preserves an explicit setting', () => {
  const initialize = installer.match(/if ! grep[^\n]*ALLOW_LEGACY_PERMANENT_KEYS[^\n]*\n[\s\S]*?\nfi/)?.[0];
  assert(initialize, 'Installer legacy compatibility block must exist');
  for (const [input, present] of [
    ['', false], ['PORT=28880\n', false], ['#ALLOW_LEGACY_PERMANENT_KEYS=false\n', false],
    ['ALLOW_LEGACY_PERMANENT_KEYS_EXTRA=false\n', false], ['ALLOW_LEGACY_PERMANENT_KEYS=false\n', true],
    [' ALLOW_LEGACY_PERMANENT_KEYS=false\n', true], ['\tALLOW_LEGACY_PERMANENT_KEYS = false\n', true],
    ['ALLOW_LEGACY_PERMANENT_KEYS=true\n', true],
  ]) {
    const result = spawnSync(bash, ['--noprofile', '--norc', '-c', `set -euo pipefail
ENV_FILE=/unused-fixture
grep() { printf '%s\\n' "$FIXTURE_ENV" | command grep "$1" "$2"; }
set_env_value() { printf 'WRITE:%s=%s\\n' "$1" "$2"; }
${initialize}
`], { encoding: 'utf8', env: { ...process.env, FIXTURE_ENV: input } });
    assert.equal(result.status, 0, result.stderr || result.error?.message);
    assert.equal(result.stdout.trim(), present ? '' : 'WRITE:ALLOW_LEGACY_PERMANENT_KEYS=true', JSON.stringify(input));
  }
});
test('empty test password is initialized with the requested default', () => {
  assert.deepEqual(run(''), [`WRITE:ADMIN_PASSWORD=${expected}`, `RESULT:${expected}`]);
});
test('existing test password is preserved without a configuration write', () => {
  assert.deepEqual(run('existing-test-password'), ['RESULT:existing-test-password']);
});

test('package guard rejects duplicate or private password assignments', { skip: process.platform !== 'win32' }, () => {
  const check = `
$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($env:PACKAGE_SCRIPT, [ref]$tokens, [ref]$errors)
$guard = $ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Assert-TestPackageEnvironment'}, $true)
if (-not $guard) { throw 'Package guard function is missing.' }
. ([ScriptBlock]::Create($guard.Extent.Text))
$normal = 'ADMIN_PASSWORD=Aym724794' + [Environment]::NewLine + 'PORT=28880'
Assert-TestPackageEnvironment $normal
foreach ($bad in @($normal + [Environment]::NewLine + 'ADMIN_PASSWORD=private-fixture',
    $normal + [Environment]::NewLine + ' ADMIN_PASSWORD=private-fixture',
    $normal + [Environment]::NewLine + 'export ADMIN_PASSWORD=private-fixture',
    $normal + [Environment]::NewLine + 'PORT=18880',
    'ADMIN_PASSWORD=private-fixture' + [Environment]::NewLine + 'PORT=28880')) {
    $rejected = $false
    try { Assert-TestPackageEnvironment $bad } catch { $rejected = $true }
    if (-not $rejected) { throw 'Invalid test-package configuration was accepted.' }
}
Write-Output 'Package guard passed.'
`;
  const result = spawnSync('powershell', ['-NoProfile', '-Command', check], {
    encoding: 'utf8', env: { ...process.env, PACKAGE_SCRIPT: path.join(root, 'scripts/package-test-identity-admin.ps1') },
  });
  assert.equal(result.status, 0, result.stderr || result.error?.message);
  assert.match(result.stdout, /Package guard passed/);
});
