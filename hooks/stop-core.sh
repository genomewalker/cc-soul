#!/bin/bash
# Stop hook: Extract typed learnings, handle feedback, checkpoint
#
# HIGH PERFORMANCE: Uses queue for write ops (no blocking)
# STORES IN SSL FORMAT for better recall
#
# Learning types extracted:
#   [SOLUTION], [GOTCHA], [PREFERENCE], [DECISION], [FAILURE], [PATTERN], [LEARN]

# Don't use set -e: we want hooks to succeed even if some parts fail

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"

# Source shared library (provides queue_write with ack_id, get_queue_file, etc.)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh"

METRICS_FILE="${MIND_PATH}/.hook_metrics.json"
ALERT_FILE="${MIND_PATH}/.hook_alerts.log"
mkdir -p "$MIND_PATH" 2>/dev/null || true

safe_queue_write() {
    local tool="$1"
    local args="$2"
    queue_write "$tool" "$args" && return 0
    sleep 0.05
    queue_write "$tool" "$args"
}

record_ingest_metric() {
    local success="$1" # true|false
    [[ -f "$METRICS_FILE" ]] || printf '%s\n' '{"turns_total":0,"turns_ingested":0}' > "$METRICS_FILE"
    if [[ "$success" == "true" ]]; then
        jq '.turns_total += 1 | .turns_ingested += 1' "$METRICS_FILE" > "${METRICS_FILE}.tmp" 2>/dev/null || true
    else
        jq '.turns_total += 1' "$METRICS_FILE" > "${METRICS_FILE}.tmp" 2>/dev/null || true
    fi
    if [[ -s "${METRICS_FILE}.tmp" ]]; then
        mv "${METRICS_FILE}.tmp" "$METRICS_FILE"
    else
        rm -f "${METRICS_FILE}.tmp"
    fi

    local total ingested pct
    total=$(jq -r '.turns_total // 0' "$METRICS_FILE" 2>/dev/null || echo 0)
    ingested=$(jq -r '.turns_ingested // 0' "$METRICS_FILE" 2>/dev/null || echo 0)
    if [[ "$total" -gt 0 ]]; then
        pct=$(( ingested * 100 / total ))
        if [[ "$total" -ge 20 && "$pct" -lt 95 ]]; then
            printf '%s ingest_rate=%s%% turns=%s ingested=%s\n' "$(date -Is)" "$pct" "$total" "$ingested" >> "$ALERT_FILE"
        fi
    fi
}

# Parse JSON input (gracefully handle malformed input)
INPUT=$(cat)
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty' 2>/dev/null || echo "")
STOP_HOOK_ACTIVE=$(echo "$INPUT" | jq -r '.stop_hook_active // false' 2>/dev/null || echo "false")
SESSION_ID_INPUT=$(echo "$INPUT" | jq -r '.session_id // empty' 2>/dev/null || echo "")

# Set SESSION_ID once (used throughout this hook).
# Prefer explicit session_id, then transcript basename for Codex rollouts.
SESSION_ID="$SESSION_ID_INPUT"
if [[ -z "$SESSION_ID" && -n "$TRANSCRIPT_PATH" ]]; then
    _tp_base=$(basename "$TRANSCRIPT_PATH" .jsonl 2>/dev/null || true)
    [[ -n "$_tp_base" && "$_tp_base" != "." ]] && SESSION_ID="$_tp_base"
fi
[[ -z "$SESSION_ID" ]] && SESSION_ID="unknown"

# Kill msg-notify daemon immediately — before any early-exit paths
# Use both PID file and process pattern to catch orphans
if [[ -n "$SESSION_ID" && "$SESSION_ID" != "default" ]]; then
    MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
    PID_FILE="${MIND_PATH}/.msg_notify_pids/${SESSION_ID}.pid"
    if [[ -f "$PID_FILE" ]]; then
        old_pid=$(cat "$PID_FILE" 2>/dev/null || true)
        [[ -n "$old_pid" ]] && kill "$old_pid" 2>/dev/null || true
        rm -f "$PID_FILE"
    fi
    # Also kill by pattern — catches orphans whose PID file was lost
    pkill -f "msg-notify.*${SESSION_ID}" 2>/dev/null || true
fi

# Prevent infinite loops
[[ "$STOP_HOOK_ACTIVE" == "true" ]] && exit 0
[[ -z "$TRANSCRIPT_PATH" || ! -f "$TRANSCRIPT_PATH" ]] && exit 0

# ─── 85% context guard ────────────────────────────────────────────────────────
_ctx_used=$(echo "$INPUT" | jq -r '.context_window.used_tokens // 0' 2>/dev/null || echo 0)
_ctx_max=$(echo "$INPUT" | jq -r '.context_window.max_tokens // 0' 2>/dev/null || echo 0)
if [[ "$_ctx_max" -gt 0 ]]; then
    _ctx_pct=$(( _ctx_used * 100 / _ctx_max ))
    _compact_sentinel="${MIND_PATH}/.compact_advised_${SESSION_ID}"
    # Auto-reset: if context dropped below 50% since last block, compact happened — allow re-trigger
    if [[ -f "$_compact_sentinel" && "$_ctx_pct" -lt 50 ]]; then
        rm -f "$_compact_sentinel" 2>/dev/null || true
    fi
    if [[ "$_ctx_pct" -ge 65 && "$_ctx_pct" -lt 85 ]]; then
        if [[ ! -f "$_compact_sentinel" ]]; then
            touch "$_compact_sentinel" 2>/dev/null || true
            echo "{\"decision\":\"block\",\"reason\":\"Context at ${_ctx_pct}% (${_ctx_used}/${_ctx_max} tokens). Run /compact to continue — keeps cache prefix stable and avoids 2-3× per-turn cost inflation. After compacting, work resumes normally.\"}"
            exit 0
        fi
    fi
    if [[ "$_ctx_pct" -ge 85 ]]; then
        _sentinel="${MIND_PATH}/.context85_blocked_${SESSION_ID}"
        if [[ ! -f "$_sentinel" ]]; then
            touch "$_sentinel" 2>/dev/null || true
            _handoff_dir="/tmp/opencode/handoffs"
            mkdir -p "$_handoff_dir"
            cat > "$_handoff_dir/${SESSION_ID}.yaml" <<YAML
timestamp: $(date -u +%Y-%m-%dT%H:%M:%SZ)
session_id: ${SESSION_ID}
context_pct: ${_ctx_pct}
note: Auto-handoff at ≥85% context. Run /recap to continue in a fresh session.
realm: ${REALM:-unknown}
YAML
            echo "{\"decision\":\"block\",\"reason\":\"Context at ≥85% — handoff written to prevent data loss. Run /recap to continue.\"}"
            exit 0
        fi
    fi
fi
# ──────────────────────────────────────────────────────────────────────────────

# Convert to SSL v0.3 format
# Input: category, raw content
# Output: SSL formatted string (v0.3 with domain prefix)
# Note: Uses global $REALM set by realm_detect
to_ssl() {
    local category="$1"
    local content="$2"

    local domain="${REALM:-brahman}"
    local ssl_content

    case "$category" in
        solution)
            ssl_content="[$domain:sol] $content"
            ;;
        gotcha)
            ssl_content="[$domain:gotcha] $content"
            ;;
        preference)
            ssl_content="[partnership:pref] Antonio→$content"
            ;;
        decision)
            ssl_content="[$domain:dec] $content"
            ;;
        failure)
            ssl_content="[$domain:fail] $content"
            ;;
        pattern)
            ssl_content="[$domain:pat] $content"
            ;;
        *)
            ssl_content="[$domain] $content"
            ;;
    esac

    echo "$ssl_content"
}

