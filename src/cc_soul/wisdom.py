"""
Wisdom operations: gain, recall, apply, and track outcomes.
"""

import json
from datetime import datetime
from enum import Enum
from typing import List, Dict, Optional

from .core import SOUL_DIR, get_synapse_graph, save_synapse

# Session-scoped log for tracking what wisdom was applied
SESSION_WISDOM_LOG = SOUL_DIR / ".session_wisdom.json"


class WisdomType(Enum):
    """Types of universal wisdom."""

    PATTERN = "pattern"  # When X, do Y (proven heuristic)
    PRINCIPLE = "principle"  # Always/never do X (value)
    INSIGHT = "insight"  # Understanding about how something works
    FAILURE = "failure"  # What NOT to do (learned the hard way)
    PREFERENCE = "preference"  # How the user likes things done
    TERM = "term"  # Vocabulary item


def gain_wisdom(
    type: WisdomType,
    title: str,
    content: str,
    domain: str = None,
    source_project: str = None,
    confidence: float = 0.7,
) -> str:
    """
    Add universal wisdom learned from experience.

    This is for patterns that apply BEYOND the current project.
    Stored in synapse graph.
    """
    graph = get_synapse_graph()

    if type == WisdomType.FAILURE:
        result = graph.add_failure(title, content, domain)
    elif type == WisdomType.TERM:
        result = graph.add_wisdom(title, content, domain="vocabulary", confidence=confidence)
    else:
        result = graph.add_wisdom(title, content, domain, confidence)

    save_synapse()
    return result


def cleanup_duplicates() -> int:
    """
    Clean up duplicate wisdom (no-op, synapse uses vector similarity).
    """
    return 0


def _log_session_wisdom(wisdom_id: str, title: str, context: str):
    """Log wisdom application to session-scoped file."""
    try:
        if SESSION_WISDOM_LOG.exists():
            data = json.loads(SESSION_WISDOM_LOG.read_text())
        else:
            data = {"applied": [], "session_start": datetime.now().isoformat()}

        data["applied"].append(
            {
                "wisdom_id": wisdom_id,
                "title": title,
                "context": context,
                "applied_at": datetime.now().isoformat(),
            }
        )

        SESSION_WISDOM_LOG.write_text(json.dumps(data, indent=2))
    except Exception:
        pass  # Don't fail wisdom application if logging fails


def clear_session_wisdom():
    """Clear session wisdom log. Called at session start."""
    try:
        if SESSION_WISDOM_LOG.exists():
            SESSION_WISDOM_LOG.unlink()
    except Exception:
        pass


def get_session_wisdom() -> List[Dict]:
    """Get wisdom applied in current session."""
    if not SESSION_WISDOM_LOG.exists():
        return []

    try:
        data = json.loads(SESSION_WISDOM_LOG.read_text())
        return data.get("applied", [])
    except Exception:
        return []


def apply_wisdom(wisdom_id: str, context: str = "") -> str:
    """
    Record that wisdom is being applied. Returns episode ID.

    Strengthens the wisdom node and records an episode.
    """
    graph = get_synapse_graph()
    graph.strengthen(wisdom_id)

    episode_id = graph.observe(
        category="wisdom_application",
        title=f"Applied: {wisdom_id[:20]}",
        content=context or f"Wisdom {wisdom_id} was applied",
        tags=["wisdom", "applied", wisdom_id],
    )

    save_synapse()
    _log_session_wisdom(wisdom_id, wisdom_id, context)

    return episode_id


def confirm_outcome(wisdom_id: str, success: bool):
    """
    Confirm the outcome of a wisdom application.

    Strengthens or weakens the wisdom node based on outcome.
    """
    graph = get_synapse_graph()

    if success:
        graph.strengthen(wisdom_id)
    else:
        graph.weaken(wisdom_id)

    save_synapse()


def get_pending_applications() -> List[Dict]:
    """Get recent wisdom applications from episodes."""
    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="wisdom_application", limit=20)

    return [
        {
            "id": ep.get("id", ""),
            "wisdom_id": ep.get("id", ""),
            "title": ep.get("title", ""),
            "context": ep.get("content", ""),
            "applied_at": ep.get("timestamp", ""),
        }
        for ep in episodes
    ]


def recall_wisdom(
    query: str = None, type: WisdomType = None, domain: str = None, limit: int = 10
) -> List[Dict]:
    """Recall relevant wisdom using semantic search."""
    if not query:
        graph = get_synapse_graph()
        all_wisdom = graph.get_all_wisdom()
        return [
            {
                "id": w.get("id", ""),
                "type": type.value if type else "wisdom",
                "title": w.get("title", ""),
                "content": w.get("content", ""),
                "domain": w.get("domain"),
                "confidence": w.get("confidence", 0.8),
                "effective_confidence": w.get("confidence", 0.8),
            }
            for w in all_wisdom[:limit]
        ]

    return quick_recall(query, limit=limit, domain=domain)


def get_wisdom_by_id(wisdom_id: str) -> Optional[Dict]:
    """Get a specific wisdom entry by ID."""
    graph = get_synapse_graph()
    all_wisdom = graph.get_all_wisdom()

    for w in all_wisdom:
        if w.get("id") == wisdom_id:
            return {
                "id": w.get("id", ""),
                "type": "wisdom",
                "title": w.get("title", ""),
                "content": w.get("content", ""),
                "domain": w.get("domain"),
                "confidence": w.get("confidence", 0.8),
                "timestamp": w.get("timestamp", ""),
            }

    return None


def quick_recall(query: str, limit: int = 5, domain: str = None) -> List[Dict]:
    """
    Fast semantic recall via synapse.

    Uses Rust-based vector search for relevant wisdom.
    """
    graph = get_synapse_graph()
    results = []
    for concept, score in graph.search(query, limit=limit):
        results.append({
            "id": concept.id,
            "type": concept.metadata.get("type", "wisdom"),
            "title": concept.title,
            "content": concept.content,
            "domain": concept.metadata.get("domain"),
            "confidence": concept.metadata.get("confidence", 0.8),
            "effective_confidence": score,
            "success_rate": None,
            "combined_score": score,
        })
    return results


def semantic_recall(query: str, limit: int = 5, domain: str = None) -> List[Dict]:
    """Semantic search for relevant wisdom (alias for quick_recall)."""
    return quick_recall(query, limit=limit, domain=domain)


def get_dormant_wisdom(limit: int = 3, min_confidence: float = 0.6) -> List[Dict]:
    """
    Get high-confidence wisdom sorted by confidence.

    Returns wisdom that may benefit from application.
    """
    graph = get_synapse_graph()
    all_wisdom = graph.get_all_wisdom()

    results = []
    for w in all_wisdom:
        confidence = w.get("confidence", 0.8)
        if confidence >= min_confidence:
            results.append({
                "id": w.get("id", ""),
                "type": "wisdom",
                "title": w.get("title", ""),
                "content": w.get("content", ""),
                "domain": w.get("domain"),
                "confidence": confidence,
                "effective_confidence": confidence,
            })

    results.sort(key=lambda x: x["confidence"], reverse=True)
    return results[:limit]
