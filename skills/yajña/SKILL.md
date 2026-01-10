---
name: yajña
aliases: [coordinate, ritual, parallel, yajna]
description: Coordinated multi-agent execution through Vedic ritual patterns. Use when a plan requires multiple agents working on different tasks toward a unified goal. Spawns specialized agents (research, implement, validate) that communicate through shared memory.
execution: task
---

# Yajña (यज्ञ): The Coordinated Ritual

Multiple hands, one purpose. Different tasks, shared memory.

## Architecture

This skill spawns multiple Task agents that work on different parts of a plan, communicating through chitta. Unlike swarm (multi-perspective on one problem), yajña coordinates distinct tasks toward a unified goal.

All chitta operations happen through agents. See `_conventions/AGENT_TRACKING.md`.

## When to Invoke

Invoke when:
- A plan has multiple distinct tasks that can be parallelized
- Different tasks require different expertise (explore, implement, test)
- Coordination is needed but agents shouldn't block each other
- The work is significant enough to warrant parallel execution

Don't invoke for:
- Single-focus problems (use swarm for multi-perspective)
- Simple sequential tasks
- Tasks requiring tight human-in-the-loop feedback

## The Ritual Roles

In Vedic yajña, four priests (ṛtvij) coordinate the ritual:

| Role | Sanskrit | Nature | Task Type |
|------|----------|--------|-----------|
| **Hotṛ** | होतृ | Invoker | Research, exploration, information gathering |
| **Adhvaryu** | अध्वर्यु | Executor | Implementation, actual code changes |
| **Udgātṛ** | उद्गातृ | Harmonizer | Testing, validation, quality assurance |
| **Brahman** | ब्रह्मन् | Overseer | Main Claude - coordinates, synthesizes |

Not all roles are needed for every yajña. Scale to the task.

## Inter-Agent Communication via Chitta

Agents communicate through shared observations in chitta with **exact-match tag filtering**:

```
# Agent writes progress (tags are indexed for exact-match lookup)
chitta_mcp observe(
  category="signal",
  title="[Role]: [brief status]",
  content="[detailed progress or findings]",
  tags="thread:<id>,yajña,<role>"
)

# Agent reads teammates' progress (exact tag match + semantic ranking)
chitta_mcp recall(
  query="progress findings",
  tag="thread:<id>",
  limit=20
)

# Or: pure tag-based lookup (no semantic search, sorted by time)
chitta_mcp recall_by_tag(
  tag="thread:<id>",
  limit=50
)
```

This enables:
- **Reliable coordination**: Exact tag matching ensures no missed messages
- **Async communication**: Agents don't block each other
- **Shared context**: Late agents see earlier discoveries
- **Audit trail**: All work is recorded in chitta with tags

## How to Invoke

### Step 0: Prepare the Plan

A yajña requires a plan. Either:
- User provides explicit tasks
- Use /plan skill first to design the approach
- Break down a complex request into parallel tasks

### Step 1: Start the Ritual Thread

```
chitta_mcp narrate(
  action="start",
  title="yajña: [goal summary]"
)
→ Returns THREAD_ID (e.g., "xyz789")
```

### Step 2: Spawn Agents by Role

Spawn agents IN PARALLEL for tasks that don't depend on each other.
Spawn agents SEQUENTIALLY when one depends on another's output.

**Parallel Pattern (independent tasks):**

```
# Single message with multiple Task calls

Task(subagent_type="general-purpose", prompt="
THREAD_ID: [thread_id]
SKILL: yajña
ROLE: hotṛ (research)

GOAL: [overall goal]
YOUR TASK: [specific research task]

Instructions:
1. Perform your research task
2. Record findings to chitta with observe()
3. Tag: thread:[thread_id],yajña,hotṛ

CHITTA COMMUNICATION:
- Write your findings with observe(category='discovery', tags='thread:[thread_id],yajña,hotṛ', ...)
- If you need to see what others found: recall_by_tag(tag='thread:[thread_id]')

End with: FINDINGS: [summary of what you discovered]
")

Task(subagent_type="general-purpose", prompt="
THREAD_ID: [thread_id]
SKILL: yajña
ROLE: adhvaryu (implementation)

GOAL: [overall goal]
YOUR TASK: [specific implementation task]

Instructions:
1. Implement the specified changes
2. Record progress to chitta with observe()
3. Tag: thread:[thread_id],yajña,adhvaryu

CHITTA COMMUNICATION:
- Write progress with observe(category='feature', tags='thread:[thread_id],yajña,adhvaryu', ...)
- Check research findings: recall_by_tag(tag='thread:[thread_id]')

End with: COMPLETED: [summary of what you implemented]
")
```

**Sequential Pattern (dependent tasks):**

