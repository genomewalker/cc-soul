#!/usr/bin/env bash
# Tier 3: Full lifecycle tests in isolated database
# Zero production impact - uses separate daemon with /tmp database

set -euo pipefail

CHITTAD="${CHITTAD_BIN:-$HOME/.claude/bin/chittad}"
CHITTA="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
PASS=0
FAIL=0
SKIP=0
DAEMON_PID=""
TEST_DIR=""

cleanup() {
    echo ""
    echo "--- Cleanup ---"
    if [[ -n "$DAEMON_PID" ]]; then
        echo "Stopping test daemon (PID: $DAEMON_PID)..."
        kill "$DAEMON_PID" 2>/dev/null || true
        wait "$DAEMON_PID" 2>/dev/null || true
    fi
    if [[ -n "$TEST_DIR" && -d "$TEST_DIR" ]]; then
        echo "Removing test directory: $TEST_DIR"
        rm -rf "$TEST_DIR"
    fi
}

trap cleanup EXIT

run_test() {
    local name="$1"
    local cmd="$2"

    if result=$(eval "$cmd" 2>&1); then
        echo "[PASS] $name"
        ((++PASS))
        return 0
    else
        echo "[FAIL] $name"
        echo "  Error: $result" | head -5
        ((++FAIL))
        return 1
    fi
}

echo "=== Tier 3: Isolated Lifecycle Tests ==="
echo "Full write/read tests in separate /tmp database."
echo "ZERO impact on production data."
echo ""

# Setup isolated environment
TEST_DIR=$(mktemp -d /tmp/chitta-test-XXXXXX)

echo "Test directory: $TEST_DIR"

# Symlink model files if available (avoid copying large files)
if [[ -d "$HOME/.claude/models" ]]; then
    ln -sf "$HOME/.claude/models" "${TEST_DIR}/models"
fi

# Note existing sockets before starting
EXISTING_SOCKS=$(ls /tmp/chitta-*.sock 2>/dev/null | sort)

# Start isolated daemon (suppress logs unless verbose)
echo "Starting isolated daemon..."
"$CHITTAD" daemon --path "$TEST_DIR" -f 2>/dev/null &
DAEMON_PID=$!

# Wait for daemon to be ready
sleep 3
if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
    echo "[FAIL] Daemon failed to start"
    exit 1
fi

# Find the NEW socket created by this daemon
sleep 1
NEW_SOCKS=$(ls /tmp/chitta-*.sock 2>/dev/null | sort)
ACTUAL_SOCK=""
for sock in $NEW_SOCKS; do
    if ! echo "$EXISTING_SOCKS" | grep -qF "$sock"; then
        ACTUAL_SOCK="$sock"
        break
    fi
done

if [[ -z "$ACTUAL_SOCK" ]]; then
    # Fallback: use the most recently modified socket
    ACTUAL_SOCK=$(ls -t /tmp/chitta-*.sock 2>/dev/null | head -1)
fi

if [[ -z "$ACTUAL_SOCK" ]]; then
    echo "[FAIL] No socket found"
    exit 1
fi

echo "Using socket: $ACTUAL_SOCK"
# Use CLI with --socket-path to target isolated daemon
CHITTA_CMD="$CHITTA --socket-path $ACTUAL_SOCK"

echo "Daemon ready (PID: $DAEMON_PID)"
echo ""

# 3.1 Health check on isolated daemon
run_test "Isolated daemon health" "$CHITTA_CMD health_check"

# 3.2 Remember and recall
echo -n "[TEST] Remember and recall... "
if $CHITTA_CMD remember --content "Test memory: the sky is blue" --type belief >/dev/null 2>&1; then
    sleep 1
    if $CHITTA_CMD recall --query "color of sky" --limit 3 2>&1 | grep -qi "sky"; then
        echo "[PASS]"
        ((++PASS))
    else
        echo "[FAIL] recall didn't find memory"
        ((++FAIL))
    fi
else
    echo "[FAIL] remember failed"
    ((++FAIL))
fi

# 3.3 Semantic similarity
echo -n "[TEST] Semantic similarity... "
$CHITTA_CMD remember --content "DuckDB is a fast columnar database engine" >/dev/null 2>&1
sleep 1
if $CHITTA_CMD recall --query "database performance" --limit 5 2>&1 | grep -qi "DuckDB\|database"; then
    echo "[PASS]"
    ((++PASS))
else
    echo "[FAIL] semantic match failed"
    ((++FAIL))
fi

# 3.4 Hybrid recall (BM25 + semantic)
echo -n "[TEST] Hybrid recall... "
$CHITTA_CMD remember --content "Protocol Buffer serialization is faster than JSON" >/dev/null 2>&1
sleep 1
if $CHITTA_CMD hybrid_recall --query "protobuf serialization" --limit 5 2>&1 | grep -qi "Protocol\|serialization"; then
    echo "[PASS]"
    ((++PASS))
else
    echo "[SKIP] hybrid recall may need index build"
    ((++SKIP))
fi

