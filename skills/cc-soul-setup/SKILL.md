---
name: cc-soul-setup
description: Build cc-soul from source (requires cmake, make, C++ compiler)
execution: inline
---

# cc-soul-setup

Build chitta binaries from source code.

## Requirements
- cmake
- make
- C++ compiler (g++ or clang++)

## Usage

```bash
# Run setup script from plugin directory
${CLAUDE_PLUGIN_ROOT}/setup.sh
```

This will:
1. Stop any running daemon
2. Build chitta and chittad from source
3. Install to ~/.claude/bin/
4. Download embedding model if needed

If cmake is not available, suggest using `/cc-soul-update` to download pre-built binaries instead.
