#!/usr/bin/env bash
set -euo pipefail

# Scenario 5: Conflicting Evidence
# Verifies that when memory B supersedes memory A via force_supersede_ids,
# A is marked superseded and B remains active.

CHITTA="${HOME}/.claude/bin/chitta"
PREFIX="scen5-$(date +%s)"
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

echo "=== Scenario 5: Conflicting Evidence ==="

# Step 1: Store memory A — the incorrect claim
echo "Storing memory A (incorrect claim: SQLite backend)..."
a_result=$($CHITTA observe \
    --title "${PREFIX}-backend-wrong" \
    --content "${PREFIX}: chitta uses SQLite as its backend storage engine" \
    --category fact \
    --source mcp_tool \
    --confidence 0.8 \
    --json 2>&1)
a_id=$(echo "$a_result" | extract_id)
STORED_IDS+=("$a_id")
echo "  Memory A stored: id=$a_id"

sleep 1

# Step 2: Store memory B — the correction, explicitly superseding A
echo "Storing memory B (correction: memory-mapped, supersedes A)..."
b_result=$($CHITTA observe \
    --title "${PREFIX}-backend-correct" \
    --content "${PREFIX}: chitta uses memory-mapped files via chitta-field, not SQLite" \
    --category correction \
    --source distillation \
    --confidence 0.95 \
    --force_supersede_ids "$a_id" \
    --json 2>&1)
b_id=$(echo "$b_result" | extract_id)
STORED_IDS+=("$b_id")
echo "  Memory B stored: id=$b_id"

sleep 1

# Step 3: Verify A is superseded
echo "Checking memory A status..."
a_status=$(rpc_call "memory_status" "{\"id\":\"$a_id\"}")
assert_contains "Memory A is superseded" "$a_status" "superseded"

# Step 4: Verify B is active
echo "Checking memory B status..."
b_status=$(rpc_call "memory_status" "{\"id\":\"$b_id\"}")
assert_contains "Memory B is active" "$b_status" "active"

# Step 5: Verify B content is intact
b_get=$($CHITTA get --id "$b_id" --json 2>&1)
assert_contains "Memory B content preserved" "$b_get" "memory-mapped files"

# Step 6: Verify supersession triplet exists
echo "Checking supersession triplet..."
triplet_result=$(rpc_call "memory_status" "{\"id\":\"$a_id\"}")
assert_contains "Supersession relationship recorded" "$triplet_result" "superseded_by"

echo ""
echo "Results: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
