# cc-soul

## Build & deploy
```bash
cd chitta && cmake --build build --parallel
install -m 0755 ../bin/chittad ~/.claude/bin/chittad
install -m 0755 ../bin/chitta  ~/.claude/bin/chitta
# chitta_hintd exists only when built with CHITTA_WITH_LLAMA_CPP=ON
[ -f ../bin/chitta_hintd ] && install -m 0755 ../bin/chitta_hintd ~/.claude/bin/chitta_hintd
systemctl --user restart chittad
systemctl --user try-restart chitta-hintd 2>/dev/null || true
pkill -f "chitta-m[c]p" 2>/dev/null; sleep 1   # process is `chitta-mcp` (hyphen); [c] so pkill -f can't match its own shell
```
`install` = atomic rename. Never `cp` over running binary → ETXTBSY.

## Dev install (editable — repo IS the live plugin)
The live plugin loads **hooks + MCP python from `~/.claude/hooks/*` and
`~/.claude/plugins/marketplaces/genomewalker-cc-soul/chitta-mcp/*`** — NOT from
this repo directly. `scripts/dev-install.sh` symlinks those live paths back at
this repo so `edit repo → restart service → live`, one source of truth, no drift.
```bash
bash scripts/dev-install.sh   # symlinks hooks + MCP *.py to this repo; restarts MCP
```
- Hooks are live immediately (bash re-reads per invocation). MCP python needs the
  MCP restart (the script does `pkill -f "chitta-m[c]p"`).
- Binaries are the exception: keep the `build → install` (atomic) flow above —
  symlinking a running binary risks ETXTBSY.
- ⚠️ The marketplace is a **git checkout** that a plugin update can re-clone,
  replacing the symlinks with a stale copy (this is what caused the
  `server.py` dual-copy drift). Re-run `dev-install.sh` after any plugin update.

## Release
`./scripts/release.sh patch|minor|major -y`

⚠️ **Rollback floor: chitta-field v2.1.0.** Snapshots are written in the V23
sectioned format since v2.1.0 — older daemons can't read the magic. Rollback
below v2.1.0 only works while a pre-V23 snapshot family still exists on disk
(`prune_old_snapshots` keeps 2 families, so ~2 save cycles after upgrade).

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

Enforce mode auto-activates once shadow log has ≥100 entries AND is
≥3 days old — no manual env flip needed.

| Env | Effect |
|---|---|
| `CC_SOUL_HOOK_ENFORCE=1` | Force enforce on early (skip wait) |
| `CC_SOUL_HOOK_ENFORCE=0` | Force shadow only (disable enforcement) |
| `CC_SOUL_ALLOW_READ=1`   | Bypass Read deny for this session |
| `CC_SOUL_AGENT_NO_FORCE=1` | Disable haiku-force on research subagents (advisory only) |
| `CC_SOUL_AGENT_WARN=N` | Subagent count to warn at (default 20) |
| `CC_SOUL_AGENT_LIMIT=N` | Subagent count for hard advisory (default 50) |
| `CC_SOUL_SUBAGENT_BASH_RECALL=1` | Run Bash recall for subagent calls too (adds 2s/call) |
| `CC_SOUL_ALLOW_READ=1` | Bypass Read dedup deny for this session (also bypasses indexed-large deny) |

Review data: `./scripts/hook-stats.sh` (decisions, reasons, tool split, enforce-status).

⚠️ **Testing manually**: `CC_SOUL_HOOK_ENFORCE=1 bash hook.sh Read` won't work — the prefix-assignment isn't exported to nested bash. Use `export CC_SOUL_HOOK_ENFORCE=1` first.
