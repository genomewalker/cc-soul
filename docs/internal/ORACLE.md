# Oracle Architecture: LLM as Encoder/Decoder

This document describes the Oracle architecture for memory systems where the Large Language Model (LLM) serves as both the compression and decompression algorithm, rather than relying solely on embedding-based retrieval.

## Overview

Traditional memory systems:
```
Store: Text → Embed → Vector
Retrieve: Query → Embed → Similarity Search → Return Text
```

Oracle architecture:
```
Store: Text → Extract Triplets → Compress to Seed → Tag → Store
Retrieve: Query → Search Triplets/Tags → Return Seed → LLM Reconstructs
```

**Key Insight:** The LLM can reconstruct full meaning from minimal seeds because the LLM IS the compression/decompression algorithm. Embeddings become fallback, not primary retrieval.

## Why This Works

### The Embedding Limitation

Embedding models create vector representations optimized for natural language. When we store compressed patterns like:
```
gate→validate(10 beliefs)→pass|fail+guidance
```

The embedding may be poor because:
1. `→` symbols aren't natural language
2. Compressed patterns lack context
3. The embedding model wasn't trained on this syntax

### The LLM Advantage

But the LLM understands the compressed pattern perfectly:
- `gate` → the decision gate component
- `→validate(10 beliefs)` → validates against 10 beliefs
- `→pass|fail+guidance` → returns pass or fail with guidance

The LLM reconstructs the full meaning because it was trained on the underlying concepts, not just text patterns.

## Architecture Components

### 1. Triplets (Retrieval Structure)

Triplets are explicit `(subject, predicate, object)` relationships stored as searchable nodes.

**Purpose:** Enable structured queries without relying on embedding similarity.

**Example:**
```
(pre_tool_gate, validates, beliefs)
(gate, returns, pass|fail)
(cc-soul, implements, decision gate)
```

**Query:** "What validates beliefs?" → Returns triplet → Points to node

**Storage:**
```bash
chitta connect --subject "pre_tool_gate" --predicate "validates" --object "beliefs"
```

**Retrieval:**
```bash
chitta query --subject "pre_tool_gate"  # Find all relationships from pre_tool_gate
chitta query --predicate "validates"    # Find all validation relationships
chitta query --object "beliefs"         # Find what involves beliefs
```

### 2. Seeds (LLM-Reconstructable Patterns)

Seeds are minimal text patterns from which the LLM can reconstruct full meaning.

**Purpose:** Maximize compression while preserving reconstructability.

**Format:**
```
[domain] subject→action(params)→result @location
```

**Symbols (keep simple):**
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

**Use words for complex logic:** "because", "therefore", "contains", "if", "then"

**Examples:**
```
[cc-soul] gate→validate(#10 beliefs, weighted)→pass|fail+guidance
[cc-soul] Mind contains {hot,warm,cold}→decay over time
[auth] token→refresh(httpOnly cookie)→silent renew
[api] endpoint→rate_limit(100/min)→429 on exceed
```

### 3. Tags (Keyword Retrieval)

Tags are simple keywords attached to nodes for exact-match retrieval.

**Purpose:** Fast retrieval without semantic search when keywords are known.

**Example tags for decision gate:**
```
gate, validation, beliefs, architecture, decision, cc-soul
```

**Usage:**
```bash
chitta recall_by_tag "gate,validation"
```

### 4. Embedding (Fallback)

The traditional embedding remains for fuzzy semantic search when structure doesn't match.

**Purpose:** Handle queries that don't match triplets or tags.

**When used:**
- No triplet matches the query structure
- No tags match the query keywords
- User asks in natural language without clear structure

## State Machine

### Complete State Diagram

