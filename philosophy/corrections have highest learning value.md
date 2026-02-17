# Corrections Have Highest Learning Value

When a user corrects Claude's behavior, that moment contains more signal than hours of smooth interaction. A correction reveals a gap between expected and actual behavior that, once captured, prevents repeated mistakes.

## Mechanism

cc-soul treats corrections specially:

1. **Detection in hooks** - The prompt-hook scans for correction patterns: "wrong", "not what I meant", "actually...", "no, I said..."

2. **Higher confidence storage** - Corrections store at 0.9 confidence vs 0.8 for general wisdom. They decay more slowly.

3. **Automatic surfacing** - When similar contexts arise, corrections surface proactively: "⚠️ BEFORE: [correction memory]"

4. **Negative outcome learning** - If a correction-based memory leads to another correction, it's flagged for review.

## Evidence

This mirrors behavioral learning research:
- **Error-driven learning** outperforms passive observation
- **Prediction errors** trigger stronger memory consolidation
- **Emotional salience** (frustration of being corrected) enhances encoding

## Implications

- Corrections justify interrupting flow to capture
- Past corrections should surface before similar mistakes
- Multiple corrections on same topic indicate deeper misunderstanding

## Trade-offs

- Over-indexing on corrections can make the system overly cautious
- Single corrections may not represent stable preferences
- User correction style varies (some say "wrong", others rephrase silently)

## Related Claims

- [[hooks capture moments humans would forget to log]]
- [[preferences shape behavior without explicit rules]]
- [[negative outcomes weaken memories faster than positive strengthen]]
