---
description: Update cc-soul binaries (downloads pre-built or builds from source)
---

# /cc-soul-update

```ssl
[cc-soul] update: smart-install.sh→{download|build}→~/.claude/bin/{chittad,chitta}
verify: chittad --version→upgrade→stats
locations (priority order):
  1. $PWD/scripts/smart-install.sh (if in cc-soul repo)
  2. ~/.claude/plugins/cache/genomewalker-cc-soul/cc-soul/*/scripts/smart-install.sh (latest version)
```