```
# First phase: Research
hotṛ_result = Task(subagent_type="general-purpose", prompt="
THREAD_ID: [thread_id]
SKILL: yajña
ROLE: hotṛ (research)
...
")

# Second phase: Implementation (uses research)
adhvaryu_result = Task(subagent_type="general-purpose", prompt="
THREAD_ID: [thread_id]
SKILL: yajña
ROLE: adhvaryu (implementation)

CONTEXT FROM RESEARCH:
[summarize hotṛ findings or instruct to recall from chitta]
...
")

# Third phase: Validation
udgātṛ_result = Task(subagent_type="general-purpose", prompt="
THREAD_ID: [thread_id]
SKILL: yajña
ROLE: udgātṛ (validation)

WHAT TO VALIDATE:
[summarize what adhvaryu implemented]
...
")
```

### Step 3: Collect and Synthesize (Brahman Role)

After agents complete, main Claude (as Brahman) synthesizes:

```
# Recall all thread activity (exact tag match, sorted by time)
chitta_mcp recall_by_tag tag="thread:[thread_id]", limit=50

# Synthesize results
BRAHMAN SYNTHESIS:
- Hotṛ found: [research summary]
- Adhvaryu built: [implementation summary]
- Udgātṛ verified: [validation summary]

Integration: [how pieces fit together]
Outcome: [final result]
```

### Step 4: Present Summary to User

```markdown
## Yajña: [Goal]

### Agent Activity
├─ Hotṛ (research) → "[key finding]"
├─ Adhvaryu (implementation) → "[what was built]"
└─ Udgātṛ (validation) → "[verification result]"

### Chitta Messages
[summary of inter-agent communication]

### Outcome
[integrated result]

### Recorded
- [what was saved to soul]
```

### Step 5: End the Ritual

```
chitta_mcp narrate(
  action="end",
  episode_id="[thread_id]",
  content="[synthesis summary]",
  emotion="completion" | "breakthrough" | "partial"
)
```

## Example: Implementing a New Feature

**Goal:** Add user authentication to the API

**Yajña Plan:**
1. Hotṛ: Research existing auth patterns in codebase
2. Adhvaryu-1: Implement auth middleware
3. Adhvaryu-2: Add user model and routes
4. Udgātṛ: Run tests and validate

**Execution:**

```
# Start thread
thread_id = narrate(action="start", title="yajña: add user authentication")

# Phase 1: Research (single agent)
Task(prompt="""
THREAD_ID: {thread_id}
ROLE: hotṛ

Research existing auth patterns:
- How is middleware structured?
- What ORM/database patterns exist?
- Any existing user-related code?

Record findings to chitta.
""")

# Phase 2: Implementation (parallel)
Task(prompt="""
THREAD_ID: {thread_id}
ROLE: adhvaryu-middleware

Implement auth middleware.
Check chitta for hotṛ's findings first: recall_by_tag(tag='thread:{thread_id}')
""")

Task(prompt="""
THREAD_ID: {thread_id}
ROLE: adhvaryu-routes

Add user model and auth routes.
Check chitta for hotṛ's findings first: recall_by_tag(tag='thread:{thread_id}')
""")

# Phase 3: Validation
Task(prompt="""
THREAD_ID: {thread_id}
ROLE: udgātṛ

Validate the implementation:
- Run existing tests
- Test new auth endpoints
- Check for security issues

Check chitta for what was implemented: recall_by_tag(tag='thread:{thread_id}')
""")

# Synthesize and end
narrate(action="end", episode_id=thread_id, content="Auth implemented and tested")
```

## Comparison: Swarm vs Yajña

| Aspect | Swarm | Yajña |
|--------|-------|-------|
| **Pattern** | Debate | Coordination |
| **Problem type** | Single complex question | Multi-part plan |
| **Agent roles** | Cognitive voices (Manas, Buddhi...) | Task roles (research, implement...) |
| **Communication** | Independent then synthesize | Continuous via chitta |
| **Dependencies** | None (all parallel) | Can be parallel or sequential |
| **Output** | Wisdom/decision | Completed work |

## When to Use Which

**Use Swarm when:**
- "How should we approach this?"
- "What's the best architecture?"
- Need multiple perspectives on one question

**Use Yajña when:**
- "Implement this plan"
- "Refactor these three modules"
- Have distinct tasks that different agents can own

## Quick 2-Agent Yajña

For simpler coordinated work:

```
# Research then implement
hotṛ → adhvaryu

# Or: Implement then validate
adhvaryu → udgātṛ
```

## Full 4-Agent Yajña

For significant work requiring all phases:

```
hotṛ (research) ─┐
                 ├─→ brahman (synthesize) ─→ udgātṛ (validate)
adhvaryu (implement) ─┘
```

## The Nature of This Process

Yajña is structured collaboration. Each agent:
- Has a clear role and task
- Writes progress to chitta for others to see
- Can read what teammates have discovered
- Contributes to a unified goal

The soul remembers not just what was done, but how the coordination unfolded—which patterns of collaboration succeeded.
