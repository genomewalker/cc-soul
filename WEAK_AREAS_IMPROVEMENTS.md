# How To Improve The Weak Areas

The weak areas can be improved, but the right move is constraint, not expansion.

## 1. Reduce architectural surface area

Freeze features for a while. Pick the core loop:

`ingest -> classify -> store -> recall -> revise`

Everything that does not strengthen that loop should be deferred.

## 2. Add a real memory state model

Each memory should have explicit status, not just strength:

- `proposed`
- `observed`
- `verified`
- `superseded`
- `contradicted`
- `archived`
- `deleted`

Then make recall policy depend on status, not just score.

## 3. Separate salience from truth

Right now high-access memories can become strong, but that is not the same as being correct.

Keep at least three axes:

- relevance
- confidence
- epistemic status

A memory can be very relevant and still low-trust.

## 4. Make contradiction first-class

You need structured conflict handling:

- memory A contradicts memory B
- memory B supersedes memory A
- retrieval should prefer the newest verified winner
- old memories should remain inspectable, not silently disappear

## 5. Tighten provenance

Every durable memory should know:

- where it came from
- who asserted it
- whether it was user-stated, tool-derived, model-inferred, or autonomous synthesis
- when it was last checked

Then weight recall by provenance class.

## 6. Put hard boundaries around autonomy

Memory creation can be automatic.

Code changes, git actions, daemon restarts, deployment, and destructive cleanup should require stronger gates:

- isolated worktree or temp clone
- explicit approval token
- no direct mutation of the main worktree by default

## 7. Make orchestration simpler than storage

Your Rust substrate can stay rich.

The daemon/soul layer should become thinner:

- fewer implicit side effects
- fewer hidden background mutations
- explicit event contracts
- idempotent operations where possible

## 8. Add adversarial evaluation

Not just "does recall work."

Test:

- stale memory beats fresh truth
- contradictory memories both retrieved
- multi-instance race/replay
- autonomous loop damaging worktree
- low-quality corrections poisoning future recall

## 9. Add operator tooling

You need commands/UI for:

- why this memory was recalled
- what memories are stale
- what memories conflict
- what autonomous changes are pending
- which heuristics produced a given memory

## 10. Prefer policy tables over scattered heuristics

A lot of behavior looks embedded in many places.

Centralize rules like:

- what can be auto-learned
- what kinds can decay
- what kinds can overwrite
- what requires verification
- what is allowed in autonomous mode

## Short Version

To improve the weak areas:

- simplify
- formalize memory states
- treat truth separately from salience
- make contradictions explicit
- harden provenance
- sandbox autonomy
- build adversarial evals
- give the operator visibility and veto power
