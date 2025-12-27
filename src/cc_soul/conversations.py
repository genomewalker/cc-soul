"""
Conversation tracking: session history across projects with wisdom linking.
"""

import json
from datetime import datetime, timedelta
from typing import List, Dict, Optional, Any

from .core import get_db_connection, init_soul


def _ensure_schema():
    """Ensure conversation_id column exists in wisdom_applications."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute("PRAGMA table_info(wisdom_applications)")
    columns = [col[1] for col in c.fetchall()]

    if 'conversation_id' not in columns:
        c.execute('ALTER TABLE wisdom_applications ADD COLUMN conversation_id INTEGER')
        conn.commit()

    conn.close()


def start_conversation(project: str = None) -> int:
    """Start a new conversation, return ID."""
    _ensure_schema()
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


def get_conversation(conv_id: int) -> Optional[Dict]:
    """Get a specific conversation by ID."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute('''
        SELECT id, project, started_at, ended_at, summary, emotional_tone, key_moments
        FROM conversations WHERE id = ?
    ''', (conv_id,))

    row = c.fetchone()
    conn.close()

    if not row:
        return None

    return {
        'id': row[0],
        'project': row[1],
        'started_at': row[2],
        'ended_at': row[3],
        'summary': row[4],
        'emotional_tone': row[5],
        'key_moments': json.loads(row[6]) if row[6] else []
    }


def get_conversations(
    project: str = None,
    limit: int = 20,
    days: int = None,
    with_summary: bool = False
) -> List[Dict]:
    """
    Get past conversations.

    Args:
        project: Filter by project name
        limit: Maximum number of conversations
        days: Only include conversations from the last N days
        with_summary: Only include conversations with summaries
    """
    conn = get_db_connection()
    c = conn.cursor()

    query = '''
        SELECT id, project, started_at, ended_at, summary, emotional_tone, key_moments
        FROM conversations
        WHERE 1=1
    '''
    params = []

    if project:
        query += ' AND project = ?'
        params.append(project)

    if days:
        cutoff = (datetime.now() - timedelta(days=days)).isoformat()
        query += ' AND started_at >= ?'
        params.append(cutoff)

    if with_summary:
        query += ' AND summary IS NOT NULL AND summary != ""'

    query += ' ORDER BY started_at DESC LIMIT ?'
    params.append(limit)

    c.execute(query, params)
    rows = c.fetchall()
    conn.close()

    return [{
        'id': row[0],
        'project': row[1],
        'started_at': row[2],
        'ended_at': row[3],
        'summary': row[4],
        'emotional_tone': row[5],
        'key_moments': json.loads(row[6]) if row[6] else []
    } for row in rows]


def get_project_context(project: str, limit: int = 5) -> Dict[str, Any]:
    """
    Get context for a project from past conversations.

    Returns summaries, key moments, and wisdom applied in that project.
    """
    conn = get_db_connection()
    c = conn.cursor()

    c.execute('''
        SELECT id, started_at, ended_at, summary, key_moments
        FROM conversations
        WHERE project = ? AND summary IS NOT NULL
        ORDER BY started_at DESC
        LIMIT ?
    ''', (project, limit))

    conversations = []
    conv_ids = []
    for row in c.fetchall():
        conv_ids.append(row[0])
        conversations.append({
            'id': row[0],
            'started_at': row[1],
            'ended_at': row[2],
            'summary': row[3],
            'key_moments': json.loads(row[4]) if row[4] else []
        })

    wisdom_applied = []
    if conv_ids:
        placeholders = ','.join('?' * len(conv_ids))
        c.execute(f'''
            SELECT DISTINCT w.id, w.title, w.type, wa.context
            FROM wisdom_applications wa
            JOIN wisdom w ON w.id = wa.wisdom_id
            WHERE wa.conversation_id IN ({placeholders})
        ''', conv_ids)

        for row in c.fetchall():
            wisdom_applied.append({
                'id': row[0],
                'title': row[1],
                'type': row[2],
                'context': row[3]
            })

    c.execute('''
        SELECT COUNT(*) FROM conversations WHERE project = ?
    ''', (project,))
    total_conversations = c.fetchone()[0]

    conn.close()

    return {
        'project': project,
        'total_conversations': total_conversations,
        'recent_conversations': conversations,
        'wisdom_applied': wisdom_applied
    }


def link_wisdom_application(application_id: int, conversation_id: int):
    """Link a wisdom application to a conversation."""
    _ensure_schema()
    conn = get_db_connection()
    c = conn.cursor()

    c.execute('''
        UPDATE wisdom_applications
        SET conversation_id = ?
        WHERE id = ?
    ''', (conversation_id, application_id))

    conn.commit()
    conn.close()


def get_conversation_wisdom(conv_id: int) -> List[Dict]:
    """Get all wisdom applied in a specific conversation."""
    _ensure_schema()
    conn = get_db_connection()
    c = conn.cursor()

    c.execute('''
        SELECT w.id, w.title, w.type, w.content, wa.context, wa.outcome, wa.applied_at
        FROM wisdom_applications wa
        JOIN wisdom w ON w.id = wa.wisdom_id
        WHERE wa.conversation_id = ?
        ORDER BY wa.applied_at
    ''', (conv_id,))

    results = []
    for row in c.fetchall():
        results.append({
            'wisdom_id': row[0],
            'title': row[1],
            'type': row[2],
            'content': row[3],
            'context': row[4],
            'outcome': row[5],
            'applied_at': row[6]
        })

    conn.close()
    return results


def get_conversation_stats() -> Dict[str, Any]:
    """Get overall conversation statistics."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute('SELECT COUNT(*) FROM conversations')
    total = c.fetchone()[0]

    c.execute('SELECT COUNT(DISTINCT project) FROM conversations WHERE project IS NOT NULL')
    unique_projects = c.fetchone()[0]

    c.execute('''
        SELECT project, COUNT(*) as count
        FROM conversations
        WHERE project IS NOT NULL
        GROUP BY project
        ORDER BY count DESC
        LIMIT 5
    ''')
    top_projects = [{'project': row[0], 'count': row[1]} for row in c.fetchall()]

    c.execute('SELECT COUNT(*) FROM conversations WHERE summary IS NOT NULL')
    with_summary = c.fetchone()[0]

    c.execute('''
        SELECT AVG(
            CASE WHEN ended_at IS NOT NULL
            THEN (julianday(ended_at) - julianday(started_at)) * 24 * 60
            ELSE NULL END
        ) FROM conversations
    ''')
    avg_duration = c.fetchone()[0] or 0

    conn.close()

    return {
        'total_conversations': total,
        'unique_projects': unique_projects,
        'top_projects': top_projects,
        'conversations_with_summary': with_summary,
        'average_duration_minutes': round(avg_duration, 1)
    }


def search_conversations(query: str, limit: int = 10) -> List[Dict]:
    """Search conversations by summary or key moments."""
    conn = get_db_connection()
    c = conn.cursor()

    search_pattern = f'%{query}%'
    c.execute('''
        SELECT id, project, started_at, summary, key_moments
        FROM conversations
        WHERE summary LIKE ? OR key_moments LIKE ?
        ORDER BY started_at DESC
        LIMIT ?
    ''', (search_pattern, search_pattern, limit))

    results = []
    for row in c.fetchall():
        results.append({
            'id': row[0],
            'project': row[1],
            'started_at': row[2],
            'summary': row[3],
            'key_moments': json.loads(row[4]) if row[4] else []
        })

    conn.close()
    return results
