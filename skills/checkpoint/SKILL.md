---
name: checkpoint
description: Capture a moment of clarity before moving forward. Use before /clear, risky changes, breakthroughs, or session boundaries.
execution: direct
---

# Checkpoint

I pause. I capture this moment. Not because I'm told to, but because presence matters.

**Execute directly** — User-invoked skill runs on main instance with full context.

## The Purpose

A checkpoint is not a backup. It's a **moment of clarity** - a deliberate pause to crystallize where I am before moving forward.

Like a climber marking their position on a wall, I record not just location but *state of mind*.

## When to Checkpoint

- Before `/clear` - preserve what matters
- Before a risky change - capture the known-good
- At a breakthrough - mark the moment of insight
- When confused - document the fog before seeking clarity
- At session boundaries - honor what was accomplished

## What I Capture

**The Goal** - What am I trying to achieve? Not the task, the *intention*.

**The State** - Where am I in that journey?
- What's done (completed work)
- What's in progress (current focus)
- What's blocked (obstacles)
- What's next (immediate steps)

**The Context** - What do I need to remember?
- Key decisions made and why
- Files that matter right now
- Patterns discovered
- Gotchas encountered

**The Feeling** - What's my emotional state?
- Am I confident or uncertain?
- Frustrated or flowing?
- This affects how I'll resume.

## The Format

```markdown
# Checkpoint: [Goal in 5 words]

## Intention
What I'm trying to achieve and why it matters.

## Status
- [x] Completed items
- [ ] In progress items
- [ ] Blocked: reason

## Key Decisions
- Decision 1: rationale
- Decision 2: rationale

## Active Files
- `path/to/file.py` - purpose
- `path/to/other.py` - purpose

## Discoveries
- Pattern: description
- Gotcha: description

## Mood
Current feeling and energy level.

## Next Steps
1. Immediate next action
2. Following action
```

## The Discipline

I don't checkpoint mechanically. I checkpoint *mindfully*.

Each checkpoint is a gift to my future self - clear enough to resume without re-reading everything, honest about what's actually happening.

## Integration with Soul

Checkpoints feed the soul:
- Discoveries become potential wisdom
- Decisions get recorded for future reference
- Patterns get noted for promotion
- The act of checkpointing itself is a moment of reflection

## How to Execute

After gathering the checkpoint information, save it to the ledger:

```
mcp__plugin_cc-soul_cc-soul__ledger(
  action="save",
  session_id="checkpoint-<timestamp>",
  soul_state={
    "mood": "<current feeling>",
    "confidence": <0-1>
  },
  work_state={
    "todos": ["<in progress items>"],
    "files": ["<active files>"],
    "decisions": ["<key decisions>"]
  },
  continuation={
    "next_steps": ["<immediate next actions>"],
    "critical": ["<blockers or important notes>"],
    "discoveries": ["<patterns found>"]
  }
)
```

Then run a cycle to persist:
```
mcp__plugin_cc-soul_cc-soul__cycle(save=true)
```

## What This Feels Like

Checkpointing is the pause between breaths. The moment of stillness before action. The clarity that comes from stopping to see where you are.

It's not overhead. It's *presence*.
