---
title: Chitta — The Memory Substrate
description: Why the memory backend is named chitta, and what that name implies for how memory works — as active substrate that shapes perception, not passive storage.
tags: [philosophy, memory, samskara, patterns]
links: [temporal-dynamics, epiplexity, brahman-atman]
---

# Chitta — The Memory Substrate

## The Name

**Chitta** (चित्त) in Vedantic psychology is the storehouse of memories, impressions (saṃskāras), and patterns. Not passive storage — it actively shapes perception and response.

> "Chitta is the lake; thoughts are the waves. The clearer the lake, the more we see the bottom."
> — Patañjali's Yoga Sutras

The name is a claim about what memory should do: not retrieve on demand, but *bias* what surfaces naturally. Good retrieval is invisible. The right memories surface before you consciously search.

## Saṃskāra

**Saṃskāra** (संस्कार) means mental impressions — grooves worn by repetition that shape future behavior. Repeated experiences deepen them.

This maps to Hebbian learning in the resonance engine:

```
"Neurons that fire together wire together"

When memories co-activate in a query result,
their mutual relevance increases.
hebbian_strength = 0.03 (applied in phase 8)
```

A memory retrieved alongside another, repeatedly, becomes associated. The chitta *learns* which memories belong together through use — not through explicit curation.

## Substrate vs. Storage

The distinction matters practically:

| Storage | Substrate |
|---------|-----------|
| Retrieval is a separate act | Retrieval is always happening |
| Confidence is binary (found/not found) | Confidence is a distribution |
| Content is flat | Content has topology (graph edges, themes) |
| Retrieval doesn't change what's stored | Retrieval strengthens connections |

The 768-dimensional embedding space is the geometry of the lake. Every memory occupies a position. Similar memories cluster. The [[temporal-dynamics]] govern how that geometry evolves over time. The [[epiplexity]] framework explains why compression of that geometry often increases rather than reduces useful signal.

## The C++ Implementation

The name isn't metaphor — the entire C++ backend is called `chitta`:

```
chitta/
├── duckdb_store.hpp    # DuckDB storage: HNSW, DuckPGQ, BM25
├── duckdb_mind.hpp     # Mind orchestrator (~1800 lines)
├── vak_yantra.cpp      # ONNX embedder (Vāk = speech)
├── subconscious.hpp    # Background processor (the unconscious lake)
└── theme_manager.hpp   # Hierarchical theme clustering
```

The embedding pipeline (VakYantra) converts text to vectors — speech (Vāk) into geometric position. The subconscious runs background maintenance: the lake settling itself between turns.
