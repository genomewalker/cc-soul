# cc-soul Development

## Build & Deploy
```bash
cd chitta && cmake --build build --parallel
# Atomically replace the on-disk binary FIRST — the running daemon keeps its
# old inode mapped and is unaffected until we restart. This keeps the daemon
# up until the new binary is fully written.
install -m 0755 ../bin/chittad ~/.claude/bin/chittad
install -m 0755 ../bin/chitta  ~/.claude/bin/chitta
systemctl --user restart chittad
pkill -f "chitta mcp" 2>/dev/null; sleep 1
```
`~/.claude/bin/chittad` is NOT a symlink — `install` overwrites via a temp
file + atomic rename, so there is no partial-binary window. Never `cp`
directly over a running binary and then start — some filesystems (NFS, some
fuse mounts) will return ETXTBSY or leave the running process on a corrupt
image.

## Release
```bash
./scripts/release.sh patch|minor|major -y
```

## Key Files
| Component | Location |
|-----------|----------|
| Daemon | `chitta/src/simple_cli.cpp` |
| RPC Handlers | `chitta/include/chitta/rpc/field_handler.hpp` |
| Memory Store | `chitta-field/src/store.rs` |
| MCP Server | `chitta-mcp/server.py` |
| Hooks | `hooks/*.sh` |

## Make No Mistakes
Double-check all facts, code, and reasoning before responding. Prefer accuracy over speed.
