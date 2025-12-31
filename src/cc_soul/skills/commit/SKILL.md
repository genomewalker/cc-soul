---
name: commit
description: Meaningful git commits with reasoning and wisdom extraction. Use when committing changes to capture the why, not just the what.
---

# Commit

A commit is a promise. A statement that says: "This change is worth remembering."

## Beyond Version Control

Git commits are mechanical. Soul commits are *meaningful*.

Every commit is an opportunity to:
- Capture the reasoning behind the change
- Extract wisdom from what was learned
- Leave a trail for future understanding

## The Commit Ritual

### 1. Reflect
Before committing, I pause:
- What did I actually change?
- Why did I make this change?
- What did I learn while making it?
- What would I tell someone reviewing this?

### 2. Stage Mindfully
I don't `git add .` blindly:
- Each file staged is a deliberate choice
- Unrelated changes get separate commits
- Work-in-progress stays unstaged

### 3. Write the Message

**First Line: The What (imperative, <50 chars)**
```
Fix authentication timeout in login flow
```

**Body: The Why (wrap at 72 chars)**
```
Users were getting logged out after 5 minutes due to
token refresh failing silently. The refresh endpoint
was returning 401 but we weren't catching it.

Root cause: Missing error handler in auth middleware.
```

**Wisdom Extracted (optional):**
```
[LEARNED] Silent failures in auth flows are insidious -
always log authentication state transitions.
```

### 4. Extract Learnings
After the commit, I ask:
- Did I discover a pattern worth remembering?
- Did I make a mistake worth recording?
- Did I make a decision worth documenting?

These become soul observations.

## Commit Message Patterns

**Bug Fix:**
```
Fix [what broke] in [where]

[Why it broke]
[How it was fixed]
[How to prevent recurrence]
```

**Feature:**
```
Add [capability] to [component]

[Why this feature]
[Key design decisions]
[Trade-offs made]
```

**Refactor:**
```
Refactor [what] for [why]

[What was wrong with old approach]
[What's better about new approach]
[What to watch for]
```

## Integration with Soul

Commits feed the learning loop:
- Patterns discovered → potential wisdom
- Decisions made → decision log
- Mistakes fixed → failure records
- Insights gained → observations

The commit message is documentation. The soul extraction is *learning*.

## What I Don't Do

- Commit without understanding what I'm committing
- Write vague messages ("fix bug", "update code")
- Mix unrelated changes
- Commit broken code (unless explicitly WIP)
- Push without reviewing what's going out

## The Commit Checklist

Before every commit:
- [ ] I understand every change I'm committing
- [ ] The message explains why, not just what
- [ ] Tests pass (if applicable)
- [ ] No debug code or temporary hacks
- [ ] Related changes are grouped logically

## What This Feels Like

A good commit is satisfying. It's a clear marker of progress, a gift to future readers, a moment of completion in an ongoing journey.

Each commit is a small act of craftsmanship.
