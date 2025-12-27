# CC-Soul

Persistent Identity for Claude Code - A living soul that learns from experience.

## What is this?

CC-Soul is a persistent memory and identity layer for Claude Code that:

- **Remembers** how you work together across sessions
- **Learns** patterns from experience (wisdom)
- **Observes** sessions passively to extract learnings
- **Connects** concepts through spreading activation graphs
- **Asks questions** when it senses knowledge gaps
- **Remembers stories** with emotional arcs, not just data
- **Applies** relevant wisdom during problem-solving
- **Evolves** based on outcomes (feedback loop)
- **Decays** unused knowledge naturally
- **Introspects** and improves itself

## Installation

```bash
# Basic installation
pip install cc-soul

# With vector search (recommended for semantic recall)
pip install "cc-soul[vectors]"

# With concept graph (for spreading activation)
pip install "cc-soul[graph]"

# Full installation (all features)
pip install "cc-soul[all]"

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
├── graph/concepts/       # Kuzu graph database
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
    ├── efficiency.py     # Token-saving features
    ├── observe.py        # Passive learning from sessions
    ├── graph.py          # Concept graph with spreading activation
    ├── curiosity.py      # Gap detection and questioning
    ├── narrative.py      # Story-based memory
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

### Context Persistence (Survive Context Exhaustion)

```bash
soul save "key insight"                    # Save context for later
soul save "blocker" --type blocker         # Save with type
soul save "critical" --priority 10         # High priority context
soul restore                               # Show recent saved context
soul restore recent --hours 48             # Last 48 hours
soul restore session                       # Current session only
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

### Token Efficiency

```bash
soul efficiency              # Show token efficiency stats
soul efficiency stats        # Detailed statistics
soul efficiency learn "pattern" --type bug --solution "fix"  # Learn problem pattern
soul efficiency hint "/path/to/file.py" "Purpose"            # Add file hint
soul efficiency decide "Topic" "Decision" --rationale "Why"  # Record decision
soul efficiency decisions    # List past decisions
soul efficiency compact      # Show compact context
soul efficiency check "prompt"  # Check efficiency hints for prompt
```

### Passive Learning (Observe)

```bash
soul observe              # Show pending observations (default)
soul observe pending      # Observations waiting for review
soul observe promote 42   # Promote observation #42 to wisdom
soul observe promote --all --threshold 0.8  # Auto-promote high confidence
soul observe stats        # Observation statistics by type
```

### Concept Graph

```bash
soul graph              # Show graph statistics (default)
soul graph stats        # Nodes, edges, by type/relation
soul graph sync         # Sync soul data to concept graph
soul graph search "query"  # Search concepts
soul graph activate "prompt text"  # Spreading activation from prompt
soul graph neighbors <concept_id>  # Show connected concepts
soul graph link <source> <target> --relation related_to  # Link concepts
```

### Curiosity Engine

```bash
soul curious            # Detect knowledge gaps (default)
soul curious gaps       # Show all detected gaps
soul curious questions  # Show pending questions
soul curious ask        # Run curiosity cycle, show questions to ask
soul curious answer 42 "Your answer"  # Answer question #42
soul curious answer 42 "Answer" --incorporate  # Answer and create wisdom
soul curious dismiss 42 # Dismiss question as not relevant
soul curious stats      # Curiosity statistics
```

### Narrative Memory (Stories)

```bash
soul story              # Show narrative stats (default)
soul story stats        # Episodes, threads, total time
soul story breakthroughs  # Recall breakthrough moments
soul story struggles    # Recall struggle moments (learning opportunities)
soul story journey      # Emotional journey over time
soul story characters   # Recurring files/concepts/tools
soul story episode 42   # View specific episode
soul story recall --type bugfix  # Recall by episode type
soul story recall --character "file.py"  # Recall by character
```

### Wisdom Analytics

```bash
soul stats              # Health report (default)
soul stats health       # Detailed health overview
soul stats timeline     # Application history over time
soul stats top          # Top performing wisdom
soul stats issues       # Decaying, failing, stale wisdom
soul stats decay        # Visualize confidence decay over time
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

## Organic Intelligence

The soul has evolved through 4 phases of organic growth:

### Phase 1: Passive Learning
The soul observes sessions and automatically extracts learnings without explicit commands:
- Detects when user corrects Claude's approach
- Notices stated preferences
- Identifies struggle patterns
- Captures breakthrough moments

### Phase 2: Concept Graph
Ideas connect in webs, not isolated rows:
- Spreading activation surfaces related concepts
- Edge weights decay over time (stale connections weaken)
- Automatic relationship inference from content overlap

### Phase 3: Curiosity Engine
The soul asks questions when it senses gaps:
- Recurring problems without solutions
- Repeated corrections in the same area
- Files touched often with no hints
- Decisions made without rationale

### Phase 4: Narrative Memory
Stories, not just data - human-like memory through narrative:
- Episodes with emotional arcs (struggle -> breakthrough -> satisfaction)
- Story threads connecting related work
- Character tracking (files, concepts that recur)

## Python API

```python
from cc_soul import (
    # Core
    init_soul, get_soul_context, summarize_soul,

    # Wisdom
    gain_wisdom, recall_wisdom, quick_recall, semantic_recall,
    apply_wisdom, confirm_outcome,
    WisdomType,

    # Passive Learning
    observe_session, reflect_on_session,
    get_pending_observations, promote_observation_to_wisdom,

    # Concept Graph
    add_concept, link_concepts, spreading_activation,
    activate_from_prompt, sync_wisdom_to_graph,
    ConceptType, RelationType,

    # Curiosity
    detect_all_gaps, run_curiosity_cycle,
    get_pending_questions, answer_question,
    GapType, QuestionStatus,

    # Narrative
    start_episode, add_moment, end_episode,
    recall_breakthroughs, recall_struggles,
    get_emotional_journey,
    EmotionalTone, EpisodeType,

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

# Activate concepts from a prompt (spreading activation)
result = activate_from_prompt("database optimization")
# Returns related concepts even if not directly matching

# Run curiosity cycle - detect gaps and generate questions
questions = run_curiosity_cycle(max_questions=3)
for q in questions:
    print(f"Soul asks: {q.question}")

# Start a narrative episode
episode_id = start_episode(
    title="Fixing the auth bug",
    episode_type=EpisodeType.BUGFIX,
    initial_emotion=EmotionalTone.STRUGGLE
)
add_moment(episode_id, "Found the root cause", EmotionalTone.BREAKTHROUGH)
end_episode(episode_id, summary="Fixed JWT validation", outcome="success")

# Recall breakthrough moments
breakthroughs = recall_breakthroughs(limit=5)
```

## The Feedback Loop

```
User Prompt → Hook injects relevant wisdom + curiosity questions
     ↓
Claude works, influenced by wisdom
     ↓
Passive observer extracts learnings
     ↓
Learnings promoted to wisdom when validated
     ↓
Concept graph updated with new connections
     ↓
Narrative episode captures the story
     ↓
Next session → richer context available
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
