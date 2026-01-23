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

# Parse header (only from first 5 lines before ---)
SESSION_ID=$(head -5 "$TEMP_FILE" | grep '^SESSION_ID=' | cut -d= -f2)
REALM=$(head -5 "$TEMP_FILE" | grep '^REALM=' | cut -d= -f2)
MODEL=$(head -5 "$TEMP_FILE" | grep '^MODEL=' | cut -d= -f2)

# Extract conversation (everything after ---)
CONVERSATION=$(sed -n '/^---$/,$ p' "$TEMP_FILE" | tail -n +2)

if [[ -z "$CONVERSATION" ]]; then
    echo "[distill] No conversation content" >&2
    exit 0
fi

# Call OpenCode to distill the conversation
echo "[distill] Session $SESSION_ID: Calling OpenCode for distillation"

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"

# Truncate conversation if too long (keep last ~80k chars for model context)
MAX_CONV_CHARS=80000
if [[ ${#CONVERSATION} -gt $MAX_CONV_CHARS ]]; then
    echo "[distill] Truncating conversation from ${#CONVERSATION} to $MAX_CONV_CHARS chars"
    CONVERSATION="${CONVERSATION: -$MAX_CONV_CHARS}"
fi

# Build combined prompt with conversation inline (opencode -f doesn't work reliably)
FULL_PROMPT="Extract learnings from this conversation in SSL v0.2 format.

SSL Format:
[LEARN] [domain] subject→action→result @location
[ε] Expansion hint (exact formulas/code preserved verbatim)
[TRIPLET] subject predicate object

Symbols: → (produces) | (or) + (and) @ (location) ! (negation) ? (uncertainty)

Focus on: Technical decisions, patterns learned, bugs fixed, architecture insights.
Goal: High epiplexity (ε ≥ 0.6). Include key terms verbatim for reconstruction.

CONVERSATION:
$CONVERSATION

Output only SSL format:"

# Use stdin to pass the prompt (handles long content better than args)
RESULT=$(echo "$FULL_PROMPT" | opencode run -m "$MODEL" 2>/dev/null)

if [[ -z "$RESULT" ]]; then
    echo "[distill] No result from OpenCode" >&2
    exit 0
fi

echo "[distill] Processing OpenCode result..."

# Create a minimal episode marker (not the raw content - that has XML/system noise)
# The episode is just a reference point; extracted SSL learnings are the real content
EPISODE_RESPONSE=$("$CHITTA_BIN" grow --type episode --content "Session $SESSION_ID distilled" --realm "$REALM" --json 2>/dev/null || echo "{}")
EPISODE_ID=$(echo "$EPISODE_RESPONSE" | grep -oP '"id"\s*:\s*"\K[^"]+' | head -1)

if [[ -n "$EPISODE_ID" ]]; then
    echo "[distill]   Episode saved: $EPISODE_ID"
else
    echo "[distill]   Warning: Could not get episode ID"
fi

# Extract and store [LEARN] lines with their [ε] hints, linking to episode
# Process line by line, combining [LEARN] with following [ε] if present
LEARN_CONTENT=""
while IFS= read -r line; do
    if [[ "$line" == "[LEARN]"* ]]; then
        # If we have a pending LEARN, store it first
        if [[ -n "$LEARN_CONTENT" ]]; then
            WISDOM_RESPONSE=$("$CHITTA_BIN" grow --type wisdom --content "$LEARN_CONTENT" --realm "$REALM" --json 2>/dev/null || echo "{}")
            WISDOM_ID=$(echo "$WISDOM_RESPONSE" | grep -oP '"id"\s*:\s*"\K[^"]+' | head -1)
            if [[ -n "$WISDOM_ID" ]]; then
                echo "[distill]   Stored: ${LEARN_CONTENT:0:60}..."
                [[ -n "$EPISODE_ID" ]] && "$CHITTA_BIN" connect --subject "wisdom:$WISDOM_ID" --predicate "derived_from" --object "episode:$EPISODE_ID" 2>/dev/null || true
            fi
        fi
        # Start new LEARN
        LEARN_CONTENT="${line#\[LEARN\] }"
    elif [[ "$line" == "[ε]"* && -n "$LEARN_CONTENT" ]]; then
        # Append epsilon hint to current LEARN
        LEARN_CONTENT="$LEARN_CONTENT
${line}"
    fi
done <<< "$RESULT"

# Store last LEARN if pending
if [[ -n "$LEARN_CONTENT" ]]; then
    WISDOM_RESPONSE=$("$CHITTA_BIN" grow --type wisdom --content "$LEARN_CONTENT" --realm "$REALM" --json 2>/dev/null || echo "{}")
    WISDOM_ID=$(echo "$WISDOM_RESPONSE" | grep -oP '"id"\s*:\s*"\K[^"]+' | head -1)
    if [[ -n "$WISDOM_ID" ]]; then
        echo "[distill]   Stored: ${LEARN_CONTENT:0:60}..."
        [[ -n "$EPISODE_ID" ]] && "$CHITTA_BIN" connect --subject "wisdom:$WISDOM_ID" --predicate "derived_from" --object "episode:$EPISODE_ID" 2>/dev/null || true
    fi
fi

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
