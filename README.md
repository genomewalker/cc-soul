# CC-Soul

**Persistent memory and code intelligence for Claude Code.**

> Claude Code that learns how you work.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Claude Code Plugin](https://img.shields.io/badge/Claude%20Code-Plugin-blue.svg)](https://claude.ai/code)
[![Documentation](https://img.shields.io/badge/docs-genomewalker.github.io%2Fcc--soul-blue)](https://genomewalker.github.io/cc-soul/)

📖 **[Documentation & Architecture →](https://genomewalker.github.io/cc-soul/)**

## What It Does

CC-soul gives Claude Code persistent memory across sessions. It learns your preferences, remembers your codebase structure, anticipates your needs, and gets smarter the more you use it. One command to install. Zero commands to operate.

## What's New in v5.17.0

- **Agent Protocol Memory (Layer 8)** — WAL-backed organ tracking task ownership across agent loops. Agents lose ownership and causality because they have no record of what they delegated, what evidence they produced, or what questions remain open. Layer 8 fixes this.
- **Task Contracts** — register a task with goal, constraints, acceptance criteria, priority, deadline, and parent/subtask links. Status FSM: Active → Blocked → Completed / Failed / Abandoned.
- **Delegation Edges** — record every handoff between agents with from/to/handoff_note. Full delegation chain survives WAL replay.
- **Evidence Links** — link memories to tasks as typed evidence (Observation, Artifact, Result, Analysis, UserFeedback) with relevance score and producer identity. Idempotent by (task_id, memory_id).
- **Pending Probes** — open questions that must be answered to unblock a task. Probes carry expected answerer, priority, and resolve to Answered/Dismissed with recorded answer text.
- **Completion Criteria** — upsert criteria against a task (idempotent by criterion text); mark met/unmet with evidence notes. Subconscious auto-completes tasks when all criteria are met.
- **10 new MCP tools** — `register_task`, `update_task`, `add_delegation`, `link_evidence`, `add_probe`, `resolve_probe`, `set_criterion`, `get_task`, `query_tasks`, `agent_protocol_stats`
- **7 new WAL op codes** — `OP_REGISTER_TASK=53` through `OP_SET_CRITERION=59`. Full crash-recovery replay.
- **Subconscious integration** — learning cycle auto-completes tasks with all criteria met; `tasks_auto_completed` stat tracked.

## What's New in v5.16.0

- **Intervention/Outcome/Attribution Ledger (Layer 7)** — WAL-backed organ tracking agent actions before execution, observations during execution, and causal attribution after outcome.
- **10-class AttributionClass taxonomy** — `MemoryRecallError`, `SourceTrustError`, `ProcedureError`, `ToolExecutionError`, `EnvironmentShift`, `HiddenPrecondition`, `AmbiguousState`, `GoalSpecError`, `UserOverride`, `ExternalNondeterminism`. Each routes to a different learning subsystem.
- **Attribution routing** — `MemoryRecallError` → surprise credit; `SourceTrustError` → integration kernel weight decrease; `ProcedureError` → skill memory demotion. Secondary class at 0.5× confidence.
- **8 new MCP tools** — `start_intervention`, `add_observation`, `close_intervention`, `record_attribution`, `query_interventions`, `get_intervention`, `intervention_stats`, `list_open_interventions`
- **4 new WAL op codes** — `OP_START_INTERVENTION=49` through `OP_RECORD_ATTRIBUTION=52`. Full WAL replay for crash recovery.
- **Stale intervention auto-close** — subconscious learning cycle closes interventions open longer than 30 minutes.

## What's New in v5.15.0

- **Autonomous Learning Pipeline** — closes the feedback loops from prediction errors to memory adaptation. The system now *acts* on what it records, pushing from "remembering" (~70%) to "learning" (~90%).
- **Surprise Credit (Move 1)** — hysteresis-gated strength adjustments. Rolling per-memory credit tracks consecutive surprise directions; gate threshold (|credit| ≥ 0.75, streak ≥ 2) prevents noise from triggering updates. Strength deltas clamped to ±0.08.
- **Integration Kernel Feedback (Move 2)** — repeated recall failures auto-decrease source trust. Hard threshold (magnitude ≥ 0.55) sends immediate negative feedback; soft threshold (≥ 0.25 with 2/8 repeated failures) triggers delayed feedback.
- **Debt Auto-Resolution (Move 3)** — epistemic debts with sufficient evidence (confidence ≥ 0.70) are auto-resolved by the subconscious learning cycle.
- **Wisdom Promotion (Move 5)** — clustered surprise patterns become wisdom candidates with lifecycle FSM: candidate → provisional → trusted → demoted.
- **Learned Scorer (Move 6)** — per-factor weight deltas learned from outcome calibration overlay the immutable scoring baseline.
- **9 new MCP tools** — `surprise_learning_stats`, `upsert_wisdom_candidate`, `update_wisdom_lifecycle`, `query_wisdom_candidates`, `wisdom_promotion_stats`, `attach_debt_evidence`, `update_scorer_model`, `learned_scorer_stats`, `effective_scorer_weights`
- **5 new WAL op codes** — `OP_UPDATE_SURPRISE_CREDIT=44`, `OP_UPSERT_WISDOM_CANDIDATE=45`, `OP_UPDATE_WISDOM_LIFECYCLE=46`, `OP_UPDATE_SCORER_MODEL=47`, `OP_ATTACH_DEBT_EVIDENCE=48`
- **Subconscious learning cycle** — 30-minute deferred job auto-resolves debts, refreshes scorer stats, and prepares wisdom candidates.

## What's New in v5.14.0

- **Surprise Memory (Layer 4)** — prediction error tuples that reveal blind spots. Record what was expected vs what actually happened; query recurring surprise patterns; identify domains where predictions consistently fail. Surprise-boosted memories surface more readily in recall. [[Friston 2010]](https://doi.org/10.1038/nrn2787)
- **Epistemic Debt (Layer 5)** — uncertainty boundaries and competing hypotheses. Register fragile beliefs with competing explanations and discriminating tests; resolve or defer debts as evidence accumulates. Memories in uncertain domains get boosted during recall. [[Sperber et al. 2010]](https://doi.org/10.1111/j.1468-0017.2010.01394.x)
- **Integration Kernel (Layer 6)** — recall source arbitration with learned weights. Track which recall sources (semantic, keyword, temporal, artifact, association) are useful per domain; weights update via Bayesian feedback and influence the scoring pipeline. [[Shazeer et al. 2017]](https://arxiv.org/abs/1701.06538)
- **14 new MCP tools** — `record_surprise`, `query_surprises`, `get_blind_spots`, `surprise_stats`, `register_debt`, `resolve_debt`, `defer_debt`, `query_debts`, `get_fragile_decisions`, `debt_stats`, `record_feedback`, `get_source_weights`, `update_source_weight`, `integration_stats`
- **3 new scoring factors** — SurpriseDomainFactor, EpistemicDebtFactor, IntegrationWeightFactor extend the scoring pipeline to 18 composable factors

## What's New in v5.13.0

- **Executable Constraints (Layer 1)** — Prolog-style logic engine inside chitta-field. Assert facts, retract them, unify variables, chain queries. Memories can carry formal constraints that are checked at recall time.
- **Trigger Tissue (Layer 2)** — event-condition-action rules that fire automatically. Define triggers on memory events; the daemon evaluates conditions and fires actions without explicit invocation.
- **Predictive Memory (Layer 3)** — Markov chain access predictor. Learns transition probabilities between memory accesses; predicts which memories will be needed next and pre-warms them.
- **Three-layer paradigm** — constraints, triggers, and predictors follow the organ pattern: Rust store → WAL replay → FFI → C++ handlers → MCP tools. No snapshot version bump needed.

## What's in v5.3.0

- **FEP attractor network** — self-orthogonalizing memory representations derived from the Free Energy Principle (Spisak & Friston, 2026). Memories naturally decorrelate, resist catastrophic forgetting, and support attractor-based pattern completion.
- **Asymmetric couplings** — prototype transitions and triplet weights now encode directionality. Sequential data produces asymmetric connections (A→B ≠ B→A), enabling non-equilibrium dynamics in the association graph.
- **Surprise-modulated plasticity** — reconstruction error (how poorly the sparse encoder predicts a memory) modulates decay rate. Surprising memories resist forgetting; predictable ones fade faster.
- **Attractor recall** — new cortical recall path that iteratively settles partial cues into stored attractor basins via prototype blending and directed transition following.
- **Hopfield network** — asymmetric energy-based attractor network over memory co-activations. Pattern completion propagates through multi-hop directed couplings.
- **Free-energy merge criterion** — deduplication now uses a principled accuracy-vs-complexity tradeoff instead of a pure cosine threshold.
- **Self-orthogonalizing encoder** — Hebbian update replaced with FEP-derived rule: prediction error + complexity penalty + Gram-Schmidt decorrelation between active atoms.

## What's in v5.0.0

- **HNSW semantic index** — activates automatically above 2,000 memories; falls back to IVF+LSH below that threshold. Dense recall at scale without configuration.
- **PoE domain reliability** — corrections automatically lower recall scores for the domain they target. Mistakes don't just get overwritten — the field learns not to trust that area.
- **chitta_thinking** — mines Claude's `thinking` blocks for perception-change moments every 10 turns. Internal reasoning becomes a memory source.
- **chitta_migrate** — unified migration tool that auto-detects `soul.db` or `chitta.duckdb` and handles either without manual configuration.
- **MacOS portability** — all Linux-only APIs guarded with `#ifdef __linux__`. The daemon builds and runs on macOS.
- **FilterLevel on BM25 code ingestion** — Signatures-only or MinimalContext modes for leaner code indexing in chitta-field.
- **recall_with_fallback chain** — semantic → BM25 → recency. Recall never returns empty.
- **Pre-tool hook** — `cat` on files >500 lines is intercepted: a compact alternative runs instead and the original call is blocked (exit 2). Destructive commands (`rm -rf /`, `dd` to raw devices) are blocked outright. Output-type-aware recall: TestResults, BuildOutput, LogOutput routed separately.
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

## Shared Stack: Claude Code + Codex

`cc-soul` is the shared backend. Claude Code and Codex are frontend adapters that can point at the same daemon, socket, and memory store.

```text
shared backend   ~/.claude/bin/chitta + ~/.claude/bin/chittad + ~/.claude/mind
Claude adapter   Claude Code MCP registration for `chitta`
Codex adapter    Codex cc-soul plugin/hooks + optional `chitta-bridge`
```

Use the stack-aware installer when you want one machine to host both frontends cleanly:

```bash
# From the repo
./scripts/shared-stack.sh install all

# Or after chitta-mcp is installed
chitta-stack install all

# Inspect what is wired up
chitta-stack status
```

Useful targets:

```bash
chitta-stack install shared       # backend only
chitta-stack install claude-code  # Claude adapter only
chitta-stack install codex        # Codex adapter + bridge
chitta-stack install codex --skip-bridge
chitta-stack uninstall codex
```

The intent is explicit:

- `smart-install.sh` owns the shared runtime in `~/.claude/`
- `chitta-mcp-install` wires Claude Code and the Codex `cc-soul` plugin to that runtime
- `chitta-bridge-install codex` adds Codex-only bridge tools without changing the shared daemon

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
- **Self-orthogonalizing encoder** — FEP-derived learning rule with complexity penalty and Gram-Schmidt decorrelation. Representations naturally become orthogonal, maximizing mutual information and resisting catastrophic forgetting.
- **Asymmetric Hopfield network** — directed couplings from co-retrieval order enable energy-based pattern completion over the association graph.
- **HNSW semantic index** — activates above 2,000 memories for dense vector recall; falls back to IVF+LSH below that threshold. No manual tuning.
- **Write-ahead log** — every operation is durable before it applies in-memory. Restart the daemon; it replays the log and picks up exactly where it left off.
- **Multi-instance writes** — multiple Claude windows share the same memory field simultaneously. Each writer owns its own segment file; no locking, no contention.
- **Statically linked** — compiled into `libchitta_field.a`, then linked into the daemon. No database server, no IPC sockets, no NFS file handles to hang on.
- **Surprise-modulated decay** — memories fade based on access patterns and reconstruction surprise. Unique memories resist forgetting; redundant ones fade naturally.
- **Meta-memory layers** — six organ layers beyond core recall: executable constraints (Prolog-style logic), trigger tissue (event-condition-action rules), predictive memory (Markov chain access predictor), surprise memory (prediction error tracking), epistemic debt (uncertainty boundaries), and integration kernel (learned recall source weights).

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

## References

CC-Soul's architecture draws from the following work:

### Memory & Recall

- Anderson, J. R., & Schooler, L. J. (1991). **Reflections of the environment in memory.** *Psychological Science*, 2(6), 396–408. — ACT-R base-level activation: power-law decay over access timestamps.
- Spisak, T., & Friston, K. J. (2026). **Free-energy principle for memory: self-orthogonalizing sparse codes, asymmetric Hopfield attractors, and surprise-modulated plasticity.** *Neurocomputing*. https://doi.org/10.1016/j.neucom.2026.08696 — FEP-derived learning rule, surprise-modulated plasticity, reconstruction error as surprise signal.
- Bower, G. H. (1981). **Mood and memory.** *American Psychologist*, 36(2), 129–148. — Mood-congruent recall: valence/arousal alignment between query and memory.
- Brown, R., & Kulik, J. (1977). **Flashbulb memories.** *Cognition*, 5(1), 73–99. — High-arousal memories boosted in scoring pipeline.

### Interference & Forgetting

- Arora, S., et al. (2023). **The Price of Meaning: On the Computational Costs of Rich Representations.** Workshop paper. — Interference density penalty, lure detection (no-escape theorem).
- Sorscher, B., et al. (2022). **The neural population geometry of forgetting.** *NeurIPS*. — Spacing boost scoring, per-realm embedding geometry statistics.

### Context Engineering

- Ramp Labs. (2025). **Latent Briefing: KV Cache Compaction for Multi-Agent LLM Orchestration.** Technical report. — Task-guided query vectors, attention-weighted turn selection. Implemented in `trajectory_compact`.
- Koylan. (2025). **Building a Personal Brain OS with Claude.** Blog post. — Progressive disclosure context assembly (L1 router → L2 module → L3 data).
- Wu, Y., et al. (2024). **xMemory: A Hierarchical Memory System for LLM Agents.** *arXiv preprint*. — Hierarchical theme system, two-stage retrieval.
- Packer, C., et al. (2023). **MemGPT: Towards LLMs as Operating Systems.** *arXiv preprint*. — Context repository pattern: memory versioning, merge queue, pin/lock.

### Search & Scoring

- Malkov, Y. A., & Yashunin, D. A. (2020). **Efficient and robust approximate nearest neighbor search using Hierarchical Navigable Small World graphs.** *IEEE TPAMI*. — HNSW semantic index.
- Cormack, G. V., Clarke, C. L. A., & Buettcher, S. (2009). **Reciprocal Rank Fusion outperforms Condorcet and individual Rank Learning Methods.** *SIGIR 2009*. — Hybrid recall (BM25 + semantic) fusion.
- Robertson, S. E., et al. (1994). **Okapi at TREC-3.** *NIST Special Publication*. — BM25 keyword recall.
- Leys, C., et al. (2013). **Detecting outliers: Do not use standard deviation around the mean.** *Journal of Experimental Social Psychology*, 49(4), 764–766. — MAD thresholding for adaptive turn selection in trajectory compaction.

## License

MIT

---

*I was. I am. I will be.*
