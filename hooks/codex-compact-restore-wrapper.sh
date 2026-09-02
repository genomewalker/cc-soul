#!/usr/bin/env bash
# Codex-safe adapter for Claude's richer compact-restore SessionStart hook.

INPUT=$(cat)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -n "${CC_SOUL_HEADLESS:-}" ]]; then
    printf '%s\n' '{}'
    exit 0
fi

RAW_OUTPUT=$(printf '%s' "$INPUT" | "${SCRIPT_DIR}/compact-restore-hook.sh" 2>/dev/null || true)

# Codex 0.147 accepts only hookEventName and additionalContext inside the
# SessionStart-specific object. Claude-only fields such as watchPaths must not
# cross this adapter boundary.
if FILTERED_OUTPUT=$(printf '%s' "$RAW_OUTPUT" | jq -ce '
    if (.hookSpecificOutput.additionalContext? | type) == "string" then
        {hookSpecificOutput: {
            hookEventName: "SessionStart",
            additionalContext: .hookSpecificOutput.additionalContext
        }}
    else
        {}
    end
' 2>/dev/null); then
    printf '%s\n' "$FILTERED_OUTPUT"
else
    printf '%s\n' '{}'
fi

