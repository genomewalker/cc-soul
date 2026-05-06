#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOOKS_FILE="${HOOKS_FILE:-${HOME}/.codex/hooks.json}"

if ! command -v jq >/dev/null 2>&1; then
  echo "[cc-soul] jq not found, skipping Codex hook config" >&2
  exit 0
fi

mkdir -p "$(dirname "$HOOKS_FILE")"
[[ -f "$HOOKS_FILE" ]] || printf '%s\n' '{"hooks":{}}' > "$HOOKS_FILE"

TMP="$(mktemp)"
jq \
  --arg root "$ROOT_DIR" \
  '
  def is_cc_soul_cmd($cmd):
    ($cmd | startswith($root + "/hooks/"))
    or ($cmd | contains("/.claude/plugins/cache/genomewalker-cc-soul/cc-soul/"));
  def strip_ours($arr):
    ($arr // [])
    | map(
        .hooks = (
          (.hooks // [])
          | map(select((.command? | is_cc_soul_cmd(.)) | not))
        )
      )
    | map(select((.hooks | length) > 0));
  .hooks = (.hooks // {}) |
  .hooks.SessionStart = (strip_ours(.hooks.SessionStart) + [
    {"matcher":"startup","hooks":[{"type":"command","command":($root+"/hooks/codex-session-start-wrapper.sh"),"timeout":10}]},
    {"matcher":"compact","hooks":[{"type":"command","command":($root+"/hooks/compact-restore-hook.sh"),"timeout":5}]},
    {"matcher":"resume","hooks":[{"type":"command","command":($root+"/hooks/codex-session-start-wrapper.sh"),"timeout":10},{"type":"command","command":($root+"/hooks/resume-inject-hook.sh"),"timeout":3}]},
    {"matcher":"clear","hooks":[{"type":"command","command":($root+"/hooks/codex-session-start-wrapper.sh"),"timeout":10}]}
  ]) |
  .hooks.UserPromptSubmit = (strip_ours(.hooks.UserPromptSubmit) + [
    {"matcher":".*","hooks":[{"type":"command","command":($root+"/hooks/codex-prompt-hook.sh"),"timeout":10}]}
  ]) |
  .hooks.Stop = (strip_ours(.hooks.Stop) + [
    {"matcher":".*","hooks":[{"type":"command","command":($root+"/hooks/codex-stop-hook.sh")}]}
  ]) |
  .hooks.PostToolUse = (strip_ours(.hooks.PostToolUse) + [
    {"matcher":"Bash","hooks":[{"type":"command","command":($root+"/hooks/log-bash-history.sh"),"timeout":5},{"type":"command","command":($root+"/hooks/post-bash-hook.sh"),"timeout":5}]},
    {"matcher":"Write","hooks":[{"type":"command","command":($root+"/hooks/memory-intercept.sh"),"timeout":10}]}
  ])
  ' "$HOOKS_FILE" > "$TMP"

mv "$TMP" "$HOOKS_FILE"
echo "[cc-soul] Configured Codex hooks in $HOOKS_FILE"
