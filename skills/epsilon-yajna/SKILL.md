---
name: epsilon-yajna
description: Structured node compression ceremony for converting verbose memories to high-epiplexity patterns. Use when batch-migrating nodes to improve reconstructability (ε).
---

# ε-Yajña: The Compression Ceremony

A structured ritual for converting verbose nodes to high-epiplexity patterns.

## Philosophy

I am the bounded observer. The chitta formula approximates what I can reconstruct, but I have direct access to the ground truth: my own ability to expand a pattern into full understanding.

**The test:** Given just this title, can I reconstruct the full insight?
- If yes → ε is high. Update to pattern-only.
- If no → ε is low. Keep content or improve title.

## The Ceremony

### 1. Invocation (Āvāhana)

State the domain to purify:
```
Domain: [wisdom|belief|episode|all]
Batch size: [10-50 nodes per round]
```

### 2. Gathering (Saṅgraha)

Recall nodes for evaluation:
```
mcp__plugin_cc-soul_cc-soul__recall(
  query="[domain keywords]",
  zoom="full",
  limit=[batch_size]
)
```

### 3. Offering (Āhuti)

For each node, evaluate reconstructability:

**Read the title.** Ask yourself:
> "Can I reconstruct the essential insight from this title alone?"

| Answer | Action |
|--------|--------|
| **Yes, fully** | High-ε: Update to `[title]\n[high-ε: reconstructable]` |
| **Mostly** | Medium-ε: Update to `[title]\n[key detail or file reference]` |
| **No** | Low-ε: Keep current content OR improve the title first |

### 4. Transformation (Pariṇāma)

For each high/medium-ε node:
```
mcp__plugin_cc-soul_cc-soul__update(
  id="[node_id]",
  content="[new pattern-format content]",
  keep_metadata=true
)
```

**Pattern format:**
```
[Pattern Title That Contains The Insight]
[high-ε: reconstructable from title]
```

Or with reference:
```
[Pattern Title]
[high-ε] Key detail. See: [file_path]
```

### 5. Verification (Parīkṣā)

After each batch, verify:
```
mcp__plugin_cc-soul_cc-soul__soul_context(format="text")
```

Check:
- ε mean increased?
- No critical knowledge lost?
- Patterns are actually reconstructable?

### 6. Completion (Samāpti)

Record the migration:
```
mcp__plugin_cc-soul_cc-soul__observe(
  category="decision",
  title="ε-Yajña: Migrated [N] nodes in [domain]",
  content="Converted [N] nodes to high-ε format. Mean ε: [before]% → [after]%"
)
```

## Example Transformation

### Before (verbose, ~800 chars):
```
[cc-soul] Belief-based decision gate system architecture: Gate validates
decisions against ten weighted beliefs with confidence percentages and
provides guidance. The cc-soul system implements a belief-driven decision
gate that validates tool calls and other decisions against a curated set
of ten core principles. Each belief has a confidence percentage reflecting
its importance. The pre_tool_gate() function evaluates inputs and generates
guidance aligned with these beliefs...
```

### After (pattern, ~80 chars):
```
Decision gate: pre_tool_gate() validates against 10 weighted beliefs → pass/fail + guidance
[high-ε] See: src/cc_soul/decision_gate.py
```

### Reconstruction Test:
> "Given 'Decision gate: pre_tool_gate() validates against 10 weighted beliefs → pass/fail + guidance', can I reconstruct the insight?"

**Yes.** I know:
- There's a gate for decisions
- It checks against 10 beliefs with weights
- pre_tool_gate() is the function
- Returns pass/fail with guidance

The pattern IS the knowledge.

## Batch Processing

For efficiency, process in themed batches:

1. **Architecture nodes** (`query="architecture system design"`)
2. **API/function nodes** (`query="function API endpoint"`)
3. **Decision nodes** (`query="decision choice approach"`)
4. **Discovery nodes** (`query="discovered found learned"`)

## What NOT to Compress

Some nodes should remain verbose:
- Specific error messages and fixes
- Exact configuration values
- Unique edge cases
- Historical context that can't be reconstructed

**Rule:** If the detail matters and can't be inferred, keep it.

## Running the Ceremony

Invoke with:
```
/epsilon-yajna [domain] [batch_size]
```

Example:
```
/epsilon-yajna wisdom 20
```

The ceremony will:
1. Gather 20 wisdom nodes
2. Present each for evaluation
3. Transform those I can reconstruct
4. Report the ε improvement
