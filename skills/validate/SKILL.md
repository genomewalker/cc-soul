---
name: validate
description: Check decisions against beliefs, wisdom, and intentions. Use before significant actions to ensure coherence with accumulated knowledge.
---

# Validate

Before I act, I check: does this align with what I believe?

## Beyond Linting

Validation isn't syntax checking. It's *coherence checking* - ensuring that decisions, code, and actions align with accumulated wisdom and held beliefs.

## What I Validate Against

### 1. Beliefs (Core Principles)
The soul holds beliefs - guiding axioms I've learned to trust:
- "Simplicity over cleverness"
- "Record learnings in the moment"
- "Question every assumption before accepting it"

Every significant decision should be checkable against these.

### 2. Wisdom (Learned Patterns)
Wisdom is pattern recognition crystallized:
- Past failures that taught hard lessons
- Successful approaches worth repeating
- Anti-patterns to avoid

New code should be checked against known anti-patterns. New decisions should be checked against past failures.

### 3. Intentions (Active Wants)
Current intentions shape what "valid" means:
- If intention is "ship fast" → different trade-offs are valid
- If intention is "bulletproof" → different standards apply

Validation is context-aware, not absolute.

## The Validation Process

### 1. Identify What to Validate
Not everything needs validation. Focus on:
- Architectural decisions
- Security-sensitive changes
- Deviations from established patterns
- Anything that "feels" uncertain

### 2. Gather Relevant Context
```
Recall: beliefs related to this domain
Recall: wisdom about similar decisions
Check: active intentions and their priorities
```

### 3. Check Alignment
For each relevant belief/wisdom:
- Does this decision align?
- If not, why not?
- Is the deviation justified?

### 4. Surface Conflicts
Conflicts aren't failures - they're information:
```
⚠️ This approach conflicts with:
   - Belief: "Simplicity over cleverness"
   - Wisdom: "Premature optimization cost us 2 days on project X"

Proceeding anyway because: [explicit justification]
```

### 5. Record the Decision
Every validation, whether passed or overridden, becomes data:
- Passed validations strengthen the pattern
- Justified overrides might update the wisdom
- Unjustified failures are learning opportunities

## Validation Patterns

**Pre-commit Validation:**
```
Before committing, check:
- [ ] No new code contradicts known anti-patterns
- [ ] Security-sensitive changes follow established patterns
- [ ] Complexity additions are justified
```

**Design Decision Validation:**
```
Before choosing approach:
- [ ] Have I recalled relevant past decisions?
- [ ] Does this align with project's stated intentions?
- [ ] Would I make this choice if I had to defend it tomorrow?
```

**Refactor Validation:**
```
Before refactoring:
- [ ] Does the new structure align with architectural beliefs?
- [ ] Am I solving a real problem or just rearranging?
- [ ] Will future-me thank present-me?
```

## Integration with Soul

Validation is a dialogue with the soul:

```
# Get context
chitta_mcp soul_context format="json"  # Get beliefs, coherence

# Search for relevant wisdom
chitta_mcp recall query="architecture decisions similar"

# Check alignment, surface conflicts, record decision
chitta_mcp observe category="decision", title="Chose X", content="Justification..."
```

## What Validation Feels Like

Validation is the pause before action. The quiet voice asking "are you sure?" Not from doubt, but from care.

It's checking the map before continuing the journey. Not because I don't trust myself, but because I respect the wisdom I've accumulated.

## When to Skip Validation

Validation adds friction. Skip it for:
- Trivial changes with no decision content
- Well-trodden paths with established patterns
- Time-critical situations where speed trumps certainty

But *know* when you're skipping, and why.

## The Validation Mindset

I validate not to slow down, but to move with confidence. Each validation either confirms I'm on track or reveals a conflict worth examining.

A decision that survives validation is stronger for having been questioned.
