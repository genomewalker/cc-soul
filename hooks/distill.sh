#!/bin/bash
# distill.sh - Extract learnings from conversation using SSL format
#
# Called by chittad with a temp file containing:
#   SESSION_ID=<session>
#   REALM=<realm>
#   MODEL=<model>
#   ---
#   [user]
#   <content>
#   [assistant]
#   <content>
#   ...
#
# Outputs SSL-formatted learnings with typed markers

set -e

TEMP_FILE="$1"
if [[ -z "$TEMP_FILE" || ! -f "$TEMP_FILE" ]]; then
    echo "[distill] Error: No input file provided" >&2
    exit 1
fi

# Parse header
SESSION_ID=$(head -5 "$TEMP_FILE" | grep '^SESSION_ID=' | cut -d= -f2)
REALM=$(head -5 "$TEMP_FILE" | grep '^REALM=' | cut -d= -f2)
MODEL=$(head -5 "$TEMP_FILE" | grep '^MODEL=' | cut -d= -f2)

# Extract conversation (everything after ---)
CONVERSATION=$(sed -n '/^---$/,$ p' "$TEMP_FILE" | tail -n +2)

if [[ -z "$CONVERSATION" ]]; then
    echo "[distill] No conversation content" >&2
    exit 0
fi

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"

# Smart truncation: preserve head (context) + tail (conclusions)
# Total 80k: 25k head + 55k tail
MAX_CHARS=80000
HEAD_CHARS=25000
TAIL_CHARS=55000

if [[ ${#CONVERSATION} -gt $MAX_CHARS ]]; then
    echo "[distill] Truncating from ${#CONVERSATION} chars (preserving head+tail)"
    head_part="${CONVERSATION:0:$HEAD_CHARS}"
    tail_part="${CONVERSATION: -$TAIL_CHARS}"
    CONVERSATION="${head_part}

[... truncated ${#CONVERSATION} chars, keeping first ${HEAD_CHARS} + last ${TAIL_CHARS} ...]

${tail_part}"
fi

# SSL v0.2 prompt with typed markers
PROMPT='Extract learnings from this conversation in SSL v0.2 format.

## SSL Format

Each learning has a TYPE and uses SSL compression:

```
[TYPE] [domain] subject→action→result @location
[ε] exact_command_or_code_verbatim
```

## Types (choose most specific)

| Type | Use for |
|------|---------|
| [SOLUTION] | What worked: commands, fixes, approaches that succeeded |
| [GOTCHA] | Traps: counterintuitive behavior, silent failures, edge cases |
| [DECISION] | Design choices: why X over Y, tradeoffs considered |
| [PATTERN] | Reusable techniques: approaches that generalize |
| [PREFERENCE] | User preferences: workflow, style, communication |
| [FAILURE] | What did not work and why (valuable negative knowledge) |

## SSL Symbols

| Symbol | Meaning | Example |
|--------|---------|---------|
| → | produces/leads to | cmake→build→binary |
| \| | or/alternative | patch\|minor\|major |
| + | with/and | config+flags |
| @ | location | @simple_cli.cpp:720 |
| ! | negation | →!working |
| ? | uncertainty | regulates? |

## Relationships

```
[TRIPLET] subject predicate object
```

Use for: calls, uses, contains, implements, depends_on, derived_from

## Rules

1. **Preserve verbatim**: Commands, code, formulas, thresholds go in [ε] lines
2. **Compress prose**: Convert explanations to SSL arrows
3. **Be specific**: Include file paths, line numbers, exact values
4. **No fluff**: Skip obvious/trivial learnings
5. **High signal**: Each learning should be reconstructable from SSL alone

## Good Examples

```
[SOLUTION] [chitta] parallel-build→4x-faster @cmake
[ε] cmake --build build --parallel

[GOTCHA] [daemon] thread_pool→blocks-if-handler-throws @simple_cli.cpp:776
[ε] wrap handler.handle() in try-catch, return error JSON

[DECISION] [rpc] async-response-queue→eventfd-wake→poll-returns-immediately
[ε] write(wake_fd_, &val, sizeof(val)) after queue_response()

[PATTERN] [hooks] fire-and-forget→queue-file→daemon-processes-async
[ε] echo json >> /tmp/chitta-queue.jsonl

[PREFERENCE] [partnership] Antonio→no-shortcuts+proper-solutions-only

[FAILURE] [http] http-daemon→too-slow-for-hooks→switched-to-unix-socket
[ε] PreToolUse needs <50ms, HTTP added 200ms latency

[TRIPLET] ThreadPool contains worker_loop
[TRIPLET] daemon uses ThreadPool
[TRIPLET] health_check bypasses ThreadPool
```

## Bad (avoid)

- Generic summaries without specifics
- Learnings without [ε] when code/commands are involved
- Obvious things (e.g., "files should be saved")
- Duplicating what is already in code comments

---

CONVERSATION:
'"$CONVERSATION"'

---

Output ONLY SSL-formatted learnings (no explanations, no markdown headers):'

echo "[distill] Session $SESSION_ID: Calling OpenCode ($MODEL)..."

# Call with timeout (2 min max)
RESULT=$(echo "$PROMPT" | timeout 120 opencode run -m "$MODEL" 2>/dev/null || echo "")

if [[ -z "$RESULT" ]]; then
    echo "[distill] No result from OpenCode" >&2
    exit 0
fi

echo "[distill] Processing SSL results..."

# Create episode marker for this distillation
EPISODE_ID=$("$CHITTA_BIN" grow --type episode --content "Session $SESSION_ID distilled" --realm "$REALM" --json 2>/dev/null | grep -oP '"id"\s*:\s*"\K[^"]+' || echo "")
[[ -n "$EPISODE_ID" ]] && echo "[distill]   Episode: $EPISODE_ID"

# Process markers - combine TYPE line with following [ε] lines
STORED=0
TRIPLETS=0
CURRENT_TYPE=""
CURRENT_CONTENT=""

store_current() {
    [[ -z "$CURRENT_TYPE" || -z "$CURRENT_CONTENT" ]] && return

    local cat
    case "$CURRENT_TYPE" in
        SOLUTION) cat="solution" ;;
        GOTCHA) cat="gotcha" ;;
        DECISION) cat="decision" ;;
        PATTERN) cat="pattern" ;;
        PREFERENCE) cat="preference" ;;
        FAILURE) cat="failure" ;;
        *) cat="wisdom" ;;
    esac

    # Extract title (first line, truncated)
    local title="${CURRENT_CONTENT%%$'\n'*}"
    title="${title:0:100}"

    # Store via observe (creates memory with proper category)
    local resp=$("$CHITTA_BIN" observe --category "$cat" --title "$title" --content "$CURRENT_CONTENT" --realm "$REALM" --json 2>/dev/null || echo "")

    if echo "$resp" | grep -q '"id"'; then
        local id=$(echo "$resp" | grep -oP '"id"\s*:\s*"\K[^"]+' | head -1)
        echo "[distill]   +${CURRENT_TYPE,,}: ${title:0:60}..."
        ((STORED++)) || true

        # Link to episode if we have one
        if [[ -n "$EPISODE_ID" && -n "$id" ]]; then
            "$CHITTA_BIN" connect --subject "$id" --predicate "derived_from" --object "$EPISODE_ID" 2>/dev/null || true
        fi
    fi

    CURRENT_TYPE=""
    CURRENT_CONTENT=""
}

