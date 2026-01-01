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
from dataclasses import dataclass, field, asdict
from datetime import datetime
from typing import List, Dict, Optional, Any
from pathlib import Path

from .core import init_soul
from .intentions import get_active_intentions, IntentionState
from .coherence import compute_coherence
from .mood import compute_mood

# Try to import cc-memory directly
try:
    import sys
    sys.path.insert(0, str(Path.home() / "repos/cc-memory/src"))
    sys.path.insert(0, "/maps/projects/fernandezguerra/apps/repos/cc-memory/src")
    from cc_memory import remember as cc_remember, recall as cc_recall
    CC_MEMORY_AVAILABLE = True
except ImportError:
    CC_MEMORY_AVAILABLE = False


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


def _get_current_project_dir() -> str:
    """Get the current project directory for cc-memory."""
    import subprocess
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode == 0:
            return result.stdout.strip()
    except Exception:
        pass
    return str(Path.cwd())


def _call_cc_memory_remember(category: str, title: str, content: str, tags: List[str]) -> Optional[str]:
    """
    Store an observation in cc-memory.

    Uses direct import of cc-memory module.
    Returns the observation ID if successful.
    """
    if not CC_MEMORY_AVAILABLE:
        return None

    try:
        project_dir = _get_current_project_dir()
        result = cc_remember(
            project_dir=project_dir,
            category=category,
            title=title,
            content=content,
            tags=tags,
        )
        # cc_remember returns a string ID directly
        return result if isinstance(result, str) else result.get("id")
    except Exception:
        return None


def _call_cc_memory_recall(query: str, category: str = None, limit: int = 1) -> List[Dict]:
    """
    Recall observations from cc-memory.
    """
    if not CC_MEMORY_AVAILABLE:
        return []

    try:
        project_dir = _get_current_project_dir()
        return cc_recall(project_dir=project_dir, query=query, category=category, limit=limit)
    except Exception:
        return []


# Local storage removed - cc-memory is the single source of truth


def capture_soul_state() -> SoulState:
    """Capture current soul state."""
    init_soul()

    # Get coherence
    try:
        coherence_state = compute_coherence()
        coherence = coherence_state.value if coherence_state else 0.0
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

    # Store in cc-memory (single source of truth)
    _call_cc_memory_remember(
        category="session_ledger",
        title=title,
        content=content,
        tags=tags,
    )

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

    # Load from cc-memory using recent observations (timestamp-ordered, not semantic)
    if not CC_MEMORY_AVAILABLE:
        return None

    try:
        from cc_memory import memory as cc_memory
        project_dir = _get_current_project_dir()
        observations = cc_memory.get_recent_observations(project_dir, limit=20)

        # Find most recent ledger for this project
        for obs in observations:
            if obs.get("category") == "session_ledger":
                content = obs.get("content", "{}")
                data = json.loads(content) if isinstance(content, str) else content
                ledger = SessionLedger.from_dict(data)

                # Set as parent for continuity
                _current_parent_ledger = ledger.ledger_id

                return ledger
        return None
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
