#!/usr/bin/env bash
# Synchronize the canonical repo hook set into active Claude/Codex installs.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOOKS_SRC="$ROOT_DIR/hooks"
HOOK_MANIFEST="$HOOKS_SRC/install-manifest.txt"
CHECK_ONLY=false
[[ "${1:-}" == "--check" ]] && CHECK_ONLY=true

[[ -f "$HOOK_MANIFEST" ]] || {
    echo "[cc-soul] missing $HOOK_MANIFEST" >&2
    exit 1
}

drift=0
sync_one() {
    local source="$1" target="$2" mode="${3:-0755}"
    if [[ -f "$target" ]] && cmp -s "$source" "$target"; then
        return 0
    fi
    drift=$((drift + 1))
    if [[ "$CHECK_ONLY" == "true" ]]; then
        echo "[drift] $target"
        return 0
    fi
    mkdir -p "$(dirname "$target")"
    local stage="$(dirname "$target")/.$(basename "$target").cc-soul-sync"
    install -m "$mode" "$source" "$stage"
    mv -f "$stage" "$target"
    echo "[sync] $target"
}

# User-level fallback hooks. These are removed from settings when the plugin is
# enabled, but keeping the files synchronized supports plugin-disabled installs.
while IFS= read -r script; do
    [[ -z "$script" || "$script" == \#* ]] && continue
    sync_one "$HOOKS_SRC/$script" "$HOME/.claude/hooks/$script"
done < "$HOOK_MANIFEST"

# Claude executes hooks from its versioned plugin cache. During local
# development, refresh that exact active install without guessing the version.
installed_json="$HOME/.claude/plugins/installed_plugins.json"
claude_root=""
if [[ -f "$installed_json" ]]; then
    claude_root=$(jq -r '
      .plugins["cc-soul@genomewalker-cc-soul"][]?
      | select(.scope == "user") | .installPath
    ' "$installed_json" | tail -1)
fi
if [[ -n "$claude_root" && -d "$claude_root" ]]; then
    while IFS= read -r script; do
        [[ -z "$script" || "$script" == \#* ]] && continue
        sync_one "$HOOKS_SRC/$script" "$claude_root/hooks/$script"
    done < "$HOOK_MANIFEST"
    sync_one "$HOOKS_SRC/hooks.json" "$claude_root/hooks/hooks.json" 0644
    for module in session_registry.py resume_selector.py task_ledger.py \
                  thread_inference.py resume_capsule.py mdl_gate.py \
                  outcome_ledger.py; do
        sync_one "$ROOT_DIR/chitta-mcp/$module" "$claude_root/chitta-mcp/$module" 0755
    done
fi

# Codex hook commands point at ROOT_DIR, while its cached skills need their own
# refresh so both frontends follow the same recap/resume ownership protocol.
codex_root="$HOME/.codex/plugins/cache/local/cc-soul/local"
if [[ -d "$codex_root" ]]; then
    for skill in recap resume; do
        sync_one "$ROOT_DIR/codex-plugin/skills/$skill/SKILL.md" \
                 "$codex_root/skills/$skill/SKILL.md" 0644
    done
fi

settings="$HOME/.claude/settings.json"
if [[ "$CHECK_ONLY" == "true" && -f "$settings" ]] && \
   jq -e '.enabledPlugins["cc-soul@genomewalker-cc-soul"] == true' "$settings" >/dev/null; then
    if jq -e '
      [.hooks[][]?.hooks[]?
       | (.command? // "")
       | select(test("(subconscious|session-start-hook|compact-restore-hook|resume-inject-hook|prompt-hook|stop-hook|session-end-hook|pre-compact-hook|subagent-stop-hook|file-changed-hook|pre-tool-hook|post-bash-hook|log-bash-history|memory-intercept)\\.sh"))]
      | length > 0
    ' "$settings" >/dev/null; then
        echo "[drift] duplicate user-level cc-soul hook entries in $settings"
        drift=$((drift + 1))
    fi
fi

# Compare Codex's effective hook JSON by applying the idempotent configurator
# to a staged copy, preserving unrelated hooks in the comparison.
if [[ "$CHECK_ONLY" == "true" ]]; then
    codex_hooks="$HOME/.codex/hooks.json"
    codex_stage=$(mktemp)
    if [[ -f "$codex_hooks" ]]; then
        install -m 0600 "$codex_hooks" "$codex_stage"
    else
        printf '%s\n' '{"hooks":{}}' > "$codex_stage"
    fi
    HOOKS_FILE="$codex_stage" "$ROOT_DIR/scripts/configure-codex-hooks.sh" >/dev/null
    if [[ ! -f "$codex_hooks" ]] || ! cmp -s "$codex_hooks" "$codex_stage"; then
        echo "[drift] $codex_hooks"
        drift=$((drift + 1))
    fi
    rm -f "$codex_stage"
fi

if [[ "$CHECK_ONLY" == "false" ]]; then
    "$ROOT_DIR/scripts/configure-codex-hooks.sh" >/dev/null

    # Plugin hooks.json is authoritative. Strip only cc-soul's legacy global
    # hook commands while preserving unrelated user hooks in the same event.
    if [[ -f "$settings" ]] && \
       jq -e '.enabledPlugins["cc-soul@genomewalker-cc-soul"] == true' "$settings" >/dev/null; then
        stage="${settings}.cc-soul-sync"
        jq '
          def is_cc_soul_hook:
            ((.command? // "") | test("(subconscious|session-start-hook|compact-restore-hook|resume-inject-hook|prompt-hook|stop-hook|session-end-hook|pre-compact-hook|subagent-stop-hook|file-changed-hook|pre-tool-hook|post-bash-hook|log-bash-history|memory-intercept)\\.sh"));
          def strip_cc_soul:
            map(.hooks = ((.hooks // []) | map(select((is_cc_soul_hook | not)))))
            | map(select((.hooks | length) > 0));
          .hooks = ((.hooks // {}) | with_entries(.value |= strip_cc_soul))
          | .hooks = (.hooks | with_entries(select((.value | length) > 0)))
        ' "$settings" > "$stage"
        if cmp -s "$settings" "$stage"; then
            rm -f "$stage"
        else
            mv -f "$stage" "$settings"
            echo "[sync] removed duplicate user-level cc-soul hook entries"
        fi
    fi
fi

if [[ "$CHECK_ONLY" == "true" ]]; then
    if (( drift > 0 )); then
        echo "[cc-soul] $drift installed hook/skill files differ from the repo" >&2
        exit 1
    fi
    echo "[cc-soul] installed hooks and shared skills match the repo"
else
    echo "[cc-soul] hook/skill synchronization complete ($drift files updated)"
fi
