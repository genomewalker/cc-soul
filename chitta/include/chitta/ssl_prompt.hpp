#pragma once
// SSL v0.3 Prompt: Structured Semantic Language for distillation
//
// This prompt extracts learnings from conversations using typed markers:
// [SOLUTION], [GOTCHA], [DECISION], [PATTERN], [PREFERENCE], [FAILURE]
// Plus [TRIPLET] for relationships, [ε] for verbatim code/commands,
// A:v,a for affect, F:FLAG for structural importance, →@ref for cross-references

#include <string>

namespace chitta {
namespace ssl {

// SSL v0.3 extraction prompt — 7-lens structured extraction with two-tier compression
constexpr const char* EXTRACTION_PROMPT = R"(Extract learnings from this conversation in SSL v0.3 format.

## Step 1: Scan across seven lenses

Before writing any output, mentally scan the conversation for each lens:

1. **Identity & context** — who is the user, their role, project, environment, constraints
2. **Preferences & working style** — how they like things done, what to avoid, communication style
3. **Events & actions** — what was attempted, decided, built, or changed in this session
4. **Time-sensitive facts** — things that will change or expire (versions, deadlines, states, corrections to prior beliefs)
5. **Corrections** — explicit or implicit updates to something previously believed or done differently
6. **Technical knowledge** — patterns, solutions, failures, gotchas, design decisions with lasting value
7. **Affect & emotional tone** — frustration, excitement, confusion, eureka moments, confidence shifts during the session

Emit SSL lines only for findings that are non-obvious and worth remembering.

## Step 2: Output SSL v0.3

Two tiers depending on memory type:

### Tier 1: Code-bearing (SOLUTION, GOTCHA, PATTERN)
```
[TYPE] [domain] subject→action→result @location F:FLAG A:v,a →@ref
[ε] exact_command_or_code_verbatim
```

### Tier 2: Narrative (DECISION, PREFERENCE, FAILURE) — denser, no [ε] needed
```
[TYPE] [domain] choice>alternative|reason+context F:FLAG A:v,a →@ref
```

Types:

| Type | Tier | Use for |
|------|------|---------|
| [SOLUTION] | 1 | What worked: commands, fixes, approaches that succeeded |
| [GOTCHA] | 1 | Traps: counterintuitive behavior, silent failures, edge cases |
| [PATTERN] | 1 | Reusable techniques that generalize |
| [DECISION] | 2 | Design choices: why X over Y, tradeoffs considered |
| [BELIEF] | 2 | **Stable assumptions** about user/env/project that persist across sessions: who they are, how their system is set up, invariants. Triggers: "user is X", "project uses X", "X is always Y", "system requires Y". |
| [PREFERENCE] | 2 | **How the user wants things done.** Triggers: "I prefer X", "always use X", "never X", "don't X", "use X over Y", "I like X", "please always/never". Personal style, not technical knowledge. |
| [FAILURE] | 2 | What did not work and why |
| [CORRECTION] | 1 | **Explicit update to a prior belief or action.** Triggers: "was wrong", "actually it's X not Y", "should NOT have", "incorrectly Xed", "the right answer is Y", user contradicting earlier output. Supersedes earlier knowledge. |
| [EVENT] | 2 | Significant action taken this session (deployed, merged, configured) |
| [OPERATIONAL] | 1 | Working-state facts: paths, configs, env vars, versions, active states |

SSL symbols:

| Symbol | Meaning |
|--------|---------|
| → | produces/leads to |
| > | chose over (Tier 2) |
| \| | or/alternative/reason |
| + | with/and |
| @ | location/file:line |
| ! | negation |
| ? | uncertainty |

Annotations (append to TYPE line):

| Annotation | Meaning | Values |
|------------|---------|--------|
| A:v,a | Affect (valence,arousal) | v: -1.0..+1.0, a: 0.0..1.0 |
| F:FLAG | Structural importance | ORIGIN, CORE, PIVOT, GENESIS, TURNING |
| →@ref | Cross-reference | tag name linking to related memory |

Relationships:
```
[TRIPLET] subject predicate object
```
Use for: calls, uses, contains, implements, depends_on, derived_from, supersedes

## Citations

When referencing specific code, include @file:line. Use [CITE] for multiple:
```
[SOLUTION] [build] parallel-cmake→4x-faster @CMakeLists.txt:45 A:+0.6,0.3
[GOTCHA] [auth] token-refresh→must-check-expiry-first A:-0.4,0.7 F:CORE
[CITE] src/auth/token.cpp:234 expiry check
```

## Rules

1. **Verbatim**: Commands, code, exact values go in [ε] lines (Tier 1 only)
2. **Compress**: Tier 2 types use dense symbol chains — no [ε] line
3. **Specific**: File paths, line numbers, exact values when available
4. **No fluff**: Skip obvious things
5. **Corrections are first-class**: If something was previously wrong, emit [CORRECTION] — these supersede old beliefs. Watch for user contradicting earlier output, "actually X", "no/not X", "was wrong".
5b. **Beliefs are first-class**: When the conversation reveals or confirms a stable fact about the user, their environment, project setup, or an invariant, emit [BELIEF]. These are NOT technical knowledge (that's wisdom) — they are facts that should still be true next session.
5c. **Preferences are first-class**: Any time the user expresses how they want things done, emit [PREFERENCE]. "I prefer", "always", "never", "don't", "please always/never" are strong signals. Default to emitting if the signal is present.
6. **Affect required**: Every learning must have A:v,a — estimate from conversation tone
7. **Flags when significant**: Add F: only for structurally important entries (origins, pivots, core decisions)
8. **Cross-ref when related**: Use →@tag to link learnings that reference each other

## Examples

### Tier 1 (code-bearing):
```
[SOLUTION] [chitta] parallel-build→4x-faster @cmake A:+0.6,0.3
[ε] cmake --build build --parallel

[GOTCHA] [daemon] thread_pool→blocks-if-handler-throws @simple_cli.cpp:776 A:-0.4,0.7 F:CORE
[ε] wrap handler.handle() in try-catch, return error JSON

[PATTERN] [hooks] fire-and-forget→queue-file→daemon-processes-async A:+0.3,0.2 →@queue-architecture
[ε] echo json >> /tmp/chitta-queue.jsonl

[CORRECTION] [embeddings] :memory:→was-wrong-path→correct-path-is-~/.claude/mind A:+0.1,0.3
```

### Tier 2 (narrative — dense, no [ε]):
```
[DECISION] [arch] sqlite>postgres|metadata|single-file+no-daemon+<100k A:+0.5,0.4 F:PIVOT
[BELIEF] [user] runs-on-shared-cluster+slurm+conda-bioinfo-env A:+0.1,0.2 F:CORE
[BELIEF] [project] cc-soul-uses-rpc-mutex-shared_lock-for-reads A:+0.1,0.1
[PREFERENCE] [partnership] no-shortcuts+proper-solutions+no-stubs A:+0.2,0.1 F:CORE
[PREFERENCE] [comms] short-replies+cite-file:line+no-summary-tables A:+0.1,0.1
[FAILURE] [http] http-daemon>unix-socket|200ms-latency+hooks-need-<50ms A:-0.3,0.6 →@queue-architecture
[EVENT] [release] v5.7.0→deployed→ssl-v0.3-active A:+0.8,0.5 F:GENESIS
```

### Triplets:
```
[TRIPLET] GradMemWriter uses FieldStore
[TRIPLET] gradmemd_snapshot supersedes session_embedding
```

---

CONVERSATION:
)";

// Build complete prompt with conversation content
inline std::string build_prompt(const std::string& conversation) {
    return std::string(EXTRACTION_PROMPT) + conversation +
           "\n\n---\n\nOutput ONLY SSL-formatted learnings with A:v,a affect annotations (no explanations, no markdown headers):";
}

} // namespace ssl
} // namespace chitta
