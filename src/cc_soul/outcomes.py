"""
Outcome tracking and structured handoffs.

Inspired by Continuous-Claude-v2's approach:
- Sessions have explicit outcomes (SUCCEEDED, PARTIAL, FAILED)
- Handoffs are human-readable markdown files for session continuity
- Outcomes feed into the learning loop
"""

import json
from datetime import datetime
from enum import Enum
from pathlib import Path
from typing import Dict, List, Optional, Any

from .core import get_db_connection, SOUL_DIR


class Outcome(Enum):
    """Session outcome types."""

    SUCCEEDED = "succeeded"  # Goal fully achieved
    PARTIAL_PLUS = "partial_plus"  # Significant progress, not complete
    PARTIAL_MINUS = "partial_minus"  # Some progress, blocked or stuck
    FAILED = "failed"  # Goal not achieved, errors or wrong approach
    UNKNOWN = "unknown"  # No clear outcome detected


# Outcome detection signals
OUTCOME_SIGNALS = {
    Outcome.SUCCEEDED: [
        "done",
        "complete",
        "finished",
        "merged",
        "shipped",
        "deployed",
        "all tests pass",
        "working",
        "fixed",
        "resolved",
    ],
    Outcome.FAILED: [
        "give up",
        "can't figure",
        "doesn't work",
        "still broken",
        "blocked",
        "impossible",
        "abort",
        "revert",
    ],
    Outcome.PARTIAL_PLUS: [
        "progress",
        "getting close",
        "almost",
        "most of it",
        "partially",
        "good start",
    ],
    Outcome.PARTIAL_MINUS: [
        "stuck",
        "not sure",
        "confused",
        "need help",
        "complicated",
        "harder than",
    ],
}


def _ensure_outcome_column():
    """Ensure outcome column exists in conversations table."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute("PRAGMA table_info(conversations)")
    columns = [col[1] for col in c.fetchall()]

    if "outcome" not in columns:
        c.execute("ALTER TABLE conversations ADD COLUMN outcome TEXT DEFAULT 'unknown'")
        conn.commit()

    if "handoff_path" not in columns:
        c.execute("ALTER TABLE conversations ADD COLUMN handoff_path TEXT")
        conn.commit()

    conn.close()


def detect_outcome(messages: List[Dict], files_touched: set = None) -> Outcome:
    """
    Detect session outcome from conversation messages.

    Analyzes final messages for success/failure signals.
    """
    if not messages:
        return Outcome.UNKNOWN

    # Focus on last few messages (most indicative of outcome)
    recent = messages[-5:] if len(messages) > 5 else messages
    full_text = " ".join(m.get("content", "") for m in recent).lower()

    # Score each outcome type
    scores = {outcome: 0 for outcome in Outcome}

    for outcome, signals in OUTCOME_SIGNALS.items():
        for signal in signals:
            if signal in full_text:
                scores[outcome] += 1

    # Find highest scoring outcome
    max_score = max(scores.values())
    if max_score == 0:
        return Outcome.UNKNOWN

    for outcome, score in scores.items():
        if score == max_score:
            return outcome

    return Outcome.UNKNOWN


def record_outcome(conv_id: int, outcome: Outcome, notes: str = "") -> bool:
    """Record the outcome of a session."""
    _ensure_outcome_column()

    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        UPDATE conversations
        SET outcome = ?
        WHERE id = ?
    """,
        (outcome.value, conv_id),
    )

    conn.commit()
    conn.close()
    return True


def get_outcome_stats(days: int = 30) -> Dict[str, Any]:
    """Get outcome statistics for learning."""
    _ensure_outcome_column()

    conn = get_db_connection()
    c = conn.cursor()

    c.execute("""
        SELECT outcome, COUNT(*) as count
        FROM conversations
        WHERE outcome IS NOT NULL
        GROUP BY outcome
    """)

    distribution = {row[0]: row[1] for row in c.fetchall()}

    c.execute("SELECT COUNT(*) FROM conversations WHERE outcome = 'succeeded'")
    succeeded = c.fetchone()[0]

    c.execute("SELECT COUNT(*) FROM conversations WHERE outcome IS NOT NULL")
    total = c.fetchone()[0]

    conn.close()

    success_rate = succeeded / total if total > 0 else 0

    return {
        "distribution": distribution,
        "success_rate": success_rate,
        "total_sessions": total,
    }


