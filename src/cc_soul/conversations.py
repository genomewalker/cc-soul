"""
Conversation tracking: session history via synapse episodes.
"""

import json
from datetime import datetime, timedelta
from typing import List, Dict, Optional, Any
import uuid

from .core import get_synapse_graph, save_synapse, SOUL_DIR


def start_conversation(project: str = None) -> str:
    """Start a new conversation, return ID."""
    graph = get_synapse_graph()
    conv_id = f"conv_{uuid.uuid4().hex[:8]}"

    graph.observe(
        category="conversation",
        title=conv_id,
        content=json.dumps({
            "project": project,
            "started_at": datetime.now().isoformat(),
            "status": "active",
        }),
        project=project,
        tags=["conversation", "active"],
    )
    save_synapse()

    # Track current conversation
    conv_file = SOUL_DIR / ".current_conversation"
    conv_file.write_text(conv_id)

    return conv_id


def end_conversation(
    conv_id: str, summary: str, emotional_tone: str = "", key_moments: List[str] = None
):
    """End a conversation with summary."""
    graph = get_synapse_graph()

    graph.observe(
        category="conversation_end",
        title=f"end:{conv_id}",
        content=json.dumps({
            "summary": summary,
            "emotional_tone": emotional_tone,
            "key_moments": key_moments or [],
            "ended_at": datetime.now().isoformat(),
        }),
        tags=["conversation", "ended", conv_id],
    )
    save_synapse()


def get_conversation(conv_id: str) -> Optional[Dict]:
    """Get a specific conversation by ID."""
    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="conversation", limit=100)

    for ep in episodes:
        if ep.get("title") == conv_id:
            try:
                data = json.loads(ep.get("content", "{}"))
                return {
                    "id": conv_id,
                    "project": data.get("project"),
                    "started_at": data.get("started_at"),
                    "ended_at": data.get("ended_at"),
                    "summary": data.get("summary", ""),
                    "emotional_tone": data.get("emotional_tone", ""),
                    "key_moments": data.get("key_moments", []),
                }
            except json.JSONDecodeError:
                pass

    return None


def get_conversations(
    project: str = None, limit: int = 20, days: int = None, with_summary: bool = False
) -> List[Dict]:
    """Get past conversations."""
    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="conversation", project=project, limit=limit * 2)

    results = []
    cutoff = None
    if days:
        cutoff = (datetime.now() - timedelta(days=days)).isoformat()

    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
            started_at = data.get("started_at", "")

            if cutoff and started_at < cutoff:
                continue

            if with_summary and not data.get("summary"):
                continue

            results.append({
                "id": ep.get("title", ""),
                "project": data.get("project"),
                "started_at": started_at,
                "ended_at": data.get("ended_at"),
                "summary": data.get("summary", ""),
                "emotional_tone": data.get("emotional_tone", ""),
                "key_moments": data.get("key_moments", []),
            })

            if len(results) >= limit:
                break
        except json.JSONDecodeError:
            continue

    return results


def get_project_context(project: str, limit: int = 5) -> Dict[str, Any]:
    """Get context for a project from past conversations."""
    conversations = get_conversations(project=project, limit=limit, with_summary=True)

    return {
        "project": project,
        "total_conversations": len(conversations),
        "recent_conversations": conversations,
        "wisdom_applied": [],
    }


def link_wisdom_application(application_id: str, conversation_id: str):
    """Link a wisdom application to a conversation (no-op in synapse)."""
    pass


def get_conversation_wisdom(conv_id: str) -> List[Dict]:
    """Get wisdom applied in a conversation (searches episodes)."""
    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="wisdom_application", limit=50)

    results = []
    for ep in episodes:
        tags = ep.get("tags", [])
        if conv_id in tags:
            results.append({
                "wisdom_id": ep.get("id", ""),
                "title": ep.get("title", ""),
                "content": ep.get("content", ""),
                "applied_at": ep.get("timestamp", ""),
            })

    return results


def get_conversation_stats() -> Dict[str, Any]:
    """Get overall conversation statistics."""
    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="conversation", limit=500)

    projects = {}
    with_summary = 0

    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
            project = data.get("project")
            if project:
                projects[project] = projects.get(project, 0) + 1
            if data.get("summary"):
                with_summary += 1
        except json.JSONDecodeError:
            continue

    top_projects = sorted(projects.items(), key=lambda x: x[1], reverse=True)[:5]

    return {
        "total_conversations": len(episodes),
        "unique_projects": len(projects),
        "top_projects": [{"project": p, "count": c} for p, c in top_projects],
        "conversations_with_summary": with_summary,
        "average_duration_minutes": 0,
    }


