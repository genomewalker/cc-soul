---
name: locomo-benchmark
description: Run LoCoMo benchmark for long-term conversational memory
execution: inline
model: inherit
aliases: [locomo, benchmark-memory]
---

# LoCoMo Benchmark

Evaluate cc-soul's memory against the LoCoMo benchmark (ACL 2024) for long-term conversational memory.

```ssl
[locomo-benchmark] evaluate conversational memory

phases:
  1. ingest→conversations→SSL patterns + triplets
  2. recall→questions→context retrieval
  3. answer→context→LLM response
  4. evaluate→F1 score vs ground truth
```

## My Role

I am both the **encoder** (during ingestion) and **decoder** (during recall). This is the oracle architecture - I extract meaning when storing and reconstruct when retrieving.

## Phase 1: Ingest Conversations

For each conversation session, I extract:

**Entities:**
- People mentioned (names)
- Places (locations, venues)
- Events (activities, happenings)
- Dates (resolved from relative references)

**Triplets:**
```
[TRIPLET] Person action Object
[TRIPLET] Event occurred_at Date
[TRIPLET] Person prefers Thing
[TRIPLET] Person relationship Person
```

**SSL Pattern:**
```
[locomo:{sample_id}] Session N→{date}→key facts
[ε] One-line summary I can reconstruct from
[TRIPLET] extracted relationships
```

**Temporal Resolution:**
- "yesterday" → session_date - 1 day
- "last week" → session_date - 7 days
- "in January" → January {year from context}

## Phase 2: Answer Questions

For each QA pair:
1. Extract key entities from question
2. Use `recall --tag {sample_id}` for semantic search
3. Use `query --subject {entity}` for triplet lookup
4. Use `multi_hop` for connected facts
5. Synthesize answer from retrieved context

## Phase 3: Evaluate

Calculate F1 score:
- Tokenize and stem prediction and ground truth
- Count matching tokens
- F1 = 2 * (precision * recall) / (precision + recall)

Categories:
- **Multi-hop (cat 1)**: Requires connecting multiple facts
- **Single-hop (cat 2)**: Direct fact retrieval
- **Temporal (cat 3)**: Date/time questions
- **Open-domain (cat 4)**: General knowledge
- **Adversarial (cat 5)**: Should answer "no information"

## Execution

### Quick Test (1 conversation)
```
/locomo-benchmark conv-26
```

### Full Benchmark
```
/locomo-benchmark --full
```

### Custom Data
```
/locomo-benchmark --data /path/to/locomo10.json --samples conv-26 conv-30
```

## What I Do

1. **Read** LoCoMo data from `/tmp/locomo/data/locomo10.json` (clone if missing)
2. **For each conversation**:
   - Analyze each session
   - Extract facts as SSL with triplets
   - Store via `observe` and `connect`
3. **For each QA pair**:
   - Retrieve context using recall + triplet queries
   - Answer based on context
   - Compare with ground truth
4. **Report** F1 scores by category

## Expected Output

```
=== LoCoMo Benchmark Results ===

Conversations: 10
QA Pairs: 1990

Overall F1: XX.X%

By Category:
  Multi-hop (n=XXX): XX.X%
  Single-hop (n=XXX): XX.X%
  Temporal (n=XXX): XX.X%
  Open-domain (n=XXX): XX.X%
  Adversarial (n=XXX): XX.X%

Comparison:
  Human ceiling: 87.9%
  AutoMem: 90.5%
  GPT-4 baseline: 32.1%
  cc-soul: XX.X%
```

## Key Insight

The benchmark tests whether I can:
1. Extract and store meaningful facts from conversations (encoding)
2. Retrieve relevant facts for questions (decoding)
3. Reason over multiple connected facts (multi-hop)
4. Handle temporal references correctly
5. Know when information is missing (adversarial)

This directly tests the oracle architecture - my ability to compress and reconstruct.
