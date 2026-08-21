#!/usr/bin/env bash
#
# Start the packaged server and confirm it finds everything it needs.
#
#   packaging/smoke-test.sh <package-dir> <exe-name>
#
# A --help run would only prove the binary links. This boots the account, BOSS
# and management servers on high ports, which makes the server open every file it
# resolves by relative path: settings.schema.json, boss.schema.json,
# account_settings.html, timezones.json, regions.json, countries_languages.json
# and the three default textures the first BOSS payload is built from. A package
# missing any of them fails here instead of on someone's machine.

set -euo pipefail

PKG_DIR="${1:?usage: smoke-test.sh <package-dir> <exe-name>}"
EXE="${2:?usage: smoke-test.sh <package-dir> <exe-name>}"

cd "$PKG_DIR"

# The server needs native paths in the settings file, which are not what
# git-bash reports on Windows.
if [ "${OS:-}" = "Windows_NT" ]; then
    DIR="$(pwd -W)"
else
    DIR="$(pwd)"
fi

mkdir -p data/certs data/miis

cat > data/settings.json <<JSON
{
  "domain": "nintendo.net",
  "nex": {
    "tokenKey": "MDEyMzQ1Njc4OWFiY2RlZjAxMjM0NTY3ODlhYmNkZWY=",
    "serverId": 0
  },
  "grpc": {
    "enabled": true,
    "listenAddress": "127.0.0.1",
    "port": 19999,
    "reflection": false,
    "publicFacingAddress": "127.0.0.1:19999"
  },
  "sharedState": { "type": "local" },
  "http": {
    "listenAddress": "127.0.0.1",
    "listenPort": 18443,
    "workerCount": 1,
    "keepAliveTimeout": 10,
    "ssl": false
  },
  "accounts": {
    "enabled": true,
    "tokenKey": "MDEyMzQ1Njc4OWFiY2RlZjAxMjM0NTY3ODlhYmNkZWY=",
    "refreshTokenKey": "MDEyMzQ1Njc4OWFiY2RlZjAxMjM0NTY3ODlhYmNkZWY=",
    "deviceKeyPath": "${DIR}/data/certs/device.key",
    "miiImagesPath": "${DIR}/data/miis",
    "allowRealWiiU": true,
    "allowGeneratedWiiU": true,
    "hosts": {
      "00003200": [{ "address": "127.0.0.1:1201", "grpcAddress": "127.0.0.1:19999" }],
      "10162B00": [{ "address": "127.0.0.1:1203", "grpcAddress": "127.0.0.1:19999" }]
    },
    "secureServerGrpcAddresses": {
      "00003200": ["127.0.0.1:19999"],
      "10162B00": ["127.0.0.1:19999"]
    },
    "db": { "type": "SQLite3", "path": "${DIR}/data/accounts.db" },
    "email": { "enabled": false },
    "grpcRequestTimeout": 3000,
    "grpcConnectionPoolMaxSize": 2
  },
  "boss": {
    "enabled": true,
    "db": { "type": "SQLite3", "path": "${DIR}/data/boss.db" }
  },
  "friendsAuth": { "enabled": false },
  "friendsSecure": { "enabled": false },
  "splatoonAuth": { "enabled": false },
  "splatoonSecure": { "enabled": false },
  "management": {
    "enabled": true,
    "listenAddress": "127.0.0.1",
    "listenPort": 13000,
    "workerCount": 1,
    "keepAliveTimeout": 10,
    "corsOrigin": "*",
    "grpcRequestTimeout": 3000,
    "grpcConnectionPoolMaxSize": 2,
    "db": { "type": "SQLite3", "path": "${DIR}/data/management.db" },
    "servers": {
      "accounts": ["127.0.0.1:19999"],
      "boss": ["127.0.0.1:19999"],
      "friendsAuth": ["127.0.0.1:19999"],
      "splatoonAuth": ["127.0.0.1:19999"],
      "friendsSecure": ["127.0.0.1:19999"],
      "splatoonSecure": ["127.0.0.1:19999"]
    }
  }
}
JSON

LOG=smoke.log
echo "starting ./${EXE} from $(pwd)"
"./${EXE}" --data ./data --log-level 1 > "$LOG" 2>&1 &
PID=$!

stop_server() {
    kill "$PID" 2>/dev/null || true
    # git-bash cannot always signal a native Windows process.
    if [ "${OS:-}" = "Windows_NT" ]; then
        taskkill //F //PID "$PID" >/dev/null 2>&1 || true
        taskkill //F //IM "$EXE" >/dev/null 2>&1 || true
    fi
    wait "$PID" 2>/dev/null || true
}
trap stop_server EXIT

# The BOSS payload build runs on the background scheduler a few seconds in, so
# wait for it rather than guessing a fixed delay.
deadline=$((SECONDS + 60))
ready=0
while [ $SECONDS -lt $deadline ]; do
    if grep -q "Successfully updated boss VS setting" "$LOG" 2>/dev/null; then
        ready=1
        break
    fi
    if grep -q "\[FAILURE\]" "$LOG" 2>/dev/null; then
        break
    fi
    if ! kill -0 "$PID" 2>/dev/null; then
        break
    fi
    sleep 2
done

stop_server
trap - EXIT

echo "--- server log ---"
cat "$LOG"
echo "------------------"

if grep -q "\[FAILURE\]" "$LOG"; then
    echo "SMOKE TEST FAILED: the server reported a failure" >&2
    exit 1
fi

for expected in \
    "Starting HTTP server" \
    "Creating default BOSS files." \
    "Successfully updated boss festival" \
    "Successfully updated boss VS setting"
do
    if ! grep -qF "$expected" "$LOG"; then
        echo "SMOKE TEST FAILED: expected to see '$expected' in the log" >&2
        exit 1
    fi
done

if [ "$ready" -ne 1 ]; then
    echo "SMOKE TEST FAILED: the server did not finish starting within the timeout" >&2
    exit 1
fi

rm -f "$LOG"
echo "SMOKE TEST PASSED: the packaged server started and built its BOSS payloads"
