#!/bin/bash
# Codex adapter: normalize input shape for shared stop core
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT="$(cat)"

# Ensure required fields exist with safe defaults.
# Keep session_id empty when missing so stop-core can derive a stable fallback.
INPUT=$(echo "$INPUT" | jq '. + {stop_hook_active: (.stop_hook_active // false), transcript_path: (.transcript_path // ""), session_id: (.session_id // "")}' 2>/dev/null || echo "$INPUT")

printf '%s' "$INPUT" | "$SCRIPT_DIR/stop-core.sh"
