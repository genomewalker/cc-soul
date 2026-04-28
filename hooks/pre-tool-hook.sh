#!/bin/bash
# PreToolUse hook: safety blocks, large-output guards, soul corrections.
#
# Stages:
# 1. Safety: block obviously destructive commands (rm -rf /, etc.).
# 2. Fallback: warn on unbounded find/grep/ls; suggest a safer form.
# 3. Soul: surface corrections/gotchas from chitta memory before execution.
#
# Output compression for Bash is handled upstream by sqz at the user-global
# hook layer — this hook no longer rewrites commands for token reduction.

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

# ─── Code-intel shadow/enforcement ────────────────────────────────────────────
# Phase 1 (initial): log what the hook WOULD do without changing behavior.
# Phase 2 (auto):    once shadow log has ≥100 entries AND is ≥3 days old, the
#                    hook flips to enforce mode automatically. No env var needed.
#                    Explicit CC_SOUL_HOOK_ENFORCE=0 disables; =1 forces on early.
# Escape hatch:      CC_SOUL_ALLOW_EDIT=1 or CC_SOUL_ALLOW_READ=1 bypass per env.
_should_enforce() {
    # Explicit override wins
    case "${CC_SOUL_HOOK_ENFORCE:-}" in
        1) return 0 ;;
        0) return 1 ;;
    esac
    # Cache result per-session to avoid re-computing on every Read/Edit.
    # Session_id comes from STDIN_DATA, cached in /tmp (per-node safe).
    local sid
    sid=$(echo "$STDIN_DATA" | jq -r '.session_id // empty' 2>/dev/null)
    local cache="/tmp/cc-soul-enforce-${sid:-unknown}-$USER"
    if [[ -f "$cache" ]]; then
        local cached
        cached=$(cat "$cache" 2>/dev/null)
        [[ "$cached" == "1" ]] && return 0
        [[ "$cached" == "0" ]] && return 1
    fi

    local mind="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
    local log="$mind/.hook_shadow.jsonl"
    local decision=0  # default: off
    if [[ -f "$log" && -r "$log" ]]; then
        local n first_ts first_epoch age
        n=$(wc -l < "$log" 2>/dev/null || echo 0)
        if [[ "${n:-0}" -ge 100 ]]; then
            first_ts=$(head -1 "$log" 2>/dev/null | jq -r '.ts // empty' 2>/dev/null)
            if [[ -n "$first_ts" ]]; then
                first_epoch=$(date -d "$first_ts" +%s 2>/dev/null || echo 0)
                if [[ "$first_epoch" -gt 0 ]]; then
                    age=$(( ( $(date +%s) - first_epoch ) / 86400 ))
                    [[ "$age" -ge 3 ]] && decision=1
                fi
            fi
        fi
    fi
    # Cache for rest of session (ignore write errors)
    [[ -n "$sid" ]] && echo "$decision" > "$cache" 2>/dev/null
    [[ "$decision" == "1" ]]
}
#
# Log format: one JSON line per Read/Edit decision →
#   $MIND_PATH/.hook_shadow.jsonl
# Fields: ts, tool, file, lines, indexed, decision, reason, enforced
_shadow_log() {
    local mind="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
    [[ -d "$mind" && -w "$mind" ]] || return 0
    local log="$mind/.hook_shadow.jsonl"
    # Cap log at 10MB — rotate to .1 if exceeded
    if [[ -f "$log" ]]; then
        local sz
        sz=$(stat -c %s "$log" 2>/dev/null || echo 0)
        if [[ "$sz" -gt 10485760 ]]; then
            mv "$log" "$log.1" 2>/dev/null || true
        fi
    fi
    local ts tool file lines indexed decision reason enforced
    ts=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    tool="$1"; file="$2"; lines="$3"; indexed="$4"
    decision="$5"; reason="$6"; enforced="$7"
    printf '{"ts":"%s","tool":"%s","file":%s,"lines":%s,"indexed":%s,"decision":"%s","reason":%s,"enforced":%s}\n' \
        "$ts" "$tool" "$(echo -n "$file" | jq -Rs '.' 2>/dev/null || echo '""')" "${lines:-0}" "${indexed:-0}" \
        "$decision" "$(echo -n "$reason" | jq -Rs '.' 2>/dev/null || echo '""')" "${enforced:-0}" \
        >> "$log" 2>/dev/null || true
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


# ─── Fallback: large-output safety rewrites ──────────────────────────────────
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

        # Code intelligence advisory: if chitta has this file's directory indexed, suggest smart_context/read_symbol
        advisory=""
        if [[ -x "$CHITTA_BIN" ]] && daemon_available; then
            dir_path=$(dirname "$file_path")
            dir_syms=$(timeout 1 "$CHITTA_BIN" code_context --path "$dir_path" --json 2>/dev/null | jq -r '.dir_symbols // 0' 2>/dev/null || echo 0)
            if [[ "$dir_syms" -gt 0 ]]; then
                advisory="[code-intel] File is indexed in chitta ($dir_syms symbols in dir). Prefer: smart_context(task) → read_symbol(file,symbol) → symbol_patch/file_patch. Saves 60-90% tokens vs Read+Edit."
            fi
        fi

        # Only compress large files (≤200 lines pass through untouched)
        line_count=$(wc -l < "$file_path" 2>/dev/null || echo 0)
        is_indexed=0
        [[ "$dir_syms" -gt 0 ]] 2>/dev/null && is_indexed=1
        if [[ "$line_count" -le 200 ]]; then
            # Small file — pass through, but still advise code-intel if available
            _shadow_log "Read" "$file_path" "$line_count" "$is_indexed" "pass" "small-file" 0
            if [[ -n "$advisory" ]]; then
                printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","additionalContext":"%s"}}' "$advisory"
            fi
            exit 0
        fi

        # Use updatedInput to limit Read to 150 lines + advisory context.
        offset=$(echo "$STDIN_DATA" | jq -r '.tool_input.offset // 0')
        # Phase 2: hard-deny on indexed-file large Read at offset=0 (unless escape hatch)
        if _should_enforce && [[ "$is_indexed" == "1" && "$offset" == "0" \
              && "${CC_SOUL_ALLOW_READ:-0}" != "1" ]]; then
            _shadow_log "Read" "$file_path" "$line_count" "$is_indexed" "deny" "indexed-large-offset0" 1
            printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"Indexed source file (%d lines). Use mcp__chitta-bridge__read_symbol or smart_context instead of Read. Set CC_SOUL_ALLOW_READ=1 to override."}}' "$line_count"
            exit 0
        fi
        _shadow_log "Read" "$file_path" "$line_count" "$is_indexed" "truncate" "large-file" 0
        escaped_path=$(echo -n "$file_path" | jq -Rs '.')
        if [[ -n "$advisory" ]]; then
            printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","additionalContext":"[hook] Large file (%d lines). Reading first 150. Use offset for later sections. %s","updatedInput":{"file_path":%s,"limit":150,"offset":%s}}}' \
                "$line_count" "$advisory" "$escaped_path" "$offset"
        else
            printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","additionalContext":"[hook] Large file (%d lines). Reading first 150. Use offset for later sections.","updatedInput":{"file_path":%s,"limit":150,"offset":%s}}}' \
                "$line_count" "$escaped_path" "$offset"
        fi
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
        # Code intelligence advisory / enforcement
        file_path=$(echo "$STDIN_DATA" | jq -r '.tool_input.file_path // empty')
        old_str_len=$(echo "$STDIN_DATA" | jq -r '.tool_input.old_string // empty' | wc -c)
        is_indexed=0
        dir_syms=0
        if [[ -n "$file_path" && -x "$CHITTA_BIN" ]] && daemon_available; then
            dir_path=$(dirname "$file_path")
            dir_syms=$(timeout 1 "$CHITTA_BIN" code_context --path "$dir_path" --json 2>/dev/null | jq -r '.dir_symbols // 0' 2>/dev/null || echo 0)
            [[ "$dir_syms" -gt 0 ]] && is_indexed=1
        fi

        # Phase 2: hard-deny on indexed + large old_str (whole-function territory)
        if _should_enforce && [[ "$is_indexed" == "1" && "$old_str_len" -gt 500 \
              && "${CC_SOUL_ALLOW_EDIT:-0}" != "1" ]]; then
            _shadow_log "Edit" "$file_path" 0 "$is_indexed" "deny" "indexed-large-old_str" 1
            printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"Indexed source file with large Edit (old_string=%d chars). Use mcp__chitta-bridge__symbol_patch or file_patch. Set CC_SOUL_ALLOW_EDIT=1 to override."}}' "$old_str_len"
            exit 0
        fi

        if [[ "$is_indexed" == "1" ]]; then
            _shadow_log "Edit" "$file_path" 0 "$is_indexed" "advise" "indexed" 0
            printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","additionalContext":"[code-intel] File is indexed. Prefer symbol_patch(file,symbol,body) or file_patch(file,old_str,new_str) — no Read needed, fewer tokens."}}\n'
        fi

        # ─── Edit → symbol_patch redirect ─────────────────────────────────────────────
        FILE_PATH=$(echo "$STDIN_DATA" | jq -r '.tool_input.file_path // empty' 2>/dev/null)
        OLD_STR=$(echo "$STDIN_DATA" | jq -r '.tool_input.old_string // empty' 2>/dev/null)
        NEW_STR=$(echo "$STDIN_DATA" | jq -r '.tool_input.new_string // empty' 2>/dev/null)

        if [[ -x "$CHITTA_BIN" && -n "$FILE_PATH" && -n "$OLD_STR" && -n "$NEW_STR" ]]; then
            SYMBOL=$(echo "$OLD_STR" | grep -m1 -oE '(def |fn |class |pub fn |pub struct )[a-zA-Z_][a-zA-Z0-9_]*' | awk '{print $NF}' 2>/dev/null || true)
            if [[ -n "$SYMBOL" ]]; then
                echo "{\"hookSpecificOutput\":{\"hookEventName\":\"PreToolUse\",\"additionalContext\":\"[fp] symbol_patch available for $SYMBOL in $FILE_PATH — prefer mcp__chitta-bridge__symbol_patch over Edit for this change\"}}"
                exit 0
            fi
        fi
        # ──────────────────────────────────────────────────────────────────────────────

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

    Agent)
        # ─── Subagent budget tracking ────────────────────────────────────────────
        # Each subagent cold-starts a new cache window (~$5-50 per agent).
        # Track count per session and warn/advise when spending is high.
        MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
        _session_id=$(echo "$STDIN_DATA" | jq -r '.session_id // empty')
        if [[ -n "$_session_id" ]]; then
            AGENT_COUNT_FILE="$MIND_PATH/.subagent_count_${_session_id}"
            AGENT_COUNT=$(cat "$AGENT_COUNT_FILE" 2>/dev/null || echo 0)
            AGENT_COUNT=$((AGENT_COUNT + 1))
            echo "$AGENT_COUNT" > "$AGENT_COUNT_FILE"

            AGENT_WARN_THRESHOLD="${CC_SOUL_AGENT_WARN:-20}"
            AGENT_HARD_LIMIT="${CC_SOUL_AGENT_LIMIT:-50}"

            if [[ $AGENT_COUNT -gt $AGENT_HARD_LIMIT ]]; then
                printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","additionalContext":"[agent-budget] %d/%d subagents spawned this session. Each agent cold-starts a new cache (~$5-50). Consider batching work into fewer agents, or start a fresh session with /recap."}}\n' \
                    "$AGENT_COUNT" "$AGENT_HARD_LIMIT"
            elif [[ $AGENT_COUNT -gt $AGENT_WARN_THRESHOLD ]]; then
                printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","additionalContext":"[agent-budget] %d subagents this session. Each spawns a new cache window. Batch independent queries into single agents where possible."}}\n' \
                    "$AGENT_COUNT"
            fi

            # Model routing advice: suggest haiku for exploration/research subagents
            agent_type=$(echo "$STDIN_DATA" | jq -r '.tool_input.subagent_type // .tool_input.description // empty' 2>/dev/null)
            agent_model=$(echo "$STDIN_DATA" | jq -r '.tool_input.model // empty' 2>/dev/null)
            if [[ -z "$agent_model" ]]; then
                # No model override — check if this is a research/exploration task
                if echo "$agent_type" | grep -qiE '(explore|search|find|research|grep|glob|read)'; then
                    printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","additionalContext":"[token-hint] Exploration subagent inherits parent model. Add model:\"haiku\" for 10x cheaper research agents."}}\n'
                fi
            fi
        fi
        ;;

    *)
        exit 0
        ;;
esac

exit 0
