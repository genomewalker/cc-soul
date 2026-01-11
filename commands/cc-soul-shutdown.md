---
description: Gracefully stop the cc-soul daemon
---

# /cc-soul-shutdown

Gracefully stop the cc-soul daemon (subconscious process). This saves state before shutting down.

## Usage

```bash
/cc-soul-shutdown
```

## Implementation

```bash
PLUGIN_DIR="$HOME/.claude/plugins/marketplaces/genomewalker-cc-soul"
"$PLUGIN_DIR/bin/chitta" shutdown 2>&1
```
