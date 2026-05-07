#!/usr/bin/env bash
# Codex PreToolUse wrapper: force low-token guardrails by default.
set -euo pipefail

MATCHER="${1:-}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Enforce strict pre-tool behavior for Codex sessions.
export CC_SOUL_STRICT_MODE="${CC_SOUL_STRICT_MODE:-1}"
export CC_SOUL_SUBAGENT_BASH_RECALL="${CC_SOUL_SUBAGENT_BASH_RECALL:-0}"
export CC_SOUL_DEEP_SEARCH="${CC_SOUL_DEEP_SEARCH:-0}"

exec "${SCRIPT_DIR}/pre-tool-hook.sh" "$MATCHER"

