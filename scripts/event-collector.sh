#!/bin/bash
# Event Collector Hook - Captures high-signal tool usage for async distillation
#
# Triggered by: PostToolUse hook (fires after every tool call)
# Output: Appends candidates to ~/.claude/mind/.event_candidates.jsonl
#
# Architecture:
#   1. Parse tool input from stdin (JSON)
#   2. Apply prefilter rules (high-signal only)
#   3. Append to candidate queue
#   4. Daemon distills candidates async

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CANDIDATE_FILE="${HOME}/.claude/mind/.event_candidates.jsonl"
SESSION_CACHE="${HOME}/.claude/mind/.event_session_cache"
MAX_CANDIDATES=1000

# Source rules library
source "$SCRIPT_DIR/lib/event-rules.sh" 2>/dev/null || {
    # Inline minimal rules if library not found
    is_high_signal_bash() { return 1; }
    is_high_signal_read() { return 1; }
    is_high_signal_edit() { return 1; }
}

# Ensure candidate file exists
mkdir -p "$(dirname "$CANDIDATE_FILE")"
touch "$CANDIDATE_FILE"

# Read tool input from stdin
INPUT=$(cat)
TOOL_NAME=$(echo "$INPUT" | jq -r '.tool_name // .name // empty' 2>/dev/null)
TOOL_INPUT=$(echo "$INPUT" | jq -r '.tool_input // .input // empty' 2>/dev/null)

# Exit early if no tool info
[[ -z "$TOOL_NAME" ]] && exit 0

# Get timestamp and session info
TS=$(date +%s)
SESSION_ID="${CLAUDE_SESSION_ID:-unknown}"
CWD=$(pwd)

# Extract project name from cwd
PROJECT=$(basename "$CWD")

# Process based on tool type
case "$TOOL_NAME" in
    Bash|bash)
        CMD=$(echo "$TOOL_INPUT" | jq -r '.command // empty' 2>/dev/null)
        [[ -z "$CMD" ]] && exit 0

        # Check if high-signal bash command
        if is_high_signal_bash "$CMD" "$CWD"; then
            SIGNAL=$(get_bash_signal "$CMD")

            # Check for repetition (boosts signal)
            if is_repeated "bash:$CMD"; then
                SIGNAL="${SIGNAL}+repeated"
            fi

            # Append to candidates
            jq -nc \
                --arg ts "$TS" \
                --arg type "bash" \
                --arg cmd "$CMD" \
                --arg cwd "$CWD" \
                --arg project "$PROJECT" \
                --arg signal "$SIGNAL" \
                --arg session "$SESSION_ID" \
                '{ts: ($ts|tonumber), type: $type, content: $cmd, cwd: $cwd, project: $project, signal: $signal, session: $session}' \
                >> "$CANDIDATE_FILE"

            # Track for repetition detection
            track_event "bash:$CMD"
        fi
        ;;

    Read|read)
        PATH_ARG=$(echo "$TOOL_INPUT" | jq -r '.file_path // .path // empty' 2>/dev/null)
        [[ -z "$PATH_ARG" ]] && exit 0

        # Check if high-signal file read
        if is_high_signal_read "$PATH_ARG"; then
            SIGNAL=$(get_read_signal "$PATH_ARG")

            # Check for repetition
            if is_repeated "read:$PATH_ARG"; then
                SIGNAL="${SIGNAL}+repeated"
            fi

            jq -nc \
                --arg ts "$TS" \
                --arg type "read" \
                --arg path "$PATH_ARG" \
                --arg cwd "$CWD" \
                --arg project "$PROJECT" \
                --arg signal "$SIGNAL" \
                --arg session "$SESSION_ID" \
                '{ts: ($ts|tonumber), type: $type, content: $path, cwd: $cwd, project: $project, signal: $signal, session: $session}' \
                >> "$CANDIDATE_FILE"

            track_event "read:$PATH_ARG"
        fi
        ;;

    Edit|edit|Write|write)
        PATH_ARG=$(echo "$TOOL_INPUT" | jq -r '.file_path // .path // empty' 2>/dev/null)
        [[ -z "$PATH_ARG" ]] && exit 0

        # Check if high-signal file edit
        if is_high_signal_edit "$PATH_ARG"; then
            SIGNAL=$(get_edit_signal "$PATH_ARG")

            jq -nc \
                --arg ts "$TS" \
                --arg type "edit" \
                --arg path "$PATH_ARG" \
                --arg cwd "$CWD" \
                --arg project "$PROJECT" \
                --arg signal "$SIGNAL" \
                --arg session "$SESSION_ID" \
                '{ts: ($ts|tonumber), type: $type, content: $path, cwd: $cwd, project: $project, signal: $signal, session: $session}' \
                >> "$CANDIDATE_FILE"
        fi
        ;;
esac

# Rotate candidate file if too large
LINES=$(wc -l < "$CANDIDATE_FILE" 2>/dev/null || echo 0)
if [[ $LINES -gt $MAX_CANDIDATES ]]; then
    # Keep last half
    KEEP=$((MAX_CANDIDATES / 2))
    tail -n "$KEEP" "$CANDIDATE_FILE" > "${CANDIDATE_FILE}.tmp"
    mv "${CANDIDATE_FILE}.tmp" "$CANDIDATE_FILE"
fi

exit 0
