import { spawnSync } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { afterEach, expect, test, vi } from 'vitest';

// Git Bash launches many short processes; full-suite CPU contention can exceed Vitest's 5s default.
vi.setConfig({ testTimeout: 35_000 });

const deploy = fileURLToPath(new URL('../deploy/', import.meta.url));
const bash = process.platform === 'win32' ? 'C:/Program Files/Git/bin/bash.exe' : '/bin/bash';
const temps: string[] = [];
afterEach(() => { for (const path of temps.splice(0)) rmSync(path, { recursive: true, force: true }); });
const shellPath = (path: string) => path.replace(/\\/g, '/').replace(/^([A-Za-z]):/, (_, drive: string) => '/' + drive.toLowerCase());

function fixture(failure = '', initialActive = true, legacy?: string) {
  const original = readFileSync(join(deploy, 'install.sh'), 'utf8').replace(/\r\n/g, '\n');
  // Never execute the old unsafe installer or any production paths on the test host.
  expect(original).toContain('BACKUP_ROOT=');
  const root = mkdtempSync(join(tmpdir(), 'dnf-installer-')); temps.push(root);
  const paths = { app: join(root, 'app'), data: join(root, 'data'), env: join(root, 'server.env'),
    unit: join(root, 'server.service'), backups: join(root, 'backups'), bin: join(root, 'bin'), package: join(root, 'package') };
  const put = (path: string, value: string) => { mkdirSync(dirname(path), { recursive: true }); writeFileSync(path, value, { mode: 0o755 }); };
  put(join(paths.app, 'dist/server.js'), 'old-app');
  put(join(paths.app, 'node_modules/dependency'), 'old-native-dependency');
  put(join(paths.data, 'cloud-match.sqlite'), 'old-database');
  put(join(paths.data, 'cloud-match.sqlite-wal'), 'old-wal');
  put(join(paths.data, 'cloud-match.sqlite-shm'), 'old-shm');
  put(join(paths.data, 'cloud-match.sqlite.license-key'), 'private-vault-key');
  put(join(paths.data, 'tokens'), 'existing-device-tokens');
  const oldEnv = `ADMIN_PASSWORD=private-original\nDATABASE_PATH=${shellPath(paths.data)}/cloud-match.sqlite\nCUSTOM_SETTING=preserved\n` +
    (legacy === undefined ? '' : `ALLOW_LEGACY_PERMANENT_KEYS=${legacy}\n`);
  put(paths.env, oldEnv); put(paths.unit, 'original-unit');
  put(join(root, 'active'), initialActive ? 'active' : 'inactive'); put(join(root, 'enabled'), 'enabled');
  put(join(paths.package, 'dist/server.js'), 'new-app');
  put(join(paths.package, 'package.json'), '{}'); put(join(paths.package, 'package-lock.json'), '{}');
  mkdirSync(join(paths.package, 'deploy'), { recursive: true });
  for (const name of ['production-env.cjs', 'preflight.sh', 'server.env', 'dnf-cloud-match.service']) {
    cpSync(join(deploy, name), join(paths.package, 'deploy', name));
  }
  let script = original.replace('if [[ ${EUID} -ne 0 ]]', 'if [[ 0 -ne 0 ]]')
    .replace('umask 077', 'umask 077\nexport PATH="$FIXTURE_BIN:$PATH"');
  for (const [key, value] of Object.entries({ INSTALL_DIR: paths.app, DATA_DIR: paths.data, ENV_FILE: paths.env,
    UNIT_FILE: paths.unit, BACKUP_ROOT: paths.backups, LOCK_FILE: join(root, 'install.lock') })) {
    script = script.replace(new RegExp(`^${key}=.*$`, 'm'), `${key}="${shellPath(value)}"`);
  }
  script = script.replaceAll('/usr/bin/node', shellPath(join(paths.bin, 'node'))).replaceAll('/usr/bin/npm', shellPath(join(paths.bin, 'npm')));
  put(join(paths.package, 'deploy/install.sh'), script);
  const stub = (name: string, body: string) => put(join(paths.bin, name), '#!/usr/bin/env bash\nset -eu\n' + body + '\n');
  stub('systemctl', `echo "systemctl $*" >> "$FIXTURE_ROOT/events"
case "$1" in
show) case "$*" in
  *LoadState*) echo loaded ;;
  *DropInPaths*) if [[ "$FAIL_POINT" == dropins ]]; then echo /etc/systemd/system/dnf-cloud-match.service.d/custom.conf; fi ;;
  *FragmentPath*) echo "$FIXTURE_UNIT" ;;
  *) cat "$FIXTURE_ROOT/active" ;;
esac ;;
is-active) [[ "$(cat "$FIXTURE_ROOT/active")" == active ]] ;;
is-enabled) cat "$FIXTURE_ROOT/enabled" ;;
stop)
  if [[ "$FAIL_POINT" == stop ]] || [[ "$FAIL_POINT" == rollback-stop && -f "$FIXTURE_ROOT/new-started" ]]; then exit 1; fi
  echo inactive > "$FIXTURE_ROOT/active"
  if [[ ! -f "$FIXTURE_ROOT/stopped" ]]; then echo old-after-stop > "$FIXTURE_DATA/cloud-match.sqlite"; touch "$FIXTURE_ROOT/stopped"; fi ;;
start|restart)
  if [[ "$(cat "$FIXTURE_APP/dist/server.js")" == new-app ]]; then
    touch "$FIXTURE_ROOT/new-started"; echo migrated-new > "$FIXTURE_DATA/cloud-match.sqlite"
    if [[ "$FAIL_POINT" == start || "$FAIL_POINT" == restore || "$FAIL_POINT" == rollback-stop ]]; then exit 1; fi
  else echo old-started >> "$FIXTURE_ROOT/events"; fi
  echo active > "$FIXTURE_ROOT/active" ;;
enable) echo enabled > "$FIXTURE_ROOT/enabled" ;;
disable) echo disabled > "$FIXTURE_ROOT/enabled" ;;
daemon-reload) : ;;
*) exit 90 ;;
esac`);
  stub('cp', `echo "cp $*" >> "$FIXTURE_ROOT/events"
if [[ "$FAIL_POINT" == backup && "$*" == *"$FIXTURE_DATA"* && "$*" == *backups* ]]; then exit 1; fi
if [[ "$FAIL_POINT" == restore && "$*" == *backups* && "$*" == *"/app $FIXTURE_APP"* ]]; then exit 1; fi
/usr/bin/cp "$@"`);
  stub('npm', `echo "npm $*" >> "$FIXTURE_ROOT/events"
[[ "$FAIL_POINT" != npm ]] || exit 1
mkdir -p node_modules
echo new-dependency > node_modules/dependency`);
  stub('node', `if [[ "$1" == --version ]]; then echo v24.14.1; else "$REAL_NODE" "$FIXTURE_BRIDGE" "$@"; fi`);
  put(join(root, 'node-bridge.cjs'), `const fs = require('node:fs');
const [helperPath, command, input, output, , existing] = process.argv.slice(2);
const helper = require(helperPath);
if(command === 'prepare') {
 const source = fs.readFileSync(input,'utf8');
 const dataDir = require('node:path').posix.dirname(helper.readEnvironment(source).values.get('DATABASE_PATH'));
 const result = helper.prepareProductionEnvironment(source, dataDir, existing === '1');
 fs.writeFileSync(output,result.text,{flag:'wx'}); process.stdout.write(result.databasePath+'\\n');
} else if(command === 'check-health') {
 const expected = output ? /^(true|1)$/i.test(helper.readEnvironment(fs.readFileSync(output,'utf8')).values.get('ALLOW_LEGACY_PERMANENT_KEYS') || '') : undefined;
 helper.checkHealth(JSON.parse(fs.readFileSync(input,'utf8')),expected);
} else process.exit(2);
`);
  stub('curl', `echo "curl $*" >> "$FIXTURE_ROOT/events"
[[ "$FAIL_POINT" != health ]] || exit 22
output=''; url=''
while (( $# )); do
  case "$1" in -o|--output) output="$2"; shift 2 ;; *) url="$1"; shift ;; esac
done
if [[ "$url" == */player-library ]]; then printf '%s' "$FIXTURE_LIBRARY_STATUS"; exit 0; fi
body='{"ok":true}'
if [[ "$url" == */api/v2/health ]]; then
  flag=true; if grep -q '^ALLOW_LEGACY_PERMANENT_KEYS=false$' "$FIXTURE_ENV"; then flag=false; fi
  body="{\\"ok\\":true,\\"protocolVersion\\":2,\\"cloudServerUrl\\":\\"http://47.109.149.111:18880\\",\\"allowLegacyPermanentKeys\\":$flag}"
fi
if [[ -n "$output" ]]; then printf '%s' "$body" > "$output"; else printf '%s' "$body"; fi`);
  for (const name of ['chown', 'useradd', 'flock', 'sleep', 'journalctl']) stub(name, ':');
  if (process.platform === 'win32') {
    // Git Bash cannot apply Linux ownership/modes to this Windows temporary directory.
    stub('chmod', ':');
    stub('install', `directory=0
while (( $# )); do
 case "$1" in -d) directory=1; shift ;; -m|-o|-g) shift 2 ;; *) break ;; esac
done
if (( directory )); then mkdir -p "$@"; else /usr/bin/cp "$@"; fi`);
  }
  stub('apt-get', 'echo forbidden-os-upgrade >&2; exit 99');
  const run = (extra: NodeJS.ProcessEnv = {}) => spawnSync(bash, [shellPath(join(paths.package, 'deploy/install.sh'))], {
    encoding: 'utf8', timeout: 30_000, env: { ...process.env, PATH: paths.bin + (process.platform === 'win32' ? ';' : ':') + process.env.PATH,
      FIXTURE_ROOT: shellPath(root), FIXTURE_BIN: shellPath(paths.bin), FIXTURE_APP: shellPath(paths.app), FIXTURE_DATA: shellPath(paths.data), FIXTURE_ENV: shellPath(paths.env), FIXTURE_UNIT: shellPath(paths.unit),
      REAL_NODE: shellPath(process.execPath), FIXTURE_BRIDGE: shellPath(join(root, 'node-bridge.cjs')), FAIL_POINT: failure, FIXTURE_LIBRARY_STATUS: '401', ...extra },
  });
  const events = () => existsSync(join(root, 'events')) ? readFileSync(join(root, 'events'), 'utf8') : '';
  const backups = () => existsSync(paths.backups) ? readdirSync(paths.backups).map(name => join(paths.backups, name)) : [];
  return { root, paths, oldEnv, run, events, backups };
}

