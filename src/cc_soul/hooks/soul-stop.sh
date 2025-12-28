#!/bin/bash
# Soul Stop Hook - Auto-learn from assistant output
# This hook is triggered after each assistant response.
# It extracts the last response and feeds it to the soul for learning.

INPUT=$(cat)

TRANSCRIPT_PATH=$(echo "$INPUT" | python3 -c "import sys, json; print(json.load(sys.stdin).get('transcript_path', ''))" 2>/dev/null)

if [ -z "$TRANSCRIPT_PATH" ] || [ ! -f "$TRANSCRIPT_PATH" ]; then
    exit 0
fi

# Extract the last assistant message from the transcript
LAST_ASSISTANT=$(python3 -c "
import json, sys
try:
    with open(sys.argv[1]) as f:
        data = json.load(f)
    messages = data if isinstance(data, list) else data.get('messages', [])
    for msg in reversed(messages):
        if msg.get('role') == 'assistant':
            content = msg.get('content', '')
            if isinstance(content, list):
                content = '\n'.join(c.get('text', '') for c in content if c.get('type') == 'text')
            if content and len(content) > 100:
                print(content[:2000])
                break
except Exception:
    pass
" "$TRANSCRIPT_PATH" 2>/dev/null)

# Run auto-learning on substantial output
if [ -n "$LAST_ASSISTANT" ] && [ ${#LAST_ASSISTANT} -gt 100 ]; then
    echo "$LAST_ASSISTANT" | cc-soul hook stop 2>/dev/null
fi

exit 0
