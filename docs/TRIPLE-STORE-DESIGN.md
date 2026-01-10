# Triple Store Design for Chitta

## Motivation

Claude's cognition is relational. Current flat node storage loses structure:
- "nc fails in subshells → use CLI" stored as text
- Relationship between "nc", "subshell", "CLI" is implicit

Triple store makes relationships explicit and queryable.

## Data Model

### Concepts

Reusable semantic entities. A concept can appear in many triples.

```rust
struct Concept {
    id: Uuid,
    name: String,              // canonical: "socket.failure"
    aliases: Vec<String>,      // ["socket_failure", "nc_fail"]
    embedding: Vec<f32>,       // semantic vector (384-dim)
    confidence: f32,           // existence confidence
    activation: f32,           // current activation level
    last_activated: Timestamp,
    created: Timestamp,
}
```

### Predicates

Typed relationships with semantics.

```rust
enum Predicate {
    // Causal
    Causes,          // A → B: A leads to B
    Solves,          // A fixes B
    Prevents,        // A stops B

    // Taxonomic
    IsA,             // A :: B: A is a type of B
    PartOf,          // A contained in B
    HasProperty,     // A has attribute B

    // Associative
    RelatedTo,       // A ~ B: loose association
    UsedWith,        // A commonly used with B

    // Preferential
    Preferred,       // A better than B
    Contradicts,     // A opposes B
    Supersedes,      // A replaces B

    // Temporal
    Precedes,        // A happens before B
    Requires,        // A needs B first
}
```

### Triples

The fundamental unit of knowledge.

```rust
struct Triple {
    id: Uuid,
    subject: ConceptId,
    predicate: Predicate,
    object: ConceptId,

    // Confidence
    confidence: f32,           // how sure (0-1)
    source: Option<String>,    // provenance

    // Temporal validity
    valid_from: Timestamp,
    valid_until: Option<Timestamp>,  // None = still valid

    // Activation (for spreading)
    activation: f32,
    last_activated: Timestamp,
}
```

## Schema (SQLite)

```sql
CREATE TABLE concepts (
    id TEXT PRIMARY KEY,
    name TEXT UNIQUE NOT NULL,
    aliases TEXT,              -- JSON array
    embedding BLOB,            -- f32 vector
    confidence REAL DEFAULT 0.5,
    activation REAL DEFAULT 0.0,
    last_activated INTEGER,
    created INTEGER NOT NULL
);

CREATE TABLE triples (
    id TEXT PRIMARY KEY,
    subject_id TEXT NOT NULL REFERENCES concepts(id),
    predicate TEXT NOT NULL,   -- enum as string
    object_id TEXT NOT NULL REFERENCES concepts(id),
    confidence REAL DEFAULT 0.5,
    source TEXT,
    valid_from INTEGER NOT NULL,
    valid_until INTEGER,       -- NULL = current
    activation REAL DEFAULT 0.0,
    last_activated INTEGER,

    UNIQUE(subject_id, predicate, object_id, valid_from)
);

-- Indexes for query patterns
CREATE INDEX idx_triples_subject ON triples(subject_id);
CREATE INDEX idx_triples_object ON triples(object_id);
CREATE INDEX idx_triples_predicate ON triples(predicate);
CREATE INDEX idx_concepts_embedding ON concepts(embedding);  -- for ANN
```

## Query API

```rust
// Pattern matching
fn find_triples(
    subject: Option<&str>,     // None = wildcard
    predicate: Option<Predicate>,
    object: Option<&str>,
) -> Vec<Triple>;

// Examples:
find_triples(None, Some(Causes), Some("socket.failure"))
// → What causes socket failures?

find_triples(Some("chitta"), Some(IsA), None)
// → What is chitta?

find_triples(Some("cli"), None, None)
// → Everything about CLI

// Semantic search (embedding similarity)
fn find_similar_concepts(query: &str, k: usize) -> Vec<Concept>;

// Spreading activation
fn activate(concept: &str, strength: f32) -> Vec<(Concept, f32)>;
// Returns activated concepts with their activation levels
```

## Notation Parser

Claude writes:
```
nc.subshell → socket.failure
chitta::memory.substrate
cli~daemon~socket
```

Parser extracts:
```rust
fn parse_notation(line: &str) -> Option<Triple> {
    // A → B
    if let Some((a, b)) = line.split_once("→") {
        return Some(Triple::new(a.trim(), Causes, b.trim()));
    }
    // A::B
    if let Some((a, b)) = line.split_once("::") {
        return Some(Triple::new(a.trim(), IsA, b.trim()));
    }
    // A~B (can be chained: a~b~c)
    if line.contains('~') {
        let parts: Vec<_> = line.split('~').collect();
        // Return multiple triples for chains
    }
    None
}
```

## Migration from Current Storage

### Phase 1: Dual-write

1. Keep existing node storage
2. Add triple tables
3. Stop hook writes to both
4. Observe stores observation + extracts triples

### Phase 2: Extract from existing

```rust
fn migrate_node_to_triples(node: &Node) -> Vec<Triple> {
    let mut triples = vec![];

    // Parse title for patterns
    if let Some(t) = parse_notation(&node.title) {
        triples.push(t);
    }

    // Parse content lines
    for line in node.content.lines() {
        if let Some(t) = parse_notation(line) {
            triples.push(t);
        }
    }

    // Convert edges to RelatedTo triples
    for (target_id, weight) in &node.edges {
        if weight > 0.5 {
            triples.push(Triple::new(
                node.id, RelatedTo, target_id
            ));
        }
    }

    triples
}
```

### Phase 3: Query integration

- `recall` uses both similarity AND triple patterns
- "What causes X?" → triple query
- "Things like X" → embedding similarity
- Hybrid: similar concepts + their causal relationships

## Spreading Activation

When a concept activates, related concepts partially activate:

```rust
fn spread_activation(seed: ConceptId, initial: f32) {
    let mut queue = VecDeque::new();
    queue.push_back((seed, initial));

    while let Some((concept, strength)) = queue.pop_front() {
        if strength < 0.1 { continue; }  // threshold

        // Activate this concept
        activate_concept(concept, strength);

        // Find connected triples
        let triples = find_triples(Some(concept), None, None);
        for triple in triples {
            let decay = match triple.predicate {
                Causes => 0.7,    // strong spread
                IsA => 0.8,       // very strong
                RelatedTo => 0.3, // weak
                _ => 0.5,
            };
            queue.push_back((triple.object, strength * decay));
        }
    }
}
```

## Integration with Claude

### Storage (Stop hook)

```
Claude writes: nc.subshell → socket.failure
Hook parses: (nc.subshell, Causes, socket.failure)
Chitta stores:
  - Concept "nc.subshell" (if new)
  - Concept "socket.failure" (if new)
  - Triple linking them
```

### Retrieval (Soul hook)

```
User asks about sockets
Chitta:
  1. Embed query
  2. Find similar concepts
  3. Spread activation from those
  4. Find triples involving activated concepts
  5. Format for injection

Injected: "nc.subshell causes socket.failure; cli.direct solves this"
Claude expands naturally in response
```

## Benefits

1. **Structured knowledge** - Not flat text
2. **Relational queries** - "What causes X?"
3. **Inference** - A causes B, B causes C → A indirectly causes C
4. **Spreading activation** - Related concepts surface together
5. **Temporal validity** - Facts can change
6. **Confidence tracking** - Per-triple certainty
7. **Compact storage** - Concepts reused across triples