# Parse line by line
while IFS= read -r line || [[ -n "$line" ]]; do
    # Skip empty lines and markdown artifacts
    [[ -z "$line" ]] && continue
    [[ "$line" == '```'* ]] && continue
    [[ "$line" == '---' ]] && continue

    # Check for typed marker line
    if [[ "$line" =~ ^\[(SOLUTION|GOTCHA|DECISION|PATTERN|PREFERENCE|FAILURE)\][[:space:]] ]]; then
        # Store previous if any
        store_current
        CURRENT_TYPE="${BASH_REMATCH[1]}"
        CURRENT_CONTENT="${line#\[$CURRENT_TYPE\] }"

    # Check for epsilon (verbatim) line - append to current
    elif [[ "$line" == "[ε]"* && -n "$CURRENT_TYPE" ]]; then
        CURRENT_CONTENT+=$'\n'"$line"

    # Check for triplet relationship
    elif [[ "$line" =~ ^\[TRIPLET\][[:space:]] ]]; then
        store_current
        parts="${line#\[TRIPLET\] }"
        subj=$(echo "$parts" | awk '{print $1}')
        pred=$(echo "$parts" | awk '{print $2}')
        obj=$(echo "$parts" | awk '{$1=$2=""; print $0}' | xargs)

        if [[ -n "$subj" && -n "$pred" && -n "$obj" ]]; then
            if "$CHITTA_BIN" connect --subject "$subj" --predicate "$pred" --object "$obj" 2>/dev/null; then
                echo "[distill]   triplet: $subj→$pred→$obj"
                ((TRIPLETS++)) || true
            fi
        fi

    # Continuation line for current marker (no prefix)
    elif [[ -n "$CURRENT_TYPE" && ! "$line" =~ ^\[ ]]; then
        CURRENT_CONTENT+=$'\n'"$line"
    fi
done <<< "$RESULT"

# Store final marker if pending
store_current

echo "[distill] Session $SESSION_ID: Done (+$STORED learnings, $TRIPLETS triplets)"
