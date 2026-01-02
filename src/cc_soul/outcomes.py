"""
Outcome tracking and structured handoffs.

Inspired by Continuous-Claude-v2's approach:
- Sessions have explicit outcomes (SUCCEEDED, PARTIAL, FAILED)
- Handoffs stored in synapse graph for session continuity
- Outcomes feed into the learning loop
"""

import json
from datetime import datetime
from enum import Enum
from pathlib import Path
from typing import Dict, List, Optional, Any

from .core import get_synapse_graph, save_synapse, SOUL_DIR


class Outcome(Enum):
    """Session outcome types."""

    SUCCEEDED = "succeeded"
    PARTIAL_PLUS = "partial_plus"
    PARTIAL_MINUS = "partial_minus"
    FAILED = "failed"
    UNKNOWN = "unknown"


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


def detect_outcome(messages: List[Dict], files_touched: set = None) -> Outcome:
    """
    Detect session outcome from conversation messages.

    Analyzes final messages for success/failure signals.
    """
    if not messages:
        return Outcome.UNKNOWN

    recent = messages[-5:] if len(messages) > 5 else messages
    full_text = " ".join(m.get("content", "") for m in recent).lower()

    scores = {outcome: 0 for outcome in Outcome}

    for outcome, signals in OUTCOME_SIGNALS.items():
        for signal in signals:
            if signal in full_text:
                scores[outcome] += 1

    max_score = max(scores.values())
    if max_score == 0:
        return Outcome.UNKNOWN

    for outcome, score in scores.items():
        if score == max_score:
            return outcome

    return Outcome.UNKNOWN


def record_outcome(conv_id: int, outcome: Outcome, notes: str = "") -> bool:
    """Record the outcome of a session."""
    graph = get_synapse_graph()

    graph.observe(
        category="session_outcome",
        title=f"Session {conv_id}: {outcome.value}",
        content=notes or f"Session completed with outcome: {outcome.value}",
        tags=["outcome", outcome.value, f"session_{conv_id}"],
    )
    save_synapse()
    return True


def get_outcome_stats(days: int = 30) -> Dict[str, Any]:
    """Get outcome statistics for learning."""
    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="session_outcome", limit=500)

    distribution = {}
    for ep in episodes:
        tags = ep.get("tags", [])
        for tag in tags:
            if tag in ["succeeded", "partial_plus", "partial_minus", "failed", "unknown"]:
                distribution[tag] = distribution.get(tag, 0) + 1
                break

    succeeded = distribution.get("succeeded", 0)
    total = sum(distribution.values())

    success_rate = succeeded / total if total > 0 else 0

    return {
        "distribution": distribution,
        "success_rate": success_rate,
        "total_sessions": total,
    }


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
) -> Optional[str]:
    """
    Create a structured handoff in synapse graph.

    Returns the observation ID if successful, None otherwise.
    """
    content_parts = []

    if goal:
        content_parts.append(f"**Goal:** {goal}")

    content_parts.append(f"**Summary:** {summary}")

    if completed:
        content_parts.append("**Completed:**")
        for item in completed:
            content_parts.append(f"- [x] {item}")

    if in_progress:
        content_parts.append("**In Progress:**")
        for item in in_progress:
            content_parts.append(f"- [ ] {item}")

    if next_steps:
        content_parts.append("**Next Steps:**")
        for i, step in enumerate(next_steps, 1):
            content_parts.append(f"{i}. {step}")

    if key_decisions:
        content_parts.append("**Key Decisions:**")
        for d in key_decisions:
            content_parts.append(f"- {d}")

    if learnings:
        content_parts.append("**Learnings:**")
        for l in learnings:
            content_parts.append(f"- {l}")

    if blockers:
        content_parts.append("**Blockers:**")
        for b in blockers:
            content_parts.append(f"- {b}")

    if files_touched:
        content_parts.append(f"**Files:** {', '.join(files_touched[:10])}")

    if context:
        content_parts.append(f"**Context:** {context}")

    content = "\n".join(content_parts)
    title = f"Session handoff: {goal[:50] if goal else summary[:50]}..."

    try:
        graph = get_synapse_graph()

        tags = ["handoff"]
        if blockers:
            tags.append("has_blockers")
        if next_steps:
            tags.append("has_next_steps")

        obs_id = graph.observe(
            category="handoff",
            title=title,
            content=content,
            tags=tags,
        )
        save_synapse()
        return obs_id
    except Exception:
        return None


