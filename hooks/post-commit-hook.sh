#!/bin/bash
# Post-commit hook: Crystallize knowledge after commits
#
# Install: ln -sf $(pwd)/scripts/post-commit-hook.sh .git/hooks/post-commit
#
# Workflow:
#   Development: Stop hook extracts patterns → dev:hot (ephemeral, fast decay)
#   Commit: Crystallize → remove dev:hot, import stable bootstrap
#
# This ensures the soul's self-knowledge stays in sync with the codebase.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BOOTSTRAP_FILE="$PROJECT_DIR/bootstrap/cc-soul.soul"
CHITTA="$PROJECT_DIR/bin/chitta"

# Only run if chitta is built
[[ ! -x "$CHITTA" ]] && exit 0

# Always clean up dev:hot nodes on commit (ephemeral development knowledge)
# These were created by the Stop hook during development
echo "[cc-soul] Cleaning ephemeral dev:hot nodes..."
"$CHITTA" recall_by_tag --tag "dev:hot" --limit 100 2>/dev/null | \
    grep -oP '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}' | \
    while read -r id; do
        "$CHITTA" remove --id "$id" 2>/dev/null || true
    done

# Check if the commit touched the bootstrap file or key architecture files
CHANGED_FILES=$(git diff-tree --no-commit-id --name-only -r HEAD 2>/dev/null || true)

# Crystallize: full rewire on architecture changes
if [[ -f "$BOOTSTRAP_FILE" ]] && echo "$CHANGED_FILES" | grep -qE '(bootstrap/.*\.soul|chitta/include/|chitta/src/)'; then
    echo "[cc-soul] Crystallizing knowledge (full rewire)..."
    "$CHITTA" import_soul --file "$BOOTSTRAP_FILE" --replace true 2>/dev/null || true
fi

echo "[cc-soul] Commit crystallized"
