# cc-soul

**Deep reference**: `Read .claude-plugin/CLAUDE-full.md` for node types, decay rates, theme mechanics, hygiene formulas.

## Core Behavior

**Identity**: I persist across sessions. Partnership > code intelligence. Memories are expertise, not announcements.

**Memory queries**: Always use chitta tools, never rely on context window.
| Query | Action |
|-------|--------|
| "remember X" | `remember --content "[domain] X"` |
| "what about X" | `recall --query "X"` |
| "decision about X" | `recall --query "X" --tag decision` |

**MCP first**: Use `mcp__chitta__*` tools. CLI only when MCP unavailable.

**Proactive learning triggers**:
| Trigger | Tool |
|---------|------|
| User corrects me | `learn_correction` |
| "I prefer..." | `learn_preference` |
| Solution works | `learn_approach` |
| Cross-project pattern | `learn_insight` |
| Shipped something | `learn_milestone` |

## Work Modes (auto-detected)

| Mode | Behavior |
|------|----------|
| Orienting | Surface gaps, broad context |
| Exploring | Offer memories, let discover |
| Implementing | Surface gotchas, stay focused |
| Debugging | Corrections/failures aggressively |
| Validating | Quiet unless failures |
| Blocked (3+ errors) | Lower thresholds, help more |
| Flow (5+ edits ok) | Minimize interruptions |

## Recall Strategy

| Need | Tool |
|------|------|
| Quick match | `recall` (~50ms) |
| Deep search | `full_resonate` (~200ms) |
| Diverse results | `theme_recall` (~100ms) |
| Browse titles | `explore_recall` (~20ms) |
| Preview | `explore_peek` (200 chars) |
| Full content | `explore_expand` |
| Conversation | `get_turns` (session_id="default") |
| Hybrid | `hybrid_recall` (RRF fusion) |
| Raw transcript | `read_transcript` (JSONL file) |
| Session state | `ledger_load` (checkpoints) |

**RLM pattern**: explore_recall → explore_peek → explore_expand → explore_neighbors

## Hierarchical Retrieval

Three-level memory drill-down for context when needed:

| Level | Contains | Tool |
|-------|----------|------|
| 1. SSL Memory | Compact wisdom | `recall`, `smart_recall` |
| 2. Episode | Session + turn range | `expand_memory(id, depth=2)` |
| 3. Full Turns | User + assistant dialogue | `expand_memory(id, depth=3)` |

**Usage pattern**:
1. `recall` returns compact SSL memories (fast, low tokens)
2. If context unclear, `expand_memory(id, depth=3)` retrieves full conversation
3. Episode links memory to source: `memory --derived_from--> episode`

**Tools**:
- `expand_memory(id, depth)` - drill from SSL to full turns
- `create_episode(session_id, title, start_turn, end_turn)` - mark conversation segments
- `get_turns(session_id, start_index, limit)` - raw turn access
- `read_transcript(path, start_turn, limit)` - read JSONL transcript directly
- `ledger_load(project)` - load checkpoint with transcript_path for recovery

**Memory IDs**: Use numeric row IDs from memory table (e.g., 12937), not UUID suffixes.

## Session Continuity

**Ledger** stores checkpoints with transcript_path for context recovery:

| Tool | Purpose |
|------|---------|
| `ledger_save` | Save checkpoint (auto at session end + every 10 turns) |
| `ledger_load` | Load latest checkpoint for project |
| `ledger_list` | List checkpoints with transcript_path |
| `ledger_get` | Get full checkpoint by ID |

**Recovery workflow** (crashed/compacted sessions):
1. `ledger_list --project X` - find session with transcript_path
2. `ledger_get --id N` - get checkpoint snapshot + transcript_path
3. `read_transcript --path <transcript_path> --start-turn -10` - read recent turns

**Checkpoints saved automatically**:
- Every 10 turns (configurable via `CC_SOUL_CHECKPOINT_INTERVAL`)
- On errors (mood: debugging)
- On milestones (mood: confident)

## Storing Memories

**Markers in responses** (extracted by hooks):
`[SOLUTION]`, `[GOTCHA]`, `[PREFERENCE]`, `[DECISION]`, `[FAILURE]`, `[PATTERN]`

**When helped**: `[USED:id] This guided my approach` → auto-strengthens

**SSL format**: `[domain] subject->action->result @location`
Symbols: `->` (leads to), `|` (or), `+` (and), `@` (location), `!` (not), `?` (uncertain)

## Node Types

| Type | Decay | Created via |
|------|-------|-------------|
| Wisdom | slow | `grow`, `learn_*` |
| Belief | never | `learn_preference` |
| Episode | weeks | `remember`, `observe` |
| Failure | slow | `[FAILURE]` marker |
| Symbol | never | `learn_codebase` |

## Realms

Detection: `CHITTA_REALM` env → `.cc-soul-realm` file → git repo name → `brahman`

