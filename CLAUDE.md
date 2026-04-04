# cc-soul Development

## Build & Deploy
```bash
cd chitta && cmake --build build --parallel
systemctl --user stop chittad
cp ../bin/chittad ../bin/chitta ~/.claude/bin/
systemctl --user start chittad
pkill -f "chitta mcp" 2>/dev/null; sleep 1
```
`~/.claude/bin/chittad` is NOT a symlink — copy explicitly after each build.

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
