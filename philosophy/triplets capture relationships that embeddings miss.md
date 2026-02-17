# Triplets Capture Relationships That Embeddings Miss

While embeddings capture semantic similarity, they lose explicit relationships. "Alice manages Bob" and "Bob manages Alice" have similar embeddings but opposite meanings. Triplets preserve directed relationships.

## Mechanism

cc-soul stores triplets as `subject --predicate--> object`:

```
memory:12345 --derived_from--> session:abc123
symbol:func_foo --calls--> symbol:func_bar
project:cc-soul --uses--> technology:DuckDB
```

DuckPGQ extension enables graph queries:
- Path traversal: "What does this function transitively call?"
- Reachability: "Is this memory connected to that session?"
- Pattern matching: "Find all X that Y relates to Z"

## Evidence

Knowledge graph research shows:
- **Triple stores** (RDF, Neo4j) capture relationships embeddings flatten
- **Graph neural networks** outperform pure embeddings on relational tasks
- **Explainability** improves when relationships are explicit

## Implications

- Embeddings for similarity, triplets for structure
- Navigation possible without vector search
- Provenance tracking: trace memory to source

## Trade-offs

- Storage overhead for relationship edges
- Query complexity for graph traversal
- Predicate vocabulary needs curation

## Related Claims

- [[chitta as substrate not container]]
- [[themes emerge from clustering not taxonomy]]
- [[embeddings live separately from content for query isolation]]