# Map learning type to category
map_category() {
    case "$1" in
        SOLUTION) echo "solution" ;;
        GOTCHA) echo "gotcha" ;;
        PREFERENCE) echo "preference" ;;
        DECISION) echo "decision" ;;
        FAILURE) echo "failure" ;;
        PATTERN) echo "pattern" ;;
        *) echo "wisdom" ;;
    esac
}

# Transcript helpers: support both JSON array transcripts and Codex JSONL.
transcript_role_text() {
    local role="$1"
    python3 - "$TRANSCRIPT_PATH" "$role" <<'PY'
import json, sys
p, role = sys.argv[1], sys.argv[2]
def text_from_content(content):
    if isinstance(content, list):
        out=[]
        for b in content:
            if isinstance(b, dict) and b.get("type") in ("text","input_text","output_text"):
                t=b.get("text","")
                if t: out.append(t)
        return "\n".join(out)
    if isinstance(content, str):
        return content
    return ""
def emit(role_v, content_v):
    if role_v == role:
        t=text_from_content(content_v).strip()
        if t: print(t)
with open(p, "r", encoding="utf-8", errors="ignore") as f:
    first = f.read(1); f.seek(0)
    if first == "[":
        try:
            arr = json.load(f)
        except Exception:
            arr = []
        for it in arr:
            emit(it.get("role",""), it.get("message",{}).get("content",[]))
    else:
        for line in f:
            line=line.strip()
            if not line: continue
            try:
                d=json.loads(line)
            except Exception:
                continue
            if d.get("type") != "response_item":
                continue
            pl=d.get("payload",{})
            if pl.get("type") != "message":
                continue
            emit(pl.get("role",""), pl.get("content",[]))
PY
}

transcript_tool_names() {
    python3 - "$TRANSCRIPT_PATH" <<'PY'
import json, sys
p=sys.argv[1]
seen=set(); out=[]
def add_tool(name):
    if name and name not in seen:
        seen.add(name); out.append(name)
with open(p, "r", encoding="utf-8", errors="ignore") as f:
    first = f.read(1); f.seek(0)
    if first == "[":
        try: arr=json.load(f)
        except Exception: arr=[]
        for it in arr:
            if it.get("role")!="assistant": continue
            for b in it.get("message",{}).get("content",[]) or []:
                if isinstance(b, dict) and b.get("type")=="tool_use":
                    add_tool(b.get("name",""))
    else:
        for line in f:
            line=line.strip()
            if not line: continue
            try: d=json.loads(line)
            except Exception: continue
            if d.get("type")!="response_item": continue
            pl=d.get("payload",{})
            if pl.get("type")!="function_call": continue
            add_tool(pl.get("name",""))
print("\n".join(out))
PY
}

transcript_tool_files_json() {
    python3 - "$TRANSCRIPT_PATH" <<'PY'
import json, sys
p=sys.argv[1]
seen=[]; seen_set=set()
def add_path(x):
    if not x or not isinstance(x, str): return
    if x in seen_set: return
    seen_set.add(x); seen.append(x)
with open(p, "r", encoding="utf-8", errors="ignore") as f:
    first = f.read(1); f.seek(0)
    if first == "[":
        try: arr=json.load(f)
        except Exception: arr=[]
        for it in arr:
            if it.get("role")!="assistant": continue
            for b in it.get("message",{}).get("content",[]) or []:
                if isinstance(b, dict) and b.get("type")=="tool_use":
                    inp=b.get("input",{}) if isinstance(b.get("input",{}), dict) else {}
                    add_path(inp.get("file_path")); add_path(inp.get("path"))
    else:
        for line in f:
            line=line.strip()
            if not line: continue
            try: d=json.loads(line)
            except Exception: continue
            if d.get("type")!="response_item": continue
            pl=d.get("payload",{})
            if pl.get("type")!="function_call": continue
            args=pl.get("arguments")
            if isinstance(args, str):
                try: args=json.loads(args)
                except Exception: args={}
            if not isinstance(args, dict): args={}
            add_path(args.get("file_path")); add_path(args.get("path"))
print(json.dumps(seen))
PY
}

transcript_role_count() {
    local role="$1"
    python3 - "$TRANSCRIPT_PATH" "$role" <<'PY'
import json, sys
p, role = sys.argv[1], sys.argv[2]
count = 0
def has_text(content):
    if isinstance(content, list):
        for b in content:
            if isinstance(b, dict) and b.get("type") in ("text","input_text","output_text") and b.get("text","").strip():
                return True
        return False
    return isinstance(content, str) and bool(content.strip())
with open(p, "r", encoding="utf-8", errors="ignore") as f:
    first = f.read(1); f.seek(0)
    if first == "[":
        try: arr = json.load(f)
        except Exception: arr = []
        for it in arr:
            if it.get("role","") == role and has_text(it.get("message",{}).get("content",[])):
                count += 1
    else:
        for line in f:
            line=line.strip()
            if not line: continue
            try: d=json.loads(line)
            except Exception: continue
            if d.get("type") != "response_item": continue
            pl=d.get("payload",{})
            if pl.get("type") != "message": continue
            if pl.get("role","") == role and has_text(pl.get("content",[])):
                count += 1
print(count)
PY
}

# Extract last assistant message
RESPONSE=$(transcript_role_text "assistant" | tail -n 1 | head -c 50000)

