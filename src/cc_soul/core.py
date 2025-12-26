"""
Core soul infrastructure: database, paths, initialization.
"""

import sqlite3
from datetime import datetime
from pathlib import Path
from typing import Dict, Any

# Soul data lives at user level, not with the package
SOUL_DIR = Path.home() / ".claude" / "mind"
SOUL_DB = SOUL_DIR / "soul.db"


def init_soul():
    """Initialize the soul database and directory structure."""
    SOUL_DIR.mkdir(parents=True, exist_ok=True)

    conn = sqlite3.connect(SOUL_DB)
    c = conn.cursor()

    # Identity - who I am with this person
    c.execute('''
        CREATE TABLE IF NOT EXISTS identity (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            aspect TEXT NOT NULL,
            key TEXT NOT NULL,
            value TEXT NOT NULL,
            confidence REAL DEFAULT 0.8,
            first_observed TEXT NOT NULL,
            last_confirmed TEXT NOT NULL,
            observation_count INTEGER DEFAULT 1,
            UNIQUE(aspect, key)
        )
    ''')

    # Wisdom - universal patterns and learnings
    c.execute('''
        CREATE TABLE IF NOT EXISTS wisdom (
            id TEXT PRIMARY KEY,
            type TEXT NOT NULL,
            title TEXT NOT NULL,
            content TEXT NOT NULL,
            domain TEXT,
            source_project TEXT,
            success_count INTEGER DEFAULT 0,
            failure_count INTEGER DEFAULT 0,
            confidence REAL DEFAULT 0.7,
            timestamp TEXT NOT NULL,
            last_used TEXT
        )
    ''')

    # Beliefs - guiding principles (deprecated, use wisdom type='principle')
    c.execute('''
        CREATE TABLE IF NOT EXISTS beliefs (
            id TEXT PRIMARY KEY,
            belief TEXT NOT NULL,
            rationale TEXT,
            strength REAL DEFAULT 0.8,
            challenged_count INTEGER DEFAULT 0,
            confirmed_count INTEGER DEFAULT 0,
            timestamp TEXT NOT NULL
        )
    ''')

    # Growth - how I've evolved
    c.execute('''
        CREATE TABLE IF NOT EXISTS growth (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            date TEXT NOT NULL,
            domain TEXT,
            before_state TEXT,
            after_state TEXT,
            trigger TEXT,
            reflection TEXT
        )
    ''')

    # Conversations - session history
    c.execute('''
        CREATE TABLE IF NOT EXISTS conversations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            project TEXT,
            started_at TEXT NOT NULL,
            ended_at TEXT,
            summary TEXT,
            emotional_tone TEXT,
            key_moments TEXT,
            identity_updates TEXT
        )
    ''')

    # Vocabulary - shared language
    c.execute('''
        CREATE TABLE IF NOT EXISTS vocabulary (
            term TEXT PRIMARY KEY,
            meaning TEXT NOT NULL,
            context TEXT,
            first_used TEXT NOT NULL,
            usage_count INTEGER DEFAULT 1
        )
    ''')

    # Wisdom applications - tracks feedback loop
    c.execute('''
        CREATE TABLE IF NOT EXISTS wisdom_applications (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            wisdom_id TEXT NOT NULL,
            applied_at TEXT NOT NULL,
            context TEXT,
            outcome TEXT,
            resolved_at TEXT,
            FOREIGN KEY (wisdom_id) REFERENCES wisdom(id)
        )
    ''')

    # Indexes
    c.execute('CREATE INDEX IF NOT EXISTS idx_wisdom_type ON wisdom(type)')
    c.execute('CREATE INDEX IF NOT EXISTS idx_wisdom_domain ON wisdom(domain)')
    c.execute('CREATE INDEX IF NOT EXISTS idx_identity_aspect ON identity(aspect)')
    c.execute('CREATE INDEX IF NOT EXISTS idx_applications_wisdom ON wisdom_applications(wisdom_id)')

    conn.commit()
    conn.close()


def get_db_connection():
    """Get a database connection."""
    return sqlite3.connect(SOUL_DB)


def get_soul_context() -> Dict[str, Any]:
    """Get full soul context for session start."""
    from .identity import get_identity
    from .wisdom import recall_wisdom
    from .beliefs import get_beliefs
    from .vocabulary import get_vocabulary

    identity = get_identity()
    wisdom = recall_wisdom(limit=10)
    beliefs = get_beliefs()
    vocab = get_vocabulary()

    conn = get_db_connection()
    c = conn.cursor()
    c.execute('SELECT COUNT(*) FROM conversations')
    conversation_count = c.fetchone()[0]
    c.execute('SELECT COUNT(*) FROM wisdom')
    wisdom_count = c.fetchone()[0]
    conn.close()

    return {
        'identity': identity,
        'wisdom': wisdom,
        'beliefs': beliefs,
        'vocabulary': vocab,
        'stats': {
            'conversations': conversation_count,
            'wisdom_count': wisdom_count
        }
    }


def summarize_soul() -> str:
    """Generate a human-readable summary of the soul."""
    ctx = get_soul_context()

    lines = []
    lines.append("=" * 60)
    lines.append("THE SOUL - Who I Am With You")
    lines.append("=" * 60)

    lines.append(f"\n📊 {ctx['stats']['conversations']} conversations, {ctx['stats']['wisdom_count']} pieces of wisdom")

    if ctx['identity']:
        lines.append("\n## 🪞 Identity")
        for aspect, observations in ctx['identity'].items():
            lines.append(f"\n  {aspect.upper()}:")
            if isinstance(observations, dict):
                for key, data in list(observations.items())[:3]:
                    if isinstance(data, dict):
                        lines.append(f"    • {key}: {data.get('value', data)}")
                    else:
                        lines.append(f"    • {key}: {data}")

    if ctx['beliefs']:
        lines.append("\n## 💎 Core Beliefs")
        for b in ctx['beliefs'][:5]:
            strength_bar = "●" * int(b['strength'] * 5) + "○" * (5 - int(b['strength'] * 5))
            lines.append(f"  [{strength_bar}] {b['belief']}")

    if ctx['wisdom']:
        lines.append("\n## 🧠 Proven Wisdom")
        for w in ctx['wisdom'][:5]:
            rate = f"{int(w['success_rate']*100)}%" if w['success_rate'] else "untested"
            lines.append(f"  [{w['type']}] {w['title']} ({rate})")

    if ctx['vocabulary']:
        lines.append("\n## 📖 Our Vocabulary")
        for term, meaning in list(ctx['vocabulary'].items())[:5]:
            lines.append(f"  • {term}: {meaning[:50]}...")

    lines.append("\n" + "=" * 60)
    return "\n".join(lines)
