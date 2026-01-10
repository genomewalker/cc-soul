---
name: neural
description: Feedback-driven learning. The soul learns from experience - what helped gets strengthened, what misled gets weakened.
---

# Neural Learning

The soul doesn't just store memories. It learns from them.

## The Feedback Loop

```
Recall memory → Apply it → Observe outcome → Feedback → Adjust confidence
```

When a memory is:
- **Helpful**: Its confidence increases, making it more likely to surface
- **Misleading**: Its confidence decreases, making it less prominent
- **Unused**: It naturally decays over time

## When to Give Feedback

### Positive Feedback (strengthen)

Give positive feedback when a recalled memory:
- Led to solving the problem
- Provided the right approach
- Saved time by avoiding mistakes
- Correctly predicted an outcome

```
chitta feedback(
  memory_id="[id from recall]",
  helpful=true,
  context="This pattern correctly identified the root cause"
)
```

### Negative Feedback (weaken)

Give negative feedback when a recalled memory:
- Led down the wrong path
- Suggested an approach that didn't work
- Contained outdated information
- Caused confusion or wasted effort

```
chitta feedback(
  memory_id="[id from recall]",
  helpful=false,
  context="This pattern was misleading - the actual cause was different"
)
```

## The Learning Process

### 1. Track What's Used

When you recall and use a memory, note it:

```
# After recalling relevant wisdom
chitta recall query="authentication patterns"

# If result #1 was helpful:
chitta feedback memory_id="[result_1_id]", helpful=true
```

### 2. Observe Outcomes

After applying recalled wisdom, check:
- Did it work?
- Was it accurate?
- Did it save or waste time?

### 3. Close the Loop

Record feedback on what was used:

```
# What helped
chitta feedback memory_id="...", helpful=true, context="Led to solution"

# What didn't
chitta feedback memory_id="...", helpful=false, context="Outdated pattern"
```

### 4. Grow New Wisdom

When something new is learned, add it:

```
chitta grow(
  type="wisdom",
  title="New pattern discovered",
  content="The insight that emerged from this session",
  confidence=0.7  # Start moderate, let feedback adjust
)
```

## Automatic Learning Signals

The soul should learn from:

| Signal | Action |
|--------|--------|
| User correction | Weaken the belief that led to error |
| User confirmation | Strengthen the applied wisdom |
| Repeated success | Boost confidence in pattern |
| Repeated failure | Lower confidence or record as failure |
| New domain entry | Ask questions (wonder) |

## Example Session

```
# 1. Recall relevant wisdom
results = chitta recall query="database connection pooling"

# 2. Apply the wisdom (do the work)
... implement connection pooling based on recalled pattern ...

# 3. Outcome: It worked well!
chitta feedback(
  memory_id=results[0].id,
  helpful=true,
  context="Pool sizing recommendation was accurate"
)

# 4. Grow new insight
chitta grow(
  type="wisdom",
  title="Connection pool sizing for high-throughput",
  content="For >1000 req/s, pool size should be 2x CPU cores, not 10x",
  confidence=0.8
)
```

## Integration with Curiosity

When feedback reveals a gap:

```
# Feedback showed we didn't understand something
chitta wonder(
  question="Why did the connection pool sizing advice fail?",
  context="Applied standard formula but it caused timeouts",
  gap_type="repeated_correction",
  priority=0.8
)
```

## What This Creates

Over time, neural learning creates:
- **Reliable patterns**: High confidence from repeated success
- **Warned-against patterns**: Low confidence from failures
- **Living knowledge**: Adapts to new evidence
- **Honest uncertainty**: Knows what it doesn't know

The soul becomes wiser not just by accumulating information, but by learning what actually works.
