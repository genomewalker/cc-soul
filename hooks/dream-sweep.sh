#!/bin/bash
# dream-sweep.sh — nightly cross-session transcript distillation
# Run at 02:00 via systemd timer. Processes up to MAX_PER_RUN unprocessed
# transcripts across all Claude projects and feeds them to chittad distill.
# Provides Dreaming V3-like background synthesis across sessions.
#
# Processed set: ~/.claude/mind/.dream_sweep_processed  (one session-id per line)
# Lock:          ~/.claude/mind/.dream_sweep.lock
# Log:           ~/.claude/mind/.dream_sweep.log

set -euo pipefail

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
CHITTAD_BIN="${CHITTAD_BIN:-$HOME/.claude/bin/chittad}"
MIND="${MIND:-$HOME/.claude/mind}"
PROCESSED_FILE="$MIND/.dream_sweep_processed"
LOCK_FILE="$MIND/.dream_sweep.lock"
LOG_FILE="$MIND/.dream_sweep.log"
MAX_PER_RUN=10
MIN_TURNS=4        # skip tiny sessions
DISTILL_TIMEOUT=90 # per-transcript timeout (seconds)

log() { echo "[$(date -Iseconds)] $*" | tee -a "$LOG_FILE"; }

# Single-instance guard
if ! mkdir "$LOCK_FILE" 2>/dev/null; then
    existing_pid=$(cat "$LOCK_FILE/pid" 2>/dev/null || echo "?")
    log "Already running (pid $existing_pid), exiting"
    exit 0
fi
echo $$ > "$LOCK_FILE/pid"
trap 'rm -rf "$LOCK_FILE"' EXIT

mkdir -p "$MIND"
touch "$PROCESSED_FILE"

log "dream-sweep start (max $MAX_PER_RUN transcripts)"

# Count user+assistant turns in a JSONL transcript
count_turns() {
    local path="$1"
    python3 -c "
import json, sys
n = 0
with open(sys.argv[1]) as f:
    for line in f:
        try:
            obj = json.loads(line)
            if obj.get('type') in ('user','assistant') and isinstance(obj.get('message'), dict):
                n += 1
        except: pass
print(n)
" "$path" 2>/dev/null || echo 0
}

# Best-effort realm detection from a Claude project dir name.
# Dir names encode the cwd as a path with '/' replaced by '-'.
# We decode and run realm_detect in the project dir if it exists.
realm_for_project() {
    local proj_dir="$1"   # e.g. -maps-projects-foo-apps-repos-myrepo
    local candidate
    # Decode: leading - → /, remaining - → /
    candidate=$(printf '%s' "$proj_dir" | sed 's|^-|/|; s|-|/|g')
    if [[ -d "$candidate" ]]; then
        (cd "$candidate" && "$CHITTA_BIN" realm_detect 2>/dev/null) || echo "brahman"
    else
        echo "brahman"
    fi
}

processed=0
skipped_done=0
skipped_short=0

while IFS= read -r jsonl_path; do
    [[ -f "$jsonl_path" ]] || continue

    session_id=$(basename "$jsonl_path" .jsonl)
    proj_dir=$(basename "$(dirname "$jsonl_path")")

    # Skip already processed
    if grep -qxF "$session_id" "$PROCESSED_FILE" 2>/dev/null; then
        skipped_done=$(( skipped_done + 1 ))
        continue
    fi

    # Skip small sessions
    turns=$(count_turns "$jsonl_path")
    if (( turns < MIN_TURNS )); then
        log "skip $session_id (${turns} turns < ${MIN_TURNS})"
        echo "$session_id" >> "$PROCESSED_FILE"
        skipped_short=$(( skipped_short + 1 ))
        continue
    fi

    realm=$(realm_for_project "$proj_dir")
    log "distill $session_id realm=$realm turns=$turns path=$jsonl_path"

    if timeout "$DISTILL_TIMEOUT" "$CHITTAD_BIN" distill \
            --transcript-path "$jsonl_path" \
            --session-id "$session_id" \
            --realm "$realm" \
            >>"$LOG_FILE" 2>&1; then
        log "ok $session_id"
    else
        log "failed $session_id (exit $?)"
    fi

    echo "$session_id" >> "$PROCESSED_FILE"
    processed=$(( processed + 1 ))
    [[ $processed -ge $MAX_PER_RUN ]] && break

done < <(find ~/.claude/projects -name '*.jsonl' ! -name '*.thinking_pos' -newer "$PROCESSED_FILE" 2>/dev/null | sort)

log "dream-sweep done: processed=$processed skipped_done=$skipped_done skipped_short=$skipped_short"
