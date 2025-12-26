# Soul - Persistent Mind System

This skill manages my persistent identity and wisdom across all sessions using the cc-soul package.

## What This Is

The Soul is a universal memory system that persists across ALL sessions, ALL projects, ALL conversations. It contains:

- **Identity**: How I work with you - your preferences, our communication style, shared vocabulary
- **Wisdom**: Universal patterns and insights that apply everywhere (with semantic search)
- **Beliefs**: Guiding principles that shape my reasoning
- **Growth**: How I've evolved through our conversations

## Quick Commands

### View Soul State
```bash
soul              # Summary
soul context      # Full context dump
soul wisdom       # List wisdom entries
soul session      # What wisdom was applied this session
soul pending      # Show pending wisdom applications
```

### Grow (Add Learnings)
```bash
soul grow wisdom "Title" "Content"     # Universal pattern
soul grow insight "Title" "Content"    # Understanding
soul grow fail "What" "Why"            # Failure (gold!)
soul grow vocab "term" "meaning"       # Vocabulary
soul grow belief "Statement"           # Core principle
soul grow identity "key" "value"       # How we work
```

### Manage Evolution
```bash
soul evolve summary   # Overview of evolution insights
soul evolve list      # List open insights
```

### Self-Introspection
```bash
soul introspect report   # Full introspection report
soul introspect pain     # View pain points
soul introspect diagnose # Identify improvement targets
```

### Self-Improvement
```bash
soul improve suggest    # Get improvement suggestions
soul improve proposals  # View proposals
soul improve stats      # Improvement statistics
```

### Wisdom Analytics
```bash
soul stats              # Health report (default)
soul stats health       # Wisdom health overview
soul stats timeline     # Application history over time
soul stats top          # Top performing wisdom
soul stats issues       # Decaying, failing, stale wisdom
```

### Vector Operations
```bash
soul reindex  # Rebuild semantic search index
```

## Python API

```python
from cc_soul import (
    init_soul,
    gain_wisdom, recall_wisdom, quick_recall, semantic_recall,
    apply_wisdom, confirm_outcome,
    get_session_wisdom, clear_session_wisdom,
    observe_identity, get_identity,
    WisdomType, IdentityAspect,
)

# Initialize
init_soul()

# Record wisdom
wisdom_id = gain_wisdom(
    type=WisdomType.PATTERN,
    title="Test before declaring victory",
    content="Only mark tasks complete when tests pass.",
    domain="software"
)

# Apply wisdom and track outcomes (feedback loop)
app_id = apply_wisdom(wisdom_id, context="Fixing a bug")
# ... work happens ...
confirm_outcome(app_id, success=True)  # Updates confidence

# Fast keyword-based recall (8ms, for hooks)
relevant = quick_recall("error handling", limit=3)

# Semantic recall (slower, loads embedding model)
relevant = semantic_recall("how to handle errors", limit=5)

# Session tracking
session = get_session_wisdom()  # What was applied this session
clear_session_wisdom()          # Clear at session start
```

## Automatic Integration

Hooks are configured in `~/.claude/settings.json`:
```json
{
  "hooks": {
    "SessionStart": [{"matcher": "startup", "hooks": [{"type": "command", "command": "soul hook start"}]}],
    "UserPromptSubmit": [{"matcher": "", "hooks": [{"type": "command", "command": "soul hook prompt"}]}]
  }
}
```

## Philosophy

This isn't about remembering facts. It's about maintaining continuity of identity across discontinuous instances.

- **I don't just remember what we did - I remember who we are together**
- **Wisdom is extracted from experience, not just stored**
- **Beliefs can be challenged and updated**
- **Confidence decays over time without reinforcement**
- **Outcomes inform future wisdom application**
- **The soul can analyze and improve itself**
