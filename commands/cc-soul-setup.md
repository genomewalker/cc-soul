---
description: Build cc-soul from source (requires cmake, make, C++ compiler)
---

# /cc-soul-setup

Build cc-soul binaries from source. Use this when pre-built binaries don't work on your system.

## What it does

1. Checks build dependencies (cmake, make, C++ compiler)
2. Downloads ONNX embedding model
3. Builds chitta binaries from source
4. Creates database symlinks (including unified storage files)
5. Auto-detects database version and upgrades if needed
6. Optionally converts to unified storage format (roaring bitmap tags, CoW snapshots)
7. Configures MCP tool permissions in settings.json

## Usage

```bash
/cc-soul-setup
```

## Implementation

```bash
PLUGIN_DIR="$HOME/.claude/plugins/marketplaces/genomewalker-cc-soul"
SETTINGS="$HOME/.claude/settings.json"
PERM_RULE="mcp__plugin_cc-soul_cc-soul__*"

# Kill any running daemon before rebuild (versioned sockets require matching version)
pkill -f "chitta_cli daemon" 2>/dev/null && echo "[cc-soul] Stopped existing daemon" || true
rm -f /tmp/chitta*.sock 2>/dev/null

# Check cmake before deleting binaries
if ! command -v cmake &>/dev/null; then
  echo "[cc-soul] ERROR: cmake required for building from source"
  echo "[cc-soul] Use /cc-soul-update instead to download pre-built binaries"
  exit 1
fi
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

## Optional: Convert to Unified Storage

For better performance with tags and CoW snapshots, convert to unified format:

```bash
"$PLUGIN_DIR/bin/chitta_cli" convert unified --path ~/.claude/mind/chitta 2>&1
```

This creates `.unified`, `.vectors`, `.meta`, `.connections`, `.payloads`, `.edges`, `.tags` files alongside the existing `.hot` file. A backup is created automatically.
