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
        [[ ! -x "$CHITTA_BIN" ]] && exit 0
        daemon_available || exit 0

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
            if [[ -n "$tags" ]]; then
                for tag in ${tags//,/ }; do
                    result=$(timeout 2 "$CHITTA_BIN" recall --query "$escaped_query" --tag "$tag" --limit 1 --text-only 2>/dev/null | head -c 400)
                    [[ -n "$result" && "$result" != *"No memories"* ]] && memories="$memories$result\n"
                done
            fi
            if [[ -z "$memories" ]]; then
                memories=$(timeout 2 "$CHITTA_BIN" recall --query "$escaped_query" --limit 1 --text-only 2>/dev/null | head -c 400)
            fi
            if [[ -n "$memories" && "$memories" != *"No memories"* ]]; then
                escaped_mem=$(json_escape "$memories")
                echo "{\"hookSpecificOutput\":{\"hookEventName\":\"PreToolUse\",\"additionalContext\":\"⚠️ BEFORE RUNNING: $escaped_mem\"}}"
            fi
        fi
        ;;
    Read)
        file_path=$(echo "$STDIN_DATA" | jq -r '.tool_input.file_path // empty')
        [[ -z "$file_path" || ! -f "$file_path" ]] && exit 0

        # Only compress large files (≤200 lines pass through untouched)
        line_count=$(wc -l < "$file_path" 2>/dev/null || echo 0)
        [[ "$line_count" -le 200 ]] && exit 0

        head_n=100
        tail_n=50
        omitted=$(( line_count - head_n - tail_n ))
        output=$(head -n "$head_n" "$file_path"; echo ""; echo "... [$omitted lines omitted, total $line_count] ..."; echo ""; tail -n "$tail_n" "$file_path")
        printf '[RTK:Read] %s (%d lines)\n\n%s\n' "$file_path" "$line_count" "$output"
        exit 2
        ;;

    Grep)
        pattern=$(echo "$STDIN_DATA" | jq -r '.tool_input.pattern // empty')
        path=$(echo "$STDIN_DATA" | jq -r '.tool_input.path // "."')
        glob=$(echo "$STDIN_DATA" | jq -r '.tool_input.glob // empty')
        output_mode=$(echo "$STDIN_DATA" | jq -r '.tool_input.output_mode // "files_with_matches"')
        [[ -z "$pattern" ]] && exit 0

        # Only compress content-mode results (files_with_matches is already compact)
        [[ "$output_mode" != "content" ]] && exit 0

        args=(-n --color=never)
        [[ -n "$glob" ]] && args+=(--glob "$glob")
        raw=$(rg "${args[@]}" "$pattern" "$path" 2>/dev/null | head -500)
        total=$(echo "$raw" | wc -l)

        if [[ "$total" -le 50 ]]; then
            exit 0  # Small — let native tool handle it
        fi

        # Group by file, show up to 5 matches per file
        output=$(echo "$raw" | awk -F: '
            {
                file=$1; rest=substr($0, index($0,$2))
                counts[file]++
                if (counts[file] <= 5) lines[file] = lines[file] "  " rest "\n"
            }
            END {
                for (f in counts) {
                    printf "%s: %d match%s\n", f, counts[f], (counts[f]>1?"es":"")
                    printf "%s", lines[f]
                    if (counts[f] > 5) printf "  ... [%d more]\n", counts[f]-5
                }
            }
        ')
        printf '[RTK:Grep] pattern=%s  total=%d matches\n\n%s\n' "$pattern" "$total" "$output"
        exit 2
        ;;

    Glob)
        pattern=$(echo "$STDIN_DATA" | jq -r '.tool_input.pattern // empty')
        [[ -z "$pattern" ]] && exit 0

        # Count results first — only compress if large
        count=$(python3 -c "import glob, sys; print(len(glob.glob('$pattern', recursive=True)))" 2>/dev/null || echo 0)
        [[ "$count" -le 50 ]] && exit 0

        results=$(python3 -c "
import glob, sys
hits = sorted(glob.glob('$pattern', recursive=True))
for h in hits[:100]:
    print(h)
if len(hits) > 100:
    print(f'... [{len(hits)-100} more]')
" 2>/dev/null)
        printf '[RTK:Glob] pattern=%s  %d files\n\n%s\n' "$pattern" "$count" "$results"
        exit 2
        ;;

    *)
        exit 0
        ;;
esac

exit 0
