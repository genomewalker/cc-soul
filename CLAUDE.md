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

## Code-intel hook enforcement
`hooks/pre-tool-hook.sh` logs every Read/Edit decision to
`$MIND/.hook_shadow.jsonl` (shadow mode, default). Fields:
`tool,file,lines,indexed,decision,reason,enforced`.

| Env | Effect |
|---|---|
| `CC_SOUL_HOOK_ENFORCE=1` | Hard-deny Read on indexed >200L @offset=0, Edit on indexed with old_string >500 chars |
| `CC_SOUL_ALLOW_READ=1`   | Bypass Read deny for this session |
| `CC_SOUL_ALLOW_EDIT=1`   | Bypass Edit deny for this session |

Review data before flipping: `jq -s 'group_by(.decision) \| map({k:.[0].decision,n:length})' ~/.claude/mind/.hook_shadow.jsonl`
