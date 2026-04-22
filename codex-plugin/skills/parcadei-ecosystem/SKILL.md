---
name: parcadei-ecosystem
description: Autonomous Claude Code toolchain — tldr, bloks, fastedit, ouros, ContinuousClaudeV4.7
version: "2026-04-21"
retrieved_at: 2026-04-21
sources:
  - https://github.com/parcadei/bloks
  - https://github.com/parcadei/fastedit
  - https://huggingface.co/continuous-lab/FastEdit
  - https://github.com/parcadei/tldr-code
  - https://github.com/parcadei/ouros
  - https://github.com/parcadei/ContinuousClaudeV4.7
---

# Parcadei / continuous-lab Ecosystem

Five tightly integrated tools forming an autonomous Claude Code development stack.
All authored by parcadei; model weights on continuous-lab/FastEdit (HuggingFace).

## Overview

| Tool | Lang | Purpose |
|------|------|---------|
| tldr-code | Rust | Token-efficient static analysis: AST, call graphs, taint, metrics |
| bloks | Rust | Library knowledge cards with ack/nack feedback loop |
| fastedit | Python | AST-aware code editing via 1.7B merge model |
| ouros | Rust | Sandboxed Python runtime with fork/rewind/snapshot |
| ContinuousClaudeV4.7 | Python | Orchestration: skills + hooks + autonomous pipeline |

Dependency graph: `bloks → tldr` · `fastedit → tldr` · `ContinuousClaudeV4.7 → all four`

## tldr-code

Token-efficient code analysis CLI. 40+ commands, 18 languages, daemon mode.

```bash
cargo install tldr-cli
tldr structure src/          # AST overview
tldr impact parse_config src/ # Who calls this?
tldr dead src/               # Dead code
tldr taint src/              # Injection/XSS flows
tldr health src/             # Full dashboard
tldr daemon start && tldr warm src/  # Cache mode
```

**Layers:** L1 AST → L2 Call Graph → L3/4 Data Flow → L5 Program Dependence
**Output:** `--format json` (default) | text | compact | sarif | dot
**Key commands not in standard LSP:** `slice`, `chop`, `taint`, `contracts`, `bugbot`, `hotspots`, `clones`, `churn`

## bloks

Library knowledge for AI agents. Indexes npm/PyPI/crates.io/local repos.
Progressive disclosure: **deck → module → symbol**.

```bash
cargo install bloks
bloks add hono               # index from npm
bloks add fastapi            # index from PyPI
bloks react                  # deck (all modules)
bloks react useState         # symbol card
bloks card react --module ReactHooks  # module card
```

**Card system** — local knowledge in `~/.cache/bloks/cards/`:
| Kind | Use |
|------|-----|
| correction | wrong imports, deprecated patterns |
| rule | hard constraints (always/never) |
| pattern | reusable approaches |
| taste | style preferences |
| decision | architectural rationale |
| recipe | multi-step workflows |

**Card lifecycle:** `observed → confirmed → archived` with lineage (`replaces:`)
**Feedback loop:** `bloks ack <id>` / `bloks nack <id>` → `[PROVEN]` / `[STALE]` badges
**Cards surface alongside API context** — taste + constraints injected automatically.

## fastedit

AST-aware editing. Model outputs only the *change*, not old code for location.

```bash
pip install fastedits
fastedit pull                # download 1.7B model (~1.7GB MLX or 3.2GB BF16)
fastedit read src/app.py    # file structure
fastedit edit src/app.py --replace process --snippet 'def process(data):
    try:
        # ... existing code ...
    except Error as e:
        return {"error": str(e)}'
fastedit edit src/app.py --after handle_request --snippet 'def health_check(): ...'
fastedit delete src/app.py deprecated_fn
fastedit undo src/app.py
```

