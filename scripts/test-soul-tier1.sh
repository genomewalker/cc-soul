#!/usr/bin/env bash
# Tier 1: Read-only tests against production database
# Zero risk - all operations verified to have no DB side-effects

set -euo pipefail

CHITTA="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
PASS=0
FAIL=0
SKIP=0

# The three helpers below `eval` their command argument. Justified: every caller
# passes a string literal written in this file, so nothing external reaches the
# shell. `eval` (not `bash -c`) is deliberate — it keeps the `set -euo pipefail`
# above in force, so a failing left-hand side of a `... | jq ...` test still
# fails the test instead of being masked by jq's exit status.
run_test() {
    local name="$1"
    local cmd="$2"

    if result=$(eval "$cmd" 2>&1); then
        echo "[PASS] $name"
        ((++PASS))
        return 0
    else
        echo "[FAIL] $name"
        [[ -n "${VERBOSE:-}" ]] && echo "  Error: $result"
        ((++FAIL))
        return 1
    fi
}

assert_json() {
    local name="$1"
    local cmd="$2"
    local jq_filter="$3"
    local expected="$4"

    local result
    if ! result=$(eval "$cmd" 2>&1); then
        echo "[FAIL] $name - command failed"
        ((++FAIL))
        return 1
    fi

    local actual
    actual=$(echo "$result" | jq -r "$jq_filter" 2>/dev/null || echo "PARSE_ERROR")

    if [[ "$actual" == "$expected" ]]; then
        echo "[PASS] $name"
        ((++PASS))
    else
        echo "[FAIL] $name - expected '$expected', got '$actual'"
        ((++FAIL))
    fi
}

assert_nonzero() {
    local name="$1"
    local cmd="$2"
    local jq_filter="$3"

    local result
    if ! result=$(eval "$cmd" 2>&1); then
        echo "[FAIL] $name - command failed"
        ((++FAIL))
        return 1
    fi

    local count
    count=$(echo "$result" | jq -r "$jq_filter" 2>/dev/null || echo "0")

    if [[ "$count" =~ ^[0-9]+$ ]] && [[ "$count" -gt 0 ]]; then
        echo "[PASS] $name (count: $count)"
        ((++PASS))
    else
        echo "[FAIL] $name - expected nonzero, got '$count'"
        ((++FAIL))
    fi
}

echo "=== Tier 1: Read-Only Tests ==="
echo "These tests have ZERO side-effects on production data."
echo ""

# 1.1 Daemon connectivity
run_test "Daemon connectivity" "$CHITTA health_check"

# 1.2 Status is OK
assert_json "Health status OK" "$CHITTA health_check --json" ".status" "ok"

# 1.3 Version reported
run_test "Version check" "$CHITTA version_check"

# 1.4 Memory count
assert_nonzero "Memories exist" "$CHITTA sql_query --query 'SELECT COUNT(*) as cnt FROM memory' --json" ".rows[0].cnt"

# 1.5 Memory schema
run_test "Memory table schema" "$CHITTA sql_query --query 'DESCRIBE memory'"

# 1.6 Realm list
run_test "Realm list" "$CHITTA realm_list"

# 1.7 Realm detection
run_test "Realm detection" "$CHITTA realm_detect"

# 1.8 Theme list
run_test "Theme list" "$CHITTA theme_list --limit 5"

# 1.9 Theme stats
run_test "Theme stats" "$CHITTA theme_stats"

# 1.10 Hygiene stats
run_test "Hygiene stats" "$CHITTA hygiene_stats"

# 1.11 Conversation turns table
run_test "Turns table exists" "$CHITTA sql_query --query 'SELECT COUNT(*) FROM conversation_turn'"

# 1.12 Episode table
run_test "Episode table exists" "$CHITTA sql_query --query 'SELECT COUNT(*) FROM episode'"

# 1.13 Session list
run_test "Session list" "$CHITTA session_list --limit 3"

# 1.14 Graph query (read-only)
run_test "Graph query" "$CHITTA query_graph --subject test --limit 1"

# 1.15 FTS extension
run_test "FTS extension loaded" "$CHITTA sql_query --query \"SELECT * FROM duckdb_extensions() WHERE extension_name = 'fts' AND loaded\""

# 1.16 Memory kind distribution
assert_nonzero "Memory kinds" "$CHITTA sql_query --query 'SELECT kind, COUNT(*) as cnt FROM memory GROUP BY kind LIMIT 10' --json" ".rows | length"

# 1.17 Triplet count
assert_nonzero "Triplets exist" "$CHITTA sql_query --query 'SELECT COUNT(*) as cnt FROM triplet' --json" ".rows[0].cnt"

# 1.18 Turns have data
assert_nonzero "Turns exist" "$CHITTA sql_query --query 'SELECT COUNT(*) as cnt FROM conversation_turn' --json" ".rows[0].cnt"

# 1.19 Get single memory (read-only)
FIRST_ID=$($CHITTA sql_query --query "SELECT id FROM memory LIMIT 1" --json 2>/dev/null | jq -r '.rows[0].id' 2>/dev/null || echo "")
if [[ -n "$FIRST_ID" && "$FIRST_ID" != "null" ]]; then
    run_test "Get memory by ID" "$CHITTA get --id $FIRST_ID"
else
    echo "[SKIP] Get memory - no memories found"
    ((++SKIP))
fi

echo ""
echo "--- Tier 1 Results ---"
echo "Passed: $PASS | Failed: $FAIL | Skipped: $SKIP"

[[ $FAIL -eq 0 ]]
