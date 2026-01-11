---
description: Check if cc-soul daemon is running
---

# /cc-soul-status

Check the status of the cc-soul daemon (subconscious process).

## Usage

```bash
/cc-soul-status
```

## Implementation

```bash
PLUGIN_DIR="$HOME/.claude/plugins/marketplaces/genomewalker-cc-soul"
"$PLUGIN_DIR/bin/chitta" status 2>&1
```
