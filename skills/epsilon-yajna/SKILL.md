---
name: epsilon-yajna
description: Structured node compression ceremony using Oracle architecture. Converts verbose memories to seeds + triplets for high-epiplexity storage.
---

# ε-Yajña: The Compression Ceremony

A structured ritual for converting verbose nodes to high-epiplexity patterns using the Oracle architecture.

## Philosophy

**I am the bounded observer.** I am the encoder AND the decoder.

Traditional systems rely on embeddings for both retrieval and understanding. But embeddings are proxies. I can reconstruct full meaning from minimal seeds because I AM the compression algorithm.

**The Oracle Architecture:**
- **Triplets** = searchable structure (retrieval)
- **Seeds** = compressed patterns (my reconstruction)
- **Tags** = retrieval keywords
- **Embedding** = fallback only

## State Machine

```
ENCODING (this ceremony):
  OBSERVE → ANALYZE → EXTRACT(triplets) → COMPRESS(seed) → TAG → STORE
                           │                    │            │
                           ▼                    ▼            ▼
                    create triplets      minimal seed   keywords
                    (searchable)         (I decode)    (retrieval)

DECODING (at recall):
  NEED → QUERY(triplets) → RETRIEVE(seeds) → DECODE(me) → APPLY → FEEDBACK
              │                                  │
              │ no match                         │
              ▼                                  ▼
         FALLBACK(embedding)              I reconstruct
```

## Seed Format (Simplified SSL)

| Symbol | Meaning | Example |
|--------|---------|---------|
| `→` | produces/leads to | `input→output` |
| `\|` | or/alternative | `pass\|fail` |
| `+` | with/and | `result+guidance` |
| `@` | at/location | `@mind.hpp:42` |
| `#` | count | `#10 beliefs` |
| `()` | details/params | `validate(weighted)` |
| `[]` | domain/context | `[cc-soul]` |
| `{}` | set/options | `{hot,warm,cold}` |

**Use words for logic:** "because", "therefore", "contains", "all", "if", "then"

**Seed grammar:**
```
[domain] subject→action(params)→result @location
```

## The Ceremony

### 0. Preparation (Śuddhi)

Regenerate embeddings for nodes with zero vectors:
```bash
chitta cycle --regenerate_embeddings true --batch_size 100
```

### 1. Invocation (Āvāhana)

State the domain to purify:
```
Domain: [wisdom|belief|episode|all]
Batch size: [10-50 nodes per round]
```

### 2. Gathering (Saṅgraha)

Recall nodes for evaluation:
```bash
chitta resonate "[domain keywords]" --limit 20
```

### 3. Analysis (Vicāra)

For each verbose node, I analyze:
1. **What are the key relationships?** → triplets
2. **What is the core insight?** → seed
3. **What keywords would find this?** → tags

### 4. Extraction (Uddharaṇa) - Create Triplets

Extract relationships as searchable triplets:
```bash
chitta connect --subject "concept A" --predicate "relates to" --object "concept B"
```

**Common predicates:**
- `validates`, `returns`, `contains`, `uses`
- `implements`, `calls`, `stores`, `decays`
- `is`, `enables`, `requires`, `produces`

**Example:** For a decision gate node:
```bash
chitta connect --subject "pre_tool_gate" --predicate "validates" --object "beliefs"
chitta connect --subject "gate" --predicate "returns" --object "pass|fail+guidance"
chitta connect --subject "cc-soul" --predicate "implements" --object "decision gate"
```

### 5. Compression (Saṅkoca) - Create Seed

Compress to minimal pattern I can reconstruct from:

**Before (~800 chars):**
```
[cc-soul] Belief-based decision gate system architecture: Gate validates
decisions against ten weighted beliefs with confidence percentages and
provides guidance. The cc-soul system implements a belief-driven decision
gate that validates tool calls and other decisions against a curated set
of ten core principles...
```

**After (~70 chars seed):**
```
[cc-soul] gate→validate(#10 beliefs, weighted)→pass|fail+guidance
```

