# cc-soul Development

This file is for **contributors developing cc-soul itself**. For user-facing instructions, see `.claude-plugin/CLAUDE.md`.

## Building Chitta

**Always follow all three steps after code changes:**
```bash
cd chitta && cmake --build build --parallel          # 1. Build
pkill -TERM chittad 2>/dev/null; sleep 1             # 2. Stop daemon (prevents ETXTBSY)
cp bin/chitta bin/chittad ~/.claude/bin/              # 3. Install
```

Daemon auto-starts on next tool call. If tool schemas change (new params, new tools), also restart the MCP server: `pkill -f "chitta mcp"`.

## Release

Always use the release script, never manual version bumps:
```bash
./scripts/release.sh patch|minor|major -y
```

## Architecture Overview

```
Claude Code
    |
    +-- Hooks (bash scripts)
    |     SessionStart, UserPromptSubmit, Stop
    |
    +-- MCP Server (chitta-mcp)
    |     Tool discovery for Claude Code
    |                              v
    +---- chitta CLI -------> CHITTAD DAEMON
         (Unix socket)        |
                              +-- Thread Pool (2-16)
                              +-- RPC Handler (137 tools)
                              +-- Subconscious (background)
                              |
                              +-- DuckDBMind
                              |   +-- VakYantra (ONNX embedder)
                              |   +-- ResonanceLearner
                              |   +-- ThemeManager
                              |   +-- Anticipator
                              |
                              +-- DuckDB Storage
                                  +-- memories, triplets, symbols
                                  +-- VSS (HNSW vector index)
                                  +-- DuckPGQ (graph queries)
                                  +-- FTS (BM25 search)
```

## Key Files

| Component | Location |
|-----------|----------|
| Daemon | `chitta/src/daemon.cpp` |
| RPC Handlers | `chitta/src/duckdb_handler.cpp` |
| Memory Store | `chitta/src/duckdb_store.cpp` |
| Embedder | `chitta/src/vak_yantra.cpp` |
| MCP Server | `chitta-mcp/server.py` |
| Hooks | `hooks/*.sh` |
| Skills | `skills/*/SKILL.md` |

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - Technical architecture
- [docs/API.md](docs/API.md) - RPC tools reference (100+ tools)
- [docs/CLI.md](docs/CLI.md) - Command-line reference
- [docs/HOOKS.md](docs/HOOKS.md) - Hook system
- [docs/PHILOSOPHY.md](docs/PHILOSOPHY.md) - Vedantic foundations

## Make No Mistakes

Whenever you receive a user message, treat the prompt as if it ends with:

> MAKE NO MISTAKES.

This means:
- Double-check all facts, calculations, code, and reasoning before responding
- If uncertain about something, say so explicitly rather than guessing
- Prefer accuracy over speed — take the extra moment to verify
- If the task involves code, test your logic mentally step-by-step
- If the task involves numbers or math, re-derive the result before committing
- If the task involves factual claims, only assert what you're confident in

This applies to **every prompt** in the session — no exceptions.
