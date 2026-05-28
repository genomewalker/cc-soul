---
name: yajna
aliases: [yajna, autonomous, loop, agentic-loop, coordinate, ritual]
description: "Runs a fully autonomous, end-to-end development loop using role-based sub-agents for research, implementation, and testing. Use when the user requests autonomous feature development, multi-step bug fixes, refactoring, or any task requiring repeated research-implement-test cycles without manual coordination — e.g. 'build this feature autonomously', 'fix and test everything', 'run the full dev loop', or 'implement end-to-end'. Loops continuously until completion, stagnation, or a hard blocker (daemon failure, missing permissions, unresolvable errors). Coordinates three agent roles: hotr (research/exploration), adhvaryu (implementation in isolated worktree), and udgātr (validation/testing)."
execution: direct
allowed-tools: Read, Write, Edit, Grep, Glob, Bash(*), Task, mcp__chitta-mcp__*
---

# Yajña (यज्ञ) - Autonomous Development Ritual

**Truly autonomous** development loop. Runs continuously until done. NO manual coordination.

## Critical Rules

1. **NEVER ask user for routine decisions** - just do it
2. **NEVER pause between iterations** - loop automatically
3. **STOP ONLY for**: Blockers | Completion | User interrupt
4. **Blockers require user**: Daemon down, permissions, unresolvable errors
5. **NEVER say**: "Let me check with you…", "Should I continue?", "I'll wait for confirmation…"
6. **NEVER output** between every agent call or pause after each iteration

## Pre-Flight Check (REQUIRED)

Before EVERY iteration, verify daemon is alive:

```javascript
// Signature: mcp__chitta-mcp__health_check() → { status: "OK" | "ERROR", error?: string }
health = mcp__chitta-mcp__health_check();
if (health.error || health.status !== "OK") {
  // BLOCKER - stop and report
  output("[BLOCKER] Daemon unreachable. Run: pkill -9 chittad && chittad daemon");
  mcp__chitta-mcp__long_task_event({ event_type: "blocker", description: "Daemon down" });
  STOP;  // Do not continue
}
```

## The Loop (Execute Without Pausing)

```
WHILE tasks_remain AND no_blocker:

  1. PRE-FLIGHT
     mcp__chitta-mcp__health_check() → if fail → STOP with blocker

  2. LOAD CONTEXT (silent, no output)
     mcp__chitta-mcp__long_task_snapshot({ task_id })
     read fix_plan.md
     count remaining tasks

  3. HOTṚ - Research (if needed)
     Task(Explore) for complex tasks
     VALIDATE: Did agent find correct files?
     If wrong files → retry with explicit paths

  4. ADHVARYU - Implement
     BEFORE ANY CODE CHANGES:
       a. Call EnterWorktree to create isolated workspace
       b. All Read/Edit/Write/Bash file mutations must happen in the worktree path
       c. On completion or failure, call ExitWorktree
       d. Proposed patches are stored as artifacts, not applied directly to main
     Task(general-purpose) with EXPLICIT file paths
     Prompt MUST include: "Edit file X at path Y"
     VALIDATE: Check file was actually modified
     If not modified → retry or mark blocker

  5. UDGĀTṚ - Test
     Task(general-purpose) for validation
     Syntax checks, tests, verification
     If FAIL → log, continue (not a blocker)

  6. CHECKPOINT (silent)
     mcp__chitta-mcp__long_task_update({ task_id, progress })
     Update fix_plan.md

  7. EVALUATE
     All done? → COMPLETION
     Same error 3x? → STAGNATION (blocker)
     Else → continue loop (NO pause, NO output)
```

## Agent Prompts Must Be Explicit

**BAD** (vague, leads to wrong files):
```
"Add pattern detection to the hooks"
```

**GOOD** (explicit paths, clear instructions):
```
"Edit /home/user/.claude/hooks/prompt-hook.sh
Add these lines after line 25:
[exact code]
Verify the file exists first with: ls -la /home/user/.claude/hooks/"
```

## Validation After Each Agent

