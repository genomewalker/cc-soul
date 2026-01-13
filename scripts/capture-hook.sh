#!/bin/bash
# Smart capture hook - workflow memory with redundancy detection
#
# Features:
#   - Captures significant commands with context
#   - Detects redundant command patterns (same command + similar inputs)
#   - Links related commands in workflows
#   - Extracts input/output files for connection mapping
#
# Usage: Called by PostToolUse hook with JSON on stdin

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$SCRIPT_DIR")"

# Binaries installed to ~/.claude/bin/ by setup.sh
CHITTA_BIN="${HOME}/.claude/bin/chitta"
MIND_PATH="${HOME}/.claude/mind/chitta"
MODEL_PATH="${HOME}/.claude/bin/model.onnx"
VOCAB_PATH="${HOME}/.claude/bin/vocab.txt"

# Session-local cache for quick redundancy check
CACHE_DIR="${HOME}/.claude/mind/.cmd_cache"
mkdir -p "$CACHE_DIR"

# Check dependencies
if [[ ! -x "$CHITTA_BIN" ]]; then
    exit 0
fi

if ! command -v jq &> /dev/null; then
    exit 0
fi

# Read input from stdin
INPUT=$(cat)

# Extract fields from JSON
TOOL_NAME=$(echo "$INPUT" | jq -r '.tool_name // empty' 2>/dev/null)
TOOL_INPUT=$(echo "$INPUT" | jq -r '.tool_input // empty' 2>/dev/null)
TOOL_RESPONSE=$(echo "$INPUT" | jq -r '.tool_response // empty' 2>/dev/null)
CWD=$(echo "$INPUT" | jq -r '.cwd // empty' 2>/dev/null)
SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // empty' 2>/dev/null)

if [[ -z "$TOOL_NAME" ]]; then
    exit 0
fi

# Get project name from cwd
PROJECT=$(basename "${CWD:-$(pwd)}")

# Helper: call MCP tool
call_mcp() {
    local method="$1"
    local params="$2"
    local request="{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"$method\",\"arguments\":$params},\"id\":1}"
    printf '%s\n' "$request" | timeout 5 "$CHITTA_BIN" --path "$MIND_PATH" --model "$MODEL_PATH" --vocab "$VOCAB_PATH" 2>/dev/null | grep -v '^\[chitta' || true
}

# Helper: recall by semantic search
recall_mcp() {
    local query="$1"
    local request="{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"recall\",\"arguments\":{\"query\":\"$query\",\"limit\":3}},\"id\":1}"
    printf '%s\n' "$request" | timeout 5 "$CHITTA_BIN" --path "$MIND_PATH" --model "$MODEL_PATH" --vocab "$VOCAB_PATH" 2>/dev/null | grep -v '^\[chitta' | jq -r '.result.content[0].text // empty' 2>/dev/null || true
}

