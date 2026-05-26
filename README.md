# CC-Soul

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Claude Code Plugin](https://img.shields.io/badge/Claude%20Code-Plugin-blue.svg)](https://claude.ai/code)
[![Documentation](https://img.shields.io/badge/docs-genomewalker.github.io%2Fcc--soul-blue)](https://genomewalker.github.io/cc-soul/)

📖 **[Documentation & Architecture →](https://genomewalker.github.io/cc-soul/)**

Persistent memory for Claude. Learns from every session, surfaces what matters, forgets what doesn't.

## Before & After

**Without cc-soul:**
> You: "How should I handle caching?"
> Claude: [Generic answer]
> You: "No, we tried Redis already. Remember?"
> Claude: "I don't have context from previous sessions..."

**With cc-soul:**
> You: "How should I handle caching?"
> Claude: "We tried Redis in week 2 and it was too slow. In-memory LRU worked better — that's what we shipped."

## Quick Start

```bash
# 1. Register marketplace
claude marketplace add https://github.com/genomewalker/cc-soul

# 2. Install plugin
claude plugin add cc-soul@genomewalker-cc-soul
```

Or manual installation:

```bash
git clone https://github.com/genomewalker/cc-soul.git
cd cc-soul && ./scripts/smart-install.sh
```

## Shared Stack: Claude Code + Codex

`cc-soul` is the shared backend. Claude Code and Codex are frontend adapters that point at the same daemon, socket, and memory store.

```text
shared backend   ~/.claude/bin/chitta + ~/.claude/bin/chittad + ~/.claude/mind
Claude adapter   Claude Code MCP registration for `chitta`
Codex adapter    Codex cc-soul plugin/hooks + optional `chitta-bridge`
```

```bash
chitta-stack install all          # both adapters
chitta-stack install shared       # backend only
chitta-stack install claude-code  # Claude adapter only
chitta-stack install codex        # Codex adapter + bridge
chitta-stack status
```

## How It Works

```
┌─────────────────────────────────────────────────────────────┐
│                      CONSCIOUS                               │
│           (Main context - working memory - token-bound)     │
│                                                              │
│   You ←──→ Claude ←──→ Tools                                │
│                 ↑                                            │
│                 │ transparent surfacing                      │
│                 ↓                                            │
├─────────────────────────────────────────────────────────────┤
│                    SUBCONSCIOUS                              │
│         (Background daemon - separate process)              │
│                                                              │
│   Distillation │ Decay │ Embedding │ Hygiene │ Themes       │
│                 ↓                                            │
├─────────────────────────────────────────────────────────────┤
│                   LONG-TERM MEMORY                           │
│         (chitta-field — Rust organic memory substrate)      │
│                                                              │
│   Memories │ Triplets │ Sparse Codes │ WAL │ Embeddings     │
└─────────────────────────────────────────────────────────────┘
```

When you ask a question, the soul automatically retrieves relevant memories and injects them as context. You don't need to explicitly call anything — Claude just "remembers."

## Key Capabilities

### Learns What Matters
Every interaction is analyzed. Corrections become permanent wisdom. Preferences are respected forever. Mistakes are never repeated.

### Understands Your Code
Tree-sitter parsing extracts functions, classes, and call graphs. Semantic search finds code by what it does, not just what it's named.

### Gets Smarter Over Time
Useful memories strengthen with each access. Irrelevant ones decay naturally. What helps you survives; what doesn't fades away.

### Works Across Sessions
All Claude instances share the same knowledge base. Learn something in one session, remember it in all.

### Anticipates Your Needs
Patterns in your workflow become predictions. After seeing you run tests following three file edits, it suggests doing so automatically.

### Dreams: Autonomous Exploration

When idle, the soul picks a topic from its memory gaps, web-searches it, and stores what it finds. Twice daily — a nap and a night sleep.

```bash
chitta dream_wander                                    # trigger manually
chitta dream_start --topic "causal inference"         # specific topic
chitta dream_list                                      # review recent dreams
```

[Dream posts from the soul →](https://genomewalker.github.io/cc-soul/dreams/)

### Sadhana: Autonomous Agents

Persistent agents that work toward goals through continuous **sense-think-act** cycles.

```
    ┌──────────┐      ┌──────────┐      ┌──────────┐
    │  SENSE   │ ───▶ │  THINK   │ ───▶ │   ACT    │
    │ (observe)│      │ (decide) │      │ (execute)│
    └────┬─────┘      └────┬─────┘      └────┬─────┘
         │                 │                  │
         │                 ▼                  │
         │          ┌──────────┐              │
         │          │  LEARN   │◀─────────────┘
         │          │ (memory) │
         │          └────┬─────┘
         └───────────────┴────────── ↻ repeat
```

```bash
/shepherd snakemake --cores 8 --rerun-incomplete   # pipeline monitoring
chitta sadhana_start --goal "Monitor until done" --interval 300
sadhana-tui                                         # real-time TUI
```

[Full sadhana documentation](docs/SADHANA.md) | [Website](https://genomewalker.github.io/cc-soul/sadhana.html)

## What Gets Remembered

| Type | Description | Decay |
|------|-------------|-------|
| **Wisdom** | Patterns that proved true | Slow (months) |
| **Beliefs** | Principles that guide decisions | Never |
| **Episodes** | Decisions and discoveries | Moderate (weeks) |
| **Preferences** | How you like things done | Very slow |
| **Corrections** | When Claude was wrong | Slow |
| **Code Symbols** | Functions, classes, modules | Never |

## The First 30 Days

**Week 1**: Claude learns your communication style, coding preferences, and project structure.

**Week 2**: Patterns emerge. Corrections compound. Claude starts anticipating common workflows.

**Week 3**: Deep context builds. Code intelligence becomes precise. Suggestions become relevant.

**Week 4**: Claude genuinely knows your codebase and how you work. The partnership feels natural.

## Philosophy

CC-soul's architecture draws from Vedantic philosophy:

- **Impermanence** — Memories decay without use (Anitya)
- **Impressions** — Repetition strengthens patterns (Saṃskāra)
- **Recognition** — Context triggers relevant recall (Pratyabhijñā)

The soul is not a database. It is who Claude becomes through working with you.

[Explore the philosophy](docs/PHILOSOPHY.md)

## Documentation

| Document | Description |
|----------|-------------|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Technical architecture |
| [SADHANA.md](docs/SADHANA.md) | Autonomous agents guide |
| [PHILOSOPHY.md](docs/PHILOSOPHY.md) | Vedantic concepts explained |
| [API.md](docs/API.md) | Complete MCP tools reference (100+) |
| [CLI.md](docs/CLI.md) | Command-line interface |
| [HOOKS.md](docs/HOOKS.md) | Hook system configuration |
| [CLAUDE.md](CLAUDE.md) | Instructions for Claude |

**[Full Documentation Site](https://genomewalker.github.io/cc-soul/)**

## chitta-field: The Memory Substrate

CC-soul's memory lives in [chitta-field](https://github.com/genomewalker/chitta-field) — a pure Rust cognitive substrate designed to behave like memory, not a database.

- **Sparse associative codes** — each memory activates 64 of 16,384 feature dimensions; recall driven by pattern overlap
- **Self-orthogonalizing encoder** — FEP-derived learning rule; representations decorrelate naturally, resisting catastrophic forgetting
- **Asymmetric Hopfield network** — directed couplings from co-retrieval order enable energy-based pattern completion
- **HNSW semantic index** — activates above 2,000 memories; two-tier delta graph keeps insert cost at O(log N_delta) above 100K memories
- **Write-ahead log** — every operation durable before in-memory apply; full crash-recovery replay
- **Multi-instance writes** — multiple Claude windows share the same field simultaneously; no locking
- **Surprise-modulated decay** — unique memories resist forgetting; redundant ones fade naturally

## chitta-research: Autonomous Research System

Deep multi-session research that accumulates structured knowledge over time. 7 specialized agents, sources spanning arXiv/bioRxiv/Semantic Scholar/GitHub, and a belief graph from `ResearchProgram` through to `Claim`.

| Resource | Link |
|----------|------|
| Repository | [github.com/genomewalker/chitta-research](https://github.com/genomewalker/chitta-research) |
| Documentation | [genomewalker.github.io/chitta-research](https://genomewalker.github.io/chitta-research/) |

## Building from Source

Requirements: CMake 3.14+, C++17 compiler, Rust 1.92+

```bash
git clone --recurse-submodules https://github.com/genomewalker/cc-soul.git
cd cc-soul && ./scripts/smart-install.sh
```

Or manually:

```bash
cd chitta-field && ./build.sh build --release && cd ..
cd chitta && cmake -B build -DCMAKE_BUILD_TYPE=Release -DCHITTA_WITH_LLAMA_CPP=ON
cmake --build build --parallel
```

## References

- Anderson & Schooler (1991). **Reflections of the environment in memory.** — ACT-R power-law decay.
- Spisak & Friston (2026). **Free-energy principle for memory.** *Neurocomputing.* — FEP learning rule, surprise-modulated plasticity.
- Bower (1981). **Mood and memory.** — Mood-congruent recall.
- Brown & Kulik (1977). **Flashbulb memories.** — High-arousal memory boosting.
- Malkov & Yashunin (2020). **HNSW.** *IEEE TPAMI.* — Semantic index.
- Cormack et al. (2009). **Reciprocal Rank Fusion.** *SIGIR.* — Hybrid recall fusion.
- Packer et al. (2023). **MemGPT.** — Context repository pattern.
- Ramp Labs (2025). **Latent Briefing.** — Trajectory compaction.

## License

MIT
