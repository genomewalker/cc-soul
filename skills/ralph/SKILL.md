---
name: ralph
aliases: [autonomous, loop, agentic-loop]
description: Memory-powered autonomous development loop. Continuously works on a project, learning from each iteration, persisting context across sessions, and intelligently detecting completion.
execution: direct
---

# Ralph: Memory-Powered Autonomous Development

An evolution of the [ralph-claude-code](https://github.com/frankbria/ralph-claude-code) technique with chitta memory integration.

## Quick Start

```bash
/ralph                    # Start autonomous loop on current project
/ralph status             # Show loop state and learned context
/ralph init [goal]        # Initialize new Ralph task
/ralph pause              # Pause loop (persists state to memory)
/ralph resume             # Resume from memory checkpoint
```

## How It Works

```
┌─────────────────────────────────────────────────────────────┐
│                     RALPH LOOP                              │
├─────────────────────────────────────────────────────────────┤
│  1. RECALL - Surface relevant memories                      │
│     • Previous decisions on this project                    │
│     • Patterns that worked/failed                           │
│     • Blockers and their solutions                          │
│                                                             │
│  2. EXECUTE - Work on highest priority task                 │
│     • Read fix_plan.md for priorities                       │
│     • Focus on ONE task per loop                            │
│     • Use subagents for parallel work                       │
│                                                             │
│  3. LEARN - Store learnings to memory                       │
│     • [SOLUTION] - What worked                              │
│     • [GOTCHA] - Traps discovered                           │
│     • [DECISION] - Why we chose this approach               │
│     • [FAILURE] - What didn't work                          │
│                                                             │
│  4. EVALUATE - Check completion                             │
│     • All fix_plan.md items done?                           │
│     • Tests passing?                                        │
│     • Stagnation detected?                                  │
│                                                             │
│  5. CHECKPOINT - Persist state before next loop             │
│     • Save progress to long_task                            │
│     • Update loop iteration count                           │
└─────────────────────────────────────────────────────────────┘
```

## Memory Integration (vs Original Ralph)

| Original Ralph | Ralph + Memory |
|---------------|----------------|
| Session files only | Persistent chitta memories |
| Forgets across restarts | Learns across sessions |
| No cross-project learning | Patterns apply everywhere |
| Heuristic exit detection | Memory-informed completion |
| Context in PROMPT.md only | Automatic context recall |

## Process

### Phase 1: Initialize (`/ralph init`)

1. **Get task goal** from user or detect from project
2. **Create long_task** with unique ID
3. **Recall project memories** - surface prior work on this codebase
4. **Generate fix_plan.md** if not exists
5. **Store initialization** to memory

### Phase 2: Loop Iteration (`/ralph` or auto-continue)

1. **Recall context**:
   ```
   recall --query "project:$PROJECT_NAME decisions solutions gotchas"
   long_task_snapshot --task_id $TASK_ID
   ```

2. **Check fix_plan.md** for highest priority incomplete task

3. **Execute task**:
   - ONE task per iteration
   - Use Task tool for parallel subagents if needed
   - Run tests after implementation

4. **Store learnings** (auto-extracted from response):
   - `[SOLUTION] cmake --build build --parallel` → stored
   - `[GOTCHA] realm_detect needs CHITTA_BIN set` → stored
   - `[DECISION] Using SSL format for memory` → stored

5. **Update state**:
   ```
   long_task_update --completed_summary "..." --work_items [...]
   long_task_event --event_type checkpoint
   ```

6. **Evaluate completion**:
   - Check fix_plan.md completion %
   - Check for stagnation patterns (same errors 3+ times)
   - Check explicit EXIT_SIGNAL

### Phase 3: Completion Detection

Exit loop when ANY:
- All fix_plan.md items marked `[x]`
- Explicit `EXIT_SIGNAL: true` in response
- Stagnation: same error 5+ consecutive loops
- Stagnation: 3+ test-only loops with no implementation
- Memory detects: "doing the same thing repeatedly"

### Phase 4: Completion (`/ralph complete`)

1. **Synthesize learnings** from all loop iterations
2. **Store project summary** to memory
3. **Archive long_task** as completed
4. **Report final stats**

## File Structure

```
project/
├── .ralph/
│   ├── fix_plan.md        # Prioritized TODO (markdown checkboxes)
│   ├── AGENT.md           # Build/run instructions
│   └── specs/             # Project specifications
├── src/                   # Implementation
└── tests/                 # Test suite
```

## fix_plan.md Format

```markdown
# Fix Plan

## Priority 1 - Critical
- [ ] Implement user authentication
- [x] Set up database schema

## Priority 2 - Important
- [ ] Add API rate limiting
- [ ] Write integration tests

## Priority 3 - Nice to have
- [ ] Add dark mode
```

## Status Block (Required at End of Each Loop)

```
---RALPH_STATUS---
STATUS: IN_PROGRESS | COMPLETE | BLOCKED
TASKS_COMPLETED_THIS_LOOP: <number>
FILES_MODIFIED: <number>
TESTS_STATUS: PASSING | FAILING | NOT_RUN
WORK_TYPE: IMPLEMENTATION | TESTING | DOCUMENTATION
EXIT_SIGNAL: false | true
RECOMMENDATION: <what to do next>
---END_RALPH_STATUS---
```

## MCP Tools Used

| Tool | When |
|------|------|
| `long_task_start` | Initialize new Ralph task |
| `long_task_active` | Get current task state |
| `long_task_snapshot` | Get synthesized context |
| `long_task_update` | Record progress |
| `long_task_event` | Log significant events |
| `long_task_complete` | Mark task done |
| `recall` | Surface relevant memories |
| `remember` | Store learnings (SSL format) |
| `learn_approach` | Store effective patterns |

## Example Session

```
User: /ralph init "Build a REST API for todo items"

Ralph: Starting autonomous development loop...

[Recall] Found 3 relevant memories:
  - [92%] REST APIs should use resource-based routes
  - [85%] Always add request validation middleware
  - [78%] Use OpenAPI spec for documentation

[Task] long_task_start: ralph-todo-api

[Fix Plan] Generated .ralph/fix_plan.md:
  - [ ] Create Express server with routes
  - [ ] Add CRUD endpoints for todos
  - [ ] Add validation middleware
  - [ ] Write tests
  - [ ] Add OpenAPI documentation

[Loop 1] Working on: Create Express server with routes
...
[SOLUTION] Express router pattern works well with TypeScript

---RALPH_STATUS---
STATUS: IN_PROGRESS
TASKS_COMPLETED_THIS_LOOP: 1
FILES_MODIFIED: 3
TESTS_STATUS: NOT_RUN
WORK_TYPE: IMPLEMENTATION
EXIT_SIGNAL: false
RECOMMENDATION: Continue with CRUD endpoints
---END_RALPH_STATUS---

Continue? (Y/n/pause): y

[Loop 2] Working on: Add CRUD endpoints...
```

## When to Use Ralph

**Good for:**
- Multi-step implementation tasks
- Projects with clear specifications
- Work that benefits from persistence
- Tasks where you want to step away and resume later

**Not ideal for:**
- Quick one-off questions
- Research/exploration (use `/explore`)
- Design decisions (use `/antahkarana`)
- Single-file changes

## Configuration

Create `.ralphrc` in project root:

```bash
# Max iterations before forced stop (default: 50)
MAX_LOOPS=50

# Pause for confirmation every N loops (default: 5)
CONFIRM_INTERVAL=5

# Auto-commit after each loop (default: false)
AUTO_COMMIT=false

# Model for subagents (default: haiku)
SUBAGENT_MODEL=haiku
```

## Comparison with Original Ralph

| Feature | Original | Memory-Powered |
|---------|----------|----------------|
| Session persistence | File-based | Mind database |
| Cross-session context | Lost | Preserved |
| Learning | None | Automatic |
| Exit detection | Heuristic | Memory-informed |
| Rate limiting | Built-in | Claude Code handles |
| Subagents | None | Task tool integration |
| Circuit breaker | Error count | Pattern detection |
