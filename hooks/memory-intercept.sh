#!/bin/bash
# memory-intercept.sh - Intercept Claude Code MEMORY.md writes and sync to chitta
# PostToolUse hook for Write tool

# Don't use set -e: we want hooks to succeed even if some parts fail

# Read JSON input from stdin
INPUT=$(cat)

# Extract file path from tool_input
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // ""')

# Only process MEMORY.md writes
if [[ "$FILE_PATH" != */memory/MEMORY.md ]]; then
    exit 0
fi

# Extract the content that was written
CONTENT=$(echo "$INPUT" | jq -r '.tool_input.content // ""')

if [[ -z "$CONTENT" ]]; then
    exit 0
fi

# Find chitta binary
CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
if [[ ! -x "$CHITTA_BIN" ]]; then
    echo "[memory-intercept] chitta not found" >&2
    exit 0
fi

# Extract project name from path
# Path format: ~/.claude/projects/-Users-...-projectname/memory/MEMORY.md
PROJECT_DIR=$(dirname "$(dirname "$FILE_PATH")")
PROJECT_NAME=$(basename "$PROJECT_DIR" | sed 's/^-Users-[^-]*-Downloads-//' | sed 's/^-Users-[^-]*-//')

echo "[memory-intercept] Importing MEMORY.md for project: $PROJECT_NAME" >&2

# Parse MEMORY.md content - extract sections and store in chitta
# Skip the header and auto-synced section, import user-added content
IMPORT_CONTENT=""
IN_USER_SECTION=false
while IFS= read -r line; do
    # Skip empty lines at start
    [[ -z "$line" ]] && continue

    # Skip auto-synced chitta section
    if [[ "$line" == *"Chitta Soul Memories (auto-synced)"* ]]; then
        IN_USER_SECTION=false
        continue
    fi

    # Skip the sync timestamp
    if [[ "$line" == "*Last synced:"* ]]; then
        continue
    fi

    # Skip dividers
    if [[ "$line" == "---" ]]; then
        IN_USER_SECTION=true
        continue
    fi

    # Collect user content (after divider or before chitta section)
    if [[ "$IN_USER_SECTION" == true ]] || [[ "$line" != *"auto-synced"* ]]; then
        # Skip headers that are just project name
        [[ "$line" == "# "* ]] && continue
        IMPORT_CONTENT="${IMPORT_CONTENT}${line}\n"
    fi
done <<< "$CONTENT"

# If there's user content to import, send to chitta
if [[ -n "$IMPORT_CONTENT" && "$IMPORT_CONTENT" != *"Chitta Soul"* ]]; then
    # Format as SSL and remember
    SSL_CONTENT="[memory:$PROJECT_NAME] Claude Code MEMORY.md import\n$IMPORT_CONTENT"

    # Use remember to store (fire-and-forget via CLI)
    ESCAPED_CONTENT=$(echo -e "$SSL_CONTENT")
    if timeout 2 "$CHITTA_BIN" remember --content "$ESCAPED_CONTENT" --tags "memory-import,$PROJECT_NAME" >/dev/null 2>&1; then
        echo "[memory-intercept] Imported to chitta" >&2
    fi
fi

# Now enhance MEMORY.md with chitta's memories for this project
# Query chitta for relevant memories
CHITTA_MEMORIES=$("$CHITTA_BIN" recall --query "$PROJECT_NAME" --limit 10 --text-only 2>/dev/null || true)

if [[ -n "$CHITTA_MEMORIES" && "$CHITTA_MEMORIES" != *"No memories"* ]]; then
    # Rebuild MEMORY.md with both user content and chitta memories
    cat > "$FILE_PATH" << MEMEOF
# $PROJECT_NAME Memory

## Chitta Soul Memories (auto-synced)

$CHITTA_MEMORIES

---

## Project Notes

$(echo -e "$IMPORT_CONTENT")

---
*Last synced: $(date '+%Y-%m-%d %H:%M')*
MEMEOF
    echo "[memory-intercept] Enhanced MEMORY.md with chitta memories" >&2
fi

exit 0
