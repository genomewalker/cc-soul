# CC-Soul Robustness Redesign Plan

## Executive Summary

The current cc-soul architecture has multiple fragility points that cause silent failures, data loss, and unreliable cross-session messaging. This plan proposes a comprehensive redesign to make the system robust and self-healing.

---

## Current Fragility Points

### 1. Session Identity Crisis
- **Problem**: 4 fallback strategies for session detection, all can fail
- **Symptoms**: Wrong sender in messages, stale session registry, PID mismatch after resume
- **Root cause**: No single source of truth, global sidecar file races

### 2. Fire-and-Forget Hooks
- **Problem**: 17+ queue operations in stop-hook with no confirmation
- **Symptoms**: Lost learnings, preferences, corrections silently dropped
- **Root cause**: "Fail open" philosophy prioritizes latency over reliability

### 3. Socket Communication Unreliability
- **Problem**: Netcat to socket fails silently, CLI fallback inconsistent
- **Symptoms**: session_register not updating PID, socket timeouts
- **Root cause**: No proper RPC client in bash, timeout handling ad-hoc

### 4. Queue Processing Black Hole
- **Problem**: Queue file appended but no confirmation of processing
- **Symptoms**: Queue grows unbounded, observations lost if daemon dies
- **Root cause**: No acknowledgment mechanism, no persistence guarantees

### 5. MCP Server Session Confusion
- **Problem**: Caches session_id globally, serves multiple sessions
- **Symptoms**: msg_send uses wrong sender, cross-contamination
- **Root cause**: MCP designed for single-session, used in multi-session

### 6. Race Conditions
- **Problem**: Turn index, dedup file, session registry all racy
- **Symptoms**: Duplicate observations, colliding turn indices
- **Root cause**: Check-then-write patterns without atomic operations

---

## Redesign Principles

1. **Single Source of Truth**: One reliable way to identify session
2. **Fail-Loud for Critical Ops**: Learnings should not silently fail
3. **Self-Healing**: System should recover from failures automatically
4. **Idempotent Operations**: Safe to retry, no duplicates
5. **Proper Acknowledgment**: Know when data is safely stored

---

## Phase 1: Session Identity (High Priority)

### 1.1 Environment Variable Injection

**Goal**: Claude Code should expose session identity to child processes.

**Implementation**:
```bash
# session-start-hook.sh exports these for all child processes
export CLAUDE_SESSION_ID="$SESSION_ID"
export CLAUDE_TRANSCRIPT_PATH="$TRANSCRIPT_PATH"
export CLAUDE_REALM="$REALM"
export CLAUDE_PID="$PPID"
```

**Files to modify**:
- `hooks/session-start-hook.sh`: Export env vars
- `hooks/lib.sh`: Add `get_session_id()` that reads env first
- `chitta-mcp/server.py`: Check env before PPID lookup

### 1.2 Remove Sidecar File

**Goal**: Eliminate race-prone global file.

**Implementation**:
- Delete `~/.claude/mind/.current_session` usage
- Remove sidecar write from session-start-hook
- Remove sidecar read from server.py

**Files to modify**:
- `hooks/session-start-hook.sh`: Remove sidecar write
- `chitta-mcp/server.py`: Remove sidecar fallback (done in v3.38.8)

### 1.3 Self-Healing Session Registry

**Goal**: Daemon auto-discovers and corrects stale entries.

**Implementation**:
```cpp
// In subconscious cycle (every 60s)
void heal_session_registry() {
    // 1. Scan running Claude processes
    // 2. Match PIDs to registry entries
    // 3. Update stale entries
    // 4. Mark dead sessions
}
```

**Files to modify**:
- `chitta/src/duckdb_store.cpp`: Add `session_heal()` method
- `chitta/src/rpc_server.cpp`: Call in subconscious cycle

---

## Phase 2: Reliable Hook Communication (High Priority)

### 2.1 Replace Netcat with CLI

**Goal**: Consistent, reliable daemon communication.

**Implementation**:
```bash
# Before (unreliable)
timeout 1 echo "$request" | nc -U "$SOCKET_PATH" 2>/dev/null || true

# After (reliable)
chitta session_register --session_id "$SESSION_ID" --pid "$CLAUDE_PID" 2>/dev/null || true
```

**Files to modify**:
- `hooks/session-start-hook.sh`: Use CLI (done in v3.38.8)
- `hooks/prompt-hook.sh`: Convert socket calls to CLI
- `hooks/stop-hook.sh`: Convert socket calls to CLI

### 2.2 Critical Operation Confirmation

**Goal**: Know when important data is stored.

**Implementation**:
```bash
# For critical operations, check return value
if ! chitta observe --category correction --content "$CONTENT" 2>/dev/null; then
    # Log to fallback file for later retry
    echo "$CONTENT" >> "$HOME/.claude/mind/.failed_observations.jsonl"
fi
```

**Files to modify**:
- `hooks/stop-hook.sh`: Add confirmation for corrections, preferences
- `hooks/lib/queue.sh`: Add acknowledgment mode

### 2.3 Fallback Persistence

**Goal**: Never lose critical learnings.

**Implementation**:
```bash
# ~/.claude/mind/.failed_observations.jsonl - retry queue
# session-start-hook retries failed observations on startup
```

**Files to modify**:
- `hooks/stop-hook.sh`: Write to fallback on failure
- `hooks/session-start-hook.sh`: Retry failed observations

---

## Phase 3: Queue Reliability (Medium Priority)

### 3.1 Acknowledgment-Based Queue

**Goal**: Confirm queue items are processed.

**Current flow**:
```
Hook → append to queue → hope daemon processes
```

