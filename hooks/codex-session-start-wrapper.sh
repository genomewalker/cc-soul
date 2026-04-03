#!/bin/bash
# Codex-compatible SessionStart wrapper for session-start-hook.sh
#
# Codex SessionStart hooks should produce no stdout output (side-effects only).
# session-start-hook.sh does useful setup (session registration, msg-notify,
# transcript queuing, realm detection) — we run it for those side effects,
# but suppress its context-injection stdout so Codex doesn't try to parse it as JSON.
#
# Context injection for Codex happens via UserPromptSubmit (prompt-hook.sh), not here.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Run session-start-hook.sh for side effects only; discard its stdout
cat | bash "${SCRIPT_DIR}/session-start-hook.sh" >/dev/null 2>/dev/null

exit 0
