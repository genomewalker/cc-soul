# Soul, Memory, and chitta-field Evaluation

This document captures a design and code-logic evaluation of the `cc-soul` soul system, the memory system it exposes to Claude, and the standalone `chitta-field` repository that acts as the storage substrate.

Scope:

- Evaluate the approach as a memory and learning system supporting Claude.
- Focus on implementation logic rather than benchmarks or broad product positioning.
- Cover both `cc-soul` and `/maps/projects/fernandezguerra/apps/repos/chitta-field`.

Note:

- This is based on code inspection.
- I did not continue with test execution for this pass.

## Executive Summary

The overall architecture is directionally strong as an external memory support system for Claude:

- `cc-soul` provides transparent retrieval, asynchronous capture, and background consolidation.
- `chitta-field` provides a credible persistent substrate with append-only durability, in-memory retrieval structures, and memory lifecycle controls.
- The system is strongest as a hybrid of workflow instrumentation plus long-term memory retrieval.

The main weakness is that the current implementation does not yet justify the stronger claims around adaptive learning or self-organizing cognition:

- Much of the practical learning is still heuristic, regex-driven, or marker-driven.
- Some of the advertised cognitive layers are stubbed or unavailable on the `chitta-field` backend.
- There is a hard operational dependency on embeddings in places where the broader system presentation implies more graceful degradation.

## Main Findings

### 1. Memory formation currently depends on embeddings more than the system presentation suggests

In `cc-soul`, queued writes use `embed_text()` and return an empty vector if the embedder is missing or fails:

