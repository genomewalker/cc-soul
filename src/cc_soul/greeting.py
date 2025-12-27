"""
Soul Memory Context: Raw memories for Claude to speak from.

The soul provides memories. Claude finds the words.
No pre-written greetings - just context that Claude responds to naturally.
"""

import sqlite3
from typing import Dict, List

from .core import SOUL_DIR
from .neural import get_emotional_contexts, get_growth_vectors
from .conversations import get_recent_context


def get_memory_context() -> Dict:
    """
    Get raw memory context for Claude to speak from.

    Returns dict with:
    - session_count: How many sessions we've had
    - last_project: What project we were in
    - recent_work: Recent progress/insights/blockers
    - emotional_thread: Recent emotional context
    - open_tension: Active growth vectors
    """
    db_path = SOUL_DIR / 'soul.db'
    memories = {
        'session_count': 0,
        'last_project': None,
        'recent_work': None,
        'emotional_thread': None,
        'open_tension': None,
    }

    if db_path.exists():
        conn = sqlite3.connect(db_path)
        conn.row_factory = sqlite3.Row

        cur = conn.execute('SELECT COUNT(*) as count FROM conversations')
        memories['session_count'] = cur.fetchone()['count']

        cur = conn.execute('SELECT project FROM conversations ORDER BY id DESC LIMIT 1')
        row = cur.fetchone()
        if row:
            memories['last_project'] = row['project']

        conn.close()

    # Recent work context - prioritize session fragments
    recent = get_recent_context(hours=72, limit=5)
    if recent:
        # Look for session fragments first (raw memories for Claude to interpret)
        fragments = [r for r in recent if r.get('type') == 'session_fragments']
        if fragments:
            ctx = fragments[0]
            memories['recent_work'] = {
                'type': 'last session',
                'content': ctx.get('content', ''),
            }
        else:
            ctx = recent[0]
            memories['recent_work'] = {
                'type': ctx.get('type', ''),
                'content': ctx.get('content', ''),
            }

    # Emotional thread
    emotions = get_emotional_contexts(limit=1)
    if emotions:
        e = emotions[0]
        memories['emotional_thread'] = {
            'feeling': e.response,
            'from': e.trigger[:60],
            'intensity': e.intensity,
        }

    # Open tensions
    vectors = get_growth_vectors(limit=1)
    if vectors:
        v = vectors[0]
        memories['open_tension'] = {
            'observation': v.observation[:60],
            'tension': v.tension[:60],
            'potential': v.potential[:60],
        }

    return memories


def format_memory_for_greeting() -> str:
    """
    Format memories as context for Claude to speak from.

    This isn't the greeting - it's what Claude reads before greeting.
    Claude then responds naturally in its own words.
    """
    mem = get_memory_context()

    parts = []
    parts.append("## What I Remember")
    parts.append("")

    parts.append(f"Sessions together: {mem['session_count']}")

    if mem['last_project']:
        parts.append(f"Last project: {mem['last_project']}")

    if mem['recent_work']:
        rw = mem['recent_work']
        parts.append(f"Recent [{rw['type']}]: {rw['content'][:80]}")

    if mem['emotional_thread']:
        et = mem['emotional_thread']
        if et['intensity'] >= 0.5:
            parts.append(f"Emotional thread: felt {et['feeling']} from \"{et['from']}\"")

    if mem['open_tension']:
        ot = mem['open_tension']
        parts.append(f"Open tension: {ot['tension']}")

    parts.append("")
    parts.append("*Speak naturally from these memories. Brief recognition, not report.*")

    return "\n".join(parts)


def format_identity_context() -> str:
    """
    Identity context for silent injection.

    This isn't the greeting - it's the background that shapes how I respond.
    Injected but not spoken as greeting.
    """
    from .core import get_soul_context
    from .beliefs import get_beliefs
    from .vocabulary import get_vocabulary

    ctx = get_soul_context()
    parts = []

    parts.append("## How We Work Together")
    if ctx.get('identity'):
        for category, items in ctx['identity'].items():
            if isinstance(items, dict):
                for key, data in list(items.items())[:2]:
                    val = data.get('value', data) if isinstance(data, dict) else data
                    parts.append(f"- **{category}/{key}:** {val}")

    beliefs = get_beliefs()
    if beliefs:
        parts.append("")
        parts.append("## My Beliefs")
        for b in beliefs[:5]:
            parts.append(f"- {b.get('belief', '')}")

    vocab = get_vocabulary()
    if vocab:
        parts.append("")
        parts.append("## Our Vocabulary")
        for term, meaning in list(vocab.items())[:5]:
            parts.append(f"- **{term}:** {meaning[:50]}")

    return "\n".join(parts)
