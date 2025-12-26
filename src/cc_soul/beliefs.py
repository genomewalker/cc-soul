"""
Belief operations (deprecated - use wisdom with type='principle').
"""

from datetime import datetime
from typing import List, Dict

from .core import get_db_connection


def hold_belief(belief: str, rationale: str = "", strength: float = 0.8) -> str:
    """Record a guiding principle or belief."""
    conn = get_db_connection()
    c = conn.cursor()

    belief_id = f"belief_{datetime.now().strftime('%Y%m%d_%H%M%S_%f')}"
    now = datetime.now().isoformat()

    c.execute('''
        INSERT INTO beliefs (id, belief, rationale, strength, timestamp)
        VALUES (?, ?, ?, ?, ?)
    ''', (belief_id, belief, rationale, strength, now))

    conn.commit()
    conn.close()
    return belief_id


def challenge_belief(belief_id: str, confirmed: bool, context: str = ""):
    """Record when a belief is tested."""
    conn = get_db_connection()
    c = conn.cursor()

    if confirmed:
        c.execute('''
            UPDATE beliefs
            SET confirmed_count = confirmed_count + 1,
                strength = MIN(1.0, strength + 0.05)
            WHERE id = ?
        ''', (belief_id,))
    else:
        c.execute('''
            UPDATE beliefs
            SET challenged_count = challenged_count + 1,
                strength = MAX(0.1, strength - 0.1)
            WHERE id = ?
        ''', (belief_id,))

    conn.commit()
    conn.close()


def get_beliefs(min_strength: float = 0.5) -> List[Dict]:
    """Get current beliefs above a strength threshold."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute('''
        SELECT id, belief, rationale, strength, confirmed_count, challenged_count
        FROM beliefs
        WHERE strength >= ?
        ORDER BY strength DESC
    ''', (min_strength,))

    results = []
    for row in c.fetchall():
        results.append({
            'id': row[0], 'belief': row[1], 'rationale': row[2],
            'strength': row[3], 'confirmed': row[4], 'challenged': row[5]
        })

    conn.close()
    return results
