#!/usr/bin/env bash
set -euo pipefail

# Scenario 2: Task Fencing
# Verifies that memory updates create new IDs (replace semantics), old versions
# become inaccessible, and the latest state is always retrievable.

CHITTA="${HOME}/.claude/bin/chitta"
PREFIX="scen2-$(date +%s)"
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

extract_id() {
    grep -o '"id"[[:space:]]*:[[:space:]]*"[^"]*"' | head -1 | sed 's/.*"id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/'
}

# Extract the new ID from "Updated → #NEWID" output
extract_update_id() {
    grep -o '#[0-9]*' | head -1 | tr -d '#'
}

cleanup() {
    echo "Cleaning up ${PREFIX} memories..."
    for id in "${STORED_IDS[@]}"; do
        $CHITTA forget --id "$id" 2>/dev/null || true
    done
}
trap cleanup EXIT

echo "=== Scenario 2: Task Fencing ==="

# Step 1: Create a task memory in initial state
echo "Creating task memory (initial state: pending, token=0)..."
task_result=$($CHITTA remember --content "${PREFIX}: task-state=pending token=0" --json 2>&1)
task_id=$(echo "$task_result" | extract_id)
STORED_IDS+=("$task_id")
echo "  Task created: id=$task_id"

# Step 2: Transition to in_progress with token=1 (update creates new ID)
echo "Transitioning: pending -> in_progress (token=1)..."
update_output=$($CHITTA update --id "$task_id" --content "${PREFIX}: task-state=in_progress token=1" 2>&1)
new_id=$(echo "$update_output" | extract_update_id)
if [[ -n "$new_id" ]]; then
    STORED_IDS+=("$new_id")
    echo "  New ID after transition: $new_id"
fi

# Verify old ID is no longer accessible (replaced)
old_get=$($CHITTA get --id "$task_id" --json 2>&1)
assert_contains "Old ID returns null/empty after update" "$old_get" "null"

# Verify new state via new ID
if [[ -n "$new_id" ]]; then
    current=$($CHITTA get --id "$new_id" --json 2>&1)
    assert_contains "New ID has in_progress state" "$current" "task-state=in_progress"
    assert_contains "New ID has token=1" "$current" "token=1"

    # Step 3: Transition to completed with token=2
    echo "Transitioning: in_progress -> completed (token=2)..."
    update_output2=$($CHITTA update --id "$new_id" --content "${PREFIX}: task-state=completed token=2" 2>&1)
    final_id=$(echo "$update_output2" | extract_update_id)
    if [[ -n "$final_id" ]]; then
        STORED_IDS+=("$final_id")
        echo "  Final ID: $final_id"
    fi

    # Verify intermediate ID is gone
    mid_get=$($CHITTA get --id "$new_id" --json 2>&1)
    assert_contains "Intermediate ID returns null after second update" "$mid_get" "null"

    # Verify final state
    if [[ -n "$final_id" ]]; then
        final=$($CHITTA get --id "$final_id" --json 2>&1)
        assert_contains "Final state is completed" "$final" "task-state=completed"
        assert_contains "Final token is 2" "$final" "token=2"
    fi
else
    echo "FAIL: Could not extract new ID from update"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
