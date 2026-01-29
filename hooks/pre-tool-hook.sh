#!/bin/bash
# PreToolUse hook: DISABLED for performance
#
# This hook fires on every Read/Grep/Glob which causes too much load.
# Memory surfacing is handled by prompt-hook.sh instead.
#
# To re-enable, uncomment the code below.

exit 0

# --- DISABLED CODE ---
# Uncomment if you want per-file context injection (impacts performance)
#
# set -e
# CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
# MAX_WAIT=1
# INPUT=$(cat)
# TOOL_NAME=$(echo "$INPUT" | jq -r '.tool_name // empty')
# case "$TOOL_NAME" in
#     Read|Grep|Glob) ;;
#     *) exit 0 ;;
# esac
# # ... rest of hook logic
