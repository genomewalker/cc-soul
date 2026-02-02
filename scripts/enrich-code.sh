#!/bin/bash
# enrich-code.sh - Generate semantic descriptions for code symbols using OpenCode
#
# Called by chittad with a temp file containing symbol info:
#   SYMBOL_ID=<id>
#   KIND=<class|function|method>
#   NAME=<symbol name>
#   FILE_PATH=<source file>
#   LINE_START=<start line>
#   LINE_END=<end line>
#   MODEL=<opencode model>
#
# The script should:
# 1. Extract code snippet from file
# 2. Call OpenCode for description
# 3. Store as memory and link to symbol

set -e

TEMP_FILE="$1"
if [[ -z "$TEMP_FILE" || ! -f "$TEMP_FILE" ]]; then
    echo "[enrich] Error: No input file provided" >&2
    exit 1
fi

# Parse input
SYMBOL_ID=$(grep '^SYMBOL_ID=' "$TEMP_FILE" | cut -d= -f2)
KIND=$(grep '^KIND=' "$TEMP_FILE" | cut -d= -f2)
NAME=$(grep '^NAME=' "$TEMP_FILE" | cut -d= -f2)
FILE_PATH=$(grep '^FILE_PATH=' "$TEMP_FILE" | cut -d= -f2)
LINE_START=$(grep '^LINE_START=' "$TEMP_FILE" | cut -d= -f2)
LINE_END=$(grep '^LINE_END=' "$TEMP_FILE" | cut -d= -f2)
MODEL=$(grep '^MODEL=' "$TEMP_FILE" | cut -d= -f2)

MODEL="${MODEL:-github-copilot/gpt-5-mini}"
CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"

if [[ -z "$SYMBOL_ID" || -z "$FILE_PATH" ]]; then
    echo "[enrich] Error: Missing required fields" >&2
    exit 1
fi

# Extract code snippet (limit to 200 lines max)
MAX_LINES=200
ACTUAL_LINES=$((LINE_END - LINE_START + 1))
if [[ $ACTUAL_LINES -gt $MAX_LINES ]]; then
    LINE_END=$((LINE_START + MAX_LINES - 1))
fi

if [[ ! -f "$FILE_PATH" ]]; then
    echo "[enrich] Warning: File not found: $FILE_PATH" >&2
    exit 0
fi

CODE=$(sed -n "${LINE_START},${LINE_END}p" "$FILE_PATH" 2>/dev/null)

if [[ -z "$CODE" ]]; then
    echo "[enrich] Warning: Could not extract code from $FILE_PATH:$LINE_START-$LINE_END" >&2
    exit 0
fi

# Detect language from extension
EXT="${FILE_PATH##*.}"
case "$EXT" in
    cpp|hpp|h|cc|cxx) LANG="C++" ;;
    py) LANG="Python" ;;
    js|ts|jsx|tsx) LANG="JavaScript/TypeScript" ;;
    go) LANG="Go" ;;
    rs) LANG="Rust" ;;
    java) LANG="Java" ;;
    rb) LANG="Ruby" ;;
    *) LANG="code" ;;
esac

# Build prompt for OpenCode
PROMPT="Describe this $LANG $KIND in 1-2 sentences. Focus on what it does, not how.

$KIND: $NAME
File: $(basename "$FILE_PATH")

\`\`\`$EXT
$CODE
\`\`\`

Response format (one line only):
$NAME: <description>"

# Call OpenCode
RESULT=$(echo "$PROMPT" | timeout 30 opencode run -m "$MODEL" 2>/dev/null || true)

if [[ -z "$RESULT" ]]; then
    echo "[enrich] No result from OpenCode for $NAME" >&2
    exit 0
fi

# Clean up result - take first line, remove any markdown
DESCRIPTION=$(echo "$RESULT" | head -1 | sed 's/^[`*#]*//; s/[`*#]*$//')

# Store description directly in symbol table (no separate wisdom entry)
# Format: "Name: description" for the description column
FULL_DESCRIPTION="$DESCRIPTION"

# Use describe_symbol to store directly in symbol.description column
RESULT=$("$CHITTA_BIN" describe_symbol --symbol-id "$SYMBOL_ID" --description "$FULL_DESCRIPTION" 2>&1 || true)

if echo "$RESULT" | grep -q "Symbol description set"; then
    echo "[enrich] $NAME: $DESCRIPTION"
    # Output success marker for daemon (no MEMORY_ID needed anymore)
    echo "DESCRIBED=true"
else
    echo "[enrich] Warning: Could not set description for $NAME: $RESULT" >&2
fi
