#!/usr/bin/env bash
set -Eeuo pipefail

SERVICE_NAME="dnf-cloud-match-test"
SERVICE_USER="dnfcloud-test"
INSTALL_DIR="/opt/dnf-cloud-match-server-test"
DATA_DIR="/var/lib/dnf-cloud-match-test"
ENV_FILE="/etc/default/dnf-cloud-match-test"
UNIT_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

if [[ ${EUID} -ne 0 ]]; then
    echo "Please run this installer as root: sudo ./deploy/install-test.sh" >&2
    exit 1
fi

for required in dist package.json package-lock.json; do
    if [[ ! -e "${PACKAGE_DIR}/${required}" ]]; then
        echo "Test package is incomplete: missing ${required}" >&2
        exit 1
    fi
done

echo "[1/7] Installing operating-system prerequisites..."
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y ca-certificates curl build-essential python3

node_major=0
if [[ -x /usr/bin/node ]]; then
    node_major="$(/usr/bin/node --version | sed -E 's/^v([0-9]+).*/\1/')"
fi
if [[ ! "${node_major}" =~ ^[0-9]+$ ]] || (( node_major < 20 )); then
    echo "Node.js 20 or newer is required for the isolated test service." >&2
    exit 1
fi

echo "[2/7] Creating isolated test directories..."
if ! id -u "${SERVICE_USER}" >/dev/null 2>&1; then
    useradd --system --user-group --home-dir "${DATA_DIR}" --shell /usr/sbin/nologin "${SERVICE_USER}"
fi
install -d -m 0755 -o root -g root "${INSTALL_DIR}"
install -d -m 0750 -o "${SERVICE_USER}" -g "${SERVICE_USER}" "${DATA_DIR}"
chown -R "${SERVICE_USER}:${SERVICE_USER}" "${DATA_DIR}"

echo "[3/7] Installing only the test service application..."
rm -rf "${INSTALL_DIR}/dist"
cp -a "${PACKAGE_DIR}/dist" "${INSTALL_DIR}/dist"
install -m 0644 "${PACKAGE_DIR}/package.json" "${INSTALL_DIR}/package.json"
install -m 0644 "${PACKAGE_DIR}/package-lock.json" "${INSTALL_DIR}/package-lock.json"

echo "[4/7] Installing test service dependencies..."
cd "${INSTALL_DIR}"
/usr/bin/npm ci --omit=dev --no-audit --no-fund
chown -R root:root "${INSTALL_DIR}"

echo "[5/7] Installing isolated environment and systemd unit..."
if [[ ! -f "${ENV_FILE}" ]]; then
    install -m 0600 "${SCRIPT_DIR}/test-server.env" "${ENV_FILE}"
fi
set_env_value() {
    local key="$1"
    local value="$2"
    if grep -q "^${key}=" "${ENV_FILE}"; then
        sed -i "s|^${key}=.*|${key}=${value}|" "${ENV_FILE}"
    else
        printf '%s=%s\n' "${key}" "${value}" >> "${ENV_FILE}"
    fi
}
set_env_value PORT 28880
set_env_value PUBLIC_URL http://47.109.149.111:28880
set_env_value ADMIN_HOST 0.0.0.0
set_env_value ADMIN_PORT 28881
set_env_value DATABASE_PATH "${DATA_DIR}/cloud-match-test.sqlite"
ADMIN_PASSWORD="$(sed -n 's/^ADMIN_PASSWORD=//p' "${ENV_FILE}" | tail -n 1)"
if [[ -z "${ADMIN_PASSWORD}" ]]; then
    ADMIN_PASSWORD="$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')"
    set_env_value ADMIN_PASSWORD "${ADMIN_PASSWORD}"
fi
chmod 0600 "${ENV_FILE}"
install -m 0644 "${SCRIPT_DIR}/dnf-cloud-match-test.service" "${UNIT_FILE}"
systemctl daemon-reload
systemctl enable "${SERVICE_NAME}.service"
systemctl restart "${SERVICE_NAME}.service"

if command -v ufw >/dev/null 2>&1 && ufw status | grep -q '^Status: active'; then
    ufw allow 28880/tcp
    ufw allow 28881/tcp
fi

echo "[6/7] Checking the isolated test service..."
for _ in {1..15}; do
    if curl -fsS http://127.0.0.1:28880/health >/dev/null &&
        curl -fsS http://127.0.0.1:28881/admin/health >/dev/null; then
        echo "Test server is running on TCP 28880; admin is on 28881."
        echo "Admin user: admin"
        echo "Admin password: ${ADMIN_PASSWORD}"
        echo "Production service was not stopped or modified."
        exit 0
    fi
    sleep 1
done

echo "Test service did not become healthy. Recent logs:" >&2
journalctl -u "${SERVICE_NAME}" -n 80 --no-pager >&2 || true
exit 1
