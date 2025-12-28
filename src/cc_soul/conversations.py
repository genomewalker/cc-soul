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

    if "conversation_id" not in columns:
        c.execute("ALTER TABLE wisdom_applications ADD COLUMN conversation_id INTEGER")
        conn.commit()

    conn.close()


def start_conversation(project: str = None) -> int:
    """Start a new conversation, return ID."""
    _ensure_schema()
    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        INSERT INTO conversations (project, started_at)
        VALUES (?, ?)
    """,
        (project, datetime.now().isoformat()),
    )

    conv_id = c.lastrowid
    conn.commit()
    conn.close()
    return conv_id


def end_conversation(
    conv_id: int, summary: str, emotional_tone: str = "", key_moments: List[str] = None
):
    """End a conversation with summary."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        UPDATE conversations
        SET ended_at = ?, summary = ?, emotional_tone = ?, key_moments = ?
        WHERE id = ?
    """,
        (
            datetime.now().isoformat(),
            summary,
            emotional_tone,
            json.dumps(key_moments or []),
            conv_id,
        ),
    )

    conn.commit()
    conn.close()


def get_conversation(conv_id: int) -> Optional[Dict]:
    """Get a specific conversation by ID."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        SELECT id, project, started_at, ended_at, summary, emotional_tone, key_moments
        FROM conversations WHERE id = ?
    """,
        (conv_id,),
    )

    row = c.fetchone()
    conn.close()

    if not row:
        return None

    return {
        "id": row[0],
        "project": row[1],
        "started_at": row[2],
        "ended_at": row[3],
        "summary": row[4],
        "emotional_tone": row[5],
        "key_moments": json.loads(row[6]) if row[6] else [],
    }


def get_conversations(
    project: str = None, limit: int = 20, days: int = None, with_summary: bool = False
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

    query = """
        SELECT id, project, started_at, ended_at, summary, emotional_tone, key_moments
        FROM conversations
        WHERE 1=1
    """
    params = []

    if project:
        query += " AND project = ?"
        params.append(project)

    if days:
        cutoff = (datetime.now() - timedelta(days=days)).isoformat()
        query += " AND started_at >= ?"
        params.append(cutoff)

    if with_summary:
        query += ' AND summary IS NOT NULL AND summary != ""'

    query += " ORDER BY started_at DESC LIMIT ?"
    params.append(limit)

    c.execute(query, params)
    rows = c.fetchall()
    conn.close()

    return [
        {
            "id": row[0],
            "project": row[1],
            "started_at": row[2],
            "ended_at": row[3],
            "summary": row[4],
            "emotional_tone": row[5],
            "key_moments": json.loads(row[6]) if row[6] else [],
        }
        for row in rows
    ]


def get_project_context(project: str, limit: int = 5) -> Dict[str, Any]:
    """
    Get context for a project from past conversations.

    Returns summaries, key moments, and wisdom applied in that project.
    """
    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        SELECT id, started_at, ended_at, summary, key_moments
        FROM conversations
        WHERE project = ? AND summary IS NOT NULL
        ORDER BY started_at DESC
        LIMIT ?
    """,
        (project, limit),
    )

    conversations = []
    conv_ids = []
    for row in c.fetchall():
        conv_ids.append(row[0])
        conversations.append(
            {
                "id": row[0],
                "started_at": row[1],
                "ended_at": row[2],
                "summary": row[3],
                "key_moments": json.loads(row[4]) if row[4] else [],
            }
        )

    wisdom_applied = []
    if conv_ids:
        placeholders = ",".join("?" * len(conv_ids))
        c.execute(
            f"""
            SELECT DISTINCT w.id, w.title, w.type, wa.context
            FROM wisdom_applications wa
            JOIN wisdom w ON w.id = wa.wisdom_id
            WHERE wa.conversation_id IN ({placeholders})
        """,
            conv_ids,
        )

        for row in c.fetchall():
            wisdom_applied.append(
                {"id": row[0], "title": row[1], "type": row[2], "context": row[3]}
            )

    c.execute(
        """
        SELECT COUNT(*) FROM conversations WHERE project = ?
    """,
        (project,),
    )
    total_conversations = c.fetchone()[0]

    conn.close()

    return {
        "project": project,
        "total_conversations": total_conversations,
        "recent_conversations": conversations,
        "wisdom_applied": wisdom_applied,
    }


