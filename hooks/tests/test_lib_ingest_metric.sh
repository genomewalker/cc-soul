#!/bin/bash
# Test record_ingest_metric and safe_queue_write, which moved out of duplicate
# copies in prompt-core.sh and stop-core.sh into hooks/lib.sh.
#
# Both read globals (METRICS_FILE, ALERT_FILE) that the calling hook assigns
# AFTER sourcing lib.sh. That late binding is the whole reason the move is
# safe, so it is what these tests pin.
set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FAIL=0
assert() { if ! eval "$2"; then echo "FAIL: $1"; FAIL=1; else echo "ok: $1"; fi; }

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

export CHITTA_DB_PATH="$T/mind"
mkdir -p "$CHITTA_DB_PATH"
source "$SCRIPT_DIR/lib.sh" >/dev/null 2>&1

# Assigned only now — after the source — exactly as the two cores do it.
METRICS_FILE="$T/mind/.hook_metrics.json"
ALERT_FILE="$T/mind/.hook_alerts.log"

assert "helpers come from lib.sh" "declare -F record_ingest_metric >/dev/null && declare -F safe_queue_write >/dev/null"

# --- creates the metrics file on first call and counts an ingested turn ---
record_ingest_metric true
assert "metrics file created" "[[ -f '$METRICS_FILE' ]]"
assert "turns_total=1" "[[ \$(jq -r .turns_total '$METRICS_FILE') -eq 1 ]]"
assert "turns_ingested=1" "[[ \$(jq -r .turns_ingested '$METRICS_FILE') -eq 1 ]]"

# --- a failed ingest bumps the total only ---
record_ingest_metric false
assert "turns_total=2 after failure" "[[ \$(jq -r .turns_total '$METRICS_FILE') -eq 2 ]]"
assert "turns_ingested still 1" "[[ \$(jq -r .turns_ingested '$METRICS_FILE') -eq 1 ]]"

# --- no alert below the 20-turn threshold, even at a bad rate ---
assert "no alert before 20 turns" "[[ ! -s '$ALERT_FILE' ]]"

# --- crossing 20 turns under 95% ingest writes exactly one alert line ---
printf '%s\n' '{"turns_total":19,"turns_ingested":10}' > "$METRICS_FILE"
record_ingest_metric false
assert "alert written at 20 turns / 50%" "[[ -s '$ALERT_FILE' ]]"
assert "one alert line" "[[ \$(wc -l < '$ALERT_FILE') -eq 1 ]]"
assert "alert names the rate" "grep -q 'ingest_rate=50%' '$ALERT_FILE'"

# --- at or above 95% no alert is added ---
printf '%s\n' '{"turns_total":99,"turns_ingested":99}' > "$METRICS_FILE"
record_ingest_metric true
assert "no alert at 100% ingest" "[[ \$(wc -l < '$ALERT_FILE') -eq 1 ]]"

# --- a corrupt metrics file must not crash the hook or leave a .tmp behind ---
printf '%s\n' 'not json' > "$METRICS_FILE"
record_ingest_metric true
assert "corrupt metrics file: no crash" "[[ \$? -eq 0 ]]"
assert "no .tmp left behind" "[[ ! -e '${METRICS_FILE}.tmp' ]]"

# --- safe_queue_write retries and reports the second attempt's result ---
_attempts=0
queue_write() { _attempts=$((_attempts + 1)); return 1; }
safe_queue_write noop '{}' >/dev/null 2>&1
assert "safe_queue_write retries once (2 attempts)" "[[ $_attempts -eq 2 ]]"
assert "safe_queue_write propagates failure" "! safe_queue_write noop '{}' >/dev/null 2>&1"

_attempts=0
queue_write() { _attempts=$((_attempts + 1)); return 0; }
safe_queue_write noop '{}' >/dev/null 2>&1
assert "safe_queue_write does not retry on success" "[[ $_attempts -eq 1 ]]"

exit $FAIL