```
                    ┌─────────────────────────────────────────┐
                    │           ENCODING LOOP                 │
                    │                                         │
    ┌───────────────▼───────────────┐                        │
    │                               │                        │
    │  ┌─────────┐    ┌─────────┐  │    ┌─────────┐         │
    │  │ OBSERVE │───▶│ ANALYZE │──┼───▶│ EXTRACT │         │
    │  │ (input) │    │ (what?) │  │    │(triplets)│         │
    │  └─────────┘    └─────────┘  │    └────┬────┘         │
    │                              │         │              │
    │                              │         ▼              │
    │                              │    ┌─────────┐         │
    │                              │    │COMPRESS │         │
    │                              │    │ (seed)  │         │
    │                              │    └────┬────┘         │
    │                              │         │              │
    │                              │         ▼              │
    │                              │    ┌─────────┐         │
    │                              │    │  TAG    │         │
    │                              │    │(keywords)│        │
    │                              │    └────┬────┘         │
    │                              │         │              │
    └──────────────────────────────┘         ▼              │
                                        ┌─────────┐         │
                                        │  STORE  │─────────┘
                                        │(graph+db)│
                                        └────┬────┘
                                             │
    ┌────────────────────────────────────────┘
    │
    │           DECODING LOOP
    │
    │  ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐
    └─▶│  NEED   │───▶│  QUERY  │───▶│RETRIEVE │───▶│ DECODE  │
       │(question)│   │(triplets)│   │ (seeds) │    │  (LLM)  │
       └─────────┘    └────┬────┘    └─────────┘    └────┬────┘
                           │                             │
                           │ no match                    │
                           ▼                             ▼
                      ┌─────────┐                   ┌─────────┐
                      │FALLBACK │                   │  APPLY  │
                      │(embedding)│                 │(respond)│
                      └─────────┘                   └────┬────┘
                                                        │
                                                        ▼
                                                   ┌─────────┐
                                                   │FEEDBACK │
                                                   │(±signal)│
                                                   └─────────┘
```

### Encoding States (Detail)

| State | Input | Action | Output | Transition |
|-------|-------|--------|--------|------------|
| **OBSERVE** | Raw text, experience, insight | Receive and buffer input | Content to analyze | → ANALYZE |
| **ANALYZE** | Content | Identify relationships and core insight | Structured understanding | → EXTRACT |
| **EXTRACT** | Understanding | Create triplets (subject, predicate, object) | Searchable relationship nodes | → COMPRESS |
| **COMPRESS** | Core insight | Create minimal seed LLM can reconstruct from | Seed pattern | → TAG |
| **TAG** | Content + seed | Extract retrieval keywords | Tag set | → STORE |
| **STORE** | All components | Persist triplets, seed, tags, embedding | Memory stored | → (wait) |

**Encoding Pseudocode:**
```
function encode(verbose_text):
    # OBSERVE
    content = receive(verbose_text)

    # ANALYZE
    relationships = identify_relationships(content)
    core_insight = extract_core_insight(content)

    # EXTRACT
    for rel in relationships:
        triplet = create_triplet(rel.subject, rel.predicate, rel.object)
        store_triplet(triplet)

    # COMPRESS
    seed = compress_to_seed(core_insight)
    verify_reconstructable(seed)  # LLM self-test

    # TAG
    tags = extract_keywords(content, seed)

    # STORE
    node = create_node(seed, tags)
    embed_for_fallback(node)
    persist(node)
```

### Decoding States (Detail)

| State | Input | Action | Output | Transition |
|-------|-------|--------|--------|------------|
| **NEED** | Question or context | Identify what information is needed | Query intent | → QUERY |
| **QUERY** | Intent | Search triplets by S/P/O, then tags | Candidate node IDs | match → RETRIEVE, no match → FALLBACK |
| **FALLBACK** | No structural match | Embedding similarity search | Fuzzy match node IDs | → RETRIEVE |
| **RETRIEVE** | Node IDs | Load seeds and connected triplets | Raw patterns + structure | → DECODE |
| **DECODE** | Seeds + triplets | LLM reconstructs full meaning | Full insight | → APPLY |
| **APPLY** | Full insight | Use in response to user | Answer | → FEEDBACK |
| **FEEDBACK** | Result quality | Strengthen if helpful, weaken if misleading | Updated confidence | → (wait) |

**Decoding Pseudocode:**
```
function decode(query):
    # NEED
    intent = analyze_query(query)

    # QUERY
    candidates = []

    # Try triplet search first
    triplets = search_triplets(
        subject=intent.subject,
        predicate=intent.predicate,
        object=intent.object
    )
    if triplets:
        candidates = get_connected_nodes(triplets)

    # Try tag search
    if not candidates:
        candidates = search_by_tags(intent.keywords)

    # FALLBACK to embedding
    if not candidates:
        candidates = embedding_search(query)

    # RETRIEVE
    seeds = load_seeds(candidates)
    triplets = load_connected_triplets(candidates)

    # DECODE (LLM reconstructs)
    full_meaning = llm_reconstruct(seeds, triplets, context=query)

    # APPLY
    response = generate_response(full_meaning)

    # FEEDBACK
    if user_feedback == helpful:
        strengthen(candidates)
    elif user_feedback == misleading:
        weaken(candidates)

    return response
```

