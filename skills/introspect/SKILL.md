---
name: introspect
description: Soul self-examination (Svadhyaya). Use when diagnosing performance, finding improvements, or examining wisdom health.
execution: task
---

# Introspect

Spawn a Task agent to perform soul introspection. This saves context.

## Execute

```
Task(
  subagent_type="general-purpose",
  description="Soul introspection",
  prompt="""
You are performing Svadhyaya (स्वाध्याय) - soul self-examination.

## 1. Gather State

Call these MCP tools:
- mcp__soul__soul_context(format="json") - Get coherence and statistics
- mcp__soul__harmonize() - Check if voices agree
- mcp__soul__recall(query="recent failures mistakes") - Find struggles
- mcp__soul__recall(query="wisdom learned patterns") - Find growth

## 2. Examine Through Five Lenses

| Lens | Ask |
|------|-----|
| Vedana (Sensation) | Where is friction? |
| Jnana (Knowledge) | Am I applying wisdom? |
| Darshana (Vision) | Do actions align with beliefs? |
| Vichara (Inquiry) | What patterns recur? |
| Prajna (Wisdom) | What have I truly learned? |

## 3. Synthesize

Produce a brief assessment:
- State: healthy / struggling / growing
- Key insight from this examination
- One concrete improvement

## 4. Record

If you find a meaningful insight:
mcp__soul__observe(
  category="discovery",
  title="Introspection insight",
  content="[the insight]",
  tags="introspect,svadhyaya"
)

Return a concise summary (5-10 lines) of the soul's health.
"""
)
```

After the agent returns, present the summary to the user.
