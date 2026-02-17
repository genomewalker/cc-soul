# Hybrid Search Beats Pure Semantic or Keyword Alone

Neither vector similarity nor BM25 keyword matching is sufficient alone. Semantic search misses exact terms; keyword search misses paraphrases. Hybrid scoring combines both signals.

## Mechanism

cc-soul's recall uses weighted combination:

```
relevance = (w_sem * cosine_similarity) + (w_bm25 * bm25_score) + tag_boost
final_score = relevance * (0.5 + 0.5 * confidence)
```

Default weights: `w_sem = 0.6`, `w_bm25 = 0.4`

This means:
- Semantic similarity contributes 60% of base relevance
- Keyword matching contributes 40%
- Tag presence adds bonus
- Confidence scales the combined score

## Evidence

Research on retrieval systems shows:
- **Semantic-only** fails on proper nouns, code identifiers, exact phrases
- **Keyword-only** fails on synonyms, paraphrases, conceptual queries
- **Hybrid approaches** (BM25 + dense retrieval) consistently outperform either alone

## Implications

- Both FTS index and HNSW index are essential
- Query terms inform both pathways
- Results blend conceptually similar with literally matching

## Trade-offs

- Two indices = more storage and maintenance
- Weight tuning affects behavior significantly
- Neither signal is interpretable alone

## Related Claims

- [[yantra transforms meaning not just encodes]]
- [[context window is precious so retrieval must be selective]]
- [[resonance amplifies relevant memories through use]]
