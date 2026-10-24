#!/usr/bin/env bash
# Starts the Node bridge (port 8801) and the Go bridge (8802) with the same
# environment, fetches /api/status from both, drops `ts` and diffs them.
# Exit status is the diff's. CUBE_EXTRA_CARDS etc. pass through.
#
#   ./tools/parity.sh
#   CUBE_EXTRA_CARDS=1 ./tools/parity.sh
#
# The countdown can tick between the two fetches; rerun if the only diff is that.

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

NODE_PORT=8801
GO_PORT=8802
TMP="$(mktemp -d)"
PIDS=()

cleanup() {
  for p in ${PIDS[@]+"${PIDS[@]}"}; do kill "$p" 2>/dev/null || true; done
  for p in ${PIDS[@]+"${PIDS[@]}"}; do wait "$p" 2>/dev/null || true; done
  rm -rf "$TMP"
}
trap cleanup EXIT

go build -o cube-bridge ./cmd/cube-bridge

CUBE_PORT=$NODE_PORT node server.mjs >"$TMP/node.log" 2>&1 & PIDS+=($!)
CUBE_PORT=$GO_PORT ./cube-bridge >"$TMP/go.log" 2>&1 & PIDS+=($!)

# Up and refreshed: /health answers and `error` is not "starting up".
wait_ready() { # port name
  local i body
  for i in $(seq 1 100); do
    body="$(curl -fsS --max-time 2 "http://127.0.0.1:$1/health" 2>/dev/null || true)"
    if [ -n "$body" ] && ! printf '%s' "$body" | grep -q 'starting up'; then return 0; fi
    sleep 0.3
  done
  echo "$2 bridge never became ready; log:" >&2
  cat "$TMP/$2.log" >&2
  return 1
}
wait_ready $NODE_PORT node
wait_ready $GO_PORT go

# Normalise: drop ts, re-serialise sorted/pretty through plutil.
norm() { # port
  curl -fsS "http://127.0.0.1:$1/api/status" |
    /usr/bin/plutil -remove ts -o - -- - |
    /usr/bin/plutil -convert json -r -o - -- -
}
norm $NODE_PORT >"$TMP/node.json"
norm $GO_PORT >"$TMP/go.json"

if diff -u "$TMP/node.json" "$TMP/go.json"; then
  echo "parity: no diff ($(grep -c '"t"' "$TMP/go.json") cards)"
else
  echo "parity: DIFFERENT (node = -, go = +)" >&2
  exit 1
fi
