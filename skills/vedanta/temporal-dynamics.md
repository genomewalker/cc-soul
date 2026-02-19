---
title: Temporal Dynamics — Decay and Strengthening
description: How impermanence (anitya) and deepening (samskara) coexist. The philosophical basis for decay rates, Hebbian learning, and Bayesian confidence as a distribution.
tags: [philosophy, decay, confidence, memory, bayesian]
links: [chitta-substrate, samarasya]
---

# Temporal Dynamics

## Impermanence (Anitya)

Buddhism and Hinduism share the recognition that nothing is permanent. CC-Soul implements this through differential decay:

```cpp
static constexpr float decay_rate(NodeType t) {
    case Wisdom:     return 0.005f;  // Slow — wisdom persists
    case Belief:     return 0.0f;    // Never — beliefs are permanent
    case Episode:    return 0.03f;   // Medium — episodes fade
    case Symbol:     return 0.0f;    // Never — code structure is stable
    case Preference: return 0.01f;   // Very slow — preferences endure
    case Correction: return 0.005f;  // Slow — lessons last
    default:         return 0.02f;
}
```

The rates aren't arbitrary. Beliefs don't decay because if we've committed to a principle, erosion through time would undermine coherent identity. Episodes decay because what happened last Tuesday matters less than what happened last month at the same confidence level. Wisdom decays slowly because earned insight should persist but not permanently crowd out newer learning.

## Strengthening (Saṃskāra)

Impermanence alone would produce amnesia. Saṃskāra provides the counterforce — deepening grooves through repetition.

Three mechanisms:

**1. Hebbian learning** — co-activated nodes strengthen each other. Retrieved together repeatedly → associated permanently.

**2. Bayesian confidence** — each observation updates a distribution, not a scalar:

```cpp
struct Confidence {
    float mu;       // Mean probability estimate
    float sigma_sq; // Variance (uncertainty)
    uint32_t n;     // Observation count
    float tau;      // Temporal decay factor

    float effective() const {
        return mu - std::sqrt(sigma_sq);  // Conservative estimate
    }

    void observe(float value) {
        n++;
        float alpha = 1.0f / n;
        mu = (1 - alpha) * mu + alpha * value;
        // Update variance...
    }
};
```

A memory observed once has high sigma_sq — uncertain. Confirmed many times: low variance — confident. The `effective()` method uses the conservative estimate, so new memories don't punch above their weight.

**3. Attractor dynamics** — high-confidence nodes pull related memories toward them during spreading activation (phase 5 of resonance).

## Why Both

Decay without strengthening → everything fades equally.
Strengthening without decay → old memories accumulate until they crowd out new signal.

Together: frequently-accessed, high-quality memories strengthen while rarely-used, low-quality ones fade. The [[chitta-substrate]] becomes self-organizing — the lake clears.

[[samarasya]] measures whether this dynamic is healthy: whether decay and strengthening are in balance, whether confidence distributions are well-calibrated.
