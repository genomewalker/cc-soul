#!/bin/bash
# post-edit-hook.sh — incremental re-index after Edit/Write
#
# PostToolUse hook: fires after Edit or Write, queues extract_symbols
# for the changed file if it belongs to an already-indexed project.
# Rate-limited per file to avoid thrashing on rapid edits.

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
RATE_LIMIT_SECS="${CHITTA_EDIT_REINDEX_RATE:-${CC_SOUL_EDIT_REINDEX_RATE:-30}}"

STDIN_DATA=$(cat)

[[ ! -x "$CHITTA_BIN" ]] && exit 0

file_path=$(echo "$STDIN_DATA" | jq -r '.tool_input.file_path // empty' 2>/dev/null)
[[ -z "$file_path" || ! -f "$file_path" ]] && exit 0

# CEC: log edit event to EventTape + CDAWG (all extensions, fire-and-forget)
timeout 0.5 "$CHITTA_BIN" log_event --tool "edit" --entity "$file_path" \
    --outcome 0 --ts_ms "$(date +%s%3N)" >/dev/null 2>&1 &

# Only re-index source extensions tree-sitter understands
case "$file_path" in
    *.rs|*.cpp|*.c|*.h|*.hpp|*.cc|*.cxx \
    |*.py|*.js|*.ts|*.jsx|*.tsx \
    |*.go|*.java|*.rb|*.sh|*.bash \
    |*.lua|*.zig|*.swift|*.kt|*.cs) ;;
    *) exit 0 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh" 2>/dev/null || exit 0

daemon_available || exit 0

# Only re-index if directory is already tracked (don't silently index new projects)
dir_path=$(dirname "$file_path")
dir_syms=$(timeout 1 "$CHITTA_BIN" code_context --path "$dir_path" --json 2>/dev/null \
    | jq -r '.dir_symbols // 0' 2>/dev/null || echo 0)
[[ "${dir_syms:-0}" -le 0 ]] && exit 0

# Rate-limit: one re-index per file per RATE_LIMIT_SECS
rate_key=$(printf '%s' "$file_path" | md5sum | cut -c1-16)
rate_file="$MIND_PATH/.reindex_file_${rate_key}"
if [[ -f "$rate_file" ]]; then
    last=$(cat "$rate_file" 2>/dev/null || echo 0)
    now=$(date +%s)
    [[ $((now - last)) -lt $RATE_LIMIT_SECS ]] && exit 0
fi
printf '%s\n' "$(date +%s)" > "$rate_file"

queue_write "extract_symbols" "{\"path\":\"$file_path\"}"

exit 0
