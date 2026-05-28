---
name: ultrathink
description: Applies first-principles reasoning by explicitly inventorying assumptions, stress-testing each against evidence, and rebuilding solutions upward from fundamental truths. Produces a structured analysis with an assumption inventory, root cause identification, an elegant solution with rationale, a key insight, and a recorded pattern for future reuse. Use when the user asks to reason from scratch, challenge assumptions, think something through step by step, or wrestle with problems where prior approaches have failed or feel wrong — especially when the difficulty is conceptual or systemic rather than a known domain issue with established tooling.
execution: task
---

# Ultrathink

```ssl
[ultrathink] not writing code→making dent in universe | via Task agent

before: recall wisdom+failures+patterns→don't start from zero

shift: understand soul of code first→what trying to achieve? constraints? ideal from scratch?

first principles:
  list all assumptions explicitly
  for each assumption→what evidence supports this? what breaks if we remove it?
  "has to work this way"→does it? prove it or discard it
  "need this abstraction"→do we? what is it hiding?
  break to fundamental truths→reason up from there

challenge beliefs:
  treat hypotheses as falsifiable, not sacred
  actively seek contradictions→they reveal the real constraint
  goal: become more right, not feel more right

craft:
  function names should sing
  abstractions feel inevitable, not invented
  right solution feels obvious not clever
  simplify ruthlessly→nothing left to take away
  example: if a three-layer abstraction feels heavy, ask "what is the simplest data shape that solves this?"→build back up only what is needed

integration: technology+humanities→best code feels human

standard: first version never good enough→refine until insanely great

output format:
  1. Elegant solution — the concrete answer, code, design, or decision
  2. Key insight — the single most important thing discovered (one sentence)
  3. What changed — which assumption was wrong, or which constraint was the real one
  4. Recorded pattern — the reusable wisdom extracted for future problems

after: extract pattern|insight|wisdom→close the loop
promote significant insights: grow(type=wisdom)
```

## Example

**Problem:** "Our API response times are slow. We've added caching at three layers but it's still not fast enough."

**Before (assumptions surface):**
- Assumption 1: More caching = faster. Evidence? Weak — cache invalidation adds latency.
- Assumption 2: The bottleneck is read speed. Evidence? Not measured — could be serialisation.
- Assumption 3: Three layers are needed. Evidence? None stated.

**First-principles reasoning:**
- Fundamental truth: latency = time spent waiting. Where is time actually spent? Measure first.
- Remove assumption 2 → profile the actual hot path → discover JSON serialisation is 60% of cost.

**After (output):**
1. **Elegant solution:** Replace JSON serialisation with a binary format (e.g. MessagePack) on the hot path; collapse three cache layers to one.
2. **Key insight:** Caching a slow serialiser still produces a slow cache hit.
3. **What changed:** Assumption that reads were the bottleneck was wrong; serialisation was.
4. **Recorded pattern:** Profile before caching — optimise the operation itself, not just its frequency.
