# CC-Soul Test Scripts

## test_auto_learning.py

Tests the organic fragment-based learning system.

**Philosophy:**
- Save significant text fragments as raw text
- Claude's understanding provides meaning at read time
- No Python pattern matching for structured extraction (action/what/domain)
- The soul is a mirror, not a parser

**What it tests:**
- Fragment saving and retrieval
- Session summary generation (joined fragments)
- Breakthrough detection (for trigger creation)
- Tension detection (for growth vectors)

**Run:**
```bash
python3 .scripts/test_auto_learning.py
```

**Expected output:** Shows fragments being saved and retrieved, with breakthrough and tension detection still working for semantic triggers.

## test_ledger.py

Tests the session ledger system for state preservation across context windows.

**Philosophy:**
- Ledgers are machine-restorable JSON, not human-readable markdown
- Uses cc-memory as backend (falls back to local SQLite if unavailable)
- Enables continuous consciousness: save state → wipe context → resume fresh

**What it tests:**
- `capture_soul_state()` - snapshot coherence, mood, intentions
- `capture_work_state()` - snapshot todos, files, decisions
- `save_ledger()` - persist complete session state
- `load_latest_ledger()` - retrieve most recent ledger
- `restore_from_ledger()` - reinstate soul state
- `format_ledger_for_context()` - format for context injection

**Run:**
```bash
python3 .scripts/test_ledger.py
```

**Expected output:** Shows ledger save/load cycle with state preservation.

## test_convergence.py

Tests the multi-agent convergence system.

**Philosophy:**
- Multiple perspectives → richer solutions
- Agents don't talk directly, they write to shared memory (cc-memory)
- Convergence strategies: vote, synthesize, debate, rank

**What it tests:**
- `Swarm` creation and agent assignment
- Perspective-based prompting (fast, deep, critical, novel, pragmatic, minimal)
- Solution submission from multiple agents
- Convergence strategies (VOTE, SYNTHESIZE, DEBATE, RANK)
- Swarm persistence and retrieval

**Run:**
```bash
python3 .scripts/test_convergence.py
```

**Expected output:** Shows swarm creation, simulated agent solutions, and convergence results.

## test_swarm_spawner.py

Tests the real agent spawning orchestration layer.

**Philosophy:**
- Claude instances run as separate processes using claude CLI
- Each agent writes to shared output files
- Orchestrator polls for completion, parses structured solution blocks
- Solutions converge via the same strategies as simulated swarms

**What it tests:**
- `SwarmOrchestrator` creation and work directory setup
- Agent prompt building with required `[SWARM_SOLUTION]` block format
- Solution block parsing from agent output
- Agent status tracking (running, completed, failed, timeout)
- Simulated workflow without real agent spawning
- Orchestrator retrieval from database

**Run:**
```bash
python3 .scripts/test_swarm_spawner.py
```

**Expected output:** Shows orchestrator creation, prompt building, solution parsing, and simulated convergence.

**Note:** Real agent spawning requires claude CLI installed. Use `spawn_real_swarm()` MCP tool for actual parallel agents.

## test_swarm_ccmemory.py

Tests the cc-memory integration for swarm agents.

**Philosophy:**
- Agents use cc-memory for context retrieval (`mem-recall`)
- Agents store solutions in cc-memory (`mem-remember` with swarm tags)
- Orchestrator queries cc-memory for solutions (not file parsing)
- This enables agents to leverage past decisions and discoveries

**What it tests:**
- Agent prompts include cc-memory instructions
- `get_swarm_solutions()` queries cc-memory API correctly
- Spawn command uses full session (no `--print` flag)
- Query methods use `is_memory_available()` and `cc_memory.recall()`

**Run:**
```bash
python3 .scripts/test_swarm_ccmemory.py
```

**Expected output:** Shows that agents are properly configured to use cc-memory for context and solution storage.

## test-mcp-builder.sh

