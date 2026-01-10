#!/bin/bash
# Stop hook: Extract learnings from Claude's last response
#
# Reads transcript, finds last assistant message, extracts learnings.
# Runs in background to avoid blocking.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Read hook input
INPUT=$(cat)
TRANSCRIPT=$(echo "$INPUT" | jq -r '.transcript_path // empty')
STOP_ACTIVE=$(echo "$INPUT" | jq -r '.stop_hook_active // false')

# Prevent infinite loops
[[ "$STOP_ACTIVE" == "true" ]] && echo '{}' && exit 0

# Skip if no transcript
[[ -z "$TRANSCRIPT" || ! -f "$TRANSCRIPT" ]] && echo '{}' && exit 0

# Extract last assistant message (background, non-blocking)
(
    source "$SCRIPT_DIR/lib/chitta-lib.sh"

    # Get last assistant message from transcript (Claude Code format: .message.content[])
    LAST_MSG=$(tac "$TRANSCRIPT" | grep -m1 '"role":"assistant"' | \
        jq -r '.message.content[] | select(.type=="text") | .text' 2>/dev/null | head -c 2000)

    [[ -z "$LAST_MSG" || ${#LAST_MSG} -lt 50 ]] && exit 0

    # Look for explicit learning markers
    if echo "$LAST_MSG" | grep -qiE '\[LEARN\]|\[REMEMBER\]|worth remembering|key insight'; then
        # Extract marked content
        INSIGHT=$(echo "$LAST_MSG" | grep -ioE '\[(LEARN|REMEMBER)\][^[]*' | sed 's/\[LEARN\]//i;s/\[REMEMBER\]//i' | head -1)

        if [[ -n "$INSIGHT" && ${#INSIGHT} -gt 10 ]]; then
            # Use chitta CLI directly (more reliable than socket in subshells)
            TITLE="${INSIGHT:0:60}"
            "$CHITTA_PLUGIN_DIR/bin/chitta" observe --category discovery --title "$TITLE" --content "$INSIGHT" >/dev/null 2>&1
        fi
    fi
) &

echo '{}'