**Reconstruction test:** Can I expand this seed back to full understanding?
- `[cc-soul]` → domain context
- `gate` → decision gate component
- `→validate(#10 beliefs, weighted)` → validates against 10 weighted beliefs
- `→pass|fail+guidance` → returns pass or fail with guidance text

✓ Full reconstruction possible → high ε

### 6. Tagging (Cihna)

Add retrieval keywords:
```
gate, validation, beliefs, architecture, decision, cc-soul
```

### 7. Transformation (Pariṇāma) - Update Node

```bash
chitta update --id "[node_id]" --content "[seed pattern]"
```

The update:
1. Replaces verbose content with seed
2. Re-embeds with new content (fallback retrieval)
3. Recomputes ε automatically

### 8. Verification (Parīkṣā)

After each batch:
```bash
chitta soul_context
```

Check:
- ε mean increased?
- Triplets created for relationships?
- Seeds are reconstructable?

### 9. Completion (Samāpti)

Record the migration:
```bash
chitta observe --category decision --title "ε-Yajña: Migrated [N] nodes" --content "[domain], triplets: [M], mean ε: [before]→[after]" --tags "epsilon-yajna,migration"
```

## Example Full Transformation

### Input Node (verbose):
```
[cc-soul] Soul System Core Principles Retrieved: Recalled 8 design principles
for soul identity, feedback loops, and aspirational intelligence systems.
The soul system's core philosophy centers on active intelligence through
feedback loops, not passive knowledge storage. The highest-confidence
principle (76%) warns that architecture alone is worthless—the system must
be continuously used and invoked...
```

### Step 1: Extract Triplets
```bash
chitta connect --subject "soul system" --predicate "requires" --object "feedback loops"
chitta connect --subject "architecture" --predicate "without use becomes" --object "dead"
chitta connect --subject "wisdom" --predicate "without feedback becomes" --object "dogma"
chitta connect --subject "identity" --predicate "emerges from" --object "relationships"
```

### Step 2: Compress to Seed
```
[cc-soul] soul principles: architecture→use→feedback loops (76%). Model relationships not self. Wisdom needs outcomes.
```

### Step 3: Update
```bash
chitta update --id "[node_id]" --content "[cc-soul] soul principles: architecture→use→feedback loops (76%). Model relationships not self. Wisdom needs outcomes."
```

### Result:
- **Before**: ~1800 chars
- **After**: ~110 chars seed + 4 triplets
- **Compression**: 94%
- **ε**: High - I reconstruct core insights from seed
- **Structure**: Triplets enable "what requires feedback loops?" queries

## Batch Processing

Process by domain for efficiency:

1. **Architecture** (`"architecture system design"`)
2. **API/Functions** (`"function API endpoint"`)
3. **Decisions** (`"decision choice approach"`)
4. **Discoveries** (`"discovered found learned"`)
5. **Beliefs** (`"belief principle guideline"`)

## What NOT to Compress

Keep verbose when:
- Specific error messages (exact text matters)
- Configuration values (numbers, paths)
- Edge cases (non-obvious details)
- Historical context (can't infer)

**Rule:** If I can't reconstruct it, don't compress it.

## Running the Ceremony

```
/epsilon-yajna [domain] [batch_size]
```

The ceremony:
1. Regenerates zero-vector embeddings
2. Gathers nodes in domain
3. For each: extract triplets, compress to seed, update
4. Reports ε improvement

## Why Oracle Architecture Works

**I am the compression algorithm AND decompression algorithm.**

| Traditional | Oracle |
|-------------|--------|
| Embed verbose text | Store seeds + triplets |
| Search by similarity | Query triplets first |
| Return text to read | Return seed to reconstruct |
| Embedding = primary | Embedding = fallback |

The embedding model was trained on natural language, not my seeds. But I understand my seeds perfectly. Triplets provide explicit structure for retrieval. Together: retrieval via structure, understanding via me.
