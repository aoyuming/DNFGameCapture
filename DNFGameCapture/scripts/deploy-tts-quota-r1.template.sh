#!/usr/bin/env bash
# 语音生成额度（TTS quota r1）服务端升级：只替换服务端程序，不导入音频，不调用付费语音 API。
# 用法：把本脚本和 DNF-5.5.4-TTS-Quota-r1.zip 一起上传到 /root，然后 root 执行：bash deploy-dnf-tts-quota-r1.sh
# 不修改环境配置、测试服、PM2、防火墙；新数据表在服务启动时自动创建。
set -Eeuo pipefail
umask 077
SERVICE=dnf-cloud-match.service
APP=/opt/dnf-cloud-match-server
DATA=/var/lib/dnf-cloud-match
DB="$DATA/cloud-match.sqlite"
ENVFILE=/etc/default/dnf-cloud-match
ZIP=/root/DNF-5.5.4-TTS-Quota-r1.zip
EXPECTED_SHA=__ZIP_SHA256__
STAGE=''
BACKUP=''
STOP_REQUESTED=0
MUTATED=0
COMMITTED=0

ports_free() {
    python3 - <<'PY'
import socket
sockets=[]
try:
    for port in (18880,18881):
        s=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
        s.bind(('0.0.0.0',port)); sockets.append(s)
finally:
    for s in sockets:s.close()
PY
}

rollback() {
    local code="${1:-1}"
    trap - ERR INT TERM
    set +e
    echo
    echo '升级未完成。不要重复执行停服/覆盖命令，请保留本次输出。'
    if [[ "$COMMITTED" == 1 ]]; then
        echo '升级已通过验证；没有执行回退。'
    elif [[ "$MUTATED" == 1 ]]; then
        echo "正在尝试恢复同一份程序与数据库备份：$BACKUP"
        if ! systemctl stop "$SERVICE" || ! ports_free; then
            echo '无法确认新服务已停止或端口已释放，已停止自动回退，未恢复旧程序。'
            echo "请保留目录 $BACKUP 并联系维护人员；不要手动启动旧程序。"
            exit "$code"
        fi
        # Never delete the failed deployment/database; retain both for inspection.
        if mv -- "$APP" "$BACKUP/failed-app" &&
           mv -- "$DATA" "$BACKUP/failed-data" &&
           cp -a -- "$BACKUP/app" "$APP" &&
           cp -a -- "$BACKUP/data" "$DATA"; then
            if systemctl start "$SERVICE" && systemctl is-active --quiet "$SERVICE"; then
                echo '已恢复备份中的程序和数据库，旧服务已启动。请再核对旧业务是否正常。'
            else
                echo '文件已恢复，但旧服务未成功启动；请联系维护人员，不要重装或重新生成音频。'
            fi
        else
            echo '恢复文件失败，未启动服务。请联系维护人员，保留备份与失败现场。'
        fi
    elif [[ "$STOP_REQUESTED" == 1 ]]; then
        echo '尚未覆盖程序，正在恢复原服务运行状态。'
        systemctl start "$SERVICE" || echo '原服务启动失败，请联系维护人员。'
    else
        echo '错误发生在停服前，脚本没有修改正式程序和数据库。'
    fi
    [[ -z "$STAGE" ]] || echo "准备目录：$STAGE"
    [[ -z "$BACKUP" ]] || echo "备份目录：$BACKUP"
    exit "$code"
}
trap 'rollback $?' ERR
trap 'rollback 130' INT
trap 'rollback 143' TERM

echo '=== 1/6 确认环境与升级包（不停服） ==='
[[ "$EUID" -eq 0 ]] || { echo '请使用root执行。'; exit 1; }
for tool in python3 node npm runuser systemctl sha256sum cp mv install mountpoint; do
    command -v "$tool" >/dev/null || { echo "缺少工具：$tool；尚未停服。"; exit 1; }
