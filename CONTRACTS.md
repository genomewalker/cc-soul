# Operational Contracts

This document defines the observable guarantees of the cc-soul memory system.
These contracts are enforced at write time in the queue processor (`simple_cli.cpp`)
and must hold across daemon restarts (WAL durability).

## 1. Source Trust Hierarchy

Every memory event carries a `source` field. The source determines what confidence
range is allowed and whether the memory is provisional or durable.

| Source           | Max Confidence | Min Confidence | Tier        | Decay Rate |
|-----------------|---------------|---------------|-------------|------------|
| `hook_regex`     | 0.70          | 0.00          | Provisional | 0.020      |
| `hook_compliance`| 0.90          | 0.50          | Provisional | 0.020      |
| `distillation`   | 1.00          | 0.75          | Durable     | 0.005      |
| `mcp_tool`       | 1.00          | 0.60          | Durable     | 0.005      |
| `system`         | 1.00          | 0.80          | Durable     | 0.005      |
| *(unknown)*      | 0.70          | 0.00          | Provisional | 0.020      |

**Enforcement:** Confidence is clamped to `[min, max]` at write time. Out-of-range
events are logged to `.failed_queue.jsonl`. `distillation` events below floor are
rejected entirely (they represent a bug in the distillation pipeline).

## 2. Durability Tiers

| Confidence    | Tier        | Semantics                                      |
|--------------|-------------|------------------------------------------------|
| < 0.75       | Provisional | May decay, may be superseded without ceremony  |
| 0.75–0.95   | Durable     | Survives restart, requires explicit supersession |
| > 0.95       | Authoritative | High-trust corrections, preferences, facts    |

Promotion path: hook event (provisional) → recurrence +0.05/occurrence → distillation
confirms → promoted to category confidence (0.75–0.95, durable).

## 3. Supersession Contract

A memory M2 supersedes M1 when:
1. M2 is stored with category `correction`
2. M1 has semantic similarity ≥ 0.85 to M2
3. M1 has kind ≠ `correction` (corrections don't supersede other corrections)

Effects:
- A `supersedes(M2, M1)` triplet is created
- M1 is weakened by 0.15 strength
- M1 status is set to `Superseded` (persisted in WAL)

Higher-confidence memories are NOT exempt from supersession. A low-confidence
hook-sourced correction CAN supersede a high-confidence distillation memory —
this is intentional: corrections are detected at interaction time, not distillation time.

## 4. Autonomous Agent Boundary

Autonomous agents (sadhana impl loop) are constrained to:
- `--allowedTools Bash,Edit,Write,Read,Glob,Grep` — no WebSearch, no Agent spawning
- `allow_deploy: false` by default — changes are saved as patches, not deployed
- `allow_deploy: true` requires explicit human opt-in in goal_dsl
- When deploying: only commits `chitta/` directory, not full repo

Memory produced by autonomous agents carries `source=mcp_tool` (via chitta CLI calls).
This makes them durable but limited to the `mcp_tool` confidence range.

## 5. Queue Processor Invariants

The queue processor enforces these invariants at write time:

- `confidence ∈ [policy.min, policy.max]` for the event's source
- `distillation` source with `confidence < 0.75` → rejected to dead-letter
- `hook_*` source with `confidence > 0.90` → clamped, logged as contract violation
- Failed items written to `{mind_path}/.failed_queue.jsonl` for inspection

Violations are observable via `queue_status` tool.

## 6. WAL Durability

- Every `put_memory` and `forget` operation is immediately synced (fdatasync)
- `set_memory_status` changes (Active/Superseded/Contradicted/Archived) are WAL-persisted
- Confidence changes via `update_confidence()` are WAL-persisted (via UpdateState op)
- `compact_wal` saves a full snapshot and deletes covered WAL segments

After `compact_wal`, startup replay only needs segments from the snapshot seqno forward.

## 7. What is NOT Guaranteed

- Multi-instance causal ordering: timestamp tie-breaker is wall-clock (clock skew risk)
- Retrieval completeness: recall is approximate (HNSW + BM25, not exhaustive)
- Deduplication completeness: exact-match dedup only; near-duplicate merge requires distillation
- Autonomous agent correctness: impl loop is supervised, not formally verified
