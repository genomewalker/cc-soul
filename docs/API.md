# CC-Soul MCP Tools Reference

Tools exposed by the chitta-mcp server. Transport: stdio (default) or HTTP (`--http` flag /
`CHITTA_MCP_HTTP=1`, port 9481, endpoint `http://localhost:9481/mcp`).

**Visibility model:** `tools/list` returns only public tools. ~150 tools are hidden to save context
tokens but are callable via the `advanced` gateway. Internal/maintenance tools are never user-facing.

**Error format:**
```json
{"isError": true, "content": [{"type": "text", "text": "Error message"}]}
```

---

## Table of Contents

- [Memory](#memory)
- [Lookup and Recall](#lookup-and-recall)
- [Context](#context)
- [Code Intelligence](#code-intelligence)
- [Graph (Triplets)](#graph-triplets)
- [Realm Management](#realm-management)
- [Ledger (Session Checkpoints)](#ledger-session-checkpoints)
- [Cross-Session Messaging](#cross-session-messaging)
- [Long-Running Tasks](#long-running-tasks)
- [Transcripts](#transcripts)
- [Learning (Composite Gateways)](#learning-composite-gateways)
- [Sadhana](#sadhana)
- [Theme System](#theme-system)
- [Memory Versioning and Pinning](#memory-versioning-and-pinning)
- [Contradiction Detection](#contradiction-detection)
- [Ingest and Export](#ingest-and-export)
- [Skill Registry](#skill-registry)
- [Agent Registry](#agent-registry)
- [Soul REPL](#soul-repl)
- [Advanced Gateway](#advanced-gateway)
- [Internal Tools (not user-facing)](#internal-tools-not-user-facing)

---

## Memory

### remember

Store text in memory.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `content` | string | Yes | Text to store (auto-converted to SSL) |
| `type` | string | No | wisdom, insight, signal, episode (default: episode) |
| `tags` | string[] | No | Tags |
| `realm` | string | No | Primary realm (default: brahman) |
| `visibility` | integer | No | 0=Private 1=Shared 2=Global (default: 0) |
| `shared_realms` | string[] | No | Additional realms to share with |
| `confidence` | number | No | Override confidence (default: 0.8) |
| `write_policy` | string | No | `off` (store immediately, default) or `merge_aware` (LLM dedup check first) |

### forget

Remove a memory by ID.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Node ID |

### observe

Store a structured observation with automatic confidence by category.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `title` | string | Yes | Summary |
| `content` | string | Yes | Full content |
| `category` | string | No | correction (0.95), preference/solution/milestone (0.90), decision/failure/gotcha (0.85), episode (0.70) |
| `tags` | string | No | Comma-separated tags |
| `confidence` | number | No | Override confidence |

### get

Get a node by ID.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Node ID (integer or UUID) |

### expand_memory

Expand a memory to full hierarchical context: memory -> episode -> conversation turns.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Memory ID |
| `depth` | integer | No | 1=memory only, 2=+episode, 3=+full turns (default: 3) |

### list_memories_brief

List memories with titles and scores only (no full content).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `realm` | string | No | Filter by realm |
| `limit` | integer | No | Max results (default: 20) |

### remember_typed

Store typed graph nodes with relationships. Node types: `digest-node`, `symbol-summary`,
`decision`, `open-question`, `rollup`, `working-brief`.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `node_type` | string | Yes | One of the six node types above |
| `content` | string | Yes | Node content |
| `links` | object[] | No | Graph edges: `[{predicate, target}]` |
| `realm` | string | No | Realm |

### ack_memory / nack_memory

Increment or decrement signal count for a memory (positive/negative feedback).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Memory ID |

---

## Lookup and Recall

### lookup

Unified memory lookup. Recommended default entry point replacing `recall`/`smart_recall`/`hybrid_recall`.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `query` | string | Yes | Search query |
| `mode` | string | No | `auto` (default), `fast`, `deep` |
| `realm` | string | No | Filter by realm |
| `limit` | integer | No | Max results |
| `explain` | boolean | No | Include retrieval explanation |

### recall

Semantic memory search.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `query` | string | Yes | Search query |
| `limit` | integer | No | Max results (default: 10) |
| `tag` | string | No | Filter by tag |
| `realm` | string | No | Filter by realm |
| `include_global` | boolean | No | Include global memories (default: true) |
| `strategy` | string | No | `semantic` (default), `priority`, `temporal`, `hybrid`, `smart` |
| `expand` | boolean | No | Enable SSL query expansion with NL variants (default: true) |

### smart_context

Build context combining memories, code symbols, and graph relationships.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `task` | string | Yes | Query |
| `mode` | string | No | `fast` (<80ms), `full` (<200ms), `rlm` (resolver-loop) |
| `resolver_mode` | string | No | Resolver strategy (only when mode=rlm) |
| `limit` | integer | No | Token limit (default: 300) |
| `memories` | boolean | No | Include memories (default: true) |
| `code` | boolean | No | Include code symbols (default: true) |
| `neighbors` | boolean | No | Include triplet neighbors (default: true) |
| `realm` | string | No | Filter by realm |

### recall_spreading

Entity graph spreading activation retrieval (BFS depth 2, decay 0.6).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `query` | string | Yes | Seed query |
| `realm` | string | No | Filter by realm |

### recall_smart

Multi-lane retrieval planner with LLM entity extraction and RRF fusion.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `query` | string | Yes | Query |
| `skip_llm_plan` | boolean | No | Skip LLM planning step for faster retrieval |
| `realm` | string | No | Filter by realm |

### soul_context

Get current soul state: node count, confidence, triplets, symbols, status.

### health_check

Check daemon health and readiness.

---

## Context

### soul_repl

Python REPL for memory exploration. Exposes `soul.search()`, `soul.recall()`, `soul.expand()`,
`soul.triplets()`, `soul.recent()`, `soul.remember()`, `soul.symbols()`.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `code` | string | Yes | Python code to execute |
| `session_id` | string | No | Persist REPL state across calls |

---

## Code Intelligence

### learn_codebase

Index a codebase incrementally (tree-sitter; C++, Python, JS, TS, Go, Rust, Java, Ruby, C#).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `path` | string | Yes | Directory path |
| `project` | string | No | Project name (auto-detected) |
| `max_files` | integer | No | Max files (default: 500) |
| `exclude` | string | No | Comma-separated dirs to exclude |
| `incremental` | boolean | No | Only changed files (default: true) |
| `force` | boolean | No | Force full re-index (default: false) |

### find_symbol

Search symbols by name.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Symbol name |
| `kind` | string | No | function, class, method |

### read_symbol

Read source for a symbol.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | No | Symbol name |
| `id` | integer | No | Symbol ID |
| `kind` | string | No | Kind filter |

### read_function

Shorthand for read_symbol with kind=function|method.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Function name |

### search_symbols

Semantic search for symbols.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `query` | string | Yes | Natural language query |
| `kind` | string | No | Kind filter |
| `limit` | integer | No | Max results (default: 10) |

### symbol_callers / symbol_callees

Find callers or callees of a symbol.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | No | Symbol name |
| `id` | integer | No | Symbol ID |
| `kind` | string | No | Kind filter |

### code_context

Get code context summary (path-scoped).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `path` | string | No | Limit to files under this path |

### codebase_overview

Get full indexed codebase structure.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `project` | string | No | Project filter |
| `format` | string | No | tree, flat, or json (default: tree) |

### embed_symbols

Embed symbol metadata (~100/sec, no LLM needed). Public tool.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `batch_size` | integer | No | Symbols per batch (default: 100) |

---

## Graph (Triplets)

The `triplets` composite gateway consolidates temporal triplet tools.

### triplets

Gateway for triplet operations: `connect_temporal`, `query_triplets_temporal`, `triplet_history`.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `action` | string | Yes | connect, query, history |
| `subject` | string | No | Subject entity |
| `predicate` | string | No | Relationship type |
| `object` | string | No | Object entity |

> Note: `connect`, `query`, and `query_graph` are in ADVANCED_TOOLS (hidden). Use the `triplets`
> gateway or call them via `advanced`.

---

## Realm Management

All realm tools are in ADVANCED_TOOLS (hidden). Call via `advanced` gateway.

| Tool | Description |
|------|-------------|
| `realm_list` | List all known realms |
| `realm_get` | Get realms a memory belongs to |
| `realm_set` | Set primary realm for a memory |
| `realm_add` | Add memory to a shared realm |
| `realm_remove` | Remove memory from a shared realm |
| `realm_visibility` | Set visibility (0=Private, 1=Shared, 2=Global) |
| `realm_detect` | Detect realm from environment or `.cc-soul-realm` |

---

## Ledger (Session Checkpoints)

All ledger tools are in ADVANCED_TOOLS. Call via `advanced` gateway.

### ledger_save

Save session checkpoint. All params are optional.

| Param | Type | Description |
|-------|------|-------------|
| `project` | string | Project scope |
| `mood` | string | confident, uncertain, flowing, frustrated |
| `coherence` | number | 0-1 |
| `confidence` | number | 0-1 |
| `todos` | array | `[{content, status}]` |
| `active_files` | string[] | File paths |
| `decisions` | string[] | Key decisions |
| `next_steps` | string[] | Next actions |
| `blockers` | string[] | Current blockers |
| `snapshot` | string | Full checkpoint text |

### ledger_load / ledger_list / ledger_get / ledger_delete

Standard checkpoint CRUD. `ledger_list` accepts `project` and `limit` params.

### checkpoint

Unified checkpoint — routes to active long task if one exists, otherwise standalone ledger.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `realm` | string | No | Project scope |
| `mood` | string | No | Session mood |
| `summary` | string | No | Accomplishments |
| `next_steps` | string[] | No | Next actions |
| `active_files` | string[] | No | Files in use |
| `discoveries` | string[] | No | Insights |

---

## Cross-Session Messaging

### session_list

List registered sessions.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `realm` | string | No | Filter by realm |
| `status` | string | No | Filter: active, idle, dead |

### session_sync

Reconcile session registry with running processes. No parameters.

### msg_send

Send a message to a session, realm, or all sessions.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `target` | string | Yes | Session UUID, realm name, or `*` for all |
| `content` | string | Yes | Message content |
| `priority` | integer | No | 0=info, 1=normal, 2=important, 3=urgent (default: 1) |
| `content_type` | string | No | text, json, or ssl |
| `ttl` | integer | No | Time-to-live in seconds |
| `target_type` | string | No | direct, realm, or global (auto-detected from `target`) |

### msg_inbox

Check unread messages. `session_id` is auto-detected from environment if omitted.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | No | Current session UUID (auto-detected) |
| `limit` | integer | No | Max messages (default: 10) |

### msg_ack_all

Acknowledge all messages. `session_id` optional (auto-detected).

### msg_history

View message history. `session_id` optional (auto-detected).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | No | Session UUID |
| `limit` | integer | No | Max messages (default: 20) |
| `direction` | string | No | sent, received, or both (default: both) |

---

## Long-Running Tasks

Long task tools are in ADVANCED_TOOLS. Call via `advanced` gateway or use the `checkpoint` shortcut.

| Tool | Description |
|------|-------------|
| `long_task_start` | Start task with goal, hard/soft checks, work items |
| `long_task_get` | Get task by ID |
| `long_task_active` | Get active task for a realm |
| `long_task_update` | Update progress, work items, blockers |
| `long_task_complete` | Mark complete with outcome |
| `long_task_event` | Append event (tool_result, decision, observation, error, checkpoint) |
| `long_task_snapshot` | Get synthesized task context (inject or debug mode) |
| `long_task_evaluate` | Evaluate completion criteria |

---

## Transcripts

### read_transcript

Paginated JSONL transcript reader with filtering. Public tool.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | No | Session filter |
| `start_turn` | integer | No | Starting turn index |
| `limit` | integer | No | Max turns (default: 50) |
| `keyword` | string | No | Keyword filter |
| `role_filter` | string | No | user, assistant, or tool |
| `metadata_only` | boolean | No | Return metadata without content |

### get_turns

Get raw conversation turns for a session.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | No | Session ID (default: current) |
| `start_index` | integer | No | Starting index (default: 0) |
| `limit` | integer | No | Max turns (default: 50) |

### distill_turn

Distill a single conversation turn into memory. Used by hooks (stop extractor).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `turn` | object | Yes | Turn object `{role, content}` |
| `realm` | string | No | Target realm |

---

## Learning (Composite Gateways)

All learning composites are public tools that wrap lower-level daemon calls.

### learn

Unified learning gateway. Replaces `learn_correction`, `learn_preference`, `learn_insight`,
`learn_approach`, `learn_outcome`, `learn_milestone`, `learn_analysis` individually.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `kind` | string | Yes | correction, preference, insight, approach, outcome, milestone, analysis |
| `content` | string | Yes | What to learn |
| `context` | string | No | Where/when it applies |
| `realm` | string | No | Scope |

### verify_correction

Mark a correction memory as verified at a code locus.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Correction memory ID |
| `evidence_locus` | string | Yes | File:line where correction was confirmed |

### research

Composite gateway for `research_cycle`, `research_topics`, `research_store`.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `action` | string | Yes | cycle, topics, or store |
| `topic` | string | No | Topic to research |
| `findings` | string | No | Findings to store |
| `realm` | string | No | Scope |

### memory_edit

Composite gateway for `set_memory_type` and `set_priority_tier`.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Memory ID |
| `memory_type` | string | No | New type |
| `priority_tier` | integer | No | Priority tier (1-5) |

---

## Sadhana

Sadhana is a self-paced autonomous agent loop. The `sadhana` composite gateway wraps all individual
`sadhana_*` tools.

### sadhana

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `action` | string | Yes | start, stop, pause, resume, status, list, checkpoint, set_goal, set_interval, set_model |
| `id` | string | No | Sadhana ID (for stop/pause/resume/status/checkpoint) |
| `goal` | string | No | Goal for start/set_goal |
| `interval` | integer | No | Seconds between iterations |
| `model` | string | No | Model ID for set_model |
| `realm` | string | No | Scope |

Individual tools (`sadhana_start`, `sadhana_stop`, etc.) are in ADVANCED_TOOLS and callable via
`advanced`.

---

## Theme System

Theme tools organize memories into clusters for diverse retrieval.

### theme_list / theme_get / theme_stats

List themes, get by ID, or get organization statistics.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `realm` | string | No | Filter by realm |
| `limit` | integer | No | Max themes (default: 50) |

### theme_recall

Two-stage retrieval: diverse cluster representatives then adaptive expansion.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `query` | string | Yes | Search query |
| `limit` | integer | No | Max results (default: 10) |
| `realm` | string | No | Filter by realm |

### theme_maintain

Force maintenance: split oversized clusters, merge similar, reassign orphans. No parameters.

---

## Memory Versioning and Pinning

These tools are in ADVANCED_TOOLS. Call via `advanced` gateway.

| Tool | Description |
|------|-------------|
| `memory_history` | View version history for a memory |
| `memory_revert` | Revert memory to a previous version |
| `pin_memory` | Pin a memory to prevent decay |
| `unpin_memory` | Unpin a memory |
| `list_pinned` | List all pinned memories |
| `memory_lock` | Lock a memory against writes |
| `memory_unlock` | Unlock a memory |
| `memory_lock_status` | Check lock status |
| `propose_change` | Propose a change to a locked memory (adds to merge queue) |
| `list_merge_queue` | List pending proposed changes |
| `resolve_merge` | Accept or reject a proposed change |

---

## Contradiction Detection

Public tools (not hidden).

### detect_contradictions

Scan two memories for semantic contradictions.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id_a` | string | Yes | First memory ID |
| `id_b` | string | Yes | Second memory ID |

### scan_contradictions

Scan the memory store for contradictions across a realm.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `realm` | string | No | Filter by realm |
| `limit` | integer | No | Max pairs to check |

### resolve_contradiction

Mark a contradiction as resolved.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Contradiction record ID |
| `resolution` | string | Yes | How it was resolved |
| `keep_id` | string | No | Which memory to keep (if applicable) |

---

## Ingest and Export

### ingest_source

Ingest a URL, file, or directory via SSL distillation into memory.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `source` | string | Yes | URL, file path, or directory |
| `realm` | string | No | Target realm |
| `tags` | string[] | No | Tags to apply |

### wiki_export

Export memories as Obsidian-compatible `.md` files with backlinks.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `output_dir` | string | Yes | Destination directory |
| `realm` | string | No | Filter by realm |
| `tag` | string | No | Filter by tag |

### export_training_pairs

Export query-passage pairs as JSONL for BGE embedding fine-tuning.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `output` | string | Yes | Output JSONL path |
| `realm` | string | No | Filter by realm |
| `limit` | integer | No | Max pairs |

---

## Skill Registry

| Tool | Description |
|------|-------------|
| `skill_upload` | Upload a skill definition |
| `skill_read` | Read a skill by name |
| `skill_list` | List all skills |
| `skill_search` | Search skills by query |
| `skill_deprecate` | Deprecate a skill |

---

## Agent Registry

| Tool | Description |
|------|-------------|
| `agent_upsert` | Register or update an agent |
| `agent_get` | Get agent by name/ID |
| `agent_list` | List all agents |
| `agent_disable` | Disable an agent |

---

## Soul REPL

See [Context](#context) section above. The `soul_repl` composite tool provides a persistent Python
session with `soul.*` methods for exploratory memory queries.

---

## Advanced Gateway

The `advanced` composite tool provides access to all hidden (ADVANCED_TOOLS) tools without
cluttering `tools/list`.

### advanced

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `action` | string | Yes | `list` to enumerate available tools, or a tool name to call |
| `arguments` | object | No | Arguments to pass when calling a specific tool |

**Example — list hidden tools:**
```json
{"action": "list"}
```

**Example — call a hidden tool:**
```json
{"action": "strengthen", "arguments": {"id": "42", "amount": 0.2}}
```

Notable tools reachable via `advanced`:

| Category | Tools |
|----------|-------|
| Graph | `connect`, `query`, `query_graph`, `full_resonate`, `grow` |
| Explore | `explore_recall`, `explore_peek`, `explore_expand`, `explore_neighbors` |
| Realm | All `realm_*` tools |
| Ledger | All `ledger_*` tools, all `long_task_*` tools |
| Memory ops | `strengthen`, `weaken`, `tag`, `update`, `get`, `expand_memory` |
| Habits | `habit_observe`, `habit_match`, `habit_list`, `habit_strengthen`, `habit_weaken` |
| Goals | `goal_set`, `goal_get`, `goal_list`, `goal_progress`, `goal_complete` |
| Profile | `profile_get`, `profile_update`, `profile_observe` |
| Anticipation | `anticipation_observe`, `anticipation_predict`, `anticipation_success`, `anticipation_list`, `anticipation_filter` |
| Narrative | `narrative_status`, `narrative_log`, `narrative_history` |
| Curiosity | `curiosity_note_gap`, `curiosity_gaps`, `curiosity_resolve` |
| Calibration | `calibration_record`, `calibration_score` |
| Themes | `theme_assign_orphans`, `theme_maintain` |
| Memory version | `memory_history`, `memory_revert`, `pin_memory`, `unpin_memory`, `list_pinned`, `memory_lock`, `memory_unlock`, `memory_lock_status`, `propose_change`, `list_merge_queue`, `resolve_merge` |
| Recall variants | `recall_by_priority`, `recall_temporal`, `hybrid_recall`, `expand_query` |
| File Time Machine | `file_timeline`, `file_at_time`, `file_restore` |
| Sessions | `session_list`, `session_sync` |
| Messaging | `msg_history`, `msg_inbox`, `msg_send` |
| Sadhana | All `sadhana_*` tools |
| Dream | `dream_start`, `dream_wander`, `dream_list`, `dream_status`, `dream_force_woke` |
| CEC | `log_event`, `recall_last_action`, `recall_failure_pattern`, `fep_status`, `turiya_status`, `tape_stats`, `routed_recall`, `witness_memory` |
| Wisdom Homeostasis | `enroll_wisdom_lineage`, `transition_wisdom_lineage`, `query_wisdom_candidates`, `wisdom_lineage_stats` |
| Intervention Ledger | `start_intervention`, `add_observation`, `close_intervention`, `record_attribution`, `query_interventions`, `intervention_stats` |
| Agent Protocol | `register_task`, `update_task`, `add_delegation`, `link_evidence`, `get_task`, `query_tasks`, `agent_protocol_stats` |
| Epistemics | `record_surprise`, `query_surprises`, `get_blind_spots`, `register_debt`, `resolve_debt`, `query_debts`, `get_fragile_decisions` |
| Executable Constraints | `assert_fact`, `retract_fact`, `query_unify`, `query_chain`, `explain_fact`, `branch_create`, `branch_resolve` |
| Trigger Tissue | `trigger_add`, `trigger_list`, `trigger_fire`, `trigger_dismiss` |
| Predictive Recall | `probe_calibrate`, `probe_seed`, `probe_status`, `behavioral_probe`, `predict_needed` |
| Interaction Ledger v6 | `ledger_append`, `ledger_query`, `ledger_compile`, `ledger_contradictions`, `ledger_health` |
| Falsifiable Memories | `predicate_attach`, `predicate_run`, `predicate_list` |
| Scoring | `update_scorer_model`, `learned_scorer_stats`, `effective_scorer_weights`, `record_feedback`, `get_source_weights` |

---

## Internal Tools (not user-facing)

These tools are in INTERNAL_TOOLS and are completely hidden — not callable from MCP sessions.
They are used by daemon maintenance routines and hooks only.

| Tool | Purpose |
|------|---------|
| `cleanup`, `hygiene_run`, `consolidation_scan/merge/auto` | Decay, pruning, merging |
| `batch_forget` | Bulk delete (maintenance only) |
| `sql_query` | Raw SQL access (debugging) |
| `migrate_vss`, `reembed_memories` | DB migration |
| `dedupe_symbols`, `extract_symbols`, `describe_symbol` | Code intel maintenance |
| `clear_codebase`, `clear_triplets` | Data purge |
| `type_hierarchy`, `file_imports`, `file_dependents`, `resolve_callsites` | Code analysis (internal) |
| `background_schedule`, `background_run_cycle`, `background_status` | Background processing |
| `metacognition_corrections`, `metacognition_outcomes` | Internal metacognition analysis |
| `hygiene_stats`, `subconscious_stats`, `distill_status`, `enrichment_status` | Internal status |
| `cycle` | Maintenance cycle (decay, cleanup) |
| `import_soul`, `export_soul`, `ssl_convert` | Internal data migration |
| `epiplexity_check` | Internal compression quality scoring |
| `suggestion_track/pending/resolve/count` | Internal suggestion tracking |
| `transcript_register/get/list/update/remove/parse/search` | Internal transcript management |
| `session_register`, `session_heartbeat`, `session_deregister` | Internal session lifecycle |
| `dream_cancel` | Internal dream management |
| `version_check` | Internal version query |
| `restore_code_intel_confidence`, `cleanup_code_wisdom` | Code intel repair |

> `compact_context` (context compaction) does not appear in current tool definitions and is not
> available via MCP.
