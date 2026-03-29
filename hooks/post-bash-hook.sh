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

# Track command sequence for habit learning on success
if [[ "$exit_code" == "0" && -n "$command" ]]; then
    curr_cmd=$(normalize_cmd "$command")

    # Read previous command if exists
    if [[ -f "$LAST_CMD_FILE" ]]; then
        prev_cmd=$(cat "$LAST_CMD_FILE" 2>/dev/null)

        # Record habit if both commands present and different
        if [[ -n "$prev_cmd" && "$prev_cmd" != "$curr_cmd" ]]; then
            # Queue habit_observe async to not block
            (
                "$CHITTA_BIN" habit_observe \
                    --trigger "bash:$prev_cmd" \
                    --response "bash:$curr_cmd" 2>/dev/null || true
            ) &
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
