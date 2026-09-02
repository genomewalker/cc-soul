#!/usr/bin/env bash
# Codex PreToolUse wrapper: force low-token guardrails by default.
set -euo pipefail

MATCHER="${1:-}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Enforce strict pre-tool behavior for Codex sessions. Set both names so the
# effective value is seen whichever one pre-tool-hook.sh reads.
export CHITTA_STRICT_MODE="${CHITTA_STRICT_MODE:-${CC_SOUL_STRICT_MODE:-1}}"
export CC_SOUL_STRICT_MODE="$CHITTA_STRICT_MODE"
export CHITTA_SUBAGENT_BASH_RECALL="${CHITTA_SUBAGENT_BASH_RECALL:-${CC_SOUL_SUBAGENT_BASH_RECALL:-0}}"
export CC_SOUL_SUBAGENT_BASH_RECALL="$CHITTA_SUBAGENT_BASH_RECALL"
export CHITTA_DEEP_SEARCH="${CHITTA_DEEP_SEARCH:-${CC_SOUL_DEEP_SEARCH:-0}}"
export CC_SOUL_DEEP_SEARCH="$CHITTA_DEEP_SEARCH"

exec "${SCRIPT_DIR}/pre-tool-hook.sh" "$MATCHER"

