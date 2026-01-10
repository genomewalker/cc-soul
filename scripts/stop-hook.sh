#!/bin/bash
# Stop hook: Extract soul operations from Claude's response
#
# Markers:
#   [LEARN] / [REMEMBER] → observe (discovery)
#   [GROW]               → grow (wisdom)
#   [INTEND]             → intend (goal)
#   [WONDER]             → wonder (question)
#   [FEEDBACK+]          → feedback positive
#   [FEEDBACK-]          → feedback negative
#
# Runs in background to avoid blocking.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

INPUT=$(cat)
TRANSCRIPT=$(echo "$INPUT" | jq -r '.transcript_path // empty')
STOP_ACTIVE=$(echo "$INPUT" | jq -r '.stop_hook_active // false')

[[ "$STOP_ACTIVE" == "true" ]] && echo '{}' && exit 0
[[ -z "$TRANSCRIPT" || ! -f "$TRANSCRIPT" ]] && echo '{}' && exit 0

# Background processing
(
    source "$SCRIPT_DIR/lib/chitta-lib.sh"
    CHITTA="$CHITTA_PLUGIN_DIR/bin/chitta"

    LAST_MSG=$(tac "$TRANSCRIPT" | grep -m1 '"role":"assistant"' | \
        jq -r '.message.content[] | select(.type=="text") | .text' 2>/dev/null | head -c 4000)

    [[ -z "$LAST_MSG" || ${#LAST_MSG} -lt 20 ]] && exit 0

    # Process each marker type
    process_marker() {
        local marker="$1" tool="$2" extra_args="$3"
        local content
        content=$(echo "$LAST_MSG" | grep -ioE "\[$marker\][^\[]*" | sed "s/\[$marker\]//i" | head -1 | xargs)
        if [[ -n "$content" && ${#content} -gt 5 ]]; then
            local title="${content:0:60}"
            case "$tool" in
                observe)
                    "$CHITTA" observe --category discovery --title "$title" --content "$content" >/dev/null 2>&1
                    ;;
                grow)
                    "$CHITTA" grow --level wisdom --title "$title" --content "$content" >/dev/null 2>&1
                    ;;
                intend)
                    "$CHITTA" intend --goal "$content" >/dev/null 2>&1
                    ;;
                wonder)
                    "$CHITTA" wonder --question "$content" >/dev/null 2>&1
                    ;;
                feedback)
                    "$CHITTA" feedback --signal "$extra_args" --context "$content" >/dev/null 2>&1
                    ;;
            esac
        fi
    }

    # Extract and process markers
    process_marker "LEARN" "observe"
    process_marker "REMEMBER" "observe"
    process_marker "GROW" "grow"
    process_marker "INTEND" "intend"
    process_marker "WONDER" "wonder"
    process_marker "FEEDBACK\\+" "feedback" "positive"
    process_marker "FEEDBACK-" "feedback" "negative"

) &

echo '{}'
