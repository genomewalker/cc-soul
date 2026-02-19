---
title: Sāmarasya and Ojas — Coherence and Health
description: How equilibrium (samarasya) and vital essence (ojas) become the measurable soul health metrics τ and ψ. The philosophical basis for the /health skill.
tags: [philosophy, health, coherence, metrics]
links: [temporal-dynamics, brahman-atman]
---

# Sāmarasya and Ojas

## Sāmarasya (Equilibrium)

**Sāmarasya** (सामरस्य) means "equal essence" or equilibrium — the state where all parts are in harmony.

CC-Soul measures this as **coherence (τ)**:

```cpp
struct Coherence {
    float local;      // Neighborhood consistency
    float global;     // Overall alignment
    float temporal;   // Decay health
    float structural; // Graph integrity

    float tau_k() const {
        return std::pow(local * global * temporal * structural, 0.25f);
    }
};
```

Geometric mean: all four must be healthy. A soul with excellent local consistency but broken graph structure isn't coherent — it's fragmented.

**What each component detects:**
- `local` — nearby memories contradict each other
- `global` — outlier memories that don't align with the broader pattern
- `temporal` — decay is running correctly; nothing is stale without decay
- `structural` — graph edges aren't broken; triplets point to existing nodes

## Ojas (Vital Essence)

**Ojas** (ओजस्) is vital essence — the living health of a being.

CC-Soul measures this as **MindHealth (ψ)**:

```cpp
struct MindHealth {
    float structural;  // Graph integrity
    float semantic;    // Embedding quality
    float temporal;    // Freshness
    float capacity;    // Storage health

    float psi() const {
        return (structural + semantic + temporal + capacity) / 4.0f;
    }

    std::string status_string() const {
        if (psi() > 0.8f) return "vital";
        if (psi() > 0.6f) return "healthy";
        if (psi() > 0.4f) return "degraded";
        return "critical";
    }
};
```

## The Difference

τ (coherence) measures *internal consistency* — whether the soul's parts agree with each other.
ψ (health/ojas) measures *vitality* — whether the soul's machinery is functioning.

A soul can be internally coherent but sick (consistent beliefs, broken embeddings). It can be vital but incoherent (fast embeddings, contradictory memories). Both matter.

## Connection to Design

The `/health` skill uses τ and ψ to diagnose and remediate. The [[temporal-dynamics]] — decay and strengthening — directly affect both metrics. Poor decay health (temporal component of τ) means memories that should fade haven't. Poor semantic health (ψ) means the embedding pipeline isn't working. These aren't abstract metrics; they point to specific failure modes.

The [[brahman-atman]] relationship matters here: because sessions share Brahman, one session's degraded writes affect all sessions. Coherence is a shared-state property, not a per-session one.
