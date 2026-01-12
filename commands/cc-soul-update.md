---
description: Update cc-soul binaries (downloads pre-built or builds from source)
---

# /cc-soul-update

Install or update cc-soul binaries.

## Steps

1. Find the plugin directory (check `~/.claude/plugins/marketplaces/genomewalker-cc-soul` or similar)
2. Run `smart-install.sh` from the plugin's scripts directory - this handles:
   - Downloading ONNX embedding model if missing
   - Downloading pre-built binaries for the platform
   - Falling back to building from source if pre-built fails
   - Creating database symlinks
3. Verify installation by checking `~/.claude/bin/chittad --version`
4. Run `~/.claude/bin/chittad upgrade` to upgrade database if needed
5. Show stats with `~/.claude/bin/chittad stats`

## Notes

- Binaries are installed to `~/.claude/bin/` (not the plugin directory)
- The daemon is gracefully stopped during install
- Database version is auto-detected and upgraded if needed