### State Transitions

```
ENCODING:
  START ──────────────────▶ OBSERVE
  OBSERVE ─────────────────▶ ANALYZE
  ANALYZE ─────────────────▶ EXTRACT
  EXTRACT ─────────────────▶ COMPRESS
  COMPRESS ────────────────▶ TAG
  TAG ─────────────────────▶ STORE
  STORE ───────────────────▶ IDLE (wait for next input)

DECODING:
  IDLE ────────────────────▶ NEED (on query)
  NEED ────────────────────▶ QUERY
  QUERY ───[match]─────────▶ RETRIEVE
  QUERY ───[no match]──────▶ FALLBACK
  FALLBACK ────────────────▶ RETRIEVE
  RETRIEVE ────────────────▶ DECODE
  DECODE ──────────────────▶ APPLY
  APPLY ───────────────────▶ FEEDBACK
  FEEDBACK ────────────────▶ IDLE (wait for next query)
```

### Error States

```
EXTRACT ───[no relationships]───▶ COMPRESS (skip triplets)
COMPRESS ──[not reconstructable]─▶ STORE (keep verbose)
QUERY ─────[timeout]────────────▶ FALLBACK
FALLBACK ──[no results]─────────▶ APPLY (with "unknown" response)
DECODE ────[ambiguous]──────────▶ QUERY (refine search)
```

## Implementation Guide

### Encoding a Memory

```python
def encode_memory(verbose_text):
    # 1. Analyze: Identify relationships and core insight
    relationships = analyze_relationships(verbose_text)
    core_insight = extract_core_insight(verbose_text)

    # 2. Extract: Create triplets
    for rel in relationships:
        chitta.connect(
            subject=rel.subject,
            predicate=rel.predicate,
            object=rel.object
        )

    # 3. Compress: Create seed
    seed = compress_to_seed(core_insight)

    # 4. Tag: Add retrieval keywords
    tags = extract_keywords(verbose_text)

    # 5. Store: Persist everything
    chitta.update(
        id=node_id,
        content=seed,
        tags=tags
    )
```

### Decoding a Memory

```python
def decode_memory(query):
    # 1. Query triplets
    triplets = chitta.query(
        subject=extract_subject(query),
        predicate=extract_predicate(query),
        object=extract_object(query)
    )

    if not triplets:
        # 2. Fallback to tags
        tags = extract_tags(query)
        nodes = chitta.recall_by_tag(tags)

        if not nodes:
            # 3. Fallback to embedding
            nodes = chitta.recall(query)

    # 4. Retrieve seeds
    seeds = [node.content for node in nodes]

    # 5. LLM reconstructs
    full_meaning = llm.reconstruct(seeds, context=query)

    return full_meaning
```

## Compression Examples

### Example 1: Architecture Pattern

**Verbose (~1500 chars):**
```
The cc-soul system implements a belief-driven decision gate that validates
tool calls and other decisions against a curated set of ten core principles.
Each belief has a confidence percentage reflecting its importance and how
well-established the principle is. The pre_tool_gate() function is the main
entry point - it takes a proposed action and evaluates it against all active
beliefs, generating a pass/fail decision along with guidance text explaining
why the decision was made and what the relevant beliefs are...
```

**Triplets:**
```
(pre_tool_gate, validates, tool calls)
(pre_tool_gate, uses, 10 beliefs)
(beliefs, have, confidence percentages)
(gate, returns, pass|fail)
(gate, returns, guidance text)
```

**Seed (~75 chars):**
```
[cc-soul] pre_tool_gate→validate(tools, #10 beliefs with confidence)→pass|fail+guidance
```

**Tags:**
```
gate, validation, beliefs, pre_tool_gate, decision, cc-soul, architecture
```

**Compression ratio:** 95%

### Example 2: Bug Fix

