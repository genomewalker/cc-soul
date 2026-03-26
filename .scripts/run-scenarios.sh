#!/usr/bin/env bash
set -uo pipefail

# Runner for scenario regression suite
# Usage: bash .scripts/run-scenarios.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PASS=0
FAIL=0

for s in "$SCRIPT_DIR"/scenarios/scenario-*.sh; do
    [[ -f "$s" ]] || continue
    echo "=== $(basename "$s") ==="
    if bash "$s"; then
        echo "SUITE PASS: $(basename "$s")"
        PASS=$((PASS + 1))
    else
        echo "SUITE FAIL: $(basename "$s")"
        FAIL=$((FAIL + 1))
    fi
    echo ""
done

echo "================================"
echo "Suite results: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
