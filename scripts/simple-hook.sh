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
CHITTAD_BIN="${CHITTAD_BIN:-$HOME/.claude/bin/chittad}"

# Ensure daemon is running
ensure_daemon() {
    if [[ -S "$SOCKET" ]]; then
        return 0  # Already running
    fi

    if [[ ! -x "$CHITTAD_BIN" ]]; then
        echo "[simple-hook] chittad not found at $CHITTAD_BIN" >&2
        return 1
    fi

    # Start daemon in background
    "$CHITTAD_BIN" daemon &
    disown

    # Wait for socket (max 5 seconds)
    local waited=0
    while [[ ! -S "$SOCKET" && $waited -lt 50 ]]; do
        sleep 0.1
        ((waited++))
    done

    if [[ -S "$SOCKET" ]]; then
        echo "[simple-hook] Daemon started"
        return 0
    else
        echo "[simple-hook] Daemon failed to start" >&2
        return 1
    fi
}

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
        # Read stdin for session data (contains transcript_path)
        STDIN_DATA=$(cat)

        # Ensure daemon is running before any RPC calls
        ensure_daemon || exit 0  # Soft fail - don't block session

        # Detect current realm/project
        CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
        if [[ -x "$CHITTA_BIN" ]]; then
            REALM=$("$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")
        else
            REALM="brahman"
        fi

        # Auto-register transcript for distillation
        # Get transcript_path from stdin (Claude Code provides this)
        TRANSCRIPT=$(echo "$STDIN_DATA" | jq -r '.transcript_path // empty' 2>/dev/null)

        if [[ -n "$TRANSCRIPT" && -f "$TRANSCRIPT" ]]; then
            SESSION_ID=$(basename "$TRANSCRIPT" .jsonl)
            escaped_session=$(json_escape "$SESSION_ID")
            escaped_path=$(json_escape "$TRANSCRIPT")
            escaped_realm=$(json_escape "$REALM")
            rpc_call "transcript_register" "{\"session_id\":\"$escaped_session\",\"transcript_path\":\"$escaped_path\",\"realm\":\"$escaped_realm\"}" >/dev/null 2>&1 || true
        fi

        # Get soul context - compact format
        response=$(rpc_call "soul_context" '{}')
        # Extract just key metrics from structured response
        structured=$(echo "$response" | jq -r '.result.structured // empty' 2>/dev/null)
        if [[ -n "$structured" ]]; then
            nodes=$(echo "$structured" | jq -r '.total_nodes // 0')
            triplets=$(echo "$structured" | jq -r '.triplet_count // 0')
            conf=$(echo "$structured" | jq -r '.avg_confidence // 0' | cut -c1-4)
            status=$(echo "$structured" | jq -r '.status // "unknown"')
            echo "[soul] n=$nodes t=$triplets c=$conf $status"
        fi

        # Load ledger checkpoint for current realm
        escaped_realm=$(json_escape "$REALM")
        response=$(rpc_call "ledger_load" "{\"project\":\"$escaped_realm\"}")
        ledger_text=$(extract_text "$response")

        # Check if checkpoint was found (structured response has "found" field)
        ledger_json=$(echo "$response" | jq -r '.result.structured // empty' 2>/dev/null)
        ledger_found=$(echo "$ledger_json" | jq -r '.found // false' 2>/dev/null)

        if [[ "$ledger_found" == "true" ]]; then
            # Ultra-compact: just next step and pending count
            todos=$(echo "$ledger_json" | jq -r '.todos // []' 2>/dev/null)
            pending=$(echo "$todos" | jq '[.[] | select(.status != "completed")] | length' 2>/dev/null || echo "0")
            next=$(echo "$ledger_json" | jq -r '.next_steps[0] // ""' 2>/dev/null)

            output=""
            [[ "$pending" -gt 0 ]] && output="[$pending pending]"
            [[ -n "$next" && "$next" != "null" ]] && output="$output next: ${next:0:80}"
            [[ -n "$output" ]] && echo "$output"
        fi

        ;;

    prompt|UserPromptSubmit)
        # Ensure daemon is running
        ensure_daemon || exit 0

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

        # Get relevant memories - minimal k to save tokens
        response=$(rpc_call "full_resonate" "{\"query\":\"$ESCAPED_QUERY\",\"k\":3,\"realm\":\"$ESCAPED_REALM\",\"include_global\":true}")
        memories=$(extract_text "$response")

        if [[ -n "$memories" && "$memories" != *"No memories"* ]]; then
            # Ultra-compact: just content, no headers, max 2 lines (~100 chars each)
            compact=$(echo "$memories" | grep -E '^\[[0-9]+%\]' | head -2 | \
                sed 's/^\[[0-9]*%\] \[[^]]*\] //' | \
                cut -c1-100 | \
                sed 's/$/.../')
            if [[ -n "$compact" ]]; then
                echo "$compact"
            fi
        fi
        ;;

    stop|Stop)
        # Ensure daemon is running
        ensure_daemon || exit 0

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
        # Ensure daemon is running
        ensure_daemon || exit 0

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
        echo "[checkpoint] $REALM"
        ;;

    *)
        echo "Usage: simple-hook.sh <start|prompt|stop|pre-compact>" >&2
        exit 1
        ;;
esac
