#!/usr/bin/env bash
# Runnable check for the task-state keyed-lane callable surface (capability #3).
#
# Guards the wiring that makes the deterministic task-hand-off lane pay:
#   1. the `chitta` CLI recognizes `task_state` (rpc_server.cpp TOOL_SPECS)
#   2. a stored `[task] id:<slug> ...` record is reachable by exact slug via the
#      CLI -> daemon -> keyed lane path, and an evolved status SUPERSEDES the old
#      one (LATEST-WINS: the lookup returns the newest record for that id)
#   3. an unknown id misses cleanly (no fuzzy fallback)
#
# Fully isolated from prod: a unique CHITTA_DB_PATH derives a unique socket
# (socket_path_for_mind hashes the mind path), so this never touches
# ~/.claude/mind or the prod daemon. Temp store is torn down at exit.
#
# Usage: scripts/probes/task_state_surface_check.sh   (needs bin/chitta + bin/chittad built)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CLI="$ROOT/bin/chitta"
DAEMON="$ROOT/bin/chittad"
[ -x "$CLI" ]    || { echo "FAIL: $CLI not built"; exit 1; }
[ -x "$DAEMON" ] || { echo "FAIL: $DAEMON not built"; exit 1; }

# 1. CLI must recognize the tool (regression: "Unknown option: task_state").
"$CLI" task_state --help >/dev/null 2>&1 || { echo "FAIL: CLI does not know task_state"; exit 1; }

STORE="$(mktemp -d "${TMPDIR:-/tmp}/task_surface.XXXXXX")"
export CHITTA_DB_PATH="$STORE"
DPID=""
cleanup() {
  "$CLI" shutdown >/dev/null 2>&1 || true
  [ -n "$DPID" ] && kill "$DPID" 2>/dev/null || true
  # Daemon may still be flushing segments on NFS; a racing rm errors non-fatally.
  sleep 0.5
  rm -rf "$STORE" 2>/dev/null || true
}
trap cleanup EXIT

"$DAEMON" daemon --path "$STORE" -f >"$STORE/daemon.log" 2>&1 &
DPID=$!
for _ in $(seq 1 40); do "$CLI" status >/dev/null 2>&1 && break; sleep 0.5; done
"$CLI" status >/dev/null 2>&1 || { echo "FAIL: test daemon did not come up"; exit 1; }

# Status evolves in-progress -> done; the lane must resolve to the LATEST.
"$CLI" remember \
  --content "[task] id:foo status:in-progress next:wire the FFI layer" \
  --kind signal --tags long-running-job >/dev/null 2>&1
sleep 1
"$CLI" remember \
  --content "[task] id:foo status:done next:ship it" \
  --kind signal --tags long-running-job >/dev/null 2>&1
sleep 1

fails=0
assert_found() { # <json> <label>
  echo "$1" | grep -q '"found": true' && echo "  PASS $2" || { echo "  FAIL $2"; fails=$((fails+1)); }
}
assert_latest() { # <json> <label>
  echo "$1" | grep -q 'status:done' && echo "  PASS $2" || { echo "  FAIL $2 (expected latest status:done)"; fails=$((fails+1)); }
}
assert_miss()  { # <json> <label>
  echo "$1" | grep -q '"found": false' && echo "  PASS $2" || { echo "  FAIL $2"; fails=$((fails+1)); }
}

OUT="$("$CLI" task_state --id foo --json 2>&1)"
assert_found  "$OUT" "lookup by slug"
assert_latest "$OUT" "latest-wins (returns status:done)"
assert_miss   "$("$CLI" task_state --id bar --json 2>&1)" "unknown id misses clean"

[ "$fails" -eq 0 ] || { echo "RESULT: $fails assertion(s) failed"; exit 1; }
echo "RESULT: task-state keyed-lane surface OK (3/3)"