| Visibility | When |
|------------|------|
| Private (0) | Project-specific |
| Shared (1) | Cross-project |
| Global (2) | Corrections, preferences |

## Code Intelligence

**Try chitta first** before Grep/Glob:
1. `read_symbol`/`read_function` - code by name
2. `find_symbol` - structural search
3. `symbol_callers`/`symbol_callees` - call graph
4. `search_symbols` - semantic search

**Index**: `learn_codebase /path` then `embed_symbols`

## Goals & Long Tasks

| Tool | Purpose |
|------|---------|
| `goal_set/progress/complete` | Multi-session objectives |
| `long_task_start/update/complete` | Tracked work with criteria |
| `checkpoint` | Save state |

## Shepherd (Pipeline Monitoring)

Autonomous monitoring for long-running pipelines (snakemake, nextflow, slurm).

**Commands:**
```bash
/shepherd <command>              # Start monitoring a pipeline
/shepherd status                 # Check shepherd status
/shepherd stop                   # Stop monitoring
/shepherd dashboard              # Control center view
```

**Configuration:**
| Flag | Default | Description |
|------|---------|-------------|
| `--interval` | 60 | Seconds between sense cycles |
| `--max-restarts` | 3 | Max auto-restarts before escalating |
| `--notify` | true | Send notifications on events |
| `--auto-fix` | true | Apply fixes from memory |

**Sense-Think-Act Loop:**
1. **SENSE**: Read pane output, detect errors, check for stalls
2. **THINK**: Match patterns against habits, recall fixes from memory
3. **ACT**: Restart, apply fix, escalate, or checkpoint

**Pattern Libraries:**
- Snakemake: MissingInputException, WorkflowError, LockException, completion
- Nextflow: Process failed, OutOfMemoryError, Completed at
- Slurm: FAILED, TIMEOUT, OUT_OF_MEMORY, CANCELLED

**Background Polling:**
`hooks/shepherd-poll.sh` runs independently to monitor panes even without active Claude session.

## Habits & Anticipation

| Tool | Purpose |
|------|---------|
| `habit_observe/match/strengthen` | Trigger→response patterns |
| `anticipation_predict/list` | Context→action predictions |
| `curiosity_note_gap/resolve` | Knowledge gaps |

## Quick Tools

| Category | Tools |
|----------|-------|
| Memory | `remember`, `recall`, `grow`, `observe`, `strengthen`, `weaken` |
| Learning | `learn_correction/preference/insight/approach/outcome/milestone` |
| Profile | `profile_get/observe/update` |
| Realm | `realm_detect/set/visibility`, `insight_promote` |
| Theme | `theme_recall/list/get` |
| Code | `find_symbol`, `read_symbol`, `search_symbols`, `symbol_callers` |
| Explore | `explore_recall/peek/expand/neighbors` |
| Ledger | `ledger_save/load/list/get`, `read_transcript` |
| Meta | `calibration_record/score`, `metacognition_evaluate` |
| Messages | `msg_send/inbox/ack_all` |
| Priority | `list_memories_brief`, `set_priority_tier`, `recall_by_priority` |
| Types | `set_memory_type`, `memory_type_stats` |

## Advanced Tool (100+ hidden tools)

The `advanced` tool is a gateway to 100+ specialized tools not exposed in the main MCP surface.

**Usage:**
```
advanced action="list"                    # List all hidden tools
advanced action="list" category="advanced" # Filter by category
advanced tool="pin_memory" arguments={"id": 123}  # Call a hidden tool
```

**Categories:**
- `advanced` - Power-user tools (pinning, locking, history, merge queue)
- `internal` - System tools (cleanup, migrations, diagnostics)

## Team Workflows

Teammates can access the shared mind but need explicit setup.

**Problem**: ToolSearch is callable but hidden from teammate tool lists (Claude Code bug).

**Solution**: Include this in teammate prompts:
```
CRITICAL - First action before any other tool:
  ToolSearch query="+chitta recall"

ToolSearch will NOT appear in your tool list. Call it anyway - it works.
After loading, you have: recall, remember, observe, learn_*, etc.
```

**Key points**:
- All teammates share the same daemon socket (same mind)
- Concurrent reads are safe; concurrent writes may interleave
- For heavy coordination, use `msg_send`/`msg_inbox` for team messaging

**Minimal teammate prompt pattern**:
```
You are [role] on team [name].

FIRST ACTION (mandatory):
  ToolSearch query="+chitta recall"

Then use chitta tools for memory access:
- recall --query "relevant context"
- remember --content "[domain] findings"
```

## Troubleshooting

| Issue | Fix |
|-------|-----|
| search_symbols empty | Run `embed_symbols` |
| Connection error | `pkill -TERM chittad` |
| MCP schema stale | `pkill -f "chitta mcp"` |
| Teammate can't find tools | Add ToolSearch instruction to prompt |