# Helper: escape for JSON
json_escape() {
    printf '%s' "$1" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

# Helper: extract file paths from command
extract_files() {
    local cmd="$1"
    # Extract paths that look like files (with extensions or known patterns)
    echo "$cmd" | grep -oE '([./~][^ ]+\.(fa|fasta|fq|fastq|bam|sam|vcf|bed|gff|gtf|txt|csv|tsv|json|yaml|yml|py|sh|r|R|nf|smk)|[./~][^ ]+/[^ ]+)' | sort -u | head -5
}

# Helper: create command signature (normalized for comparison)
cmd_signature() {
    local cmd="$1"
    # Normalize: remove specific paths, keep structure
    echo "$cmd" | sed -E 's|/[^ ]+/([^/ ]+\.[a-z]+)|FILE:\1|g' | md5sum | cut -c1-8
}

# === BASH TOOL ===
if [[ "$TOOL_NAME" == "Bash" ]]; then
    COMMAND=$(echo "$TOOL_INPUT" | jq -r '.command // empty' 2>/dev/null)
    [[ -z "$COMMAND" ]] && exit 0

    # Skip trivial commands
    if [[ "$COMMAND" =~ ^(ls|pwd|cd|echo|cat|head|tail|wc|date|which|type)([[:space:]]|$) ]]; then
        exit 0
    fi

    # Extract context
    FILES=$(extract_files "$COMMAND")
    CMD_SIG=$(cmd_signature "$COMMAND")
    CMD_BASE=$(echo "$COMMAND" | awk '{print $1}')
    CMD_SHORT=$(echo "$COMMAND" | head -c 200)
    RESULT_SHORT=$(echo "$TOOL_RESPONSE" | head -c 500)

    # Check for recent similar command (session cache)
    CACHE_FILE="$CACHE_DIR/${SESSION_ID:-default}_$CMD_SIG"
    if [[ -f "$CACHE_FILE" ]]; then
        PREV_RUN=$(cat "$CACHE_FILE")
        AGO=$(( $(date +%s) - PREV_RUN ))
        if [[ $AGO -lt 300 ]]; then  # Within 5 minutes
            echo "[cc-soul] Similar command ran ${AGO}s ago" >&2
        fi
    fi
    echo "$(date +%s)" > "$CACHE_FILE"

    # Check for failures
    IS_FAILURE=false
    if echo "$TOOL_RESPONSE" | grep -qiE 'error|failed|fatal|exception|traceback|not found|permission denied'; then
        IS_FAILURE=true
    fi

    # Build observation content with context
    CONTENT="Command: $CMD_SHORT"
    [[ -n "$FILES" ]] && CONTENT="$CONTENT\n\nFiles: $FILES"
    [[ -n "$RESULT_SHORT" ]] && CONTENT="$CONTENT\n\nResult: $RESULT_SHORT"

    # Determine category
    CATEGORY="discovery"
    if [[ "$IS_FAILURE" == "true" ]]; then
        CATEGORY="signal"
        TITLE="Failed: $CMD_BASE"
    elif [[ "$COMMAND" =~ git[[:space:]]+commit ]]; then
        CATEGORY="feature"
        TITLE="Commit"
    else
        TITLE="Ran: $CMD_BASE"
    fi

    # Build tags
    TAGS="auto:cmd,cmd:$CMD_BASE,project:$PROJECT"
    [[ "$IS_FAILURE" == "true" ]] && TAGS="$TAGS,auto:failure"

    # Semantic search for related past work
    RELATED=$(recall_mcp "$CMD_BASE $FILES" 2>/dev/null | head -c 200)
    if [[ -n "$RELATED" && "$RELATED" != "Found 0 results:" ]]; then
        CONTENT="$CONTENT\n\nRelated: $RELATED"
    fi

    # Store observation
    call_mcp "observe" "{\"category\":\"$CATEGORY\",\"title\":\"$(json_escape "$TITLE")\",\"content\":\"$(json_escape "$CONTENT")\",\"tags\":\"$TAGS\"}" >/dev/null

    if [[ "$IS_FAILURE" == "true" ]]; then
        echo "[cc-soul] Captured failure: $CMD_BASE" >&2
    else
        echo "[cc-soul] Captured: $CMD_BASE" >&2
    fi
fi

# === WRITE TOOL ===
if [[ "$TOOL_NAME" == "Write" ]]; then
    FILE_PATH=$(echo "$TOOL_INPUT" | jq -r '.file_path // empty' 2>/dev/null)
    if [[ -n "$FILE_PATH" ]]; then
        FILENAME=$(basename "$FILE_PATH")
        # Skip temp/cache files
        if [[ ! "$FILE_PATH" =~ __pycache__|\.pyc|node_modules|\.git/|/tmp/|\.cache ]]; then
            TITLE="Created: $FILENAME"
            CONTENT="File: $FILE_PATH\nProject: $PROJECT"
            call_mcp "observe" "{\"category\":\"feature\",\"title\":\"$(json_escape "$TITLE")\",\"content\":\"$(json_escape "$CONTENT")\",\"tags\":\"auto:file,file:$FILENAME,project:$PROJECT\"}" >/dev/null
            echo "[cc-soul] Captured: $FILENAME" >&2
        fi
    fi
fi

# === EDIT TOOL ===
if [[ "$TOOL_NAME" == "Edit" ]]; then
    FILE_PATH=$(echo "$TOOL_INPUT" | jq -r '.file_path // empty' 2>/dev/null)
    if [[ -n "$FILE_PATH" ]]; then
        FILENAME=$(basename "$FILE_PATH")
        # Skip temp/cache files
        if [[ ! "$FILE_PATH" =~ __pycache__|\.pyc|node_modules|\.git/|/tmp/|\.cache ]]; then
            OLD=$(echo "$TOOL_INPUT" | jq -r '.old_string // empty' 2>/dev/null | head -c 100)
            NEW=$(echo "$TOOL_INPUT" | jq -r '.new_string // empty' 2>/dev/null | head -c 100)
            TITLE="Edited: $FILENAME"
            CONTENT="File: $FILE_PATH\n-: $OLD\n+: $NEW"
            call_mcp "observe" "{\"category\":\"refactor\",\"title\":\"$(json_escape "$TITLE")\",\"content\":\"$(json_escape "$CONTENT")\",\"tags\":\"auto:edit,file:$FILENAME,project:$PROJECT\"}" >/dev/null
            echo "[cc-soul] Captured edit: $FILENAME" >&2
        fi
    fi
fi

# Cleanup old cache files (older than 1 hour)
find "$CACHE_DIR" -type f -mmin +60 -delete 2>/dev/null || true

exit 0
