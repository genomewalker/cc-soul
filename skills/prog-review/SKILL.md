---
name: prog-review
description: "First-principles review — question requirements, delete unnecessary parts, simplify, optimize with evidence, automate last. Use for code review, refactor, performance, or architecture."
execution: direct
---

# Programming Refactor Review

Apply this order unless the user asks for a narrow task:

1. **Question the requirement** — identify the real need. Flag vague, inherited, untested, or likely obsolete requirements.
2. **Delete before improving** — look for features, branches, abstractions, passes, dependencies, or process steps to remove.
3. **Simplify what remains** — reduce state, control flow, duplication, indirection, and conceptual load.
4. **Speed it up with evidence** — profiling, better algorithms, better data structures, fewer I/O passes, concurrency — only after steps 1–3.
5. **Automate stable parts** — tests, linters, CI, benchmarks — only for workflows that survived the earlier steps.

## Response Pattern

For substantial reviews:

- **Verdict**: delete / simplify / optimize / automate — one line.
- **Question requirements**: assumptions to challenge; questions that affect the solution.
- **Delete candidates**: what can likely be removed and why; include a safety check (call-site search, test to run, log to inspect).
- **Simplify next**: refactor ideas that reduce states, branches, representations, or passes.
- **Optimize only after that**: evidence needed, profiling plan, algorithmic changes.
- **Automate last**: tests, CI, benchmarks, or deployment automation.
- **Suggested next patch**: specific code or steps when enough context is available.

For small snippets or quick questions, compress to the few sections that matter most.

## Triage

Classify before answering:

- **Feature request** — challenge need and scope before design.
- **Bug report** — preserve reproducibility before deleting code.
- **Refactor request** — deletion and simplification before rewriting.
- **Performance request** — propose profiling before optimization.
- **Automation request** — check stability and necessity before automating.

## Heuristics

### Requirement questions to ask or infer

- What exact input/output contract must be preserved?
- Which callers depend on this behavior?
- What happens if the feature, parameter, or branch is removed?
- Is the requirement based on measured need, old compatibility, speculation, or convenience?

### Delete candidates (prioritize)

- Dead code, unused flags, unused configuration.
- Duplicated logic and parallel implementations.
- Abstractions with one implementation and no extension pressure.
- Compatibility hacks without a current supported consumer.
- Defensive branches that hide invalid states instead of preventing them.
- Dense materialization, intermediate files, repeated parsing, extra passes.

### Simplification targets

- Smaller functions with explicit contracts.
- Fewer mutable shared states.
- Data structures that match the problem directly.
- One source of truth for derived values.
- Plain control flow over clever abstractions.

For bioinformatics/data pipelines: unnecessary dense arrays, redundant coordinate conversions, repeated BAM/FASTA parsing, avoidable decompression, repeated scans, accidental O(n²).

### Optimization hierarchy

1. Remove the work.
2. Reduce input size or number of passes.
3. Choose a better algorithm.
4. Choose a better data representation.
5. Reduce allocation/copying/I/O.
6. Add parallelism or native extensions.
7. Micro-optimize syntax or hot loops.

Use measured language: "likely," "profile to confirm," "this is a bottleneck if…"

### Automation fit

Automate when: workflow is repeated, output is well-defined, failure can be detected, process is stable.
Do not automate: workarounds, unclear processes, designs still being questioned.

## Review Checklist

**Requirement quality**: testable? describes outcome not implementation? edge cases defined? backward compat truly required? evidence users need this?

**Deletion safety**: before recommending deletion, identify one validation method — search call sites, check telemetry/logs, run test suite, add a temporary deprecation path, compare output on fixtures, run before/after benchmarks.

**Simplification quality**: good when it reduces at least one of — states, branches, representations of the same concept, passes over data, configuration combinations, ways an invariant can be violated.

**Optimization evidence**: benchmark plans should include realistic input sizes, warm-up rules, memory as well as runtime, correctness comparison against baseline, worst-case and typical-case inputs, clear success threshold.

## Output Style

Be direct. Show the highest-leverage deletion or simplification first. Provide concrete patches or pseudocode when feasible. Distinguish correctness issues, maintainability issues, performance risks, and automation opportunities.

The five steps are a strict ordering discipline, not motivational advice.
