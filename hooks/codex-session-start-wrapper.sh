#!/bin/bash
# Codex-compatible SessionStart wrapper for session-start-hook.sh
#
# Codex requires SessionStart hooks to output valid JSON {"context": "..."}.
# session-start-hook.sh outputs plain text (which works for Claude Code).
# This wrapper captures that output and re-emits it as JSON.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

INPUT=$(cat)

OUTPUT=$(echo "$INPUT" | bash "${SCRIPT_DIR}/session-start-hook.sh" 2>/dev/null)

if [[ -n "$OUTPUT" ]]; then
    printf '{"context": %s}\n' "$(printf '%s' "$OUTPUT" | jq -Rs .)"
else
    printf '{"context": ""}\n'
fi