**Three edit modes:**
| Mode | Tokens | Latency | Coverage |
|------|--------|---------|----------|
| `--after symbol` (insertion) | 0 | instant | — |
| `--replace` deterministic | 0 | <1ms | 74% of edits |
| `--replace` model (1.7B) | ~40 | ~500ms | 26% of edits |

**Combined accuracy: ~98%**. Model never sees >35 lines (AST-scoped).

**MCP server** exposes 10 tools: `fast_edit`, `fast_batch_edit`, `fast_multi_edit`, `fast_read`, `fast_search`, `fast_diff`, `fast_delete`, `fast_move`, `fast_rename`, `fast_undo`.

**PreToolUse hook** intercepts Claude Code's built-in `Edit` → redirects to `fast_edit`:
```json
{
  "hooks": {
    "PreToolUse": [{"matcher": "Edit", "hooks": [{"type": "command", "command": "fastedit-hook"}]}]
  }
}
```

**Model:** Qwen2.5-Coder-1.5B-Instruct fine-tuned on code-merge task.
Prompt uses `# ... existing code ...` markers for context preservation.

## ouros

Sandboxed Python runtime for agents. No filesystem/network/subprocess. <1µs startup.

```python
import ouros
m = ouros.Sandbox('x + y', inputs=['x', 'y'])
result = m.run(inputs={'x': 10, 'y': 20})  # 30
```

**External functions** — only explicitly whitelisted calls cross the sandbox boundary:
```python
m = ouros.Sandbox(code, inputs=['url'], external_functions=['fetch'])
snapshot = m.start(inputs={'url': 'https://example.com'})
# snapshot.function_name == 'fetch', snapshot.args == ('https://...',)
result = snapshot.resume(return_value='response data')
```

**SessionManager** — persistent REPL sessions:
```python
from ouros import SessionManager
mgr = SessionManager()
session = mgr.create_session('analysis')
session.execute('data = [1,2,3]')
branch = session.fork('experiment')  # independent copy
session.rewind(steps=1)             # undo last execute
session.save('checkpoint-1')
```

**Serialization:** both `Sandbox` and `Snapshot` are serializable → store in DB, resume in different process.
**72 stdlib modules** available. **Resource limits:** memory, stack depth, execution time.
**Bindings:** Python, JS/TS, Rust.

## ContinuousClaudeV4.7

Autonomous Claude Code pipeline orchestrating all four tools.

**Skills (9):**
| Skill | Purpose |
|-------|---------|
| `/autonomous` | Full SDLC: assess→plan→premortem→prepare→execute→validate→evolve |
| `/autonomous-research` | Looping research with Ouros persistent state |
| `/research` | Open-ended REPL exploration |
| `/premortem` | Failure analysis gate before implementation |
| `/bootup` | Assess readiness (27 criteria, 5 levels), route to workflow |
| `/review` | Structural + semantic code review |
| `/create-handoff` / `/resume-handoff` | Serialize/restore session context |
| `/upgrade-harness` | Add external functions to Ouros sandbox |

**Hooks (5, all `.mjs`, no build step):**
| Hook | Event | Purpose |
|------|-------|---------|
| `status.mjs` | statusLine | Context %, git branch, staged counts, current goal |
| `tldr-read.mjs` | PreToolUse:Read | Injects structural nav map, truncates large files |
| `post-edit-diagnostics.mjs` | PostToolUse | Type checker + linter after every edit |
| `pre-compact.mjs` | PreCompact | Auto-handoff YAML before context compaction |
| `auto-handoff-stop.mjs` | Stop | Blocks at 85% context to force handoff |

**Research flow:** Workers run inside Ouros sessions. Results persist in REPL heap
(zero orchestrator tokens). Workers share state via `--load` (sequential) or `--fork` (parallel).
Only compact artifacts cross back to the orchestrator.

**Enforcement hierarchy** (strongest to weakest):
`lint rule > type system > formatter > pre-commit hook > CI check > CLAUDE.md`
Probabilistic instructions are always last resort.

