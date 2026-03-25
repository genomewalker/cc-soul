# Soul, Memory, and chitta-field Solutions

This document turns the evaluation into an implementation-focused set of solutions for improving `cc-soul` and `chitta-field` as a memory and learning system supporting Claude.

Scope:

- Fix the most important architectural weaknesses.
- Improve reliability of memory capture and retrieval.
- Make the learning story match the actual implementation.
- Prioritize practical changes over conceptual expansion.

## Executive Summary

The right path is not to replace the current architecture. The base shape is good:

- transparent prompt-time retrieval,
- asynchronous post-turn learning,
- persistent storage in `chitta-field`,
- and background consolidation.

The work now is to make that shape reliable, less heuristic, and more honestly adaptive.

Top priorities:

1. Make memory writes robust when embeddings are unavailable.
2. Stop silently dropping learning events.
3. Replace regex-only learning triggers with structured extraction.
4. Put actual learners on the live retrieval path.
5. Close the gap between the documented architecture and the implemented backend.

## Priority 1: Make Memory Writes Embedding-Optional

### Problem

Right now, memory formation can fail if embeddings are unavailable, even though the system presents itself as a persistent learning system rather than an embedder-dependent cache.

### Solution

Allow memory writes without a full embedding.

Implementation direction:

- Accept zero-vector or missing embeddings in `chitta-field` write paths.
- Store the payload, metadata, and state immediately.
- Mark the memory as `unembedded` or `encoding_pending`.
- Let background consolidation encode it later when the embedder is available.

Recommended changes:

- In `chitta-field`, relax the strict `embedding.len() == 768` requirement for `put_memory`.
- Add an explicit state flag for memories that need embedding.
- Exclude unembedded memories from semantic retrieval until encoded.
- Still index them for keyword recall and triplet-based recall.

Expected effect:

- Learning no longer stops when ONNX or model loading fails.
- The system degrades gracefully instead of failing at the storage boundary.

## Priority 2: Make Write Failures Visible and Recoverable

### Problem

The queue processor currently catches exceptions and can effectively drop writes quietly.

### Solution

Turn the queue into a durable ingestion pipeline with retries and dead-letter handling.

Implementation direction:

- Separate `accepted`, `processed`, `failed`, and `retried` states.
- Write failed queue items to a dead-letter JSONL file.
- Expose queue health through a CLI or RPC tool.
- Emit visible daemon warnings even when not in verbose mode for memory-write failures.

Recommended changes:

- Add `.failed_queue.jsonl` and `.retry_queue.jsonl` under the mind path.
- For each failed queued operation, record:
  - tool
  - args
  - timestamp
  - error message
  - retry count
- Add a `queue_status` or `memory_ingestion_status` tool.

Expected effect:

- You stop losing learning events silently.
- Operational debugging becomes possible.
- Claude support quality becomes inspectable rather than assumed.

## Priority 3: Replace Regex-Only Learning With Structured Extraction

### Problem

The soul layer currently depends too much on regex detection and explicit markers like `[SOLUTION]` or `[USED:id]`.

### Solution

Keep heuristics as a fallback, but move primary learning extraction to structured turn analysis.

Implementation direction:

- Parse user and assistant turns into typed events.
- Use a small schema for learnable outcomes.
- Let regex remain as a cheap backup, not the primary path.

Suggested event schema:

```json
{
  "kind": "correction|preference|decision|solution|failure|milestone|approach",
  "source": "user|assistant|system",
  "confidence": 0.0,
  "evidence": ["text spans or turn ids"],
  "content": "normalized memory text",
  "realm": "project or shared scope"
}
```

Recommended changes:

- Add a structured learning extractor in the distillation path.
- Run it over complete turn pairs, not just assistant output.
- Store extracted evidence with the memory.
- Keep explicit markers as a high-confidence shortcut rather than a requirement.

Expected effect:

- Better learning recall quality.
- Less prompt brittleness.
- Fewer missed corrections and preferences.

## Priority 4: Put the Learning Primitives on the Retrieval Path

### Problem

`chitta-field` has route, plasticity, and context learners, but the live retrieval path is still dominated by heuristics and fixed routing logic.

### Solution

Use the existing learners to influence real retrieval choices.

Implementation direction:

- Call the route learner during `smart_recall` instead of mostly hard-coding retrieval strategy.
- Record retrieval episodes with route choice and downstream outcome.
- Feed explicit positive and negative outcomes back into the learner.
- Use context learner output to decide how much memory to surface into Claude.

Recommended changes:

- Replace heuristic-only route selection with:
  1. detect intent,
  2. ask route learner for route,
  3. execute route,
  4. record episode id,
  5. later attach reward.
- Connect `learn_outcome`, explicit correction signals, and successful memory usage to route rewards.
- Use recommended window size to tune prompt injection budgets dynamically.

Expected effect:

- Retrieval policy becomes genuinely adaptive.
- Memory surfacing becomes more session-aware.
- The “learning system” starts to mean more than decay adjustment.

## Priority 5: Make Retrieval Multi-Channel by Design

### Problem

The current system has several retrieval channels, but the logic is still fragmented between hooks, handlers, and ad hoc reranking.

### Solution

Define a single retrieval pipeline with explicit stages.

Suggested retrieval pipeline:

1. Intent detection
2. Candidate generation from multiple channels:
   - semantic
   - keyword
   - temporal
   - triplet/entity
   - artifact/code
   - association expansion
3. Score normalization
4. Learned reranking
5. context budget selection
6. expansion of only top results
7. exposure logging
8. outcome logging

Recommended changes:

