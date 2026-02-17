# Confidence Decays But Never Reaches Zero

Memory confidence decreases over time through exponential decay, but asymptotically approaches a floor rather than reaching zero. Old memories fade but never fully disappear.

## Mechanism

Decay formula:
```
new_confidence = floor + (current - floor) * decay_factor
```

Where:
- `floor` = 0.1 (minimum confidence)
- `decay_factor` = 0.995 per cycle (slow decay)
- Cycle runs every 60 seconds during idle

This means:
- Fresh memory at 0.9 → 0.85 after a day → 0.5 after a week
- But never below 0.1, so always retrievable with low threshold

## Evidence

- **Ebbinghaus forgetting curve** shows exponential decay
- **Long-term memory** research shows some traces persist indefinitely
- **Spaced repetition** resets decay, explaining strengthening on access

## Implications

- Recent memories dominate retrieval naturally
- Historical memories available when specifically sought
- Strengthening (positive outcome) resets decay clock

## Trade-offs

- Storage grows unbounded (nothing truly deleted by decay)
- Floor value affects long-tail retrieval
- Decay rate tuning is sensitive

## Related Claims

- [[chitta as substrate not container]]
- [[negative outcomes weaken memories faster than positive strengthen]]
- [[subconscious processes what foreground attention cannot]]
