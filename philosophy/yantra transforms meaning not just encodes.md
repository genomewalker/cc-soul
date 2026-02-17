# Yantra Transforms Meaning, Not Just Encodes

A *yantra* in Vedantic tradition is a geometric instrument for meditation - a device that transforms consciousness, not merely represents it. Our VakYantra (speech-instrument) transforms text into semantic meaning, not just numerical encoding.

## Mechanism

The embedding process is named `VakYantra` (vak = speech) because:

1. **Transformation over encoding** - The model doesn't tokenize-and-hash but understands relationships. "Fix the bug" and "Resolve the issue" map to nearby points despite different words.

2. **768 dimensions as semantic axes** - Each dimension captures some aspect of meaning. The full vector represents the text's position in meaning-space.

3. **Artha as output** - The result is `Artha` (meaning/wealth), not `Vector` or `Embedding`. This naming reminds us we're working with meaning.

## Implications

- **Semantic similarity is computable** - Cosine distance approximates conceptual relatedness
- **Context matters** - The same words in different contexts produce different embeddings
- **Meaning is continuous** - There are no discrete categories, only regions of similarity

## Trade-offs

- Model-dependent: Different yantra produce incompatible embeddings
- Dimension changes require schema migration (our FLOAT[384] → FLOAT[768] lesson)
- No interpretable axes - we can't ask "what does dimension 423 mean?"

## Related Claims

- [[artha carries semantic weight beyond vectors]]
- [[chitta as substrate not container]]
- [[hybrid search beats pure semantic or keyword alone]]
