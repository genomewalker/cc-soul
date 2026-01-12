---
description: Build cc-soul from source (requires cmake, make, C++ compiler)
---

# /cc-soul-setup

```ssl
[cc-soul] setup: requires{cmake,make,g++}
find plugin@~/.claude/plugins/marketplaces/genomewalker-cc-soul
shutdown existing→run setup.sh→install@~/.claude/bin
verify: chittad --version→upgrade→stats
no cmake? suggest /cc-soul-update (pre-built)
db@~/.claude/mind/chitta
```
