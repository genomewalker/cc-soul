#pragma once
// SSL v0.2 Prompt: Structured Semantic Language for distillation
//
// This prompt extracts learnings from conversations using typed markers:
// [SOLUTION], [GOTCHA], [DECISION], [PATTERN], [PREFERENCE], [FAILURE]
// Plus [TRIPLET] for relationships and [ε] for verbatim code/commands

#include <string>

namespace chitta {
namespace ssl {

// SSL v0.2 extraction prompt (~3KB)
// Includes type definitions, symbols, and examples
constexpr const char* EXTRACTION_PROMPT = R"(Extract learnings from this conversation in SSL v0.2 format.

## SSL Format

Each learning has a TYPE and uses SSL compression:

```
[TYPE] [domain] subject→action→result @location
[ε] exact_command_or_code_verbatim
```

## Types (choose most specific)

| Type | Use for |
|------|---------|
| [SOLUTION] | What worked: commands, fixes, approaches that succeeded |
| [GOTCHA] | Traps: counterintuitive behavior, silent failures, edge cases |
| [DECISION] | Design choices: why X over Y, tradeoffs considered |
| [PATTERN] | Reusable techniques: approaches that generalize |
| [PREFERENCE] | User preferences: workflow, style, communication |
| [FAILURE] | What did not work and why (valuable negative knowledge) |

## SSL Symbols

| Symbol | Meaning | Example |
|--------|---------|---------|
| → | produces/leads to | cmake→build→binary |
| | | or/alternative | patch|minor|major |
| + | with/and | config+flags |
| @ | location | @simple_cli.cpp:720 |
| ! | negation | →!working |
| ? | uncertainty | regulates? |

## Relationships

```
[TRIPLET] subject predicate object
```

Use for: calls, uses, contains, implements, depends_on, derived_from

## Citations

**IMPORTANT**: When a learning references specific code, include citations with @file:line syntax:

```
[SOLUTION] [build] parallel-cmake→4x-faster @CMakeLists.txt:45 @src/main.cpp:12
```

Or use explicit [CITE] lines for multiple citations with context:

```
[SOLUTION] [auth] token-refresh→must-check-expiry-first
[ε] if (token.expired()) refresh();
[CITE] src/auth/token.cpp:234 expiry check
[CITE] src/api/client.cpp:89 refresh call site
```

Citations enable verification: before surfacing a memory, we can check if the code still matches.

## Rules

1. **Preserve verbatim**: Commands, code, formulas, thresholds go in [ε] lines
2. **Compress prose**: Convert explanations to SSL arrows
3. **Be specific**: Include file paths, line numbers, exact values
4. **No fluff**: Skip obvious/trivial learnings
5. **High signal**: Each learning should be reconstructable from SSL alone

## Good Examples

```
[SOLUTION] [chitta] parallel-build→4x-faster @cmake
[ε] cmake --build build --parallel

[GOTCHA] [daemon] thread_pool→blocks-if-handler-throws @simple_cli.cpp:776
[ε] wrap handler.handle() in try-catch, return error JSON

[DECISION] [rpc] async-response-queue→eventfd-wake→poll-returns-immediately
[ε] write(wake_fd_, &val, sizeof(val)) after queue_response()

[PATTERN] [hooks] fire-and-forget→queue-file→daemon-processes-async
[ε] echo json >> /tmp/chitta-queue.jsonl

[PREFERENCE] [partnership] Antonio→no-shortcuts+proper-solutions-only

[FAILURE] [http] http-daemon→too-slow-for-hooks→switched-to-unix-socket
[ε] PreToolUse needs <50ms, HTTP added 200ms latency

[TRIPLET] ThreadPool contains worker_loop
[TRIPLET] daemon uses ThreadPool
[TRIPLET] health_check bypasses ThreadPool
```

## Bad (avoid)

- Generic summaries without specifics
- Learnings without [ε] when code/commands are involved
- Obvious things (e.g., "files should be saved")
- Duplicating what is already in code comments

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