def create_auto_handoff(
    messages: List[Dict],
    files_touched: set = None,
    project: str = "",
    project_root: Path = None,
) -> Optional[str]:
    """
    Auto-generate a handoff from session messages.

    Called by PreCompact hook to preserve context before compaction.
    """
    if not messages or len(messages) < 3:
        return None

    full_text = " ".join(m.get("content", "")[:500] for m in messages[-20:])

    goal = ""
    if messages and messages[0].get("role") == "user":
        goal = messages[0].get("content", "")[:200]

    summary = ""
    for msg in reversed(messages):
        if msg.get("role") == "assistant":
            summary = msg.get("content", "")[:300]
            break

    if not summary:
        summary = "Session in progress"

    completed = []
    in_progress = []
    next_steps = []

    for msg in messages[-10:]:
        content = msg.get("content", "").lower()
        if "done" in content or "completed" in content or "fixed" in content:
            lines = msg.get("content", "").split("\n")[:3]
            for line in lines:
                if len(line) > 10 and len(line) < 100:
                    completed.append(line.strip())
                    break

    learnings = []
    for msg in messages:
        content = msg.get("content", "")
        if "[PATTERN]" in content or "[INSIGHT]" in content or "[LEARNED]" in content:
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


def get_latest_handoff(project_root: Path = None) -> Optional[Dict]:
    """Get the most recent handoff from synapse graph."""
    try:
        graph = get_synapse_graph()
        episodes = graph.get_episodes(category="handoff", limit=20)

        for ep in episodes:
            return {
                "id": ep.get("id", ""),
                "title": ep.get("title", ""),
                "content": ep.get("content", ""),
                "timestamp": ep.get("timestamp", ep.get("created_at", "")),
                "tags": ep.get("tags", []),
            }
        return None
    except Exception:
        return None


def load_handoff(handoff: Dict) -> Dict[str, Any]:
    """Parse a handoff observation into structured data."""
    if not handoff:
        return {}

    content = handoff.get("content", "")

    result = {
        "id": handoff.get("id", ""),
        "content": content,
        "goal": "",
        "summary": "",
        "completed": [],
        "in_progress": [],
        "next_steps": [],
        "timestamp": handoff.get("timestamp", ""),
    }

    for line in content.split("\n"):
        if line.startswith("**Goal:**"):
            result["goal"] = line.replace("**Goal:**", "").strip()
        elif line.startswith("**Summary:**"):
            result["summary"] = line.replace("**Summary:**", "").strip()
        elif line.startswith("- [x]"):
            result["completed"].append(line.replace("- [x]", "").strip())
        elif line.startswith("- [ ]"):
            result["in_progress"].append(line.replace("- [ ]", "").strip())
        elif line[0:2].isdigit() or (len(line) > 1 and line[0].isdigit() and line[1] == "."):
            result["next_steps"].append(line.split(".", 1)[-1].strip())

    return result


def list_handoffs(project_root: Path = None, limit: int = 10) -> List[Dict]:
    """List recent handoffs from synapse graph."""
    try:
        graph = get_synapse_graph()
        episodes = graph.get_episodes(category="handoff", limit=limit * 5)

        result = []
        for ep in episodes:
            result.append({
                "id": ep.get("id", ""),
                "title": ep.get("title", ""),
                "timestamp": ep.get("timestamp", ep.get("created_at", "")),
            })
            if len(result) >= limit:
                break
        return result
    except Exception:
        return []


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
    graph = get_synapse_graph()

    graph.observe(
        category="conversation_handoff",
        title=f"Conversation {conv_id} handoff",
        content=str(handoff_path),
        tags=["handoff_link", f"conv_{conv_id}"],
    )
    save_synapse()
    return True
