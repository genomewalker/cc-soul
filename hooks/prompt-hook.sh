#!/bin/bash
# UserPromptSubmit hook: Surface relevant memories + detect learning opportunities
#
# HIGH PERFORMANCE: Single call with smart routing
# - Uses structured_recall (three-lens: facts/context/temporal, falls back to smart_recall)
# - Handles temporal, aspect, entity, code, and exploratory queries
# - Minimum 30% confidence threshold
# - Detects patterns for proactive learning

# Don't use set -e: we want hooks to succeed even if some parts fail

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"
MIN_CONFIDENCE=30
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"

# Source shared library
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh"

# Parse input - Claude Code sends JSON with session_id and prompt (gracefully handle malformed input)
INPUT=$(cat)
# Try to extract session_id from JSON first (most reliable source)
SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // empty' 2>/dev/null || echo "")
# Fall back to registry lookup if not in JSON
[[ -z "$SESSION_ID" ]] && SESSION_ID=$(get_session_id)
[[ -z "$SESSION_ID" ]] && SESSION_ID="default"

# Try to extract prompt from JSON, fall back to raw input if not JSON
QUERY=$(echo "$INPUT" | jq -r '.prompt // empty' 2>/dev/null || echo "")
[[ -z "$QUERY" ]] && QUERY="$INPUT"
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty' 2>/dev/null || echo "")

