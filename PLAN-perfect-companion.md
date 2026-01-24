# Perfect Companion Implementation Plan

## Phase 1: Performance & Indexing (Quick Wins)

### 1.1 Pre-embed symbols during learn_codebase
**Files:** `chitta/include/chitta/rpc/duckdb_handler.hpp`, `chitta/src/duckdb_store.cpp`
- After storing symbols, embed them immediately if yantra available
- Add `get_unembedded_symbols_for_files()` method
- Batch embed during indexing (move cost to index time)

### 1.2 Code diff awareness
**Files:** `scripts/simple-hook.sh`, `chitta-mcp/server.py`
- On SessionStart, detect git changes since last session
- Surface: "Files changed: X, Y, Z" in context
- Store last commit hash in ledger

### 1.3 External embedding API option
**Files:** `chitta/include/chitta/vak.hpp`, new `chitta/include/chitta/vak_api.hpp`
- Add VakAPI class for Voyage/Jina embeddings
- Config option: `embedding_source: local|voyage|jina`
- Fallback to local if API unavailable

## Phase 2: Memory & Learning

### 2.1 Incremental distillation
**Files:** `scripts/simple-hook.sh`, `chitta-mcp/server.py`
- On Stop hook, extract learnings from current turn (not whole session)
- Detect [LEARN] patterns in assistant responses
- Store immediately, don't wait for full session distillation

### 2.2 Correction learning
**Files:** `chitta-mcp/server.py`, new tool `learn_correction`
- Detect patterns: "actually", "no,", "that's wrong", "instead"
- Store as high-confidence counter-memory
- Link to original incorrect statement via triplet

### 2.3 Proactive memory surfacing
**Files:** `scripts/simple-hook.sh`
- Analyze query for implicit needs (not just explicit keywords)
- Surface related memories even if not directly matching
- Add confidence threshold for proactive suggestions

## Phase 3: Session Continuity

### 3.1 Session momentum
**Files:** `chitta-mcp/server.py`, ledger system
- Auto-checkpoint every N turns (not just on /checkpoint)
- Store: current task, open questions, momentum state
- On resume: restore full context including "feel"

### 3.2 Cross-project learning
**Files:** `chitta/include/chitta/mind/duckdb_mind.hpp`
- Extract generalizable patterns (not project-specific)
- Tag with `visibility: global`
- Apply to new projects when relevant

## Phase 4: Relationship & Trust

### 4.1 Preference learning
**Files:** `chitta-mcp/server.py`
- Track: communication style, detail level, when to ask vs act
- Store as beliefs with high confidence
- Adapt responses based on learned preferences

### 4.2 Emotional memory
**Files:** new `chitta/include/chitta/affect.hpp`
- Track session "mood": flowing, stuck, frustrated, exploring
- Remember what helped in similar states
- Adjust approach based on emotional context

## Implementation Order

```
Day 1: 1.1 (pre-embed) + 1.2 (diff awareness) ✅ DONE
Day 2: 2.1 (incremental distill) SKIPPED (MCP direct) + 2.2 (correction learning) ✅ DONE
Day 3: 3.1 (session momentum) ✅ DONE + 4.1 (preferences) ✅ DONE
Day 4: 1.3 (external API) SKIPPED + 3.2 (cross-project) ✅ DONE
Day 5: 2.3 (proactive surfacing) ✅ DONE + 4.2 (emotional memory) ✅ DONE
```

**STATUS: COMPLETE** - All planned features implemented.

## Completed Features

1. **Pre-embed symbols** - Symbols embedded during learn_codebase
2. **Code diff awareness** - SessionStart shows git changes since last session
3. **Correction learning** - `learn_correction` MCP tool with triplets
4. **Session momentum** - Turn tracking, auto-checkpoint every 5 turns, blocker extraction
5. **Preference learning** - `learn_preference` MCP tool with global visibility
6. **[LEARN] removal** - Memories via MCP direct, not hook extraction
7. **Proactive memory surfacing** - Detect implicit needs (debug→corrections, planning→preferences, stuck→approaches)
8. **Cross-project learning** - `learn_insight` MCP tool for generalizable patterns (global visibility)
9. **Emotional memory** - `learn_approach` MCP tool for what helps in different states

## Success Metrics

- First query response: < 200ms (vs 2-5s before)
- Context relevance: > 80% useful (vs ~50% before)
- Session resume: feels like "picking up where we left off"
- Corrections remembered: 100% (never repeat same mistake)
- Proactive suggestions: helpful 70%+ of the time