def search_conversations(query: str, limit: int = 10) -> List[Dict]:
    """Search conversations using semantic search."""
    graph = get_synapse_graph()
    results = graph.search(query, limit=limit)

    conversations = []
    for concept, score in results:
        if "conversation" in concept.metadata.get("category", ""):
            try:
                data = json.loads(concept.content)
                conversations.append({
                    "id": concept.title,
                    "project": data.get("project"),
                    "started_at": data.get("started_at"),
                    "summary": data.get("summary", ""),
                    "key_moments": data.get("key_moments", []),
                })
            except json.JSONDecodeError:
                pass

    return conversations[:limit]


def save_context(
    content: str, context_type: str = "insight", priority: int = 5, conv_id: str = None
) -> str:
    """Save key context as an episode."""
    graph = get_synapse_graph()

    conv_file = SOUL_DIR / ".current_conversation"
    if conv_id is None and conv_file.exists():
        try:
            conv_id = conv_file.read_text().strip()
        except FileNotFoundError:
            pass

    ep_id = graph.observe(
        category="session_context",
        title=f"{context_type}:{priority}",
        content=content,
        tags=["context", context_type, conv_id] if conv_id else ["context", context_type],
    )
    save_synapse()

    return ep_id


def get_saved_context(
    conv_id: str = None, limit: int = 20, context_types: List[str] = None
) -> List[Dict]:
    """Get saved context for current or specified session."""
    graph = get_synapse_graph()

    conv_file = SOUL_DIR / ".current_conversation"
    if conv_id is None and conv_file.exists():
        try:
            conv_id = conv_file.read_text().strip()
        except FileNotFoundError:
            pass

    episodes = graph.get_episodes(category="session_context", limit=limit * 2)

    results = []
    for ep in episodes:
        tags = ep.get("tags", [])
        title = ep.get("title", "")

        if conv_id and conv_id not in tags:
            continue

        ctx_type = title.split(":")[0] if ":" in title else "insight"
        priority = 5
        try:
            priority = int(title.split(":")[1]) if ":" in title else 5
        except (ValueError, IndexError):
            pass

        if context_types and ctx_type not in context_types:
            continue

        results.append({
            "id": ep.get("id", ""),
            "type": ctx_type,
            "content": ep.get("content", ""),
            "priority": priority,
            "created_at": ep.get("timestamp", ""),
        })

        if len(results) >= limit:
            break

    results.sort(key=lambda x: x["priority"], reverse=True)
    return results


def get_recent_context(hours: int = 24, limit: int = 30) -> List[Dict]:
    """Get context saved in the last N hours."""
    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="session_context", limit=limit * 2)

    cutoff = (datetime.now() - timedelta(hours=hours)).isoformat()

    results = []
    for ep in episodes:
        timestamp = ep.get("timestamp", "")
        if timestamp and timestamp > cutoff:
            title = ep.get("title", "")
            ctx_type = title.split(":")[0] if ":" in title else "insight"
            priority = 5
            try:
                priority = int(title.split(":")[1]) if ":" in title else 5
            except (ValueError, IndexError):
                pass

            results.append({
                "id": ep.get("id", ""),
                "type": ctx_type,
                "content": ep.get("content", ""),
                "priority": priority,
                "created_at": timestamp,
                "project": None,
                "session_summary": None,
            })

    results.sort(key=lambda x: x["priority"], reverse=True)
    return results[:limit]


def format_context_restoration(contexts: List[Dict]) -> str:
    """Format saved context for injection at session start."""
    if not contexts:
        return ""

    lines = []
    lines.append("## Saved Context (from recent work)")
    lines.append("")

    type_labels = {
        "insight": "Insights",
        "decision": "Decisions",
        "blocker": "Blockers",
        "progress": "Progress",
        "key_file": "Key Files",
        "todo": "Todos",
    }

    by_type = {}
    for ctx in contexts:
        t = ctx["type"]
        if t not in by_type:
            by_type[t] = []
        by_type[t].append(ctx)

    for ctx_type, items in by_type.items():
        label = type_labels.get(ctx_type, ctx_type.title())
        lines.append(f"### {label}")
        for item in items[:5]:
            content = item["content"][:150] + "..." if len(item["content"]) > 150 else item["content"]
            lines.append(f"- {content}")
        lines.append("")

    return "\n".join(lines)


def clear_old_context(days: int = 7) -> int:
    """Clear old context (handled by synapse decay, returns 0)."""
    return 0