- Make `smart_recall` the only first-class retrieval entry point.
- Relegate `recall`, `recall_keyword`, and `recall_temporal` to primitive tools.
- Standardize result objects so every channel returns the same shape.
- Add provenance fields for why a memory was surfaced.

Expected effect:

- Easier debugging.
- Better evaluation.
- Stronger retrieval coherence for Claude.

## Priority 6: Strengthen the Memory Model

### Problem

The memory model stores useful content, but it is still too weakly typed to support better downstream behavior.

### Solution

Add stronger first-class structure to memories.

Recommended additions:

- `kind`: keep existing semantic type
- `status`: active, superseded, contradicted, archived
- `source_type`: hook, distillation, user, system, dream, sadhana
- `evidence_refs`: transcript turns, files, URLs, symbols
- `applicability`: global, realm, session, tool, file
- `trust_score`: separate from confidence
- `resolution_links`: what corrected or replaced this memory

Critical behavior change:

- Do not treat corrections as just more memories.
- Add first-class supersession and contradiction handling.

Expected effect:

- Fewer contradictory memories resurfacing together.
- Better preference and correction handling.
- More trustworthy support for Claude.

## Priority 7: Improve Correction Handling

### Problem

Corrections are important but currently captured with shallow heuristics and weak downstream resolution logic.

### Solution

Make correction memory a first-class control loop.

Implementation direction:

- Detect correction events structurally.
- Link incorrect memory or action to the correction.
- Mark the losing memory as superseded or contradicted.
- Increase retrieval weight for corrective memories in matching contexts.

Recommended changes:

- Add `corrects(memory_id)` or `supersedes(memory_id)` as a first-class relation, not just an optional triplet convention.
- Penalize resurfacing of corrected memories when the correction has higher trust.
- Track whether corrected behavior still recurs.

Expected effect:

- Better “don’t repeat mistakes” behavior.
- Corrections become operational, not merely archival.

## Priority 8: Rework Theme and Narrative Features Honestly

### Problem

Themes and some higher-order features are currently present in interface form but not fully implemented in backend behavior.

### Solution

Either implement them properly or shrink the interface until they are real.

Recommended path:

- Short term: mark theme and resonance features clearly as limited or experimental.
- Medium term: implement true theme clustering with:
  - centroid maintenance,
  - orphan assignment,
  - theme-to-memory membership,
  - theme-aware retrieval,
  - and theme decay/merge logic.

Alternative if time is limited:

- Remove theme tools from the main cognitive story and treat them as diagnostics until complete.

Expected effect:

- Less architecture drift.
- Better trust in the interface.
- Reduced maintenance cost from pseudo-features.

## Priority 9: Make Distillation More Central

### Problem

The strongest long-term path in this system is not regex hooks. It is transcript and turn distillation.

### Solution

Promote distillation to the primary memory-formation mechanism.

Implementation direction:

- Treat hook capture as raw observation.
- Treat distillation as the step that produces stable long-term memory.
- Separate:
  - raw turn storage,
  - extracted events,
  - durable memories,
  - and synthesized higher-order knowledge.

Recommended architecture:

- Raw transcript/turns: always stored.
- Event extraction: lightweight and near-real-time.
- Distillation: periodic and structured.
- Consolidation: merge, dedup, resolve contradictions, promote wisdom.

Expected effect:

- Higher quality memory.
- Less duplication.
- Cleaner separation between observation and knowledge.

## Priority 10: Tighten Documentation to the Real System

### Problem

Contributor docs still describe a different storage architecture than the live code.

### Solution

Bring documentation into exact alignment with the current backend.

Recommended changes:

- Rewrite contributor architecture docs around `chitta-field`.
- Remove or clearly mark legacy DuckDB references.
- Add a single authoritative architecture diagram.
- Distinguish clearly between:
  - implemented,
  - experimental,
  - and planned features.

Expected effect:

- Easier maintenance.
- Fewer incorrect assumptions.
- Better contributor onboarding.

## Suggested Implementation Plan

### Phase 1: Reliability

Focus:

- embedding-optional writes,
- queue durability,
- visible failures,
- docs alignment.

Goal:

- ensure the soul never silently stops learning.

### Phase 2: Better Memory Formation

Focus:

- structured event extraction,
- stronger correction model,
- richer memory typing,
- distillation-first learning.

Goal:

- improve memory quality and trustworthiness.

### Phase 3: Real Adaptive Retrieval

Focus:

- live route learner integration,
- window learner integration,
- multi-channel retrieval pipeline,
- measured reward feedback.

Goal:

- make retrieval policy genuinely adaptive.

### Phase 4: Higher-Order Organization

Focus:

- theme engine,
- contradiction resolution,
- consolidation and promotion,
- memory provenance and trust.

Goal:

- turn a useful memory layer into a more coherent cognitive substrate.

## Proposed Success Criteria

The system is meaningfully improved when the following become true:

- Memory writes succeed even when embeddings are temporarily unavailable.
- No queued learning event is silently lost.
- Corrections suppress the resurfacing of the thing they corrected.
- Retrieval route selection adapts based on explicit outcomes.
- Prompt injection size adapts by session type and measured usefulness.
- Distillation produces most durable long-term memory.
- Documented architecture matches implemented architecture.

## Bottom Line

The right solution is not a rewrite.

The right solution is to harden the current layered design:

- make storage graceful under failure,
- make learning structured rather than regex-led,
- make adaptive components actually influence retrieval,
- and remove the gap between what the system claims and what it currently does.

If that is done well, `cc-soul` can become a genuinely strong memory support system for Claude, and `chitta-field` can serve as a credible long-term substrate rather than just an interesting backend experiment.
