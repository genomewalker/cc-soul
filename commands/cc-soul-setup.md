---
description: Build cc-soul from source (requires cmake, make, C++ compiler)
---

# /cc-soul-setup

Build cc-soul binaries from source. Use when pre-built binaries don't work on your system.

## Prerequisites

- cmake (3.14+)
- C++ compiler with C++17 support (g++ or clang++)
- make

## What to do

1. Verify build dependencies are available (cmake, make, C++ compiler)
2. Find the plugin directory (`~/.claude/plugins/marketplaces/genomewalker-cc-soul`)
3. Gracefully shutdown existing daemon if running
4. Run `setup.sh` from the plugin directory - this handles:
   - Downloading ONNX embedding model if missing
   - Building chitta binaries from source
   - Installing to `~/.claude/bin/`
   - Creating database symlinks
5. Verify installation with `~/.claude/bin/chittad --version`
6. Run `~/.claude/bin/chittad upgrade` if database needs upgrading
7. Show stats with `~/.claude/bin/chittad stats`

## Notes

- Binaries are installed to `~/.claude/bin/`
- If cmake is not available, suggest using `/cc-soul-update` for pre-built binaries
- Database is at `~/.claude/mind/chitta`
