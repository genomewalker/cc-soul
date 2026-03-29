#!/bin/bash
# PreToolUse hook: Surface corrections/gotchas BEFORE I make mistakes
#
# Searches for relevant warnings based on command patterns.
# Returns context that gets injected into my prompt.

# Don't use set -e: we want hooks to succeed even if some parts fail

MATCHER="${1:-}"
[[ -z "$MATCHER" ]] && exit 0

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
STDIN_DATA=$(cat)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh" 2>/dev/null || true

json_escape() {
    echo -n "$1" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

classify_and_rewrite() {
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

    local rewritten=""
    local reason=""

    if echo "$cmd" | grep -qE '^\s*cat\s+'; then
        local file
        # Strip pipes/redirects before extracting filename
        file=$(echo "$cmd" | sed -n 's/^\s*cat\s\+\([^ |&;>]*\).*/\1/p' | head -1)
        if [[ -n "$file" && -f "$file" ]]; then
            local lines
            lines=$(wc -l < "$file" 2>/dev/null || echo 0)
            if [[ "$lines" -gt 500 ]]; then
                rewritten="head -200 $file && wc -l $file"
                reason="File has $lines lines; showing head + line count"
            fi
        fi
    elif echo "$cmd" | grep -qE '^\s*find\s+(/|~)\s+-name'; then
        if ! echo "$cmd" | grep -q 'maxdepth'; then
            rewritten=$(echo "$cmd" | sed 's/\(find\s\+[^ ]*\)/\1 -maxdepth 5/')
            reason="Unbounded find on / or ~; added -maxdepth 5"
        fi
    elif echo "$cmd" | grep -qE '^\s*grep\s+-r\s+'; then
        local has_path
        has_path=$(echo "$cmd" | sed 's/^\s*grep\s\+-r\s\+//' | sed "s/^'[^']*'//;s/^\"[^\"]*\"//;s/^[^ ]*//" | sed 's/^\s*//')
        if [[ -z "$has_path" || "$has_path" == "|"* ]]; then
            if ! echo "$cmd" | grep -q 'head'; then
                rewritten="$cmd | head -200"
                reason="Unbounded recursive grep; added head limit"
            fi
        fi
    elif echo "$cmd" | grep -qE '^\s*ls\s+.*-[^ ]*R' || echo "$cmd" | grep -qE '^\s*ls\s+-la\s+/\s*$'; then
        if ! echo "$cmd" | grep -q 'head'; then
            rewritten="$cmd | head -100"
            reason="Large directory listing; added head limit"
        fi
    fi

    if [[ -n "$rewritten" ]]; then
        # updatedInput not supported in current Claude Code — inject strong advisory instead
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

        if command -v jq >/dev/null 2>&1; then
            rewrite_result=$(classify_and_rewrite "$command")
            rewrite_rc=$?
            if [[ $rewrite_rc -eq 2 ]]; then
                echo "$rewrite_result"
                exit 2
            elif [[ $rewrite_rc -eq 0 && -n "$rewrite_result" ]]; then
                printf '⚠️ BEFORE RUNNING: %s\n' "$rewrite_result"
            fi
        fi

        # Daemon gate: chitta recall only runs when daemon is available
        [[ ! -x "$CHITTA_BIN" ]] && exit 0
        daemon_available || exit 0

        query=""
        tags=""

        # Pattern detection - build query from command content
        if echo "$command" | grep -qiE '(Rscript|R --vanilla|R -e|module.*load.*R)'; then
            query="R environment conda activation"
            tags="correction"
        elif echo "$command" | grep -qiE '(python3?[[:space:]]|pip[[:space:]]|conda activate)'; then
            query="python conda environment"
            tags="correction"
        elif echo "$command" | grep -qiE '(chittad|daemon)'; then
            query="chittad daemon startup"
            tags="correction,gotcha"
        elif echo "$command" | grep -qiE '(git push|git commit|git rebase)'; then
            query="git push commit"
            tags="gotcha"
        elif echo "$command" | grep -qiE '(nohup|&$)'; then
            query="background daemon nohup"
            tags="correction"
        elif echo "$command" | grep -qiE '(release\.sh|version)'; then
            query="release version bump"
            tags="gotcha"
        elif echo "$command" | grep -qiE '(cmake|make|build)'; then
            query="build cmake"
            tags="solution"
        fi

        if [[ -n "$query" ]]; then
            escaped_query=$(json_escape "$query")

            # Search corrections and gotchas
            memories=""
            if [[ -n "$tags" ]]; then
                for tag in ${tags//,/ }; do
                    result=$(timeout 2 "$CHITTA_BIN" recall --query "$escaped_query" --tag "$tag" --limit 1 --text-only 2>/dev/null | head -c 400)
                    [[ -n "$result" && "$result" != *"No memories"* ]] && memories="$memories$result\n"
                done
            fi

            # Fallback: general search if no tagged results
            if [[ -z "$memories" ]]; then
                memories=$(timeout 2 "$CHITTA_BIN" recall --query "$escaped_query" --limit 1 --text-only 2>/dev/null | head -c 400)
            fi

            if [[ -n "$memories" && "$memories" != *"No memories"* ]]; then
                escaped_mem=$(json_escape "$memories")
                echo "{\"hookSpecificOutput\":{\"hookEventName\":\"PreToolUse\",\"additionalContext\":\"⚠️ BEFORE RUNNING: $escaped_mem\"}}"
            fi
        fi
        ;;
    *)
        exit 0
        ;;
esac

exit 0