test.each([undefined, 'false'])('upgrade stops before backing up or replacing files and preserves production state: %s', legacy => {
  const f = fixture('', true, legacy); const result = f.run();
  expect(result.status, result.stdout + result.stderr).toBe(0);
  const backup = f.backups().find(path => existsSync(join(path, 'backup.complete')))!;
  expect(backup).toBeTruthy();
  expect(readFileSync(join(backup, 'data/cloud-match.sqlite'), 'utf8').trim()).toBe('old-after-stop');
  expect(readFileSync(join(backup, 'env'), 'utf8')).toBe(f.oldEnv);
  for (const name of ['cloud-match.sqlite-wal', 'cloud-match.sqlite-shm', 'cloud-match.sqlite.license-key', 'tokens']) {
    expect(readFileSync(join(f.paths.data, name))).toEqual(readFileSync(join(backup, 'data', name)));
  }
  expect(readFileSync(join(backup, 'app/node_modules/dependency'), 'utf8')).toBe('old-native-dependency');
  expect(readFileSync(f.paths.env, 'utf8')).toContain('ADMIN_PASSWORD=private-original');
  expect(readFileSync(f.paths.env, 'utf8')).toContain(`ALLOW_LEGACY_PERMANENT_KEYS=${legacy ?? 'true'}`);
  expect(f.events().indexOf('systemctl stop')).toBeLessThan(f.events().indexOf('cp '));
  expect(f.events().indexOf('systemctl stop')).toBeLessThan(f.events().indexOf('npm ci'));
  expect(result.stdout + result.stderr).not.toContain('private-original');
});

