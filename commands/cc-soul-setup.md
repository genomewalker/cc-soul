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

## Usage

```bash
/cc-soul-setup
```

## Implementation

```bash
PLUGIN_DIR=~/.claude/plugins/marketplaces/genomewalker-cc-soul
rm -rf "$PLUGIN_DIR/bin" "$PLUGIN_DIR/chitta/build" 2>/dev/null
bash "$PLUGIN_DIR/setup.sh"
"$PLUGIN_DIR/bin/chitta_cli" --version
```
