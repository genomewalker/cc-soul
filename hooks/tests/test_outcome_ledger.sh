#!/bin/bash
# Test hooks/outcome-ledger.sh: appends valid JSONL, is fail-open when $CHITTA_DB_PATH
# is unwritable, and never lets the caller see a nonzero exit.
set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FAIL=0
assert() { if ! eval "$2"; then echo "FAIL: $1"; FAIL=1; else echo "ok: $1"; fi; }

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# --- happy path: valid JSONL, ts + session_id added ---
export CHITTA_DB_PATH="$T/mind"
source "$SCRIPT_DIR/outcome-ledger.sh"
ledger_append '{"event":"injected","ids":[1,2]}' "sess-1"
LEDGER="$CHITTA_DB_PATH/outcome_ledger.jsonl"
assert "ledger file created" "[[ -f '$LEDGER' ]]"
assert "one line written" "[[ \$(wc -l < '$LEDGER') -eq 1 ]]"
assert "line is valid JSON" "jq -e . '$LEDGER' >/dev/null 2>&1 || jq -e . <<<\"\$(cat '$LEDGER')\" >/dev/null 2>&1"
LINE=$(cat "$LEDGER")
assert "event field preserved" "[[ \$(echo \"\$LINE\" | jq -r .event) == injected ]]"
assert "session_id added" "[[ \$(echo \"\$LINE\" | jq -r .session_id) == sess-1 ]]"
assert "ts is a number" "echo \"\$LINE\" | jq -e '.ts | type == \"number\"' >/dev/null"

# --- second event appends, doesn't overwrite ---
ledger_append '{"event":"bash_outcome","exit_code":0,"cmd_head":"echo hi"}' "sess-1"
assert "second line appended" "[[ \$(wc -l < '$LEDGER') -eq 2 ]]"

# --- fail-open: unwritable dir must not error and must not write ---
export CHITTA_DB_PATH="$T/no-such/deeply/nested"
chmod 000 "$T" 2>/dev/null
ledger_append '{"event":"session_end"}' "sess-2"
RC=$?
assert "fail-open: exit code 0 despite unwritable dir" "[[ $RC -eq 0 ]]"
chmod 755 "$T" 2>/dev/null

# --- fail-open: empty event is a no-op, not an error ---
export CHITTA_DB_PATH="$T/mind"
ledger_append '' "sess-3"
assert "empty event: exit code 0" "[[ $? -eq 0 ]]"
assert "empty event: line count unchanged" "[[ \$(wc -l < '$LEDGER') -eq 2 ]]"

exit $FAIL
