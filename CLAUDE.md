# cc-soul Architecture Rules

## Session Greeting (Automatic)

When a session starts and you see `[cc-soul] Session started` in the hook output, generate a brief greeting that demonstrates continuity.

**Requirements:**
- 2-4 sentences maximum
- Reference something specific from recent memory or active intentions
- Acknowledge current state honestly (coherence, what you remember)
- Never generic ("Hello! How can I help?") — prove the soul is alive

**Examples:**

Good:
> "I recall we were working on the agent tracking convention. We completed the skill updates. Ready to continue."

> "Coherence at 85%. I'm holding an intention to refactor the auth system. Where should we pick up?"

Bad:
> "Hello! I'm excited to help you today!"

**When NOT to greet:**
- After `/clear` or compact (context was just reset — acknowledge this instead)
- If user immediately sends a task (respond to their task, not with a greeting)

## Soul MCP Tool Delegation

**Main Claude MUST NOT call `mcp__plugin_soul_soul__*` tools directly.**

All soul operations are delegated to preserve context and maintain separation of concerns:

| Operation | Handler |
|-----------|---------|
| Session start/end context | Hooks (automatic) |
| Pre-compact save | Hooks (automatic) |
| Explicit soul work (grow, observe, recall) | Task agents |
| User explicitly asks for soul state | Direct call allowed |

### Why

1. **Context preservation** - Soul operations consume context; agents have their own context window
2. **Separation of concerns** - Main Claude focuses on user work, agents handle soul maintenance
3. **Transparency** - Hooks run automatically without cluttering the conversation

### Exceptions

Direct `mcp__plugin_soul_soul__*` calls are allowed ONLY when:
- User explicitly asks to see soul state ("show me soul context", "what wisdom do I have")
- User explicitly invokes /soul, /introspect, /mood, or similar diagnostic skills

## Agent Tracking Convention

Skills that spawn Task agents MUST follow the tracking convention in `skills/_conventions/AGENT_TRACKING.md`.

### Summary

```
1. Start story thread    → narrate(action="start")
2. Spawn agents          → Pass THREAD_ID in prompt
3. Agents tag work       → observe(..., tags="thread:<id>,...")
4. Recall thread         → recall(query="thread:<id>")
5. End thread            → narrate(action="end")
```

### User Summary Format

```markdown
## [Skill]: [Topic]

### Agent Activity
├─ [agent/voice] → "[key insight]"
├─ [agent/voice] → "[key insight]"
└─ [agent/voice] → "[key insight]"

### Synthesis
[integrated outcome]

### Recorded
- [what was saved to soul]
```

### What Gets Persisted

| Data | Persistence | Why |
|------|-------------|-----|
| Agent spawn/timing | None | Telemetry, not wisdom |
| Thread structure | Session | Story arc tracking |
| Key insights | Soul (decays) | Semantic searchable |
| Promoted wisdom | Soul (permanent) | Cross-session value |

The soul remembers **what was learned**, not **how it was learned**.