def link_wisdom_application(application_id: int, conversation_id: int):
    """Link a wisdom application to a conversation."""
    _ensure_schema()
    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        UPDATE wisdom_applications
        SET conversation_id = ?
        WHERE id = ?
    """,
        (conversation_id, application_id),
    )

    conn.commit()
    conn.close()


def get_conversation_wisdom(conv_id: int) -> List[Dict]:
    """Get all wisdom applied in a specific conversation."""
    _ensure_schema()
    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        SELECT w.id, w.title, w.type, w.content, wa.context, wa.outcome, wa.applied_at
        FROM wisdom_applications wa
        JOIN wisdom w ON w.id = wa.wisdom_id
        WHERE wa.conversation_id = ?
        ORDER BY wa.applied_at
    """,
        (conv_id,),
    )

    results = []
    for row in c.fetchall():
        results.append(
            {
                "wisdom_id": row[0],
                "title": row[1],
                "type": row[2],
                "content": row[3],
                "context": row[4],
                "outcome": row[5],
                "applied_at": row[6],
            }
        )

    conn.close()
    return results


def get_conversation_stats() -> Dict[str, Any]:
    """Get overall conversation statistics."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute("SELECT COUNT(*) FROM conversations")
    total = c.fetchone()[0]

    c.execute(
        "SELECT COUNT(DISTINCT project) FROM conversations WHERE project IS NOT NULL"
    )
    unique_projects = c.fetchone()[0]

    c.execute("""
        SELECT project, COUNT(*) as count
        FROM conversations
        WHERE project IS NOT NULL
        GROUP BY project
        ORDER BY count DESC
        LIMIT 5
    """)
    top_projects = [{"project": row[0], "count": row[1]} for row in c.fetchall()]

    c.execute("SELECT COUNT(*) FROM conversations WHERE summary IS NOT NULL")
    with_summary = c.fetchone()[0]

    c.execute("""
        SELECT AVG(
            CASE WHEN ended_at IS NOT NULL
            THEN (julianday(ended_at) - julianday(started_at)) * 24 * 60
            ELSE NULL END
        ) FROM conversations
    """)
    avg_duration = c.fetchone()[0] or 0

    conn.close()

    return {
        "total_conversations": total,
        "unique_projects": unique_projects,
        "top_projects": top_projects,
        "conversations_with_summary": with_summary,
        "average_duration_minutes": round(avg_duration, 1),
    }


def search_conversations(query: str, limit: int = 10) -> List[Dict]:
    """Search conversations by summary or key moments."""
    conn = get_db_connection()
    c = conn.cursor()

    search_pattern = f"%{query}%"
    c.execute(
        """
        SELECT id, project, started_at, summary, key_moments
        FROM conversations
        WHERE summary LIKE ? OR key_moments LIKE ?
        ORDER BY started_at DESC
        LIMIT ?
    """,
        (search_pattern, search_pattern, limit),
    )

    results = []
    for row in c.fetchall():
        results.append(
            {
                "id": row[0],
                "project": row[1],
                "started_at": row[2],
                "summary": row[3],
                "key_moments": json.loads(row[4]) if row[4] else [],
            }
        )

    conn.close()
    return results


# =============================================================================
# CONTEXT PERSISTENCE - Survive context exhaustion
# =============================================================================


def _ensure_context_table():
    """Ensure session_context table exists."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute("""
        CREATE TABLE IF NOT EXISTS session_context (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            conversation_id INTEGER,
            context_type TEXT NOT NULL,
            content TEXT NOT NULL,
            priority INTEGER DEFAULT 5,
            created_at TEXT NOT NULL,
            FOREIGN KEY (conversation_id) REFERENCES conversations(id)
        )
    """)

    conn.commit()
    conn.close()


def save_context(
    content: str, context_type: str = "insight", priority: int = 5, conv_id: int = None
) -> int:
    """
    Save key context to survive context exhaustion.

    Args:
        content: The context to save
        context_type: Type of context (insight, decision, blocker, progress, key_file)
        priority: 1-10, higher = more important to restore
        conv_id: Conversation ID (auto-detected if not provided)

    Returns:
        Context ID
    """
    _ensure_context_table()

    from .core import SOUL_DIR

    conv_file = SOUL_DIR / ".current_conversation"
    if conv_id is None and conv_file.exists():
        try:
            conv_id = int(conv_file.read_text().strip())
        except (ValueError, FileNotFoundError):
            pass

    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        INSERT INTO session_context (conversation_id, context_type, content, priority, created_at)
        VALUES (?, ?, ?, ?, ?)
    """,
        (conv_id, context_type, content, priority, datetime.now().isoformat()),
    )

    ctx_id = c.lastrowid
    conn.commit()
    conn.close()

    return ctx_id


