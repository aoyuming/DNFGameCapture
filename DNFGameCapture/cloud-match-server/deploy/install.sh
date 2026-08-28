#!/usr/bin/env bash
set -Eeuo pipefail

SERVICE_NAME="dnf-cloud-match"
SERVICE_USER="dnfcloud"
INSTALL_DIR="/opt/dnf-cloud-match-server"
DATA_DIR="/var/lib/dnf-cloud-match"
ENV_FILE="/etc/default/dnf-cloud-match"
UNIT_FILE="/etc/systemd/system/${SERVICE_NAME}.service"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

if [[ ${EUID} -ne 0 ]]; then
    echo "Please run this installer as root: sudo ./deploy/install.sh" >&2
    exit 1
fi

for required in dist package.json package-lock.json; do
    if [[ ! -e "${PACKAGE_DIR}/${required}" ]]; then
        echo "Package is incomplete: missing ${required}" >&2
        exit 1
    fi
done

echo "[1/7] Installing operating-system dependencies..."
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y \
    ca-certificates curl gnupg build-essential python3 unzip

node_major=0
if [[ -x /usr/bin/node ]]; then
    node_major="$(/usr/bin/node --version | sed -E 's/^v([0-9]+).*/\1/')"
fi

if [[ ! "${node_major}" =~ ^[0-9]+$ ]] || (( node_major < 20 )); then
    echo "[2/7] Installing Node.js 22 LTS..."
    install -d -m 0755 /etc/apt/keyrings
    curl -fsSL https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key \
        | gpg --dearmor --yes -o /etc/apt/keyrings/nodesource.gpg
    echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_22.x nodistro main" \
        > /etc/apt/sources.list.d/nodesource.list
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y nodejs
else
    echo "[2/7] Existing system Node.js $(/usr/bin/node --version) is suitable."
fi

system_node_major=0
if [[ -x /usr/bin/node ]]; then
    system_node_major="$(/usr/bin/node --version | sed -E 's/^v([0-9]+).*/\1/')"
fi
if [[ ! "${system_node_major}" =~ ^[0-9]+$ ]] || (( system_node_major < 20 )) ||
    [[ ! -x /usr/bin/npm ]]; then
    echo "System Node.js installation is invalid; /usr/bin/node 20+ and /usr/bin/npm are required." >&2
    exit 1
fi

echo "[3/7] Creating service account and directories..."
if ! id -u "${SERVICE_USER}" >/dev/null 2>&1; then
    useradd --system --user-group --home-dir "${DATA_DIR}" \
        --shell /usr/sbin/nologin "${SERVICE_USER}"
fi
install -d -m 0755 -o root -g root "${INSTALL_DIR}"
install -d -m 0750 -o "${SERVICE_USER}" -g "${SERVICE_USER}" "${DATA_DIR}"

echo "[4/7] Installing application files..."
rm -rf "${INSTALL_DIR}/dist"
cp -a "${PACKAGE_DIR}/dist" "${INSTALL_DIR}/dist"
install -m 0644 "${PACKAGE_DIR}/package.json" "${INSTALL_DIR}/package.json"
install -m 0644 "${PACKAGE_DIR}/package-lock.json" "${INSTALL_DIR}/package-lock.json"

echo "[5/7] Installing production Node.js dependencies..."
cd "${INSTALL_DIR}"
/usr/bin/npm ci --omit=dev --no-audit --no-fund
chown -R root:root "${INSTALL_DIR}"

echo "[6/7] Installing systemd service..."
if [[ ! -f "${ENV_FILE}" ]]; then
    install -m 0644 "${SCRIPT_DIR}/server.env" "${ENV_FILE}"
fi
install -m 0644 "${SCRIPT_DIR}/dnf-cloud-match.service" "${UNIT_FILE}"
systemctl daemon-reload
systemctl enable "${SERVICE_NAME}.service"
systemctl restart "${SERVICE_NAME}.service"

if command -v ufw >/dev/null 2>&1 && ufw status | grep -q '^Status: active'; then
    ufw allow 18880/tcp
fi

echo "[7/7] Checking service health..."
for _ in {1..15}; do
    if curl -fsS http://127.0.0.1:18880/health >/dev/null; then
        echo
        echo "DNF cloud match server is running on TCP port 18880."
        echo "Public URL: http://<server-ip>:18880"
        echo "Logs: journalctl -u ${SERVICE_NAME} -f"
        exit 0
    fi
    sleep 1
done

echo "Service did not become healthy. Recent logs:" >&2
journalctl -u "${SERVICE_NAME}" -n 80 --no-pager >&2 || true
exit 1