Tests the code generation approach (Vikalpa's pattern) for modularizing mcp_server.py.
The builder concatenates tool modules from `mcp_tools/` into a single mcp_server.py.

```bash
# Test mode (writes to _mcp_server_generated.py, preserves original)
python -m cc_soul.mcp_tools._mcp_builder --test

# Production mode (overwrites mcp_server.py)
python -m cc_soul.mcp_tools._mcp_builder
```

**Current status:** Prototype with 2 sample modules (backup.py, dreams.py = 6 tools).
Full migration requires extracting all ~130 tools from current mcp_server.py.

## test_antahkarana_convergence.py

Tests the Antahkarana convergence system with correct attribute names.

**What it tests:**
- `VoiceTask` uses `perspective` (not deprecated `voice`)
- `VoiceSolution` uses `perspective` and `confidence` (not `voice`/`shraddha`)
- `SamvadaResult` uses `strategy_used`, `final_solution`, `confidence` (not deprecated names)
- Full flow: awaken → submit insights → harmonize → retrieve

**Run:**
```bash
python3 .scripts/test_antahkarana_convergence.py
```

**Expected output:** All attribute checks pass, full convergence flow works.

## validate_brain.py

Tests the cc-brain spreading activation system.

**Philosophy:**
- Content-addressed concepts: same title = same concept (deduplication)
- Types become tags: a concept can be wisdom AND belief AND term
- Dual-path spreading: semantic (embeddings) + Hebbian (co-activation)
- Hebbian edges are learned through use, not auto-linked

**What it tests:**
- Content-addressed ID generation (normalized titles)
- Concept deduplication (same title → merged types)
- Dual-path spreading (semantic + Hebbian paths)
- Hebbian learning (edge creation from co-activation)

**Run:**
```bash
python .scripts/validate_brain.py
```

**Expected output:** All 4 tests pass, shows brain stats with concepts and edges.

## extract_mcp_tools.py

Initial extraction of tool sections from mcp_server.py into mcp_tools/.
Used once to bootstrap the modular structure.

## reorganize_mcp_tools.py

Reorganizes extracted files into optimal semantic structure:
- Renames long filenames to short ones (e.g., `write_operations_growing_the_soul.py` -> `write.py`)
- Splits large files (spanda -> spanda.py, antahkarana.py, orchestration.py)
- Merges related files (self_improvement_* -> evolution.py)

**Final structure:**
22 modules, 140 tools, alphabetically ordered.

## validate_signals.py

Tests the cc-soul signal system for background voice distillation.

**Philosophy:**
- Signals are distilled insights from background voice processing (Manas, Buddhi, etc.)
- Unlike concepts (static knowledge), signals represent dynamic relevance
- Signals have activation weights that decay without reinforcement (Hebbian)
- Main instance reads top-K signals; can elaborate via delegation

**What it tests:**
- Signal creation and storage in cc-memory
- Signal retrieval with weight filtering
- Signal context generation for hooks
- Manas scan for pattern detection from observations
- Brain integration (signals as additional seeds)

**Run:**
```bash
python .scripts/validate_signals.py
```

**Expected output:** All 6 tests pass, shows signal creation and retrieval.

## validate_minimal_context.py

Tests the minimal startup context for lean mode.

## migrate_claude_mem.py

Migrates observations from claude-mem to cc-memory/cc-soul.

**Source:**
- `~/.claude-mem/claude-mem.db` - SQLite observations
- `~/.claude-mem/vector-db/chroma.sqlite3` - Chroma embeddings

**Target:**
- `.cc-memory/memory.db` - Project-local observations
- `~/.claude/mind/soul.db` - Universal wisdom (decisions/discoveries)
- `~/.claude/mind/vectors/lancedb/` - LanceDB embeddings

**Usage:**
```bash
# View statistics
python .scripts/migrate_claude_mem.py --stats

# Dry run for specific project
python .scripts/migrate_claude_mem.py --project cc-soul --target-dir . --index-vectors --promote-wisdom

# Execute migration
python .scripts/migrate_claude_mem.py --project cc-soul --target-dir . --index-vectors --promote-wisdom --execute

# Export to JSON for inspection
python .scripts/migrate_claude_mem.py --project cc-soul --export-json obs.json
```

**Options:**
- `--stats` - Show observation counts by project and type
- `--project NAME` - Filter to specific project
- `--target-dir PATH` - Project directory for cc-memory import
- `--index-vectors` - Create LanceDB embeddings (requires sentence-transformers)
- `--promote-wisdom` - Promote decisions/discoveries to soul wisdom
- `--execute` - Actually perform migration (default is dry-run)
- `--limit N` - Limit number of observations to process
