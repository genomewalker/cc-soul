#!/usr/bin/env bash
# Fails CI if prune_episodes() or run_prune_episodes() is called from anywhere
# outside the explicit, user-invoked RPC handler. Automatic/periodic pruning
# silently truncates episode count to 80% of max_count regardless of real
# history size, and has caused two data-loss incidents (9faaf4d9 removed it,
# a1b1abae reintroduced it 3 hours later). See chitta/src/subconscious.cpp.
set -euo pipefail

cd "$(dirname "$0")/.."

ALLOWED_FILE="chitta/src/handlers/register_system_tools.cpp"
ALLOWED_DECL="chitta/include/chitta/field_store.hpp"

violations=$(grep -rn "run_prune_episodes\|->prune_episodes(\|\.prune_episodes(" \
    --include="*.cpp" --include="*.hpp" chitta/ \
    | grep -v "^${ALLOWED_FILE}:" \
    | grep -v "^${ALLOWED_DECL}:" \
    || true)

if [ -n "$violations" ]; then
    echo "ERROR: prune_episodes must only be called from the manual RPC handler in $ALLOWED_FILE"
    echo "$violations"
    exit 1
fi

echo "OK: prune_episodes has no automatic call sites"
