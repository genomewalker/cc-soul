#!/usr/bin/env bash
set -euo pipefail

# Scenario 3: Replay Integrity
# Verifies that the memory store is consistent within a single daemon lifecycle:
# 1. Memories stored via remember/observe are immediately retrievable by ID
# 2. Multiple memories can coexist without corruption
# 3. The health_check endpoint reports consistent counts
#
# NOTE: Cross-restart durability depends on WAL/snapshot compatibility between
# the binary that wrote the snapshot and the binary reading it. If the field
# store schema changed, older snapshots may not deserialize (known limitation).

CHITTA="${HOME}/.claude/bin/chitta"
PREFIX="scen3-$(date +%s)"
PASS=0; FAIL=0
STORED_IDS=()
NUM_MEMORIES=5

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

echo "=== Scenario 3: Replay Integrity ==="

# Step 1: Record baseline health
echo "Recording baseline health..."
pre_health=$($CHITTA health_check --json 2>&1)
pre_count=$(echo "$pre_health" | grep -o '"memory_count"[[:space:]]*:[[:space:]]*[0-9]*' | sed 's/.*:[[:space:]]*//')
pre_count=${pre_count:-0}
echo "  Baseline memory_count: $pre_count"

assert_contains "Daemon is healthy" "$pre_health" '"status"'

# Step 2: Store N memories with known unique content
echo "Storing $NUM_MEMORIES memories..."
for i in $(seq 1 $NUM_MEMORIES); do
    result=$($CHITTA remember --content "${PREFIX}: replay-item-${i} unique-payload-${RANDOM}" --json 2>&1)
    id=$(echo "$result" | extract_id)
    STORED_IDS+=("$id")
    echo "  Stored item $i: id=$id"
done

# Step 3: Verify all retrievable immediately by ID
echo "Verifying all memories retrievable by ID..."
for i in $(seq 0 $((NUM_MEMORIES - 1))); do
    id="${STORED_IDS[$i]}"
    result=$($CHITTA get --id "$id" --json 2>&1)
    assert_contains "Item $((i+1)) retrievable by ID" "$result" "replay-item-$((i+1))"
done

# Step 4: Verify health count increased
post_health=$($CHITTA health_check --json 2>&1)
post_count=$(echo "$post_health" | grep -o '"memory_count"[[:space:]]*:[[:space:]]*[0-9]*' | sed 's/.*:[[:space:]]*//')
post_count=${post_count:-0}
echo "  Post-store memory_count: $post_count"

expected_count=$((pre_count + NUM_MEMORIES))
if [[ "$post_count" -ge "$expected_count" ]]; then
    echo "PASS: Memory count increased by at least $NUM_MEMORIES ($pre_count -> $post_count)"
    PASS=$((PASS + 1))
else
    echo "FAIL: Memory count didn't increase as expected ($pre_count + $NUM_MEMORIES = $expected_count, got $post_count)"
    FAIL=$((FAIL + 1))
fi

# Step 5: Verify content integrity (round-trip: store -> get -> compare)
echo "Verifying content integrity..."
for i in $(seq 0 $((NUM_MEMORIES - 1))); do
    id="${STORED_IDS[$i]}"
    result=$($CHITTA get --id "$id" --json 2>&1)
    # Verify the unique prefix is in the content
    assert_contains "Item $((i+1)) content has correct prefix" "$result" "$PREFIX"
done

echo ""
echo "Results: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
