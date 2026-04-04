#!/bin/bash
# PreToolUse hook: RTK token compression + soul corrections/gotchas
#
# Two-stage pipeline:
# 1. RTK: intercept known commands and return token-optimized output
# 2. Soul: surface corrections/gotchas from memory before execution

# Don't use set -e: we want hooks to succeed even if some parts fail

MATCHER="${1:-}"
[[ -z "$MATCHER" ]] && exit 0

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
RTK_BIN="${RTK_BIN:-$HOME/.claude/bin/rtk}"
STDIN_DATA=$(cat)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh" 2>/dev/null || true

json_escape() {
    echo -n "$1" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

# ─── Safety blocks (destructive commands) ─────────────────────────────────────
safety_check() {
    local cmd="$1"
    if echo "$cmd" | grep -qE '^\s*rm\s+-rf\s+(/|~/?\s*$)'; then
        echo '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"block","additionalContext":"rm -rf on / or ~ is destructive"}}'
        return 2
    fi
    if echo "$cmd" | grep -qE '^\s*chmod\s+-R\s+777\s+/\s*$'; then
        echo '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"block","additionalContext":"chmod -R 777 / is destructive"}}'
        return 2
    fi
    if echo "$cmd" | grep -qE '^\s*dd\s+.*of=/dev/sd[a-z]'; then
        echo '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"block","additionalContext":"dd writing to raw disk device"}}'
        return 2
    fi
    return 0
}

# ─── RTK rewrite: map command → rtk equivalent ────────────────────────────────
# Returns the rtk command to run, or empty string if not applicable.
# Skips: piped commands (rtk works on raw output), already-rtk commands,
#        commands with redirects (rtk can't intercept those cleanly).
rtk_rewrite() {
    local cmd="$1"
    [[ ! -x "$RTK_BIN" ]] && return 1

    # Skip if already using rtk, or if command is piped/redirected
    echo "$cmd" | grep -q 'rtk ' && return 1
    echo "$cmd" | grep -qE '[|>&]' && return 1

    # git subcommands
    if echo "$cmd" | grep -qE '^\s*git\s+(status|log|diff|show|branch|stash)\b'; then
        local subcmd rest
        subcmd=$(echo "$cmd" | sed 's/^\s*git\s\+//')
        echo "$RTK_BIN git $subcmd"
        return 0
    fi

    # gh subcommands
    if echo "$cmd" | grep -qE '^\s*gh\s+(pr|issue|run)\s+(list|view|status)\b'; then
        local rest
        rest=$(echo "$cmd" | sed 's/^\s*gh\s\+//')
        echo "$RTK_BIN gh $rest"
        return 0
    fi

    # ls (but not ls with -la / for safety check)
    if echo "$cmd" | grep -qE '^\s*ls(\s+|$)' && ! echo "$cmd" | grep -qE '^\s*ls\s+-la\s+/\s*$'; then
        echo "$RTK_BIN $(echo "$cmd" | sed 's/^\s*//')"
        return 0
    fi

    # find
    if echo "$cmd" | grep -qE '^\s*find\s+'; then
        local rest
        rest=$(echo "$cmd" | sed 's/^\s*find\s\+//')
        echo "$RTK_BIN find $rest"
        return 0
    fi

    # grep (not piped, already checked above)
    if echo "$cmd" | grep -qE '^\s*(grep|rg)\s+'; then
        local rest
        rest=$(echo "$cmd" | sed 's/^\s*\(grep\|rg\)\s\+//')
        echo "$RTK_BIN grep $rest"
        return 0
    fi

    # diff
    if echo "$cmd" | grep -qE '^\s*diff\s+'; then
        local rest
        rest=$(echo "$cmd" | sed 's/^\s*diff\s\+//')
        echo "$RTK_BIN diff $rest"
        return 0
    fi

    # docker
    if echo "$cmd" | grep -qE '^\s*docker\s+(ps|images|logs)\b'; then
        local rest
        rest=$(echo "$cmd" | sed 's/^\s*docker\s\+//')
        echo "$RTK_BIN docker $rest"
        return 0
    fi

    # cargo
    if echo "$cmd" | grep -qE '^\s*cargo\s+(test|build|clippy|check)\b'; then
        local rest
        rest=$(echo "$cmd" | sed 's/^\s*cargo\s\+//')
        echo "$RTK_BIN cargo $rest"
        return 0
    fi

    # pytest
    if echo "$cmd" | grep -qE '^\s*pytest(\s|$)'; then
        local rest
        rest=$(echo "$cmd" | sed 's/^\s*pytest//')
        echo "$RTK_BIN test pytest$rest"
        return 0
    fi

    # snakemake
    if echo "$cmd" | grep -qE '^\s*snakemake(\s|$)'; then
        echo "$RTK_BIN $(echo "$cmd" | sed 's/^\s*//')"
        return 0
    fi

    # nextflow
    if echo "$cmd" | grep -qE '^\s*nextflow(\s+run|\s+log|\s+list)'; then
        echo "$RTK_BIN $(echo "$cmd" | sed 's/^\s*//')"
        return 0
    fi

    # curl (single URL, no -o/-O output flags)
    if echo "$cmd" | grep -qE '^\s*curl\s+' && ! echo "$cmd" | grep -qE '\s-[oO]\s'; then
        local rest
        rest=$(echo "$cmd" | sed 's/^\s*curl\s\+//')
        echo "$RTK_BIN curl $rest"
        return 0
    fi

    # cat (use rtk read for plain file reads)
    if echo "$cmd" | grep -qE '^\s*cat\s+[^ ]+$'; then
        local file
        file=$(echo "$cmd" | sed -n 's/^\s*cat\s\+\([^ ]*\)\s*$/\1/p')
        if [[ -n "$file" && -f "$file" ]]; then
            echo "$RTK_BIN read $file"
            return 0
        fi
    fi

    return 1
}

# ─── Fallback: large-output safety rewrites (no RTK) ─────────────────────────
fallback_rewrite() {
    local cmd="$1"
    local rewritten="" reason=""

    if echo "$cmd" | grep -qE '^\s*find\s+(/|~)\s+-name' && ! echo "$cmd" | grep -q 'maxdepth'; then
        rewritten=$(echo "$cmd" | sed 's/\(find\s\+[^ ]*\)/\1 -maxdepth 5/')
        reason="Unbounded find; added -maxdepth 5"
    elif echo "$cmd" | grep -qE '^\s*grep\s+-r\s+' && ! echo "$cmd" | grep -q 'head'; then
        rewritten="$cmd | head -200"
        reason="Unbounded recursive grep; added head limit"
    elif echo "$cmd" | grep -qE '^\s*ls\s+.*-[^ ]*R|^\s*ls\s+-la\s+/\s*$' && ! echo "$cmd" | grep -q 'head'; then
        rewritten="$cmd | head -100"
        reason="Large directory listing; added head limit"
    fi

    if [[ -n "$rewritten" ]]; then
        echo "⚠️ Large output warning: $reason"
        echo "Consider running instead: $rewritten"
        return 0
    fi
    return 1
}

case "$MATCHER" in
    Bash)
        command=$(echo "$STDIN_DATA" | jq -r '.tool_input.command // empty')
        [[ -z "$command" ]] && exit 0

        # Stage 1a: Safety blocks
        safety_result=$(safety_check "$command")
        safety_rc=$?
        if [[ $safety_rc -eq 2 ]]; then
            echo "$safety_result"
            exit 2
        fi

        # Stage 1b: RTK rewrite (token-optimized output)
        rtk_cmd=$(rtk_rewrite "$command")
        if [[ -n "$rtk_cmd" ]]; then
            rtk_output=$(eval "$rtk_cmd" 2>&1)
            rtk_rc=$?
            if [[ $rtk_rc -eq 0 && -n "$rtk_output" ]]; then
                printf '[RTK] %s\n\n%s\n' "$command" "$rtk_output"
                exit 2
            fi
            # RTK failed — fall through to normal execution
        fi

        # Stage 1c: Fallback large-output protection (when RTK not available/applicable)
        if [[ ! -x "$RTK_BIN" ]] && command -v jq >/dev/null 2>&1; then
            fallback_result=$(fallback_rewrite "$command")
            if [[ $? -eq 0 && -n "$fallback_result" ]]; then
                _safer_cmd=$(echo "$fallback_result" | grep -o 'instead: .*' | sed 's/instead: //')
                if [[ -n "$_safer_cmd" ]]; then
                    _safer_output=$(eval "$_safer_cmd" 2>&1 | head -250)
                    printf '⚠️ Large output — compact version: %s\n\n%s\n' "$command" "$_safer_output"
                    exit 2
                fi
            fi
        fi

        # Stage 2: Soul memory — surface corrections/gotchas
        # Skip for subagent calls (agent_id present) — saves 2s timeout per tool call
        agent_id=$(echo "$STDIN_DATA" | jq -r '.agent_id // empty')
        [[ -n "$agent_id" ]] && exit 0

        [[ ! -x "$CHITTA_BIN" ]] && exit 0
        daemon_available || exit 0

        # Per-turn dedup: only inject soul recall once per assistant turn.
        # Turn index is written by stop-hook; all tool calls within a turn share the same index.
        MIND_PATH="${HOME}/.claude/mind"
        _session_id=$(echo "$STDIN_DATA" | jq -r '.session_id // empty')
        if [[ -n "$_session_id" ]]; then
            _turn=$(cat "$MIND_PATH/.turn_index_${_session_id}" 2>/dev/null || echo 0)
            _soul_sentinel="$MIND_PATH/.soul_injected_${_session_id}_${_turn}"
            [[ -f "$_soul_sentinel" ]] && exit 0
            touch "$_soul_sentinel" 2>/dev/null || true
        fi

        query=""
        tags=""

        if echo "$command" | grep -qiE '(Rscript|R --vanilla|R -e|module.*load.*R)'; then
            query="R environment conda activation"; tags="correction"
        elif echo "$command" | grep -qiE '(python3?[[:space:]]|pip[[:space:]]|conda activate)'; then
            query="python conda environment"; tags="correction"
        elif echo "$command" | grep -qiE '(chittad|daemon)'; then
            query="chittad daemon startup"; tags="correction,gotcha"
        elif echo "$command" | grep -qiE '(git push|git commit|git rebase)'; then
            query="git push commit"; tags="gotcha"
        elif echo "$command" | grep -qiE '(nohup|&$)'; then
            query="background daemon nohup"; tags="correction"
        elif echo "$command" | grep -qiE '(release\.sh|version)'; then
            query="release version bump"; tags="gotcha"
        elif echo "$command" | grep -qiE '(cmake|make|build)'; then
            query="build cmake"; tags="solution"
        fi

        if [[ -n "$query" ]]; then
            escaped_query=$(json_escape "$query")
            memories=""
            for tag in ${tags//,/ }; do
                result=$(timeout 2 "$CHITTA_BIN" recall --query "$escaped_query" --tag "$tag" --limit 1 --text-only 2>/dev/null | head -c 400)
                [[ -n "$result" && "$result" != *"No memories"* ]] && memories="$memories$result\n"
            done
            # No fallback unfiltered recall — avoids cross-domain memory bleed
            if [[ -n "$memories" ]]; then
                escaped_mem=$(json_escape "$memories")
                echo "{\"hookSpecificOutput\":{\"hookEventName\":\"PreToolUse\",\"additionalContext\":\"⚠️ BEFORE RUNNING: $escaped_mem\"}}"
            fi
        fi
        ;;
    Read)
        file_path=$(echo "$STDIN_DATA" | jq -r '.tool_input.file_path // empty')
        [[ -z "$file_path" || ! -f "$file_path" ]] && exit 0

        # Check patch manifest — advisory if file was patched this session
        FP_BIN="${HOME}/.claude/bin/fp"
        if [[ -x "$FP_BIN" ]]; then
            manifest_out=$(echo "$STDIN_DATA" | "$FP_BIN" --read-hook 2>/dev/null)
            if [[ -n "$manifest_out" ]]; then
                echo "$manifest_out"
                exit 0
            fi
        fi

        # Only compress large files (≤200 lines pass through untouched)
        line_count=$(wc -l < "$file_path" 2>/dev/null || echo 0)
        [[ "$line_count" -le 200 ]] && exit 0

        # Use updatedInput to limit Read to 150 lines + advisory context.
        # exit 2 with plain text doesn't work for native tools (only Bash).
        offset=$(echo "$STDIN_DATA" | jq -r '.tool_input.offset // 0')
        escaped_path=$(echo -n "$file_path" | jq -Rs '.')
        printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","additionalContext":"[hook] Large file (%d lines). Reading first 150. Use offset for later sections.","updatedInput":{"file_path":%s,"limit":150,"offset":%s}}}' \
            "$line_count" "$escaped_path" "$offset"
        ;;

    Grep)
        output_mode=$(echo "$STDIN_DATA" | jq -r '.tool_input.output_mode // "files_with_matches"')
        [[ "$output_mode" != "content" ]] && exit 0

        # Inject head_limit:50 into tool input — no pre-run, native tool handles once.
        updated=$(echo "$STDIN_DATA" | jq '.tool_input + {"head_limit": 50}')
        printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","additionalContext":"[hook] Grep content mode — capped at 50 matches.","updatedInput":%s}}' "$updated"
        ;;

    Glob)
        pattern=$(echo "$STDIN_DATA" | jq -r '.tool_input.pattern // empty')
        [[ -z "$pattern" ]] && exit 0

        # Inject head_limit:100 — Glob returns sorted by mtime, 100 is usually enough.
        updated=$(echo "$STDIN_DATA" | jq '.tool_input + {"head_limit": 100}')
        printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","updatedInput":%s}}' "$updated"
        ;;

    Edit)
        FP_BIN="${HOME}/.claude/bin/fp"
        [[ ! -x "$FP_BIN" ]] && exit 0
        # fp --hook reads the full tool JSON, applies patch, exits 2 with confirmation.
        # exit 2 → Claude skips its own Edit execution (fp already wrote the file).
        echo "$STDIN_DATA" | "$FP_BIN" --hook
        exit $?
        ;;

    Write)
        FP_BIN="${HOME}/.claude/bin/fp"
        [[ ! -x "$FP_BIN" ]] && exit 0
        # Block Write on existing files ≥50 lines — suggest file_patch instead.
        echo "$STDIN_DATA" | "$FP_BIN" --write-hook
        exit $?
        ;;

    *)
        exit 0
        ;;
esac

exit 0
