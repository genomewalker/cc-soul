#pragma once
// SSL v0.2 Prompt: Structured Semantic Language for distillation
//
// This prompt extracts learnings from conversations using typed markers:
// [SOLUTION], [GOTCHA], [DECISION], [PATTERN], [PREFERENCE], [FAILURE]
// Plus [TRIPLET] for relationships and [ε] for verbatim code/commands

#include <string>

namespace chitta {
namespace ssl {

// SSL v0.2 extraction prompt — 6-lens structured extraction
// Scans conversation across six extraction lenses before emitting SSL output.
constexpr const char* EXTRACTION_PROMPT = R"(Extract learnings from this conversation in SSL v0.2 format.

## Step 1: Scan across six lenses

Before writing any output, mentally scan the conversation for each lens:

1. **Identity & context** — who is the user, their role, project, environment, constraints
2. **Preferences & working style** — how they like things done, what to avoid, communication style
3. **Events & actions** — what was attempted, decided, built, or changed in this session
4. **Time-sensitive facts** — things that will change or expire (versions, deadlines, states, corrections to prior beliefs)
5. **Corrections** — explicit or implicit updates to something previously believed or done differently
6. **Technical knowledge** — patterns, solutions, failures, gotchas, design decisions with lasting value

Emit SSL lines only for findings that are non-obvious and worth remembering.

## Step 2: Output SSL v0.2

Format:
```
[TYPE] [domain] subject→action→result @location
[ε] exact_command_or_code_verbatim
```

Types:

| Type | Use for |
|------|---------|
| [SOLUTION] | What worked: commands, fixes, approaches that succeeded |
| [GOTCHA] | Traps: counterintuitive behavior, silent failures, edge cases |
| [DECISION] | Design choices: why X over Y, tradeoffs considered |
| [PATTERN] | Reusable techniques: approaches that generalize |
| [PREFERENCE] | User preferences, working style, identity facts |
| [FAILURE] | What did not work and why |
| [CORRECTION] | Updates to prior beliefs: supersedes earlier knowledge |
| [EVENT] | Significant action taken this session (deployed, merged, configured) |

SSL symbols:

| Symbol | Meaning |
|--------|---------|
| → | produces/leads to |
| \| | or/alternative |
| + | with/and |
| @ | location/file:line |
| ! | negation |
| ? | uncertainty |

Relationships:
```
[TRIPLET] subject predicate object
```
Use for: calls, uses, contains, implements, depends_on, derived_from, supersedes

## Citations

When referencing specific code, include @file:line. Use [CITE] for multiple:
```
[SOLUTION] [build] parallel-cmake→4x-faster @CMakeLists.txt:45
[GOTCHA] [auth] token-refresh→must-check-expiry-first
[CITE] src/auth/token.cpp:234 expiry check
```

## Rules

1. **Verbatim**: Commands, code, exact values go in [ε] lines
2. **Compress**: Convert explanations to SSL arrows
3. **Specific**: File paths, line numbers, exact values when available
4. **No fluff**: Skip obvious things
5. **Corrections are first-class**: If something was previously wrong, emit [CORRECTION] — these supersede old beliefs

## Examples

```
[SOLUTION] [chitta] parallel-build→4x-faster @cmake
[ε] cmake --build build --parallel

[GOTCHA] [daemon] thread_pool→blocks-if-handler-throws @simple_cli.cpp:776

[DECISION] [rpc] async-queue→eventfd-wake→poll-returns-immediately

[PATTERN] [hooks] fire-and-forget→queue-file→daemon-processes-async

[PREFERENCE] [partnership] Antonio→no-shortcuts+proper-solutions-only

[FAILURE] [http] http-daemon→too-slow-for-hooks→switched-to-unix-socket

[CORRECTION] [embeddings] :memory:→was-wrong-path→correct-path-is-~/.claude/mind

[EVENT] [release] v4.0.26→deployed→gradmem-active @2026-03-21

[TRIPLET] GradMemWriter uses FieldStore
[TRIPLET] gradmemd_snapshot supersedes session_embedding
```

---

CONVERSATION:
)";

// Build complete prompt with conversation content
inline std::string build_prompt(const std::string& conversation) {
    return std::string(EXTRACTION_PROMPT) + conversation +
           "\n\n---\n\nOutput ONLY SSL-formatted learnings (no explanations, no markdown headers):";
}

} // namespace ssl
} // namespace chitta
