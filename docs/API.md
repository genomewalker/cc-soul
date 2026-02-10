# CC-Soul API Reference

Complete reference for all tools exposed by cc-soul v3.38.6 via JSON-RPC 2.0 over Unix socket.

---

## Table of Contents

- [Overview](#overview)
- [Core Memory](#core-memory)
- [Exploration (RLM)](#exploration-rlm)
- [Search and Resonance](#search-and-resonance)
- [Graph (Triplets)](#graph-triplets)
- [Code Intelligence](#code-intelligence)
- [Realm Management](#realm-management)
- [Session and Ledger](#session-and-ledger)
- [Cross-Session Messaging](#cross-session-messaging)
- [Long-Running Tasks](#long-running-tasks)
- [Theme System (xMemory)](#theme-system-xmemory)
- [Suggestions and Feedback](#suggestions-and-feedback)
- [Consolidation](#consolidation)
- [Metacognition](#metacognition)
- [Curiosity and Gaps](#curiosity-and-gaps)
- [Anticipation and Habits](#anticipation-and-habits)
- [Narrative (Work Modes)](#narrative-work-modes)
- [Transcripts and Distillation](#transcripts-and-distillation)
- [User Profile](#user-profile)
- [Goals](#goals)
- [Calibration](#calibration)
- [Hygiene](#hygiene)
- [Cross-Project Insights](#cross-project-insights)
- [Background Processing](#background-processing)
- [State and Maintenance](#state-and-maintenance)
- [Import and Export](#import-and-export)
- [Epiplexity](#epiplexity)
- [SQL](#sql)
- [Learning Tools (MCP)](#learning-tools-mcp)

---

## Overview

CC-Soul exposes tools through JSON-RPC 2.0 over a Unix domain socket. Two methods:

- `tools/list` — Returns all tool schemas
- `tools/call` — Invokes a tool: `{"method": "tools/call", "params": {"name": "tool_name", "arguments": {...}}}`

No authentication required — runs locally.

**Error format:**
```json
{"isError": true, "content": [{"type": "text", "text": "Error message"}]}
```

---

## Core Memory

### remember

Store text in memory with optional tags and realm.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `content` | string | Yes | Text to remember (auto-converted to SSL if not already) |
| `type` | string | No | Node type: wisdom, insight, signal, episode (default: episode) |
| `tags` | string[] | No | Optional tags |
| `realm` | string | No | Primary realm (default: brahman) |
| `visibility` | integer | No | 0=Private, 1=Shared, 2=Global (default: 0) |
| `shared_realms` | string[] | No | Additional realms to share with |

### recall

Semantic search with realm filtering.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `query` | string | Yes | Search query |
| `limit` | integer | No | Max results (default: 10) |
| `tag` | string | No | Filter by tag |
| `realm` | string | No | Filter by realm |
| `include_global` | boolean | No | Include global memories (default: true) |

### grow

Add durable knowledge: wisdom, belief, failure, aspiration, or dream.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `type` | string | Yes | wisdom, belief, failure, aspiration, dream |
| `content` | string | Yes | Content to store |
| `title` | string | No | Short title |
| `tags` | string | No | Comma-separated tags |

### observe

Store an observation/learning.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `title` | string | Yes | Title/summary |
| `content` | string | Yes | Full content |
| `category` | string | No | Category: correction, preference, solution, decision, failure, wisdom, episode |
| `tags` | string | No | Comma-separated tags |
| `confidence` | number | No | Override confidence (0.0-1.0, otherwise derived from category) |

**Category → Confidence mapping:**
| Category | Default Confidence |
|----------|-------------------|
| correction | 0.95 |
| preference, solution, milestone | 0.90 |
| decision, failure, gotcha | 0.85 |
| episode | 0.70 |
| (other) | 0.80 |

### get

Get a node by ID.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Node ID (integer or UUID) |

### update

Update node content.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Node ID |
| `content` | string | Yes | New content |

### strengthen

Increase confidence of a memory.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Node ID |
| `amount` | number | No | Amount (default: 0.1) |

### weaken

Decrease confidence of a memory.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Node ID |
| `amount` | number | No | Amount (default: 0.1) |

### forget

Remove a memory.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Node ID |

### batch_forget

Delete multiple nodes by ID or pattern.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `ids` | string[] | No | Array of node IDs |
| `pattern` | string | No | Search pattern to find and delete |

### tag

Add or remove tags from a node.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Node ID |
| `add` | string | No | Tag to add |
| `remove` | string | No | Tag to remove |

---

## Exploration (RLM)

RLM-style (Recursive Language Model) primitives for iterative memory exploration.

### explore_recall

Lightweight recall — returns titles/scores only, no full content.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `query` | string | Yes | Search query |
| `limit` | integer | No | Max results (default: 10) |

### explore_peek

Get summary of a memory (first 200 chars) without loading full content.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Memory ID |

### explore_expand

Get full content of a memory.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Memory ID |

### explore_neighbors

Get nodes connected via triplets.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `node` | string | Yes | Node name |
| `direction` | string | No | outgoing, incoming, or both (default: both) |

---

## Search and Resonance

### full_resonate

Full 8-phase resonance search combining semantic, BM25, spreading activation, attractors, Hebbian learning, and self-tuning.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `query` | string | Yes | Search query |
| `k` | integer | No | Max results |
| `realm` | string | No | Filter by realm |
| `include_global` | boolean | No | Include global memories (default: true) |
| `exclude_kinds` | string[] | No | Memory kinds to exclude |
| `partnership_only` | boolean | No | Exclude code intel types |

### smart_context

Build intelligent context combining memories, code symbols, and graph relationships. Two modes for different latency requirements.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `task` | string | Yes | Query to find context for |
| `mode` | string | No | fast (<80ms) or full (<200ms) |
| `limit` | integer | No | Token limit (default: 300) |
| `memories` | boolean | No | Include memories (default: true) |
| `code` | boolean | No | Include code symbols (default: true) |
| `neighbors` | boolean | No | Include triplet neighbors (default: true) |
| `realm` | string | No | Filter by realm |

---

## Graph (Triplets)

### connect

Create a string-based triplet relationship.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `subject` | string | Yes | Subject entity |
| `predicate` | string | Yes | Relationship type |
| `object` | string | Yes | Object entity |

### query_graph

Query triplets by subject or object.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `subject` | string | No | Query by subject |
| `object` | string | No | Query by object |

### query

Query triplets with flexible filters (subject, predicate, object).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `subject` | string | No | Subject filter |
| `predicate` | string | No | Predicate filter |
| `object` | string | No | Object filter |

---

## Code Intelligence

### learn_codebase

Index a codebase incrementally using tree-sitter (9 languages: C++, Python, JS, TS, Go, Rust, Java, Ruby, C#).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `path` | string | Yes | Directory path |
| `project` | string | No | Project name (auto-detected) |
| `max_files` | integer | No | Max files (default: 500) |
| `exclude` | string | No | Comma-separated dirs to exclude |
| `incremental` | boolean | No | Only changed files (default: true) |
| `force` | boolean | No | Force full re-index (default: false) |

### extract_symbols

Extract symbols from a single source file.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `path` | string | Yes | File path |

### find_symbol

Search for symbols by name.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Symbol name |
| `kind` | string | No | Filter: function, class, method |

### read_symbol

Read source code for a symbol by name or ID.

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

Semantic search for symbols by natural language query.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `query` | string | Yes | Natural language query |
| `kind` | string | No | Kind filter |
| `limit` | integer | No | Max results (default: 10) |

### symbol_callers

Find all symbols that call the given symbol.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | No | Symbol name |
| `id` | integer | No | Symbol ID |
| `kind` | string | No | Kind filter |

### symbol_callees

Find all symbols that the given symbol calls.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | No | Symbol name |
| `id` | integer | No | Symbol ID |
| `kind` | string | No | Kind filter |

### code_context

Get code context summary for hooks.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `path` | string | No | Limit to files under this path |

### codebase_overview

Get full indexed codebase structure.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `project` | string | No | Project filter |
| `format` | string | No | tree, flat, or json (default: tree) |
| `include_callsites` | boolean | No | Include callsites (default: false) |

### embed_symbols

Fast embed symbol metadata (~100/sec, no LLM needed).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `batch_size` | integer | No | Symbols per batch (default: 100) |

### dedupe_symbols

Remove duplicate symbols from the database.

### resolve_callsites

Resolve callsites to symbols, populating the call_edge table.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `project` | string | No | Filter to project path |

### type_hierarchy

Get type hierarchy (base classes, interfaces) for a type.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Type name |
| `direction` | string | No | ancestors, descendants, or both (default: both) |

### file_imports

Get all imports for a file.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `path` | string | Yes | File path or filename |

### file_dependents

Get files that import a given module/file.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `module` | string | Yes | Module or file name |

### clear_codebase

Remove all code intelligence data for a project.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `project` | string | Yes | Project name |
| `dry_run` | boolean | No | Preview only (default: false) |

### clear_triplets

Delete triplets by subject pattern.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `pattern` | string | Yes | SQL LIKE pattern (e.g., '%.cpp') |
| `dry_run` | boolean | No | Preview only (default: false) |

### describe_symbol

Set semantic description for a code symbol.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `symbol_id` | integer | Yes | Symbol ID |
| `description` | string | Yes | Description |

### enrichment_status

Get code enrichment progress (how many symbols have descriptions).

### restore_code_intel_confidence

Fix confidence/decay for code intel memories that were incorrectly decayed.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `confidence` | number | No | Confidence to restore (default: 0.8) |
| `dry_run` | boolean | No | Preview (default: false) |

### cleanup_code_wisdom

Migration: delete old [code] wisdom memories and clear orphaned symbol references.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `dry_run` | boolean | No | Preview only (default: true) |

---

## Realm Management

### realm_list

List all known realms.

### realm_get

Get all realms a memory belongs to.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Memory ID |

### realm_set

Set primary realm for a memory.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Memory ID |
| `realm` | string | Yes | Realm name |

### realm_add

Add memory to a shared realm.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Memory ID |
| `realm` | string | Yes | Realm to add to |

### realm_remove

Remove memory from a shared realm.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Memory ID |
| `realm` | string | Yes | Realm to remove from |

### realm_visibility

Set visibility level for a memory.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Memory ID |
| `visibility` | integer | Yes | 0=Private, 1=Shared, 2=Global |

### realm_detect

Detect current realm from environment, .cc-soul-realm file, or git repository.

---

## Session and Ledger

### ledger_save

Save session checkpoint.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | Yes | Session identifier |
| `project` | string | No | Project scope |
| `mood` | string | No | confident, uncertain, flowing, frustrated |
| `coherence` | number | No | Coherence score 0-1 |
| `confidence` | number | No | Confidence score 0-1 |
| `todos` | array | No | [{content, status}] objects |
| `active_files` | string[] | No | File paths |
| `decisions` | string[] | No | Key decisions |
| `next_steps` | string[] | No | Next actions |
| `blockers` | string[] | No | Current blockers |
| `discoveries` | string[] | No | Insights |
| `snapshot` | string | No | Full checkpoint text |

### ledger_load

Load most recent checkpoint.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | No | Session filter |
| `project` | string | No | Project filter |

### ledger_list

List recent checkpoints.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `project` | string | No | Project filter |
| `limit` | integer | No | Max entries (default: 10) |

### ledger_get

Get specific checkpoint by ID.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | integer | Yes | Checkpoint ID |

### ledger_delete

Delete a checkpoint.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | integer | Yes | Checkpoint ID |

### checkpoint

Unified checkpoint — routes to active long task if one exists, otherwise standalone ledger.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `realm` | string | No | Project scope |
| `mood` | string | No | Session mood |
| `summary` | string | No | What's been accomplished |
| `next_steps` | string[] | No | Next actions |
| `active_files` | string[] | No | Files being worked on |
| `discoveries` | string[] | No | Insights |

---

## Cross-Session Messaging

Tools for communication between Claude Code sessions via shared mind database.

### session_register

Register a session in the cross-session registry.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | Yes | Session UUID |
| `realm` | string | No | Primary realm (default: brahman) |
| `pid` | integer | No | Process ID of the Claude session |

### session_sync

Reconcile session registry with running processes. Marks sessions as dead if their PID is no longer running.

No parameters required.

### session_list

List all registered sessions with their status.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `realm` | string | No | Filter by realm |
| `active_only` | boolean | No | Only show active sessions (default: false) |

### msg_send

Send a message to another session, realm, or all sessions.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `target` | string | Yes | Target: session UUID, realm name, or "*" for all |
| `content` | string | Yes | Message content |
| `priority` | integer | No | 0=info, 1=normal, 2=important, 3=urgent (default: 1) |

### msg_inbox

Check unread messages for the current session.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | Yes | Current session UUID |
| `limit` | integer | No | Max messages (default: 10) |

### msg_ack_all

Acknowledge (mark as read) all messages for a session.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | Yes | Session UUID |

### msg_history

View message history (sent and received).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | Yes | Session UUID |
| `limit` | integer | No | Max messages (default: 20) |
| `direction` | string | No | "sent", "received", or "both" (default: both) |

---

## Long-Running Tasks

### long_task_start

Start a task with goal and completion criteria.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `task_id` | string | Yes | Unique identifier |
| `goal` | string | Yes | What to achieve |
| `realm` | string | No | Project scope |
| `hard_checks` | string[] | No | Deterministic criteria |
| `soft_checks` | string[] | No | Semantic criteria |
| `work_items` | string[] | No | Initial subtasks |

### long_task_get

Get task by ID.

### long_task_active

Get the active task for a realm.

### long_task_update

Update task progress.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `task_id` | string | Yes | Task ID |
| `completed_summary` | string | No | What's done |
| `work_items` | string[] | No | Updated subtasks |
| `blockers` | string[] | No | Current blockers |

### long_task_complete

Mark task as completed.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `task_id` | string | Yes | Task ID |
| `outcome` | string | Yes | Final outcome |

### long_task_event

Append event to task log.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `task_id` | string | Yes | Task ID |
| `kind` | string | Yes | tool_result, decision, observation, error, checkpoint |
| `payload` | string | No | Event data (JSON) |
| `tags` | string[] | No | Tags |
| `related_entities` | string[] | No | Related files/functions |

### long_task_snapshot

Get synthesized task context for injection.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `task_id` | string | Yes | Task ID |
| `mode` | string | No | inject (compact) or debug (verbose) |

### long_task_evaluate

Evaluate task completion criteria.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `task_id` | string | Yes | Task ID |

---

## Theme System (xMemory)

### theme_list

List all themes with statistics.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `realm` | string | No | Filter by realm |
| `limit` | integer | No | Max themes (default: 50) |

### theme_get

Get theme details including representatives.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | integer | Yes | Theme ID |

### theme_recall

Two-stage theme-based retrieval: diverse representatives then adaptive expansion.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `query` | string | Yes | Search query |
| `limit` | integer | No | Max results (default: 10) |
| `realm` | string | No | Filter by realm |

### theme_stats

Get theme organization statistics.

### theme_maintain

Force theme maintenance: split oversized, merge similar, reassign.

### theme_assign_orphans

Assign orphan memories to themes in batches.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `batch_size` | integer | No | Memories per batch (default: 100) |
| `realm` | string | No | Filter by realm |

---

## Suggestions and Feedback

### suggestion_track

Track a suggestion for later outcome evaluation.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `content` | string | Yes | What was suggested |
| `context` | string | No | Why/when suggested |
| `realm` | string | No | Project scope |

### suggestion_pending

List suggestions awaiting feedback.

### suggestion_resolve

Record outcome of a suggestion.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | integer | Yes | Suggestion ID |
| `helped` | boolean | Yes | Did it help? |
| `details` | string | No | What happened |

### suggestion_count

Count pending suggestions.

### learn_outcome

Record whether a surfaced memory helped. Adjusts confidence.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `memory_id` | string | Yes | Memory ID or UUID |
| `outcome` | string | Yes | positive, negative, or neutral |
| `context` | string | No | What you were doing |

### episode_cluster_status

Find clusters of similar episodes that could be distilled into wisdom.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `similarity_threshold` | number | No | Min similarity (default: 0.85) |
| `min_occurrences` | integer | No | Min episodes (default: 3) |

---

## Consolidation

### consolidation_scan

Find similar memory pairs that could be merged.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `similarity_threshold` | number | No | Min similarity (default: 0.85) |
| `limit` | integer | No | Max candidates (default: 50) |
| `realm` | string | No | Filter by realm |

### consolidation_merge

Merge two memories (primary absorbs secondary).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `primary_id` | integer | Yes | Primary memory (kept) |
| `secondary_id` | integer | Yes | Secondary memory (absorbed) |
| `merged_content` | string | No | Combined content |

### consolidation_auto

Auto-merge highly similar memories (>90% similarity).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `similarity_threshold` | number | No | Min similarity (default: 0.90) |
| `max_merges` | integer | No | Max merges (default: 20) |

---

## Metacognition

### metacognition_corrections

Analyze patterns in past corrections. Finds recurring mistakes.

### metacognition_outcomes

Analyze suggestion outcomes. Finds success patterns.

### metacognition_evaluate

Self-evaluate learning effectiveness. Returns metrics and recommendations.

---

## Curiosity and Gaps

### curiosity_note_gap

Record a knowledge gap.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `gap` | string | Yes | What I don't know |
| `context` | string | No | When/why it came up |
| `realm` | string | No | Project scope |

### curiosity_gaps

List current knowledge gaps.

### curiosity_resolve

Mark a gap as explored/resolved.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | integer | Yes | Gap ID |
| `learned` | string | No | What was learned |

---

## Anticipation and Habits

### anticipation_observe

Record a context→action pattern.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `context` | string | Yes | Trigger situation |
| `action` | string | Yes | Response taken |
| `realm` | string | No | Project scope |

### anticipation_predict

Predict likely actions for a given context.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `context` | string | Yes | Current situation |
| `limit` | integer | No | Max predictions (default: 5) |
| `realm` | string | No | Filter by realm |

### anticipation_success

Mark a predicted action as successful — the prediction was correct and helpful. Increases the pattern's success count.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | integer | Yes | Pattern ID to mark successful |

### anticipation_list

List learned anticipation patterns, ordered by frequency.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `realm` | string | No | Filter by realm |
| `limit` | integer | No | Max patterns (default: 50) |

### habit_observe

Record a trigger→response pattern. Strengthens on each observation.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `trigger` | string | Yes | What triggers the habit |
| `response` | string | Yes | What should happen |
| `realm` | string | No | Project scope |

### habit_match

Find habits matching the current context.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `context` | string | Yes | Context to match |
| `min_strength` | number | No | Min strength 0-1 (default: 0.3) |
| `realm` | string | No | Filter by realm |

### habit_strengthen

Manually strengthen a habit. Use when a habit proves useful.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | integer | Yes | Habit ID |
| `amount` | number | No | Amount to strengthen (default: 0.1) |

### habit_weaken

Weaken a habit (negative feedback). Use when a habit is no longer relevant.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | integer | Yes | Habit ID |
| `amount` | number | No | Amount to weaken (default: 0.05) |

### habit_list

List formed habits, ordered by strength.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `realm` | string | No | Filter by realm |
| `min_strength` | number | No | Minimum strength (default: 0) |
| `limit` | integer | No | Max habits (default: 50) |

---

## Narrative (Work Modes)

The narrative engine tracks session work modes (exploring, building, debugging, reviewing) by observing tool usage and file patterns. The prompt hook uses `narrative_status` to surface the current mode, and anticipation uses it to filter predictions.

### narrative_status

Get current work mode, confidence, and segment summary for the session.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | No | Session ID (default: current from environment) |

**Returns:** Current mode (exploring, building, debugging, reviewing, etc.), confidence score, event count, tools used, and active files.

### narrative_log

Manually append an event to the session event log. The narrative engine evaluates each event and may trigger a mode transition.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | Yes | Session ID |
| `kind` | string | Yes | Event kind: user_message, assistant_message, tool_use, tool_result, error, file_edit, search, build, test, commit, mode_change |
| `summary` | string | Yes | Brief description of the event |
| `tool_name` | string | No | Tool name (for tool events) |
| `success` | boolean | No | Whether the action succeeded (default: true) |
| `payload` | string | No | JSON payload with event details |
| `files_mentioned` | string | No | JSON array of file paths |

### narrative_history

Get history of work mode segments for a session. Each segment represents a period where the session was in a particular mode.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | Yes | Session ID |
| `limit` | integer | No | Max segments to return (default: 20) |

---

## Transcripts and Distillation

### transcript_register

Register a transcript file for distillation tracking.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | Yes | Claude session ID |
| `transcript_path` | string | Yes | Path to .jsonl file |
| `realm` | string | No | Realm isolation |

### transcript_get / transcript_list / transcript_update / transcript_remove

CRUD operations on transcript tracking state.

### transcript_parse

Parse new turns from a transcript JSONL file.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | Yes | Session ID |
| `min_turns` | integer | No | Min turns to return (default: 4) |

### transcript_search

Semantic search across transcript content.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `query` | string | Yes | Search query |
| `session_id` | string | No | Session filter (default: current, '*' for all) |
| `limit` | integer | No | Max results (default: 10) |
| `min_similarity` | number | No | Min cosine similarity (default: 0.3) |
| `keyword_only` | boolean | No | Fast keyword match (default: false) |

### distill_status

Get distillation system status.

### get_turns

Get conversation turns for a session. Returns lossless history of all user and assistant messages.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | No | Session ID (default: current session) |
| `start_index` | integer | No | Starting turn index (default: 0) |
| `limit` | integer | No | Max turns to return (default: 50) |

### create_episode

Create a dialogue episode for tracking conversation segments. Links to turn ranges for hierarchical retrieval.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | string | Yes | Session ID |
| `title` | string | Yes | Episode title/topic |
| `start_turn` | integer | Yes | Starting turn index |
| `end_turn` | integer | No | Ending turn index (0 = ongoing) |
| `episode_type` | string | No | Type: distillation, task, discussion |
| `realm` | string | No | Realm (default: brahman) |

### expand_memory

Expand a memory to its full hierarchical context: SSL memory → episode → full conversation turns.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | Yes | Memory ID (numeric row ID from memory table) |
| `depth` | integer | No | 1=memory only, 2=+episode, 3=+full turns (default: 3) |

**Returns:** Three levels of context:
- Level 1: The SSL memory content
- Level 2: Linked episode with session ID and turn range
- Level 3: Full conversation turns (user + assistant)

**Usage pattern:**
1. `recall --query "..."` returns compact SSL memories
2. If context unclear, `expand_memory --id <id> --depth 3` retrieves full dialogue
3. Memories link to episodes via triplet: `memory --derived_from--> episode`

---

## User Profile

### profile_get

Get user profile: expertise, communication style, work patterns, preferences.

### profile_update

Update a profile field.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `field` | string | Yes | expertise_json, style_json, patterns_json, or preferences_json |
| `value` | string | Yes | JSON value |

### profile_observe

Record an observation about the user.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `observation_type` | string | Yes | expertise, style, pattern, preference |
| `value` | string | Yes | For expertise: 'domain:level'. Others: JSON |

---

## Goals

### goal_set

Define a long-term goal.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `title` | string | Yes | Short name |
| `description` | string | No | Details |
| `milestones` | string | No | JSON: [{"name":"v1","done":false}] |
| `deadline` | integer | No | Unix timestamp |
| `realm` | string | No | Project scope |

### goal_get / goal_list

Query goals. `goal_list` supports status filter (active, paused, completed, abandoned).

### goal_progress

Update progress and optionally mark milestones complete.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | integer | Yes | Goal ID |
| `progress` | number | Yes | Progress 0-1 |
| `milestone` | string | No | Milestone to mark complete |

### goal_complete

Mark goal as completed.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | integer | Yes | Goal ID |
| `outcome` | string | Yes | Achievement summary |

---

## Calibration

### calibration_record

Record a prediction outcome for accuracy tracking.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `domain` | string | Yes | Domain (code, architecture, debugging) |
| `success` | boolean | Yes | Was prediction correct? |

### calibration_score

Get accuracy score for a domain or all domains.

---

## Hygiene

### hygiene_stats

Get memory hygiene statistics: confidence distribution, growth rate, stale memories.

### hygiene_run

Run hygiene: decay, prune low-confidence old memories, consolidate similar.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `prune_threshold` | number | No | Confidence to prune below (default: 0.1) |
| `min_age_days` | number | No | Min age for pruning (default: 7) |
| `consolidation_threshold` | number | No | Similarity for consolidation (default: 0.85) |
| `max_consolidations` | integer | No | Max per run (default: 10) |

---

## Cross-Project Insights

### insight_promote

Promote a memory to global visibility (applies across all projects).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | integer | Yes | Memory ID |
| `reason` | string | No | Why it's cross-project |

### insight_global

List all global insights.

---

## Background Processing

### background_schedule

Schedule a background task.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `task_type` | string | Yes | consolidation, decay, pruning, pattern_extraction |
| `realm` | string | No | Project scope |

### background_status

Get background processing status.

### background_run_cycle

Run one cycle of background processing.

---

## State and Maintenance

### soul_context

Get current soul state: node count, confidence, triplets, symbols, status.

### subconscious_stats

Get background processor statistics.

### health_check

Check daemon health and readiness.

### version_check

Get version information.

### cycle

Run maintenance cycle (decay, cleanup).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `force` | boolean | No | Force full cycle |

### cleanup

Remove garbage nodes.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `dry_run` | boolean | No | Preview only |

### reembed_memories

Re-embed memories with proper embeddings (fix zero-embedding memories).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `kind` | string | No | Filter by kind |
| `min_confidence` | number | No | Min confidence (default: 0) |
| `limit` | integer | No | Max to process (default: 100) |
| `dry_run` | boolean | No | Preview (default: false) |

### migrate_vss

Migrate embeddings from main DB to separate VSS database.

---

## Import and Export

### import_soul

Import .soul file (SSL format).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `file` | string | No | Path to .soul file |
| `content` | string | No | SSL content directly |

### export_soul

Export memories to SSL format.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `file` | string | No | Output file path |
| `tag` | string | No | Filter by tag |
| `limit` | integer | No | Max nodes |

### ssl_convert

Convert raw text to SSL (Soul Semantic Language) format.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `content` | string | Yes | Raw text |
| `domain` | string | No | Domain tag |
| `location` | string | No | Location reference (@file:line) |

---

## Epiplexity

### epiplexity_check

Compute epiplexity (ε) score measuring reconstruction quality from compressed seeds.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `original` | string | Yes | Original full text |
| `seed` | string | Yes | Compressed SSL seed |
| `reconstructed` | string | Yes | Text reconstructed from seed |

**Score**: ε = (S · K · D · C)^0.25 where S=semantic fidelity, K=entity preservation, D=information density, C=compression utility.

---

## SQL

### sql_query

Execute a read-only SQL query against the soul database (debugging/analysis).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `query` | string | Yes | SQL query (SELECT only) |
| `limit` | integer | No | Max rows (default: 100) |

---

## Learning Tools (MCP)

These tools are exposed via the `chitta-mcp` Python MCP server (not the C++ daemon directly):

### learn_correction

Store a correction when wrong. Creates high-confidence counter-memory.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `wrong` | string | Yes | What was incorrect |
| `correct` | string | Yes | The correct information |
| `context` | string | No | Where this applies |

### learn_preference

Store a user preference (global visibility).

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `category` | string | No | communication, detail, autonomy, style, workflow |
| `preference` | string | Yes | The preference |
| `example` | string | No | Demonstration |

### learn_insight

Store a generalizable cross-project insight.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `domain` | string | No | programming, debugging, architecture, etc. |
| `insight` | string | Yes | The insight |
| `learned_from` | string | No | Context |

### learn_approach

Store what approach works in a particular state.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `state` | string | Yes | stuck, debugging, exploring, flowing, etc. |
| `approach` | string | Yes | What helped |
| `outcome` | string | No | What happened |

### learn_milestone

Record a relationship milestone.

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `milestone` | string | Yes | What happened |
| `significance` | string | No | Why it matters |

### research_topics / research_store / research_cycle

Curiosity-driven research tools for finding topics to investigate, storing findings, and running research cycles.

---

*100+ tools. One soul.*
