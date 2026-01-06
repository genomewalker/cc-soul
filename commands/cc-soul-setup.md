---
description: Build cc-soul from source (requires cmake, make, C++ compiler)
---

# /cc-soul-setup

Build cc-soul binaries from source. Use this when pre-built binaries don't work on your system.

## What it does

1. Checks build dependencies (cmake, make, C++ compiler)
2. Downloads ONNX embedding model
3. Builds chitta binaries from source
4. Creates database symlinks
5. Auto-detects database version and upgrades if needed
6. Configures MCP tool permissions in settings.json

## Usage

```bash
/cc-soul-setup
```

## Implementation

```bash
PLUGIN_DIR="$HOME/.claude/plugins/marketplaces/genomewalker-cc-soul"
SETTINGS="$HOME/.claude/settings.json"
PERM_RULE="mcp__plugin_cc-soul_cc-soul__*"

rm -rf "$PLUGIN_DIR/bin" "$PLUGIN_DIR/chitta/build" 2>/dev/null
bash "$PLUGIN_DIR/setup.sh"
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
