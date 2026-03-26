# Daemon Policy Reference

All implicit behaviors, triggers, and conditions for the chittad daemon.
Source of truth for behavior that cannot be derived from code alone.

## Scheduled Background Passes

The subconscious thread runs continuously while the daemon is alive. Each pass
fires only when its interval has elapsed *and* its feature flag is enabled.

| Pass | Default Interval | Config Key | What It Does | Source File |
|------|-----------------|------------|--------------|-------------|
| Sleep consolidation | 10 min | `sleep_consolidation_interval` | `encode_all()` (cortical sparse codes) + `save_snapshot()` + `save_full_snapshot()` + lite-encoder training (once, when `memory_count >= 500`) | `subconscious.cpp:620` |
| Tier demotion | 60 min | `demotion_interval` | `run_demotion()` on chitta-field: demotes weak memories, hard-deletes below threshold | `subconscious.cpp:661` |
| Theme maintenance | 60 min | `theme_maintenance_interval` | `theme_maintain()` via FieldStore: splits/merges memory themes | `subconscious.cpp:594` |
| Auto-dream | 4 hr cooldown, idle > 10 min | `dream_callback_` | Curiosity-driven exploration via registered callback | `subconscious.cpp:142` |
| Auto-think | 1 hr cooldown, idle > 5 min | `think_callback_` | Internal memory synthesis via registered callback | `subconscious.cpp:152` |
| Background embedding | 30 s | `embedding_interval` | **Disabled by default** (`enable_background_embedding=false`). Kept for API compatibility. | `subconscious.hpp:69` |
| Event processing | 1 s | `process_interval` | Polls event queue, runs pattern detection on messages | `subconscious.cpp:108` |

### Idle Gating

Theme maintenance requires `is_idle() == true` (no queries for `idle_threshold` seconds, default 30s).
Sleep consolidation and demotion fire unconditionally on interval.
Dream requires idle > 10 min (600,000 ms). Think requires idle > 5 min (300,000 ms).

## Event-Driven Behaviors

| Event | Trigger | Side Effects |
|-------|---------|-------------|
| Session start | Hook: `SessionStart` | `subconscious.sh start` awakens daemon; `session-start-hook.sh` recalls context |
| User message | Hook: `UserPromptSubmit` | `prompt-hook.sh` runs pattern detection (correction, preference, frustration, milestone) and suggestion outcome checking |
| Bash tool pre-use | Hook: `PreToolUse` (matcher: `Bash`) | `pre-tool-hook.sh` checks command patterns |
| Bash tool post-use | Hook: `PostToolUse` (matcher: `Bash`) | `log-bash-history.sh` (async) logs command; `post-bash-hook.sh` checks result |
| Write tool post-use | Hook: `PostToolUse` (matcher: `Write`) | `memory-intercept.sh` (async) syncs memory |
| Session stop | Hook: `Stop` | `stop-hook.sh` preserves state |
| Context compaction | Hook: `PreCompact` | `pre-compact-hook.sh` consolidates memory before Claude Code compresses context |

## Source Trust Table

Defined in `daemon_config.hpp:24`. Enforced at write time by the queue processor.

| Source | Max Confidence | Min Confidence | Allow Durable | Decay Rate | Initial Status | Epistemic Status |
|--------|---------------|---------------|---------------|-----------|----------------|-----------------|
| `hook_regex` | 0.70 | 0.00 | false | 0.020 | Proposed (4) | ToolDerived (1) |
| `hook_compliance` | 0.90 | 0.50 | false | 0.020 | Proposed (4) | ToolDerived (1) |
| `distillation` | 1.00 | 0.75 | true | 0.005 | Active (0) | ModelInferred (2) |
| `mcp_tool` | 1.00 | 0.60 | true | 0.005 | Active (0) | UserStated (0) |
| `system` | 1.00 | 0.80 | true | 0.005 | Active (0) | ModelInferred (2) |
| *(unknown)* | 0.70 | 0.00 | false | 0.020 | Observed (5) | ToolDerived (1) |

