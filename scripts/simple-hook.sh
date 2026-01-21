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
        # Detect current realm/project
        CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
        if [[ -x "$CHITTA_BIN" ]]; then
            REALM=$("$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")
        else
            REALM="brahman"
        fi

        # Get soul context
        response=$(rpc_call "soul_context" '{}')
        context=$(extract_text "$response")

        if [[ -n "$context" ]]; then
            echo "$context"
        fi

        # Load ledger checkpoint for current realm
        escaped_realm=$(json_escape "$REALM")
        response=$(rpc_call "ledger_load" "{\"project\":\"$escaped_realm\"}")
        ledger_text=$(extract_text "$response")

        # Check if checkpoint was found (structured response has "found" field)
        ledger_json=$(echo "$response" | jq -r '.result.structured // empty' 2>/dev/null)
        ledger_found=$(echo "$ledger_json" | jq -r '.found // false' 2>/dev/null)

        if [[ "$ledger_found" == "true" ]]; then
            mood=$(echo "$ledger_json" | jq -r '.mood // ""' 2>/dev/null)
            snapshot=$(echo "$ledger_json" | jq -r '.snapshot // ""' 2>/dev/null)

            # Count pending todos
            todos=$(echo "$ledger_json" | jq -r '.todos // []' 2>/dev/null)
            pending_count=$(echo "$todos" | jq '[.[] | select(.status != "completed" and .status != "done")] | length' 2>/dev/null || echo "0")

            # Get first next step
            next_step=$(echo "$ledger_json" | jq -r '.next_steps[0] // ""' 2>/dev/null)

            echo ""
            echo "[I remember working on: $REALM]"
            [[ -n "$snapshot" && "$snapshot" != "null" ]] && echo "  $snapshot"
            [[ "$pending_count" -gt 0 ]] && echo "  ($pending_count pending tasks)"
            [[ -n "$next_step" && "$next_step" != "null" ]] && echo "  Next: $next_step"
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

        # Detect current realm/project for scoped recall
        CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
        if [[ -x "$CHITTA_BIN" ]]; then
            REALM=$("$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")
        else
            REALM="brahman"
        fi

        # Escape query for JSON
        ESCAPED_QUERY=$(json_escape "$QUERY")
        ESCAPED_REALM=$(json_escape "$REALM")

        # Get relevant memories filtered by realm (includes global memories)
        response=$(rpc_call "full_resonate" "{\"query\":\"$ESCAPED_QUERY\",\"k\":5,\"realm\":\"$ESCAPED_REALM\",\"include_global\":true}")
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
            # Detect realm for project scoping
            CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
            if [[ -x "$CHITTA_BIN" ]]; then
                REALM=$("$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")
            else
                REALM="brahman"
            fi

            # Generate session ID from timestamp
            SESSION_ID="auto-$(date +%Y%m%d-%H%M%S)"

            # Extract file paths (common extensions)
            files_json=$(echo "$RESPONSE" | grep -oE '[a-zA-Z0-9_/-]+\.(py|js|ts|tsx|cpp|hpp|c|h|rs|go|sh|md|json|yaml|yml|toml|sql|html|css|scss)' | sort -u | head -10 | jq -R . | jq -s '.')

            # Extract decisions (patterns: "chose X", "decided to", "using X instead", "went with")
            decisions_json=$(echo "$RESPONSE" | grep -iE '(chose|decided|using .* instead|went with|picked|selected|opting for)' | head -5 | sed 's/^[[:space:]]*//' | jq -R . | jq -s '.')

            # Extract next steps (patterns: "next:", "todo:", "- [ ]", numbered items after "next")
            next_steps_json=$(echo "$RESPONSE" | grep -iE '(^[0-9]+\.|^- \[.\]|next:|todo:|should .* next|will .* next|need to)' | head -5 | sed 's/^[[:space:]]*//' | jq -R . | jq -s '.')

            # Extract discoveries ([LEARN] content without the tag)
            discoveries_json=$(echo "$RESPONSE" | grep -E '^\[LEARN\]' | sed 's/^\[LEARN\][[:space:]]*//' | head -5 | jq -R . | jq -s '.')

            # Detect mood from keywords
            if echo "$RESPONSE" | grep -qiE '(error|failed|bug|issue|problem|stuck)'; then
                mood="debugging"
            elif echo "$RESPONSE" | grep -qiE '(complete|done|finished|working|success|passed)'; then
                mood="confident"
            elif echo "$RESPONSE" | grep -qiE '(trying|attempting|testing|investigating)'; then
                mood="exploring"
            else
                mood="working"
            fi

            # Extract topic from first meaningful line
            topic=$(echo "$RESPONSE" | grep -v '^$' | head -1 | head -c 150)
            if [[ "$topic" == *"[LEARN]"* ]]; then
                topic="${topic#*\[LEARN\] }"
            fi

            escaped_topic=$(json_escape "$topic")
            escaped_realm=$(json_escape "$REALM")

            # Ensure JSON arrays have defaults
            [[ -z "$files_json" || "$files_json" == "null" ]] && files_json='[]'
            [[ -z "$decisions_json" || "$decisions_json" == "null" ]] && decisions_json='[]'
            [[ -z "$next_steps_json" || "$next_steps_json" == "null" ]] && next_steps_json='[]'
            [[ -z "$discoveries_json" || "$discoveries_json" == "null" ]] && discoveries_json='[]'

            # Use CLI for reliable checkpoint
            if "$CHITTA_BIN" ledger_save \
                --session_id "$SESSION_ID" \
                --project "$REALM" \
                --mood "$mood" \
                --active_files "$files_json" \
                --decisions "$decisions_json" \
                --next_steps "$next_steps_json" \
                --discoveries "$discoveries_json" \
                --snapshot "$topic" >/dev/null 2>&1; then
                echo "[cc-soul] Checkpoint: $SESSION_ID ($mood)"
            fi
        fi
        ;;

    pre-compact|PreCompact)
        # Detect realm for project scoping
        CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
        if [[ -x "$CHITTA_BIN" ]]; then
            REALM=$("$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")
        else
            REALM="brahman"
        fi

        SESSION_ID="pre-compact-$(date +%Y%m%d-%H%M%S)"

        # Save checkpoint before context is compacted using CLI
        "$CHITTA_BIN" ledger_save \
            --session_id "$SESSION_ID" \
            --project "$REALM" \
            --mood "pre-compact" \
            --next_steps '["Review previous work","Continue from checkpoint"]' \
            --snapshot "Context compacted - review ledger for continuation" >/dev/null 2>&1 || true
        echo "[checkpoint] cc-soul v3.1.0 DuckDB $REALM"
        ;;

    *)
        echo "Usage: simple-hook.sh <start|prompt|stop|pre-compact>" >&2
        exit 1
        ;;
esac
