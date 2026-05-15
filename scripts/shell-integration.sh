#!/usr/bin/env bash
# Shell integration for task tracking outside Claude Code.
# Source this from ~/.bashrc or ~/.zshrc:
#   source ~/.claude/plugins/cache/genomewalker-cc-soul/cc-soul/*/scripts/shell-integration.sh
#
# Installs a PROMPT_COMMAND / precmd hook that intercepts analysis commands
# and registers them in the task ledger automatically.

_CHITTA_MCP_DIR=$(find "$HOME/.claude/plugins/cache/genomewalker-cc-soul" \
    -name "task_ledger.py" 2>/dev/null | head -1 | xargs dirname 2>/dev/null || echo "")

[[ -z "$_CHITTA_MCP_DIR" ]] && return 0

_chitta_track_last_cmd() {
    local exit_code=$?
    local cmd
    cmd=$(history 1 2>/dev/null | sed 's/^[[:space:]]*[0-9]*[[:space:]]*//')
    [[ -z "$cmd" ]] && return

    # Only track analysis/long-running patterns
    if echo "$cmd" | grep -qE \
        '(^|\s)(sbatch|srun|bsub|qsub|nohup|snakemake|nextflow)\s' \
        || echo "$cmd" | grep -qE \
        '(^|\s)(python3?|Rscript|julia)(\s+\S+\.(py|R|jl))' \
        || echo "$cmd" | grep -qE \
        '(^|\s)(bash|sh)\s+\S+\.sh(\s|$)'; then

        local cwd
        cwd=$(pwd 2>/dev/null || echo "")
        local thread_id
        thread_id=$(cat "$HOME/.claude/mind/.current_thread_id" 2>/dev/null || echo "")

        timeout 5 python3 "$_CHITTA_MCP_DIR/provenance.py" extract \
            --cmd "$cmd" --cwd "$cwd" >/dev/null 2>&1 &
    fi
}

# Bash
if [[ -n "${BASH_VERSION:-}" ]]; then
    PROMPT_COMMAND="${PROMPT_COMMAND:+$PROMPT_COMMAND; }_chitta_track_last_cmd"
fi

# Zsh
if [[ -n "${ZSH_VERSION:-}" ]]; then
    precmd_functions+=(_chitta_track_last_cmd)
fi