# =============================================================================
# HANDOFFS - Structured session continuation documents
# =============================================================================

# Default handoff directory (in project, not soul)
HANDOFF_DIR_NAME = ".claude/handoffs"


def get_handoff_dir(project_root: Path = None) -> Path:
    """Get handoff directory for current project."""
    if project_root is None:
        project_root = Path.cwd()

    handoff_dir = project_root / HANDOFF_DIR_NAME
    handoff_dir.mkdir(parents=True, exist_ok=True)
    return handoff_dir


def create_handoff(
    summary: str,
    goal: str = "",
    completed: List[str] = None,
    in_progress: List[str] = None,
    next_steps: List[str] = None,
    key_decisions: List[str] = None,
    files_touched: List[str] = None,
    learnings: List[str] = None,
    blockers: List[str] = None,
    context: str = "",
    project_root: Path = None,
) -> Path:
    """
    Create a structured handoff document for session continuation.

    Returns the path to the created handoff file.
    """
    handoff_dir = get_handoff_dir(project_root)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    handoff_path = handoff_dir / f"handoff-{timestamp}.md"

    lines = []

    # Header
    lines.append(f"# Session Handoff - {datetime.now().strftime('%Y-%m-%d %H:%M')}")
    lines.append("")

    # Goal
    if goal:
        lines.append("## Goal")
        lines.append(goal)
        lines.append("")

    # Summary
    lines.append("## Summary")
    lines.append(summary)
    lines.append("")

    # Status
    lines.append("## Status")
    lines.append("")

    if completed:
        lines.append("### Completed")
        for item in completed:
            lines.append(f"- [x] {item}")
        lines.append("")

    if in_progress:
        lines.append("### In Progress")
        for item in in_progress:
            lines.append(f"- [ ] {item}")
        lines.append("")

    if next_steps:
        lines.append("### Next Steps")
        for i, step in enumerate(next_steps, 1):
            lines.append(f"{i}. {step}")
        lines.append("")

    # Key Decisions
    if key_decisions:
        lines.append("## Key Decisions")
        for decision in key_decisions:
            lines.append(f"- {decision}")
        lines.append("")

    # Files Touched
    if files_touched:
        lines.append("## Files Modified")
        for f in files_touched[:20]:  # Limit to 20 files
            lines.append(f"- `{f}`")
        lines.append("")

    # Learnings
    if learnings:
        lines.append("## Learnings")
        for learning in learnings:
            lines.append(f"- {learning}")
        lines.append("")

    # Blockers
    if blockers:
        lines.append("## Blockers")
        for blocker in blockers:
            lines.append(f"- {blocker}")
        lines.append("")

    # Additional Context
    if context:
        lines.append("## Context")
        lines.append(context)
        lines.append("")

    # Footer
    lines.append("---")
    lines.append(f"*Generated: {datetime.now().isoformat()}*")

    # Write file
    handoff_path.write_text("\n".join(lines))

    return handoff_path


def create_auto_handoff(
    messages: List[Dict],
    files_touched: set = None,
    project: str = "",
    project_root: Path = None,
) -> Optional[Path]:
    """
    Auto-generate a handoff from session messages.

    Called by PreCompact hook to preserve context before compaction.
    """
    if not messages or len(messages) < 3:
        return None

    # Extract key information from messages
    full_text = " ".join(m.get("content", "")[:500] for m in messages[-20:])

    # Detect goal from early messages
    goal = ""
    if messages and messages[0].get("role") == "user":
        goal = messages[0].get("content", "")[:200]

    # Summary from recent assistant output
    summary = ""
    for msg in reversed(messages):
        if msg.get("role") == "assistant":
            summary = msg.get("content", "")[:300]
            break

    if not summary:
        summary = "Session in progress"

    # Extract completed items (look for completion signals)
    completed = []
    in_progress = []
    next_steps = []

    for msg in messages[-10:]:
        content = msg.get("content", "").lower()
        if "done" in content or "completed" in content or "fixed" in content:
            # Extract what was done
            lines = msg.get("content", "").split("\n")[:3]
            for line in lines:
                if len(line) > 10 and len(line) < 100:
                    completed.append(line.strip())
                    break

    # Extract learnings (look for insight markers)
    learnings = []
    for msg in messages:
        content = msg.get("content", "")
        if "[PATTERN]" in content or "[INSIGHT]" in content or "[LEARNED]" in content:
            # Extract the learning
            for line in content.split("\n"):
                if any(marker in line for marker in ["[PATTERN]", "[INSIGHT]", "[LEARNED]"]):
                    learnings.append(line.strip()[:100])
                    break

    return create_handoff(
        summary=summary,
        goal=goal,
        completed=completed[:5],
        in_progress=in_progress[:5],
        next_steps=next_steps[:5],
        learnings=learnings[:5],
        files_touched=list(files_touched)[:20] if files_touched else None,
        project_root=project_root,
    )


