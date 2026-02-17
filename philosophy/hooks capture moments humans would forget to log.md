# Hooks Capture Moments Humans Would Forget to Log

Relying on explicit "remember this" commands fails because the moments worth remembering are often recognized only in retrospect. Hooks observe conversation flow and capture automatically.

## Mechanism

cc-soul uses three Claude Code hooks:

1. **SessionStart** - Registers session, loads context, surfaces relevant memories
2. **UserPromptSubmit** - Detects corrections, extracts learning signals before Claude responds
3. **Stop** - Captures learnings, records outcomes, triggers distillation

Each hook runs shell scripts that invoke chitta tools without requiring user action.

## Evidence

- **Prospective memory** research shows humans forget ~50% of intended actions
- **Zeigarnik effect** - incomplete tasks drain attention until captured
- **Auto-commit patterns** - Version control proves automated capture beats manual commits

## Implications

- Capture happens at natural boundaries (session start/stop, before response)
- User doesn't need to "remember to remember"
- Hooks can be intrusive if misconfigured (hence conservative defaults)

## Trade-offs

- Hook failures block Claude Code operation
- Over-capturing creates noise
- Users lose visibility into what's being stored

## Related Claims

- [[corrections have highest learning value]]
- [[subconscious processes what foreground attention cannot]]
- [[queue processing decouples capture from storage]]