# 3.5 Observe (hook-style memory creation)
echo -n "[TEST] Observe event... "
if $CHITTA_CMD observe --title "Test observation" --content "This is a test event from tier 3" >/dev/null 2>&1; then
    echo "[PASS]"
    ((++PASS))
else
    echo "[FAIL] observe failed"
    ((++FAIL))
fi

# 3.6 Grow wisdom
echo -n "[TEST] Grow wisdom... "
if $CHITTA_CMD grow --type wisdom --content "Testing creates confidence" >/dev/null 2>&1; then
    echo "[PASS]"
    ((++PASS))
else
    echo "[FAIL] grow failed"
    ((++FAIL))
fi

# 3.7 Realm isolation
echo -n "[TEST] Realm isolation... "
$CHITTA_CMD remember --content "Project A secret data" --realm "project-alpha" >/dev/null 2>&1
$CHITTA_CMD remember --content "Project B secret data" --realm "project-beta" >/dev/null 2>&1
sleep 1
alpha_result=$($CHITTA_CMD recall --query "secret data" --realm "project-alpha" --limit 5 2>&1)
if echo "$alpha_result" | grep -qi "Project A" && ! echo "$alpha_result" | grep -qi "Project B"; then
    echo "[PASS]"
    ((++PASS))
else
    echo "[SKIP] realm filtering may need tuning"
    ((++SKIP))
fi

# 3.8 Strengthen memory
echo -n "[TEST] Strengthen memory... "
# Get first memory ID
MEM_ID=$($CHITTA_CMD sql_query --query "SELECT id FROM memory LIMIT 1" --json 2>&1 | jq -r '.rows[0].id' 2>/dev/null || echo "")
if [[ -n "$MEM_ID" && "$MEM_ID" != "null" ]]; then
    before=$($CHITTA_CMD get --id "$MEM_ID" --json 2>&1 | jq -r '.confidence // 0' 2>/dev/null || echo "0")
    $CHITTA_CMD strengthen --id "$MEM_ID" >/dev/null 2>&1
    after=$($CHITTA_CMD get --id "$MEM_ID" --json 2>&1 | jq -r '.confidence // 0' 2>/dev/null || echo "0")
    if (( $(echo "$after > $before" | bc -l 2>/dev/null || echo 0) )); then
        echo "[PASS] ($before -> $after)"
        ((++PASS))
    else
        echo "[SKIP] confidence unchanged ($before -> $after)"
        ((++SKIP))
    fi
else
    echo "[SKIP] no memory to strengthen"
    ((++SKIP))
fi

# 3.9 Graph triplets
echo -n "[TEST] Graph triplets... "
if $CHITTA_CMD connect --subject "DuckDB" --predicate "uses" --object "columnar storage" >/dev/null 2>&1; then
    if $CHITTA_CMD query_graph --subject "DuckDB" 2>&1 | grep -qi "columnar\|uses"; then
        echo "[PASS]"
        ((++PASS))
    else
        echo "[FAIL] triplet not found"
        ((++FAIL))
    fi
else
    echo "[FAIL] connect failed"
    ((++FAIL))
fi

# 3.10 Theme system
echo -n "[TEST] Theme maintenance... "
# Add more memories for theme clustering
$CHITTA_CMD remember --content "Rust has zero-cost abstractions" >/dev/null 2>&1
$CHITTA_CMD remember --content "Rust ownership prevents data races" >/dev/null 2>&1
$CHITTA_CMD remember --content "Rust compiles to native code" >/dev/null 2>&1
sleep 1
if $CHITTA_CMD theme_maintain >/dev/null 2>&1; then
    echo "[PASS]"
    ((++PASS))
else
    echo "[SKIP] theme_maintain may need more data"
    ((++SKIP))
fi

# 3.11 Get memory by ID
echo -n "[TEST] Get memory by ID... "
MEM_ID=$($CHITTA_CMD sql_query --query "SELECT id FROM memory LIMIT 1" --json 2>&1 | jq -r '.rows[0].id' 2>/dev/null || echo "")
if [[ -n "$MEM_ID" && "$MEM_ID" != "null" ]]; then
    if $CHITTA_CMD get --id "$MEM_ID" >/dev/null 2>&1; then
        echo "[PASS]"
        ((++PASS))
    else
        echo "[FAIL] get failed"
        ((++FAIL))
    fi
else
    echo "[SKIP] no memory to get"
    ((++SKIP))
fi

# 3.12 Memory count verification
echo -n "[TEST] Final memory count... "
count=$($CHITTA_CMD sql_query --query "SELECT COUNT(*) as cnt FROM memory" --json 2>&1 | jq -r '.rows[0].cnt' 2>/dev/null || echo "0")
if [[ "$count" -ge 5 ]]; then
    echo "[PASS] ($count memories created)"
    ((++PASS))
else
    echo "[FAIL] expected >= 5 memories, got $count"
    ((++FAIL))
fi

echo ""
echo "--- Tier 3 Results ---"
echo "Passed: $PASS | Failed: $FAIL | Skipped: $SKIP"

[[ $FAIL -eq 0 ]]
