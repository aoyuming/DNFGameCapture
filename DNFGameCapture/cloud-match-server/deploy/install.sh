#!/usr/bin/env bash
set -Eeuo pipefail
umask 077

SERVICE_NAME="dnf-cloud-match"
SERVICE_USER="dnfcloud"
INSTALL_DIR="/opt/dnf-cloud-match-server"
DATA_DIR="/var/lib/dnf-cloud-match"
ENV_FILE="/etc/default/dnf-cloud-match"
UNIT_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
BACKUP_ROOT="/var/backups/dnf-cloud-match"
LOCK_FILE="/run/lock/dnf-cloud-match-install.lock"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

die() { echo "$*" >&2; exit 1; }
if [[ ${EUID} -ne 0 ]]; then
    die "Please run this installer as root: sudo bash ./deploy/install.sh"
fi

# Keep the old Node/native-module ABI available for rollback.
for command in systemctl curl flock readlink find cp install; do
    command -v "$command" >/dev/null || die "Missing prerequisite: $command (see README)."
done
[[ -x /usr/bin/node && -x /usr/bin/npm ]] || die "/usr/bin/node 20+ and /usr/bin/npm are required before installation."
node_major="$(/usr/bin/node --version)"
node_major="${node_major#v}"
node_major="${node_major%%.*}"
[[ "$node_major" =~ ^[0-9]+$ ]] && (( node_major >= 20 )) || die "Node.js 20+ is required; arrange a separate runtime upgrade."
exec 9>"$LOCK_FILE"
flock -n 9 || die "Another production installation is running."

for required in dist/server.js package.json package-lock.json deploy/production-env.cjs deploy/preflight.sh deploy/server.env deploy/dnf-cloud-match.service; do
    [[ -f "$PACKAGE_DIR/$required" ]] || die "Incomplete package: missing $required"
done
[[ "$PACKAGE_DIR" != "$INSTALL_DIR" && "$PACKAGE_DIR" != "$INSTALL_DIR/"* ]] || die "Extract the package outside the active installation."
for path in "$INSTALL_DIR" "$DATA_DIR" "$ENV_FILE" "$UNIT_FILE" "$BACKUP_ROOT"; do
    [[ "$(readlink -m -- "$path")" == "$path" && ! -L "$path" ]] || die "Refusing symlink/redirected deployment path: $path"
done
if [[ -d "$DATA_DIR" ]]; then
    [[ -z "$(find "$DATA_DIR" -type l -print -quit)" ]] || die "Data/vault symlinks need manual backup planning; refusing automatic upgrade."
fi
if [[ -e "$INSTALL_DIR" && ! -f "$ENV_FILE" ]]; then
    die "Existing application has no environment file; locate its database before upgrading."
fi
if [[ ! -f "$ENV_FILE" && -d "$DATA_DIR" && -n "$(find "$DATA_DIR" -mindepth 1 -print -quit)" ]]; then
    die "Existing data has no environment file; refusing to select a new database."
fi

load_state="$(systemctl show "$SERVICE_NAME.service" -p LoadState --value)" || die "Cannot inspect existing service."
if [[ "$load_state" != not-found ]]; then
    drop_ins="$(systemctl show "$SERVICE_NAME.service" -p DropInPaths --value)" || die "Cannot inspect service overrides."
    [[ -z "$drop_ins" ]] || die "Custom systemd drop-ins require a separately reviewed migration."
    fragment="$(systemctl show "$SERVICE_NAME.service" -p FragmentPath --value)" || die "Cannot inspect service unit path."
    [[ "$fragment" == "$UNIT_FILE" ]] || die "Unexpected service unit location; review the effective database configuration manually."
fi
active_state="$(systemctl show "$SERVICE_NAME.service" -p ActiveState --value)" || die "Cannot inspect service state."
[[ "$active_state" == active || "$active_state" == inactive || "$active_state" == failed ]] || die "Service is transitioning; retry when stable."
[[ "$load_state" != masked ]] || die "Service is masked; resolve its policy manually."
was_active=0
[[ "$active_state" != active ]] || was_active=1
was_enabled="$(systemctl is-enabled "$SERVICE_NAME.service" 2>/dev/null || true)"
[[ "$was_enabled" != masked && "$was_enabled" != enabled-runtime ]] || die "Unsupported service enable policy; resolve it manually."

install -d -m 0700 "$BACKUP_ROOT"
BACKUP_DIR="$(mktemp -d "$BACKUP_ROOT/upgrade-$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")"
WORK_DIR="$(mktemp -d "${INSTALL_DIR}.upgrade.XXXXXX")"
stopped=0
changed=0
completed=0

