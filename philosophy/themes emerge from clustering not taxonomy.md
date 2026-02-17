# Themes Emerge from Clustering, Not Taxonomy

Pre-defined categories force memories into boxes that may not fit. Themes emerge organically from semantic clustering - memories that belong together find each other.

## Mechanism

Inspired by xMemory paper, themes form through:

1. **Orphan detection** - Memories not in any theme are candidates
2. **Similarity scoring** - Cosine similarity to existing theme centroids
3. **Assignment or creation** - Join existing theme if score > threshold, else create new
4. **Centroid update** - Theme centroid recalculated as members change
5. **Maintenance** - Split oversized themes, merge redundant ones

## Evidence

- **Self-organizing maps** outperform fixed taxonomies for evolving domains
- **K-means clustering** naturally groups similar items without labels
- **Emergent categories** in cognitive science: humans form categories from examples, not definitions

## Implications

- No configuration of "what themes should exist"
- Themes may not have interpretable names (auto-generated from content)
- Related memories cluster regardless of explicit tagging

## Trade-offs

- Themes may not align with user's mental model
- Centroid drift as membership changes
- Requires embeddings to work (no embeddings = no themes)

## Related Claims

- [[chitta as substrate not container]]
- [[triplets capture relationships that embeddings miss]]
- [[embeddings live separately from content for query isolation]]
