#!/bin/bash
# Epsilon-Yajna Batch Orchestrator
#
# Lists verbose nodes ready for compression and outputs node IDs
# for batch processing by yajna-processor sub-agents.

# Don't use set -e: we want the script to continue even if parts fail

CHITTA="${HOME}/.claude/bin/chitta"
BATCH_SIZE="${1:-50}"
MIN_LENGTH="${2:-200}"

# Ensure daemon is running. Prefer systemd if a unit is installed — bare
# "chittad daemon" calls race with the systemd-managed instance and trigger
# SIGKILL self-clobbers via daemon_lifecycle's stale-PID handler.
if ! pgrep -f "chittad daemon" >/dev/null; then
    echo "[yajna] Starting daemon..." >&2
    if [[ -f "${HOME}/.config/systemd/user/chittad.service" ]] && \
       command -v systemctl >/dev/null 2>&1; then
        systemctl --user start chittad 2>/dev/null || true
    else
        # Fallback (no systemd unit): never spawn with default flags — that enables
        # hygiene/consolidation and races as an unmanaged second writer (store corruption).
        _yj_model="${HOME}/.claude/bin/bge-large-en-v1.5.gguf"
        _yj_flags="--path ${HOME}/.claude/mind --no-autonomous --no-distill --no-hygiene --no-enrich"
        [[ -f "$_yj_model" ]] && _yj_flags="$_yj_flags --embed-model $_yj_model"
        "${HOME}/.claude/bin/chittad" daemon $_yj_flags &
        disown
    fi
    sleep 2
fi

# Get verbose nodes
echo "[yajna] Gathering verbose nodes (min ${MIN_LENGTH} chars)..." >&2

# Use yajna_list to get candidates
result=$("$CHITTA" yajna_list --limit "$BATCH_SIZE" --min_length "$MIN_LENGTH" 2>/dev/null)

# Extract node IDs (format: [uuid] title...)
echo "$result" | grep -oE '\[[0-9a-f-]{36}\]' | tr -d '[]' | head -n "$BATCH_SIZE"

# Count
count=$(echo "$result" | grep -c '\[.*\]' || echo 0)
echo "[yajna] Found $count verbose nodes ready for processing" >&2

exit 0