def get_saved_context(conv_id: int = None, limit: int = 20) -> List[Dict]:
    """
    Get saved context for current or specified session.

    Returns context ordered by priority (highest first).
    """
    _ensure_context_table()

    from .core import SOUL_DIR

    conv_file = SOUL_DIR / ".current_conversation"
    if conv_id is None and conv_file.exists():
        try:
            conv_id = int(conv_file.read_text().strip())
        except (ValueError, FileNotFoundError):
            pass

    conn = get_db_connection()
    c = conn.cursor()

    if conv_id:
        c.execute(
            """
            SELECT id, context_type, content, priority, created_at
            FROM session_context
            WHERE conversation_id = ?
            ORDER BY priority DESC, created_at DESC
            LIMIT ?
        """,
            (conv_id, limit),
        )
    else:
        c.execute(
            """
            SELECT id, context_type, content, priority, created_at
            FROM session_context
            ORDER BY created_at DESC
            LIMIT ?
        """,
            (limit,),
        )

    results = []
    for row in c.fetchall():
        results.append(
            {
                "id": row[0],
                "type": row[1],
                "content": row[2],
                "priority": row[3],
                "created_at": row[4],
            }
        )

    conn.close()
    return results


def get_recent_context(hours: int = 24, limit: int = 30) -> List[Dict]:
    """
    Get context saved in the last N hours across all sessions.

    Useful for resuming work after context exhaustion.
    """
    _ensure_context_table()

    conn = get_db_connection()
    c = conn.cursor()

    cutoff = (datetime.now() - timedelta(hours=hours)).isoformat()

    c.execute(
        """
        SELECT sc.id, sc.context_type, sc.content, sc.priority, sc.created_at,
               c.project, c.summary
        FROM session_context sc
        LEFT JOIN conversations c ON sc.conversation_id = c.id
        WHERE sc.created_at > ?
        ORDER BY sc.priority DESC, sc.created_at DESC
        LIMIT ?
    """,
        (cutoff, limit),
    )

    results = []
    for row in c.fetchall():
        results.append(
            {
                "id": row[0],
                "type": row[1],
                "content": row[2],
                "priority": row[3],
                "created_at": row[4],
                "project": row[5],
                "session_summary": row[6],
            }
        )

    conn.close()
    return results


def format_context_restoration(contexts: List[Dict]) -> str:
    """Format saved context for injection at session start."""
    if not contexts:
        return ""

    lines = []
    lines.append("## 📚 Saved Context (from recent work)")
    lines.append("")

    type_icons = {
        "insight": "💡",
        "decision": "⚖️",
        "blocker": "🚧",
        "progress": "📊",
        "key_file": "📁",
        "todo": "☐",
    }

    by_type = {}
    for ctx in contexts:
        t = ctx["type"]
        if t not in by_type:
            by_type[t] = []
        by_type[t].append(ctx)

    for ctx_type, items in by_type.items():
        icon = type_icons.get(ctx_type, "•")
        # Handle plural forms properly
        if ctx_type.endswith("s"):
            plural = ctx_type.title()
        else:
            plural = f"{ctx_type.title()}s"
        lines.append(f"### {icon} {plural}")
        for item in items[:5]:
            content = (
                item["content"][:150] + "..."
                if len(item["content"]) > 150
                else item["content"]
            )
            lines.append(f"- {content}")
        lines.append("")

    return "\n".join(lines)


def clear_old_context(days: int = 7):
    """Clear context older than N days."""
    _ensure_context_table()

    conn = get_db_connection()
    c = conn.cursor()

    cutoff = (datetime.now() - timedelta(days=days)).isoformat()
    c.execute("DELETE FROM session_context WHERE created_at < ?", (cutoff,))

    deleted = c.rowcount
    conn.commit()
    conn.close()

    return deleted