service_stopped() {
    local state
    state="$(systemctl show "$SERVICE_NAME.service" -p ActiveState --value)" || return 1
    [[ "$state" == inactive || "$state" == failed ]]
}
restore_one() {
    local name="$1" target="$2"
    rm -rf -- "$target" || return 1
    if [[ -e "$BACKUP_DIR/$name" ]]; then cp -a -- "$BACKUP_DIR/$name" "$target" || return 1; fi
}
rollback() {
    # A partial restore must never pair old code with a migrated DB.
    systemctl stop "$SERVICE_NAME.service" || return 1
    service_stopped || return 1
    [[ -f "$BACKUP_DIR/backup.complete" ]] || return 1
    restore_one app "$INSTALL_DIR" || return 1
    restore_one data "$DATA_DIR" || return 1
    restore_one env "$ENV_FILE" || return 1
    restore_one unit "$UNIT_FILE" || return 1
    systemctl daemon-reload || return 1
    if [[ "$was_enabled" == enabled ]]; then
        systemctl enable "$SERVICE_NAME.service" || return 1
    elif [[ -e "$UNIT_FILE" ]]; then
        systemctl disable "$SERVICE_NAME.service" || return 1
    fi
    if (( was_active )); then systemctl start "$SERVICE_NAME.service" || return 1; fi
}
finish() {
    local result=$?
    trap - EXIT INT TERM
    if (( ! completed )); then
        (( result != 0 )) || result=1
        if (( changed )); then
            if rollback; then
                echo "Upgrade failed; restored matching old app/data/env/unit. Backup: $BACKUP_DIR" >&2
            else
                echo "MANUAL RECOVERY REQUIRED. Do not start old code against migrated data. Backup: $BACKUP_DIR; staging: $WORK_DIR" >&2
            fi
        elif (( stopped && was_active )); then
            if ! systemctl start "$SERVICE_NAME.service"; then
                echo "MANUAL RECOVERY REQUIRED: old service could not restart. Backup: $BACKUP_DIR" >&2
            fi
        fi
        echo "Installation aborted; retained diagnostic files in $WORK_DIR and $BACKUP_DIR" >&2
    fi
    exit "$result"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

source_env="$ENV_FILE"
[[ -f "$source_env" ]] || source_env="$SCRIPT_DIR/server.env"
existing=0
[[ ! -e "$INSTALL_DIR" ]] || existing=1
database_path="$(/usr/bin/node "$SCRIPT_DIR/production-env.cjs" prepare "$source_env" "$WORK_DIR/server.env" "$DATA_DIR" "$existing")"
if [[ -e "$INSTALL_DIR" && ! -f "$database_path" ]]; then
    die "Configured production database does not exist: $database_path. Refusing an empty replacement."
fi

echo "[1/5] Stopping production service before consistent backup..."
if [[ "$load_state" != not-found ]]; then systemctl stop "$SERVICE_NAME.service"; fi
service_stopped || die "Service did not stop; no backup or replacement attempted."
stopped=1

echo "[2/5] Backing up application, complete data directory, environment and unit..."
for entry in app data env unit; do
    case "$entry" in app) path="$INSTALL_DIR";; data) path="$DATA_DIR";; env) path="$ENV_FILE";; unit) path="$UNIT_FILE";; esac
    if [[ -e "$path" ]]; then cp -a -- "$path" "$BACKUP_DIR/$entry"; fi
done
printf 'active=%s\nenabled=%s\n' "$was_active" "$was_enabled" > "$BACKUP_DIR/service-state"
touch "$BACKUP_DIR/backup.complete"

echo "[3/5] Staging new application and dependencies with the existing Node runtime..."
mkdir "$WORK_DIR/app"
cp -a -- "$PACKAGE_DIR/dist" "$WORK_DIR/app/dist"
cp -a -- "$PACKAGE_DIR/package.json" "$PACKAGE_DIR/package-lock.json" "$WORK_DIR/app/"
(
    cd "$WORK_DIR/app"
    /usr/bin/npm ci --omit=dev --no-audit --no-fund
)
chown -R root:root "$WORK_DIR/app"
# umask 077 protects backups; the service account must read the app.
chmod -R a+rX "$WORK_DIR/app"

echo "[4/5] Installing production service..."
changed=1
if ! id -u "$SERVICE_USER" >/dev/null 2>&1; then
    useradd --system --user-group --home-dir "$DATA_DIR" --shell /usr/sbin/nologin "$SERVICE_USER"
fi
install -d -m 0750 "$DATA_DIR"
chown -R "$SERVICE_USER:$SERVICE_USER" "$DATA_DIR"
rm -rf -- "$INSTALL_DIR"
mv -- "$WORK_DIR/app" "$INSTALL_DIR"
install -m 0600 "$WORK_DIR/server.env" "$ENV_FILE"
install -m 0644 "$SCRIPT_DIR/dnf-cloud-match.service" "$UNIT_FILE"
systemctl daemon-reload
systemctl enable "$SERVICE_NAME.service"
systemctl start "$SERVICE_NAME.service"

echo "[5/5] Checking production v2 without activating keys..."
healthy=0
for _ in {1..15}; do
    if curl --connect-timeout 2 --max-time 3 -fsS http://127.0.0.1:18880/health >/dev/null &&
       curl --connect-timeout 2 --max-time 3 -fsS http://127.0.0.1:18881/admin/health >/dev/null; then
        healthy=1
        break
    fi
    sleep 1
done
(( healthy )) || die "Service health timeout."
bash "$SCRIPT_DIR/preflight.sh" http://127.0.0.1:18880 "$ENV_FILE"
systemctl is-active --quiet "$SERVICE_NAME.service" || die "Service exited during preflight."
completed=1
rm -rf -- "$WORK_DIR"
echo "Production upgrade complete: http://47.109.149.111:18880"
echo "Backup (contains private credentials): $BACKUP_DIR"
echo "Admin uses port 18881; password remains in $ENV_FILE. Firewall rules were not changed."
echo "Verify the public endpoint with deploy/preflight.sh before distributing the 5.2.0 client."
