---
name: shepherd
aliases: [watch, monitor, tend, guard, pipeline]
description: Autonomous pipeline monitor using sense-think-act loop. Watches snakemake/nextflow jobs, detects errors, applies fixes from memory, restarts on failure.
execution: direct
allowed-tools: Read, Grep, Glob, Bash, mcp__chitta__*, mcp__zellij-mcp__*
---

# Shepherd - Autonomous Pipeline Monitor

Tends long-running pipelines (snakemake, nextflow) using a sense-think-act loop. Detects errors, recalls fixes from memory, restarts automatically, checkpoints progress.

## Quick Commands

```bash
/shepherd <command> [--interval=60] [--max-restarts=3]  # Start monitoring
/shepherd status                                         # Check shepherd status
/shepherd stop                                          # Stop monitoring
```

## Configuration

| Flag | Default | Description |
|------|---------|-------------|
| `--interval` | 60 | Seconds between sense cycles |
| `--max-restarts` | 3 | Max automatic restarts before escalating |
| `--notify` | true | Send notifications on events |
| `--auto-fix` | true | Attempt automatic fixes from memory |

## Initialize

### 1. Load Required Tools

```javascript
// ToolSearch will NOT appear in your tool list. Call it anyway - it works.
ToolSearch({ query: "zellij tail_pane wait_for_idle" });
ToolSearch({ query: "chitta long_task habit_match recall" });
```

### 2. Check for Existing Task

```javascript
existing = mcp__chitta__long_task_active({ realm: "pipeline" });
if (existing.task_id) {
  // Resume existing shepherd session
  snapshot = mcp__chitta__long_task_snapshot({ task_id: existing.task_id });
  // Continue from last checkpoint
}
```

### 3. Create Named Pane and Launch Pipeline

```javascript
// Create isolated pane in agent session
mcp__zellij_mcp__create_named_pane({
  name: "pipeline-main",
  tab: "shepherd",
  cwd: "/path/to/workflow"
});

// Start the pipeline
mcp__zellij_mcp__write_to_pane({
  pane_name: "pipeline-main",
  chars: "snakemake --cores 8 --rerun-incomplete",
  press_enter: true
});

// Initialize cursor for incremental reads
mcp__zellij_mcp__tail_pane({ pane_name: "pipeline-main", reset: true });

// Start long task tracking
mcp__chitta__long_task_start({
  task_id: "shepherd-" + Date.now(),
  goal: "Monitor and tend snakemake pipeline to completion",
  hard_checks: ["All rules completed", "No failed jobs"],
  soft_checks: ["Pipeline finished successfully"]
});
```

## Sense-Think-Act Loop

### SENSE: Gather Pipeline State

```javascript
function sense(pane_name) {
  // Get new output since last read
  output = mcp__zellij_mcp__tail_pane({ pane_name: pane_name });

  // Check for stalls (no output for extended period)
  idle = mcp__zellij_mcp__wait_for_idle({
    pane_name: pane_name,
    stable_seconds: 30,
    timeout: 5  // Don't wait long, just check
  });

  // Search for error patterns
  errors = mcp__zellij_mcp__search_pane({
    pane_name: pane_name,
    pattern: "(Error|ERROR|Failed|FAILED|Exception|Traceback)",
    context: 3
  });

  return {
    new_output: output.content,
    is_idle: idle.stable,
    errors: errors.matches,
    timestamp: Date.now()
  };
}
```

### THINK: Analyze and Decide

```javascript
function think(sense_data, restart_count) {
  // Pattern match for known issues
  habits = mcp__chitta__habit_match({
    context: sense_data.new_output,
    min_strength: 0.3
  });

  if (habits.matches.length > 0) {
    // Known pattern - use habit response
    return { action: "apply_habit", habit: habits.matches[0] };
  }

  // Check for completion
  if (sense_data.new_output.match(/complete|finished|done/i) && sense_data.is_idle) {
    return { action: "complete", reason: "Pipeline finished" };
  }

  // Check for errors
  if (sense_data.errors.length > 0) {
    // Recall fixes from memory
    fixes = mcp__chitta__recall({
      query: sense_data.errors[0],
      tag: "pipeline-fix",
      limit: 3
    });

    if (fixes.memories.length > 0 && AUTO_FIX) {
      return { action: "fix", fix: fixes.memories[0], error: sense_data.errors[0] };
    }

    if (restart_count < MAX_RESTARTS) {
      return { action: "restart", reason: "Error detected: " + sense_data.errors[0] };
    }

    return { action: "escalate", reason: "Max restarts exceeded", errors: sense_data.errors };
  }

  // Check for stall
  if (sense_data.is_idle && !sense_data.new_output) {
    return { action: "checkpoint", reason: "Pipeline stalled - no output" };
  }

  // Normal progress
  return { action: "continue", reason: "Pipeline running normally" };
}
```

