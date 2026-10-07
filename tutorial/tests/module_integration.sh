#!/usr/bin/env bash
set -euo pipefail

if [[ "${TUTORIAL_INTEGRATION:-0}" != "1" ]]; then
    printf '%s\n' "SKIP: set TUTORIAL_INTEGRATION=1 for a running FreeSWITCH lab"
    exit 77
fi

FS_CLI="${FS_CLI:-fs_cli}"
MODULE="${TUTORIAL_MODULE:-}"
CONFIG="${TUTORIAL_MODULE_CONFIG:-}"
IVR_ENDPOINT="${TUTORIAL_IVR_ENDPOINT:-loopback/9000@tutorial}"
METRICS_URL="${TUTORIAL_METRICS_URL:-}"
EVENT_MARKER="$(mktemp "${TMPDIR:-/tmp}/mod-tutorial-event.XXXXXX")"
EVENT_LOG="${EVENT_MARKER}.log"
CALL_UUID=""
MODULE_LOADED=0

cleanup() {
    if [[ -n "$CALL_UUID" ]]; then
        "$FS_CLI" -n -q -x "uuid_kill $CALL_UUID" >/dev/null 2>&1 || true
    fi
    if [[ "$MODULE_LOADED" == "1" ]]; then
        "$FS_CLI" -n -q -x "unload mod_tutorial" >/dev/null 2>&1 || true
    fi
    if [[ -n "${EVENT_PID:-}" ]]; then
        kill "$EVENT_PID" >/dev/null 2>&1 || true
        wait "$EVENT_PID" >/dev/null 2>&1 || true
    fi
    rm -f "$EVENT_MARKER" "$EVENT_LOG"
}
trap cleanup EXIT INT TERM

command -v "$FS_CLI" >/dev/null
[[ -n "$MODULE" && -f "$MODULE" ]] || {
    printf '%s\n' "TUTORIAL_MODULE must point to mod_tutorial.so" >&2
    exit 2
}
[[ -n "$CONFIG" && -f "$CONFIG" ]] || {
    printf '%s\n' "TUTORIAL_MODULE_CONFIG must point to mod_tutorial.conf.xml" >&2
    exit 2
}

python3 -u - "$EVENT_MARKER" >"$EVENT_LOG" 2>&1 <<'PY' &
import os
import socket
import sys
import time

marker = sys.argv[1]
host = os.environ.get("ESL_HOST", "127.0.0.1")
port = int(os.environ.get("ESL_PORT", "8021"))
password = os.environ.get("ESL_PASSWORD", "ClueCon")

sock = socket.create_connection((host, port), timeout=5)
sock.sendall(("auth " + password + "\n\n").encode())
sock.sendall(b"event plain CUSTOM\n\n")
sock.settimeout(1)
buffer = b""
deadline = time.time() + float(os.environ.get("TUTORIAL_EVENT_TIMEOUT", "20"))
while time.time() < deadline:
    try:
        data = sock.recv(8192)
    except socket.timeout:
        continue
    if not data:
        break
    buffer += data
    while b"\n\n" in buffer:
        frame, buffer = buffer.split(b"\n\n", 1)
        headers = {}
        for line in frame.decode("utf-8", errors="replace").splitlines():
            if ": " in line:
                key, value = line.split(": ", 1)
                headers[key] = value
        if headers.get("Event-Subclass") == "tutorial::ivr_choice":
            with open(marker, "a", encoding="ascii") as output:
                output.write(f"{headers.get('Menu', '')} {headers.get('Choice', '')}\n")
sock.close()
PY
EVENT_PID=$!

load_output="$("$FS_CLI" -n -q -x "load $MODULE")"
printf '%s\n' "$load_output" | python3 -c \
    'import sys; value=sys.stdin.read(); raise SystemExit(0 if "+OK" in value or "loaded" in value.lower() else 1)'
MODULE_LOADED=1

module_state="$("$FS_CLI" -n -q -x "module_exists mod_tutorial")"
printf '%s\n' "$module_state" | python3 -c \
    'import sys; raise SystemExit(0 if "true" in sys.stdin.read().lower() else 1)'

reload_output="$("$FS_CLI" -n -q -x "reloadxml")"
printf '%s\n' "$reload_output" | python3 -c \
    'import sys; value=sys.stdin.read(); raise SystemExit(0 if "+OK" in value or "success" in value.lower() else 1)'

wait_for_event() {
    local menu="$1"
    local choice="$2"
    for _ in {1..20}; do
        if python3 - "$EVENT_MARKER" "$menu" "$choice" <<'PY'
import pathlib
import sys

marker, menu, choice = sys.argv[1:]
expected = f"{menu} {choice}\n"
path = pathlib.Path(marker)
raise SystemExit(0 if path.exists() and expected in path.read_text(encoding="ascii") else 1)
PY
        then
            return 0
        fi
        sleep 1
    done
    printf '%s\n' "did not observe tutorial::ivr_choice for ${menu} ${choice}" >&2
    return 1
}

for branch in "submenu 1:11" "submenu 2:12" "main 2:2" "main 3:3"; do
    expected="${branch%%:*}"
    digits="${branch##*:}"
    CALL_UUID="$("$FS_CLI" -n -q -x "originate $IVR_ENDPOINT &park()" |
        tr -d '\r' | awk '/^[0-9a-fA-F-]{36}$/ { print; exit }')"
    [[ -n "$CALL_UUID" ]] || {
        printf '%s\n' "originate did not return a UUID; verify tutorial dialplan is mounted" >&2
        exit 1
    }
    "$FS_CLI" -n -q -x "uuid_send_dtmf $CALL_UUID $digits" >/dev/null
    wait_for_event "${expected% *}" "${expected#* }"
    "$FS_CLI" -n -q -x "uuid_kill $CALL_UUID" >/dev/null 2>&1 || true
    CALL_UUID=""
done

api_output="$("$FS_CLI" -n -q -x "tutorial_metrics json")"
printf '%s\n' "$api_output" | python3 -c '
import json
import sys
snapshot = json.load(sys.stdin)
assert snapshot["version"] == "1"
assert snapshot["invocations"] >= 6
expected = {("main", "1"), ("main", "2"), ("main", "3"),
            ("submenu", "1"), ("submenu", "2")}
observed = {(item["menu"], item["choice"]) for item in snapshot["choices"]
            if item["count"] >= 1}
assert expected <= observed
'

if [[ -n "$METRICS_URL" ]]; then
    curl --fail --silent --show-error "$METRICS_URL" |
        python3 -c '
import sys
body = sys.stdin.read()
assert "freeswitch_ivr_choice_total" in body
assert "menu=\"submenu\"" in body
'
fi

if [[ -n "$CALL_UUID" ]]; then
    "$FS_CLI" -n -q -x "uuid_kill $CALL_UUID" >/dev/null 2>&1 || true
    CALL_UUID=""
fi
"$FS_CLI" -n -q -x "unload mod_tutorial" >/dev/null
MODULE_LOADED=0
"$FS_CLI" -n -q -x status |
    python3 -c 'import sys; raise SystemExit(0 if sys.stdin.read().lstrip().startswith("UP") else 1)'

printf '%s\n' "PASS: module load, IVR branch, API, event, and optional Prometheus checks"
