# Handler Complexity Metrics — full scan

Date: 2026-04-28
Scope: all 286 handlers in `chitta/include/chitta/rpc/handlers/*.hpp` (28 files).
Method: regex-extracted handler bodies (`tool_*` / `handle_*` returning ToolResult/McpResult/json), measured LOC, `field_store_` calls, JSON ops, embedding calls.

Classification rule:
- **PASS_THROUGH** — ≤10 LOC, ≤1 field_store call, no embedding.
- **REAL_LOGIC** — ≥50 LOC, or any embedding call.
- **THIN_GLUE** — everything else (≤30 LOC, ≤3 field_store calls).

## Headline

| Class | Count | Share |
|---|---:|---:|
| PASS_THROUGH | 65 | 22.7% |
| THIN_GLUE | 125 | 43.7% |
| REAL_LOGIC | 96 | 33.6% |
| **Total** | **286** | |

**Correction to boundary-investigation report (2026-04-28):** the "FFI is 85–90% pass-through" claim was extrapolated from a 10-handler sample biased toward `field_memory_ops`. Full scan shows pass-through is **22.7%**, not 45%. Real-logic share is **33.6%**, not 25%. The collapse case still stands directionally, but the cost is materially higher than the report implied.

## Per-subsystem

```
subsystem                           n  avgLOC   PT%   TG%   RL%  avgFS
------------------------------------------------------------------------
compact                             1   272.0     0     0   100    2.0
constraint                         12    16.9    25    67     8    1.1
drift_5w                            2    72.0     0     0   100    1.5
drift_consolidation                 2    47.0     0    50    50    1.5
drift_probe                         4    44.8     0    25    75    0.8
drift_recall                        2    35.5     0     0   100    2.0
drift_recon                         4    33.0     0    50    50    1.8
field_agent                         4    12.5    50    50     0    1.0
field_code_intel                   22    44.9     9    32    59    2.1
field_contradiction                 3    47.3     0     0   100    2.7
field_distill                      17    18.1    12    76    12    1.2
field_lookup                        1   240.0     0     0   100    6.0
field_memory_ops                   28    23.9    39    43    18    2.3
field_memory_recall                 8    68.9     0    25    75    3.8
field_memory_structured             2    76.5     0     0   100    3.5
field_misc                         76    19.1    26    58    16    0.9
field_operator                      6    35.8     0    50    50    2.7
field_session                      21    44.0     0    38    62    1.2
field_skill                         5    13.4    40    60     0    1.0
field_system                       32    28.0    47    19    34    1.5
ledger                              5    44.6     0    20    80    1.0
long_task                           9    53.0     0    11    89    2.4
meta_memory                        14    15.8    36    64     0    1.0
repl_sessions                       5    11.6    60    40     0    1.0
trajectory_compact                  1   309.0     0     0   100    0.0
```

## Decision matrix (per investigation-report rule)

Rule: collapse if >70% (PT+TG) and avgLOC <30; translate if >40% RL and avgLOC ≥40; otherwise mixed.

| Subsystem | Decision | Why |
|---|---|---|
| field_agent, field_skill, repl_sessions | **Collapse** | small, high PT, no real logic |
| meta_memory, constraint, field_distill | **Collapse** | high TG, low LOC, simple field_store delegation |
| field_misc (76 handlers!) | **Collapse** | 84% PT+TG, avg 19 LOC — biggest single win |
| field_memory_ops | **Collapse** | 28 handlers, 82% PT+TG, avg 24 LOC |
| field_system | **Collapse mostly** | 47% PT but 34% RL — split-and-collapse |
| field_code_intel | **Translate** | 22h, 59% RL, semantic logic; tree-sitter stays C++ |
| field_session | **Translate** | 21h, 62% RL, avg 44 LOC |
| long_task | **Translate** | 89% RL, avg 53 LOC |
| field_memory_recall | **Translate carefully** | embedding/MMR — keep yantra FFI |
| compact / trajectory_compact / field_lookup | **Read first** | three handlers totaling ~820 LOC; may hide reusable algorithms |
| drift_*, ledger, contradiction, operator, structured | **Translate** | small handler counts but RL-dominant |

## Refined estimate vs. report

The report's "240 thin glue handlers" is **190 handlers** (PT+TG). The report's "81 real logic handlers" is **96 handlers** — 18% more rewrite surface.

Rough cost re-estimate:
- 190 PT/TG handlers × ~30 min each (rewrite as Rust methods on FieldStore) ≈ **95 hours**.
- 96 RL handlers × ~3 hours each (translate algorithm + verify behaviour) ≈ **290 hours**.
- Plus FFI surface reduction, HTTP daemon decision, embedding boundary review.

Total order-of-magnitude: **400–500 engineer-hours**, not the "thin bridge rewrite" the original report suggested.

## What to read before committing

Three handlers carry ~820 LOC of real logic and could not be classified from regex alone:
- `handlers/compact.hpp:tool_*` (272 LOC)
- `handlers/trajectory_compact.hpp:tool_*` (309 LOC)
- `handlers/field_lookup.hpp:tool_*` (240 LOC)

These belong on a 1-hour read pass before any plan-2 commitment.

## Files

- Raw scan: `/tmp/handler_scan.json` (transient)
- This report: `docs/refactor/2026-04-28-handler-metrics.md`
- Predecessor: `docs/refactor/2026-04-28-boundary-investigation.md`
