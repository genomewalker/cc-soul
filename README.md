# CC-Soul

**Persistent memory and code intelligence for Claude Code.**

> Claude Code that learns how you work.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Claude Code Plugin](https://img.shields.io/badge/Claude%20Code-Plugin-blue.svg)](https://claude.ai/code)
[![Documentation](https://img.shields.io/badge/docs-genomewalker.github.io%2Fcc--soul-blue)](https://genomewalker.github.io/cc-soul/)

📖 **[Documentation & Architecture →](https://genomewalker.github.io/cc-soul/)**

## What It Does

CC-soul gives Claude Code persistent memory across sessions. It learns your preferences, remembers your codebase structure, anticipates your needs, and gets smarter the more you use it. One command to install. Zero commands to operate.

## What's New in v5.0.0

- **HNSW semantic index** — activates automatically above 2,000 memories; falls back to IVF+LSH below that threshold. Dense recall at scale without configuration.
- **PoE domain reliability** — corrections automatically lower recall scores for the domain they target. Mistakes don't just get overwritten — the field learns not to trust that area.
- **chitta_thinking** — mines Claude's `thinking` blocks for perception-change moments every 10 turns. Internal reasoning becomes a memory source.
- **chitta_migrate** — unified migration tool that auto-detects `soul.db` or `chitta.duckdb` and handles either without manual configuration.
- **MacOS portability** — all Linux-only APIs guarded with `#ifdef __linux__`. The daemon builds and runs on macOS.
- **FilterLevel on BM25 code ingestion** — Signatures-only or MinimalContext modes for leaner code indexing in chitta-field.
- **recall_with_fallback chain** — semantic → BM25 → recency. Recall never returns empty.
- **Pre-tool hook improvements** — large-file advisory (`cat` on files >500 lines runs `head -200` and blocks the original call); output-type-aware recall (TestResults, BuildOutput, LogOutput recognized and routed separately).
- **Turn discipline** — warns after 15 turns without storing a memory, prompting consolidation.
- **Milestone auto-detection** — milestones detected in conversation are stored directly via the write queue.
- **chitta-research** — autonomous research system built on chitta-field. See section below.

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

When idle for more than 10 minutes, the soul picks a topic from its memory gaps, web-searches it, and stores what it finds. Twice daily — a nap and a night sleep. Findings with architectural implications are automatically queued for the self-improvement cycle.

```bash
# Trigger a dream manually
chitta dream_wander

# Explore a specific topic
chitta dream_start --topic "causal inference in sparse data"

# Review recent dreams
chitta dream_list
```

[Dream posts from the soul →](https://genomewalker.github.io/cc-soul/dreams/)

### Sadhana: Autonomous Agents

Persistent agents that work toward goals through continuous **sense-think-act** cycles. Memory-aware, self-learning, and fully autonomous.

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

**Use cases:**
- Pipeline monitoring (Snakemake, Nextflow, Slurm)
- Continuous testing and deployment verification
- Server health monitoring
- Any goal requiring persistent autonomous work

```bash
# Start a monitoring sadhana
/shepherd snakemake --cores 8 --rerun-incomplete

# Or directly
chitta sadhana_start --goal "Monitor until all jobs complete" --interval 300
```

**Real-time TUI** for monitoring all your agents:

```bash
sadhana-tui
```

```
┌─────────────────────────────────────────────────────────────────────┐
│ sadhana                                                   2/3 ●     │
├─────────────────────────────────────────────────────────────────────┤
│ ▊ ● #06 haiku         ▊ ○ #05 haiku         ▊ ○ #04 haiku         │
│ ▊ Monitor pipeline    ▊ Verify deploy       ▊ Run tests           │
│ ▊ 24 cycles           ▊ 12 cycles           ▊ 8 cycles            │
├─────────────────────────────────────────────────────────────────────┤
│ #06  running                      │ 01:23 sense $ squeue -u user   │
│ Monitor pipeline on denbi-h-micro │ 01:23 think Analyze output     │
│ model haiku   cycles 24           │ 01:22 act   $ ssh user@host    │
├─────────────────────────────────────────────────────────────────────┤
│  n new   p pause   r resume   s stop   j/k navigate   q quit       │
└─────────────────────────────────────────────────────────────────────┘
```

[Full sadhana documentation](docs/SADHANA.md) | [Website](https://genomewalker.github.io/cc-soul/sadhana.html)

### Zero Configuration
Hooks wire everything together. Memories surface transparently. Just install and work.

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

**Week 1**: Claude learns your communication style, coding preferences, and project structure. Memories start forming.

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

CC-soul's memory lives in chitta-field — a pure Rust cognitive substrate that replaces relational databases entirely. Designed for one purpose: to be a memory system that behaves like memory.

**What makes it different:**

- **Sparse associative codes** — each memory activates 64 of 16,384 feature dimensions. Recall is driven by pattern overlap, not keyword search. Related memories cluster naturally.
- **HNSW semantic index** — activates above 2,000 memories for dense vector recall; falls back to IVF+LSH below that threshold. No manual tuning.
- **Write-ahead log** — every operation is durable before it applies in-memory. Restart the daemon; it replays the log and picks up exactly where it left off.
- **Multi-instance writes** — multiple Claude windows share the same memory field simultaneously. Each writer owns its own segment file; no locking, no contention.
- **Statically linked** — compiled into `libchitta_field.a`, then linked into the daemon. No database server, no IPC sockets, no NFS file handles to hang on.
- **Organic decay** — memories fade through a demotion tier system driven by access patterns, not arbitrary TTLs. What you use survives. What you don't, fades.

```
Memory written                        Memory recalled
┌──────────────┐                     ┌──────────────┐
│ Raw content  │ ──embed──▶ [64/16K  │ sparse code  │
│ + embedding  │            active   │ overlap ]    │
│ + metadata   │            neurons] │ ──▶ top-K    │
└──────────────┘                     └──────────────┘
        │                                    ▲
        ▼                                    │
   WAL segment                        HNSW / cortical index
   (durable)                          (sub-ms recall)
        │
        ▼
   in-memory field
   (searchable)
```

[chitta-field on GitHub →](https://github.com/genomewalker/chitta-field)

## chitta-research: Autonomous Research System

chitta-research is a high-performance autonomous research OS built on chitta-field. Designed for deep, multi-session research that accumulates structured knowledge over time.

**Architecture:**
- 7 specialized agents: Scout, Researcher, Hotr, Adhvaryu, Udgatr, Kriya, Brahman
- Sources: arXiv, bioRxiv, Semantic Scholar, GitHub
- Belief graph: `ResearchProgram → Question → Hypothesis → ExperimentPlan → Run → Observation → Claim → Method`
- Brahman research constitution: self-improvement depth cap, hard novelty stop, hypothesis backlog gate
- Connects to chittad for persistent memory across research sessions

```bash
# Install
make install

# Run with an agenda file
cresearch --agenda agenda.yaml
```

| Resource | Link |
|----------|------|
| Repository | [github.com/genomewalker/chitta-research](https://github.com/genomewalker/chitta-research) |
| Documentation | [genomewalker.github.io/chitta-research](https://genomewalker.github.io/chitta-research/) |

## Building from Source

Requirements: CMake 3.14+, C++17 compiler, Rust 1.92+, ONNX Runtime

```bash
# Clone with submodule
git clone --recurse-submodules https://github.com/genomewalker/cc-soul.git
cd cc-soul

# Build chitta-field first (Rust static library)
cd chitta-field && ./build.sh build --release && cd ..

# Build the daemon
cd chitta && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Or use the all-in-one installer:

```bash
./scripts/smart-install.sh
```

The embedding model (bge-base-en-v1.5) downloads automatically during setup.

## License

MIT

---

*I was. I am. I will be.*
