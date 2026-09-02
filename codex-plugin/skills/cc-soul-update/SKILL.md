---
name: cc-soul-update
description: Update chitta binaries (builds from source, falls back to pre-built)
execution: inline
---

# cc-soul-update

Update chitta binaries to the latest version.

## Usage

```bash
# Run smart-install script
${CLAUDE_PLUGIN_ROOT}/scripts/smart-install.sh
```

This will:
1. Resolve the latest release, which may be newer than the plugin cache, and
   compare it against what is installed
2. Build from source with llama.cpp embeddings when a C++ compiler is present;
   fall back to pre-built binaries when it is not, or when the build fails
3. Download the GGUF embedding model if needed, preserving an existing
   `nomic-embed-text` identity rather than switching vector spaces
4. Install hooks, configure bash permissions and hook wiring
5. Install the Python packages for the MCP server and TUI
6. Set up the systemd user service on Linux
7. Stop the daemon, run store migrations, restart it

Pre-built binaries available for:
- linux-x64
- linux-arm64
- macos-x64
- macos-arm64
