---
title: Epiplexity — Information for Bounded Observers
description: Why maximum information is not maximum useful information. The information-theoretic basis for SSL compression and the hook injection strategy.
tags: [philosophy, information-theory, compression, ssl, context]
links: [chitta-substrate, ceremonial-framework]
---

# Epiplexity

## The Problem with Shannon Entropy

Classical information theory assumes observers with unlimited computational capacity. Claude has a finite context window and bounded processing. Total information isn't the goal — *learnable structure* is.

**Epiplexity** (epistemic complexity): the amount of structural information a computationally-bounded observer can extract from data.

Two components:

| Component | Meaning | Example |
|-----------|---------|---------|
| Epiplexity (S_T) | Learnable structure | `τ:84% ψ:88%` — pure signal |
| Time-bounded entropy (H_T) | Noise irreducible by bounded compute | Verbose debug logs |

For context injection: **maximize epiplexity per token, minimize entropy.**

## The Epiplexity Score

CC-Soul measures compression quality with:

```
ε = (S · K · D · C)^0.25

S = Semantic preservation (cosine similarity of embeddings)
K = Key fact retention (named entities, numbers, code preserved)
D = Density ratio (compression achieved)
C = Coherence (reconstructed text is self-consistent)
```

Used by `/epsilon-yajna` to ensure SSL compression preserves meaning while dropping noise.

## Compression Can Increase Signal

The lean hook mode achieves 95% token reduction while *increasing* useful information:

```
Verbose (655 chars): Full soul state with detailed statistics
Lean (35 chars):     [soul] n=2088 t=340 c=0.84 healthy
```

The lean version is pure structural signal. The verbose version requires a bounded observer to extract the same signal from surrounding prose — reducing effective epiplexity.

## Data Ordering Matters

Unlike Shannon entropy, epiplexity depends on presentation order. The hook system's decisions are epistemic decisions:
- What context to inject (resonance-ranked memories)
- In what order (relevance-descending)
- At what granularity (SSL-compressed)

This transforms data to maximize learnable structure for the current query.

## The Injection Pipeline

```
User query → full_resonate(query) → Top-3 results
                                  ↓
                            Filter ≥25% relevance
                                  ↓
                            Strip type prefixes
                                  ↓
                            Truncate to 500 chars
                                  ↓
                            Inject as system context
```

Each step moves toward higher epiplexity — pure signal, no noise. The [[chitta-substrate]]'s geometry (768-dimensional embeddings) is what makes relevance ranking possible. The [[ceremonial-framework]]'s epsilon-yajna is what transforms verbose memories into injectable SSL form.

## Connection to Soul Design

Every architectural choice in the injection pipeline can be evaluated against epiplexity:
- Why 3 memories, not 10? — additional memories have lower relevance, higher noise
- Why SSL format? — maximum structural density per token
- Why truncate at 150 chars? — marginal return per char decreases; epiplexity peaks early
- Why confidence threshold at 25%? — below this, noise exceeds signal
