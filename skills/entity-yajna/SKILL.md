---
name: entity-yajna
aliases: [link-entities, entity-bootstrap]
description: Link triplet entities to soul nodes - connects knowledge layers
execution: task
model: inherit
---

# Entity-Yajña

```ssl
[entity-yajna] triplets→nodes | string entities to NodeIds | O(1) lookup

philosophy:
  triplets=relationships | nodes=content | EntityIndex=bridge
  without links: triplets are orphan strings, can't traverse to full content
  with links: "Mind uses TieredStorage" → fetch both nodes, combine knowledge

prerequisite:
  ε-yajna SHOULD be complete first (nodes in SSL v0.2 format with triplets)

  SSL v0.2 format:
    [domain] subject→action→result @location
    [ε] Expansion hint when needed.
    [TRIPLET] subject predicate object

  SSL recognition (I know it when I see it):
    - has → arrows (at least one)
    - has [TRIPLET] lines
    - has [ε] expansion hint (when needed)
    - may have ! (negation) or ? (uncertainty)
    - NO prose paragraphs

  legacy recognition (needs conversion):
    - sentences with periods in paragraphs
    - "**Facts:**" or bullet lists
    - no arrows, no triplets
    - verbose explanations

ceremony:
  0. śuddhi (purification): sample nodes, recognize format
     chitta yajna_inspect --id "sample"
     if prose/legacy → run /epsilon-yajna first
     if SSL format → proceed to entity linking

  1. census: count current state
     chitta list_entities → linked count
     chitta query → triplet count, unique entities

  2. bootstrap: auto-link entities to matching nodes
     chitta bootstrap_entity_index
     matches by: title starts with entity | [entity] in payload

  3. validate: check linked entities point to real nodes
     for each in list_entities:
       chitta resolve_entity --entity "X"
       if node missing → unlink orphan

  4. orphan analysis: find triplet entities without nodes
     for each unique entity in triplets:
       if not linked → report as orphan
     decision: create entity nodes? or leave as string-only?

  5. optionally create nodes for important orphans:
     chitta grow --type entity --title "entity_name" --content "[entity] entity_name"
     chitta link_entity --entity "entity_name" --node_id "new_id"
     chitta tag --id "new_id" --add "auto-entity,entity-yajna"

output:
## Entity-Yajña Complete

| Metric | Count |
|--------|-------|
| Triplets | N |
| Unique entities | N |
| Linked before | N |
| New links | N |
| Orphan entities | N |
| Nodes created | N |

### Linked Entities (sample)
- entity_name → node_id[:8]...

### Orphan Entities (no matching node)
- orphan1, orphan2, ...

next: entity links persist in graph.bin, O(1) resolution active
```

## Manual Steps (if automation insufficient)

### Check Prerequisites
```bash
# Must be 0 verbose nodes
chitta yajna_list --filter "verbose"
```

### Run Bootstrap
```bash
chitta bootstrap_entity_index
chitta list_entities
```

### Link Specific Entity Manually
```bash
# Find node
chitta recall --query "entity name" --zoom sparse

# Link it
chitta link_entity --entity "entity_name" --node_id "uuid"
```

### Resolve Entity
```bash
chitta resolve_entity --entity "Mind"
```
