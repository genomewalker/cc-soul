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
pkill -f "chitta m[c]p" 2>/dev/null; sleep 1   # [c] so pkill -f can't match this command's own shell
```
`install` = atomic rename. Never `cp` over running binary → ETXTBSY.

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

<!-- BEGIN sqz-claude-guidance (auto-installed by sqz init; remove this block to disable) -->

## sqz — Context Compression (READ FIRST)

sqz is installed in this project. It compresses tool output so large
files, long logs, and verbose command output cost far fewer tokens.
There are **two ways** sqz is wired in, and you should prefer each
one in the situations below.

### Preferred tools (MCP)

The `sqz-mcp` server is registered in this project's MCP config. It
exposes three read-only tools that compress their output through the
sqz pipeline:

- **`sqz_read_file`** — read a file from disk and return a compressed
  view. **PREFER this over the built-in `Read` tool** for any file
  larger than ~2KB or any file you might read more than once in the
  same session. Repeat reads return a 13-token `§ref:HASH§` reference
  instead of the full content.

- **`sqz_grep`** — search files for a literal string or regex.
  **PREFER this over the built-in `Grep`** for anything that might
  match more than a handful of lines. Caps at 200 matches by default;
  raise with `max_matches` if needed.

- **`sqz_list_dir`** — list a directory. Skips `.git`, `node_modules`,
  `target`, `dist`, `build`, `vendor`, `__pycache__` so the output
  stays focused. **PREFER this over `ls -la` via Bash** when you want
  to see a project layout.

The built-in `Read`, `Grep`, `Glob` tools remain available. Use them for:
- Tiny config files (<1KB) where compression can't help.
- Byte-exact reads you'll hash or diff (lockfiles, signatures).
- Globbing (sqz has no glob tool; `Glob` is still the right choice).

### Bash commands (hooked automatically)

When you run a shell command through the `Bash` tool, a PreToolUse hook
rewrites it to pipe output through `sqz compress`. This is transparent:
you don't need to remember to add anything, but it's useful to know
that these commands get compressed automatically:

```bash
git status           # → git status 2>&1 | sqz compress --cmd git
cargo test           # → cargo test 2>&1 | sqz compress --cmd cargo
docker ps            # → docker ps 2>&1 | sqz compress --cmd docker
kubectl get pods     # → kubectl get pods 2>&1 | sqz compress --cmd kubectl
```

The rewrite is skipped for interactive commands (`vim`, `ssh`,
`python`), compound commands (`a && b`, `a > file.txt`), and anything
already going through sqz.

### Escape hatch — when you see a `§ref:HASH§` token

If tool output contains a `§ref:a1b2c3d4§` token and you need the full
content it points at, resolve it. Three equivalent ways:

- Shell: `/home/kbd606/.claude/bin/sqz expand a1b2c3d4` (or paste the whole token
  `/home/kbd606/.claude/bin/sqz expand §ref:a1b2c3d4§`).
- MCP tool: call `expand` with `{ "prefix": "a1b2c3d4" }`.
- To get uncompressed output for one command: prefix it with
  `SQZ_NO_DEDUP=1` (e.g. `SQZ_NO_DEDUP=1 git log | sqz compress`).

If the compressed output is actively making the task harder (looping
on refs, small retries replacing one big read), call the `passthrough`
MCP tool to get raw text.

### When NOT to use sqz tools

- Writing or editing files — use the built-in `Write`/`Edit` tools.
  sqz has no write tools (by design; see issue #5 follow-up).
- Running commands interactively or in watch mode.
- Reading very small files (<1KB) where compression can't help.

<!-- END sqz-claude-guidance -->
