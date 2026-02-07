#!/bin/bash
# Auto-index codebase on session start
#
# Runs incremental indexing in background to avoid blocking session startup.
# Only indexes if:
#   1. We're in a git repo
#   2. Haven't indexed in the last 10 minutes (rate limiting)
#
# Usage: auto-index.sh (no arguments)

set -e

# Config
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind/chitta}"
INDEX_INTERVAL="${CC_SOUL_INDEX_INTERVAL:-600}"  # 10 minutes default
MAX_FILES="${CC_SOUL_MAX_INDEX_FILES:-200}"
LOCK_FILE="/tmp/chitta-autoindex.lock"
LAST_INDEX_FILE="$MIND_PATH/.last_autoindex"

# djb2 hash for socket path
djb2_hash() {
    local str="$1" hash=5381 i c
    for ((i=0; i<${#str}; i++)); do
        c=$(printf '%d' "'${str:$i:1}")
        hash=$(( ((hash << 5) + hash) + c ))
        hash=$((hash & 0xFFFFFFFF))
    done
    echo "$hash"
}

SOCKET="/tmp/chitta-$(djb2_hash "$MIND_PATH").sock"
CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"

# Ensure daemon is running (required for indexing)
if [[ ! -S "$SOCKET" ]]; then
    exit 0  # Daemon not running, let simple-hook.sh handle it
fi

# Check if we're in a git repo
if ! git rev-parse --git-dir >/dev/null 2>&1; then
    exit 0  # Not a git repo, skip indexing
fi

# Get project root
PROJECT_ROOT=$(git rev-parse --show-toplevel 2>/dev/null)
if [[ -z "$PROJECT_ROOT" ]]; then
    exit 0
fi

# Rate limiting: check last index time
if [[ -f "$LAST_INDEX_FILE" ]]; then
    last_index=$(cat "$LAST_INDEX_FILE" 2>/dev/null || echo 0)
    now=$(date +%s)
    elapsed=$((now - last_index))

    if [[ $elapsed -lt $INDEX_INTERVAL ]]; then
        # Recently indexed, skip
        exit 0
    fi
fi

# Prevent concurrent indexing
if ! mkdir "$LOCK_FILE" 2>/dev/null; then
    # Another indexing in progress
    exit 0
fi

# Cleanup lock on exit
trap 'rmdir "$LOCK_FILE" 2>/dev/null' EXIT

# Run incremental indexing in background
(
    # Detect project name
    PROJECT_NAME=$(basename "$PROJECT_ROOT")

    if [[ -x "$CHITTA_BIN" ]]; then
        result=$("$CHITTA_BIN" learn_codebase \
            --path "$PROJECT_ROOT" \
            --project "$PROJECT_NAME" \
            --incremental true \
            --max_files "$MAX_FILES" 2>&1)

        # Log result if any files were indexed
        if echo "$result" | grep -qE 'indexed [1-9]'; then
            echo "[auto-index] $PROJECT_NAME: $result" | head -1

            # Generate embeddings for newly indexed symbols
            embed_result=$("$CHITTA_BIN" embed_symbols --project "$PROJECT_NAME" 2>&1 || true)
            if echo "$embed_result" | grep -qE 'embedded [1-9]'; then
                echo "[auto-index] $PROJECT_NAME: embeddings - $embed_result" | head -1
            fi
        fi

        # Update last index time
        mkdir -p "$MIND_PATH"
        date +%s > "$LAST_INDEX_FILE"
    fi
) &

# Don't wait - let it run in background
disown

exit 0
