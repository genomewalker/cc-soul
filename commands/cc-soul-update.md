---
description: Update cc-soul binaries (downloads pre-built or builds from source)
---

# /cc-soul-update

Install or update cc-soul binaries. Prefers pre-built binaries, falls back to building from source.

## What it does

1. Downloads ONNX embedding model if missing
2. Tries to download pre-built binaries for your platform
3. Falls back to building from source if pre-built fails
4. Creates database symlinks (including unified storage files)
5. Auto-detects database version and upgrades if needed
6. Optionally converts to unified storage format (roaring bitmap tags, CoW snapshots)
7. Configures MCP tool permissions in settings.json

## Usage

```bash
/cc-soul-update
```

## Implementation

Run all commands in a single bash block (variables don't persist across separate calls):

```bash
PLUGIN_DIR="$HOME/.claude/plugins/marketplaces/genomewalker-cc-soul"
SETTINGS="$HOME/.claude/settings.json"
PERM_RULE="mcp__plugin_cc-soul_cc-soul__*"

bash "$PLUGIN_DIR/scripts/smart-install.sh"
"$PLUGIN_DIR/bin/chitta_cli" --version
"$PLUGIN_DIR/bin/chitta_cli" upgrade 2>&1
"$PLUGIN_DIR/bin/chitta_cli" stats 2>&1 | grep -v "^\["

# Configure permissions if not present
if command -v jq &>/dev/null && [ -f "$SETTINGS" ]; then
  if ! jq -e ".permissions.allow | index(\"$PERM_RULE\")" "$SETTINGS" &>/dev/null; then
    jq ".permissions.allow += [\"$PERM_RULE\"]" "$SETTINGS" > "$SETTINGS.tmp" && mv "$SETTINGS.tmp" "$SETTINGS"
    echo "[cc-soul] Added permission: $PERM_RULE"
  fi
fi
```
