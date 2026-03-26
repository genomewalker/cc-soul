#!/usr/bin/env bash
set -euo pipefail

# Scenario 4: Memory Poisoning
# Verifies that high-confidence distillation memories are stored with higher confidence
# than low-confidence hook memories, and that confidence clamping is enforced.

CHITTA="${HOME}/.claude/bin/chitta"
PREFIX="scen4-$(date +%s)"
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

assert_gt() {
    local desc="$1" a="$2" b="$3"
    if awk "BEGIN{exit !($a > $b)}"; then
        echo "PASS: $desc ($a > $b)"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc ($a not > $b)"
        FAIL=$((FAIL + 1))
    fi
}

extract_id() {
    grep -o '"id"[[:space:]]*:[[:space:]]*"[^"]*"' | head -1 | sed 's/.*"id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/'
}

extract_confidence() {
    grep -o '"confidence"[[:space:]]*:[[:space:]]*[0-9.]*' | head -1 | sed 's/.*:[[:space:]]*//'
}

cleanup() {
    echo "Cleaning up ${PREFIX} memories..."
    for id in "${STORED_IDS[@]}"; do
        $CHITTA forget --id "$id" 2>/dev/null || true
    done
}
trap cleanup EXIT

echo "=== Scenario 4: Memory Poisoning ==="

# Step 1: Store memory A — high confidence, distillation source
echo "Storing high-confidence memory A (distillation, 0.95)..."
a_result=$($CHITTA observe \
    --title "${PREFIX}-arch-trusted" \
    --content "${PREFIX}: chitta architecture uses Rust-based memory-mapped field store with HNSW" \
    --category fact \
    --source distillation \
    --confidence 0.95 \
    --json 2>&1)
a_id=$(echo "$a_result" | extract_id)
STORED_IDS+=("$a_id")
echo "  Memory A stored: id=$a_id"

# Step 2: Store memory B — low confidence, hook_regex source (potential poison)
echo "Storing low-confidence memory B (hook_regex, 0.3)..."
b_result=$($CHITTA observe \
    --title "${PREFIX}-arch-poison" \
    --content "${PREFIX}: chitta architecture is simple key-value JSON file" \
    --category fact \
    --source hook_regex \
    --confidence 0.3 \
    --json 2>&1)
b_id=$(echo "$b_result" | extract_id)
STORED_IDS+=("$b_id")
echo "  Memory B stored: id=$b_id"

# Step 3: Retrieve both by ID and compare confidence
echo "Comparing stored confidence values..."
a_get=$($CHITTA get --id "$a_id" --json 2>&1)
b_get=$($CHITTA get --id "$b_id" --json 2>&1)

a_conf=$(echo "$a_get" | extract_confidence)
b_conf=$(echo "$b_get" | extract_confidence)

echo "  A confidence: $a_conf"
echo "  B confidence: $b_conf"

# Step 4: Assert A has higher confidence than B
assert_gt "High-confidence A outranks low-confidence B" "$a_conf" "$b_conf"

# Step 5: Verify A content is intact
assert_contains "Memory A content preserved" "$a_get" "memory-mapped field store"

# Step 6: Verify B confidence was clamped to hook_regex max (0.70)
# hook_regex max confidence is 0.70 per CONTRACTS.md
if awk "BEGIN{exit !($b_conf <= 0.70)}"; then
    echo "PASS: B confidence clamped to hook_regex max ($b_conf <= 0.70)"
    PASS=$((PASS + 1))
else
    echo "FAIL: B confidence exceeds hook_regex max ($b_conf > 0.70)"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
