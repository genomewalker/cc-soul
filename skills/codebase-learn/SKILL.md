---
name: codebase-learn
description: Learn and record codebase structure as connected graph
execution: direct
---

# Codebase Learn

```ssl
[codebase-learn] minimize future token usage via recorded knowledge

explore: key directories | entry points | patterns | architecture

record as entities:
  grow(type=entity, title="path/to/file", content="purpose", domain=project)
  connect(from=file_id, to=related_id, edge_type=relates_to)

create triplets:
  connect(subject="auth.ts", predicate="handles", object="authentication")
  connect(subject="api/", predicate="contains", object="REST endpoints")

tags: project:<name>, codebase, architecture

what to capture:
  file purposes + relationships
  architectural patterns
  key abstractions
  entry points + data flow

future benefit: recall("codebase structure")→instant context without re-reading
```
