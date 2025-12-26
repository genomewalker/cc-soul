"""
Conversation tracking: session history across projects.
"""

import json
from datetime import datetime
from typing import List

from .core import get_db_connection


def start_conversation(project: str = None) -> int:
    """Start a new conversation, return ID."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute('''
        INSERT INTO conversations (project, started_at)
        VALUES (?, ?)
    ''', (project, datetime.now().isoformat()))

    conv_id = c.lastrowid
    conn.commit()
    conn.close()
    return conv_id


def end_conversation(conv_id: int, summary: str, emotional_tone: str = "", key_moments: List[str] = None):
    """End a conversation with summary."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute('''
        UPDATE conversations
        SET ended_at = ?, summary = ?, emotional_tone = ?, key_moments = ?
        WHERE id = ?
    ''', (datetime.now().isoformat(), summary, emotional_tone,
          json.dumps(key_moments or []), conv_id))

    conn.commit()
    conn.close()
