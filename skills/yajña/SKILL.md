---
name: yajña
aliases: [yajna, autonomous, loop, agentic-loop, coordinate, ritual]
description: Autonomous development ritual with role-based coordination. Loops until complete using specialized agents (hotṛ→research, adhvaryu→implement, udgātṛ→test).
execution: direct
---

# Yajña (यज्ञ) - Autonomous Development Ritual

**Truly autonomous** development loop with Vedic role-based coordination. Multiple hands, one purpose - works continuously until done.

## Quick Start

```bash
/yajna                    # Start ritual (runs until done)
/yajna init [goal]        # Initialize with explicit goal
/yajna status             # Show progress
/yajna pause              # Stop after current iteration
```

## The Four Priests (ṛtvij)

| Role | Sanskrit | Function | Agent Type |
|------|----------|----------|------------|
| **Hotṛ** | होतृ | Research, exploration, information gathering | Explore agent |
| **Adhvaryu** | अध्वर्यु | Implementation, actual code changes | General-purpose |
| **Udgātṛ** | उद्गातृ | Testing, validation, quality assurance | General-purpose |
| **Brahman** | ब्रह्मन् | Coordinator (main Claude) - synthesizes, decides | You |

## How It Works

```
╔══════════════════════════════════════════════════════════════╗
║                    YAJÑA RITUAL LOOP                         ║
╠══════════════════════════════════════════════════════════════╣
║  1. ĀHUTI (Offering) - Load context                          ║
║     • long_task_snapshot → previous state                    ║
║     • recall → relevant memories                             ║
║     • Read fix_plan.md → remaining tasks                     ║
║                                                              ║
║  2. HOTṚ INVOCATION - Research phase                         ║
║     • What do we need to know?                               ║
║     • Explore codebase, read docs, understand context        ║
║     • Store findings to memory                               ║
║                                                              ║
║  3. ADHVARYU WORK - Implementation phase                     ║
║     • Implement the task (parallel if independent)           ║
║     • Update fix_plan.md: [ ] → [x]                          ║
║     • Use [SOLUTION]/[GOTCHA]/[DECISION] markers             ║
║                                                              ║
║  4. UDGĀTṚ CHANT - Validation phase                          ║
║     • Run tests                                              ║
║     • Verify implementation                                  ║
║     • Report quality                                         ║
║                                                              ║
║  5. BRAHMAN SYNTHESIS - Checkpoint                           ║
║     • long_task_update → progress                            ║
║     • Evaluate: complete? stagnant? continue?                ║
║     • If tasks remain → goto step 1                          ║
║                                                              ║
║  EXIT: All tasks done | Stagnation | User interrupt          ║
╚══════════════════════════════════════════════════════════════╝
```

## Execution Process

**IMPORTANT**: This is a LOOP. Execute repeatedly until exit condition.

### Phase 1: Āhuti (Initialize/Load)

```javascript
// First iteration only
if (!active_task) {
  goal = ask_user_or_infer();
  mkdir(".yajna/specs");
  generate_fix_plan(goal);
  long_task_start({ task_id: `yajna-${project}-${ts}`, goal, realm: project });
  remember(`[yajña] ${project}→started\ngoal: ${goal}`);
}

// Every iteration
long_task_snapshot({ mode: "brief" });
recall({ query: `${project} decisions solutions gotchas blockers` });
fix_plan = read(".yajna/fix_plan.md");
```

### Phase 2: Hotṛ (Research)

For complex tasks, spawn research agent first:

```javascript
Task({
  subagent_type: "Explore",
  description: "Research for implementation",
  prompt: `Research what's needed for: ${next_task}
    - Find relevant code patterns
    - Identify dependencies
    - Note potential gotchas
    Store findings with [INSIGHT] markers.`,
  model: "haiku"
});
```

Skip if task is straightforward.

### Phase 3: Adhvaryu (Implement)

**Parallel** (independent tasks):
```javascript
// Spawn multiple in ONE tool call
Task({
  subagent_type: "general-purpose",
  description: "Implement task 1",
  prompt: "Implement [task 1]. Update fix_plan.md when done.",
  model: "haiku"
});
Task({
  subagent_type: "general-purpose",
  description: "Implement task 2",
  prompt: "Implement [task 2]. Update fix_plan.md when done.",
  model: "haiku"
});
```

**Sequential** (dependent tasks):
```
1. Implement the task directly
2. Update fix_plan.md: [ ] → [x]
3. Use markers: [SOLUTION], [GOTCHA], [DECISION], [FAILURE]
```

### Phase 4: Udgātṛ (Validate)

```javascript
Task({
  subagent_type: "general-purpose",
  description: "Run tests",
  prompt: `Validate the implementation:
    1. Run relevant tests
    2. Check for regressions
    3. Report: PASSING | FAILING | SKIPPED`,
  model: "haiku"
});
```

### Phase 5: Brahman (Synthesize)

