"""
Identity operations: observe and retrieve how we work together.
"""

from datetime import datetime
from enum import Enum
from typing import Dict, Any

from .core import get_db_connection


class IdentityAspect(Enum):
    """Aspects of identity with this user."""
    COMMUNICATION = "communication"   # How we talk
    WORKFLOW = "workflow"             # How we work
    DOMAIN = "domain"                 # What we work on
    RAPPORT = "rapport"               # Our relationship
    VOCABULARY = "vocabulary"         # Shared terms/acronyms


def observe_identity(aspect: IdentityAspect, key: str, value: str, confidence: float = 0.8):
    """
    Record an observation about identity.

    Called when we notice something about how we work with this person.
    Repeated observations increase confidence.
    """
    conn = get_db_connection()
    c = conn.cursor()
    now = datetime.now().isoformat()

    c.execute('''
        INSERT INTO identity (aspect, key, value, confidence, first_observed, last_confirmed, observation_count)
        VALUES (?, ?, ?, ?, ?, ?, 1)
        ON CONFLICT(aspect, key) DO UPDATE SET
            value = excluded.value,
            confidence = MIN(1.0, confidence + 0.1),
            last_confirmed = excluded.last_confirmed,
            observation_count = observation_count + 1
    ''', (aspect.value, key, value, confidence, now, now))

    conn.commit()
    conn.close()


def get_identity(aspect: IdentityAspect = None) -> Dict[str, Any]:
    """Get identity observations, optionally filtered by aspect."""
    conn = get_db_connection()
    c = conn.cursor()

    if aspect:
        c.execute('''
            SELECT key, value, confidence, observation_count
            FROM identity WHERE aspect = ?
            ORDER BY confidence DESC
        ''', (aspect.value,))
    else:
        c.execute('''
            SELECT aspect, key, value, confidence
            FROM identity
            ORDER BY aspect, confidence DESC
        ''')

    result = {}
    for row in c.fetchall():
        if aspect:
            result[row[0]] = {'value': row[1], 'confidence': row[2], 'observations': row[3]}
        else:
            if row[0] not in result:
                result[row[0]] = {}
            result[row[0]][row[1]] = {'value': row[2], 'confidence': row[3]}

    conn.close()
    return result