done
node -e 'if(process.versions.node.split(".")[0]!=="22")throw Error("Expected Node.js 22")'
[[ -f "$ZIP" && ! -L "$ZIP" ]] || { echo "未找到普通文件：$ZIP"; exit 1; }
printf '%s  %s\n' "$EXPECTED_SHA" "$ZIP" | sha256sum -c -
[[ -d "$APP" && ! -L "$APP" && -d "$DATA" && ! -L "$DATA" && -f "$DB" && ! -L "$DB" ]]
if mountpoint -q "$APP" || mountpoint -q "$DATA"; then
    echo '程序或数据目录是独立挂载点，需要调整备份/回退方式；尚未停服。'
    exit 1
fi
[[ "$(systemctl show "$SERVICE" -p WorkingDirectory --value)" == "$APP" ]]
[[ "$(systemctl show "$SERVICE" -p User --value)" == dnfcloud ]]
systemctl is-active --quiet "$SERVICE"
# Only test known non-secret settings. Never print or source the environment file.
grep -qxF "DATABASE_PATH=$DB" "$ENVFILE"
grep -qxF 'PORT=18880' "$ENVFILE"
grep -qxF 'ADMIN_PORT=18881' "$ENVFILE"

STAGE=$(mktemp -d /opt/dnf-tts-quota-stage.XXXXXX)
chmod 755 "$STAGE"
python3 - "$ZIP" "$STAGE" <<'PY'
import sys,zipfile,json,hashlib,pathlib
archive,destination=sys.argv[1:]
with zipfile.ZipFile(archive) as z:
    names=z.namelist()
    assert len(names)==len(set(names)), 'Duplicate ZIP names'
    for name in names:
        p=pathlib.PurePosixPath(name)
        assert not p.is_absolute() and '..' not in p.parts and '\\' not in name, 'Unsafe ZIP path'
    manifest=json.loads(z.read('SHA256SUMS.json'))
    assert manifest['version']=='5.5.4' and manifest.get('revision')=='tts-quota-r1', 'Wrong package revision'
    for item in manifest['files']:
        data=z.read(item['path'])
        assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256'], 'Manifest mismatch'
    for name in names:
        if name.startswith('server/') or name=='SHA256SUMS.json':
            z.extract(name,destination)
print('升级包及逐文件校验通过。只解压服务端程序。')
PY
# Root's private umask must not prevent the service user reading public package assets.
find "$STAGE/server" -type d -exec chmod 755 {} +
find "$STAGE/server" -type f -exec chmod 644 {} +
install -d -m 700 -o dnfcloud -g "$(id -gn dnfcloud)" "$STAGE/npm-cache"
chown -R dnfcloud:"$(id -gn dnfcloud)" "$STAGE/server"

echo '=== 2/6 准备依赖（不停服） ==='
# Install as the existing unprivileged service account, never as root.
cd "$STAGE/server"
runuser -u dnfcloud -- env npm_config_cache="$STAGE/npm-cache" \
    npm ci --omit=dev --no-audit --no-fund
cd /

# 只检查密钥变量是否存在，不打印内容。
KEYS_OK=1
grep -Eq '^DOUBAO_TTS_API_KEY=.+' "$ENVFILE" || KEYS_OK=0
grep -Eq '^DOUBAO_TTS_APP_KEY=.+' "$ENVFILE" || KEYS_OK=0

printf '\n正式服务：%s\n程序目录：%s\n数据库：%s\n' "$SERVICE" "$APP" "$DB"
if [[ "$KEYS_OK" == 1 ]]; then
    echo '环境配置里已有豆包语音密钥：升级后主播即可按额度生成语音（每日总花费封顶 5 元）。'
else
    echo '注意：环境配置里没有豆包语音密钥，升级后生成功能显示「未配置」，其它功能不受影响。'
