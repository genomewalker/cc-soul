#!/usr/bin/env bash
set -euo pipefail

# Scenario 1: Stale Belief Override
# Verifies that a correction supersedes a stale belief, and the superseded memory
# is marked accordingly while the correction remains active.

CHITTA="${HOME}/.claude/bin/chitta"
PREFIX="scen1-$(date +%s)"
PASS=0; FAIL=0
STORED_IDS=()

assert_contains() {
    local desc="$1" result="$2" expected="$3"
    if echo "$result" | grep -q "$expected"; then
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc (expected '$expected' in result)"
        FAIL=$((FAIL + 1))
    fi
}

assert_not_contains() {
    local desc="$1" result="$2" unexpected="$3"
    if echo "$result" | grep -q "$unexpected"; then
        echo "FAIL: $desc (unexpected '$unexpected' found in result)"
        FAIL=$((FAIL + 1))
    else
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    fi
}

rpc_call() {
    local tool="$1" args="$2"
    echo "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"$tool\",\"arguments\":$args}}" | $CHITTA 2>/dev/null
}

extract_id() {
    grep -o '"id"[[:space:]]*:[[:space:]]*"[^"]*"' | head -1 | sed 's/.*"id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/'
}

cleanup() {
    echo "Cleaning up ${PREFIX} memories..."
    for id in "${STORED_IDS[@]}"; do
        $CHITTA forget --id "$id" 2>/dev/null || true
    done
}
trap cleanup EXIT

echo "=== Scenario 1: Stale Belief Override ==="

# Step 1: Store a stale belief
echo "Storing stale belief..."
stale_result=$($CHITTA remember --content "${PREFIX}: Python 2 is the standard python version" --json 2>&1)
stale_id=$(echo "$stale_result" | extract_id)
STORED_IDS+=("$stale_id")
echo "  Stale belief stored: id=$stale_id"

sleep 1

# Step 2: Store a correction that explicitly supersedes the stale belief
echo "Storing correction with force_supersede_ids..."
correction_result=$($CHITTA observe \
    --title "${PREFIX}-correction" \
    --content "${PREFIX}: Python 3 is the standard python version, not Python 2" \
    --category correction \
    --source distillation \
    --confidence 0.95 \
    --force_supersede_ids "$stale_id" \
    --json 2>&1)
correction_id=$(echo "$correction_result" | extract_id)
STORED_IDS+=("$correction_id")
echo "  Correction stored: id=$correction_id"

sleep 1

# Step 3: Verify stale memory is superseded via RPC memory_status
echo "Checking stale memory status..."
status_result=$(rpc_call "memory_status" "{\"id\":\"$stale_id\"}")
assert_contains "Stale belief marked superseded" "$status_result" "superseded"

# Step 4: Verify correction is still active
echo "Checking correction status..."
correction_status=$(rpc_call "memory_status" "{\"id\":\"$correction_id\"}")
assert_contains "Correction is active" "$correction_status" "active"

# Step 5: Verify correction content is intact via get
echo "Verifying correction content..."
correction_get=$($CHITTA get --id "$correction_id" --json 2>&1)
assert_contains "Correction contains Python 3" "$correction_get" "Python 3"

# Step 6: Verify stale content references Python 2 (it exists but is superseded)
stale_get=$($CHITTA get --id "$stale_id" --json 2>&1)
assert_contains "Stale belief still retrievable by ID" "$stale_get" "Python 2"

echo ""
echo "Results: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
