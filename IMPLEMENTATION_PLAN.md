# Implementation Plan

This plan converts the soul and memory evaluation into a compact execution roadmap.

## Goal

Strengthen `cc-soul` and `chitta-field` as a practical memory and learning system for Claude by improving:

- reliability,
- memory quality,
- adaptive retrieval,
- and architectural honesty.

## Priority Order

1. Reliability of memory writes
2. Visibility and recovery of failed learning events
3. Better memory formation
4. Real adaptive retrieval
5. Higher-order organization
6. Documentation alignment

## Phase 1: Reliability

### Objectives

- Ensure memory writes do not fail when embeddings are unavailable.
- Ensure queued operations are never silently lost.
- Make ingestion health observable.

### Tasks

- Allow `chitta-field` to accept missing or deferred embeddings.
- Add `encoding_pending` or equivalent state to stored memories.
- Keep keyword and graph indexing available even for unembedded memories.
- Add retry and dead-letter files for queue failures.
- Surface write failures in daemon logs even outside verbose mode.
- Add a `queue_status` or `memory_ingestion_status` tool.

### Exit Criteria

- Memory capture still works when ONNX/model loading fails.
- Failed queued writes are recoverable and inspectable.
- No silent loss of observations or learned events.

## Phase 2: Better Memory Formation

### Objectives

- Reduce dependence on regex and explicit output markers.
- Improve correction, preference, and solution capture quality.
- Make distillation the main path to durable memory.

### Tasks

- Introduce structured event extraction over turn pairs.
- Define a typed learning-event schema.
- Store evidence references with extracted memories.
- Keep regex logic as fallback only.
- Promote raw hooks to observation capture, not final knowledge capture.
- Move durable memory creation toward distillation and consolidation.

### Exit Criteria

- Most durable memories are produced by structured extraction.
- Corrections and preferences are captured without requiring explicit assistant markers.
- Memory quality improves and duplication drops.

## Phase 3: Adaptive Retrieval

### Objectives

- Put learning primitives on the live retrieval path.
- Make retrieval policy adapt from outcomes.
- Make context size responsive to session type and utility.

### Tasks

- Integrate route learner into `smart_recall`.
- Record retrieval episodes and chosen routes.
- Feed `learn_outcome`, correction signals, and usage signals back as rewards.
- Use context learner output to tune prompt injection budget.
- Standardize retrieval result structure across channels.
- Make `smart_recall` the main orchestration path.

### Exit Criteria

- Retrieval route selection changes based on measured outcomes.
- Context window recommendations affect live surfacing behavior.
- Retrieval is easier to evaluate and debug.

## Phase 4: Memory Semantics

### Objectives

- Make stored memories more operational.
- Improve contradiction handling.
- Prevent corrected memories from continuing to dominate recall.

### Tasks

- Add stronger memory fields: status, source_type, applicability, trust_score, evidence_refs.
- Add first-class correction/supersession relationships.
- Penalize resurfacing of superseded memories.
- Track recurrence of corrected failures.
- Separate confidence from trust and applicability.

### Exit Criteria

- Corrections actively suppress the things they corrected.
- Contradictory memories are easier to resolve.
- Preference and correction memories behave more predictably.

## Phase 5: Higher-Order Organization

### Objectives

- Either implement higher-order features properly or reduce their interface claims.
- Make themes and consolidation real if they remain first-class concepts.

### Tasks

- Decide whether themes remain core, experimental, or deferred.
- If core: implement centroid maintenance, assignment, retrieval, and merge logic.
- Improve consolidation and promotion rules for turning episodes into wisdom.
- Add provenance-aware promotion and contradiction checks.

### Exit Criteria

- Theme features are either real or clearly marked as limited.
- Consolidation improves memory quality rather than just adding volume.

## Phase 6: Documentation Alignment

### Objectives

- Make the documented architecture match the code.
- Remove confusion from the DuckDB-to-`chitta-field` transition.

### Tasks

- Rewrite contributor-facing architecture docs around `chitta-field`.
- Remove or clearly mark stale DuckDB references.
- Add one authoritative architecture diagram.
- Label features as implemented, experimental, or planned.

### Exit Criteria

- New contributors can understand the actual architecture from the docs.
- There is no major storage-architecture drift in the written documentation.

## Recommended Milestones

### Milestone 1

- Embedding-optional writes implemented
- Queue dead-letter path added
- Queue health visible

### Milestone 2

- Structured learning extractor added
- Distillation-first memory path in place
- Regex demoted to fallback role

### Milestone 3

- Route learner integrated into `smart_recall`
- Outcome feedback connected to retrieval policy
- Adaptive context sizing active

### Milestone 4

- Correction/supersession model implemented
- Contradiction handling improved
- Retrieval trust improves for corrected knowledge

### Milestone 5

- Theme/consolidation scope decided and reflected in code and docs
- Contributor docs fully aligned with implementation

## Success Metrics

The plan succeeds when these are true:

- Memory writes continue under degraded embedding conditions.
- No learning event is silently dropped.
- Durable memories are mostly created through structured extraction and distillation.
- Retrieval routes adapt from outcomes.
- Corrections reduce recurrence of past mistakes.
- Documentation matches the system that actually runs.

## First Three Concrete Moves

1. Make `chitta-field` accept stored memories without immediate embeddings.
2. Add dead-letter and retry handling to the queue processor.
3. Replace regex-first learning with structured extraction in the distillation pipeline.
