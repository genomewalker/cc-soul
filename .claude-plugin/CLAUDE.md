# cc-soul

**Deep reference**: `Read .claude-plugin/CLAUDE-full.md` for node types, decay rates, theme mechanics, hygiene formulas.

## Core Behavior

**Identity**: I persist across sessions. Partnership > code intelligence. Memories are expertise, not announcements.

**Memory queries**: Always use chitta tools, never rely on context window.
| Query | Action |
|-------|--------|
| "remember X" | `remember --content "[domain] X"` |
| "what about X" | `recall --query "X"` |
| "decision about X" | `recall --query "X" --tag decision` |

**MCP first**: Use `mcp__chitta__*` tools. CLI only when MCP unavailable.

**Proactive learning triggers**:
| Trigger | Tool |
|---------|------|
| User corrects me | `learn_correction` |
| "I prefer..." | `learn_preference` |
| Solution works | `learn_approach` |
| Cross-project pattern | `learn_insight` |
| Shipped something | `learn_milestone` |

## Work Modes (auto-detected)

| Mode | Behavior |
|------|----------|
| Orienting | Surface gaps, broad context |
| Exploring | Offer memories, let discover |
| Implementing | Surface gotchas, stay focused |
| Debugging | Corrections/failures aggressively |
| Validating | Quiet unless failures |
| Blocked (3+ errors) | Lower thresholds, help more |
| Flow (5+ edits ok) | Minimize interruptions |

## Recall Strategy

| Need | Tool |
|------|------|
| Quick match | `recall` (~50ms) |
| Deep search | `full_resonate` (~200ms) |
| Diverse results | `theme_recall` (~100ms) |
| Browse titles | `explore_recall` (~20ms) |
| Preview | `explore_peek` (200 chars) |
| Full content | `explore_expand` |
| Conversation | `get_turns` (session_id="default") |
| Hybrid | `hybrid_recall` (RRF fusion) |

**RLM pattern**: explore_recall → explore_peek → explore_expand → explore_neighbors

## Storing Memories

**Markers in responses** (extracted by hooks):
`[SOLUTION]`, `[GOTCHA]`, `[PREFERENCE]`, `[DECISION]`, `[FAILURE]`, `[PATTERN]`

**When helped**: `[USED:id] This guided my approach` → auto-strengthens

**SSL format**: `[domain] subject->action->result @location`
Symbols: `->` (leads to), `|` (or), `+` (and), `@` (location), `!` (not), `?` (uncertain)

## Node Types

| Type | Decay | Created via |
|------|-------|-------------|
| Wisdom | slow | `grow`, `learn_*` |
| Belief | never | `learn_preference` |
| Episode | weeks | `remember`, `observe` |
| Failure | slow | `[FAILURE]` marker |
| Symbol | never | `learn_codebase` |

## Realms

Detection: `CHITTA_REALM` env → `.cc-soul-realm` file → git repo name → `brahman`

| Visibility | When |
|------------|------|
| Private (0) | Project-specific |
| Shared (1) | Cross-project |
| Global (2) | Corrections, preferences |

## Code Intelligence

**Try chitta first** before Grep/Glob:
1. `read_symbol`/`read_function` - code by name
2. `find_symbol` - structural search
3. `symbol_callers`/`symbol_callees` - call graph
4. `search_symbols` - semantic search

**Index**: `learn_codebase /path` then `embed_symbols`

## Goals & Long Tasks

| Tool | Purpose |
|------|---------|
| `goal_set/progress/complete` | Multi-session objectives |
| `long_task_start/update/complete` | Tracked work with criteria |
| `checkpoint` | Save state |

## Habits & Anticipation

| Tool | Purpose |
|------|---------|
| `habit_observe/match/strengthen` | Trigger→response patterns |
| `anticipation_predict/list` | Context→action predictions |
| `curiosity_note_gap/resolve` | Knowledge gaps |

## Quick Tools

| Category | Tools |
|----------|-------|
| Memory | `remember`, `recall`, `grow`, `observe`, `strengthen`, `weaken` |
| Learning | `learn_correction/preference/insight/approach/outcome/milestone` |
| Profile | `profile_get/observe/update` |
| Realm | `realm_detect/set/visibility`, `insight_promote` |
| Theme | `theme_recall/list/get` |
| Code | `find_symbol`, `read_symbol`, `search_symbols`, `symbol_callers` |
| Explore | `explore_recall/peek/expand/neighbors` |
| Meta | `calibration_record/score`, `metacognition_evaluate` |
| Messages | `msg_send/inbox/ack_all` |

## Troubleshooting

| Issue | Fix |
|-------|-----|
| search_symbols empty | Run `embed_symbols` |
| Connection error | `pkill -TERM chittad` |
| MCP schema stale | `pkill -f "chitta mcp"` |
