#!/bin/bash
# PreToolUse hook: Surface corrections/gotchas BEFORE I make mistakes
#
# Searches for relevant warnings based on command patterns.
# Returns context that gets injected into my prompt.

set -e

MATCHER="${1:-}"
[[ -z "$MATCHER" ]] && exit 0

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
[[ ! -x "$CHITTA_BIN" ]] && exit 0

STDIN_DATA=$(cat)

json_escape() {
    echo -n "$1" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

case "$MATCHER" in
    Bash)
        command=$(echo "$STDIN_DATA" | jq -r '.tool_input.command // empty')
        [[ -z "$command" ]] && exit 0

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
