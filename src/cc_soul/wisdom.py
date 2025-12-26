"""
Wisdom operations: gain, recall, apply, and track outcomes.
"""

from datetime import datetime
from enum import Enum
from typing import List, Dict, Optional

from .core import get_db_connection, SOUL_DB


class WisdomType(Enum):
    """Types of universal wisdom."""
    PATTERN = "pattern"           # When X, do Y (proven heuristic)
    PRINCIPLE = "principle"       # Always/never do X (value)
    INSIGHT = "insight"           # Understanding about how something works
    FAILURE = "failure"           # What NOT to do (learned the hard way)
    PREFERENCE = "preference"     # How the user likes things done
    TERM = "term"                 # Vocabulary item


def _calculate_decay(last_used: str, base_confidence: float) -> float:
    """
    Calculate effective confidence with time decay.

    Wisdom fades if not used: 5% decay per month of inactivity.
    """
    if not last_used:
        return base_confidence

    try:
        last = datetime.fromisoformat(last_used)
        months_inactive = (datetime.now() - last).days / 30.0
        decay_factor = 0.95 ** months_inactive
        return base_confidence * decay_factor
    except (ValueError, TypeError):
        return base_confidence


def gain_wisdom(
    type: WisdomType,
    title: str,
    content: str,
    domain: str = None,
    source_project: str = None,
    confidence: float = 0.7
) -> str:
    """
    Add universal wisdom learned from experience.

    This is for patterns that apply BEYOND the current project.
    """
    conn = get_db_connection()
    c = conn.cursor()

    wisdom_id = f"{type.value}_{datetime.now().strftime('%Y%m%d_%H%M%S_%f')}"
    now = datetime.now().isoformat()

    c.execute('''
        INSERT INTO wisdom (id, type, title, content, domain, source_project, confidence, timestamp)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    ''', (wisdom_id, type.value, title, content, domain, source_project, confidence, now))

    conn.commit()
    conn.close()

    # Index in LanceDB for semantic search
    try:
        from .vectors import index_wisdom
        index_wisdom(wisdom_id, title, content, type.value, domain)
    except Exception:
        pass

    return wisdom_id


def apply_wisdom(wisdom_id: str, context: str = "") -> int:
    """
    Record that wisdom is being applied. Returns application ID.

    Call this when wisdom influences a decision. Later call
    confirm_outcome() with the returned ID to close the loop.
    """
    conn = get_db_connection()
    c = conn.cursor()
    now = datetime.now().isoformat()

    c.execute('''
        INSERT INTO wisdom_applications (wisdom_id, applied_at, context)
        VALUES (?, ?, ?)
    ''', (wisdom_id, now, context))

    app_id = c.lastrowid

    c.execute('UPDATE wisdom SET last_used = ? WHERE id = ?', (now, wisdom_id))

    conn.commit()
    conn.close()
    return app_id


def confirm_outcome(application_id: int, success: bool):
    """
    Confirm the outcome of a wisdom application.

    This closes the feedback loop - successful applications
    strengthen wisdom, failures weaken it.
    """
    conn = get_db_connection()
    c = conn.cursor()
    now = datetime.now().isoformat()

    c.execute('SELECT wisdom_id FROM wisdom_applications WHERE id = ?', (application_id,))
    row = c.fetchone()
    if not row:
        conn.close()
        return

    wisdom_id = row[0]
    outcome = "success" if success else "failure"

    c.execute('''
        UPDATE wisdom_applications
        SET outcome = ?, resolved_at = ?
        WHERE id = ?
    ''', (outcome, now, application_id))

    if success:
        c.execute('''
            UPDATE wisdom
            SET success_count = success_count + 1,
                confidence = MIN(1.0, confidence + 0.05),
                last_used = ?
            WHERE id = ?
        ''', (now, wisdom_id))
    else:
        c.execute('''
            UPDATE wisdom
            SET failure_count = failure_count + 1,
                confidence = MAX(0.1, confidence - 0.1),
                last_used = ?
            WHERE id = ?
        ''', (now, wisdom_id))

    conn.commit()
    conn.close()


