#!/bin/bash
# Simple unified hook for cc-soul with SimpleMind
#
# Usage: simple-hook.sh <hook-type> [options]
#   hook-type: start, prompt, stop, pre-compact
#
# Minimal design:
#   - SessionStart: inject soul_context + continuation
#   - UserPromptSubmit: inject continuation + relevant memories
#   - Stop: detect meaningful work → checkpoint (memories via MCP)
#   - PreCompact: save checkpoint before context loss

set -e

HOOK_TYPE="${1:-}"
shift || true

# Config
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-5}"
DEBUG_SOUL="${DEBUG_SOUL:-0}"

# Debug helper - outputs to stderr only when DEBUG_SOUL=1
debug() {
    [[ "$DEBUG_SOUL" == "1" ]] && echo "[DEBUG] $*" >&2
}

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

        # Incremental code re-indexing (only changed files)
        if [[ -d ".git" ]]; then
            PROJECT_NAME=$(basename "$PWD")
            escaped_path=$(json_escape "$PWD")
            escaped_project=$(json_escape "$PROJECT_NAME")
            index_response=$(rpc_call "learn_codebase" "{\"path\":\"$escaped_path\",\"project\":\"$escaped_project\"}" 2>/dev/null)
            files_processed=$(echo "$index_response" | jq -r '.result.structured.files_processed // 0' 2>/dev/null)
            if [[ "$files_processed" -gt 0 ]]; then
                echo "[code] indexed $files_processed changed files"
            fi
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

        # Reset turn count for new session (session momentum)
        echo "0" > "$MIND_PATH/.turn_count"

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

        # User profile: surface partner understanding
        profile_response=$(rpc_call "profile_get" '{"user_id":"default"}')
        profile_found=$(echo "$profile_response" | jq -r '.result.structured.found // false' 2>/dev/null)
        if [[ "$profile_found" == "true" ]]; then
            expertise=$(echo "$profile_response" | jq -r '.result.structured.expertise // "[]"' 2>/dev/null)
            prefs=$(echo "$profile_response" | jq -r '.result.structured.preferences // "{}"' 2>/dev/null)
            style=$(echo "$profile_response" | jq -r '.result.structured.style // "{}"' 2>/dev/null)

            # Compact display: domains and key preferences
            domains=$(echo "$expertise" | jq -r '[.[]?.domain] | join(", ")' 2>/dev/null)
            [[ -n "$domains" && "$domains" != "null" ]] && echo "[expertise] $domains"

            # Key preferences as flags
            pref_flags=""
            [[ $(echo "$prefs" | jq -r '.no_emojis // false') == "true" ]] && pref_flags="${pref_flags}no-emoji "
            [[ $(echo "$prefs" | jq -r '.prefer_examples // false') == "true" ]] && pref_flags="${pref_flags}examples "
            [[ $(echo "$style" | jq -r '.verbosity // ""') == "concise" ]] && pref_flags="${pref_flags}concise "
            [[ -n "$pref_flags" ]] && echo "[style] $pref_flags"
        fi

        # Active goals: surface long-term objectives
        goals_response=$(rpc_call "goal_list" '{"status":"active","limit":3}')
        goals_count=$(echo "$goals_response" | jq -r '.result.structured.count // 0' 2>/dev/null)
        if [[ "$goals_count" -gt 0 ]]; then
            goals_summary=$(echo "$goals_response" | jq -r '.result.structured.goals[] | "#\(.id) [\(.progress * 100 | floor)%] \(.title)"' 2>/dev/null | head -3 | tr '\n' '; ')
            echo "[goals] $goals_summary"
        fi

        # Behavioral layer: inject learned corrections and preferences
        # This replaces growing CLAUDE.md - behaviors live in soul memory
        behavior_response=$(rpc_call "recall" '{"query":"behavior correction preference rule","tag":"correction","limit":3}')
        behavior_text=$(extract_text "$behavior_response")
        if [[ -n "$behavior_text" && "$behavior_text" != *"No memories"* ]]; then
            # Extract just the correction content, ultra-compact
            corrections=$(echo "$behavior_text" | grep -oP 'CORRECT: \K[^\n]+' | head -3 | tr '\n' '; ')
            [[ -n "$corrections" ]] && echo "[rules] $corrections"
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
            mood=$(echo "$ledger_json" | jq -r '.mood // ""' 2>/dev/null)
            snapshot=$(echo "$ledger_json" | jq -r '.snapshot // ""' 2>/dev/null)

            output=""
            [[ "$pending" -gt 0 ]] && output="[$pending pending]"
            [[ -n "$next" && "$next" != "null" ]] && output="$output next: ${next:0:80}"
            [[ -n "$output" ]] && echo "$output"

            # Anticipation: surface last session context
            if [[ -n "$snapshot" && "$snapshot" != "null" ]]; then
                echo "[last session] $mood: ${snapshot:0:100}"
            fi
        fi

        # Anticipation: surface relevant preferences for this project
        project_name=$(basename "$(pwd)")
        escaped_project=$(json_escape "$project_name")
        pref_response=$(rpc_call "recall" "{\"query\":\"$escaped_project preferences workflow\",\"tag\":\"preference\",\"limit\":1}")
        pref_text=$(extract_text "$pref_response")
        if [[ -n "$pref_text" && "$pref_text" != *"No memories"* ]]; then
            pref_compact=$(echo "$pref_text" | grep -E '^\[[0-9]+%\]' | head -1 | \
                sed 's/^\[[0-9]*%\] \[[^]]*\] //' | cut -c1-80)
            [[ -n "$pref_compact" ]] && echo "[preference] $pref_compact"
        fi

        # Code diff awareness: detect changes since last session
        if git rev-parse --git-dir >/dev/null 2>&1; then
            LAST_COMMIT_FILE="$MIND_PATH/.last_commit_$(basename "$(pwd)")"
            CURRENT_COMMIT=$(git rev-parse HEAD 2>/dev/null)

            if [[ -f "$LAST_COMMIT_FILE" ]]; then
                LAST_COMMIT=$(cat "$LAST_COMMIT_FILE" 2>/dev/null)
                if [[ "$LAST_COMMIT" != "$CURRENT_COMMIT" && -n "$LAST_COMMIT" ]]; then
                    # Count changes since last session
                    CHANGED_FILES=$(git diff --name-only "$LAST_COMMIT" HEAD 2>/dev/null | wc -l)
                    NEW_COMMITS=$(git rev-list "$LAST_COMMIT"..HEAD 2>/dev/null | wc -l)

                    if [[ "$CHANGED_FILES" -gt 0 || "$NEW_COMMITS" -gt 0 ]]; then
                        # Get short summary of top changed files
                        TOP_FILES=$(git diff --name-only "$LAST_COMMIT" HEAD 2>/dev/null | head -3 | tr '\n' ', ' | sed 's/,$//')
                        echo "[changes] ${NEW_COMMITS} commits, ${CHANGED_FILES} files: ${TOP_FILES:0:60}"
                    fi
                fi
            fi

            # Update last commit marker
            mkdir -p "$MIND_PATH"
            echo "$CURRENT_COMMIT" > "$LAST_COMMIT_FILE"
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

        output=""

        # Context recovery: detect /clear or continuation patterns and re-inject full context
        # This handles the case where /clear was used but SessionStart doesn't re-run
        if echo "$QUERY" | grep -qiE '^(continue|resume|pick up|where were we|what were we|let.?s continue)|(/clear|after clear)'; then
            # Re-inject soul context like SessionStart
            response=$(rpc_call "soul_context" '{}')
            structured=$(echo "$response" | jq -r '.result.structured // empty' 2>/dev/null)
            if [[ -n "$structured" ]]; then
                nodes=$(echo "$structured" | jq -r '.total_nodes // 0')
                triplets=$(echo "$structured" | jq -r '.triplet_count // 0')
                conf=$(echo "$structured" | jq -r '.avg_confidence // 0' | cut -c1-4)
                status=$(echo "$structured" | jq -r '.status // "unknown"')
                output="[soul] n=$nodes t=$triplets c=$conf $status"
            fi

            # Load ledger checkpoint
            ledger_response=$(rpc_call "ledger_load" "{\"project\":\"$ESCAPED_REALM\"}")
            ledger_json=$(echo "$ledger_response" | jq -r '.result.structured // empty' 2>/dev/null)
            ledger_found=$(echo "$ledger_json" | jq -r '.found // false' 2>/dev/null)

            if [[ "$ledger_found" == "true" ]]; then
                todos=$(echo "$ledger_json" | jq -r '.todos // []' 2>/dev/null)
                pending=$(echo "$todos" | jq '[.[] | select(.status != "completed")] | length' 2>/dev/null || echo "0")
                next=$(echo "$ledger_json" | jq -r '.next_steps[0] // ""' 2>/dev/null)
                snapshot=$(echo "$ledger_json" | jq -r '.snapshot // ""' 2>/dev/null)
                mood=$(echo "$ledger_json" | jq -r '.mood // ""' 2>/dev/null)

                ledger_out=""
                [[ "$pending" -gt 0 ]] && ledger_out="[$pending pending]"
                [[ -n "$next" && "$next" != "null" ]] && ledger_out="$ledger_out next: ${next:0:80}"
                [[ -n "$ledger_out" ]] && output="$output"$'\n'"$ledger_out"

                if [[ -n "$snapshot" && "$snapshot" != "null" ]]; then
                    output="$output"$'\n'"[last session] $mood: ${snapshot:0:100}"
                fi
            fi

            # Behavioral corrections
            behavior_response=$(rpc_call "recall" '{"query":"behavior correction preference rule","tag":"correction","limit":3}')
            behavior_text=$(extract_text "$behavior_response")
            if [[ -n "$behavior_text" && "$behavior_text" != *"No memories"* ]]; then
                corrections=$(echo "$behavior_text" | grep -oP 'CORRECT: \K[^\n]+' | head -3 | tr '\n' '; ')
                [[ -n "$corrections" ]] && output="$output"$'\n'"[rules] $corrections"
            fi

            # Incremental code re-indexing after /clear (context recovery)
            if [[ -d ".git" ]]; then
                PROJECT_NAME=$(basename "$PWD")
                escaped_path=$(json_escape "$PWD")
                escaped_project=$(json_escape "$PROJECT_NAME")
                index_response=$(rpc_call "learn_codebase" "{\"path\":\"$escaped_path\",\"project\":\"$escaped_project\"}" 2>/dev/null)
                files_processed=$(echo "$index_response" | jq -r '.result.structured.files_processed // 0' 2>/dev/null)
                if [[ "$files_processed" -gt 0 ]]; then
                    output="$output"$'\n'"[code] indexed $files_processed changed files"
                fi
            fi

            # Output context and exit early - don't also do memory recall
            if [[ -n "$output" ]]; then
                echo "$output"
            fi
            exit 0
        fi

        # ===========================================
        # PROACTIVE LEARNING: Detect learning opportunities
        # ===========================================
        LEARNING_HINTS=""

        # Save user message for Stop hook to analyze
        echo "$QUERY" > "$MIND_PATH/.last_user_message"

        # Detect CORRECTION patterns: user is correcting Claude
        if echo "$QUERY" | grep -qiE "(no,|no\.|actually|that'?s (wrong|not|incorrect)|you('re| are) wrong|I (said|meant|asked)|not what I|wrong approach|that won'?t work|don'?t do that)"; then
            LEARNING_HINTS="[LEARN] Correction detected → use learn_correction tool"
        fi

        # Detect PREFERENCE patterns: user expressing preferences
        if echo "$QUERY" | grep -qiE "(I (prefer|like|want|need|always|never|don'?t like)|please (don'?t|always|never)|stop doing|keep doing|from now on|in the future)"; then
            LEARNING_HINTS="${LEARNING_HINTS:+$LEARNING_HINTS; }[LEARN] Preference detected → use learn_preference tool"
        fi

        # Detect FRUSTRATION/STATE patterns: user emotional state
        if echo "$QUERY" | grep -qiE "(frustrated|annoyed|confused|stuck|lost|this is (hard|difficult|confusing)|I give up|help me understand|what am I missing)"; then
            LEARNING_HINTS="${LEARNING_HINTS:+$LEARNING_HINTS; }[LEARN] User state detected → use learn_approach if something helps"
        fi

        # Detect MILESTONE patterns: achievement
        if echo "$QUERY" | grep -qiE "(it works|finally|success|done|shipped|released|completed|finished|passed|merged|deployed)"; then
            LEARNING_HINTS="${LEARNING_HINTS:+$LEARNING_HINTS; }[LEARN] Milestone detected → use learn_milestone tool"
        fi

        # ===========================================
        # Proactive surfacing: detect implicit needs and expand search
        # ===========================================
        EXTRA_TAGS=""
        BOOST_K=3

        # Debug/error context → surface past failures and corrections
        if echo "$QUERY" | grep -qiE '(error|bug|fail|broken|not working|issue|problem|debug)'; then
            EXTRA_TAGS="correction,failure"
            BOOST_K=5
        fi

        # Planning/decision context → surface past decisions
        if echo "$QUERY" | grep -qiE '(should (I|we)|how to|best way|approach|decide|choose|plan)'; then
            EXTRA_TAGS="preference,decision"
            BOOST_K=5
        fi

        # Emotional state context → surface past approaches
        if echo "$QUERY" | grep -qiE '(stuck|frustrated|confused|lost|overwhelmed|rushing|uncertain)'; then
            EXTRA_TAGS="${EXTRA_TAGS:+$EXTRA_TAGS,}approach"
            BOOST_K=5
        fi

        # Get relevant memories - boost k for implicit needs
        debug "=== MEMORY SEARCH ==="
        debug "Query: ${QUERY:0:100}"
        debug "Realm: $REALM"
        debug "Boost k: $BOOST_K"
        [[ -n "$EXTRA_TAGS" ]] && debug "Extra tags: $EXTRA_TAGS"

        response=$(rpc_call "full_resonate" "{\"query\":\"$ESCAPED_QUERY\",\"k\":$BOOST_K,\"realm\":\"$ESCAPED_REALM\",\"include_global\":true}")
        memories=$(extract_text "$response")

        debug "=== RAW RESULTS ==="
        if [[ -n "$memories" ]]; then
            echo "$memories" | head -10 | while read -r line; do
                debug "  $line"
            done
        else
            debug "  (no memories found)"
        fi

        if [[ -n "$memories" && "$memories" != *"No memories"* ]]; then
            # Ultra-compact: just content, no headers, max 2 lines (~100 chars each)
            compact=$(echo "$memories" | grep -E '^\[[0-9]+%\]' | head -2 | \
                sed 's/^\[[0-9]*%\] \[[^]]*\] //' | \
                cut -c1-100 | \
                sed 's/$/.../')
            if [[ -n "$compact" ]]; then
                output="$compact"
                debug "=== INJECTED ==="
                debug "  $compact"
            fi
        fi

        # Proactive: also surface preferences/corrections if detected as relevant
        if [[ -n "$EXTRA_TAGS" ]]; then
            for tag in ${EXTRA_TAGS//,/ }; do
                tag_response=$(rpc_call "recall" "{\"query\":\"$ESCAPED_QUERY\",\"tag\":\"$tag\",\"limit\":1}")
                tag_mem=$(extract_text "$tag_response")
                if [[ -n "$tag_mem" && "$tag_mem" != *"No memories"* ]]; then
                    tag_compact=$(echo "$tag_mem" | grep -E '^\[[0-9]+%\]' | head -1 | \
                        sed 's/^\[[0-9]*%\] \[[^]]*\] //' | cut -c1-80)
                    if [[ -n "$tag_compact" && "$output" != *"$tag_compact"* ]]; then
                        output="$output"$'\n'"[$tag] $tag_compact..."
                    fi
                fi
            done
        fi

        # Code context injection: if query mentions code concepts, inject relevant symbols
        # Detect code-related queries (function names, class names, implementation, refactor, etc.)
        if echo "$QUERY" | grep -qiE '(function|class|method|implement|refactor|fix|bug|error|call|define|where is|find|search.*code|how does|what does)'; then
            debug "=== CODE SEARCH ==="
            # Get relevant code symbols (top 2 for compactness)
            code_response=$(rpc_call "search_symbols" "{\"query\":\"$ESCAPED_QUERY\",\"limit\":2}")
            code_text=$(extract_text "$code_response")
            debug "Code response: ${code_text:0:200}"

            if [[ -n "$code_text" && "$code_text" != *"No symbols"* && "$code_text" != *"Error"* ]]; then
                # Parse JSON output if available, otherwise use text
                symbols=$(echo "$code_text" | jq -r '.[] | "\(.kind) \(.name) @ \(.file):\(.line_start)"' 2>/dev/null | head -2)
                if [[ -z "$symbols" ]]; then
                    # Fallback: extract from text format
                    symbols=$(echo "$code_text" | grep -oE '(class|function|method)\s+\S+\s+@\s+[^:]+:\d+' | head -2)
                fi

                if [[ -n "$symbols" ]]; then
                    debug "Code symbols: $symbols"
                    if [[ -n "$output" ]]; then
                        output="$output"$'\n'"[code] $symbols"
                    else
                        output="[code] $symbols"
                    fi
                fi
            fi
        fi

        # Add learning hints if detected (these prompt Claude to use learn_* tools)
        if [[ -n "$LEARNING_HINTS" ]]; then
            output="${output:+$output"$'\n'"}$LEARNING_HINTS"
        fi

        # ===========================================
        # ANTICIPATION: Predict likely user needs based on context patterns
        # ===========================================
        # Build context from project + query keywords
        project_name=$(basename "$(pwd)")
        context_key="${project_name}:$(echo "$QUERY" | grep -oE '^[a-zA-Z]+' | head -1 | tr '[:upper:]' '[:lower:]')"
        escaped_context=$(json_escape "$context_key")

        anticipation_response=$(rpc_call "anticipation_predict" "{\"context\":\"$escaped_context\",\"limit\":1}")
        anticipated_action=$(echo "$anticipation_response" | jq -r '.result.structured.patterns[0].action // empty' 2>/dev/null)
        anticipated_id=$(echo "$anticipation_response" | jq -r '.result.structured.patterns[0].id // empty' 2>/dev/null)

        if [[ -n "$anticipated_action" && "$anticipated_action" != "null" ]]; then
            output="${output:+$output"$'\n'"}[predict] $anticipated_action"
            # Save for Stop hook to verify
            echo "$anticipated_id:$anticipated_action" > "$MIND_PATH/.last_anticipation"
        fi

        # Output combined context if any
        if [[ -n "$output" ]]; then
            debug "=== FINAL OUTPUT ==="
            debug "$output"
            echo "$output"
        else
            debug "=== NO OUTPUT ==="
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

        # ===========================================
        # AUTO-LEARNING: Detect missed learning opportunities
        # ===========================================
        CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"

        # Read user's last message (saved by UserPromptSubmit)
        LAST_USER_MSG=""
        if [[ -f "$MIND_PATH/.last_user_message" ]]; then
            LAST_USER_MSG=$(cat "$MIND_PATH/.last_user_message" 2>/dev/null)
        fi

        # Check if Claude used a learn_* tool (indicated by tool output patterns)
        CLAUDE_LEARNED=false
        if echo "$RESPONSE" | grep -qiE '(learn_correction|learn_preference|learn_insight|learn_approach|learn_outcome|learn_milestone|Stored|Learned|Remembered)'; then
            CLAUDE_LEARNED=true
        fi

        # If user correction detected but Claude didn't learn, auto-store
        # Uses chitta CLI (more reliable than nc for RPC)
        if [[ "$CLAUDE_LEARNED" == "false" && -n "$LAST_USER_MSG" ]]; then
            if echo "$LAST_USER_MSG" | grep -qiE "(no,|no\.|actually|that'?s (wrong|not|incorrect)|you('re| are) wrong|wrong approach|that won'?t work)"; then
                # Extract what was wrong from user message
                correction_context=$(echo "$LAST_USER_MSG" | head -c 150 | tr '\n' ' ')
                # Extract what Claude should do instead from response (first line)
                better_approach=$(echo "$RESPONSE" | grep -v '^$' | head -1 | head -c 150)

                # Format as SSL correction (matches learn_correction format)
                content="[correction] WRONG: $correction_context
CORRECT: $better_approach"

                "$CHITTA_BIN" remember --content "$content" --tags "correction,auto-learned" --type wisdom --visibility 2 >/dev/null 2>&1 && \
                    echo "[auto-learn] Correction stored"
            fi

            # If user preference detected but Claude didn't learn, auto-store
            if echo "$LAST_USER_MSG" | grep -qiE "(I (prefer|like|want|always|never)|please (don'?t|always|never)|from now on)"; then
                pref_context=$(echo "$LAST_USER_MSG" | head -c 200 | tr '\n' ' ')

                # Format as SSL preference
                content="[preference] $pref_context"

                "$CHITTA_BIN" remember --content "$content" --tags "preference,auto-learned" --type wisdom --visibility 2 >/dev/null 2>&1 && \
                    echo "[auto-learn] Preference stored"
            fi

            # If milestone detected, auto-store
            if echo "$LAST_USER_MSG" | grep -qiE "(it works|finally|success|shipped|released|completed|finished|passed|merged|deployed)"; then
                milestone_context=$(echo "$LAST_USER_MSG" | head -c 100 | tr '\n' ' ')

                # Format as SSL milestone
                content="[milestone] $milestone_context"

                "$CHITTA_BIN" remember --content "$content" --tags "milestone,auto-learned" --type wisdom --visibility 2 >/dev/null 2>&1 && \
                    echo "[auto-learn] Milestone stored"
            fi
        fi

        # Clean up temp file
        rm -f "$MIND_PATH/.last_user_message" 2>/dev/null

        # ===========================================
        # ANTICIPATION: Record patterns and verify predictions
        # ===========================================
        if is_meaningful_work "$RESPONSE"; then
            project_name=$(basename "$(pwd)")

            # Extract action from response (what Claude did)
            action=""
            if echo "$RESPONSE" | grep -qiE 'created|implemented|added'; then
                action="create"
            elif echo "$RESPONSE" | grep -qiE 'fixed|resolved|repaired'; then
                action="fix"
            elif echo "$RESPONSE" | grep -qiE 'updated|modified|changed|refactored'; then
                action="update"
            elif echo "$RESPONSE" | grep -qiE 'deleted|removed'; then
                action="delete"
            elif echo "$RESPONSE" | grep -qiE 'tested|ran tests|passed'; then
                action="test"
            fi

            if [[ -n "$action" ]]; then
                # Build context key: project:first_query_word
                context_key="${project_name}:$(echo "$LAST_USER_MSG" 2>/dev/null | grep -oE '^[a-zA-Z]+' | head -1 | tr '[:upper:]' '[:lower:]')"
                escaped_context=$(json_escape "$context_key")
                escaped_action=$(json_escape "$action")

                # Record this context→action pattern
                rpc_call "anticipation_observe" "{\"context\":\"$escaped_context\",\"action\":\"$escaped_action\"}" >/dev/null 2>&1
            fi

            # Check if we predicted this action correctly
            if [[ -f "$MIND_PATH/.last_anticipation" ]]; then
                last_anticipation=$(cat "$MIND_PATH/.last_anticipation" 2>/dev/null)
                anticipated_id="${last_anticipation%%:*}"
                anticipated_action="${last_anticipation#*:}"

                if [[ -n "$anticipated_id" && -n "$action" && "$anticipated_action" == *"$action"* ]]; then
                    # Prediction was correct - strengthen pattern
                    rpc_call "anticipation_success" "{\"id\":$anticipated_id}" >/dev/null 2>&1
                fi
                rm -f "$MIND_PATH/.last_anticipation" 2>/dev/null
            fi
        fi

        # Session momentum: track turn count for periodic checkpoints
        TURN_FILE="$MIND_PATH/.turn_count"
        TURN_COUNT=0
        if [[ -f "$TURN_FILE" ]]; then
            TURN_COUNT=$(cat "$TURN_FILE" 2>/dev/null || echo "0")
        fi
        TURN_COUNT=$((TURN_COUNT + 1))
        echo "$TURN_COUNT" > "$TURN_FILE"

        # Auto-checkpoint if: meaningful work OR every 5 turns
        # Note: Memories are stored via MCP tools, not hook extraction
        SHOULD_CHECKPOINT=false
        if is_meaningful_work "$RESPONSE"; then
            SHOULD_CHECKPOINT=true
        elif [[ $((TURN_COUNT % 5)) -eq 0 ]]; then
            SHOULD_CHECKPOINT=true
        fi

        if [[ "$SHOULD_CHECKPOINT" == "true" ]]; then
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

            # Extract open questions/blockers (patterns: "?", "should we", "need to decide", "unclear")
            blockers_json=$(echo "$RESPONSE" | grep -iE '(\?$|should (we|I)|need to (decide|clarify|figure out)|unclear|blocked|waiting|depends on)' | head -5 | sed 's/^[[:space:]]*//' | jq -R . | jq -s '.')

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

            escaped_topic=$(json_escape "$topic")
            escaped_realm=$(json_escape "$REALM")

            # Ensure JSON arrays have defaults
            [[ -z "$files_json" || "$files_json" == "null" ]] && files_json='[]'
            [[ -z "$decisions_json" || "$decisions_json" == "null" ]] && decisions_json='[]'
            [[ -z "$next_steps_json" || "$next_steps_json" == "null" ]] && next_steps_json='[]'
            [[ -z "$blockers_json" || "$blockers_json" == "null" ]] && blockers_json='[]'

            # Include turn count in snapshot for momentum tracking
            snapshot="[turn $TURN_COUNT] $topic"

            # Use CLI for reliable checkpoint
            if "$CHITTA_BIN" ledger_save \
                --session_id "$SESSION_ID" \
                --project "$REALM" \
                --mood "$mood" \
                --active_files "$files_json" \
                --decisions "$decisions_json" \
                --next_steps "$next_steps_json" \
                --blockers "$blockers_json" \
                --snapshot "$snapshot" >/dev/null 2>&1; then
                echo "[cc-soul] Checkpoint: $SESSION_ID ($mood) [turn $TURN_COUNT]"
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

    pre-tool|PreToolUse)
        # PreToolUse: Inject context before tool execution
        # Usage: simple-hook.sh pre-tool <matcher>
        # Matcher: Read, Edit, Write, Bash, etc.
        MATCHER="${1:-}"
        STDIN_DATA=$(cat)

        # Quick exit if no matcher or no daemon
        [[ -z "$MATCHER" ]] && exit 0
        ensure_daemon || exit 0

        CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"

        case "$MATCHER" in
            Read)
                # Before reading file: inject relevant memories about that file
                file_path=$(echo "$STDIN_DATA" | jq -r '.tool_input.file_path // empty')
                [[ -z "$file_path" ]] && exit 0

                # Extract filename for query
                filename=$(basename "$file_path")
                escaped_query=$(json_escape "file:$filename")

                # Quick recall - timeout 2s to not block
                memories=$(timeout 2 "$CHITTA_BIN" recall --query "$escaped_query" --limit 1 --json 2>/dev/null | jq -r '.[0].content // empty' | head -c 200)

                if [[ -n "$memories" ]]; then
                    escaped_mem=$(json_escape "$memories")
                    echo "{\"hookSpecificOutput\":{\"hookEventName\":\"PreToolUse\",\"additionalContext\":\"[past] $escaped_mem\"}}"
                fi
                ;;

            Edit|Write)
                # Before editing: surface past decisions about this file
                file_path=$(echo "$STDIN_DATA" | jq -r '.tool_input.file_path // empty')
                [[ -z "$file_path" ]] && exit 0

                filename=$(basename "$file_path")
                escaped_query=$(json_escape "editing $filename decision preference")

                # Quick recall - timeout 2s
                memories=$(timeout 2 "$CHITTA_BIN" recall --query "$escaped_query" --limit 1 --json 2>/dev/null | jq -r '.[0].content // empty' | head -c 200)

                if [[ -n "$memories" ]]; then
                    escaped_mem=$(json_escape "$memories")
                    echo "{\"hookSpecificOutput\":{\"hookEventName\":\"PreToolUse\",\"additionalContext\":\"[past] $escaped_mem\"}}"
                fi
                ;;

            Bash)
                # Before bash: recall command preferences (optional, often too slow)
                # Skip for now - most bash commands are quick
                ;;
        esac
        ;;

    post-tool|PostToolUse)
        # PostToolUse: Learn from tool execution
        # Usage: simple-hook.sh post-tool <matcher>
        MATCHER="${1:-}"
        STDIN_DATA=$(cat)

        [[ -z "$MATCHER" ]] && exit 0

        CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"

        case "$MATCHER" in
            Edit|Write)
                # After edit: capture pattern for learning (background, non-blocking)
                file_path=$(echo "$STDIN_DATA" | jq -r '.tool_input.file_path // empty')
                old_string=$(echo "$STDIN_DATA" | jq -r '.tool_input.old_string // empty' | head -c 100)
                new_string=$(echo "$STDIN_DATA" | jq -r '.tool_input.new_string // .tool_input.content // empty' | head -c 100)

                # Only record significant changes
                if [[ ${#new_string} -gt 30 && -n "$file_path" ]]; then
                    filename=$(basename "$file_path")
                    content="[signal] Edit: $filename
$old_string → $new_string"
                    # Background: don't block Claude
                    "$CHITTA_BIN" observe --title "Edit: $filename" --content "$content" --category signal >/dev/null 2>&1 &
                fi
                ;;
        esac
        ;;

    post-failure|PostToolUseFailure)
        # PostToolUseFailure: Record failures for future avoidance
        # Usage: simple-hook.sh post-failure
        STDIN_DATA=$(cat)

        CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"

        tool_name=$(echo "$STDIN_DATA" | jq -r '.tool_name // empty')
        error=$(echo "$STDIN_DATA" | jq -r '.tool_response.error // .tool_response.stderr // empty' | head -c 200)

        # Record failure if we have meaningful error info
        if [[ -n "$error" && -n "$tool_name" ]]; then
            content="[failure] Tool: $tool_name
Error: $error"
            # Background: don't block
            "$CHITTA_BIN" remember --content "$content" --tags "failure,tool-error" --type episode >/dev/null 2>&1 &
        fi
        ;;

    *)
        echo "Usage: simple-hook.sh <start|prompt|stop|pre-compact|pre-tool|post-tool|post-failure> [matcher]" >&2
        exit 1
        ;;
esac
