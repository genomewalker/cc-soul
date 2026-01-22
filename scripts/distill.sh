#!/bin/bash
# distill.sh - Distill conversation into memories using OpenCode
#
# Called by chittad with a temp file containing:
#   SESSION_ID=<session>
#   REALM=<realm>
#   MODEL=<model>
#   ---
#   [user]
#   <content>
#
#   [assistant]
#   <content>
#   ...
#
# The script should:
# 1. Parse the conversation
# 2. Call OpenCode for distillation
# 3. Store results using chitta CLI

set -e

TEMP_FILE="$1"
if [[ -z "$TEMP_FILE" || ! -f "$TEMP_FILE" ]]; then
    echo "[distill] Error: No input file provided" >&2
    exit 1
fi

# Parse header
SESSION_ID=$(grep '^SESSION_ID=' "$TEMP_FILE" | cut -d= -f2)
REALM=$(grep '^REALM=' "$TEMP_FILE" | cut -d= -f2)
MODEL=$(grep '^MODEL=' "$TEMP_FILE" | cut -d= -f2)

# Extract conversation (everything after ---)
CONVERSATION=$(sed -n '/^---$/,$ p' "$TEMP_FILE" | tail -n +2)

if [[ -z "$CONVERSATION" ]]; then
    echo "[distill] No conversation content" >&2
    exit 0
fi

# Create distillation prompt
PROMPT="Analyze this conversation and extract learnings in SSL (Soul Semantic Language) format.

For each insight, output:
[LEARN] [domain] pattern → result
[TRIPLET] subject predicate object

Rules:
- Extract key decisions, insights, patterns learned
- Use arrows (→) to show cause/effect
- Create triplets for relationships discovered
- Be concise - compress to reconstructable seeds
- Domain should match the conversation topic

Conversation:
$CONVERSATION

Output only SSL format, no explanation:"

# Call OpenCode to distill the conversation
echo "[distill] Session $SESSION_ID: Calling OpenCode for distillation"

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"

RESULT=$(opencode run -m "$MODEL" "$PROMPT" 2>/dev/null)

if [[ -z "$RESULT" ]]; then
    echo "[distill] No result from OpenCode" >&2
    exit 0
fi

echo "[distill] Processing OpenCode result..."

# First, save the conversation as an episode and get its ID
EPISODE_SUMMARY=$(echo "$CONVERSATION" | head -c 500 | tr '\n' ' ')
EPISODE_RESPONSE=$("$CHITTA_BIN" grow --type episode --content "[session:$SESSION_ID] $EPISODE_SUMMARY" --realm "$REALM" --json 2>/dev/null || echo "{}")
EPISODE_ID=$(echo "$EPISODE_RESPONSE" | grep -oP '"id"\s*:\s*"\K[^"]+' | head -1)

if [[ -n "$EPISODE_ID" ]]; then
    echo "[distill]   Episode saved: $EPISODE_ID"
else
    echo "[distill]   Warning: Could not get episode ID"
fi

# Extract and store [LEARN] lines, linking to episode
echo "$RESULT" | grep '^\[LEARN\]' | while read -r line; do
    content="${line#\[LEARN\] }"
    if [[ -n "$content" ]]; then
        # Store wisdom and get its ID
        WISDOM_RESPONSE=$("$CHITTA_BIN" grow --type wisdom --content "$content" --realm "$REALM" --json 2>/dev/null || echo "{}")
        WISDOM_ID=$(echo "$WISDOM_RESPONSE" | grep -oP '"id"\s*:\s*"\K[^"]+' | head -1)

        if [[ -n "$WISDOM_ID" ]]; then
            echo "[distill]   Stored wisdom: ${content:0:50}..."

            # Link wisdom to episode
            if [[ -n "$EPISODE_ID" ]]; then
                "$CHITTA_BIN" connect --subject "wisdom:$WISDOM_ID" --predicate "derived_from" --object "episode:$EPISODE_ID" 2>/dev/null || true
            fi
        fi
    fi
done

# Extract and store [TRIPLET] lines
echo "$RESULT" | grep '^\[TRIPLET\]' | while read -r line; do
    parts="${line#\[TRIPLET\] }"
    subject=$(echo "$parts" | awk '{print $1}')
    predicate=$(echo "$parts" | awk '{print $2}')
    object=$(echo "$parts" | awk '{$1=$2=""; print $0}' | xargs)

    if [[ -n "$subject" && -n "$predicate" && -n "$object" ]]; then
        "$CHITTA_BIN" connect --subject "$subject" --predicate "$predicate" --object "$object" 2>/dev/null && \
            echo "[distill]   Connected: $subject -> $predicate -> $object" || true
    fi
done

echo "[distill] Session $SESSION_ID: Done (episode=$EPISODE_ID)"
