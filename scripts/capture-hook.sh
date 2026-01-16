#!/bin/bash
# DISABLED: Automatic capture creates noise, not wisdom
#
# The soul learns from Claude's [LEARN] and [REMEMBER] markers in responses,
# not from automatic command logging. All text that enters the mind should
# be processed by Claude first.
#
# To store learnings, Claude writes in responses:
#   [LEARN] pattern→insight
#   [REMEMBER] decision or fact
#
# These are extracted by stop-hook.sh
#
# This hook now only passes through - no automatic storage.

exit 0

# === LEGACY CODE BELOW (disabled) ===

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$SCRIPT_DIR")"

# Binaries installed to ~/.claude/bin/ by setup.sh
CHITTA_BIN="${HOME}/.claude/bin/chitta"
MIND_PATH="${HOME}/.claude/mind/chitta"
CAPTURE_WARNED=false
TIMEOUT_CMD=()
TIMEOUT_WARNED=false
MAX_WAIT="${CC_SOUL_MAX_WAIT:-5}"

if [[ "$MAX_WAIT" != "0" ]] && command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD=(timeout "$MAX_WAIT")
fi

run_with_timeout() {
    if [[ "$MAX_WAIT" != "0" && ${#TIMEOUT_CMD[@]} -eq 0 && "$TIMEOUT_WARNED" != "true" ]]; then
        echo "[cc-soul] timeout not available; running without limit" >&2
        TIMEOUT_WARNED=true
    fi

    if [[ ${#TIMEOUT_CMD[@]} -gt 0 ]]; then
        "${TIMEOUT_CMD[@]}" "$@"
    else
        "$@"
    fi
}

CHITTA_ARGS=()

_djb2_hash() {
    local str="$1"
    local hash=5381
    local i c
    for ((i=0; i<${#str}; i++)); do
        c=$(printf '%d' "'${str:$i:1}")
        hash=$(( ((hash << 5) + hash) + c ))
        hash=$((hash & 0xFFFFFFFF))
    done
    echo "$hash"
}

init_chitta_args() {
    local help_output
    help_output=$(run_with_timeout "$CHITTA_BIN" --help 2>/dev/null || true)

    if [[ -z "$help_output" ]]; then
        return
    fi

    if echo "$help_output" | grep -q -- "--socket-path"; then
        local mind_hash
        mind_hash=$(_djb2_hash "$MIND_PATH")
        local socket_path="/tmp/chitta-${mind_hash}.sock"
        if [[ -S "$socket_path" ]]; then
            CHITTA_ARGS+=("--socket-path" "$socket_path")
        fi
    fi
}

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

init_chitta_args

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
    local raw_response
    if ! raw_response=$(printf '%s\n' "$request" | run_with_timeout "$CHITTA_BIN" "${CHITTA_ARGS[@]}" 2>/dev/null); then
        if [[ "$CAPTURE_WARNED" != "true" ]]; then
            echo "[cc-soul] Capture failed: chitta not responding" >&2
            CAPTURE_WARNED=true
        fi
        return 1
    fi
    if [[ -z "$raw_response" ]]; then
        if [[ "$CAPTURE_WARNED" != "true" ]]; then
            echo "[cc-soul] Capture failed: chitta not responding" >&2
            CAPTURE_WARNED=true
        fi
        return 1
    fi
    echo "$raw_response" | grep -v '^\[chitta' || true
}

# Helper: recall by semantic search
recall_mcp() {
    local query="$1"
    local request="{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"recall\",\"arguments\":{\"query\":\"$query\",\"limit\":3}},\"id\":1}"
    local raw_response
    if ! raw_response=$(printf '%s\n' "$request" | run_with_timeout "$CHITTA_BIN" "${CHITTA_ARGS[@]}" 2>/dev/null); then
        if [[ "$CAPTURE_WARNED" != "true" ]]; then
            echo "[cc-soul] Capture failed: chitta not responding" >&2
            CAPTURE_WARNED=true
        fi
        return 1
    fi
    if [[ -z "$raw_response" ]]; then
        if [[ "$CAPTURE_WARNED" != "true" ]]; then
            echo "[cc-soul] Capture failed: chitta not responding" >&2
            CAPTURE_WARNED=true
        fi
        return 1
    fi
    echo "$raw_response" | grep -v '^\[chitta' | jq -r '.result.content[0].text // empty' 2>/dev/null || true
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
    if [[ "$COMMAND" =~ ^(ls|pwd|cd|echo|cat|head|tail|wc|date|which|type|sleep|true|false)([[:space:]]|$) ]]; then
        exit 0
    fi

    # Skip chitta/soul commands - these are tool invocations, not learnings
    # The LEARNING is extracted from Claude's response by stop-hook.sh
    if [[ "$COMMAND" =~ chitta|chittad|soul-hook ]]; then
        exit 0
    fi

    # Skip internal operations (pkill, rm sockets, etc.)
    if [[ "$COMMAND" =~ ^(pkill|rm -f /tmp/chitta|kill) ]]; then
        exit 0
    fi

    # Extract context
    FILES=$(extract_files "$COMMAND")
    CMD_SIG=$(cmd_signature "$COMMAND")
    CMD_BASE=$(echo "$COMMAND" | awk '{print $1}' | sed 's|.*/||')  # basename
    CMD_SHORT=$(echo "$COMMAND" | head -c 200)
    RESULT_SHORT=$(echo "$TOOL_RESPONSE" | head -c 300)

    # Check for recent similar command (skip if duplicate within 5 min)
    CACHE_FILE="$CACHE_DIR/${SESSION_ID:-default}_$CMD_SIG"
    if [[ -f "$CACHE_FILE" ]]; then
        PREV_RUN=$(cat "$CACHE_FILE")
        AGO=$(( $(date +%s) - PREV_RUN ))
        if [[ $AGO -lt 300 ]]; then
            # Skip duplicate - don't pollute memory with repeated commands
            exit 0
        fi
    fi
    echo "$(date +%s)" > "$CACHE_FILE"

    # Check for failures - these ARE worth capturing
    IS_FAILURE=false
    if echo "$TOOL_RESPONSE" | grep -qiE 'error|failed|fatal|exception|traceback|not found|permission denied'; then
        IS_FAILURE=true
    fi

    # Only capture: failures OR significant commands (git, make, npm, docker, etc.)
    SIGNIFICANT=false
    if [[ "$IS_FAILURE" == "true" ]]; then
        SIGNIFICANT=true
    elif [[ "$COMMAND" =~ ^(git|make|cmake|npm|yarn|pip|cargo|docker|kubectl) ]]; then
        SIGNIFICANT=true
    fi

    # Skip non-significant, non-failure commands
    if [[ "$SIGNIFICANT" != "true" ]]; then
        exit 0
    fi

    # Build SSL-style content (extracting the PATTERN, not verbatim)
    if [[ "$IS_FAILURE" == "true" ]]; then
        # For failures: capture what went wrong and how to fix
        TITLE="[failure] $CMD_BASE"
        ERROR_MSG=$(echo "$TOOL_RESPONSE" | grep -iE 'error|failed|fatal' | head -1 | head -c 100)
        CONTENT="[$PROJECT] $CMD_BASE→failed
[ε] $ERROR_MSG
Command: $CMD_SHORT"
        CATEGORY="signal"
        TAGS="auto:failure,cmd:$CMD_BASE,project:$PROJECT"
    else
        # For significant commands: extract usage pattern
        TITLE="[$PROJECT] $CMD_BASE usage"
        # Extract flags/subcommands used
        FLAGS=$(echo "$COMMAND" | grep -oE ' --?[a-zA-Z0-9-]+' | tr '\n' ' ' | head -c 50)
        CONTENT="[$PROJECT] $CMD_BASE$FLAGS→success
[ε] Usage pattern for $CMD_BASE in this project."
        CATEGORY="discovery"
        TAGS="cmd:$CMD_BASE,project:$PROJECT"
    fi

    # Store observation
    call_mcp "observe" "{\"category\":\"$CATEGORY\",\"title\":\"$(json_escape "$TITLE")\",\"content\":\"$(json_escape "$CONTENT")\",\"tags\":\"$TAGS\"}" >/dev/null

    if [[ "$IS_FAILURE" == "true" ]]; then
        echo "[cc-soul] Captured failure: $CMD_BASE" >&2
    else
        echo "[cc-soul] Captured pattern: $CMD_BASE" >&2
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
