# Subconscious Processes What Foreground Attention Cannot

The daemon runs background tasks that would interrupt foreground conversation: confidence decay, memory consolidation, theme maintenance, hygiene cleanup. These happen during idle periods.

## Mechanism

The Subconscious component runs when:
1. **Idle detection** - No queries for 30+ seconds
2. **Scheduled intervals** - Every 60 seconds regardless
3. **Event triggers** - Session end, memory threshold

Background tasks:
- **Hygiene** - Decay confidence, prune low-value memories
- **Theme maintenance** - Split/merge/reassign theme memberships
- **Session cleanup** - Remove stale session registrations
- **Distillation triggers** - Queue transcripts for processing

## Evidence

- **Spaced repetition** research shows memory consolidation requires gaps
- **Background processing** in operating systems handles housekeeping during idle
- **Defragmentation** works best when system is not under load

## Implications

- System improves while user is away
- Low-confidence memories fade naturally
- Themes self-organize over time

## Trade-offs

- Background activity can cause unexpected behavior
- Hard to debug issues in background processes
- Idle detection may misfire during long-running tools

## Related Claims

- [[confidence decays but never reaches zero]]
- [[themes emerge from clustering not taxonomy]]
- [[hooks capture moments humans would forget to log]]
