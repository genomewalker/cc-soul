---
description: Update cc-soul binaries (downloads pre-built or builds from source)
---

# /cc-soul-update

Install or update cc-soul binaries. Prefers pre-built binaries, falls back to building from source.

## What it does

1. Downloads ONNX embedding model if missing
2. Tries to download pre-built binaries for your platform
3. Falls back to building from source if pre-built fails
4. Creates database symlinks

## Usage

```bash
/cc-soul-update
```

## Implementation

```bash
PLUGIN_DIR="$(find ~/.claude/plugins -type d -name 'genomewalker-cc-soul' 2>/dev/null | grep marketplaces | head -1)"
bash "$PLUGIN_DIR/scripts/smart-install.sh"
"$PLUGIN_DIR/bin/chitta_cli" stats 2>&1 | grep -E "^(Soul|Nodes:|Total:|Yantra:)" | head -5
```