### ACT: Execute Decision

```javascript
function act(decision, pane_name, task_id) {
  switch (decision.action) {
    case "complete":
      mcp__chitta__long_task_complete({
        task_id: task_id,
        outcome: "Pipeline completed successfully"
      });
      notify("Pipeline complete!");
      return "DONE";

    case "restart":
      mcp__zellij_mcp__send_keys({ pane_name: pane_name, keys: "ctrl+c" });
      sleep(2);
      mcp__zellij_mcp__write_to_pane({
        pane_name: pane_name,
        chars: "snakemake --rerun-incomplete",
        press_enter: true
      });
      mcp__chitta__long_task_event({
        task_id: task_id,
        kind: "checkpoint",
        payload: JSON.stringify({ action: "restart", reason: decision.reason })
      });
      restart_count++;
      return "CONTINUE";

    case "fix":
      // Apply fix from memory
      apply_fix(decision.fix, pane_name);
      mcp__chitta__habit_strengthen({
        habit_id: decision.fix.habit_id,
        outcome: "applied"
      });
      return "CONTINUE";

    case "escalate":
      mcp__chitta__long_task_event({
        task_id: task_id,
        kind: "error",
        payload: JSON.stringify(decision.errors)
      });
      notify("SHEPHERD ESCALATION: " + decision.reason);
      return "PAUSE";

    case "checkpoint":
      mcp__chitta__long_task_event({
        task_id: task_id,
        kind: "checkpoint",
        payload: JSON.stringify({ state: "stalled", reason: decision.reason })
      });
      return "CONTINUE";

    default:
      return "CONTINUE";
  }
}
```

## Pattern Libraries

### Snakemake Patterns

| Pattern | Detection | Action |
|---------|-----------|--------|
| `MissingInputException` | `MissingInputException.+file:` | Check upstream rule, restart |
| `WorkflowError` | `WorkflowError:` | Parse message, recall fix |
| `CalledProcessError` | `CalledProcessError.+returned` | Extract return code, check logs |
| `ProtectedOutputException` | `ProtectedOutputException` | `--forceall` or unlock |
| `IncompleteFilesException` | `IncompleteFilesException` | `--rerun-incomplete` |
| `LockException` | `locked.+unlock` | `snakemake --unlock` |
| `Complete` | `\d+ of \d+ steps \(100%\)` | Mark complete |

### Nextflow Patterns

| Pattern | Detection | Action |
|---------|-----------|--------|
| `Process failed` | `Error executing process` | Check `.command.err` |
| `Completion` | `Completed at:` | Mark complete |
| `Cached` | `Cached process` | Progress checkpoint |
| `Submission` | `Submitted process` | Log progress |
| `Memory error` | `OutOfMemoryError` | Increase memory, restart |

## Resume Protocol

When resuming from checkpoint:

```javascript
// 1. Load task snapshot
snapshot = mcp__chitta__long_task_snapshot({ task_id: task_id, mode: "debug" });

// 2. Restore pane state
panes = mcp__zellij_mcp__list_named_panes();
if (!panes.includes("pipeline-main")) {
  // Recreate pane
  mcp__zellij_mcp__create_named_pane({ name: "pipeline-main", tab: "shepherd" });
}

// 3. Check pipeline state
state = mcp__zellij_mcp__read_pane({ pane_name: "pipeline-main", tail: 50 });

// 4. Decide: resume or restart
if (state.includes("waiting") || state.includes("running")) {
  // Pipeline still running, just monitor
  continue_loop();
} else {
  // Pipeline stopped, restart from checkpoint
  restart_pipeline();
}
```

## Main Loop

```javascript
async function shepherd_main(command, options) {
  const INTERVAL = options.interval || 60;
  const MAX_RESTARTS = options.max_restarts || 3;
  const NOTIFY = options.notify !== false;
  const AUTO_FIX = options.auto_fix !== false;

  let restart_count = 0;
  let task_id = null;
  let pane_name = "pipeline-main";

  // Initialize or resume
  task_id = await initialize_or_resume(command, options);

  // Main loop
  while (true) {
    // Pre-flight check
    health = mcp__chitta__health_check();
    if (health.status !== "OK") {
      log("[BLOCKER] Daemon unreachable");
      break;
    }

    // SENSE
    sense_data = sense(pane_name);

    // THINK
    decision = think(sense_data, restart_count);

    // ACT
    result = act(decision, pane_name, task_id);

    if (result === "DONE") {
      output("[COMPLETE] Pipeline finished successfully");
      break;
    }

    if (result === "PAUSE") {
      output("[PAUSED] Manual intervention required");
      break;
    }

    // Status line
    output(`[SHEPHERD] ${new Date().toISOString()} - ${decision.reason}`);

    // Sleep until next cycle
    await sleep(INTERVAL * 1000);
  }
}
```