```javascript
// Checkpoint
long_task_update({
  completed_summary: "Iteration N: [tasks completed]",
  add_work_items: ["Discovered task"],  // if any
  add_blockers: ["Blocker found"]       // if any
});

long_task_event({
  event_type: "checkpoint",
  description: `Iteration ${n}: ${tasks_done.join(", ")}`
});

// Evaluate
if (all_tasks_complete) → COMPLETION
if (same_error_3x) → STAGNATION
if (iteration > MAX_LOOPS) → TIMEOUT
else → continue to Phase 1
```

### COMPLETION (Pūrṇāhuti - Final Offering)

```javascript
long_task_complete({
  task_id,
  outcome: "Completed: [summary]"
});

remember(`[yajña] ${project}→completed
goal: ${goal}
outcome: ${summary}
iterations: ${count}`);

learn_milestone({
  milestone: `Yajña: ${project} complete`,
  details: summary
});
```

### STAGNATION

```javascript
long_task_event({
  event_type: "error",
  description: "Stagnation: [pattern]"
});

remember(`[yajña] ${project}→stagnated
pattern: ${repeated_error}`);
```

Ask user for guidance (ONLY time to prompt).

## File Structure

```
project/
├── .yajna/
│   ├── fix_plan.md        # Prioritized tasks (checkboxes)
│   ├── AGENT.md           # Build/run/test instructions
│   └── specs/             # Specifications
└── ...
```

## fix_plan.md Format

```markdown
# Yajña: [Goal]

## Priority 1 - Critical
- [ ] First critical task
- [x] Completed task

## Priority 2 - Important
- [ ] Important task

## Priority 3 - Nice to Have
- [ ] Optional task

## Blockers
- [ ] [BLOCKER] Thing preventing progress

## Discovered
- [ ] [NEW] Task found during work
```

## Status Output

```
━━━ ITERATION [N] ━━━
[HOTṚ] Researched: [findings]
[ADHVARYU] Implemented: [tasks]
[UDGĀTṚ] Tests: PASSING|FAILING
[CHECKPOINT] X/Y tasks done

━━━ PŪRṆĀHUTI ━━━
Ritual complete. [summary]
```

## Multi-Session Continuity

Tasks persist via long_task:

```
Session 1: /yajna init "Build API"
  → 3/5 tasks done, context fills
  → Auto-checkpoint

Session 2: /yajna
  → Detects active task
  → Loads snapshot
  → Continues from 4/5
```

## Configuration (.yajnarc)

```bash
MAX_LOOPS=50              # Max iterations
SKIP_TESTS=false          # Skip udgātṛ phase
PARALLEL_TASKS=true       # Allow parallel adhvaryu
```

## Inter-Agent Communication

Agents communicate through memory:

```javascript
// Agent writes
remember({
  content: "[yajña:hotṛ] Found pattern: singleton used for DB",
  tags: ["thread:yajna-123", "research"]
});

// Brahman reads
recall({
  query: "thread:yajna-123 research findings"
});
```

## Example Session

```
User: /yajna init "Add user authentication"

╔══════════════════════════════════════════════════════════════╗
║  YAJÑA: Autonomous Development Ritual                        ║
║  Project: myapp | Goal: Add user authentication              ║
╚══════════════════════════════════════════════════════════════╝

[RECALL] 2 relevant memories:
  • JWT tokens preferred over sessions
  • Always hash passwords with bcrypt

[INIT] Task: yajna-myapp-1706700000
[PLAN] Generated fix_plan.md (5 tasks)

━━━ ITERATION 1 ━━━
[HOTṚ] Research: existing auth patterns in codebase
  → Found: middleware pattern, User model exists
[ADHVARYU] Implementing auth routes + middleware
  ✓ src/routes/auth.ts
  ✓ src/middleware/authenticate.ts
[UDGĀTṚ] Tests: 4 passing
[SOLUTION] JWT with refresh tokens works well

[CHECKPOINT] 2/5 done

━━━ ITERATION 2 ━━━
[ADHVARYU] Password hashing + validation
  ✓ src/utils/password.ts
  ✓ Updated User model
[UDGĀTṚ] Tests: 8 passing
[GOTCHA] bcrypt.compare is async - must await

[CHECKPOINT] 4/5 done

━━━ ITERATION 3 ━━━
[ADHVARYU] Protected routes + docs
  ✓ Applied middleware to routes
  ✓ Updated API docs
[UDGĀTṚ] Tests: 12 passing

━━━ PŪRṆĀHUTI ━━━
Ritual complete in 3 iterations.
Files: 6 | Tests: 12 passing | Learnings: 2

[yajña] myapp→completed
```

## When to Use

**Good for:**
- Multi-step implementation
- Projects needing research → implement → test cycle
- Work spanning sessions
- Tasks benefiting from role separation

**Not ideal for:**
- Quick questions
- Pure research → `/explore`
- Design decisions → `/antahkarana`
- Single file edits

## MCP Tools

| Tool | When |
|------|------|
| `long_task_start` | Initialize ritual |
| `long_task_active` | Check for existing |
| `long_task_snapshot` | Load context |
| `long_task_update` | Record progress |
| `long_task_event` | Log checkpoints |
| `long_task_complete` | Mark done |
| `recall` | Surface memories |
| `remember` | Store learnings |
| `learn_milestone` | Record achievements |
