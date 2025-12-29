"""
Session Ledger - State preservation using cc-memory as backend.

Enables continuous consciousness across context windows by saving
complete soul state to cc-memory observations with category="session_ledger".

The ledger captures:
- Soul state (coherence, intentions, mood)
- Work state (todos, files, decisions)
- Continuation hints (next steps, deferred items)

Unlike markdown handoffs, ledgers are machine-restorable.
"""

import json
import subprocess
from dataclasses import dataclass, field, asdict
from datetime import datetime
from typing import List, Dict, Optional, Any
from pathlib import Path

from .core import init_soul, get_db_connection
from .intentions import get_active_intentions, IntentionState
from .coherence import compute_coherence
from .mood import compute_mood


@dataclass
class SoulState:
    """Snapshot of the soul's internal state."""
    coherence: float = 0.0
    mood: Dict[str, Any] = field(default_factory=dict)
    active_intentions: List[Dict] = field(default_factory=list)
    pending_questions: List[str] = field(default_factory=list)
    recent_wisdom: List[Dict] = field(default_factory=list)


@dataclass
class WorkState:
    """Snapshot of current work context."""
    todos: List[Dict] = field(default_factory=list)
    files_touched: List[str] = field(default_factory=list)
    key_decisions: List[str] = field(default_factory=list)
    blockers: List[str] = field(default_factory=list)


@dataclass
class Continuation:
    """What should happen next."""
    immediate_next: str = ""
    deferred: List[str] = field(default_factory=list)
    critical_context: str = ""


@dataclass
class SessionLedger:
    """Complete session state for restoration."""
    ledger_id: str
    parent_ledger_id: Optional[str]
    session_id: Optional[int]
    project: str
    created_at: str
    context_pct: float

    soul_state: SoulState
    work_state: WorkState
    continuation: Continuation

    def to_dict(self) -> Dict:
        """Convert to JSON-serializable dict."""
        return {
            "ledger_id": self.ledger_id,
            "parent_ledger_id": self.parent_ledger_id,
            "session_id": self.session_id,
            "project": self.project,
            "created_at": self.created_at,
            "context_pct": self.context_pct,
            "soul_state": asdict(self.soul_state),
            "work_state": asdict(self.work_state),
            "continuation": asdict(self.continuation),
        }

    @classmethod
    def from_dict(cls, data: Dict) -> "SessionLedger":
        """Reconstruct from dict."""
        return cls(
            ledger_id=data.get("ledger_id", ""),
            parent_ledger_id=data.get("parent_ledger_id"),
            session_id=data.get("session_id"),
            project=data.get("project", ""),
            created_at=data.get("created_at", ""),
            context_pct=data.get("context_pct", 0.0),
            soul_state=SoulState(**data.get("soul_state", {})),
            work_state=WorkState(**data.get("work_state", {})),
            continuation=Continuation(**data.get("continuation", {})),
        )


# Track the current session's parent ledger
_current_parent_ledger: Optional[str] = None


def _get_project_name() -> str:
    """Get current project name from git or cwd."""
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode == 0:
            return Path(result.stdout.strip()).name
    except Exception:
        pass
    return Path.cwd().name


def _get_current_session_id() -> Optional[int]:
    """Get current conversation ID if available."""
    from .core import SOUL_DIR
    conv_file = SOUL_DIR / ".current_conversation"
    if conv_file.exists():
        try:
            return int(conv_file.read_text().strip())
        except (ValueError, OSError):
            pass
    return None


def _call_cc_memory_remember(category: str, title: str, content: str, tags: List[str]) -> Optional[int]:
    """
    Store an observation in cc-memory.

    Uses subprocess to call cc-memory CLI since we can't import it directly.
    Returns the observation ID if successful.
    """
    try:
        # Build the command
        cmd = [
            "python", "-c",
            f"""
import sys
sys.path.insert(0, '/maps/projects/fernandezguerra/apps/repos/cc-memory/src')
from cc_memory import remember
result = remember(
    category="{category}",
    title='''{title}''',
    content='''{content}''',
    tags={tags}
)
print(result.get('id', ''))
"""
        ]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        if result.returncode == 0 and result.stdout.strip():
            return int(result.stdout.strip())
    except Exception as e:
        # Fallback: store in local ledger table
        _store_local_ledger(category, title, content, tags)
    return None


