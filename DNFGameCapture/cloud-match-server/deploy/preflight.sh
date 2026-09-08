#!/usr/bin/env bash
set -Eeuo pipefail
umask 077
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BASE_URL="${1:-http://47.109.149.111:18880}"
BASE_URL="${BASE_URL%/}"
ENV_FILE="${2:-}"
TEMP_FILE="$(mktemp)"
trap 'rm -f -- "$TEMP_FILE"' EXIT

curl --connect-timeout 3 --max-time 10 -fsS "$BASE_URL/health" >/dev/null
status="$(curl --connect-timeout 3 --max-time 10 -sS -o /dev/null -w '%{http_code}' "$BASE_URL/api/v2/player-library")"
if [[ "$status" != 401 ]]; then
    echo "Release blocked: unauthenticated /api/v2/player-library must return 401, got $status. 404 means server not upgraded, not an invalid card." >&2
    exit 1
fi
curl --connect-timeout 3 --max-time 10 -fsS "$BASE_URL/api/v2/health" -o "$TEMP_FILE"
node "$SCRIPT_DIR/production-env.cjs" check-health "$TEMP_FILE" "$ENV_FILE"
echo "Read-only production preflight passed. No license activation was performed."