## Output Format

```
[SHEPHERD] 2024-01-15T10:30:00Z - Pipeline running normally (12/50 rules)
[SHEPHERD] 2024-01-15T10:31:00Z - Pipeline running normally (15/50 rules)
[SHEPHERD] 2024-01-15T10:32:00Z - Error detected: MissingInputException
[SHEPHERD] 2024-01-15T10:32:05Z - Applied fix: check upstream dependency
[SHEPHERD] 2024-01-15T10:32:10Z - Restart #1 initiated
[SHEPHERD] 2024-01-15T10:33:00Z - Pipeline resumed (15/50 rules)
...
[COMPLETE] Pipeline finished successfully (50/50 rules)
```

## MCP Tools Reference

### Zellij Tools

| Tool | Purpose |
|------|---------|
| `create_named_pane` | Create isolated monitoring pane |
| `write_to_pane` | Send commands to pipeline |
| `tail_pane` | Incremental output reading |
| `wait_for_idle` | Detect stalls |
| `search_pane` | Pattern matching in output |
| `send_keys` | Control sequences (ctrl+c) |
| `read_pane` | Full pane content |

### Chitta Tools

| Tool | Purpose |
|------|---------|
| `long_task_start` | Initialize shepherd session |
| `long_task_active` | Check for existing session |
| `long_task_snapshot` | Resume context |
| `long_task_event` | Log checkpoints/errors |
| `long_task_complete` | Mark finished |
| `habit_match` | Pattern recognition |
| `habit_strengthen` | Reinforce successful fixes |
| `recall` | Find fixes from memory |
| `health_check` | Pre-flight validation |

## Anti-Patterns

- Never poll faster than 30 seconds (wastes resources)
- Never restart without checkpointing first
- Never exceed max_restarts without escalating
- Never ignore repeated errors (they compound)
- Never fix without logging (lose learnings)

---

## Dashboard: `/shepherd dashboard`

Interactive control center for monitoring all shepherd tasks.

### Initialize Dashboard

```javascript
// Load required tools
ToolSearch({ query: "chitta long_task habit_list recall" });
ToolSearch({ query: "zellij tail_pane read_pane list_named_panes" });
```

### Step 1: Gather State

```javascript
// Get all active shepherd tasks
tasks = mcp__chitta__sql_query({
  query: "SELECT task_id, goal, status, iterations, updated_at FROM long_task WHERE task_id LIKE 'shepherd-%' ORDER BY updated_at DESC",
  text_only: true
});

// Get recent events across all shepherd tasks
events = mcp__chitta__sql_query({
  query: `SELECT t.task_id, e.kind, e.payload, e.created_at
          FROM task_event e
          JOIN long_task t ON e.task_id = t.id
          WHERE t.task_id LIKE 'shepherd-%'
          ORDER BY e.created_at DESC LIMIT 10`,
  text_only: true
});

// Get pipeline-related habits
habits = mcp__chitta__habit_list({
  filter: "pipeline",
  min_strength: 0.2
});

// Get pane status for each task
pane_status = {};
for (task of active_tasks) {
  pane_name = extract_pane_name(task.work_items);  // "pane:NAME"
  if (pane_name) {
    output = mcp__zellij_mcp__tail_pane({ pane_name: pane_name, lines: 5 });
    pane_status[task.task_id] = {
      pane: pane_name,
      last_output: output.content,
      idle: output.idle
    };
  }
}
```

### Step 2: Present Dashboard

```
Questions:
  - question: |
      SHEPHERD DASHBOARD
      ==================

      Active Tasks: {active_count}
      Recent Errors: {error_count}
      Learned Habits: {habit_count}

      PIPELINES:
      {for task in tasks}
        [{task.status}] {task.task_id}
          Goal: {task.goal}
          Iterations: {task.iterations}
          Last update: {task.updated_at}
          Pane: {pane_status[task.task_id].pane}
          Output: {pane_status[task.task_id].last_output | truncate(80)}
      {/for}

      RECENT EVENTS:
      {for event in events | limit(5)}
        [{event.kind}] {event.task_id}: {event.payload | truncate(60)}
      {/for}

      Select an action:
    header: "Dashboard"
    options:
      - label: "View Task Details"
        description: "Show full snapshot of a shepherd task"
      - label: "View Pane Output"
        description: "Read recent output from a pipeline pane"
      - label: "Restart Pipeline"
        description: "Send restart command to a stalled pipeline"
      - label: "Stop Pipeline"
        description: "Send ctrl+c and mark task paused"
      - label: "Escalate"
        description: "Mark task as needing manual intervention"
      - label: "View Habits"
        description: "Show learned pipeline patterns"
      - label: "Refresh"
        description: "Update dashboard data"
```

