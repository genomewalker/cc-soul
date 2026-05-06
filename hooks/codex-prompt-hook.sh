#!/bin/bash
# Codex adapter: normalize input shape for shared prompt core
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT="$(cat)"

# Ensure .prompt exists (Codex may send different user text fields)
if ! echo "$INPUT" | jq -e '.prompt' >/dev/null 2>&1; then
  PROMPT=$(echo "$INPUT" | jq -r '.prompt // .message // .user_message // .text // empty' 2>/dev/null)
  INPUT=$(echo "$INPUT" | jq --arg p "$PROMPT" '. + {prompt: $p}' 2>/dev/null || echo "$INPUT")
fi

printf '%s' "$INPUT" | "$SCRIPT_DIR/prompt-core.sh"
