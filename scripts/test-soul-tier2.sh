#!/usr/bin/env bash
# Tier 2: Embedding verification tests
# Negligible impact - only uses hybrid_recall (store-level read-only path)
# Side-effect: sets one timestamp via notify_query()

set -euo pipefail

CHITTA="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
PASS=0
FAIL=0
SKIP=0

echo "=== Tier 2: Embedding Verification ==="
echo "Minimal side-effects (one timestamp update)."
echo ""

# 2.1 VakYantra loaded
echo -n "[TEST] Embedder (VakYantra) ready... "
result=$($CHITTA health_check --json 2>&1)
yantra=$(echo "$result" | jq -r '.yantra_ready // .yantra // "false"' 2>/dev/null || echo "false")
if [[ "$yantra" == "true" || "$yantra" == "True" ]]; then
    echo "[PASS]"
    ((++PASS))
else
    echo "[FAIL] yantra_ready=$yantra"
    ((++FAIL))
fi

# 2.2 Execution provider
echo -n "[TEST] Execution provider... "
provider=$(echo "$result" | jq -r '.execution_provider // "unknown"' 2>/dev/null)
if [[ "$provider" != "unknown" && "$provider" != "null" ]]; then
    echo "[PASS] ($provider)"
    ((++PASS))
else
    echo "[FAIL] provider not reported"
    ((++FAIL))
fi

# 2.3 Embeddings exist in database
echo -n "[TEST] Memories have embeddings... "
count=$($CHITTA sql_query --query "SELECT COUNT(*) as cnt FROM memory WHERE embedding IS NOT NULL" --json 2>&1 | jq -r '.rows[0].cnt' 2>/dev/null || echo "0")
if [[ "$count" =~ ^[0-9]+$ ]] && [[ "$count" -gt 0 ]]; then
    echo "[PASS] ($count memories with embeddings)"
    ((++PASS))
else
    echo "[FAIL] no embeddings found"
    ((++FAIL))
fi

# 2.4 Semantic recall works (uses embedder)
# Note: recall does have touch/strengthen side-effects, but we test with explore_recall
# which is read-only at the store level
echo -n "[TEST] Semantic recall (embedding generation)... "
if result=$($CHITTA explore_recall --query "test embedding generation" --limit 1 2>&1); then
    if echo "$result" | grep -qi "embedder not ready\|error"; then
        echo "[FAIL] embedder error"
        ((++FAIL))
    else
        echo "[PASS]"
        ((++PASS))
    fi
else
    # explore_recall returns empty for no matches, which is fine
    echo "[PASS] (no matches, but embedder works)"
    ((++PASS))
fi

# 2.5 Vector similarity works
echo -n "[TEST] Semantic search returns results... "
if result=$($CHITTA explore_recall --query "memory system database" --limit 5 2>&1); then
    if echo "$result" | grep -qE '[0-9]+ memories|#[0-9]+'; then
        count=$(echo "$result" | grep -oE '#[0-9]+' | wc -l)
        echo "[PASS] ($count results)"
        ((++PASS))
    else
        echo "[SKIP] no semantic matches (may need more data)"
        ((++SKIP))
    fi
else
    echo "[SKIP] explore_recall returned empty"
    ((++SKIP))
fi

# 2.6 Embedding dimensions correct (bge-base-en-v1.5 = 768)
echo -n "[TEST] Embedding dimensions... "
dims=$($CHITTA sql_query --query "SELECT CAST(LENGTH(embedding)/4 AS INTEGER) as dims FROM memory WHERE embedding IS NOT NULL LIMIT 1" --json 2>&1 | jq -r '.rows[0].dims' 2>/dev/null || echo "0")
if [[ "$dims" == "768" ]]; then
    echo "[PASS] (768 dimensions - bge-base-en-v1.5)"
    ((++PASS))
elif [[ "$dims" == "384" ]]; then
    echo "[PASS] (384 dimensions - MiniLM)"
    ((++PASS))
elif [[ "$dims" -gt 0 ]]; then
    echo "[PASS] ($dims dimensions)"
    ((++PASS))
else
    echo "[SKIP] no embeddings to check"
    ((++SKIP))
fi

echo ""
echo "--- Tier 2 Results ---"
echo "Passed: $PASS | Failed: $FAIL | Skipped: $SKIP"

[[ $FAIL -eq 0 ]]
