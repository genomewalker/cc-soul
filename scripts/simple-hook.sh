#!/bin/bash
# Simple unified hook for cc-soul with SimpleMind
#
# Usage: simple-hook.sh <hook-type> [options]
#   hook-type: start, prompt, stop, pre-compact
#
# Minimal design:
#   - SessionStart: inject soul_context + continuation
#   - UserPromptSubmit: inject continuation + relevant memories
#   - Stop: extract [LEARN] → observe, detect meaningful work → checkpoint
#   - PreCompact: save checkpoint before context loss

set -e

HOOK_TYPE="${1:-}"
shift || true

# Config
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-5}"

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

# RPC call helper
rpc_call() {
    local tool="$1"
    local args="$2"
    local request="{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"$tool\",\"arguments\":$args}}"

    if [[ ! -S "$SOCKET" ]]; then
        echo "[simple-hook] Daemon not running" >&2
        return 1
    fi

    echo "$request" | timeout "$MAX_WAIT" nc -U "$SOCKET" 2>/dev/null | head -1
}

# Extract text from RPC response
extract_text() {
    local response="$1"
    echo "$response" | jq -r '.result.content[0].text // empty' 2>/dev/null
}

# Check if response contains meaningful work
is_meaningful_work() {
    local text="$1"

    # Contains [LEARN] or [REMEMBER]
    if echo "$text" | grep -qE '\[(LEARN|REMEMBER)\]'; then
        return 0
    fi

    # Contains file path and change verb
    if echo "$text" | grep -qE '\.(py|js|ts|cpp|hpp|rs|go|sh|md|json|yaml)' && \
       echo "$text" | grep -qiE '(created|updated|added|removed|fixed|refactored|implemented|changed)'; then
        return 0
    fi

    # Contains "next steps" or explicit planning
    if echo "$text" | grep -qiE '(next steps|next:|todo:|plan:)'; then
        return 0
    fi

    return 1
}

# Extract [LEARN] blocks
extract_learns() {
    local text="$1"
    echo "$text" | grep -E '^\[LEARN\]' || true
}

# Extract [TRIPLET] lines
extract_triplets() {
    local text="$1"
    echo "$text" | grep -E '^\[TRIPLET\]' || true
}

# JSON escape
json_escape() {
    local text="$1"
    echo -n "$text" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

case "$HOOK_TYPE" in
    start|SessionStart)
        # Get soul context
        response=$(rpc_call "soul_context" '{}')
        context=$(extract_text "$response")

        if [[ -n "$context" ]]; then
            echo "Soul State:"
            echo "$context"
        fi

        # Get continuation node
        response=$(rpc_call "full_resonate" '{"query":"continuity:current","k":1}')
        continuation=$(extract_text "$response")

        if [[ -n "$continuation" && "$continuation" != *"No memories"* ]]; then
            echo ""
            echo "[Continuation]"
            echo "$continuation"
        fi
        ;;

    prompt|UserPromptSubmit)
        # Get query from stdin or argument
        QUERY="${1:-}"
        if [[ -z "$QUERY" ]]; then
            QUERY=$(cat)
        fi

        if [[ -z "$QUERY" ]]; then
            exit 0
        fi

        # Escape query for JSON
        ESCAPED_QUERY=$(json_escape "$QUERY")

        # Get relevant memories
        response=$(rpc_call "full_resonate" "{\"query\":\"$ESCAPED_QUERY\",\"k\":5}")
        memories=$(extract_text "$response")

        if [[ -n "$memories" && "$memories" != *"No memories"* ]]; then
            echo "[I know]"
            echo "$memories"
        fi
        ;;

    stop|Stop)
        # Get response from stdin
        RESPONSE=$(cat)

        if [[ -z "$RESPONSE" ]]; then
            exit 0
        fi

        # Extract and store [LEARN] blocks
        while IFS= read -r learn_line; do
            if [[ -n "$learn_line" ]]; then
                # Remove [LEARN] prefix
                content="${learn_line#\[LEARN\] }"
                title=$(echo "$content" | head -c 100)
                escaped_content=$(json_escape "$content")
                escaped_title=$(json_escape "$title")

                rpc_call "observe" "{\"category\":\"wisdom\",\"title\":\"$escaped_title\",\"content\":\"$escaped_content\"}" >/dev/null 2>&1 || true
                echo "[cc-soul] Learned: $title"
            fi
        done <<< "$(extract_learns "$RESPONSE")"

        # Extract and store [TRIPLET] lines
        while IFS= read -r triplet_line; do
            if [[ -n "$triplet_line" ]]; then
                # Parse: [TRIPLET] subject predicate object
                triplet="${triplet_line#\[TRIPLET\] }"
                subject=$(echo "$triplet" | awk '{print $1}')
                predicate=$(echo "$triplet" | awk '{print $2}')
                object=$(echo "$triplet" | awk '{print $3}')

                if [[ -n "$subject" && -n "$predicate" && -n "$object" ]]; then
                    rpc_call "connect" "{\"subject\":\"$subject\",\"predicate\":\"$predicate\",\"object\":\"$object\"}" >/dev/null 2>&1 || true
                fi
            fi
        done <<< "$(extract_triplets "$RESPONSE")"

        # Auto-checkpoint if meaningful work detected
        if is_meaningful_work "$RESPONSE"; then
            # Extract topic from first line or [LEARN]
            topic=$(echo "$RESPONSE" | head -1 | head -c 100)
            if [[ "$topic" == *"[LEARN]"* ]]; then
                topic="${topic#*\[LEARN\] }"
            fi

            escaped_topic=$(json_escape "$topic")
            rpc_call "checkpoint" "{\"topic\":\"$escaped_topic\",\"state\":\"Work in progress\",\"decisions\":[],\"next\":[]}" >/dev/null 2>&1 || true
        fi
        ;;

    pre-compact|PreCompact)
        # Save checkpoint before context is compacted
        rpc_call "checkpoint" "{\"topic\":\"Context compacted\",\"state\":\"Resuming from compact\",\"decisions\":[],\"next\":[\"Review continuation node for context\"]}" >/dev/null 2>&1 || true
        echo "[cc-soul] Checkpoint saved before compact"
        ;;

    *)
        echo "Usage: simple-hook.sh <start|prompt|stop|pre-compact>" >&2
        exit 1
        ;;
esac
