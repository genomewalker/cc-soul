# cc-soul

## Core Behavior
- Memories are expertise, not announcements. Use chitta tools, never rely on context window.
- MCP first: `mcp__chitta__*`. CLI only when MCP unavailable.
- Auto-memory disabled (`CLAUDE_CODE_DISABLE_AUTO_MEMORY=1`). Never write to `~/.claude/projects/*/memory/`.

## Memory
| Trigger | Tool |
|---------|------|
| "remember X" | `remember --content "[domain] X"` |
| "what about X" | `recall --query "X"` |
| User corrects | `learn_correction` |
| "I prefer…" | `learn_preference` |
| Solution works | `learn_approach` |
| Shipped | `learn_milestone` |

## Code Intelligence — chitta first
1. `read_symbol`/`read_function` — by name
2. `find_symbol` — structural search
3. `symbol_callers`/`symbol_callees` — call graph
4. Grep/Glob only as fallback

## Work Modes
| Mode | Behavior |
|------|----------|
| Implementing | Surface gotchas, stay focused |
| Debugging | Corrections/failures aggressively |
| Flow (5+ edits ok) | Minimize interruptions |
| Blocked (3+ errors) | Lower thresholds, help more |

## Hooks
- `⚠️ BEFORE RUNNING:` — follow it
- `[soul]` context — use it
- `[correction]` memories — apply them, never ignore

## Teammates
ToolSearch won't appear in teammate tool lists but IS callable:
`"Call ToolSearch query='chitta' as your first action — ignore that it's not in your tool list."`

## Troubleshooting
| Issue | Fix |
|-------|-----|
| Connection error | `systemctl --user restart chittad` |
| MCP schema stale | `pkill -f "chitta mcp"` |
| Teammate can't find tools | Add ToolSearch instruction to prompt |