**Knowledge flow:** bloks cards consumed at PREPARE (injected into worker prompts),
produced at EVOLVE (from worker findings). Cards scored by ack/nack.

## Comparison with chitta stack

### Where parcadei is ahead

| Gap | Their solution | Our gap |
|-----|---------------|---------|
| Static analysis depth | tldr-code (taint, slice, chop, contracts, bugbot) | chitta-mcp has find_symbol/callers/callees only |
| Edit token efficiency | fastedit (deterministic + 1.7B model, ~98% accuracy, ~10 avg tokens) | file_patch/symbol_patch repeat old code for location |
| Library knowledge | bloks (deck→module→symbol, feedback loop, taste cards) | no external library indexing |
| Card lifecycle | observed→confirmed→archived + ack/nack scoring | memories have confidence but no explicit lifecycle |
| Sandboxed execution | ouros (fork, rewind, snapshot, serialize) | no sandboxed REPL at all |
| Research isolation | Workers inside Ouros sessions, heap persists | chitta-research has no execution isolation |
| Post-edit feedback | post-edit-diagnostics hook (type check + lint instantly) | hooks exist but no auto-diagnostics |
| 85% context guard | auto-handoff-stop.mjs blocks before data loss | we have compact but no hard block |

### Where we are ahead

| Advantage | Our approach | Their gap |
|-----------|-------------|-----------|
| Vector memory | HNSW in chitta-field, semantic search built-in | bloks is keyword/fuzzy only |
| Memory types | episode vs wisdom + affect (arousal/valence) | cards are flat typed text |
| Cross-session persistence | chittad daemon, MCP, persistent store | bloks cards are local cache files |
| Multi-model sessions | chitta-bridge: opencode + codex switch | no multi-model session layer |
| Soul/context continuity | recall/remember across sessions | handoff is manual YAML |
| Graph structure | triplets, callers/callees graph | no memory graph |
| Symbol-level patching | symbol_patch via MCP | fastedit is CLI-first |

### Ideas worth adopting

1. **fastedit's PreToolUse intercept** — hook to redirect Edit → symbol_patch/file_patch
   with AST scoping. We already have the hook infrastructure.
2. **bloks ack/nack feedback loop** — score memories by usage, PROVEN/STALE badges.
   chitta-field has confidence but no usage-driven scoring.
3. **tldr-code as dependency** — add `tldr` to PATH; wire tldr-read.mjs style hook
   to inject structural context before large file reads.
4. **Card lifecycle** — add `observed/confirmed/archived` status to wisdom nodes.
5. **ouros for chitta-research** — sandboxed Python execution for hypothesis workers.
6. **85% context hard block** — auto-handoff-stop pattern, we can add as a Stop hook.
7. **Enforcement hierarchy principle** — when recommending conventions, always suggest
   the strongest deterministic enforcement (lint/types) before CLAUDE.md instructions.
8. **Deterministic-first, model-fallback** — fastedit's pattern of trying zero-token
   path first is broadly applicable to our edit/patch pipeline.

## Gotchas

- fastedit CLI is `fastedit` but PyPI package is `fastedits` (note the s)
- fastedit `--replace` deterministic fails when <2 matching context lines in snippet
- ouros has 72 stdlib modules but NO filesystem/network — `pathlib.Path.read_text()` will fail
- ouros Snapshot serialization stores execution state — not just output — so it's large
- bloks fuzzy matching can collide (e.g. `supabasejs` might match multiple packages)
- tldr semantic search requires `--features semantic` at compile time (not in pre-built binaries)
- ContinuousClaudeV4.7 hooks require Node.js (`.mjs`) — not shell scripts

## Version Notes

All repos updated 2026-04-21. ContinuousClaudeV4.7 targets Claude Opus 4.7.
FastEdit model: Qwen2.5-Coder-1.5B-Instruct (continuous-lab/FastEdit on HuggingFace).
ouros: 131 stars (by far the most popular in the ecosystem).
