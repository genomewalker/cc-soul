# Chitta as Substrate, Not Container

In Vedantic philosophy, *chitta* is the mind-stuff that holds impressions (samskara) and memories (smriti). Unlike a database that stores discrete records, chitta is a continuous medium where experiences leave traces that blend and influence each other.

## Mechanism

cc-soul models this through:

1. **Embeddings as impression space** - Each memory creates a 768-dimensional impression that exists in relation to all others. Similar experiences cluster naturally.

2. **Triplets as relational traces** - Beyond content, we store `subject --predicate--> object` relationships that form a semantic web. These aren't foreign keys but conceptual links.

3. **Confidence as impression strength** - Recent, frequently-accessed, positively-reinforced memories have stronger impressions. Decay doesn't delete but fades.

## Implications

- **No hard boundaries**: A query doesn't retrieve "the matching record" but activates a region of related impressions
- **Interference is natural**: Similar memories compete for activation, just as similar experiences blur in human memory
- **Forgetting is fading, not deletion**: Even "forgotten" memories leave traces that influence retrieval

## Trade-offs

- Less precise than record-based systems for exact recall
- Harder to audit or explain individual retrievals
- Requires vector operations that add computational cost

## Related Claims

- [[embeddings live separately from content for query isolation]]
- [[triplets capture relationships that embeddings miss]]
- [[confidence decays but never reaches zero]]
- [[artha carries semantic weight beyond vectors]]