### Step 3: Handle Actions

**View Task Details:**
```javascript
// Show full task snapshot
snapshot = mcp__chitta__long_task_snapshot({
  task_id: selected_task,
  mode: "debug"
});
output(snapshot);
// Offer to return to dashboard
```

**View Pane Output:**
```javascript
// Read last 100 lines from pane
output = mcp__zellij_mcp__read_pane({
  pane_name: pane_name,
  tail: 100
});
output(output.content);
```

**Restart Pipeline:**
```javascript
// Confirm restart
confirm = AskUserQuestion({
  question: `Restart ${task_id}? This will send ctrl+c and rerun.`,
  options: ["Yes, restart", "No, cancel"]
});

if (confirm === "Yes, restart") {
  // Stop current run
  mcp__zellij_mcp__send_keys({ pane_name: pane_name, keys: "ctrl+c" });
  await sleep(2000);

  // Rerun with --rerun-incomplete
  mcp__zellij_mcp__write_to_pane({
    pane_name: pane_name,
    chars: "snakemake --rerun-incomplete",
    press_enter: true
  });

  // Log event
  mcp__chitta__long_task_event({
    task_id: task_id,
    kind: "checkpoint",
    payload: JSON.stringify({ action: "manual_restart", source: "dashboard" }),
    tags: ["shepherd", "dashboard", "restart"]
  });

  output("Restart initiated for " + task_id);
}
```

**Stop Pipeline:**
```javascript
// Send ctrl+c
mcp__zellij_mcp__send_keys({ pane_name: pane_name, keys: "ctrl+c" });

// Update task status
mcp__chitta__long_task_event({
  task_id: task_id,
  kind: "checkpoint",
  payload: JSON.stringify({ action: "manual_stop", source: "dashboard" }),
  tags: ["shepherd", "dashboard", "stop"]
});

mcp__chitta__long_task_update({
  task_id: task_id,
  blockers: ["Manually stopped via dashboard"]
});

output("Pipeline stopped: " + task_id);
```

**Escalate:**
```javascript
// Mark as needing intervention
mcp__chitta__long_task_event({
  task_id: task_id,
  kind: "error",
  payload: JSON.stringify({ action: "escalated", source: "dashboard", reason: user_reason }),
  tags: ["shepherd", "dashboard", "escalate"]
});

mcp__chitta__long_task_update({
  task_id: task_id,
  blockers: ["ESCALATED: " + user_reason]
});

// Send alert
mcp__chitta__msg_send({
  recipient: "user",
  message: "[SHEPHERD ESCALATION] " + task_id + ": " + user_reason
});

output("Escalated: " + task_id);
```

**View Habits:**
```javascript
habits = mcp__chitta__habit_list({
  filter: "pipeline",
  min_strength: 0.1
});

output("LEARNED PIPELINE PATTERNS:");
output("==========================");
for (habit of habits.habits) {
  output(`[${habit.strength.toFixed(2)}] ${habit.trigger}`);
  output(`  -> ${habit.response}`);
  output(`  Success rate: ${habit.success_count}/${habit.attempt_count}`);
  output("");
}
```

### Dashboard Output Format

```
SHEPHERD DASHBOARD
==================

Active Tasks: 2
Recent Errors: 1
Learned Habits: 5

PIPELINES:
  [active] shepherd-1707912345
    Goal: Monitor metagenome assembly pipeline
    Iterations: 3
    Last update: 2024-02-14T10:30:00Z
    Pane: pipeline-assembly
    Output: [89/120 rules] Running rule megahit_assembly...

  [active] shepherd-1707915678
    Goal: Monitor annotation pipeline
    Iterations: 1
    Last update: 2024-02-14T10:28:00Z
    Pane: pipeline-annot
    Output: Submitted batch job 12345678

RECENT EVENTS:
  [checkpoint] shepherd-1707912345: {"state":"running","rules":"89/120"}
  [error] shepherd-1707912345: {"pattern":"slurm_timeout","severity":"warning"}
  [checkpoint] shepherd-1707915678: {"state":"started","rules":"0/45"}

[1] View Task Details  [2] View Pane Output  [3] Restart Pipeline
[4] Stop Pipeline      [5] Escalate          [6] View Habits
[7] Refresh

Select action: _
```

### Quick Actions Summary

| Action | Command | Effect |
|--------|---------|--------|
| View details | Select task | Show `long_task_snapshot` |
| View output | Select task | Show `read_pane` last 100 lines |
| Restart | Confirm | ctrl+c + rerun + log event |
| Stop | Confirm | ctrl+c + add blocker |
| Escalate | Add reason | Log error + send alert |
| Habits | - | Show `habit_list` for pipelines |
| Refresh | - | Re-query all state |