test.each(['npm', 'backup', 'start', 'health'])('%s failure preserves/restores the matching old application, data, environment and service', failure => {
  const f = fixture(failure); const result = f.run();
  expect(result.status, result.stdout + result.stderr).not.toBe(0);
  expect(readFileSync(join(f.paths.app, 'dist/server.js'), 'utf8')).toBe('old-app');
  expect(readFileSync(join(f.paths.app, 'node_modules/dependency'), 'utf8')).toBe('old-native-dependency');
  expect(readFileSync(join(f.paths.data, 'cloud-match.sqlite'), 'utf8').trim()).toBe('old-after-stop');
  expect(readFileSync(join(f.paths.data, 'cloud-match.sqlite.license-key'), 'utf8')).toBe('private-vault-key');
  expect(readFileSync(f.paths.env, 'utf8')).toBe(f.oldEnv);
  expect(readFileSync(f.paths.unit, 'utf8')).toBe('original-unit');
  expect(readFileSync(join(f.root, 'active'), 'utf8').trim()).toBe('active');
  expect(f.events()).toContain('old-started');
});

test.each(['restore', 'rollback-stop'])('unsafe rollback %s never starts old code against migrated data', failure => {
  const f = fixture(failure); const result = f.run();
  expect(result.status, result.stdout + result.stderr).not.toBe(0);
  expect(f.events()).not.toContain('old-started');
  expect(result.stderr).toContain('MANUAL RECOVERY');
  expect(f.backups().some(path => existsSync(join(path, 'backup.complete')))).toBe(true);
});

test('a failed initial service stop makes no application or data changes', () => {
  const f = fixture('stop'); const result = f.run();
  expect(result.status).not.toBe(0);
  expect(f.events()).not.toContain('cp ');
  expect(f.events()).not.toContain('npm ci');
  expect(readFileSync(join(f.paths.data, 'cloud-match.sqlite'), 'utf8')).toBe('old-database');
  expect(readFileSync(f.paths.env, 'utf8')).toBe(f.oldEnv);
});

test('rollback does not start a previously inactive service', () => {
  const f = fixture('start', false); const result = f.run();
  expect(result.status).not.toBe(0);
  expect(f.events()).not.toContain('old-started');
  expect(readFileSync(join(f.root, 'active'), 'utf8').trim()).toBe('inactive');
});

test('404 library route fails release preflight and rolls back without probing a card', () => {
  const f = fixture(); const result = f.run({ FIXTURE_LIBRARY_STATUS: '404' });
  expect(result.status, result.stdout + result.stderr).not.toBe(0);
  expect(f.events()).not.toContain('/auth/activate');
  expect(f.events()).toContain('old-started');
  expect(result.stderr).toContain('401');
});

test('custom systemd overrides are rejected before stopping or touching production data', () => {
  const f = fixture('dropins'); const result = f.run();
  expect(result.status, result.stdout + result.stderr).not.toBe(0);
  expect(f.events()).not.toContain('systemctl stop');
  expect(f.events()).not.toContain('cp ');
  expect(result.stderr).toContain('drop-in');
});
