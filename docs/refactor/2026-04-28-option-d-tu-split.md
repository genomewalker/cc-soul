# Option D — Split handlers into translation units

Date: 2026-04-28
Status: proposal

## The problem

Builds touching anything in `chitta/include/chitta/rpc/` take 20+ minutes
because `field_handler.hpp` is a 3,156-line header-only class with **28
handler files (286 handlers) `#include`d into its class body**. Every TU
that pulls `field_handler.hpp` (chittad, chitta_tool, chitta_thinking,
chitta_migrate) re-instantiates every handler's templates — primarily
`nlohmann/json` machinery — on every change.

Demolition pass-1's analysis named this directly: *"41 `field_*.hpp`
headers all funnelling into one 3,156-line `field_handler.hpp`."* Pass-2
treated it as a *consequence* of the C++/Rust boundary; it isn't.
**It's an independent structural problem and the dominant contributor
to "code is hard to change."**

## What "split into TUs" means

Today:
```
field_handler.hpp:
    class FieldRpcHandler {
        ...
        #include "handlers/field_memory_ops.hpp"    // 28 handler bodies
        #include "handlers/field_code_intel.hpp"    //   inlined into class
        #include "handlers/field_session.hpp"       //   via textual splice
        ... (28 files, 286 methods)
    };
```

Target:
```
field_handler.hpp:        // declarations only
    class FieldRpcHandler {
        ToolResult tool_remember(const json&);
        ToolResult tool_recall(const json&);
        ... (286 method declarations)
    };

handlers/field_memory_ops.cpp:    // bodies in proper TUs
    ToolResult FieldRpcHandler::tool_remember(const json& p) { ... }
    ToolResult FieldRpcHandler::tool_forget(const json& p)   { ... }
    ...
```

## Why this is the highest-leverage refactor on the table

| Option | Effort | Build-time win | Risk | Touches semantics |
|---|---|---|---|---|
| A. Collapse C++ → Rust | 400–500h | yes (eventually) | high (language port) | yes |
| B. Formalise split | 40–80h | no | medium | yes (boundary contract) |
| C. Utility hoists | 2–4h | marginal | none | no |
| **D. TU split** | **8–16h** | **20min → 1–2min** | **low** | **no** |

D directly attacks the "feedback loop is slow" pain — which is the real
expression of "hard to change." A and B don't fix that until very late.
C doesn't fix it at all.

## Approach (one PR, one direction)

1. **Move FieldRpcHandler header → .hpp/.cpp split.** Methods declared
   in `field_handler.hpp`, bodies move into `field_handler.cpp` (or
   one `.cpp` per `handlers/*` group).

2. **Per-group TU files.** Convert `handlers/field_memory_ops.hpp`
   to `handlers/field_memory_ops.cpp` containing the bodies of those
   28 methods as `ToolResult FieldRpcHandler::tool_remember(...) { ... }`.

3. **Lambdas / locals stay where they are.** No need to refactor body
   contents — just textual move from `class { ... }` to TU scope.

4. **Common helpers (`embed_query`, `display_path`, etc.)** that were
   previously in-class lambdas or inline helpers may need to become
   private member functions or free functions in `text_utils.hpp` /
   a new `handler_helpers.hpp`. Audit cost: ~1h.

5. **CMake**: add the new `.cpp` files as sources; `field_handler.hpp`
   stops being compiled per-TU and becomes a normal forward-declared
   class header.

## Cost estimate

- Mechanical move: 28 files × ~30 min each = **14h**
- Helper hoisting (lambdas → private methods or free functions): **2h**
- Build verification, fixing private/public access for any helpers: **2h**
- Total: **~16–20h**, one engineer, one week of part-time or 2 days
  focused.

## Risks

| Risk | Mitigation |
|---|---|
| Header-defined lambdas captured private state — moving to free function breaks access | Convert those into private member functions, not free functions |
| Some handlers reference each other (helper handlers calling helper handlers) | Bodies move together; class-internal calls still resolve via `this->` |
| Build flags differ between TUs (e.g. one expects an extra include) | Single CMake target; same flags applied |
| Behaviour change | None expected — purely mechanical move; existing tests catch regressions |

## What this does NOT do

- Doesn't change the C++/Rust boundary.
- Doesn't change FFI surface.
- Doesn't change handler logic.
- Doesn't pre-empt option A or B — both become *easier* afterward
  because handlers become individually visible TUs instead of one
  monolithic header.

## Recommendation

**Do D first, then re-evaluate A/B.** With 1–2 minute incremental
builds, the cost calculus on the bigger refactors changes — A becomes
genuinely tractable, B becomes obviously cheap, and even option C's
utility hoists land in seconds instead of needing 20-minute build
cycles to verify.

If A/B are deferred indefinitely, D still pays for itself within a
month of normal development.
