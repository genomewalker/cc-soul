#!/bin/bash
# Shepherd background poller
#
# Lightweight script to monitor zellij panes for shepherd tasks.
# Runnable from cron, daemon subconscious, or manually.
#
# Usage: shepherd-poll.sh [--verbose] [--dry-run]
#
# Scans active shepherd-* tasks, reads their associated panes,
# detects error patterns, and logs events / alerts as needed.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh" 2>/dev/null || true

# Configuration
VERBOSE="${SHEPHERD_VERBOSE:-false}"
DRY_RUN="${SHEPHERD_DRY_RUN:-false}"
CHITTA="${HOME}/.claude/bin/chitta"
ZELLIJ_SESSION="${ZELLIJ_SESSION:-zellij-agent}"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --dry-run|-n)
            DRY_RUN=true
            shift
            ;;
        *)
            echo "Unknown option: $1" >&2
            echo "Usage: shepherd-poll.sh [--verbose] [--dry-run]" >&2
            exit 1
            ;;
    esac
done

log() {
    if [[ "$VERBOSE" == "true" ]]; then
        echo "[shepherd-poll] $*" >&2
    fi
}

# Error patterns for workflow managers
# Each pattern is: NAME|REGEX|SEVERITY (critical, warning, info)
ERROR_PATTERNS=(
    # Snakemake
    "snakemake_error|Error in rule|critical"
    "snakemake_missing|MissingInputException|critical"
    "snakemake_failed|WorkflowError|critical"
    "snakemake_lock|LockException|warning"

    # Nextflow
    "nextflow_error|ERROR ~ |critical"
    "nextflow_failed|Process .* failed|critical"
    "nextflow_abort|Execution aborted|critical"

    # Slurm
    "slurm_failed|FAILED|warning"
    "slurm_timeout|TIMEOUT|warning"
    "slurm_oom|OUT_OF_MEMORY|critical"
    "slurm_cancelled|CANCELLED|info"

    # Generic
    "traceback|Traceback \\(most recent call last\\)|critical"
    "segfault|Segmentation fault|critical"
    "killed|Killed|warning"
    "oom|Out of memory|critical"
    "permission|Permission denied|warning"
    "disk_full|No space left on device|critical"
    "connection|Connection refused|warning"
)

# Completion patterns (task done successfully)
COMPLETION_PATTERNS=(
    "snakemake_complete|100% done|info"
    "snakemake_finish|Finished job|info"
    "nextflow_complete|Succeeded|info"
    "nextflow_done|Completed at:|info"
    "slurm_complete|COMPLETED|info"
)

# Read pane output via zellij
read_pane() {
    local pane_name="$1"
    local session="${2:-$ZELLIJ_SESSION}"

    # Focus pane and dump screen
    # Using zellij action with session targeting
    if ! zellij -s "$session" action go-to-tab-name "$pane_name" 2>/dev/null; then
        # Try direct pane focus if tab name doesn't work
        if ! zellij -s "$session" action toggle-pane-frames 2>/dev/null; then
            log "Cannot focus pane: $pane_name"
            return 1
        fi
    fi

    # Dump screen content
    zellij -s "$session" action dump-screen /dev/stdout 2>/dev/null || return 1
}

# Alternative: Read pane using pane name focus pattern
read_pane_by_name() {
    local pane_name="$1"
    local session="${2:-$ZELLIJ_SESSION}"

    # Get layout to find pane
    local layout
    layout=$(zellij -s "$session" action dump-layout 2>/dev/null) || return 1

    # Check if pane exists in layout
    if ! echo "$layout" | grep -q "\"$pane_name\""; then
        log "Pane not found: $pane_name"
        return 1
    fi

    # Read pane (assumes focus is set properly by caller)
    zellij -s "$session" action dump-screen /dev/stdout --full 2>/dev/null || return 1
}

# Check output for error patterns
# Returns: pattern_name|severity if found, empty if clean
check_errors() {
    local output="$1"

    for pattern_def in "${ERROR_PATTERNS[@]}"; do
        IFS='|' read -r name regex severity <<< "$pattern_def"
        if echo "$output" | grep -qE "$regex"; then
            echo "$name|$severity"
            return 0
        fi
    done

    return 1
}

# Check output for completion patterns
check_completion() {
    local output="$1"

    for pattern_def in "${COMPLETION_PATTERNS[@]}"; do
        IFS='|' read -r name regex severity <<< "$pattern_def"
        if echo "$output" | grep -qE "$regex"; then
            echo "$name"
            return 0
        fi
    done

    return 1
}

