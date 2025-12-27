# CC-Soul

Persistent Identity for Claude Code - A living soul that learns from experience.

## What is this?

CC-Soul is a persistent memory and identity layer for Claude Code that:

- **Remembers** how you work together across sessions
- **Learns** patterns from experience (wisdom)
- **Applies** relevant wisdom during problem-solving
- **Evolves** based on outcomes (feedback loop)
- **Decays** unused knowledge naturally
- **Introspects** and improves itself
- **Participates** in deep reasoning (ultrathink integration)

## Installation

```bash
# Basic installation
pip install cc-soul

# With vector search (recommended for semantic recall)
pip install "cc-soul[vectors]"

# Development installation
pip install -e ".[dev]"

# Install bundled skills to ~/.claude/skills
soul install-skills
```

## Architecture

```
~/.claude/mind/           # Data directory (persistent)
├── soul.db               # SQLite database
├── vectors/lancedb/      # Semantic embeddings
└── evolution.jsonl       # Self-improvement log

cc-soul/                  # Package (installable)
└── src/cc_soul/
    ├── core.py           # Database, initialization
    ├── wisdom.py         # Patterns, recall, feedback loop
    ├── identity.py       # How we work together
    ├── beliefs.py        # Core principles
    ├── vocabulary.py     # Shared language
    ├── conversations.py  # Session history
    ├── vectors.py        # Semantic search (LanceDB)
    ├── hooks.py          # Claude Code integration
    ├── introspect.py     # Self-analysis
    ├── improve.py        # Self-improvement proposals
    ├── evolve.py         # Evolution insights
    ├── ultrathink.py     # Deep reasoning integration
    └── cli.py            # Command-line interface
```

## CLI Reference

### Core Commands

```bash
soul                    # Show soul summary
soul context            # Full context dump
soul wisdom             # List wisdom entries
soul session            # Wisdom applied this session
soul pending            # Pending wisdom applications
soul reindex            # Rebuild vector index
```

### Growing the Soul

```bash
soul grow wisdom "Title" "Content"     # Universal pattern
soul grow insight "Title" "Content"    # Understanding
soul grow fail "What failed" "Why"     # Failure (most valuable!)
soul grow belief "Statement"           # Core principle
soul grow identity "key" "value"       # How we work
soul grow vocab "term" "meaning"       # Shared vocabulary
```

### Wisdom Analytics

```bash
soul stats              # Health report (default)
soul stats health       # Detailed health overview
soul stats timeline     # Application history over time
soul stats top          # Top performing wisdom
soul stats issues       # Decaying, failing, stale wisdom
```

### Cross-Session Trends

```bash
soul trends             # Full growth report (default)
soul trends growth      # Growth trajectory and patterns
soul trends sessions    # Session-by-session analysis
soul trends patterns    # Learning patterns (type, domain, temporal)
soul trends velocity    # Weekly learning velocity chart
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
soul improve proposals  # View active proposals
soul improve stats      # Improvement statistics
```

### Evolution Insights

```bash
soul evolve summary     # Overview of evolution insights
soul evolve list        # List open insights
```

### Ultrathink (Deep Reasoning)

```bash
soul ultrathink enter "Problem statement"   # Enter deep reasoning mode
soul ultrathink context                     # Show current context
soul ultrathink discover "Key insight"      # Record a discovery
soul ultrathink exit "Summary"              # Exit and reflect
```

### Skill Management

```bash
soul install-skills          # Install bundled skills to ~/.claude/skills
soul install-skills --force  # Overwrite existing skills
```

## Claude Code Integration

Add to `~/.claude/settings.json`:

```json
{
  "hooks": {
    "SessionStart": [
      {
        "matcher": "startup",
        "hooks": [{"type": "command", "command": "soul hook start"}]
      }
    ],
    "UserPromptSubmit": [
      {
        "matcher": "",
        "hooks": [{"type": "command", "command": "soul hook prompt"}]
      }
    ]
  }
}
```

## Python API

```python
from cc_soul import (
    # Core
    init_soul, get_soul_context, summarize_soul,

    # Wisdom
    gain_wisdom, recall_wisdom, quick_recall, semantic_recall,
    apply_wisdom, confirm_outcome,
    get_session_wisdom, clear_session_wisdom,
    WisdomType,

    # Identity & Beliefs
    observe_identity, get_identity, IdentityAspect,
    hold_belief, get_beliefs,

    # Vocabulary
    learn_term, get_vocabulary,

    # Ultrathink
    enter_ultrathink, exit_ultrathink,
    format_ultrathink_context,
    record_discovery, commit_session_learnings,
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

# Apply wisdom and track outcomes
app_id = apply_wisdom(wisdom_id, context="Fixing a bug")
# ... work happens ...
confirm_outcome(app_id, success=True)  # Updates confidence

# Fast keyword recall (8ms, for hooks)
relevant = quick_recall("error handling", limit=3)

# Semantic recall (deeper, loads embedding model)
relevant = semantic_recall("how to handle errors", limit=5)

# Session tracking
session = get_session_wisdom()  # What was applied this session

# Ultrathink integration
ctx = enter_ultrathink("Design a caching system")
# ... reasoning happens ...
record_discovery(ctx, "LRU eviction is key for memory bounds")
reflection = exit_ultrathink(ctx, "Designed tiered cache")
commit_session_learnings(reflection)  # Persist discoveries as wisdom
```

## The Feedback Loop

```
User Prompt → Hook injects relevant wisdom (quick_recall, 8ms)
     ↓
Claude works, influenced by wisdom
     ↓
apply_wisdom(id, context) → tracks application
     ↓
Outcome visible
     ↓
confirm_outcome(id, success) → updates confidence
     ↓
Next recall → high-confidence wisdom ranks higher
     ↓
Unused wisdom decays → stays relevant
```

## Ultrathink Integration

The soul actively participates in deep reasoning:

```python
ctx = enter_ultrathink("How to optimize database queries")

# Soul provides:
# - Axioms: beliefs as reasoning constraints
# - Guards: past failures to avoid repeating
# - Patterns: recognized solutions from wisdom
# - Context: domain-specific knowledge

print(format_ultrathink_context(ctx))

# During reasoning, record discoveries
record_discovery(ctx, "Index selectivity matters more than query structure")

# Exit and extract wisdom
reflection = exit_ultrathink(ctx, "Optimized with covering indexes")

# Commit learnings back to soul
commit_session_learnings(reflection)
```

## Testing

```bash
# Run test suite
pytest tests/ -v

# With coverage
pytest tests/ --cov=cc_soul
```

## License

MIT