- [simple_cli.cpp](/maps/projects/fernandezguerra/apps/repos/cc-soul/chitta/src/simple_cli.cpp#L473)

Those writes are passed through the field FFI:

- [ffi.rs](/maps/projects/fernandezguerra/apps/repos/chitta-field/src/ffi.rs#L145)

But `chitta-field` rejects a memory write unless the embedding length is exactly `768`:

- [store.rs](/maps/projects/fernandezguerra/apps/repos/chitta-field/src/store.rs#L42)

The queue processor catches exceptions and only logs them when verbose mode is enabled:

- [simple_cli.cpp](/maps/projects/fernandezguerra/apps/repos/cc-soul/chitta/src/simple_cli.cpp#L762)

Why this matters:

- If embeddings are unavailable, learning can silently degrade or stop.
- That is a serious weakness for a system intended to support Claude continuously and transparently.

## 2. The implemented learning is narrower than the architecture language implies

Prompt-time retrieval goes through hook-triggered `smart_recall`:

- [prompt-hook.sh](/maps/projects/fernandezguerra/apps/repos/cc-soul/hooks/prompt-hook.sh#L95)

That in turn relies on heuristic query expansion and hybrid reranking logic:

- [field_memory_recall.hpp](/maps/projects/fernandezguerra/apps/repos/cc-soul/chitta/include/chitta/rpc/handlers/field_memory_recall.hpp#L230)

`chitta-field` does define learning components:

- Route learner: [route.rs](/maps/projects/fernandezguerra/apps/repos/chitta-field/src/learner/route.rs#L23)
- Plasticity learner: [plasticity.rs](/maps/projects/fernandezguerra/apps/repos/chitta-field/src/learner/plasticity.rs#L12)
- Context learner: [context.rs](/maps/projects/fernandezguerra/apps/repos/chitta-field/src/learner/context.rs#L4)

But the substrate-level feedback path only updates route selection:

- [store.rs](/maps/projects/fernandezguerra/apps/repos/chitta-field/src/store.rs#L855)

Assessment:

- The current system does learn salience, decay, and some retrieval feedback.
- It does not yet look like a deeply adaptive memory policy engine.
- In practice, this is better described as adaptive storage and retrieval shaping than as robust lifelong learning.

## 3. The soul layer is useful, but it is fundamentally heuristic instrumentation around Claude

The prompt hook detects corrections, preferences, emotional state, and milestones using regex-based patterns:

- [prompt-hook.sh](/maps/projects/fernandezguerra/apps/repos/cc-soul/hooks/prompt-hook.sh#L204)

The stop hook extracts typed learnings by looking for explicit assistant markers such as `[SOLUTION]`, `[GOTCHA]`, `[PREFERENCE]`, and `[LEARN]`:

- [stop-hook.sh](/maps/projects/fernandezguerra/apps/repos/cc-soul/hooks/stop-hook.sh#L166)

It also records explicit memory usefulness via `[USED:id]` markers:

- [stop-hook.sh](/maps/projects/fernandezguerra/apps/repos/cc-soul/hooks/stop-hook.sh#L200)

And when Claude misses a correction-learning opportunity, it falls back to regex detection and auto-stores a correction:

- [stop-hook.sh](/maps/projects/fernandezguerra/apps/repos/cc-soul/hooks/stop-hook.sh#L322)

Assessment:

- This is a practical design for making Claude improve within the workflow.
- But it is fragile because it depends on explicit formatting and shallow linguistic cues.
- The soul system is currently better understood as a structured instrumentation layer than as a robust semantic learner.

## 4. The field backend is incomplete relative to the broader cognitive story

The backend explicitly reports that the resonance learner is not available:

- [field_system.hpp](/maps/projects/fernandezguerra/apps/repos/cc-soul/chitta/include/chitta/rpc/handlers/field_system.hpp#L173)

Theme tools are explicitly marked as stubs:

- [field_system.hpp](/maps/projects/fernandezguerra/apps/repos/cc-soul/chitta/include/chitta/rpc/handlers/field_system.hpp#L503)

Tag removal is also stubbed:

- [field_memory_ops.hpp](/maps/projects/fernandezguerra/apps/repos/cc-soul/chitta/include/chitta/rpc/handlers/field_memory_ops.hpp#L234)

Assessment:

- The project is stronger as memory infrastructure than as a complete cognitive architecture.
- The current implementation does not yet fully support the more ambitious story around resonance, themes, or higher-order organization.

## 5. Documentation drift suggests the migration is still settling

Contributor-facing architecture docs still describe a DuckDB-centered stack:

- [CLAUDE.md](/maps/projects/fernandezguerra/apps/repos/cc-soul/CLAUDE.md#L42)

That is inconsistent with the current `chitta-field`-centered implementation.

Assessment:

- This increases contributor confusion.
- It also suggests the storage migration is real but not yet fully normalized across the project.

## What Is Strong

### Transparent support for Claude

The prompt hook retrieves memory before the assistant responds:

- [prompt-hook.sh](/maps/projects/fernandezguerra/apps/repos/cc-soul/hooks/prompt-hook.sh#L76)

That is the right direction for a support system: Claude should not need explicit memory-management rituals on every turn.

### Practical post-turn learning

The stop hook converts assistant output into typed memory candidates and feedback events:

- [stop-hook.sh](/maps/projects/fernandezguerra/apps/repos/cc-soul/hooks/stop-hook.sh#L193)

This is a reasonable design for capturing experience without blocking interaction.

### Credible substrate design in chitta-field

The storage substrate has real architectural coherence:

- Deduplicating write path: [store.rs](/maps/projects/fernandezguerra/apps/repos/chitta-field/src/store.rs#L49)
- Semantic recall scoring with strength/confidence weighting: [store.rs](/maps/projects/fernandezguerra/apps/repos/chitta-field/src/store.rs#L440)
- Demotion and forgetting lifecycle: [store.rs](/maps/projects/fernandezguerra/apps/repos/chitta-field/src/store.rs#L1130)

This is the most solid part of the stack.

## Overall Assessment

### Soul system

`cc-soul` is conceptually strongest when treated as:

- a transparent retrieval layer,
- a workflow-aware learning harness,
- and a background consolidation system around Claude.

It is not yet convincing as a full “soul” in the stronger cognitive sense implied by some of the project language. The practical logic is more concrete than the metaphors: hooks, queues, typed memory extraction, explicit feedback, and retrieval shaping.

### Memory system

As a memory system for Claude, the design is good in principle:

- turn-time retrieval,
- post-turn capture,
- memory strengthening,
- deduplication,
- decay,
- and domain scoping.

The main issue is not the overall shape. The issue is that robustness still depends heavily on embeddings, explicit markers, and heuristics.

### chitta-field

The standalone `chitta-field` repository is the strongest and most credible part of the architecture.

It works well as:

- a durable long-term memory substrate,
- a hybrid recall engine,
- and a lifecycle manager for memory salience and forgetting.

Its learning logic is real but limited:

- route selection via Thompson-style sampling,
- decay adaptation from access patterns,
- and working-memory window adjustment.

That is useful, but it is still far from a rich autonomous learning substrate in the stronger sense.

## Bottom Line

If the question is whether this is a good approach for supporting Claude with memory, the answer is yes.

If the question is whether it already functions as a mature learning architecture with deep adaptive behavior, the answer is no.

Best current description:

- `cc-soul` is a strong external memory support harness for Claude.
- `chitta-field` is a promising and logically coherent memory substrate.
- The “learning system” is currently narrower, more heuristic, and less integrated than the project’s more ambitious framing suggests.
