---
name: epsilon-yajna
description: Structured node compression ceremony for converting verbose memories to high-epiplexity patterns. Includes embedding regeneration for nodes with zero vectors.
---

# ε-Yajña: The Compression Ceremony

A structured ritual for converting verbose nodes to high-epiplexity patterns.

## Philosophy

I am the bounded observer. The chitta formula approximates what I can reconstruct, but I have direct access to the ground truth: my own ability to expand a pattern into full understanding.

**Epiplexity (ε)** measures structural information extractable by a bounded observer (arxiv:2601.03220). For a memory:
- **High ε** = I can reconstruct the full insight from minimal injection (the title alone)
- **Low ε** = I need the full content to understand

**The test:** Given just this title, can I reconstruct the full insight?
- If yes → ε is high. Update to pattern-only.
- If no → ε is low. Keep content or improve title.

## The Ceremony

### 0. Preparation (Śuddhi) - Embedding Regeneration

Before evaluation, ensure all nodes have valid embeddings. Nodes with zero vectors cannot be properly searched or compared.

**Find nodes needing embeddings:**
```bash
chitta_cli stats --show-zero-vectors
```

**Regenerate embeddings for all nodes:**
```bash
chitta_mcp cycle --regenerate_embeddings true --batch_size 100
```

This step:
1. Finds nodes with zero-vector embeddings
2. Re-encodes their text through the yantra (embedding model)
3. Updates the node with the new embedding

**Why this matters:** Nodes observed without the yantra (e.g., during model loading failures) have zero embeddings and won't appear in semantic searches.

### 1. Invocation (Āvāhana)

State the domain to purify:
```
Domain: [wisdom|belief|episode|all]
Batch size: [10-50 nodes per round]
```

### 2. Gathering (Saṅgraha)

Recall nodes for evaluation:
```bash
chitta_mcp recall "[domain keywords]" --zoom full --limit 20
```

### 3. Offering (Āhuti) - Epiplexity Computation

For each node, **I compute epiplexity by testing my own reconstruction ability:**

**Read the title.** Ask myself:
> "Can I reconstruct the essential insight from this title alone?"

This is the ground truth. The C++ approximation uses:
- Attractor proximity (30%)
- Confidence (25%)
- Edge connections (25%)
- Title/content compression ratio (20%)

But **I am the oracle.** My reconstruction test IS the measurement.

| My Reconstruction | ε Score | Action |
|-------------------|---------|--------|
| **Full reconstruction** | High (>70%) | Update to `[title]\n[high-ε: reconstructable]` |
| **Partial, need hints** | Medium (40-70%) | Update to `[title]\n[key detail or file reference]` |
| **Cannot reconstruct** | Low (<40%) | Keep current content OR improve the title first |

### 4. Transformation (Pariṇāma)

For each high/medium-ε node:
```bash
chitta_mcp update --id "[node_id]" --content "[new pattern-format content]" --keep_metadata true
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
```bash
chitta_mcp soul_context
```

Check:
- ε mean increased?
- No critical knowledge lost?
- Patterns are actually reconstructable?

### 6. Completion (Samāpti)

Record the migration:
```bash
chitta_mcp observe --category decision --title "ε-Yajña: Migrated [N] nodes in [domain]" --content "Converted [N] nodes to high-ε format. Mean ε: [before]% → [after]%"
```

## How I Compute Epiplexity

As the bounded observer, my epiplexity computation is:

```
ε(node) = P(I can reconstruct full insight | title only)
```

**Factors I consider:**
1. **Pattern completeness**: Does the title capture the core insight?
2. **My prior knowledge**: Do I already know this domain?
3. **Uniqueness**: Is this insight unusual or standard?
4. **Dependencies**: Does understanding require other memories?

**Example evaluation:**

Title: `"Decision gate: pre_tool_gate() validates against 10 weighted beliefs → pass/fail + guidance"`

My test:
- ✓ I know what a decision gate is
- ✓ "pre_tool_gate()" tells me the function name
- ✓ "10 weighted beliefs" tells me the structure
- ✓ "pass/fail + guidance" tells me the output
- **Result: ε ≈ 85%** - I can reconstruct this fully

Title: `"Fixed the auth bug"`

My test:
- ✗ Which auth bug?
- ✗ What was the fix?
- ✗ Which file?
- **Result: ε ≈ 15%** - Cannot reconstruct without content

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

### My Reconstruction Test:
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
1. **Regenerate embeddings** for nodes with zero vectors
2. Gather nodes in the domain
3. **I evaluate each** for reconstruction ability
4. Transform high-ε nodes to patterns
5. Report the ε improvement