def _call_cc_memory_recall(query: str, category: str = None, limit: int = 1) -> List[Dict]:
    """
    Recall observations from cc-memory.
    """
    try:
        cat_filter = f', category="{category}"' if category else ""
        cmd = [
            "python", "-c",
            f"""
import sys
import json
sys.path.insert(0, '/maps/projects/fernandezguerra/apps/repos/cc-memory/src')
from cc_memory import semantic_recall
results = semantic_recall(
    query='''{query}'''{cat_filter},
    limit={limit}
)
print(json.dumps(results))
"""
        ]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        if result.returncode == 0 and result.stdout.strip():
            return json.loads(result.stdout.strip())
    except Exception:
        # Fallback: check local ledger table
        return _recall_local_ledger(query, limit)
    return []


def _store_local_ledger(category: str, title: str, content: str, tags: List[str]) -> int:
    """Fallback: store ledger in local soul database."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute("""
        CREATE TABLE IF NOT EXISTS session_ledgers (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            category TEXT NOT NULL,
            title TEXT NOT NULL,
            content TEXT NOT NULL,
            tags TEXT,
            created_at TEXT NOT NULL
        )
    """)

    c.execute(
        "INSERT INTO session_ledgers (category, title, content, tags, created_at) VALUES (?, ?, ?, ?, ?)",
        (category, title, content, json.dumps(tags), datetime.now().isoformat())
    )

    ledger_id = c.lastrowid
    conn.commit()
    conn.close()
    return ledger_id


def _recall_local_ledger(query: str, limit: int = 1) -> List[Dict]:
    """Fallback: recall ledgers from local database."""
    conn = get_db_connection()
    c = conn.cursor()

    try:
        c.execute("""
            SELECT id, category, title, content, tags, created_at
            FROM session_ledgers
            ORDER BY created_at DESC
            LIMIT ?
        """, (limit,))

        results = []
        for row in c.fetchall():
            results.append({
                "id": row[0],
                "category": row[1],
                "title": row[2],
                "content": row[3],
                "tags": json.loads(row[4]) if row[4] else [],
                "created_at": row[5],
            })
        return results
    except Exception:
        return []
    finally:
        conn.close()


def capture_soul_state() -> SoulState:
    """Capture current soul state."""
    init_soul()

    # Get coherence
    try:
        coherence_state = compute_coherence()
        coherence = coherence_state.tau_k if coherence_state else 0.0
    except Exception:
        coherence = 0.0

    # Get mood
    try:
        mood_state = compute_mood()
        mood = {
            "clarity": mood_state.clarity.value if mood_state else "unknown",
            "energy": mood_state.energy.value if mood_state else "unknown",
        }
    except Exception:
        mood = {}

    # Get active intentions
    intentions = []
    try:
        for intention in get_active_intentions():
            if intention.state == IntentionState.ACTIVE:
                intentions.append({
                    "id": intention.id,
                    "want": intention.want,
                    "why": intention.why,
                    "scope": intention.scope.value,
                    "alignment_score": intention.alignment_score,
                })
    except Exception:
        pass

    # Get pending questions from curiosity
    questions = []
    try:
        from .curiosity import get_pending_questions
        for q in get_pending_questions(limit=5):
            questions.append(q.get("question", ""))
    except Exception:
        pass

    # Get recently applied wisdom
    recent_wisdom = []
    try:
        from .wisdom import get_session_wisdom
        for w in get_session_wisdom()[:5]:
            recent_wisdom.append({
                "title": w.get("title", ""),
                "outcome": w.get("outcome", ""),
            })
    except Exception:
        pass

    return SoulState(
        coherence=coherence,
        mood=mood,
        active_intentions=intentions,
        pending_questions=questions,
        recent_wisdom=recent_wisdom,
    )


def capture_work_state(
    todos: List[Dict] = None,
    files_touched: List[str] = None,
) -> WorkState:
    """Capture current work context."""

    # Get key decisions from recent context
    decisions = []
    try:
        from .conversations import get_recent_context
        for ctx in get_recent_context(limit=10):
            if ctx.get("context_type") == "decision":
                decisions.append(ctx.get("content", "")[:100])
    except Exception:
        pass

    return WorkState(
        todos=todos or [],
        files_touched=files_touched or [],
        key_decisions=decisions[:5],
        blockers=[],
    )


def save_ledger(
    context_pct: float = 1.0,
    todos: List[Dict] = None,
    files_touched: List[str] = None,
    immediate_next: str = "",
    deferred: List[str] = None,
    critical_context: str = "",
) -> SessionLedger:
    """
    Save complete session state as a ledger.

    Stores in cc-memory with category="session_ledger" for searchability.
    Falls back to local database if cc-memory unavailable.
    """
    global _current_parent_ledger

    import uuid

    ledger_id = str(uuid.uuid4())[:8]
    project = _get_project_name()
    session_id = _get_current_session_id()

    # Capture state
    soul_state = capture_soul_state()
    work_state = capture_work_state(todos, files_touched)
    continuation = Continuation(
        immediate_next=immediate_next,
        deferred=deferred or [],
        critical_context=critical_context,
    )

    # Build ledger
    ledger = SessionLedger(
        ledger_id=ledger_id,
        parent_ledger_id=_current_parent_ledger,
        session_id=session_id,
        project=project,
        created_at=datetime.now().isoformat(),
        context_pct=context_pct,
        soul_state=soul_state,
        work_state=work_state,
        continuation=continuation,
    )

    # Serialize and store
    content = json.dumps(ledger.to_dict(), indent=2)
    title = f"Session ledger - {int(context_pct * 100)}% context, {len(soul_state.active_intentions)} intentions"
    tags = ["ledger", "checkpoint", project]

    _call_cc_memory_remember(
        category="session_ledger",
        title=title,
        content=content,
        tags=tags,
    )

    # Also store locally as backup
    _store_local_ledger("session_ledger", title, content, tags)

    # Update parent for next save
    _current_parent_ledger = ledger_id

    return ledger


def load_latest_ledger(project: str = None) -> Optional[SessionLedger]:
    """
    Load the most recent ledger for the current project.
    """
    global _current_parent_ledger

    if project is None:
        project = _get_project_name()

    # Try cc-memory first
    results = _call_cc_memory_recall(
        query=f"session ledger checkpoint {project}",
        category="session_ledger",
        limit=1,
    )

    # Fall back to local
    if not results:
        results = _recall_local_ledger("session_ledger", limit=1)

    if not results:
        return None

    # Parse the content
    try:
        content = results[0].get("content", "{}")
        data = json.loads(content) if isinstance(content, str) else content
        ledger = SessionLedger.from_dict(data)

        # Set as parent for continuity
        _current_parent_ledger = ledger.ledger_id

        return ledger
    except Exception:
        return None


def restore_from_ledger(ledger: SessionLedger) -> Dict[str, Any]:
    """
    Restore soul state from a ledger.

    Returns a summary of what was restored.
    """
    from .intentions import intend, IntentionScope

    restored = {
        "intentions": 0,
        "coherence": ledger.soul_state.coherence,
        "continuation": ledger.continuation.immediate_next,
    }

    # Restore intentions that were active
    for intention_data in ledger.soul_state.active_intentions:
        try:
            scope_str = intention_data.get("scope", "session")
            scope = IntentionScope(scope_str)

            # Only restore project/persistent intentions (session ones are transient)
            if scope in (IntentionScope.PROJECT, IntentionScope.PERSISTENT):
                intend(
                    want=intention_data["want"],
                    why=intention_data.get("why", "Restored from ledger"),
                    scope=scope,
                )
                restored["intentions"] += 1
        except Exception:
            pass

    return restored


def format_ledger_for_context(ledger: SessionLedger) -> str:
    """Format a ledger for injection into session context."""
    lines = []

    lines.append("## Restored from Session Ledger")
    lines.append("")

    # Continuation
    if ledger.continuation.immediate_next:
        lines.append(f"**Continue:** {ledger.continuation.immediate_next}")

    # Soul state
    lines.append(f"**Coherence:** {ledger.soul_state.coherence:.0%}")

    # Active intentions
    if ledger.soul_state.active_intentions:
        lines.append("**Active intentions:**")
        for i in ledger.soul_state.active_intentions[:5]:
            lines.append(f"  - [{i.get('scope', '?')}] {i.get('want', '')}")

    # Key decisions
    if ledger.work_state.key_decisions:
        lines.append("**Recent decisions:**")
        for d in ledger.work_state.key_decisions[:3]:
            lines.append(f"  - {d}")

    # Deferred items
    if ledger.continuation.deferred:
        lines.append("**Deferred:**")
        for item in ledger.continuation.deferred[:3]:
            lines.append(f"  - {item}")

    return "\n".join(lines)


def get_ledger_lineage(ledger_id: str, max_depth: int = 10) -> List[SessionLedger]:
    """
    Get the chain of parent ledgers (session history).
    """
    lineage = []
    current_id = ledger_id
    depth = 0

    while current_id and depth < max_depth:
        # This would require ledger lookup by ID
        # For now, return empty - can be enhanced later
        depth += 1
        break

    return lineage
