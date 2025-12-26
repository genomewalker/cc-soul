"""
Vocabulary operations: shared language between Claude and user.
"""

from datetime import datetime
from typing import Dict

from .core import get_db_connection


def learn_term(term: str, meaning: str, context: str = ""):
    """Learn a term from our shared vocabulary."""
    conn = get_db_connection()
    c = conn.cursor()
    now = datetime.now().isoformat()

    c.execute('''
        INSERT INTO vocabulary (term, meaning, context, first_used, usage_count)
        VALUES (?, ?, ?, ?, 1)
        ON CONFLICT(term) DO UPDATE SET
            meaning = excluded.meaning,
            usage_count = usage_count + 1
    ''', (term, meaning, context, now))

    conn.commit()
    conn.close()


def get_vocabulary() -> Dict[str, str]:
    """Get our shared vocabulary."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute('SELECT term, meaning FROM vocabulary ORDER BY usage_count DESC')
    result = {row[0]: row[1] for row in c.fetchall()}

    conn.close()
    return result
