# Context Window Is Precious So Retrieval Must Be Selective

Claude's context window is finite (~200K tokens). Every memory surfaced consumes space that could hold user content or Claude's reasoning. Retrieval must maximize signal-to-noise ratio.

## Mechanism

cc-soul applies multiple filters:

1. **Confidence threshold** - Below 0.3 rarely surfaces
2. **Relevance scoring** - Only top-k results returned
3. **Deduplication** - Similar memories consolidated
4. **SSL compression** - Reduces token footprint per memory
5. **Selective surfacing** - Hooks choose what goes in system prompt

Budget allocation:
- Session start: ~2000 tokens for context
- Per-query recall: ~500 tokens max
- Correction warnings: ~100 tokens

## Evidence

- **LLM attention degrades** as context fills (arscontexta claim)
- **Retrieval precision** matters more than recall for generation
- **Token costs** scale linearly with context size

## Implications

- Better to surface 3 relevant memories than 10 vaguely related
- Compression (SSL) directly increases useful memory density
- Low-confidence memories may never surface despite existing

## Trade-offs

- Potentially useful memories filtered out
- Threshold tuning affects behavior significantly
- Users can't see everything that's stored

## Related Claims

- [[SSL format compresses without losing retrievability]]
- [[hybrid search beats pure semantic or keyword alone]]
- [[anticipation surfaces memories before explicit query]]
