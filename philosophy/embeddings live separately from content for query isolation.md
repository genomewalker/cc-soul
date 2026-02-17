# Embeddings Live Separately from Content for Query Isolation

The embeddings database (`chitta_embeddings.duckdb`) is separate from the main content database (`chitta.duckdb`). This architectural split enables concurrent access patterns that would otherwise conflict.

## Mechanism

1. **Write contention** - HNSW index updates during embedding inserts can block reads. Isolation prevents query slowdowns during memory storage.

2. **Schema independence** - Embedding dimensions can change (384→768) without main schema migration. The embeddings DB can be rebuilt from main DB VARCHAR backup.

3. **Query parallelism** - Vector similarity search in embeddings DB runs independently of BM25/FTS queries in main DB.

## Evidence

DuckDB's single-writer model means:
- Only one connection can write at a time
- Long writes (HNSW updates) block subsequent writers
- Read queries during write can see inconsistent state

Separation via two databases eliminates these conflicts.

## Implications

- Two-phase retrieval: IDs from embeddings DB, content from main DB
- Migration tools needed to sync embeddings between databases
- Both databases must use compatible memory_id references

## Lessons Learned

The FLOAT[384] vs FLOAT[768] bug taught us:
- Schema mismatches fail silently (insert errors caught but swallowed)
- Migration must cast VARCHAR→FLOAT[N] explicitly
- Debug logging at insert time catches dimension errors early

## Related Claims

- [[chitta as substrate not container]]
- [[yantra transforms meaning not just encodes]]
- [[daemon architecture enables instant recall]]
