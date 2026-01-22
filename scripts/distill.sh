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

# Create distillation prompt with SSL v0.2 format and epiplexity tuning
PROMPT="Extract learnings from this conversation in SSL v0.2 format.

SSL Format:
[LEARN] [domain] subject→action→result @location
[ε] Expansion hint (exact formulas/code preserved verbatim)
[TRIPLET] subject predicate object

Symbols: → (produces) | (or) + (and) @ (location) ! (negation) ? (uncertainty)

EPIPLEXITY (ε) - Reconstruction Quality:
Seeds scored on 4 dimensions (geometric mean, so all must be decent):
- S (semantic): embedding similarity original↔reconstructed (usually 0.9+)
- K (entities): F1 of key terms in seed vs original (CRITICAL - include key words!)
- D (density): concepts per token (40%+ target, arrows/symbols help)
- C (compression): reward 2-5x compression (β=0.7 saturation)

Goal: ε ≥ 0.6 (Good) or ε ≥ 0.8 (Excellent)

K is usually the bottleneck! To boost K:
- INCLUDE original terms: "token bucket" not "token_bucket", "100 tokens" not "100"
- Keep technical terms verbatim: "rate limiter", "burst protection", "refill"
- Don't over-abbreviate: "capacity" not "cap", "seconds" not "s"

Balance K and C:
- 2-3x compression is sweet spot (C≈0.5-0.65)
- Preserve ~60-70% of key terms (K≈0.6-0.7)
- Use [ε] for exact values without bloating seed

ENCODING EXAMPLES (with ε scores):

BAD (ε=0.36): Over-compressed, lost key terms
[LEARN] [rate] bucket→cap+refill→protect
❌ K=0.07 (lost: "token", "limiter", "100", "burst", "throughput")

FAIR (ε=0.57): Good compression but missing terms
[LEARN] [rate-limiting] token_bucket→capacity:100+refill:10/s→burst_protection
❌ K=0.44 (lost: "tokens", "per second", "throughput")

GOOD (ε=0.75): Balanced - key terms preserved, good compression
[LEARN] [rate-limiting] token bucket implemented→capacity 100 tokens+refills 10 per second→protects burst+sustained throughput
[ε] rate limiter using token bucket algorithm
[TRIPLET] token_bucket implements rate_limiting
✓ K=0.74, C=0.35, S=0.99

DECODING RULE: I expand seeds by:
1. Reading the pattern: subject→action→result
2. Using [ε] for exact values
3. Reconstructing prose that includes ALL seed terms

Conversation:
$CONVERSATION

Output only SSL format, optimized for high ε:"

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