[[ -z "$QUERY" ]] && exit 0
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Strip system markup (task-notifications, system-reminders, command blocks) to get real user intent.
# When a message is purely system markup (e.g. task-notification firing UserPromptSubmit),
# skip all processing — no soul context needed.
CLEAN_QUERY=$(echo "$QUERY" | python3 -c "
import sys, re
text = sys.stdin.read()
text = re.sub(r'<(task-notification|system-reminder|command-name|command-message|local-command-\w+)[^>]*>.*?</\1>', '', text, flags=re.DOTALL|re.IGNORECASE)
print(text.strip())
" 2>/dev/null || echo "$QUERY")
[[ -z "$CLEAN_QUERY" ]] && exit 0

# Save cleaned message for Stop hook compliance detection (no system markup pollution)
mkdir -p "$MIND_PATH"
echo "$CLEAN_QUERY" > "$MIND_PATH/.last_user_message"

# Get turn index (locked read+increment via lib.sh)
TURN_INDEX=$(get_next_turn "$SESSION_ID")

# Store user turn in lossless conversation storage (uses lib.sh queue_write with ack_id)
queue_write "store_turn" "{\"session_id\":\"$SESSION_ID\",\"role\":\"user\",\"content\":$(echo "$QUERY" | jq -Rs .),\"turn_index\":$TURN_INDEX}"

# Use clean query for all recall and pattern detection (strip markup from QUERY)
QUERY="$CLEAN_QUERY"

# ===========================================
# CACHE EXPIRY WARNING: Warn when >5 min idle
# ===========================================
LAST_STOP_FILE="${MIND_PATH}/.last_stop_time"
if [[ -f "$LAST_STOP_FILE" ]]; then
    LAST_STOP=$(cat "$LAST_STOP_FILE" 2>/dev/null || echo 0)
    NOW=$(date +%s)
    GAP=$(( NOW - LAST_STOP ))
    if [[ $GAP -gt 300 ]]; then
        GAP_MIN=$(( GAP / 60 ))
        echo "[cache-expired: ${GAP_MIN}m idle — full context re-prices at cache-write rates; run /compact or start new session with /recap]"
    fi
fi

# ===========================================
# SESSION SIZE WARNING: Warn when transcript is bloating
# ===========================================
if [[ -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" ]]; then
    TRANSCRIPT_SIZE=$(stat -c%s "$TRANSCRIPT_PATH" 2>/dev/null || echo 0)
    TRANSCRIPT_MB=$(( TRANSCRIPT_SIZE / 1048576 ))
    # >50MB = expensive session, warn every prompt
    if [[ $TRANSCRIPT_MB -gt 50 ]]; then
        echo "[context-bloat: ${TRANSCRIPT_MB}MB transcript — consider /compact or start fresh with /recap to reduce cache-write costs]"
    # >20MB = getting large, warn once
    elif [[ $TRANSCRIPT_MB -gt 20 && ! -f "$MIND_PATH/.size_warned_${SESSION_ID}" ]]; then
        echo "[context-growing: ${TRANSCRIPT_MB}MB — /compact saves cache-write tokens; /recap starts lean]"
        touch "$MIND_PATH/.size_warned_${SESSION_ID}" 2>/dev/null || true
    fi
fi

# Skip daemon-dependent operations immediately if daemon is not running.
# queue_write above is file-based (no daemon needed), everything below requires it.
daemon_available || exit 0

# ===========================================
# TURN-BASED CHECKPOINT: Save ledger every N turns
# ===========================================
CHECKPOINT_INTERVAL="${CC_SOUL_CHECKPOINT_INTERVAL:-10}"
if [[ $((TURN_INDEX % CHECKPOINT_INTERVAL)) -eq 0 && $TURN_INDEX -gt 0 ]]; then
    # Detect realm for project
    REALM=$(timeout 1 "$CHITTA_BIN" realm_detect 2>/dev/null | grep -oP 'realm": "\K[^"]+' || echo "brahman")

    CHECKPOINT_ARGS=$(jq -n \
        --arg session_id "$SESSION_ID" \
        --arg project "$REALM" \
        --arg transcript_path "$TRANSCRIPT_PATH" \
        --arg mood "working" \
        --arg snapshot "Turn $TURN_INDEX checkpoint" \
        '{session_id: $session_id, project: $project, transcript_path: $transcript_path, mood: $mood, snapshot: $snapshot}')

    queue_write "ledger_save" "$CHECKPOINT_ARGS"
    echo "[ledger] checkpoint at turn $TURN_INDEX" >&2

    # Incremental distillation every N turns (fire-and-forget, non-blocking)
    queue_write "distill_trigger" "{\"session_id\":\"$SESSION_ID\"}"
    echo "[distill] queued incremental distillation at turn $TURN_INDEX" >&2

    # Mine thinking blocks for perception-change insights (fast C++ binary, non-blocking)
    THINKING_BIN="${HOME}/.claude/bin/chitta_thinking"
    if [[ -x "$THINKING_BIN" && -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" ]]; then
        "$THINKING_BIN" --transcript "$TRANSCRIPT_PATH" >/dev/null 2>&1 &
    fi
fi

# ===========================================
# REALM DETECTION: Filter recall to current project realm
# ===========================================
REALM=$(timeout 1 "$CHITTA_BIN" realm_detect 2>/dev/null | grep -oP 'realm": "\K[^"]+' || echo "brahman")

# ===========================================
# MEMORY RETRIEVAL: Choose strategy based on mode
# ===========================================
# RLM mode: Use soul_repl for dynamic exploration (more powerful but slower)
# Standard mode: Use structured_recall (three-lens: facts/context/temporal)
RLM_MODE="${CC_SOUL_RLM_MODE:-}"

if [[ -n "$RLM_MODE" ]]; then
    # RLM-style exploration via Python soul_repl
    memories=$(timeout "$MAX_WAIT" python3 -c "
import sys
sys.path.insert(0, '${SCRIPT_DIR}/../chitta-mcp')
from server import handle_smart_context
result = handle_smart_context({'mode': 'rlm', 'task': '''$QUERY'''})
print(result)
" 2>/dev/null || true)
else
    # Run structured recall + hybrid recall in parallel, then merge.
    # Hybrid (BM25 + semantic + graph) catches literal tokens (filenames, IDs, paths)
    # that pure semantic recall misses when they have low cosine similarity.
    _sem_out=$(timeout "$MAX_WAIT" "$CHITTA_BIN" structured_recall --query "$QUERY" --limit 8 --realm "$REALM" --include-global true 2>/dev/null || \
               timeout "$MAX_WAIT" "$CHITTA_BIN" smart_recall --query "$QUERY" --limit 6 --realm "$REALM" --include-global true 2>/dev/null || true) &
    _sem_pid=$!
    _hyb_out=$(timeout "$MAX_WAIT" "$CHITTA_BIN" recall --query "$QUERY" --strategy hybrid --limit 5 --realm "$REALM" --include-global true 2>/dev/null || true) &
    _hyb_pid=$!
    # Pure keyword lane: BM25 only, surfaces exact tokens (filenames, IDs, paths)
    # regardless of semantic similarity. Use --toon for consistent parseable output.
    _kw_out=$(timeout "$MAX_WAIT" "$CHITTA_BIN" recall --query "$QUERY" --strategy keyword \
              --limit 3 --toon --realm "$REALM" --include-global true 2>/dev/null || true) &
    _kw_pid=$!
    wait "$_sem_pid" "$_hyb_pid" "$_kw_pid" 2>/dev/null || true

    # Keyword lane TOON format: results[N]{...}: id,realm,relevance,similarity,text,type
    # Extract text field and reformat as [hyb][50%] [type] text
    _kw_fmt=$(printf '%s\n' "$_kw_out" | grep -v '^\(realm:\|results\[' | \
              sed 's/^ [0-9]*,[0-9]*,[^,]*,[^,]*,[^,]*,"\(.*\)",\([a-z]*\)$/[hyb][50%] [\2] \1/' | \
              grep '^\[hyb\]' | grep -v '\[thought\]' || true)

    # Merge: prefix hybrid lines with [hyb] marker so filter can use lower threshold.
    # Strip [thought] from hybrid+keyword lanes — soul:meta artifacts, not domain knowledge.
    memories=$(printf '%s\n' "$_sem_out"; \
               printf '%s\n' "$_hyb_out" | grep -v '\[thought\]' | sed 's/^\[/[hyb]/'; \
               printf '%s\n' "$_kw_fmt")
fi

if [[ -z "$memories" || "$memories" == *"No memories"* ]]; then
    exit 0
fi

# Filter and format results
OUTPUT=""
COUNT=0
while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    # Match both formats: "[79%]..." (full_resonate) and "#123 [kind] [79%]..." (smart_recall)
    [[ ! "$line" =~ \[[0-9]+%\] ]] && continue

    # Extract confidence from anywhere in line
    conf=$(echo "$line" | grep -oE '\[[0-9]+%\]' | head -1 | tr -d '[]%')
    [[ -z "$conf" ]] && continue

    # Hybrid-lane results use BM25 keyword matching — allow lower confidence threshold
    # (they surface literal tokens: filenames, IDs, paths that semantic misses)
    if [[ "$line" == \[hyb\]* ]]; then
        [[ "$conf" -lt 1 ]] && continue
        line="${line#\[hyb\]}"  # strip marker before output
    else
        [[ "$conf" -lt "$MIN_CONFIDENCE" ]] && continue
    fi

    # Skip episode thinking-blocks — internal reasoning traces, never useful as injected context
    [[ "$line" =~ \[episode\].*\[thinking.block: ]] && continue

    # Skip [thought] synthesis memories — soul:meta artifacts, not domain knowledge.
    # Soul cycles that need [thought] memories query soul:meta explicitly.
    [[ "$line" =~ \[thought\] ]] && continue

    # Code symbol filtering is now done server-side via --partnership-only flag

    # Truncate and add
    OUTPUT="$OUTPUT${line:0:150}
"
    ((++COUNT))
    [[ $COUNT -ge 3 ]] && break
done <<< "$memories"

# ===========================================
# SUS: Log memory exposures for utility scoring (fire-and-forget)
# ===========================================
(
    _sus_ids="["
    _sus_ranks="["
    _sus_scores="["
    _sus_first=true
    _sus_rank=0

    while IFS= read -r _sus_line; do
        [[ -z "$_sus_line" ]] && continue
        [[ ! "$_sus_line" =~ \[[0-9]+%\] ]] && continue

        # Extract memory ID: #NNN at start of line
        if [[ "$_sus_line" =~ ^#([0-9]+) ]]; then
            _sus_mid="${BASH_REMATCH[1]}"
        else
            continue
        fi

        # Extract confidence percentage
        _sus_pct=$(echo "$_sus_line" | grep -oE '\[[0-9]+%\]' | head -1 | tr -d '[]%')
        [[ -z "$_sus_pct" ]] && continue
        [[ "$_sus_pct" -lt "$MIN_CONFIDENCE" ]] && continue

        (( _sus_rank++ ))
        if [[ "$_sus_first" == "true" ]]; then
            _sus_first=false
        else
            _sus_ids+=","
            _sus_ranks+=","
            _sus_scores+=","
        fi
        _sus_ids+="$_sus_mid"
        _sus_ranks+="$_sus_rank"
        _sus_scores+="$(awk "BEGIN{printf \"%.2f\", $_sus_pct/100}")"

        [[ $_sus_rank -ge 3 ]] && break
    done <<< "$memories"

    _sus_ids+="]"
    _sus_ranks+="]"
    _sus_scores+="]"

    if [[ "$_sus_ids" != "[]" && -n "$SESSION_ID" ]]; then
        queue_write "log_exposure" "{\"session_id\":\"$SESSION_ID\",\"turn_id\":$TURN_INDEX,\"hook_type\":\"user_prompt\",\"memory_ids\":$_sus_ids,\"ranks\":$_sus_ranks,\"resonance_scores\":$_sus_scores}"
    fi
) 2>/dev/null || true

# Save exposed correction memory IDs for M metric (stop-hook reads this)
_sus_corr_ids="["
_sus_corr_first=true
_sus_last_mid=""
while IFS= read -r _sus_corr_line; do
    [[ -z "$_sus_corr_line" ]] && continue
    # Track memory IDs from #NNN lines
    if [[ "$_sus_corr_line" =~ ^#([0-9]+) ]]; then
        _sus_last_mid="${BASH_REMATCH[1]}"
        continue
    fi
    # Check if content line has [correction] marker
    if [[ -n "$_sus_last_mid" && "$_sus_corr_line" =~ ^[[:space:]]+\[correction\] ]]; then
        [[ "$_sus_corr_first" == "true" ]] && _sus_corr_first=false || _sus_corr_ids+=","
        _sus_corr_ids+="$_sus_last_mid"
        _sus_last_mid=""
    fi
done <<< "${memories:-}"
_sus_corr_ids+="]"
if [[ "$_sus_corr_ids" != "[]" && -n "${SESSION_ID:-}" && -n "${MIND_PATH:-}" ]]; then
    printf '%s' "$_sus_corr_ids" > "${MIND_PATH}/.exposed_corrections_${SESSION_ID}"
fi

# ===========================================
# PATTERN DETECTION: Detect learning opportunities
# fasttext model when available, regex fallback
# ===========================================
LEARNING_HINTS=""
_CLASSIFIER_MODEL="${MIND_PATH}/hook-classifier.bin"
_CLASSIFIER_THRESHOLD="0.55"
_CLASSIFIER_SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../scripts" 2>/dev/null && pwd)/classify-intent.py"

_DETECTED_INTENT=""
_DETECTED_SCORE=""

if [[ -f "$_CLASSIFIER_MODEL" && -x "$_CLASSIFIER_SCRIPT" ]]; then
    _clf_out=$(echo "$QUERY" | python3 "$_CLASSIFIER_SCRIPT" "$_CLASSIFIER_MODEL" "$_CLASSIFIER_THRESHOLD" 2>/dev/null || true)
    if [[ -n "$_clf_out" ]]; then
        _DETECTED_INTENT="${_clf_out%% *}"
        _DETECTED_SCORE="${_clf_out##* }"
    fi
fi

# Regex fallback for each category (used when model absent OR as safety net)
_regex_correction=0
_regex_preference=0
_regex_belief=0
_regex_milestone=0
_regex_frustration=0

echo "$QUERY" | grep -qiE "(wrong|mistake|not working|incorrect|actually[, ]|that'?s not|you('re| are) (wrong|missing)|I (said|meant|asked)|not what I|won'?t work|should be|not quite|use your memory|check.*memory|did you forget|should not\b|shouldn'?t\b|should never\b|you forgot\b|you missed\b|that breaks\b|wrong order\b|backwards\b|not like that\b|not this way\b|^no[,. ]|first.*then\b|before.*not after\b|kill.*before\b|binary.*first\b)" \
    && _regex_correction=1 || true
echo "$QUERY" | grep -qiE "(I (prefer|like|always|never|don'?t like)|please (don'?t|always|never)|stop doing|keep doing|from now on|in the future|more concise|always use\b|never use\b|don'?t use\b|use .* instead\b|prefer .* over\b|no inline\b|no comments\b|no stubs\b|no placeholders\b)" \
    && _regex_preference=1 || true
echo "$QUERY" | grep -qiE "(I always|we always|I never|we never|our convention|our standard|we typically|we usually|by convention|in this (project|codebase|repo)|the standard (approach|way)|we (always|never) use|our (approach|workflow|setup) is)" \
    && _regex_belief=1 || true
echo "$QUERY" | grep -qiE "(it works|finally|success|done|shipped|released|completed|finished|passed|merged|deployed)" \
    && _regex_milestone=1 || true
echo "$QUERY" | grep -qiE "(frustrated|annoyed|confused|stuck|lost|this is (hard|difficult|confusing)|I give up|help me understand|what am I missing|tedious|repetitive|not sure|overthinking)" \
    && _regex_frustration=1 || true

# Merge: model wins when confident; regex fills gaps
_intent_correction=0
_intent_preference=0
_intent_belief=0
_intent_milestone=0

[[ "$_DETECTED_INTENT" == "correction"  ]] && _intent_correction=1  || true
[[ "$_DETECTED_INTENT" == "preference"  ]] && _intent_preference=1  || true
[[ "$_DETECTED_INTENT" == "belief"      ]] && _intent_belief=1      || true
[[ "$_DETECTED_INTENT" == "milestone"   ]] && _intent_milestone=1   || true
[[ $_regex_correction -eq 1 ]]  && _intent_correction=1  || true
[[ $_regex_preference -eq 1 ]]  && _intent_preference=1  || true
[[ $_regex_belief -eq 1 ]]      && _intent_belief=1      || true
[[ $_regex_milestone -eq 1 ]]   && _intent_milestone=1   || true

# --- ACT on detected intents ---

if [[ $_intent_correction -eq 1 ]]; then
    correction_ctx=$(echo "$QUERY" | head -c 200 | tr '\n' ' ')
    LEARNING_HINTS="[LEARN] ⚠️ CORRECTION detected - call learn_correction NOW
  User said: \"${correction_ctx}\""
    echo "$QUERY" > "$MIND_PATH/.last_correction_context"
    _corr_payload=$(jq -n --arg c "[correction] $correction_ctx" --arg r "$REALM" \
        '{content: $c, category: "correction", realm: $r, tags: ["correction","auto"], visibility: 2}')
    queue_write "observe" "$_corr_payload" 2>/dev/null || true
fi

if [[ $_intent_preference -eq 1 ]]; then
    LEARNING_HINTS="${LEARNING_HINTS:+$LEARNING_HINTS; }[LEARN] Preference detected → use learn_preference tool"
    _pref_ctx=$(echo "$QUERY" | head -c 200 | tr '\n' ' ')
    _pref_payload=$(jq -n --arg c "[preference] $_pref_ctx" --arg r "$REALM" \
        '{content: $c, category: "preference", realm: $r, tags: ["preference","auto"], visibility: 2}')
    queue_write "observe" "$_pref_payload" 2>/dev/null || true
fi

if [[ $_intent_belief -eq 1 ]]; then
    _belief_ctx=$(echo "$QUERY" | head -c 200 | tr '\n' ' ')
    _belief_payload=$(jq -n --arg c "[belief] $_belief_ctx" --arg r "$REALM" \
        '{content: $c, category: "belief", realm: $r, tags: ["belief","auto"], visibility: 2}')
    queue_write "observe" "$_belief_payload" 2>/dev/null || true
fi

if [[ $_regex_frustration -eq 1 ]]; then
    LEARNING_HINTS="${LEARNING_HINTS:+$LEARNING_HINTS; }[LEARN] User state detected → use learn_approach if something helps"
fi

if [[ $_intent_milestone -eq 1 ]]; then
    milestone_text=$(echo "$QUERY" | head -c 300 | tr '\n' ' ')
    queue_write "observe" "{\"content\":\"[milestone] $milestone_text\",\"category\":\"milestone\",\"realm\":\"$REALM\",\"tags\":[\"milestone\"]}"
fi

# ===========================================
# TURN DISCIPLINE: Nudge if too many turns without storing
# Inspired by SAGE's 7-call enforcement. We warn, not block.
# ===========================================
STORE_INTERVAL="${CC_SOUL_STORE_INTERVAL:-15}"
LAST_STORE_FILE="${MIND_PATH}/.last_store_turn_${SESSION_ID}"
# Initialize on first prompt of a session (state file absent = fresh or resumed session)
if [[ ! -f "$LAST_STORE_FILE" ]]; then
    echo "$TURN_INDEX" > "$LAST_STORE_FILE"
fi
last_store_turn=$(cat "$LAST_STORE_FILE" 2>/dev/null || echo "$TURN_INDEX")
turns_since_store=$((TURN_INDEX - last_store_turn))
if [[ $turns_since_store -ge $STORE_INTERVAL && $TURN_INDEX -gt 0 ]]; then
    LEARNING_HINTS="${LEARNING_HINTS:+$LEARNING_HINTS; }[DISCIPLINE] $turns_since_store turns without storing — consider remember/learn_correction/learn_milestone"
fi

# ===========================================
# NARRATIVE: Log user message (always) + get status (periodic)
# ===========================================
NARRATIVE_STATUS=""

# Log user_message event via CLI (fire-and-forget, always runs)
timeout 0.5 "$CHITTA_BIN" gate_init --session_id "$SESSION_ID" >/dev/null 2>&1 || true
summary=$(echo "$QUERY" | head -c 200 | tr '\n' ' ')
timeout 0.5 "$CHITTA_BIN" narrative_log --session_id "$SESSION_ID" --kind "user_message" --summary "$summary" >/dev/null 2>&1 || true

# Token diet: narrative/anticipation/habits/goals only fire every Nth turn
# Saves 4-6 daemon calls and ~200-400 output tokens on non-Nth turns
ENRICH_INTERVAL="${CC_SOUL_ENRICH_INTERVAL:-5}"
ENRICH_TURN=$(( TURN_INDEX % ENRICH_INTERVAL == 0 || TURN_INDEX <= 1 ? 1 : 0 ))

if [[ $ENRICH_TURN -eq 1 ]]; then
    response=$(timeout 1 "$CHITTA_BIN" narrative_status --session_id "$SESSION_ID" --json 2>/dev/null || true)
    if [[ -n "$response" ]]; then
        mode=$(echo "$response" | jq -r '.metadata.mode // "unknown"' 2>/dev/null)
        confidence=$(echo "$response" | jq -r '.metadata.confidence // 0' 2>/dev/null)
        if [[ "$mode" != "unknown" && "$mode" != "null" ]]; then
            conf_pct=$(awk "BEGIN {printf \"%.0f\", $confidence * 100}")
            NARRATIVE_STATUS="[narrative:$mode:$conf_pct%]"
        fi
    fi
fi

# ===========================================
# ANTICIPATION: Predict likely next actions (periodic — token diet)
# ===========================================
ANTICIPATIONS=""
PREDICTIONS_FILE="$MIND_PATH/.last_predictions.json"

# Clear old predictions
rm -f "$PREDICTIONS_FILE" 2>/dev/null

if [[ $ENRICH_TURN -eq 1 ]]; then
    # Call anticipation_filter via CLI to get gated predictions
    response=$(timeout 1 "$CHITTA_BIN" anticipation_filter --session_id "$SESSION_ID" --max 3 --json 2>/dev/null || true)

    if [[ -n "$response" ]]; then
        candidates=$(echo "$response" | jq -r '.metadata.candidates // []' 2>/dev/null)

        if [[ "$candidates" != "[]" && -n "$candidates" ]]; then
            echo "$candidates" > "$PREDICTIONS_FILE"

            while read -r candidate; do
                [[ -z "$candidate" ]] && continue
                prediction=$(echo "$candidate" | jq -r '.prediction // ""' 2>/dev/null)
                source=$(echo "$candidate" | jq -r '.source // "rule"' 2>/dev/null)
                confidence=$(echo "$candidate" | jq -r '.confidence // 0' 2>/dev/null)

                if [[ -n "$prediction" && "$prediction" != "null" ]]; then
                    conf_pct=$(awk "BEGIN {printf \"%.0f\", $confidence * 100}")
                    ANTICIPATIONS="${ANTICIPATIONS}[anticipate:$source:$conf_pct%] ${prediction:0:100}
"
                fi
            done <<< "$(echo "$candidates" | jq -c '.[]' 2>/dev/null)"
        fi
    fi

    # Fall back to old anticipation_predict if no candidates from filter
    if [[ -z "$ANTICIPATIONS" ]]; then
        context=$(echo "$QUERY" | tr '\n' ' ')
        response=$(timeout 1 "$CHITTA_BIN" anticipation_predict --context "$context" --limit 3 --json 2>/dev/null || true)

        if [[ -n "$response" ]]; then
            patterns=$(echo "$response" | jq -r '.metadata.patterns // []' 2>/dev/null)

            if [[ "$patterns" != "[]" && -n "$patterns" ]]; then
                while read -r pattern; do
                    [[ -z "$pattern" ]] && continue
                    freq=$(echo "$pattern" | jq -r '.frequency // 0' 2>/dev/null)
                    success=$(echo "$pattern" | jq -r '.success_count // 0' 2>/dev/null)
                    action=$(echo "$pattern" | jq -r '.action // ""' 2>/dev/null)

                    if [[ "$freq" -gt 2 || "$success" -gt 0 ]] && [[ -n "$action" ]]; then
                        ANTICIPATIONS="${ANTICIPATIONS}[anticipate] ${action:0:100}
"
                    fi
                done <<< "$(echo "$patterns" | jq -c '.[]' 2>/dev/null)"
            fi
        fi
    fi
fi

# ===========================================
# HABITS: Surface strong habits matching context (periodic — token diet)
# ===========================================
HABITS_OUTPUT=""
if [[ $ENRICH_TURN -eq 1 ]]; then
    context=$(echo "$QUERY" | head -c 100 | tr '\n' ' ')
    response=$(timeout 1 "$CHITTA_BIN" habit_match --context "$context" --min_strength 0.7 --json 2>/dev/null || true)

    if [[ -n "$response" ]]; then
        habits_array=$(echo "$response" | jq -c '.metadata.habits // []' 2>/dev/null)
        if [[ "$habits_array" != "[]" && -n "$habits_array" ]]; then
            while read -r habit; do
                [[ -z "$habit" ]] && continue
                response_text=$(echo "$habit" | jq -r '.response // ""' 2>/dev/null)
                strength=$(echo "$habit" | jq -r '.strength // 0' 2>/dev/null)
                if [[ -n "$response_text" && "$response_text" != "null" ]]; then
                    strength_pct=$(awk "BEGIN {printf \"%.0f\", $strength * 100}")
                    HABITS_OUTPUT="${HABITS_OUTPUT}[habit:${strength_pct}%] ${response_text:0:100}
"
                fi
            done <<< "$(echo "$habits_array" | jq -c '.[]' 2>/dev/null)"
        fi
    fi
fi

# ===========================================
# GOALS: Surface active goals for context (periodic — token diet)
# ===========================================
GOALS_OUTPUT=""
if [[ $ENRICH_TURN -eq 1 ]]; then
    response=$(timeout 1 "$CHITTA_BIN" goal_list --status "active" --limit 3 --json 2>/dev/null || true)
    if [[ -n "$response" ]]; then
        goals_array=$(echo "$response" | jq -c '.metadata.goals // []' 2>/dev/null)
        if [[ "$goals_array" != "[]" && -n "$goals_array" ]]; then
            while read -r goal; do
                [[ -z "$goal" ]] && continue
                id=$(echo "$goal" | jq -r '.id // ""' 2>/dev/null)
                title=$(echo "$goal" | jq -r '.title // ""' 2>/dev/null)
                progress=$(echo "$goal" | jq -r '.progress // 0' 2>/dev/null)
                if [[ -n "$title" && "$title" != "null" ]]; then
                    progress_pct=$(awk "BEGIN {printf \"%.0f\", $progress * 100}")
                    GOALS_OUTPUT="${GOALS_OUTPUT}[goal:${id}] ${title:0:80} (${progress_pct}%)
"
                fi
            done <<< "$(echo "$goals_array" | jq -c '.[]' 2>/dev/null)"
        fi
    fi
fi

# ===========================================
# CURIOSITY: Surface unresolved knowledge gaps (once per session)
# ===========================================
CURIOSITY_OUTPUT=""
if [[ ! -f "$MIND_PATH/.gaps_surfaced" ]]; then
    touch "$MIND_PATH/.gaps_surfaced"
    response=$(timeout 1 "$CHITTA_BIN" curiosity_gaps --limit 1 --json 2>/dev/null || true)
    if [[ -n "$response" ]]; then
        gaps_array=$(echo "$response" | jq -c '.metadata.gaps // []' 2>/dev/null)
        if [[ "$gaps_array" != "[]" && -n "$gaps_array" ]]; then
            while read -r gap; do
                [[ -z "$gap" ]] && continue
                content=$(echo "$gap" | jq -r '.content // ""' 2>/dev/null)
                if [[ -n "$content" && "$content" != "null" ]]; then
                    # Extract just the gap text, strip [gap] prefix if present
                    gap_text=$(echo "$content" | sed 's/^\[gap\] //' | head -c 150 | tr '\n' ' ')
                    CURIOSITY_OUTPUT="[curiosity] Unresolved: ${gap_text}"
                    break  # Only show one gap per session
                fi
            done <<< "$(echo "$gaps_array" | jq -c '.[]' 2>/dev/null)"
        fi
    fi
fi

# ===========================================
# SESSION CONTINUITY: Surface last session for context
# ===========================================
if [[ ! -f "$MIND_PATH/.session_active" ]]; then
    touch "$MIND_PATH/.session_active"
    # Surface last session summary for continuity
    recent_session=$(timeout 1 "$CHITTA_BIN" recall --query "session_summary" --limit 1 2>/dev/null || true)
    if [[ -n "$recent_session" && "$recent_session" != *"No memories"* ]]; then
        # Extract just the first relevant line
        session_line=$(echo "$recent_session" | grep -v '^$' | head -1 | head -c 150)
        [[ -n "$session_line" ]] && ANTICIPATIONS="${ANTICIPATIONS}[last-session] ${session_line}
"
    fi
fi

# ===========================================
# CROSS-SESSION MESSAGING: Heartbeat and inbox check
# ===========================================
CROSS_SESSION_MSGS=""
# Use SESSION_ID from JSON input (already extracted above) - most reliable source
MSG_SESSION_ID="$SESSION_ID"
if [[ -n "$MSG_SESSION_ID" && "$MSG_SESSION_ID" != "default" ]]; then
    # Session register (upserts PID on each prompt - handles Claude restarts/resumes)
    # PPID is Claude's PID (hook runs as: Claude → bash → hook script)
    CLAUDE_PID=${PPID:-$$}
    timeout 0.3 "$CHITTA_BIN" session_register --session_id "$MSG_SESSION_ID" --realm "${REALM:-brahman}" --pid "$CLAUDE_PID" >/dev/null 2>&1 || true

    # Check notification file from msg-notify daemon first (fast-poll messages)
    NOTIFY_FILE="${MIND_PATH}/.msg_notify/${MSG_SESSION_ID}"
    if [[ -f "$NOTIFY_FILE" && -s "$NOTIFY_FILE" ]]; then
        FAST_MSGS=$(cat "$NOTIFY_FILE" 2>/dev/null || true)
        > "$NOTIFY_FILE"  # Clear after reading
        if [[ -n "$FAST_MSGS" ]]; then
            CROSS_SESSION_MSGS="${FAST_MSGS}"
        fi
    fi

    # Check for cross-session messages via daemon (authoritative)
    response=$(timeout 1 "$CHITTA_BIN" msg_inbox --session_id "$MSG_SESSION_ID" --limit 3 --min_priority 1 --json 2>/dev/null || true)
    if [[ -n "$response" ]]; then
        msg_count=$(echo "$response" | jq -r '.count // 0' 2>/dev/null)
        if [[ "$msg_count" -gt 0 ]]; then
            # Format messages based on priority:
            # priority 3 = [MSG:URGENT:sender], priority 2 = [MSG:important:sender], else = [msg:sender]
            CROSS_SESSION_MSGS=$(echo "$response" | jq -r '.messages[] |
                (.sender_session_id | if . == "" or . == null then "unknown" else .[0:8] end) as $sender |
                (.sender_realm      | if . == "" or . == null then "" else "@\(.)" end) as $realm |
                (.sender_host       | if . == "" or . == null then "" else "/\(.)" end) as $host |
                (.memory_id | tostring) as $mid |
                "\($sender)\($realm)\($host)" as $from |
                if .score >= 3 then "[MSG:URGENT:\($from)|id:\($mid)] \(.content)"
                elif .score >= 2 then "[MSG:important:\($from)|id:\($mid)] \(.content)"
                else "[msg:\($from)|id:\($mid)] \(.content)"
                end' 2>/dev/null || true)
            # Ack all messages that were just read
            echo "$response" | jq -r '.messages[].memory_id' 2>/dev/null | while read -r mid; do
                [[ -n "$mid" && "$mid" != "null" ]] && timeout 0.3 "$CHITTA_BIN" msg_ack --message_id "$mid" >/dev/null 2>&1 || true
            done
        fi
    fi
fi

# ===========================================
# OUTPUT — Priority-ordered, budget-capped (~800 chars ≈ ~200 tokens)
# ===========================================
# Token diet: accumulate into FINAL_OUTPUT, enforce hard cap.
# Priority order (highest first): learning hints > cross-session msgs >
# memories > narrative > goals > curiosity > habits > anticipations
MAX_OUTPUT_CHARS="${CC_SOUL_MAX_OUTPUT_CHARS:-800}"
FINAL_OUTPUT=""

_append() {
    local text="$1"
    [[ -z "$text" ]] && return
    local current_len=${#FINAL_OUTPUT}
    local remaining=$((MAX_OUTPUT_CHARS - current_len))
    [[ $remaining -le 20 ]] && return  # Not enough room for anything useful
    if [[ ${#text} -gt $remaining ]]; then
        text="${text:0:$remaining}"
    fi
    FINAL_OUTPUT="${FINAL_OUTPUT}${text}"
}

# 1. Learning hints (action items — highest priority)
_append "$LEARNING_HINTS"
[[ -n "$LEARNING_HINTS" ]] && _append $'\n'

# 2. Cross-session messages (time-sensitive)
if [[ -n "$CROSS_SESSION_MSGS" ]]; then
    _append "[cross-session messages]"$'\n'
    _append "$CROSS_SESSION_MSGS"$'\n'
    _append "[/cross-session messages]"$'\n'
fi

# 3. Memories (core value)
# Save full exposed memory content for implicit resonance detection (stop-hook)
if [[ ${COUNT:-0} -gt 0 && -n "${memories:-}" && -n "${MIND_PATH:-}" && -n "${SESSION_ID:-}" ]]; then
    printf '%s\n' "$memories" > "${MIND_PATH}/.exposed_memories_${SESSION_ID}"
fi
if [[ -n "$OUTPUT" && $COUNT -gt 0 ]]; then
    _append "[soul]"$'\n'
    _append "$OUTPUT"
fi

# 4. Narrative status (one-liner, cheap)
[[ -n "$NARRATIVE_STATUS" ]] && _append "${NARRATIVE_STATUS}"$'\n'

# 5. Goals
[[ -n "$GOALS_OUTPUT" ]] && _append "$GOALS_OUTPUT"

# 6. Curiosity
[[ -n "$CURIOSITY_OUTPUT" ]] && _append "${CURIOSITY_OUTPUT}"$'\n'

# 7. Habits
[[ -n "$HABITS_OUTPUT" ]] && _append "$HABITS_OUTPUT"

# 8. Anticipations (lowest priority)
[[ -n "$ANTICIPATIONS" ]] && _append "$ANTICIPATIONS"

# ===========================================
# EMIT: Structured JSON hookSpecificOutput
# JSON output → Claude Code creates hook_additional_context attachment
# (managed independently during compaction, not just system-reminder text)
# Plain text fallback if jq unavailable
# ===========================================
if [[ -n "$FINAL_OUTPUT" ]]; then
    # Strip trailing whitespace
    FINAL_OUTPUT=$(echo -n "$FINAL_OUTPUT" | sed 's/[[:space:]]*$//')

    if command -v jq &>/dev/null; then
        # Emit as JSON hookSpecificOutput for UserPromptSubmit
        # systemMessage: urgent items shown as warning to user (corrections)
        # additionalContext: everything else, injected as context attachment
        SYSTEM_MSG=""
        if [[ -n "$LEARNING_HINTS" && "$LEARNING_HINTS" == *"CORRECTION"* ]]; then
            SYSTEM_MSG="$LEARNING_HINTS"
        fi

        JSON_OUT=$(jq -n \
            --arg ctx "$FINAL_OUTPUT" \
            --arg sys "$SYSTEM_MSG" \
            '{
                hookSpecificOutput: {
                    hookEventName: "UserPromptSubmit",
                    additionalContext: $ctx
                }
            } + (if $sys != "" then {systemMessage: $sys} else {} end)' 2>/dev/null)

        if [[ -n "$JSON_OUT" && "$JSON_OUT" == "{"* ]]; then
            echo "$JSON_OUT"
        else
            # jq failed at runtime — fall back to plain text
            echo "$FINAL_OUTPUT"
        fi
    else
        # Fallback: plain text (starts without {, so Claude Code treats as plain text)
        echo "$FINAL_OUTPUT"
    fi
fi

exit 0
