#!/usr/bin/env bash
# cc-soul Memory System Health Verification
#
# Three-tier testing strategy to avoid observer effect:
# - Tier 1: Read-only tests (zero production impact)
# - Tier 2: Embedding tests (negligible impact)
# - Tier 3: Full lifecycle in isolated /tmp database
#
# Usage: scripts/test-soul-health.sh [--tier N] [--verbose] [--isolated-only]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERBOSE=${VERBOSE:-false}
TIER="all"

# Parse args
while [[ $# -gt 0 ]]; do
    case $1 in
        --tier) TIER="$2"; shift 2 ;;
        --verbose) VERBOSE=true; shift ;;
        --isolated-only) TIER="3"; shift ;;
        -h|--help)
            echo "Usage: $0 [--tier 1|2|3|all] [--verbose] [--isolated-only]"
            echo ""
            echo "Tiers:"
            echo "  1  Read-only tests against production (zero risk)"
            echo "  2  Embedding verification (negligible impact)"
            echo "  3  Full lifecycle in isolated /tmp database"
            echo ""
            echo "Options:"
            echo "  --verbose        Show detailed output"
            echo "  --isolated-only  Only run tier 3 (safe for CI)"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

echo "=== cc-soul Memory System Health Check ==="
echo "Tier: $TIER | Verbose: $VERBOSE"
echo ""

PASS=0
FAIL=0
SKIP=0

run_tier() {
    local tier=$1
    local script="${SCRIPT_DIR}/test-soul-tier${tier}.sh"

    if [[ ! -x "$script" ]]; then
        echo "[SKIP] Tier $tier - script not found: $script"
        ((SKIP++))
        return
    fi

    echo "--- Tier $tier ---"
    if $VERBOSE; then
        if "$script"; then
            ((PASS++))
        else
            ((FAIL++))
        fi
    else
        if "$script" 2>&1 | grep -E '^\[(PASS|FAIL|SKIP)\]'; then
            # Count results from output
            :
        else
            echo "[FAIL] Tier $tier - script error"
            ((FAIL++))
        fi
    fi
    echo ""
}

# Run requested tiers
case $TIER in
    1) run_tier 1 ;;
    2) run_tier 2 ;;
    3) run_tier 3 ;;
    all)
        for t in 1 2 3; do
            run_tier $t
        done
        ;;
    *) echo "Invalid tier: $TIER"; exit 1 ;;
esac

echo "=== Summary ==="
echo "Passed: $PASS | Failed: $FAIL | Skipped: $SKIP"

[[ $FAIL -eq 0 ]] && exit 0 || exit 1
