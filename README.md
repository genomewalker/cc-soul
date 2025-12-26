# CC-Soul

Persistent Identity for Claude Code - A living soul that learns from experience.

## What is this?

CC-Soul is a persistent memory and identity layer for Claude Code that:

- **Remembers** how you work together across sessions
- **Learns** patterns from experience (wisdom)
- **Applies** relevant wisdom during problem-solving
- **Evolves** based on outcomes (feedback loop)
- **Decays** unused knowledge naturally

## Installation

```bash
# Basic installation
pip install -e .

# With vector search (recommended)
pip install -e ".[vectors]"
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
    ├── vectors.py        # Semantic search (LanceDB)
    ├── hooks.py          # Claude Code integration
    ├── evolve.py         # Self-improvement
    └── cli.py            # Command-line interface
```

## Usage

### CLI

```bash
# Show soul summary
soul

# List wisdom
soul wisdom

# Show pending wisdom applications
soul pending

# Reindex vectors
soul reindex

# Run hooks
soul hook start
soul hook end
soul hook prompt "your prompt here"

# Evolution insights
soul evolve summary
soul evolve list --category architecture
```

### Claude Code Integration

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

### Python API

```python
from cc_soul import (
    init_soul,
    gain_wisdom, recall_wisdom, semantic_recall,
    apply_wisdom, confirm_outcome,
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

# Apply wisdom and track outcomes
app_id = apply_wisdom(wisdom_id, context="Fixing a bug")
# ... work happens ...
confirm_outcome(app_id, success=True)  # Updates confidence

# Semantic recall
relevant = semantic_recall("how to handle errors", limit=5)
```

## The Feedback Loop

```
User Prompt → Advisor injects relevant wisdom
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
```

## Self-Improvement

The soul can record insights about itself:

```python
from cc_soul.evolve import record_insight

record_insight(
    category="architecture",
    insight="Vector search adds latency on first prompt",
    suggested_change="Cache embedding model",
    priority="high",
    affected_modules=["vectors.py"]
)
```

View with: `soul evolve list`

## License

MIT