fi
echo '接下来会短暂停服并完整备份（程序 + 数据库 + 环境配置），然后替换服务端程序。'
echo '不会导入或改动已有音频，不修改环境配置，不操作测试服或PM2，不调用付费语音生成API。'
read -r -p '确认现在升级，请输入 UPGRADE（其他输入取消）：' ANSWER
if [[ "$ANSWER" != UPGRADE ]]; then
    echo "已取消，正式服务未停止。准备文件保留在 $STAGE"
    exit 0
fi

echo '=== 3/6 停服并完整备份 ==='
install -d -m 700 /var/backups/dnf-cloud-match
BACKUP=$(mktemp -d /var/backups/dnf-cloud-match/tts-quota-$(date +%Y%m%d-%H%M%S).XXXXXX)
STOP_REQUESTED=1
systemctl stop "$SERVICE"
if systemctl is-active --quiet "$SERVICE"; then echo '服务仍在运行，拒绝继续。'; false; fi
ports_free
cp -a -- "$APP" "$BACKUP/app"
cp -a -- "$DATA" "$BACKUP/data"
cp -a -- "$ENVFILE" "$BACKUP/environment"
cp -a -- /etc/systemd/system/dnf-cloud-match.service "$BACKUP/service-unit"
printf '%s\n' "$BACKUP" > "$STAGE/backup-path.txt"
echo "完整备份已保存：$BACKUP"

echo '=== 4/6 更新服务端程序 ==='
MUTATED=1
# Replace only generated runtime files/dependencies; preserve all other live app files.
for name in dist node_modules package.json package-lock.json; do
    if [[ -e "$APP/$name" || -L "$APP/$name" ]]; then
        mv -- "$APP/$name" "$BACKUP/replaced-$name"
    fi
    cp -a -- "$STAGE/server/$name" "$APP/$name"
done
# Do not edit the existing service unit/environment or expose any additional ports.
echo '=== 5/6 启动并验证 ==='
systemctl start "$SERVICE"
python3 - <<'PY'
import json,urllib.request,urllib.error,time
base='http://127.0.0.1:18880'
def get(path):
    with urllib.request.urlopen(base+path,timeout=3) as response:return response.read()
def status(url):
    try:
        urllib.request.urlopen(url,timeout=3); return 200
    except urllib.error.HTTPError as e:
        return e.code
deadline=time.monotonic()+60
last='service not ready'
while time.monotonic()<deadline:
    try:
        assert json.loads(get('/health'))['ok'] is True
        catalog=json.loads(get('/api/voice/catalog'))
        assert any(v['id']=='lol-announcer' for v in catalog['voices']), 'Existing voice catalog missing'
        assert status(base+'/api/v2/tts/status')==401, 'TTS route missing or not protected'
        assert status('http://127.0.0.1:18881/admin/tts')==401, 'Admin TTS page must require authentication'
        assert status(base+'/admin/tts')==404, 'Admin route exposed on public port'
        print('健康检查通过；已有音色目录正常；语音生成接口需要授权；/admin/tts 仅在管理端口且需要登录。')
        break
    except Exception as e:
        last=str(e);time.sleep(1)
else:
    raise SystemExit('启动验证失败：'+last)
PY
systemctl is-active --quiet "$SERVICE"
COMMITTED=1
trap - ERR INT TERM

echo '=== 6/6 升级完成 ==='
printf '正式服务：%s\n状态：running\n备份：%s\n准备目录：%s\n' "$SERVICE" "$BACKUP" "$STAGE"
echo '现有环境配置未修改；测试服和PM2未操作；本次升级付费语音生成调用为0。'
echo '管理后台首页新增「语音生成额度」（http://服务器:18881/admin/tts）：可改全局默认额度/总开关/每日预算（≤5元），'
echo '给单个卡密加额外额度、单独设置日/月额度、禁止生成、清零今日用量。'
echo '不要删除备份。18881是管理端口，请将阿里云安全组访问来源限制为可信地址。'
