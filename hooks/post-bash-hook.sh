#!/bin/bash
# PostToolUse hook for Bash: Surface relevant memories on command failure
#
# When a command fails, searches for gotchas/corrections related to the command
# and injects them as context for debugging.

# Don't use set -e: we want hooks to succeed even if some parts fail

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MIND_PATH="${MIND_PATH:-$HOME/.claude/mind}"
LAST_CMD_FILE="$MIND_PATH/.last_bash_cmd"

[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Source shared library for fire-and-forget queue_write — never block on daemon RPC
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh" 2>/dev/null || true

# Read stdin (PostToolUse gets tool result)
STDIN_DATA=$(cat)

# Extract exit code and command
exit_code=$(echo "$STDIN_DATA" | jq -r '.tool_result.exit_code // 0')
command=$(echo "$STDIN_DATA" | jq -r '.tool_input.command // empty')
output=$(echo "$STDIN_DATA" | jq -r '.tool_result.stdout // empty' | head -c 500)
stderr=$(echo "$STDIN_DATA" | jq -r '.tool_result.stderr // empty' | head -c 500)

# Normalize command to first word (basename only)
normalize_cmd() {
    echo "$1" | awk '{print $1}' | sed 's|.*/||'
}

# Detect long-running job launches and store operational state
is_long_running_launch() {
    local cmd="$1"
    echo "$cmd" | grep -qE \
        '(^|\s)(nohup|sbatch|srun|qsub|bsub)\s' \
        || echo "$cmd" | grep -qE '(parallel\s.*--sshlogin|parallel\s.*-S\s|gnu parallel)' \
        || echo "$cmd" | grep -qE '(screen\s+-dm|tmux\s+new(-session)?\s+-d)' \
        || { echo "$cmd" | grep -qE '\s*&\s*$' && echo "$cmd" | grep -qvE '^\s*(sleep|echo|true|false|wait)\s'; }
}

extract_job_paths() {
    local cmd="$1"
    local paths=""
    # --out_dir / --output / -o / --outdir
    local outdir
    outdir=$(echo "$cmd" | grep -oE '(--(out_dir|output|outdir|out)\s+\S+|-o\s+\S+)' | grep -oE '\S+$' | head -1)
    [[ -n "$outdir" ]] && paths+="out:$outdir "
    # --log / --joblog / > file redirection
    local logfile
    logfile=$(echo "$cmd" | grep -oE '(--joblog\s+\S+|--log\s+\S+)' | grep -oE '\S+$' | head -1)
    [[ -z "$logfile" ]] && logfile=$(echo "$cmd" | grep -oE '>\s*\S+\.log' | grep -oE '\S+\.log' | head -1)
    [[ -n "$logfile" ]] && paths+="log:$logfile "
    echo "$paths"
}

# Detect analysis scripts worth tracking (python/R/shell/snakemake/nextflow/etc.)
is_analysis_script() {
    local cmd="$1"
    echo "$cmd" | grep -qE \
        '(^|\s)(python3?|Rscript|julia|perl|snakemake|nextflow)\s+\S+\.(py|R|jl|pl|nf|smk)' \
        || echo "$cmd" | grep -qE '(^|\s)bash\s+\S+\.sh(\s|$)' \
        || echo "$cmd" | grep -qE '(^|\s)\./\S+\.(sh|py|R)(\s|$)' \
        || echo "$cmd" | grep -qE '(^|\s)(snakemake|nextflow)\s' \
        || { echo "$cmd" | grep -qE "(^|\s)(python3?|Rscript)\s" \
             && echo "$cmd" | grep -qvE '^\s*(pip|conda|which|python.*--version)'; }
}

if [[ "$exit_code" == "0" && -n "$command" ]]; then
    curr_cmd=$(normalize_cmd "$command")

    # Long-running launch: store raw command + extracted paths as a signal
    # so other sessions can find it via recall. Claude adds semantic layer separately.
    if is_long_running_launch "$command"; then
        job_paths=$(extract_job_paths "$command")
        short_cmd=$(echo "$command" | head -c 300)
        # Determine realm from CWD
        cwd=$(pwd 2>/dev/null || echo "")
        realm=""
        if [[ "$cwd" == *"/repos/"* ]]; then
            realm=$(echo "$cwd" | grep -oE '/repos/[^/]+' | sed 's|/repos/||')
        fi
        content="[job:auto] cmd: $short_cmd | $job_paths| cwd: $cwd"
        content_json=$(printf '%s' "$content" | jq -Rs .)
        realm_json=$(printf '%s' "$realm" | jq -Rs .)
        queue_write "remember" "{\"content\":$content_json,\"kind\":\"signal\",\"realm\":$realm_json,\"tags\":[\"long-running-job\"],\"visibility\":1}" 2>/dev/null || true
    fi

    # ── Task ledger: provenance extraction ──────────────────────────────────
    _PLUGIN_DIR="${CC_SOUL_PLUGIN_DIR:-$(dirname "$(dirname "$(realpath "${BASH_SOURCE[0]}")")")}"
    _MCP_DIR="$_PLUGIN_DIR/chitta-mcp"
    if is_long_running_launch "$command" || is_analysis_script "$command"; then
        _task_id=$(cat "$MIND_PATH/.pending_task_id" 2>/dev/null || echo "")
        [[ -n "$_task_id" ]] && rm -f "$MIND_PATH/.pending_task_id"
        _thread_id=$(cat "$MIND_PATH/.current_thread_id" 2>/dev/null || echo "")
        _snap_flag=""
        [[ -n "$_task_id" && -f "$MIND_PATH/.fs_snapshot_${_task_id}" ]] \
            && _snap_flag="--before-snapshot $MIND_PATH/.fs_snapshot_${_task_id}"
        _prov_json=$(echo "$STDIN_DATA" | timeout 3 python3 "$_MCP_DIR/provenance.py" extract \
            --cmd "$command" --cwd "${cwd:-$(pwd)}" --stdout-from-stdin \
            ${_snap_flag} 2>/dev/null || echo "{}")
        if [[ -n "$_task_id" ]]; then
            _prov_patch=$(echo "$_prov_json" | python3 -c "
import sys,json; d=json.load(sys.stdin)
keys='cmd cwd git_branch git_commit conda_env inputs outputs params config_files references job_id scheduler status_check_cmd completion_digest'.split()
print(json.dumps({k:d[k] for k in keys if k in d}))" 2>/dev/null || echo "{}")
            queue_write "long_task_update" "{\"task_id\":\"$_task_id\",\"payload_patch\":$_prov_patch}" 2>/dev/null || true
            while IFS= read -r _path; do
                [[ -z "$_path" ]] && continue
                timeout 2 python3 "$_MCP_DIR/task_ledger.py" artifact_register \
                    --task-id "$_task_id" --path "$_path" --thread-id "$_thread_id" 2>/dev/null || true
            done < <(echo "$_prov_json" | python3 -c "
import sys,json; d=json.load(sys.stdin)
[print(p) for p in (d.get('outputs') or [])+(d.get('new_files') or [])]" 2>/dev/null)
            rm -f "$MIND_PATH/.fs_snapshot_${_task_id}"
        fi
        if [[ "$exit_code" != "0" && -n "$_task_id" ]]; then
            timeout 2 python3 "$_MCP_DIR/task_ledger.py" inbox_push \
                --task-id "$_task_id" --event-type "failure" \
                --digest "${command:0:60} — exit $exit_code" \
                --target-realm "${realm:-}" --thread-id "$_thread_id" 2>/dev/null || true
        fi
    fi
    # ── End task ledger ──────────────────────────────────────────────────────

    # Read previous command if exists
    if [[ -f "$LAST_CMD_FILE" ]]; then
        prev_cmd=$(cat "$LAST_CMD_FILE" 2>/dev/null)

        # Record habit if both commands present and different.
        # Uses queue_write (fire-and-forget file append) instead of RPC —
        # bash post-hooks fire on every command; a blocking RPC here can
        # stack up behind WAL/consolidation work and stall MCP responsiveness.
        if [[ -n "$prev_cmd" && "$prev_cmd" != "$curr_cmd" ]]; then
            trigger_json=$(printf 'bash:%s' "$prev_cmd" | jq -Rs .)
            response_json=$(printf 'bash:%s' "$curr_cmd" | jq -Rs .)
            queue_write "habit_observe" "{\"trigger\":$trigger_json,\"response\":$response_json}" 2>/dev/null || true
        fi
    fi

    # Save current command for next time
    mkdir -p "$MIND_PATH" 2>/dev/null
    echo "$curr_cmd" > "$LAST_CMD_FILE"

    exit 0
fi

[[ -z "$command" ]] && exit 0

json_escape() {
    echo -n "$1" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

detect_output_type() {
    local out="$1"
    local err="$2"
    local combined="$out$err"

    if echo "$combined" | grep -qE '(FAILED|PASSED|test result:|not ok|ok [0-9]+ test)'; then
        echo "TestResults"
    elif echo "$combined" | grep -qE '(error\[E[0-9]|undefined reference|ld: |make\[|CMake Error)'; then
        echo "BuildOutput"
    elif echo "$combined" | grep -qE '(^[0-9]{4}-[0-9]{2}-[0-9]{2}|\[(INFO|WARN|ERROR)\])'; then
        echo "LogOutput"
    elif echo "$out" | grep -qE '^\s*[\{\[]'; then
        echo "JsonOutput"
    else
        echo "Generic"
    fi
}

build_typed_query() {
    local cmd="$1"
    local out="$2"
    local err="$3"
    local otype="$4"

    case "$otype" in
        TestResults)
            local test_name
            test_name=$(echo "$out$err" | grep -iE '(FAILED|not ok)' | head -1 | sed 's/.*FAILED\s*//;s/.*not ok\s*[0-9]*\s*//' | head -c 100)
            local framework
            framework=$(echo "$cmd" | grep -oE '(pytest|cargo test|jest|mocha|rspec|go test|npm test)' | head -1)
            echo "test failure $test_name $framework"
            ;;
        BuildOutput)
            local error_line
            error_line=$(echo "$out$err" | grep -m1 'error' | head -c 120)
            local lang
            lang=$(echo "$cmd" | grep -oE '(cargo|cmake|make|gcc|g\+\+|rustc|javac|go build|npm run build)' | head -1)
            echo "build error $lang $error_line"
            ;;
        LogOutput)
            local error_msg
            error_msg=$(echo "$out$err" | grep -i 'ERROR' | head -1 | sed 's/.*ERROR[]\s:]*//' | head -c 120)
            local service
            service=$(echo "$cmd" | awk '{print $1}' | sed 's|.*/||')
            echo "error $service $error_msg"
            ;;
        *)
            local q="$cmd"
            if [[ -n "$err" ]]; then
                local error_terms
                error_terms=$(echo "$err" | grep -oiE '(error|failed|not found|permission denied|no such|cannot|invalid)' | head -3 | tr '\n' ' ')
                [[ -n "$error_terms" ]] && q="$q $error_terms"
            fi
            echo "$q"
            ;;
    esac
}

output_type=$(detect_output_type "$output" "$stderr")
query=$(build_typed_query "$command" "$output" "$stderr" "$output_type")

escaped_query=$(json_escape "$query")

# Search for gotchas and corrections
memories=$(timeout 2 "$CHITTA_BIN" recall --query "$escaped_query" --limit 2 --text-only 2>/dev/null | head -c 400 || true)

if [[ -n "$memories" && "$memories" != *"No memories"* ]]; then
    escaped_mem=$(json_escape "$memories")
    echo "{\"hookSpecificOutput\":{\"hookEventName\":\"PostToolUse\",\"additionalContext\":\"🔴 COMMAND FAILED (exit $exit_code) - Related memories:\\n$escaped_mem\"}}"
fi

exit 0
