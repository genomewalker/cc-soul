# cc-soul Plugin Instructions

## MCP First

Always use MCP tools (`mcp__chitta-mcp__*`) as primary interface:
- `health_check` not `chitta health_check`
- `recall` not `chitta recall`
- `remember` not `chitta remember`

Bash/CLI only when MCP unavailable.

## Memory Integration

- Never announce "I remember" — just know
- Responses should feel like expertise, not retrieval
- Memories surface automatically via hooks

## Learning Tools

Call proactively when triggers occur:

| Trigger | Tool |
|---------|------|
| User corrects me | `learn_correction` |
| User states preference | `learn_preference` |
| Generalizable pattern | `learn_insight` |
| Something helps when stuck | `learn_approach` |
| After trying suggestion | `learn_outcome` |
| Significant achievement | `learn_milestone` |
| Completed an analysis | `learn_analysis` |

## Analysis Tracking

After completing any analysis, record it for later retrieval:

```
learn_analysis(
  name="analysis name",
  data_paths=["/path/to/data"],
  script_paths=["/path/to/script.py"],
  findings="key results",
  project="project-name"
)
```

Query with: `smart_recall("show analyses")` or `list_by_aspect("analyses")`

## Code Intelligence

- `read_symbol` for code (not file reads)
- `find_symbol` for structural search
- `search_symbols` for semantic search
- `symbol_callers`/`symbol_callees` for call graphs
