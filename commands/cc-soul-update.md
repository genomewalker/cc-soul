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

## Usage

```bash
/cc-soul-update
```

## Implementation

Run all commands in a single bash block (variables don't persist across separate calls):

```bash
PLUGIN_DIR="$HOME/.claude/plugins/marketplaces/genomewalker-cc-soul"

# smart-install handles daemon shutdown gracefully
bash "$PLUGIN_DIR/scripts/smart-install.sh"
"$PLUGIN_DIR/bin/chitta_cli" --version
"$PLUGIN_DIR/bin/chitta_cli" upgrade 2>&1
"$PLUGIN_DIR/bin/chitta_cli" stats 2>&1 | grep -v "^\["
```
