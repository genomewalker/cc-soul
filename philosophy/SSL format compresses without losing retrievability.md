# SSL Format Compresses Without Losing Retrievability

Soul Shorthand Language (SSL) is a compression format that reduces token count while preserving semantic content. Unlike lossy compression, SSL-encoded memories remain retrievable by the same search mechanisms.

## SSL v0.3

Two-tier compression with affect, structural flags, and cross-references.

### Tier 1: Code-bearing (SOLUTION, GOTCHA, PATTERN)
```
[domain] subject→action→result @location F:FLAG A:v,a →@ref
[ε] exact_command_or_code_verbatim
[TRIPLET] subject predicate object
```

### Tier 2: Narrative (DECISION, PREFERENCE, FAILURE) — denser, no [ε]
```
[domain] choice>alternative|reason+context F:FLAG A:v,a →@ref
[TRIPLET] subject predicate object
```

### Annotations

| Annotation | Purpose | Example |
|------------|---------|---------|
| `A:v,a` | Affect (valence, arousal) | A:+0.6,0.3 |
| `F:FLAG` | Structural importance | F:PIVOT |
| `→@ref` | Cross-reference | →@queue-architecture |

### Symbols

| Symbol | Meaning |
|--------|---------|
| `→` | produces/leads to |
| `>` | chose over (Tier 2) |
| `\|` | or/alternative/reason |
| `+` | with/and |
| `@` | location |
| `!` | negation |
| `?` | uncertainty |

## Mechanism

Compression techniques:
1. **Eliminate boilerplate** — No "I learned that...", "The user said..."
2. **Use symbols** — `→` for implications, `@` for references
3. **Two-tier density** — Code-bearing memories preserve verbatim in `[ε]`; narrative memories use denser `>` and `|` chains
4. **Affect encoding** — `A:v,a` populates chitta-field's cortical arousal boost (high-arousal memories recall faster)
5. **Structural flags** — `F:PIVOT` enables filtered retrieval by importance
6. **Cross-references** — `→@ref` makes the association graph visible in compressed text

## Evidence

- **40-60% token reduction** for Tier 1 (code-bearing)
- **60-80% token reduction** for Tier 2 (narrative, dense symbol chains)
- **Embeddings still work** — semantic content preserved
- **Affect activates cortical boost** — high-arousal memories get recall priority
- **Human and model readable** — no decoder required

## Deterministic Fallback

When no LLM is available for distillation, a rule-based encoder extracts basic SSL from conversation patterns:
- "fixed X" → `[SOLUTION]`
- "chose X over Y" → `[DECISION]`
- "prefer X" → `[PREFERENCE]`
- "watch out for X" → `[GOTCHA]`
- "failed because X" → `[FAILURE]`

Lower quality than LLM distillation, but ensures memories are always captured.

## Epiplexity Score

Compression quality is measured by:
```
ε = (S · K · D · C)^0.25

S = Semantic preservation (cosine similarity of embeddings)
K = Key fact retention (named entities, numbers, code preserved)
D = Density ratio (compression achieved)
C = Coherence (reconstructed text is self-consistent)
```

## Related Claims

- [[context window is precious so retrieval must be selective]]
- [[distillation extracts wisdom from conversation noise]]
- [[yantra transforms meaning not just encodes]]
