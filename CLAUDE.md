# cc-soul

## Build & deploy
```bash
cd chitta && cmake --build build --parallel
install -m 0755 ../bin/chittad ~/.claude/bin/chittad
install -m 0755 ../bin/chitta  ~/.claude/bin/chitta
systemctl --user restart chittad
pkill -f "chitta mcp" 2>/dev/null; sleep 1
```
`install` = atomic rename. Never `cp` over running binary → ETXTBSY.

## Release
`./scripts/release.sh patch|minor|major -y`

## Key files
| | Path |
|---|---|
| Daemon | `chitta/src/simple_cli.cpp` |
| RPC | `chitta/include/chitta/rpc/field_handler.hpp` |
| Store | `chitta-field/src/store.rs` |
| MCP | `chitta-mcp/server.py` |
| Hooks | `hooks/*.sh` |
