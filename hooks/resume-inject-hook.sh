#!/bin/bash
# Headless bridge participant: nothing to resume, and /recap would hijack the
# one prompt it exists to answer.
if [[ -n "$CC_SOUL_HEADLESS" ]]; then cat >/dev/null; printf '{}'; exit 0; fi

# SessionStart hook for source=resume: Auto-trigger /recap
#
# Emits hookSpecificOutput.initialUserMessage to inject "/recap" as the first
# user message, providing token-efficient session continuation (~1500 tokens
# instead of replaying 100K+ of raw history).
#
# Disable with CC_SOUL_AUTO_RECAP=0

[[ "${CC_SOUL_AUTO_RECAP:-1}" == "0" ]] && exit 0

# Emit JSON with initialUserMessage
# stdout starts with { → Claude Code parses as hookSpecificOutput
cat <<'EOF'
{
  "hookSpecificOutput": {
    "hookEventName": "SessionStart",
    "initialUserMessage": "/recap"
  }
}
EOF

exit 0