def get_latest_handoff(project_root: Path = None) -> Optional[Path]:
    """Get the most recent handoff file."""
    handoff_dir = get_handoff_dir(project_root)

    handoffs = sorted(handoff_dir.glob("handoff-*.md"), reverse=True)
    return handoffs[0] if handoffs else None


def load_handoff(handoff_path: Path) -> Dict[str, Any]:
    """Load and parse a handoff file."""
    if not handoff_path.exists():
        return {}

    content = handoff_path.read_text()

    # Simple parsing - extract sections
    result = {
        "path": str(handoff_path),
        "content": content,
        "goal": "",
        "summary": "",
        "completed": [],
        "next_steps": [],
    }

    current_section = None
    for line in content.split("\n"):
        if line.startswith("## Goal"):
            current_section = "goal"
        elif line.startswith("## Summary"):
            current_section = "summary"
        elif line.startswith("### Completed"):
            current_section = "completed"
        elif line.startswith("### Next Steps"):
            current_section = "next_steps"
        elif line.startswith("## "):
            current_section = None
        elif current_section and line.strip():
            if current_section in ("goal", "summary"):
                result[current_section] += line + " "
            elif current_section in ("completed", "next_steps"):
                if line.startswith("- ") or line.startswith("1."):
                    result[current_section].append(line[2:].strip())

    return result


def list_handoffs(project_root: Path = None, limit: int = 10) -> List[Dict]:
    """List recent handoffs."""
    handoff_dir = get_handoff_dir(project_root)

    handoffs = sorted(handoff_dir.glob("handoff-*.md"), reverse=True)[:limit]

    result = []
    for h in handoffs:
        try:
            stat = h.stat()
            result.append({
                "path": str(h),
                "name": h.name,
                "created": datetime.fromtimestamp(stat.st_mtime).isoformat(),
                "size": stat.st_size,
            })
        except OSError:
            pass

    return result


def cleanup_old_handoffs(keep: int = 20, project_root: Path = None) -> int:
    """Remove old handoffs, keeping the most recent ones."""
    handoff_dir = get_handoff_dir(project_root)

    handoffs = sorted(handoff_dir.glob("handoff-*.md"), reverse=True)

    deleted = 0
    for h in handoffs[keep:]:
        try:
            h.unlink()
            deleted += 1
        except OSError:
            pass

    return deleted


def format_handoff_for_context(handoff: Dict) -> str:
    """Format a loaded handoff for context injection."""
    lines = []

    if handoff.get("goal"):
        lines.append(f"**Goal:** {handoff['goal'].strip()}")

    if handoff.get("summary"):
        lines.append(f"**Last session:** {handoff['summary'].strip()[:200]}")

    if handoff.get("next_steps"):
        lines.append("**Next steps:**")
        for step in handoff["next_steps"][:3]:
            lines.append(f"  - {step}")

    return "\n".join(lines)


def link_handoff_to_conversation(conv_id: int, handoff_path: Path) -> bool:
    """Link a handoff file to a conversation record."""
    _ensure_outcome_column()

    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        UPDATE conversations
        SET handoff_path = ?
        WHERE id = ?
    """,
        (str(handoff_path), conv_id),
    )

    conn.commit()
    conn.close()
    return True
