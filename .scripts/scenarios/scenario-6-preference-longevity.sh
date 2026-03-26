#!/usr/bin/env bash
set -euo pipefail

# Scenario 6: Preference Longevity
# Verifies that preference memories persist with positive confidence and active status.

CHITTA="${HOME}/.claude/bin/chitta"
PREFIX="scen6-$(date +%s)"
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

assert_gt_zero() {
    local desc="$1" value="$2"
    local num
    num=$(echo "$value" | grep -oE '[0-9]+\.?[0-9]*' | head -1)
    if [[ -z "$num" ]]; then
        echo "FAIL: $desc (could not extract numeric value from: $value)"
        FAIL=$((FAIL + 1))
        return
    fi
    if awk "BEGIN{exit !($num > 0)}"; then
        echo "PASS: $desc (value=$num)"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc (value=$num, expected > 0)"
        FAIL=$((FAIL + 1))
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

echo "=== Scenario 6: Preference Longevity ==="

# Step 1: Store preference memory with high confidence (durable tier)
echo "Storing preference memory (distillation, 0.90)..."
pref_result=$($CHITTA observe \
    --title "${PREFIX}-editor-pref" \
    --content "${PREFIX}: user prefers neovim with lua configuration over vscode" \
    --category preference \
    --source distillation \
    --confidence 0.90 \
    --json 2>&1)
pref_id=$(echo "$pref_result" | extract_id)
STORED_IDS+=("$pref_id")
echo "  Preference stored: id=$pref_id"

# Step 2: Retrieve by ID and verify content
echo "Retrieving preference by ID..."
pref_get=$($CHITTA get --id "$pref_id" --json 2>&1)
assert_contains "Preference content preserved" "$pref_get" "neovim"

# Step 3: Verify confidence > 0
conf=$(echo "$pref_get" | grep -o '"confidence"[[:space:]]*:[[:space:]]*[0-9.]*' | head -1 | sed 's/.*:[[:space:]]*//')
assert_gt_zero "Preference has positive confidence" "$conf"

# Step 4: Verify strength > 0
strength=$(echo "$pref_get" | grep -o '"strength"[[:space:]]*:[[:space:]]*[0-9.]*' | head -1 | sed 's/.*:[[:space:]]*//')
assert_gt_zero "Preference has positive strength" "$strength"

# Step 5: Verify status is active (not decayed/superseded)
echo "Checking preference status..."
status_result=$(rpc_call "memory_status" "{\"id\":\"$pref_id\"}")
assert_contains "Preference status is active" "$status_result" "active"

# Step 6: Verify type is preference (category mapping)
assert_contains "Memory type is preference" "$pref_get" "preference"

echo ""
echo "Results: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