# Log event to chitta
log_event() {
    local task_id="$1"
    local kind="$2"
    local payload="$3"
    local tags="$4"

    if [[ "$DRY_RUN" == "true" ]]; then
        log "DRY-RUN: long_task_event task_id=$task_id kind=$kind payload=$payload"
        return 0
    fi

    "$CHITTA" long_task_event \
        --task_id "$task_id" \
        --kind "$kind" \
        --payload "$payload" \
        --tags "$tags" 2>/dev/null || true
}

# Send alert message
send_alert() {
    local recipient="$1"
    local message="$2"

    if [[ "$DRY_RUN" == "true" ]]; then
        log "DRY-RUN: msg_send recipient=$recipient message=$message"
        return 0
    fi

    "$CHITTA" msg_send \
        --recipient "$recipient" \
        --message "$message" 2>/dev/null || true
}

# Get active shepherd tasks
get_shepherd_tasks() {
    # Query for active tasks with shepherd- prefix
    "$CHITTA" sql_query \
        --query "SELECT task_id, goal, work_items FROM long_task WHERE status = 'active' AND task_id LIKE 'shepherd-%'" \
        --text-only 2>/dev/null || echo ""
}

# Extract pane name from task metadata or work_items
# Convention: pane name is stored in work_items as "pane:NAME"
get_pane_name() {
    local work_items="$1"

    # Look for pane: prefix in work items
    echo "$work_items" | grep -oP 'pane:\K[^\s,]+' | head -1
}

# Main polling loop
poll_once() {
    log "Starting poll cycle..."

    # Check if chitta daemon is running
    if ! "$CHITTA" health_check --text-only 2>/dev/null | grep -q "daemon"; then
        log "Chitta daemon not running, skipping poll"
        return 0
    fi

    # Check if zellij session exists
    if ! zellij list-sessions -n 2>/dev/null | grep -q "^$ZELLIJ_SESSION"; then
        log "Zellij session $ZELLIJ_SESSION not found, skipping poll"
        return 0
    fi

    # Get active shepherd tasks
    local tasks
    tasks=$(get_shepherd_tasks)

    if [[ -z "$tasks" ]]; then
        log "No active shepherd tasks"
        return 0
    fi

    # Process each task
    while IFS=$'\t' read -r task_id goal work_items; do
        [[ -z "$task_id" || "$task_id" == "task_id" ]] && continue

        log "Checking task: $task_id"

        # Get pane name from metadata
        local pane_name
        pane_name=$(get_pane_name "$work_items")

        if [[ -z "$pane_name" ]]; then
            log "No pane name found for task $task_id, skipping"
            continue
        fi

        log "Reading pane: $pane_name"

        # Read pane output
        local output
        output=$(read_pane_by_name "$pane_name" 2>/dev/null) || {
            log "Failed to read pane $pane_name"
            continue
        }

        # Get last 100 lines for pattern matching
        local recent_output
        recent_output=$(echo "$output" | tail -100)

        # Check for errors
        local error_match
        if error_match=$(check_errors "$recent_output"); then
            IFS='|' read -r pattern_name severity <<< "$error_match"

            log "ERROR DETECTED: $pattern_name (severity: $severity)"

            # Extract error context (surrounding lines)
            local error_context
            error_context=$(echo "$recent_output" | tail -20 | head -c 500)

            # Log event
            local payload
            payload=$(printf '{"pattern":"%s","severity":"%s","context":"%s"}' \
                "$pattern_name" "$severity" "$(echo "$error_context" | tr '\n' ' ' | sed 's/"/\\"/g')")

            log_event "$task_id" "error" "$payload" "shepherd,poll,$pattern_name"

            # Send alert for critical errors
            if [[ "$severity" == "critical" ]]; then
                send_alert "user" "[SHEPHERD] Critical error in $task_id: $pattern_name"
            fi
        fi

        # Check for completion
        local completion_match
        if completion_match=$(check_completion "$recent_output"); then
            log "COMPLETION DETECTED: $completion_match"

            local payload
            payload=$(printf '{"pattern":"%s"}' "$completion_match")

            log_event "$task_id" "observation" "$payload" "shepherd,poll,completion"
        fi

    done <<< "$tasks"

    log "Poll cycle complete"
}

# Entry point
main() {
    # Ensure chitta CLI exists
    if [[ ! -x "$CHITTA" ]]; then
        echo "[shepherd-poll] chitta CLI not found at $CHITTA" >&2
        exit 1
    fi

    poll_once
}

main "$@"
