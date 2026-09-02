#!/bin/bash
# Test hooks/lib.sh registry_call(): invokes the registry with stdin passed
# through, returns 1 (not an error exit) when the registry file is missing so
# callers can fall back, and never lets a failing subprocess kill the caller.
set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FAIL=0
assert() { if ! eval "$2"; then echo "FAIL: $1"; FAIL=1; else echo "ok: $1"; fi; }

source "$SCRIPT_DIR/lib.sh"

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# --- registry present: stdin is passed through, timeout/subcmd/args forwarded ---
mkdir -p "$T/plugin-ok/chitta-mcp"
cat > "$T/plugin-ok/chitta-mcp/session_registry.py" <<'PY'
import sys
sys.stdout.write(sys.stdin.read())
with open(sys.argv[-1], "w") as f:
    f.write(" ".join(sys.argv[1:]))
PY
export CC_SOUL_PLUGIN_DIR="$T/plugin-ok"
OUT_MARKER="$T/call.log"
echo -n 'hello-stdin' | registry_call 2 heartbeat --queued "$OUT_MARKER"
RC=$?
assert "registry_call returns 0 when registry present" "[[ $RC -eq 0 ]]"
assert "subcmd + args forwarded" "grep -q 'heartbeat --queued' '$OUT_MARKER'"

# --- CHITTA_PLUGIN_DIR (renamed knob) resolves the same as CC_SOUL_PLUGIN_DIR ---
unset CC_SOUL_PLUGIN_DIR
export CHITTA_PLUGIN_DIR="$T/plugin-ok"
OUT_MARKER2="$T/call2.log"
echo -n 'hello-stdin' | registry_call 2 heartbeat --queued "$OUT_MARKER2"
RC=$?
assert "registry_call resolves via CHITTA_PLUGIN_DIR" "[[ $RC -eq 0 ]]"
assert "CHITTA_PLUGIN_DIR: subcmd + args forwarded" "grep -q 'heartbeat --queued' '$OUT_MARKER2'"
unset CHITTA_PLUGIN_DIR

# --- registry missing: fail-open, returns 1 so caller can branch to a fallback ---
# resolve_cc_soul_root() only checks that <root>/chitta-mcp exists as a
# directory (not that session_registry.py is in it) before accepting
# CHITTA_PLUGIN_DIR/CC_SOUL_PLUGIN_DIR, and it falls back to scanning for a
# *real* chitta checkout otherwise — so simulate "missing" with an empty
# chitta-mcp dir rather than a nonexistent plugin root, or the fallback
# chain would find this repo's own (present) registry and defeat the test.
mkdir -p "$T/plugin-missing/chitta-mcp"
export CC_SOUL_PLUGIN_DIR="$T/plugin-missing"
printf '%s' 'x' | registry_call 2 close
RC=$?
assert "registry_call returns 1 when registry file is absent" "[[ $RC -eq 1 ]]"

# --- caller fallback pattern (mirrors session-end-hook.sh) never errors ---
FALLBACK_RAN=0
if ! printf '%s' 'x' | registry_call 2 close; then
    FALLBACK_RAN=1
fi
assert "fallback branch runs when registry missing" "[[ $FALLBACK_RAN -eq 1 ]]"

exit $FAIL