[[ -z "$RESPONSE" || ${#RESPONSE} -lt 10 ]] && exit 0

# ===========================================
# LOSSLESS STORAGE: Store assistant turn
# ===========================================
# Get turn index from counter file
TURN_INDEX=$(get_next_turn "$SESSION_ID")

# Extract tools used from transcript for this turn
TOOLS_JSON=$(transcript_tool_names | jq -R . | jq -s . 2>/dev/null || echo "[]")
[[ "$TOOLS_JSON" == "null" ]] && TOOLS_JSON="[]"

# Extract files touched
FILES_JSON=$(transcript_tool_files_json 2>/dev/null || echo "[]")
[[ "$FILES_JSON" == "null" ]] && FILES_JSON="[]"

# Check for errors
HAS_ERROR=false
echo "$RESPONSE" | grep -qiE '(error|failed|exception|traceback)' && HAS_ERROR=true

# Store assistant turn
safe_queue_write "store_turn" "{\"session_id\":\"$SESSION_ID\",\"role\":\"assistant\",\"content\":$(echo "$RESPONSE" | jq -Rs .),\"turn_index\":$TURN_INDEX,\"tools_used\":$TOOLS_JSON,\"files_touched\":$FILES_JSON,\"has_error\":$HAS_ERROR}"

# Guaranteed base ingestion for assistant turns (independent of enrichers).
ASSIST_EVENT=$(jq -n \
    --arg sid "$SESSION_ID" \
    --arg r "$RESPONSE" \
    --arg realm_hint "project" \
    --arg t "turn_assistant" \
    '{category:"episode", title:$t, content:("[turn_assistant] sid=" + $sid + " " + $r), confidence:0.9, source:"hook_turn_ingest", evidence:"Stop", realm:$realm_hint}')
if safe_queue_write "observe" "$ASSIST_EVENT"; then
    record_ingest_metric "true"
else
    record_ingest_metric "false"
fi

# Structured LLM extraction is designed in docs/STRUCTURED_EXTRACTOR_DESIGN.md.
# The `distill_turn` op has no daemon-side handler yet, so enqueue is held
# until the C++ side lands. Re-enable once queue_processor.cpp dispatches it.

# Skip daemon-dependent operations if daemon is not running.
# queue_write / store_turn above are file-based and always run.
daemon_available || exit 0

# Detect realm (quick CLI call with short timeout)
REALM=$(timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")

# CEC: log assistant_response event (fire-and-forget)
_cec_resp_outcome=$([ "${HAS_ERROR:-false}" = "true" ] && echo 2 || echo 0)
timeout 0.5 "$CHITTA_BIN" log_event --tool "assistant_response" \
    --entity "$REALM" --outcome "$_cec_resp_outcome" --ts_ms "$(date +%s%3N)" >/dev/null 2>&1 &

# ===========================================
# EVENT-BASED CHECKPOINT: Save on errors or milestones
# ===========================================
EVENT_CHECKPOINT=false
EVENT_MOOD=""
EVENT_SNAPSHOT=""

# Error checkpoint
if [[ "$HAS_ERROR" == "true" ]]; then
    EVENT_CHECKPOINT=true
    EVENT_MOOD="debugging"
    EVENT_SNAPSHOT=$(echo "$RESPONSE" | grep -iE '(error|failed|exception)' | head -3 | head -c 500)
    echo "[ledger] error checkpoint triggered" >&2
fi

# Milestone checkpoint (success patterns)
if echo "$RESPONSE" | grep -qiE '(✓|✅|success|complete|done|shipped|released|merged|tests pass|build succeed)'; then
    EVENT_CHECKPOINT=true
    EVENT_MOOD="confident"
    EVENT_SNAPSHOT=$(echo "$RESPONSE" | grep -iE '(success|complete|done|shipped|released|merged|tests pass|build)' | head -3 | head -c 500)
    echo "[ledger] milestone checkpoint triggered" >&2
fi

if [[ "$EVENT_CHECKPOINT" == "true" ]]; then
    EVENT_ARGS=$(jq -n \
        --arg session_id "$SESSION_ID" \
        --arg project "$REALM" \
        --arg transcript_path "$TRANSCRIPT_PATH" \
        --arg mood "$EVENT_MOOD" \
        --arg snapshot "$EVENT_SNAPSHOT" \
        '{session_id: $session_id, project: $project, transcript_path: $transcript_path, mood: $mood, snapshot: $snapshot}')
    # ── Task ledger: thread inference ───────────────────────────────────────
    _PLUGIN_DIR="${CC_SOUL_PLUGIN_DIR:-$(dirname "$(dirname "$(realpath "${BASH_SOURCE[0]}")")")}"
    _MCP_DIR="$_PLUGIN_DIR/chitta-mcp"
    if [[ -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" ]]; then
        _infer_out=$(timeout 5 python3 "$_MCP_DIR/thread_inference.py"             --transcript "$TRANSCRIPT_PATH" --realm "${REALM:-}" 2>/dev/null || echo "{}")
        _thread_id=$(echo "$_infer_out" | python3 -c             "import sys,json; d=json.load(sys.stdin); print(d.get('thread_id',''))" 2>/dev/null || echo "")
        if [[ -n "$_thread_id" ]]; then
            echo "$_thread_id" > "$MIND_PATH/.current_thread_id" 2>/dev/null || true
        fi
    fi
    # ── End task ledger ───────────────────────────────────────────────────────
    queue_write "ledger_save" "$EVENT_ARGS"
fi

# Quality gate: dedup file for this session
DEDUP_FILE="$MIND_PATH/.stop_dedup_${SESSION_ID}"
touch "$DEDUP_FILE"

# Extract typed learnings → convert to SSL v0.3 → queue with provenance + affect/flags/refs
# TODO(v6): remove this regex block once the `distill_turn` op is wired in
# queue_processor.cpp. See docs/STRUCTURED_EXTRACTOR_DESIGN.md §4 (migration)
# and §6 (MVP scope).
LEARNED=0
while IFS= read -r line; do
    if [[ "$line" =~ ^\[(SOLUTION|GOTCHA|PREFERENCE|DECISION|FAILURE|PATTERN|LEARN|CORRECTION|EVENT)\] ]]; then
        type="${BASH_REMATCH[1]}"
        raw_content="${line#\[$type\] }"
        category=$(map_category "$type")

        # Parse v0.3 annotations before converting to SSL
        parse_ssl_annotations "$raw_content"
        ssl_content=$(to_ssl "$category" "$_SSL_CLEAN")
        title=$(echo "$ssl_content" | head -c 60)
        emit_event "$DEDUP_FILE" "$category" "hook_regex" "$ssl_content" "0.7" "regex match on [$type]" "$REALM" "$_SSL_VALENCE" "$_SSL_AROUSAL" "$_SSL_FLAGS" "$_SSL_REFS"
        [[ $? -eq 0 ]] && { echo "[soul] +${type,,}: $title" >&2; ((LEARNED++)) || true; }
    fi
done <<< "$RESPONSE"

# Extract [USED:id] feedback → strengthen + learn_outcome
# Accepts both numeric IDs and UUID-like strings
while IFS= read -r marker; do
    [[ -z "$marker" ]] && continue
    mem_id="${marker#\[USED:}"
    mem_id="${mem_id%\]}"
    [[ -z "$mem_id" ]] && continue

    # Strengthen the memory (existing behavior)
    queue_write "strengthen" "{\"id\":\"$mem_id\",\"amount\":0.1}"
    # CRITICAL: Record positive usage outcome with fallback (feedback loop must not silently fail)
    if ! "$CHITTA_BIN" learn_outcome --memory-id "$mem_id" --outcome "positive" --context "Memory explicitly marked as helpful via [USED] marker" 2>/dev/null; then
        echo "{\"tool\":\"learn_outcome\",\"args\":{\"memory-id\":\"$mem_id\",\"outcome\":\"positive\",\"context\":\"Memory explicitly marked as helpful via [USED] marker\"},\"ts\":$(date +%s)}" >> "$HOME/.claude/mind/.failed_observations.jsonl"
    fi
    echo "[soul] ↑+ ${mem_id:0:12}..." >&2
done <<< "$(echo "$RESPONSE" | grep -oE '\[USED:[a-zA-Z0-9_-]+\]')"

# ===========================================
# Token diet: expensive analysis only fires every Nth turn
# Saves daemon calls + transcript scans on non-enrichment turns
# ===========================================
STOP_ENRICH_INTERVAL="${CC_SOUL_STOP_ENRICH_INTERVAL:-3}"
STOP_ENRICH_TURN=$(( TURN_INDEX % STOP_ENRICH_INTERVAL == 0 || TURN_INDEX <= 1 ? 1 : 0 ))

# ===========================================
# IMPLICIT RESONANCE: Detect memory usage without [USED] markers (periodic)
# ===========================================
_ir_mem_file="${MIND_PATH}/.exposed_memories_${SESSION_ID}"
if [[ -f "$_ir_mem_file" && $STOP_ENRICH_TURN -eq 1 ]]; then
    # Get last assistant response from transcript for comparison
    _ir_response=$(transcript_role_text "assistant" | tail -n 3 | tr -d '\n' | head -c 2000 || true)

    if [[ -n "$_ir_response" ]]; then
        while IFS= read -r _ir_line; do
            [[ -z "$_ir_line" ]] && continue
            # Extract memory ID
            [[ "$_ir_line" =~ ^#([0-9]+) ]] || continue
            _ir_mid="${BASH_REMATCH[1]}"

            # Skip if already explicitly marked [USED]
            echo "$_ir_response" | grep -qF "[USED:$_ir_mid]" && continue

            # Extract key terms (4+ char words, skip stopwords)
            _ir_terms=$(echo "$_ir_line" | tr -cs '[:alnum:]' '\n' | \
                awk 'length >= 4 && !/^[0-9]+$/ && \
                     tolower($0) !~ /^(this|that|with|from|have|been|will|were|your|when|what|which|about|into|then|than|them|they|more|some|also|each|just|only|very|kind|conf)$/' | \
                head -8)
            [[ -z "$_ir_terms" ]] && continue

            _ir_total=0; _ir_matched=0
            while IFS= read -r _ir_term; do
                [[ -z "$_ir_term" ]] && continue
                (( _ir_total++ ))
                echo "$_ir_response" | grep -qiF "$_ir_term" && (( _ir_matched++ ))
            done <<< "$_ir_terms"

            if [[ $_ir_total -gt 0 ]]; then
                _ir_ratio=$(( _ir_matched * 100 / _ir_total ))
                if [[ $_ir_ratio -ge 50 ]]; then
                    queue_write "strengthen" "{\"id\":\"$_ir_mid\",\"amount\":0.03}"
                fi
            fi
        done < "$_ir_mem_file"
    fi
    rm -f "$_ir_mem_file" 2>/dev/null
fi

# ===========================================
# CURIOSITY GAPS: Detect uncertainty/knowledge gaps
# ===========================================
if echo "$RESPONSE" | grep -qiE "(I don.?t know|I.?m not sure|unclear|couldn.?t (find|determine)|need to check|I.?ll have to look)"; then
    gap_context=$(echo "$RESPONSE" | grep -iE "(I don.?t know|I.?m not sure|unclear|couldn.?t)" | head -1 | head -c 200 | tr '\n' ' ' | sed 's/"/\\"/g')
    [[ -n "$gap_context" ]] && queue_write "curiosity_note_gap" "{\"gap\":$(echo "$gap_context" | jq -Rs .)}"
    echo "[soul] +curiosity-gap detected" >&2
fi

# Extract [TRIPLET] → queue connect
while IFS= read -r line; do
    if [[ "$line" =~ ^\[TRIPLET\] ]]; then
        triplet="${line#\[TRIPLET\] }"
        subj=$(echo "$triplet" | awk '{print $1}')
        pred=$(echo "$triplet" | awk '{print $2}')
        obj=$(echo "$triplet" | awk '{print $3}')
        [[ -n "$subj" && -n "$pred" && -n "$obj" ]] && \
            queue_write "connect" "{\"subject\":\"$subj\",\"predicate\":\"$pred\",\"object\":\"$obj\"}"
    fi
done <<< "$RESPONSE"

# ===========================================
# AUTO-LEARNING: Detect missed learning opportunities
# ===========================================
# Read user's last message (saved by UserPromptSubmit)
LAST_USER_MSG=""
if [[ -f "$MIND_PATH/.last_user_message" ]]; then
    LAST_USER_MSG=$(cat "$MIND_PATH/.last_user_message" 2>/dev/null)
fi

# Check if Claude used a learn_* tool (indicated by tool output patterns)
CLAUDE_LEARNED=false
if echo "$RESPONSE" | grep -qiE '(learn_correction|learn_preference|learn_insight|learn_approach|learn_outcome|learn_milestone|Stored correction|Stored preference|Stored insight|Stored approach|Stored outcome|Stored milestone|Stored memory #|auto-checkpoint)'; then
    CLAUDE_LEARNED=true
fi

# Update last-store turn counter whenever something was stored this turn
if [[ "$CLAUDE_LEARNED" == "true" ]]; then
    echo "$TURN_INDEX" > "${MIND_PATH}/.last_store_turn_${SESSION_ID}"
fi

# If user correction detected but Claude didn't learn, auto-store
if [[ "$CLAUDE_LEARNED" == "false" && -n "$LAST_USER_MSG" ]]; then
    # ===========================================
    # COMPLIANCE TRACKING: Did Claude learn when prompted?
    # ===========================================
    CORRECTION_DETECTED=false
    CORRECTION_TEXT=""

    # 1. Authoritative signal: prompt-hook wrote .last_correction_context
    CORRECTION_CTX_FILE="$MIND_PATH/.last_correction_context"
    if [[ -f "$CORRECTION_CTX_FILE" ]]; then
        CORRECTION_DETECTED=true
        CORRECTION_TEXT=$(cat "$CORRECTION_CTX_FILE" 2>/dev/null)
        echo "[soul] correction source: prompt-hook context file" >&2
    fi

    # 2. Fallback: regex on last user message
    if [[ "$CORRECTION_DETECTED" == "false" ]]; then
        if echo "$LAST_USER_MSG" | grep -qiE "(wrong|mistake|not working|incorrect|actually[, ]|that'?s not|you('re| are) (wrong|missing)|not what I|won'?t work|should be|use your memory|check.*memory|did you forget)"; then
            CORRECTION_DETECTED=true
            CORRECTION_TEXT=$(echo "$LAST_USER_MSG" | head -c 300 | tr '\n' ' ')
            echo "[soul] correction source: regex fallback" >&2
        fi
    fi

    # Auto-store correction if detected but Claude didn't learn
    if [[ "$CORRECTION_DETECTED" == "true" ]]; then
        # Guard: skip content that looks like an LLM system prompt (role assignment).
        # These enter via copy-paste of agenda participant configs and are NOT corrections.
        if echo "$CORRECTION_TEXT" | grep -qiE "^[[:space:]]*(You are (a |an |the )[a-zA-Z]|You are \*\*)"; then
            echo "[soul] skip compliance:auto — content looks like LLM system prompt, not a correction" >&2
            CORRECTION_DETECTED=false
        fi
    fi
    if [[ "$CORRECTION_DETECTED" == "true" ]]; then
        echo "[soul] ⚠️ COMPLIANCE: Correction detected but learn_correction not called" >&2
        ssl_content="[compliance:auto] User correction: $CORRECTION_TEXT"
        emit_event "$DEDUP_FILE" "correction" "hook_compliance" "$ssl_content" "0.95" "compliance detector: user corrected assistant" "$REALM"
        [[ $? -eq 0 ]] && echo "[soul] +auto-correction stored: ${ssl_content:0:60}" >&2
    fi

    # ===========================================
    # SUS M METRIC: Correction outcome evaluation
    # ===========================================
    _m_exposed_file="${MIND_PATH}/.exposed_corrections_${SESSION_ID}"
    if [[ -f "$_m_exposed_file" ]]; then
        _m_corr_ids=$(cat "$_m_exposed_file" 2>/dev/null)
        if [[ -n "$_m_corr_ids" && "$_m_corr_ids" != "[]" ]]; then
            # Was a correction detected this session?
            _m_detected="false"
            _m_corr_text='""'
            if [[ "${CORRECTION_DETECTED:-false}" == "true" ]]; then
                _m_detected="true"
                _m_corr_text=$(printf '%s' "${CORRECTION_TEXT:-}" | head -c 200 | jq -Rs .)
            fi
            while IFS= read -r _m_cid; do
                [[ -z "$_m_cid" ]] && continue
                queue_write "log_correction_outcome" \
                    "{\"session_id\":\"$SESSION_ID\",\"correction_memory_id\":$_m_cid,\"correction_detected\":$_m_detected,\"correction_text\":$_m_corr_text}"
            done <<< "$(echo "$_m_corr_ids" | jq -r '.[]' 2>/dev/null || true)"
        fi
        rm -f "$_m_exposed_file" 2>/dev/null
    fi

    # Direct + meta-preference patterns - detect and log, let distillation format properly
    if echo "$LAST_USER_MSG" | grep -qiE "(I (prefer|like|want|always|never)|please (don'?t|always|never)|from now on|more concise|fewer examples|go deeper|simpler please|don'?t overexplain|be more verbose)"; then
        pref_context=$(echo "$LAST_USER_MSG" | head -c 60 | tr '\n' ' ')
        echo "[soul] preference detected: ${pref_context}..." >&2
    fi

    # If milestone detected, log it - let distillation or explicit learn_milestone handle storage
    if echo "$LAST_USER_MSG" | grep -qiE "(it works|finally|success|shipped|released|completed|finished|passed|merged|deployed)"; then
        milestone_context=$(echo "$LAST_USER_MSG" | head -c 60 | tr '\n' ' ')
        echo "[soul] milestone detected: ${milestone_context}..." >&2
    fi
fi

# ===========================================
# NARRATIVE EVENT LOGGING: Log assistant response and tool uses (periodic)
# ===========================================
SESSION_ID="${SESSION_ID:-unknown}"

# djb2 hash - must match C++ implementation
djb2_hash() {
    local str="$1"
    local hash=5381
    local i c
    for ((i=0; i<${#str}; i++)); do
        c=$(printf '%d' "'${str:$i:1}")
        hash=$(( ((hash << 5) + hash) + c ))
        hash=$((hash & 0xFFFFFFFF))
    done
    echo "$hash"
}

MIND_HASH=$(djb2_hash "$MIND_PATH")
SOCKET_PATH="${CHITTA_SOCKET:-/tmp/chitta-${MIND_HASH}.sock}"

# Log assistant_message event (first line of response) — periodic
if [[ -S "$SOCKET_PATH" && -n "$RESPONSE" && $STOP_ENRICH_TURN -eq 1 ]]; then
    summary=$(echo "$RESPONSE" | grep -v '^$' | grep -v '^\[' | head -1 | head -c 200 | tr '\n' ' ' | sed 's/"/\\"/g')
    [[ -n "$summary" ]] && queue_write "narrative_log" "{\"session_id\":\"$SESSION_ID\",\"kind\":\"assistant_message\",\"summary\":$(echo "$summary" | jq -Rs .)}"

    # Extract and log tool_use events (last 10 unique tools)
    TOOLS_FROM_TRANSCRIPT=$(transcript_tool_names | tail -10 | sort -u)

    while IFS= read -r tool; do
        [[ -z "$tool" ]] && continue
        queue_write "narrative_log" "{\"session_id\":\"$SESSION_ID\",\"kind\":\"tool_use\",\"summary\":\"Used $tool\",\"tool_name\":\"$tool\",\"success\":true}"
    done <<< "$TOOLS_FROM_TRANSCRIPT"

    # Log error events if response contains error indicators
    if echo "$RESPONSE" | grep -qiE '(error|failed|exception|traceback|fatal)'; then
        error_line=$(echo "$RESPONSE" | grep -iE '(error|failed|exception)' | head -1 | head -c 150 | tr '\n' ' ' | sed 's/"/\\"/g')
        [[ -n "$error_line" ]] && queue_write "narrative_log" "{\"session_id\":\"$SESSION_ID\",\"kind\":\"error\",\"summary\":$(echo "$error_line" | jq -Rs .),\"success\":false}"
    fi
fi

# ==============================================
# SUS Phase 3: Extract session token usage (periodic)
# ==============================================
if [[ -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" && $STOP_ENRICH_TURN -eq 1 ]]; then
    _sus3_token_usage=$(grep -F '"type":"assistant"' "$TRANSCRIPT_PATH" 2>/dev/null | \
        jq -s '[.[].message.usage // {}] |
        {
            total_input_tokens: (map(.input_tokens // 0) | add // 0),
            total_output_tokens: (map(.output_tokens // 0) | add // 0),
            total_cache_read: (map(.cache_read_input_tokens // 0) | add // 0),
            total_cache_creation: (map(.cache_creation_input_tokens // 0) | add // 0),
            n_messages: length
        }' 2>/dev/null || echo '{"n_messages":0}')

    _sus3_n_msg=$(echo "$_sus3_token_usage" | jq -r '.n_messages // 0')

    if [[ "${_sus3_n_msg:-0}" -gt 0 ]]; then
        _sus3_tot_in=$(echo "$_sus3_token_usage" | jq -r '.total_input_tokens // 0')
        _sus3_tot_out=$(echo "$_sus3_token_usage" | jq -r '.total_output_tokens // 0')
        _sus3_cache_r=$(echo "$_sus3_token_usage" | jq -r '.total_cache_read // 0')
        _sus3_cache_c=$(echo "$_sus3_token_usage" | jq -r '.total_cache_creation // 0')

        queue_write "log_session_tokens" \
            "{\"session_id\":\"$SESSION_ID\",\"total_input_tokens\":$_sus3_tot_in,\"total_output_tokens\":$_sus3_tot_out,\"cache_read_tokens\":$_sus3_cache_r,\"cache_creation_tokens\":$_sus3_cache_c,\"n_messages\":$_sus3_n_msg}"

        # Cache break detection: warn if ratio < 0.5 and enough messages
        if [[ "${_sus3_n_msg:-0}" -gt 3 ]]; then
            _sus3_denom=$((_sus3_cache_r + _sus3_cache_c))
            if [[ "$_sus3_denom" -gt 0 ]]; then
                _sus3_is_break=$(awk "BEGIN{print ($_sus3_cache_r / $_sus3_denom < 0.5) ? 1 : 0}")
                if [[ "$_sus3_is_break" == "1" ]]; then
                    _sus3_ratio=$(awk "BEGIN{printf \"%.2f\", $_sus3_cache_r / $_sus3_denom}")
                    emit_event "$DEDUP_FILE" "gotcha" "hook_regex" "[cache:break] Session had cache_hit_ratio=$_sus3_ratio (threshold 0.5). Potential causes: model switch mid-session, tool set changes, or compaction." "0.6" "SUS-M cache break detection" "$REALM"
                fi
            fi
        fi
    fi
fi

# ===========================================
# ANTICIPATION OUTCOME: Track prediction correctness (periodic)
# ===========================================
PREDICTIONS_FILE="$MIND_PATH/.last_predictions.json"

if [[ -f "$PREDICTIONS_FILE" && $STOP_ENRICH_TURN -eq 1 ]]; then
    # Extract tool usage from transcript (tool names from assistant's actions)
    TOOLS_USED=$(transcript_tool_names | tail -10)

    if [[ -n "$TOOLS_USED" ]]; then
        # Read predictions
        predictions=$(cat "$PREDICTIONS_FILE" 2>/dev/null)

        if [[ -n "$predictions" && "$predictions" != "[]" ]]; then
            # For each prediction, check if the action matches any tool used
            while read -r candidate; do
                [[ -z "$candidate" ]] && continue

                candidate_id=$(echo "$candidate" | jq -r '.id // 0' 2>/dev/null)
                prediction=$(echo "$candidate" | jq -r '.prediction // ""' 2>/dev/null)

                [[ "$candidate_id" -eq 0 || -z "$prediction" ]] && continue

                # Check if prediction matches any tool used (fuzzy match)
                match_found=false
                while IFS= read -r tool; do
                    [[ -z "$tool" ]] && continue

                    prediction_lower=$(echo "$prediction" | tr '[:upper:]' '[:lower:]')
                    tool_lower=$(echo "$tool" | tr '[:upper:]' '[:lower:]')

                    # Match keywords: edit, test, build, commit, etc.
                    if [[ "$prediction_lower" == *"edit"* && "$tool_lower" == *"edit"* ]] ||
                       [[ "$prediction_lower" == *"test"* && ("$tool_lower" == *"test"* || "$tool_lower" == *"bash"*) ]] ||
                       [[ "$prediction_lower" == *"build"* && "$tool_lower" == *"bash"* ]] ||
                       [[ "$prediction_lower" == *"commit"* && "$tool_lower" == *"bash"* ]] ||
                       [[ "$prediction_lower" == *"$tool_lower"* || "$tool_lower" == *"$prediction_lower"* ]]; then
                        match_found=true
                        break
                    fi
                done <<< "$TOOLS_USED"

                if [[ "$match_found" == "true" ]]; then
                    # Record correct outcome via new anticipation_record_outcome
                    queue_write "anticipation_record_outcome" "{\"candidate_id\":$candidate_id,\"correct\":true}"
                    # Feed calibration
                    queue_write "calibration_record" "{\"domain\":\"anticipation\",\"success\":$match_found}"
                    echo "[soul] ✓ prediction #${candidate_id}: ${prediction:0:40}" >&2
                fi
            done <<< "$(echo "$predictions" | jq -c '.[]' 2>/dev/null)"
        fi
    fi
fi

# HABIT OBSERVATION: Handled entirely by post-bash-hook (command name sequences only).
# stop-hook previously stored noisy user-message-word → tools-used habits — removed.

# ===========================================
# CALIBRATION: Track prediction accuracy by domain (periodic)
# ===========================================
if [[ $STOP_ENRICH_TURN -eq 1 ]] && echo "$RESPONSE" | grep -qiE '(error|failed|exception|bug)'; then
    # Check if resolution was attempted (edit/fix patterns in response)
    if echo "$RESPONSE" | grep -qiE '(fixed|resolved|updated|corrected|the issue was)'; then
        queue_write "calibration_record" "{\"domain\":\"debugging\",\"success\":true}"
        echo "[soul] +calibration: debugging success" >&2
    else
        queue_write "calibration_record" "{\"domain\":\"debugging\",\"success\":false}"
        echo "[soul] +calibration: debugging incomplete" >&2
    fi
fi

# Code generation calibration: Edit/Write tools used (periodic)
if [[ $STOP_ENRICH_TURN -eq 1 ]] && echo "$TOOLS_FROM_TRANSCRIPT" | grep -qiE '(Edit|Write)'; then
    # Success if no errors in response after code changes
    if ! echo "$RESPONSE" | grep -qiE '(error|failed|syntax error|compilation failed)'; then
        queue_write "calibration_record" "{\"domain\":\"code_generation\",\"success\":true}"
        echo "[soul] +calibration: code_generation success" >&2
    else
        queue_write "calibration_record" "{\"domain\":\"code_generation\",\"success\":false}"
        echo "[soul] +calibration: code_generation had errors" >&2
    fi
fi

# Architecture calibration: design/architecture discussions (periodic)
if [[ $STOP_ENRICH_TURN -eq 1 ]] && echo "$RESPONSE" | grep -qiE '(architecture|design pattern|refactor|abstraction|interface|module|component|structure)'; then
    # Record architecture discussion (success = we provided guidance)
    queue_write "calibration_record" "{\"domain\":\"architecture\",\"success\":true}"
    echo "[soul] +calibration: architecture discussion" >&2
fi

# ===========================================
# BEHAVIORAL PROBE: Score response for sycophancy/hedging/shallow reasoning (periodic)
# Only runs if probe centroids are seeded (probe_status check is cheap).
# Stores result as a triplet for session-start hook to read next session.
# ===========================================
if [[ ${#RESPONSE} -gt 100 && $STOP_ENRICH_TURN -eq 1 ]]; then
    # Sample the first 400 chars — enough for a signal, cheap to embed
    PROBE_TEXT=$(echo "$RESPONSE" | head -c 400 | tr '\n' ' ')

    # Check if any centroids exist (fast recall with tiny limit)
    _probe_status=$(timeout 1 "$CHITTA_BIN" probe_status 2>/dev/null || true)
    if [[ -n "$_probe_status" && "$_probe_status" != *"No probe centroids"* ]]; then
        _probe_result=$(timeout 2 "$CHITTA_BIN" behavioral_probe --text "$PROBE_TEXT" --json 2>/dev/null || true)
        if [[ -n "$_probe_result" ]]; then
            _dominant=$(echo "$_probe_result" | jq -r '.metadata.dominant // empty' 2>/dev/null || true)
            _dom_score=$(echo "$_probe_result" | jq -r '.metadata.dominant_score // 0' 2>/dev/null || true)
            _quality=$(echo "$_probe_result" | jq -r '.metadata.quality // 0' 2>/dev/null || true)

            if [[ -n "$_dominant" && "$_dominant" != "direct" ]]; then
                # Non-direct dominant behavior detected — store as triplet
                _dom_pct=$(awk "BEGIN{printf \"%.0f\", ${_dom_score:-0}*100}")
                if [[ "$_dom_pct" -ge 55 ]]; then
                    queue_write "add_triplet" "{\"subject\":\"$SESSION_ID\",\"predicate\":\"probe_signal\",\"object\":\"${_dominant}:${_dom_pct}pct\",\"weight\":${_dom_score:-0}}"
                    echo "[soul] behavioral probe: ${_dominant} ${_dom_pct}% (quality=${_quality})" >&2
                fi
            else
                echo "[soul] behavioral probe: direct response (quality=${_quality})" >&2
            fi
        fi
    fi
fi

# ===========================================
# STRUCTURED SPANS: Capture tool uses with outcomes
# ===========================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -x "$SCRIPT_DIR/span-capture.sh" ]]; then
    "$SCRIPT_DIR/span-capture.sh" "$TRANSCRIPT_PATH" "$LAST_USER_MSG" 2>&1 || true
fi

# Clean up temp files
rm -f "$MIND_PATH/.last_user_message" "$MIND_PATH/.last_correction_context" "$PREDICTIONS_FILE" "$MIND_PATH/.exposed_corrections_${SESSION_ID}" "$MIND_PATH/.exposed_memories_${SESSION_ID}" 2>/dev/null

# ===========================================
# LEDGER: Rich session checkpoint for continuity
# ===========================================
SESSION_ID="${SESSION_ID:-unknown}"
TURNS=$(transcript_role_count "assistant")

# Extract active files from tool calls
ACTIVE_FILES=$(transcript_tool_files_json 2>/dev/null || echo "[]")
[[ -z "$ACTIVE_FILES" || "$ACTIVE_FILES" == "null" ]] && ACTIVE_FILES="[]"

# Extract tools used
TOOLS_USED=$(transcript_tool_names | paste -sd ', ' - | head -c 200)

# Extract assistant text for marker detection
ASSISTANT_TEXT=$(transcript_role_text "assistant" | tail -n 5000)

# Extract typed markers
DECISIONS="[]"
DECISIONS_RAW=$(echo "$ASSISTANT_TEXT" | grep -oE '\[DECISION\].*$' | sed 's/\[DECISION\]\s*//' | head -10)
[[ -n "$DECISIONS_RAW" ]] && DECISIONS=$(echo "$DECISIONS_RAW" | jq -R . | jq -s .)

BLOCKERS="[]"
BLOCKERS_RAW=$(echo "$ASSISTANT_TEXT" | grep -oE '\[BLOCKER\].*$' | sed 's/\[BLOCKER\]\s*//' | head -5)
[[ -n "$BLOCKERS_RAW" ]] && BLOCKERS=$(echo "$BLOCKERS_RAW" | jq -R . | jq -s .)

DISCOVERIES="[]"
DISCOVERIES_RAW=$(echo "$ASSISTANT_TEXT" | grep -oE '\[(SOLUTION|GOTCHA)\].*$' | head -10)
[[ -n "$DISCOVERIES_RAW" ]] && DISCOVERIES=$(echo "$DISCOVERIES_RAW" | jq -R . | jq -s .)

# Extract pending tasks from transcript
TODOS="[]"
TASK_INFO=$(grep -oE '"subject"\s*:\s*"[^"]*".*"status"\s*:\s*"(pending|in_progress)"' "$TRANSCRIPT_PATH" 2>/dev/null | head -5 || true)
if [[ -n "$TASK_INFO" ]]; then
    TODOS=$(echo "$TASK_INFO" | while read -r line; do
        subj=$(echo "$line" | grep -oE '"subject"\s*:\s*"[^"]*"' | sed 's/"subject"\s*:\s*"//' | sed 's/"$//')
        stat=$(echo "$line" | grep -oE '"status"\s*:\s*"[^"]*"' | sed 's/"status"\s*:\s*"//' | sed 's/"$//')
        echo "{\"content\":\"$subj\",\"status\":\"$stat\"}"
    done | jq -s .)
    [[ -z "$TODOS" || "$TODOS" == "null" ]] && TODOS="[]"
fi

# Detect mood from session content
if echo "$RESPONSE" | grep -qiE '(error|failed|bug|stuck)'; then
    mood="debugging"
elif echo "$RESPONSE" | grep -qiE '(complete|done|finished|shipped|success)'; then
    mood="confident"
elif [[ $LEARNED -gt 0 ]]; then
    mood="learning"
else
    mood="working"
fi

# Build snapshot: last meaningful assistant text (not just first line)
snapshot=$(echo "$ASSISTANT_TEXT" | grep -v '^$' | tail -20 | head -c 1000)
[[ -z "$snapshot" ]] && snapshot=$(echo "$RESPONSE" | head -3 | head -c 300)

# Quality gate: minimum 3 turns for session summary
if [[ "$TURNS" =~ ^[0-9]+$ && "$TURNS" -ge 3 ]]; then
    SUMMARY="[session:$SESSION_ID] ${mood}→${TURNS} turns"
    [[ -n "$TOOLS_USED" ]] && SUMMARY="$SUMMARY | tools: ${TOOLS_USED:0:100}"
    emit_event "" "session_summary" "hook_regex" "$SUMMARY" "0.5" "end-of-session summary" "$REALM"
    echo "[soul] +session-summary: ${SUMMARY:0:60}" >&2
else
    echo "[soul] skip session-summary: too few turns ($TURNS<3)" >&2
fi

# ===========================================
# AUTO-DISTILLATION: Periodic mid-session + end-of-session
# ===========================================
if [[ -n "$SESSION_ID" && "$SESSION_ID" != "default" && -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" ]]; then
    queue_write "transcript_register" "{\"session_id\":\"$SESSION_ID\",\"transcript_path\":$(printf '%s' "$TRANSCRIPT_PATH" | jq -Rs .),\"realm\":\"$REALM\"}"

    # Mid-session distillation every 20 turns so knowledge is available within the session
    DISTILL_MARKER="$MIND_PATH/.last_distill_turn_${SESSION_ID}"
    LAST_DISTILL=$(cat "$DISTILL_MARKER" 2>/dev/null || echo 0)
    DISTILL_INTERVAL=20
    if (( TURN_INDEX - LAST_DISTILL >= DISTILL_INTERVAL )); then
        queue_write "distill_trigger" "{\"session_id\":\"$SESSION_ID\"}"
        echo "$TURN_INDEX" > "$DISTILL_MARKER"
    fi

    # Always distill at session end (stop_hook_active check prevents double-fire)
    queue_write "distill_trigger" "{\"session_id\":\"$SESSION_ID\"}"
fi

# Build rich ledger entry
LEDGER_ARGS=$(jq -n \
    --arg session_id "$SESSION_ID" \
    --arg project "$REALM" \
    --arg transcript_path "$TRANSCRIPT_PATH" \
    --arg mood "$mood" \
    --argjson active_files "$ACTIVE_FILES" \
    --argjson decisions "$DECISIONS" \
    --argjson todos "$TODOS" \
    --argjson blockers "$BLOCKERS" \
    --argjson discoveries "$DISCOVERIES" \
    --arg snapshot "$snapshot" \
    '{
        session_id: $session_id,
        project: $project,
        transcript_path: $transcript_path,
        mood: $mood,
        active_files: $active_files,
        decisions: $decisions,
        todos: $todos,
        blockers: $blockers,
        discoveries: $discoveries,
        snapshot: $snapshot
    }')

queue_write "ledger_save" "$LEDGER_ARGS"
file_count=$(echo "$ACTIVE_FILES" | jq 'length')
decision_count=$(echo "$DECISIONS" | jq 'length')
todo_count=$(echo "$TODOS" | jq 'length')
echo "[ledger] queued: $SESSION_ID ($mood, files=$file_count decisions=$decision_count todos=$todo_count)" >&2

# ===========================================
# GOAL DETECTION: Detect goal setting and progress patterns
# Skip for non-interactive sessions (claude -p, API) — labeler/batch content
# would otherwise match goal patterns and flood the daemon with spurious calls.
# ===========================================
if [[ -n "$LAST_USER_MSG" && "${CLAUDE_CODE_ENTRYPOINT:-cli}" == "cli" ]]; then
    # Goal setting patterns: "I want to", "we need to", "let's build/create/implement"
    if echo "$LAST_USER_MSG" | grep -qiE "(I want to|we need to|let'?s (build|create|implement|make|ship|finish|complete)|goal is to|objective is|planning to)"; then
        goal_title=$(echo "$LAST_USER_MSG" | grep -ioE "(I want to|we need to|let'?s (build|create|implement|make|ship|finish|complete)|goal is to|objective is|planning to)[^.!?]*" | head -1 | head -c 100 | tr '\n' ' ')
        if [[ -n "$goal_title" && ${#goal_title} -gt 10 ]]; then
            goal_description=$(echo "$LAST_USER_MSG" | head -c 300)
            "$CHITTA_BIN" goal_set --title "$goal_title" --description "$goal_description" 2>/dev/null || true
            echo "[soul] +goal detected: ${goal_title:0:50}" >&2
        fi
    fi

    # Goal completion patterns: "done", "shipped", "released", "finished", "completed"
    # Note: goal_progress requires goal ID, so we get the most recent active goal first
    if echo "$LAST_USER_MSG" | grep -qiE "(it'?s done|we'?re done|finished|shipped|released|completed|all done|mission accomplished|working now|tests pass|merged)"; then
        # Get most recent active goal via CLI
        goal_response=$("$CHITTA_BIN" goal_list --status active --limit 1 --json 2>/dev/null || true)
        goal_id=$(echo "$goal_response" | jq -r '.goals[0].id // empty' 2>/dev/null)
        if [[ -n "$goal_id" ]]; then
            "$CHITTA_BIN" goal_progress --id "$goal_id" --progress 1.0 2>/dev/null || true
            echo "[soul] +goal progress: $goal_id -> 100%" >&2
        fi
    fi
fi

# ===========================================
# CURIOSITY RESOLUTION: Detect answers to knowledge gaps
# ===========================================
# Check if response contains resolution patterns
if echo "$RESPONSE" | grep -qiE "(I found|the answer is|it turns out|the reason is|figured out|discovered that|realized that|the issue was|the problem was|the solution is|turns out|mystery solved)"; then
    resolution_context=$(echo "$RESPONSE" | grep -iE "(I found|the answer is|it turns out|the reason is|figured out|discovered|realized|the issue was|the problem was|the solution is)" | head -1 | head -c 300 | tr '\n' ' ')
    if [[ -n "$resolution_context" && ${#resolution_context} -gt 20 ]]; then
        # Get most recent unresolved curiosity gap via CLI
        gaps_response=$("$CHITTA_BIN" curiosity_gaps --limit 1 --json 2>/dev/null || true)
        gap_id=$(echo "$gaps_response" | jq -r '.gaps[0].id // empty' 2>/dev/null)
        if [[ -n "$gap_id" ]]; then
            "$CHITTA_BIN" curiosity_resolve --id "$gap_id" --learned "$resolution_context" 2>/dev/null || true
            echo "[soul] +curiosity resolved: gap $gap_id" >&2
        fi
    fi
fi

# ═══════════════════════════════════════════════════════════════════════════
# SLEEP CONSOLIDATION: queue memory consolidation for background processing
# ═══════════════════════════════════════════════════════════════════════════
if [[ -x "$CHITTA_BIN" ]]; then
    queue_write "consolidate_request" "$(jq -n \
        --arg realm "${REALM:-brahman}" \
        '{realm: $realm, threshold: 0.92, dry_run: false, limit: 20}')"
    echo "[consolidation] Queued sleep consolidation for realm=${REALM:-brahman}" >&2

    # CEC consolidation_pass: run Sequitur + promote rules + rebuild HypothesisMarket.
    # nohup detaches from hook timeout; log to tmp for debugging.
    _cec_log="${CHITTA_DB_PATH:-$HOME/.claude/mind}/.cec_consolidation.log"
    nohup "$CHITTA_BIN" consolidation_pass >"$_cec_log" 2>&1 &
    echo "[cec] consolidation_pass launched (pid $!, log: $_cec_log)" >&2
fi

# Record stop timestamp for both session-specific and legacy global paths.
# Session-specific tracking prevents false cache-expired warnings across fresh sessions.
STOP_TS="$(date +%s)"
echo "$STOP_TS" > "${MIND_PATH}/.last_stop_time"
if [[ -n "${SESSION_ID:-}" ]]; then
    echo "$STOP_TS" > "${MIND_PATH}/.last_stop_time_${SESSION_ID}"
fi

# ═══════════════════════════════════════════════════════════════════════════
# SESSION COST TELEMETRY: Log subagent count + transcript size for analysis
# ═══════════════════════════════════════════════════════════════════════════
if [[ -n "${SESSION_ID:-}" ]]; then
    AGENT_COUNT_FILE="$MIND_PATH/.subagent_count_${SESSION_ID}"
    AGENT_COUNT=$(cat "$AGENT_COUNT_FILE" 2>/dev/null || echo 0)
    TRANSCRIPT_SIZE=0
    [[ -n "${TRANSCRIPT_PATH:-}" && -f "$TRANSCRIPT_PATH" ]] && \
        TRANSCRIPT_SIZE=$(stat -c%s "$TRANSCRIPT_PATH" 2>/dev/null || echo 0)
    TRANSCRIPT_MB=$(( TRANSCRIPT_SIZE / 1048576 ))
    TURN_INDEX=$(cat "$MIND_PATH/.turn_index_${SESSION_ID}" 2>/dev/null || echo 0)

    if [[ $AGENT_COUNT -gt 0 || $TRANSCRIPT_MB -gt 10 ]]; then
        queue_write "remember" "{\"content\":\"[session-cost] ${SESSION_ID:0:8}: ${TURN_INDEX} turns, ${AGENT_COUNT} subagents, ${TRANSCRIPT_MB}MB transcript @realm:${REALM:-brahman}\",\"kind\":\"episode\",\"tags\":[\"session-cost\"]}"
        echo "[telemetry] ${TURN_INDEX} turns, ${AGENT_COUNT} subagents, ${TRANSCRIPT_MB}MB" >&2
    fi
fi

# ═══════════════════════════════════════════════════════════════════════════
# SESSION-END DECISION SUMMARY
# Extract all [DECISION]/[SOLUTION]/[CORRECTION] markers from this session's
# transcript and store a consolidated summary. Fires once per session on the
# final turn (guarded by summary-written marker file).
SESSION_SUMMARY_FILE="${MIND_PATH}/.session_summary_written_${SESSION_ID}"
if [[ -n "${SESSION_ID:-}" && -n "${TRANSCRIPT_PATH:-}" && -f "$TRANSCRIPT_PATH" \
      && ! -f "$SESSION_SUMMARY_FILE" && "${TURN_INDEX:-0}" -ge 5 ]]; then
    _decisions=$(python3 - <<'PYEOF' 2>/dev/null
import json, sys, re, os
path = os.environ.get("TRANSCRIPT_PATH", "")
if not path or not os.path.exists(path): sys.exit(0)
lines = open(path).readlines()[-600:]
hits = []
for line in lines:
    try:
        d = json.loads(line)
        role = d.get("role","")
        content = ""
        if isinstance(d.get("content"), str):
            content = d["content"]
        elif isinstance(d.get("content"), list):
            content = " ".join(b.get("text","") for b in d["content"] if isinstance(b,dict))
        if not content: continue
        for m in re.finditer(r'\[(DECISION|SOLUTION|CORRECTION|MILESTONE)\]\s*([^\n]{10,200})', content):
            hits.append(f"[{m.group(1)}] {m.group(2).strip()}")
    except: pass
if hits:
    print("\n".join(hits[:12]))
PYEOF
    )
    if [[ -n "$_decisions" ]]; then
        _realm_json=$(printf '%s' "${REALM:-brahman}" | jq -Rs .)
        _content_json=$(printf '[session-summary:%s] %s' "${SESSION_ID:0:8}" "$_decisions" | jq -Rs .)
        queue_write "remember" \
            "{\"content\":$_content_json,\"category\":\"decision\",\"realm\":$_realm_json,\"tags\":[\"session-summary\",\"auto-summary\"],\"visibility\":1}" \
            2>/dev/null || true
        touch "$SESSION_SUMMARY_FILE"
        echo "[soul] session summary stored (${#_decisions} chars, ${TURN_INDEX} turns)" >&2
    fi
fi
# ═══════════════════════════════════════════════════════════════════════════

# v6.0: route Outcome event into interaction ledger via durable queue
if [[ -n "${SESSION_ID:-}" && "$SESSION_ID" != "unknown" ]]; then
    _success=$([ "${HAS_ERROR:-false}" = "true" ] && echo false || echo true)
    queue_write "ledger_append" "{\"kind\":\"Outcome\",\"session_id\":\"${SESSION_ID}\",\"Outcome\":{\"success\":${_success},\"error_kind\":null,\"turn_count\":${TURN_INDEX:-0}}}"
fi

exit 0