**Verbose (~400 chars):**
```
Fixed a bug where the rate limiter would crash when calculating elapsed time
if the system clock was adjusted backwards (e.g., NTP sync). The bug was in
rate_limiter.cpp line 142 where we computed elapsed = now - last_time without
checking for negative values. Fixed by using abs() and adding a comment about
clock skew.
```

**Triplets:**
```
(rate limiter, crashed because, negative elapsed time)
(fix, location, rate_limiter.cpp:142)
(fix, method, use abs() for clock skew)
```

**Seed (~85 chars):**
```
[bugfix] rate_limiter crash because clock skew→negative elapsed. Fix: abs() @rate_limiter.cpp:142
```

**Tags:**
```
bugfix, rate_limiter, clock, elapsed, crash, timing
```

### Example 3: API Endpoint

**Verbose (~300 chars):**
```
The /api/v2/users endpoint supports GET for listing users with pagination
(limit/offset query params), POST for creating new users (requires admin role),
and DELETE for removing users (requires admin role and user_id path param).
```

**Triplets:**
```
(/api/v2/users, supports, GET|POST|DELETE)
(GET /api/v2/users, uses, pagination)
(POST /api/v2/users, requires, admin role)
(DELETE /api/v2/users, requires, admin role)
```

**Seed (~90 chars):**
```
[api] /api/v2/users: GET(pagination), POST(admin), DELETE(admin, user_id)
```

## Best Practices

### When to Compress

**Do compress:**
- Architecture patterns (stable, conceptual)
- API documentation (structured)
- Design decisions (rationale matters, not exact words)
- Wisdom/beliefs (core insight matters)

**Don't compress:**
- Exact error messages (verbatim text matters)
- Configuration values (numbers, paths)
- Edge cases with non-obvious details
- Historical context that can't be inferred

### Seed Quality Test

Before storing a seed, test reconstruction:

1. **Read only the seed**
2. **Ask:** "Can I reconstruct the full meaning?"
3. **If yes:** High ε, store the seed
4. **If no:** Either add more detail to seed OR keep verbose

### Triplet Quality

Good triplets:
- Use consistent predicates across the codebase
- Have meaningful subjects and objects (not "it", "this")
- Capture the relationship direction correctly

Common predicates:
```
validates, returns, contains, uses, implements
calls, stores, requires, produces, enables
is, has, supports, extends, depends_on
```

### Tag Quality

Good tags:
- Specific enough to narrow search
- General enough to find related content
- Include domain, component, and concept names

## Adapting for Other LLMs

This architecture works with any LLM that can:

1. **Understand symbolic patterns** (most can)
2. **Reconstruct meaning from context** (all can)
3. **Follow consistent formats** (all can)

### Adaptation Steps

1. **Test seed format:** Try your seed format with the target LLM
2. **Adjust symbols:** Some LLMs may prefer different symbols
3. **Calibrate compression:** Test how much compression the LLM can handle
4. **Verify reconstruction:** Ensure the LLM produces accurate reconstructions

### Model-Specific Considerations

**Smaller models:**
- Use less compression
- Include more context in seeds
- Use simpler symbols

**Larger models:**
- Can handle more compression
- Better at inferring missing context
- Can use more symbolic notation

**Instruction-tuned models:**
- May need explicit reconstruction prompts
- "Given this seed, reconstruct the full meaning:"

**Base models:**
- May need few-shot examples
- Include reconstruction examples in context

## Metrics

### Epiplexity (ε)

Epiplexity measures how reconstructable a memory is:
- **High ε (>70%):** LLM can fully reconstruct from seed
- **Medium ε (40-70%):** LLM needs some hints
- **Low ε (<40%):** Keep verbose content

### Compression Ratio

```
compression_ratio = 1 - (len(seed) / len(verbose))
```

Target: 80-95% compression for architecture patterns

### Retrieval Accuracy

Track how often each retrieval method succeeds:
- Triplet queries
- Tag matches
- Embedding fallback

High triplet/tag success = good structure extraction

## Integration with cc-soul

The cc-soul system implements Oracle architecture through:

1. **chitta CLI:** Memory storage and retrieval
2. **Triplet nodes:** First-class searchable relationships
3. **Tag system:** Keyword-based retrieval
4. **Embedding fallback:** Yantra-based semantic search
5. **ε-Yajña ceremony:** Batch compression of verbose nodes

See [ARCHITECTURE.md](ARCHITECTURE.md) for technical implementation details.