```javascript
// After Adhvaryu returns
// Task signature: Task({ description: string, prompt: string }) → string
result = Task({ description: "Implement dark mode CSS", prompt: "Edit /path/to/dark.css ..." });

// Validate the work
if (result.includes("Error") || result.includes("not found")) {
  retry_count++;
  if (retry_count >= 3) {
    // BLOCKER
    mcp__chitta-mcp__long_task_event({ event_type: "blocker", description: result });
    STOP;
  }
  continue;  // Retry this iteration
}

// Check file was actually modified
// Bash signature: Bash(cmd: string) → { stdout: string, stderr: string, error?: string }
file_check = Bash(`ls -la ${target_file}`);
if (file_check.error) {
  // File doesn't exist - agent edited wrong path
  retry_count++;
  continue;
}
```

## Output Rules

**During loop**: Minimal output. Just status lines:
```
━━━ ITERATION 3 ━━━
[HOTṚ] ✓ Found 2 patterns
[ADHVARYU] ✓ Modified 3 files
[UDGĀTṚ] ✓ Tests passing
[4/7 tasks done]
```

**On blocker**: Full details + stop:
```
━━━ BLOCKER ━━━
[ERROR] Daemon not responding
[ACTION] Run: pkill -9 chittad && chittad daemon
[STATUS] Yajna paused at iteration 3
```

**On completion**: Summary only:
```
━━━ PŪRṆĀHUTI ━━━
Complete in 5 iterations.
Files: 8 | Tests: passing | Learnings: 3
```

## Blocker Conditions

These STOP the loop and require user:

| Condition | Action |
|-----------|--------|
| Daemon health_check fails | Stop, show restart command |
| File not found after 3 retries | Stop, ask user for correct path |
| Same error 3 consecutive times | Stop, show stagnation |
| Build/compile fails critically | Stop, show error |
| Permission denied | Stop, ask user |

## NOT Blockers (Continue Automatically)

| Condition | Action |
|-----------|--------|
| Test fails | Log, continue to next task |
| Agent returns partial result | Use what's there, retry rest |
| Minor validation warning | Log, continue |
| Task takes long | Wait, don't timeout |

## Spawn Agents Correctly

**Parallel** (independent tasks, ONE tool call):
```javascript
// Send ALL parallel agents in SINGLE message
Task({ description: "Task 1", prompt: "..." });
Task({ description: "Task 2", prompt: "..." });
Task({ description: "Task 3", prompt: "..." });
// Wait for all, then continue
```

**Sequential** (dependent):
```javascript
result1 = Task({ description: "Task 1", prompt: "..." });
// Validate result1
result2 = Task({ description: "Task 2", prompt: `Using ${result1}...` });
// Validate result2
```

## Quick Reference

```bash
/yajna              # Start/continue (runs until done)
/yajna init GOAL    # New ritual with explicit goal
/yajna status       # Check progress without running
/yajna pause        # Stop after current iteration
```

## Example: Correct Autonomous Flow

```
User: /yajna init "Add dark mode"

[PRE-FLIGHT] Daemon OK
[INIT] yajna-myapp-1234
[PLAN] 4 tasks generated

━━━ ITERATION 1 ━━━
[HOTṚ] ✓ Found ThemeProvider, CSS variables
[ADHVARYU] ✓ src/theme/dark.css, src/context/Theme.tsx
[UDGĀTṚ] ✓ Builds, renders
[2/4 done]

━━━ ITERATION 2 ━━━
[ADHVARYU] ✓ Toggle component, localStorage
[UDGĀTṚ] ✓ Tests passing
[4/4 done]

━━━ PŪRṆĀHUTI ━━━
Complete in 2 iterations.
Files: 4 | Tests: 6 passing

[NO USER INTERACTION NEEDED - FULLY AUTONOMOUS]
```

## MCP Tools

| Tool | Signature / Purpose |
|------|---------------------|
| `mcp__chitta-mcp__health_check` | `() → { status: "OK"\|"ERROR", error?: string }` — Pre-flight daemon check |
| `mcp__chitta-mcp__long_task_start` | `({ goal: string }) → { task_id: string }` — Initialize ritual |
| `mcp__chitta-mcp__long_task_active` | `() → { task_id?: string }` — Check for existing task |
| `mcp__chitta-mcp__long_task_snapshot` | `({ task_id: string }) → { context: object }` — Load context |
| `mcp__chitta-mcp__long_task_update` | `({ task_id: string, progress: object }) → void` — Record progress |
| `mcp__chitta-mcp__long_task_event` | `({ event_type: string, description: string }) → void` — Log blockers/checkpoints |
| `mcp__chitta-mcp__long_task_complete` | `({ task_id: string }) → void` — Mark done |