**Decay rate rule:** durable sources (`allow_durable=true`) get 0.005; provisional sources get 0.02.

## Queue Processor Side Effects

The queue processor (`queue_processor.cpp`) reads a JSONL queue file and dispatches
each line by `tool` name. Beyond the primary write, several tools have hidden side effects:

| Tool | Primary Action | Side Effects |
|------|---------------|-------------|
| `observe` | `remember()` into chitta-field | 1. Sets epistemic status based on source. 2. Sets memory status (Proposed/Active) based on source. 3. Stores `source` and `evidence` as provenance triplets. 4. **Correction supersession**: if `category=correction`, finds semantically similar memories (cosine > 0.92, same realm, different kind) and marks them Superseded with -0.15 strength. 5. `force_supersede_ids` bypasses cosine threshold for explicit targets. |
| `learn_outcome` | Emits analytics event | Strengthens (+0.1) or weakens (-0.15) the referenced memory based on `outcome` field |
| `distill_trigger` | Runs distillation pipeline | Looks up transcript path from in-process registry or FieldStore events; spawns external distillation script |
| `strengthen` | `strengthen()` on memory | No side effects |
| `connect` | `add_triplet()` | No side effects |
| `curiosity_note_gap` | Stores `[curiosity]` episode | Stored with confidence 0.7, zero decay |
| `store_policy` | Stores `[policy:type]` wisdom | Stored with zero decay |

All failed items are written to `{mind_path}/.failed_queue.jsonl` as dead-letter entries.

## Correction Supersession

Two code paths perform correction supersession with different thresholds:

| Path | Threshold | Source |
|------|-----------|--------|
| Queue processor (hook events) | cosine > 0.92 | `queue_processor.cpp:224` |
| RPC `observe` tool (direct calls) | `semantic_score` > 0.88 | `field_memory_ops.hpp:136` |

Both paths enforce: same realm, target kind != `correction`, weaken by 0.15, set status to Superseded (1).
Explicit `target_id` or `force_supersede_ids` bypass the cosine threshold entirely.

## Compaction Safety

`compact_wal` refuses if live memory count < 100. This guards against data loss on
near-empty stores where a compaction bug could silently zero out the WAL.

## Pattern Detection (Subconscious)

The subconscious thread detects patterns in user/assistant messages using compiled regexes.
All detections store memories via `FieldStore::remember()` with embeddings.

| Pattern | Detected In | Kind Stored | Confidence | Decay |
|---------|------------|-------------|-----------|-------|
| Correction ("no", "wrong", "actually") | User messages | wisdom | 0.50-0.95 (quality-scored) | 0.005 |
| Preference ("I prefer", "always", "never") | User messages | belief | 0.80 | 0.000 |
| Frustration ("stuck", "not working") | User messages | episode | 0.80 | 0.030 |
| Milestone ("done", "shipped", "fixed") | User messages | wisdom | 0.80 | 0.005 |
| Uncertainty ("I don't know", "not sure") | Assistant messages | episode | 0.80 | 0.030 |
| Suggestion ("try", "consider", "you could") | Assistant messages | episode | 0.60 | 0.030 |
| Anticipation (context -> action) | Assistant messages | episode | 0.50 | 0.030 |
| Habit (tool sequence patterns) | Tool results | wisdom | 0.60 | 0.005 |

### Category-to-Confidence Map (Queue Processor)

When the queue processor receives an `observe` event without an explicit `confidence` field,
it uses the category to assign confidence:

| Category | Default Confidence |
|----------|--------------------|
| correction | 0.95 |
| preference | 0.90 |
| solution | 0.90 |
| milestone | 0.90 |
| decision | 0.85 |
| failure | 0.85 |
| gotcha | 0.85 |
| episode | 0.70 |
| *(other)* | 0.80 |

These are then clamped by the source trust policy.
