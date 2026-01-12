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

## Usage

```bash
/cc-soul-setup
```

## Implementation

```bash
PLUGIN_DIR="$HOME/.claude/plugins/marketplaces/genomewalker-cc-soul"

# Graceful daemon shutdown before rebuild
"$PLUGIN_DIR/bin/chitta" shutdown 2>/dev/null || "$PLUGIN_DIR/bin/chittad" shutdown 2>/dev/null || true

# Check cmake before deleting binaries
if ! command -v cmake &>/dev/null; then
  echo "[cc-soul] ERROR: cmake required for building from source"
  echo "[cc-soul] Use /cc-soul-update instead to download pre-built binaries"
  exit 1
fi
rm -rf "$PLUGIN_DIR/bin" "$PLUGIN_DIR/chitta/build" 2>/dev/null
bash "$PLUGIN_DIR/setup.sh"
"$PLUGIN_DIR/bin/chittad" --version
"$PLUGIN_DIR/bin/chittad" upgrade 2>&1
"$PLUGIN_DIR/bin/chittad" stats 2>&1 | grep -v "^\["
```

## Optional: Convert to Unified Storage

For better performance with tags and CoW snapshots, convert to unified format:

```bash
"$PLUGIN_DIR/bin/chittad" convert unified --path ~/.claude/mind/chitta 2>&1
```

This creates `.unified`, `.vectors`, `.meta`, `.connections`, `.payloads`, `.edges`, `.tags` files alongside the existing `.hot` file. A backup is created automatically.
