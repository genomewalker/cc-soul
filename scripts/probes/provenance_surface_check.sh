#!/usr/bin/env bash
# Runnable check for the provenance keyed-lane callable surface (capability #1).
#
# Guards the wiring that makes the deterministic anti-reprocessing lane pay:
#   1. the `chitta` CLI recognizes `provenance_check` (rpc_server.cpp TOOL_SPECS)
#   2. a stored `[done] ... kind:signal` record is reachable by exact key
#      (content-hash AND input-path) via the CLI -> daemon -> keyed lane path
#   3. an unknown key misses cleanly (no fuzzy fallback)
#
# Fully isolated from prod: a unique CHITTA_DB_PATH derives a unique socket
# (socket_path_for_mind hashes the mind path), so this never touches
# ~/.claude/mind or the prod daemon. Temp store is torn down at exit.
#
# Usage: scripts/probes/provenance_surface_check.sh   (needs bin/chitta + bin/chittad built)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CLI="$ROOT/bin/chitta"
DAEMON="$ROOT/bin/chittad"
[ -x "$CLI" ]    || { echo "FAIL: $CLI not built"; exit 1; }
[ -x "$DAEMON" ] || { echo "FAIL: $DAEMON not built"; exit 1; }

# 1. CLI must recognize the tool (regression: "Unknown option: provenance_check").
"$CLI" provenance_check --help >/dev/null 2>&1 || { echo "FAIL: CLI does not know provenance_check"; exit 1; }

STORE="$(mktemp -d "${TMPDIR:-/tmp}/prov_surface.XXXXXX")"
export CHITTA_DB_PATH="$STORE"
DPID=""
cleanup() {
  "$CLI" shutdown >/dev/null 2>&1 || true
  [ -n "$DPID" ] && kill "$DPID" 2>/dev/null || true
  rm -rf "$STORE"
}
trap cleanup EXIT

"$DAEMON" daemon --path "$STORE" -f >"$STORE/daemon.log" 2>&1 &
DPID=$!
for _ in $(seq 1 40); do "$CLI" status >/dev/null 2>&1 && break; sleep 0.5; done
"$CLI" status >/dev/null 2>&1 || { echo "FAIL: test daemon did not come up"; exit 1; }

# kind:signal is required — the lane is gated to signal records (store.rs).
"$CLI" remember \
  --content "[done] input:/data/x.fastq sha:deadbeef12 task:qc output:/out/x.tsv status:ok" \
  --kind signal --tags provenance >/dev/null 2>&1
sleep 1

fails=0
assert_found() { # <json> <label>
  echo "$1" | grep -q '"found": true' && echo "  PASS $2" || { echo "  FAIL $2"; fails=$((fails+1)); }
}
assert_miss()  { # <json> <label>
  echo "$1" | grep -q '"found": false' && echo "  PASS $2" || { echo "  FAIL $2"; fails=$((fails+1)); }
}

assert_found "$("$CLI" provenance_check --sha deadbeef12 --json 2>&1)"      "lookup by content-hash"
assert_found "$("$CLI" provenance_check --input /data/x.fastq --json 2>&1)" "lookup by input path"
assert_miss  "$("$CLI" provenance_check --sha cafef00d99 --json 2>&1)"      "unknown key misses clean"

[ "$fails" -eq 0 ] || { echo "RESULT: $fails assertion(s) failed"; exit 1; }
echo "RESULT: provenance keyed-lane surface OK (3/3)"