**New flow**:
```
Hook → append to queue → daemon processes → writes ack marker
Hook (on next run) → check ack → retry unacked items
```

**Implementation**:
- Add `ack_id` to queue items
- Daemon writes to `~/.claude/mind/.queue_acks`
- Hooks check acks on startup

**Files to modify**:
- `hooks/lib/queue.sh`: Add ack_id, check mechanism
- `chitta/src/duckdb_store.cpp`: Write acks after processing
- `hooks/session-start-hook.sh`: Retry unacked items

### 3.2 Queue Persistence

**Goal**: Queue survives daemon restart.

**Implementation**:
- Move queue from `/tmp/` to `~/.claude/mind/.queue.jsonl`
- Add sequence numbers for ordering
- Daemon processes in order, persists position

**Files to modify**:
- `hooks/lib/queue.sh`: New queue location
- `chitta/src/rpc_server.cpp`: Persist queue position

---

## Phase 4: MCP Server Per-Session Context (Medium Priority)

### 4.1 Session Context from Environment

**Goal**: MCP server knows its session without guessing.

**Implementation**:
```python
def get_session_from_env() -> Optional[str]:
    """Get session from environment (set by hooks)."""
    return os.environ.get("CLAUDE_SESSION_ID")
```

**Files to modify**:
- `chitta-mcp/server.py`: Add env check first

### 4.2 No Global Caching

**Goal**: Fresh session lookup for each messaging operation.

**Implementation**:
- Already done in v3.38.8 for MSG_TOOLS
- Extend to other session-sensitive operations

### 4.3 Session Context Validation

**Goal**: Detect session mismatches early.

**Implementation**:
```python
def validate_session_context():
    """Warn if session detection is ambiguous."""
    env_session = os.environ.get("CLAUDE_SESSION_ID")
    ppid_session = lookup_session_by_ppid()

    if env_session and ppid_session and env_session != ppid_session:
        logger.warning(f"Session mismatch: env={env_session} ppid={ppid_session}")
```

---

## Phase 5: Race Condition Fixes (Low Priority)

### 5.1 Atomic Turn Index

**Goal**: No turn index collisions.

**Implementation**:
```bash
# Use flock for turn index updates
turn_file="$MIND_PATH/.turn_index_$SESSION_ID"
{
    flock -x 200
    turn=$(cat "$turn_file" 2>/dev/null || echo 0)
    echo $((turn + 1)) > "$turn_file"
} 200>"$turn_file.lock"
```

### 5.2 Atomic Dedup

**Goal**: No duplicate observations from race.

**Implementation**:
- Move dedup to daemon (database-level UNIQUE constraint)
- Hook just sends, daemon deduplicates

**Files to modify**:
- `chitta/src/duckdb_store.cpp`: Add content hash dedup
- `hooks/stop-hook.sh`: Remove bash-level dedup

---

## Phase 6: Monitoring and Observability (Low Priority)

### 6.1 Health Metrics

**Goal**: Know when system is unhealthy.

**Implementation**:
```bash
# chitta health_check returns structured status
{
  "daemon": "healthy",
  "queue_depth": 0,
  "failed_observations": 0,
  "session_registry_stale": false,
  "last_subconscious_cycle": "2m ago"
}
```

### 6.2 Failure Logging

**Goal**: Know what failed and why.

**Implementation**:
- Add `~/.claude/mind/logs/failures.jsonl`
- Log all silent failures with context
- Health check surfaces recent failures

---

## Implementation Order

| Phase | Priority | Effort | Impact |
|-------|----------|--------|--------|
| 1.1 Env var injection | High | Low | High |
| 1.2 Remove sidecar | High | Low | Medium |
| 2.1 CLI over netcat | High | Medium | High |
| 2.2 Critical confirmation | High | Medium | High |
| 1.3 Self-healing registry | High | Medium | High |
| 2.3 Fallback persistence | Medium | Low | Medium |
| 3.1 Ack-based queue | Medium | High | Medium |
| 4.1-4.3 MCP context | Medium | Low | Medium |
| 5.1-5.2 Race fixes | Low | Medium | Low |
| 6.1-6.2 Monitoring | Low | Medium | Low |

---

## Success Criteria

1. **Session identity**: 100% correct sender in messages
2. **No silent failures**: All critical ops logged or retried
3. **Self-healing**: Stale registry entries auto-corrected within 60s
4. **Queue reliability**: 0 lost observations
5. **Testable**: Script to verify all robustness properties

---

## Testing Plan

```bash
# .scripts/test-robustness.sh
# 1. Start multiple sessions simultaneously - verify no races
# 2. Kill daemon mid-queue-processing - verify retry
# 3. Resume session with new PID - verify messaging works
# 4. Send cross-session messages - verify correct routing
# 5. Simulate hook failures - verify fallback persistence
```

---

## Files to Create/Modify

### New Files
- `.scripts/test-robustness.sh` - Robustness test suite
- `hooks/lib/retry.sh` - Retry logic for failed operations

### Modified Files (by phase)

**Phase 1**:
- `hooks/session-start-hook.sh`
- `hooks/lib.sh`
- `chitta-mcp/server.py`
- `chitta/src/duckdb_store.cpp`

**Phase 2**:
- `hooks/stop-hook.sh`
- `hooks/prompt-hook.sh`
- `hooks/lib/queue.sh`

**Phase 3**:
- `chitta/src/rpc_server.cpp`

**Phase 4**:
- `chitta-mcp/server.py`

**Phase 5**:
- `hooks/stop-hook.sh`
- `chitta/src/duckdb_store.cpp`