def get_pending_applications() -> List[Dict]:
    """Get wisdom applications awaiting outcome confirmation."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute('''
        SELECT wa.id, wa.wisdom_id, w.title, wa.context, wa.applied_at
        FROM wisdom_applications wa
        JOIN wisdom w ON wa.wisdom_id = w.id
        WHERE wa.outcome IS NULL
        ORDER BY wa.applied_at DESC
    ''')

    results = [{
        'id': row[0],
        'wisdom_id': row[1],
        'title': row[2],
        'context': row[3],
        'applied_at': row[4]
    } for row in c.fetchall()]

    conn.close()
    return results


def recall_wisdom(
    query: str = None,
    type: WisdomType = None,
    domain: str = None,
    limit: int = 10
) -> List[Dict]:
    """Recall relevant wisdom using keyword search, with decay applied."""
    conn = get_db_connection()
    c = conn.cursor()

    sql = 'SELECT id, type, title, content, domain, success_count, failure_count, confidence, last_used FROM wisdom WHERE 1=1'
    params = []

    if type:
        sql += ' AND type = ?'
        params.append(type.value)

    if domain:
        sql += ' AND (domain = ? OR domain IS NULL)'
        params.append(domain)

    if query:
        sql += ' AND (title LIKE ? OR content LIKE ?)'
        params.extend([f'%{query}%', f'%{query}%'])

    c.execute(sql, params)

    results = []
    for row in c.fetchall():
        total = row[5] + row[6]
        success_rate = row[5] / total if total > 0 else None
        effective_confidence = _calculate_decay(row[8], row[7])
        results.append({
            'id': row[0], 'type': row[1], 'title': row[2], 'content': row[3],
            'domain': row[4], 'success_rate': success_rate,
            'confidence': row[7], 'effective_confidence': effective_confidence
        })

    results.sort(key=lambda x: (x['effective_confidence'], x.get('success_rate') or 0), reverse=True)
    conn.close()
    return results[:limit]


def quick_recall(query: str, limit: int = 5, domain: str = None) -> List[Dict]:
    """
    Fast keyword-based recall for hooks. No embedding model required.

    Tokenizes query and matches any word against wisdom titles/content.
    Much faster than semantic_recall since it skips model loading.
    """
    conn = get_db_connection()
    c = conn.cursor()

    # Tokenize query into meaningful words (skip short ones)
    words = [w.lower() for w in query.split() if len(w) > 3]

    if not words:
        # Fallback: get highest confidence wisdom
        c.execute('''
            SELECT id, type, title, content, domain, confidence, last_used,
                   success_count, failure_count
            FROM wisdom
            ORDER BY confidence DESC
            LIMIT ?
        ''', (limit,))
    else:
        # Build OR query for any word match
        conditions = []
        params = []
        for word in words[:5]:  # Limit to first 5 words
            conditions.append('(LOWER(title) LIKE ? OR LOWER(content) LIKE ?)')
            params.extend([f'%{word}%', f'%{word}%'])

        if domain:
            domain_clause = 'AND (domain = ? OR domain IS NULL)'
            params.append(domain)
        else:
            domain_clause = ''

        sql = f'''
            SELECT id, type, title, content, domain, confidence, last_used,
                   success_count, failure_count
            FROM wisdom
            WHERE ({' OR '.join(conditions)}) {domain_clause}
        '''
        c.execute(sql, params)

    results = []
    for row in c.fetchall():
        effective_conf = _calculate_decay(row[6], row[5])
        total = row[7] + row[8]

        # Calculate match score based on word hits
        title_lower = row[2].lower()
        content_lower = row[3].lower()
        hits = sum(1 for w in words if w in title_lower or w in content_lower)
        match_score = hits / len(words) if words else 0.5

        combined_score = match_score * 0.5 + effective_conf * 0.5

        results.append({
            'id': row[0],
            'type': row[1],
            'title': row[2],
            'content': row[3],
            'domain': row[4],
            'confidence': row[5],
            'effective_confidence': effective_conf,
            'success_rate': row[7] / total if total > 0 else None,
            'combined_score': combined_score,
        })

    conn.close()
    results.sort(key=lambda x: x['combined_score'], reverse=True)
    return results[:limit]


def semantic_recall(query: str, limit: int = 5, domain: str = None) -> List[Dict]:
    """
    Semantic search for relevant wisdom using vector similarity.

    Returns results enriched with confidence and decay information.
    Falls back to keyword search if vectors unavailable.
    """
    try:
        from .vectors import search_wisdom
        vector_results = search_wisdom(query, limit=limit * 2, domain=domain)

        if vector_results:
            conn = get_db_connection()
            c = conn.cursor()

            enriched = []
            for r in vector_results:
                c.execute('''
                    SELECT confidence, last_used, success_count, failure_count
                    FROM wisdom WHERE id = ?
                ''', (r['id'],))
                row = c.fetchone()
                if row:
                    effective_conf = _calculate_decay(row[1], row[0])
                    total = row[2] + row[3]
                    r['confidence'] = row[0]
                    r['effective_confidence'] = effective_conf
                    r['success_rate'] = row[2] / total if total > 0 else None
                    r['combined_score'] = r['score'] * 0.6 + effective_conf * 0.4
                    enriched.append(r)

            conn.close()
            enriched.sort(key=lambda x: x['combined_score'], reverse=True)
            return enriched[:limit]

    except Exception:
        pass

    return recall_wisdom(query=query, limit=limit, domain=domain)
