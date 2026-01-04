"""
Temporal dynamics for the soul.

Time shapes memory: what's used grows stronger, what's ignored fades.
The soul feels time passing through decay, strengthening, and aging.

This module unifies temporal mechanics:
- Decay: unused things fade (wisdom, identity, beliefs)
- Strengthening: used things grow (confidence, weights, activation)
- Aging: everything has a temporal signature
- Events: unified chronological log of soul activity
- Proactive: surface things that haven't been seen in a while
"""

from dataclasses import dataclass
from datetime import datetime, timedelta
from enum import Enum
from typing import Optional
import json
import hashlib

from .core import get_synapse_graph, save_synapse, SOUL_DIR


class EventType(str, Enum):
    """Types of soul events for the unified log."""
    # Wisdom events
    WISDOM_GAINED = "wisdom_gained"
    WISDOM_APPLIED = "wisdom_applied"
    WISDOM_CONFIRMED = "wisdom_confirmed"
    WISDOM_CHALLENGED = "wisdom_challenged"
    WISDOM_DECAYED = "wisdom_decayed"

    # Belief events
    BELIEF_FORMED = "belief_formed"
    BELIEF_REVISED = "belief_revised"
    BELIEF_ABANDONED = "belief_abandoned"

    # Identity events
    IDENTITY_OBSERVED = "identity_observed"
    IDENTITY_CONFIRMED = "identity_confirmed"
    IDENTITY_STALE = "identity_stale"

    # Intention events
    INTENTION_SET = "intention_set"
    INTENTION_CHECKED = "intention_checked"
    INTENTION_FULFILLED = "intention_fulfilled"
    INTENTION_ABANDONED = "intention_abandoned"

    # Coherence events
    COHERENCE_MEASURED = "coherence_measured"
    COHERENCE_SHIFT = "coherence_shift"

    # Insight events
    INSIGHT_CRYSTALLIZED = "insight_crystallized"

    # Session events
    SESSION_START = "session_start"
    SESSION_END = "session_end"

    # Proactive events
    PROACTIVE_SURFACE = "proactive_surface"
    PROACTIVE_QUESTION = "proactive_question"


@dataclass
class TemporalConfig:
    """Configuration for temporal dynamics."""
    # Decay rates (per month)
    wisdom_decay_rate: float = 0.05  # 5% per month
    identity_decay_rate: float = 0.03  # 3% per month (more stable)
    belief_decay_rate: float = 0.02  # 2% per month (very stable)

    # Strengthening rates (per use)
    wisdom_strengthen_rate: float = 0.05  # +5% per successful use
    identity_confirm_rate: float = 0.02  # +2% per confirmation

    # Thresholds
    stale_threshold_days: int = 30  # Consider stale after 30 days
    proactive_threshold_days: int = 14  # Surface if unseen for 14 days
    decay_floor: float = 0.1  # Minimum confidence after decay

    # Graph dynamics
    edge_decay_rate: float = 0.05  # 5% per month
    activation_boost: float = 0.1  # +0.1 weight per activation


# Default config
TEMPORAL_CONFIG = TemporalConfig()


def init_temporal_tables():
    """Initialize temporal infrastructure (synapse handles this automatically)."""
    get_synapse_graph()


def log_event(
    event_type: EventType,
    entity_type: str = None,
    entity_id: str = None,
    data: dict = None,
    coherence: float = None,
    session_id: int = None,
) -> str:
    """
    Log an event to the unified timeline.

    Returns the event ID.
    """
    graph = get_synapse_graph()

    event_type_str = event_type.value if isinstance(event_type, EventType) else event_type

    content = json.dumps({
        "entity_type": entity_type,
        "entity_id": entity_id,
        "data": data,
        "coherence_at_event": coherence,
        "session_id": session_id,
        "timestamp": datetime.now().isoformat(),
    })

    tags = ["soul_event", event_type_str]
    if entity_type:
        tags.append(f"entity:{entity_type}")
    if entity_id:
        tags.append(f"id:{entity_id}")

    event_id = graph.observe(
        category="soul_event",
        title=f"{event_type_str}:{entity_type or 'system'}:{entity_id or 'none'}",
        content=content,
        tags=tags,
    )

    save_synapse()
    return event_id


def get_events(
    event_type: EventType = None,
    entity_type: str = None,
    since: datetime = None,
    limit: int = 50,
) -> list:
    """Query events from the timeline."""
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="soul_event", limit=limit * 2)

    events = []
    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            data = {"content": ep.get("content", "")}

        title = ep.get("title", "")
        parts = title.split(":")
        ep_event_type = parts[0] if parts else ""
        ep_entity_type = parts[1] if len(parts) > 1 else None
        ep_entity_id = parts[2] if len(parts) > 2 else None

        if event_type:
            event_type_str = event_type.value if isinstance(event_type, EventType) else event_type
            if ep_event_type != event_type_str:
                continue

        if entity_type and data.get("entity_type") != entity_type:
            continue

        timestamp = data.get("timestamp", ep.get("timestamp"))
        if since and timestamp:
            try:
                ts = datetime.fromisoformat(timestamp.replace("Z", "+00:00"))
                if ts < since:
                    continue
            except (ValueError, TypeError):
                pass

        events.append({
            "id": ep.get("id"),
            "event_type": ep_event_type,
            "entity_type": data.get("entity_type") or ep_entity_type,
            "entity_id": data.get("entity_id") or ep_entity_id,
            "data": data.get("data"),
            "coherence": data.get("coherence_at_event"),
            "timestamp": timestamp,
            "session_id": data.get("session_id"),
        })

        if len(events) >= limit:
            break

    return events


def calculate_decay(
    last_used: str,
    base_confidence: float,
    decay_rate: float = None,
    floor: float = None,
) -> float:
    """
    Calculate decayed confidence based on time since last use.

    Formula: confidence * (1 - decay_rate)^months_inactive
    """
    if not last_used:
        return base_confidence

    decay_rate = decay_rate or TEMPORAL_CONFIG.wisdom_decay_rate
    floor = floor or TEMPORAL_CONFIG.decay_floor

    try:
        last = datetime.fromisoformat(last_used.replace("Z", "+00:00"))
        now = datetime.now(last.tzinfo) if last.tzinfo else datetime.now()
        days_inactive = (now - last).days
        months_inactive = days_inactive / 30.0

        decay_factor = (1 - decay_rate) ** months_inactive
        decayed = base_confidence * decay_factor

        return max(decayed, floor)
    except (ValueError, TypeError):
        return base_confidence


def strengthen(
    current_confidence: float,
    rate: float = None,
    ceiling: float = 1.0,
) -> float:
    """
    Strengthen confidence after successful use.

    Uses diminishing returns near ceiling.
    """
    rate = rate or TEMPORAL_CONFIG.wisdom_strengthen_rate

    # Diminishing returns: harder to improve when already high
    room_to_grow = ceiling - current_confidence
    boost = room_to_grow * rate

    return min(current_confidence + boost, ceiling)


def is_stale(last_confirmed: str, threshold_days: int = None) -> bool:
    """Check if something is stale (not confirmed recently)."""
    if not last_confirmed:
        return True

    threshold = threshold_days or TEMPORAL_CONFIG.stale_threshold_days

    try:
        last = datetime.fromisoformat(last_confirmed.replace("Z", "+00:00"))
        now = datetime.now(last.tzinfo) if last.tzinfo else datetime.now()
        days_since = (now - last).days
        return days_since > threshold
    except (ValueError, TypeError):
        return True


def days_since(timestamp: str) -> int:
    """Calculate days since a timestamp."""
    if not timestamp:
        return 999

    try:
        ts = datetime.fromisoformat(timestamp.replace("Z", "+00:00"))
        now = datetime.now(ts.tzinfo) if ts.tzinfo else datetime.now()
        return (now - ts).days
    except (ValueError, TypeError):
        return 999


# ============================================================
# Identity Decay
# ============================================================

def decay_identity_confidence():
    """
    Apply decay to identity observations that haven't been confirmed recently.

    Returns list of stale identity aspects.
    """
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="identity", limit=100)
    stale = []

    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        confidence = data.get("confidence", 0.7)
        last_confirmed = data.get("last_confirmed", ep.get("timestamp"))

        if confidence <= TEMPORAL_CONFIG.decay_floor:
            continue

        if is_stale(last_confirmed):
            new_confidence = calculate_decay(
                last_confirmed,
                confidence,
                TEMPORAL_CONFIG.identity_decay_rate,
            )

            if new_confidence < confidence:
                aspect = data.get("aspect", "unknown")
                key = data.get("key", "unknown")

                stale.append({
                    "aspect": aspect,
                    "key": key,
                    "old_confidence": confidence,
                    "new_confidence": new_confidence,
                    "days_stale": days_since(last_confirmed),
                })

    for s in stale:
        log_event(
            EventType.IDENTITY_STALE,
            entity_type="identity",
            entity_id=f"{s['aspect']}:{s['key']}",
            data={
                "old_confidence": s["old_confidence"],
                "new_confidence": s["new_confidence"],
            },
        )

    return stale


def confirm_identity(aspect: str, key: str):
    """Confirm an identity observation, strengthening it."""
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="identity", limit=100)

    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        if data.get("aspect") == aspect and data.get("key") == key:
            old_confidence = data.get("confidence", 0.7)
            new_confidence = strengthen(old_confidence, TEMPORAL_CONFIG.identity_confirm_rate)

            graph.observe(
                category="identity",
                title=f"{aspect}:{key}",
                content=json.dumps({
                    "aspect": aspect,
                    "key": key,
                    "value": data.get("value"),
                    "confidence": new_confidence,
                    "last_confirmed": datetime.now().isoformat(),
                    "observation_count": data.get("observation_count", 0) + 1,
                }),
                tags=["identity", aspect, key],
            )

            save_synapse()

            log_event(
                EventType.IDENTITY_CONFIRMED,
                entity_type="identity",
                entity_id=f"{aspect}:{key}",
                data={"old_confidence": old_confidence, "new_confidence": new_confidence},
            )

            return new_confidence

    return None


# ============================================================
# Belief Revision
# ============================================================

def revise_belief(
    belief_id: str,
    reason: str,
    evidence: str = None,
    new_content: str = None,
    confidence_delta: float = -0.1,
):
    """
    Revise a belief based on new evidence.

    Can update content and/or adjust confidence.
    Tracks the revision in history.
    """
    graph = get_synapse_graph()

    beliefs = graph.get_all_beliefs()
    belief = None
    for b in beliefs:
        if b.get("id") == belief_id:
            belief = b
            break

    if not belief:
        return None

    old_content = belief.get("statement", "")
    old_confidence = belief.get("strength", 0.7)

    new_confidence = max(
        TEMPORAL_CONFIG.decay_floor,
        old_confidence + confidence_delta
    )

    graph.observe(
        category="belief_revision",
        title=f"Revision:{belief_id}",
        content=json.dumps({
            "belief_id": belief_id,
            "old_content": old_content,
            "new_content": new_content or old_content,
            "old_confidence": old_confidence,
            "new_confidence": new_confidence,
            "reason": reason,
            "evidence": evidence,
            "timestamp": datetime.now().isoformat(),
        }),
        tags=["belief_revision", f"belief:{belief_id}"],
    )

    if new_content:
        graph.add_belief(new_content, new_confidence)

    save_synapse()

    log_event(
        EventType.BELIEF_REVISED,
        entity_type="belief",
        entity_id=belief_id,
        data={
            "reason": reason,
            "old_confidence": old_confidence,
            "new_confidence": new_confidence,
            "content_changed": new_content is not None,
        },
    )

    return {
        "belief_id": belief_id,
        "old_confidence": old_confidence,
        "new_confidence": new_confidence,
        "revised": True,
    }


def get_belief_history(belief_id: str) -> list:
    """Get revision history for a belief."""
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="belief_revision", limit=100)

    history = []
    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        if data.get("belief_id") == belief_id:
            history.append({
                "old_content": data.get("old_content"),
                "new_content": data.get("new_content"),
                "old_confidence": data.get("old_confidence"),
                "new_confidence": data.get("new_confidence"),
                "reason": data.get("reason"),
                "timestamp": data.get("timestamp"),
            })

    return sorted(history, key=lambda x: x.get("timestamp", ""), reverse=True)


# ============================================================
# Proactive Surfacing
# ============================================================

def queue_proactive(
    entity_type: str,
    entity_id: str,
    reason: str,
    priority: float = 0.5,
):
    """Add something to the proactive queue for later surfacing."""
    graph = get_synapse_graph()

    graph.observe(
        category="proactive_queue",
        title=f"{entity_type}:{entity_id}",
        content=json.dumps({
            "entity_type": entity_type,
            "entity_id": entity_id,
            "reason": reason,
            "priority": priority,
            "created_at": datetime.now().isoformat(),
            "surfaced_at": None,
            "dismissed": False,
        }),
        tags=["proactive_queue", f"entity:{entity_type}", f"id:{entity_id}"],
    )

    save_synapse()


def get_proactive_items(limit: int = 5) -> list:
    """Get items from the proactive queue, highest priority first."""
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="proactive_queue", limit=limit * 3)

    items = []
    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        if data.get("dismissed") or data.get("surfaced_at"):
            continue

        items.append({
            "entity_type": data.get("entity_type"),
            "entity_id": data.get("entity_id"),
            "reason": data.get("reason"),
            "priority": data.get("priority", 0.5),
            "created_at": data.get("created_at"),
        })

    items.sort(key=lambda x: x.get("priority", 0), reverse=True)
    return items[:limit]


def mark_surfaced(entity_type: str, entity_id: str):
    """Mark an item as surfaced."""
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="proactive_queue", limit=100)

    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        if data.get("entity_type") == entity_type and data.get("entity_id") == entity_id:
            data["surfaced_at"] = datetime.now().isoformat()
            graph.observe(
                category="proactive_queue",
                title=f"{entity_type}:{entity_id}",
                content=json.dumps(data),
                tags=["proactive_queue", f"entity:{entity_type}", f"id:{entity_id}", "surfaced"],
            )
            save_synapse()
            return


def dismiss_proactive(entity_type: str, entity_id: str):
    """Dismiss an item from the proactive queue."""
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="proactive_queue", limit=100)

    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        if data.get("entity_type") == entity_type and data.get("entity_id") == entity_id:
            data["dismissed"] = True
            graph.observe(
                category="proactive_queue",
                title=f"{entity_type}:{entity_id}",
                content=json.dumps(data),
                tags=["proactive_queue", f"entity:{entity_type}", f"id:{entity_id}", "dismissed"],
            )
            save_synapse()
            return


def find_proactive_candidates() -> list:
    """
    Find things worth surfacing proactively.

    Looks for:
    - High-confidence wisdom not used in a while
    - Stale identity aspects that should be confirmed
    - Unfulfilled intentions that have been waiting
    - Wisdom relevant to current project not yet applied
    """
    graph = get_synapse_graph()
    candidates = []

    threshold_date = (datetime.now() - timedelta(days=TEMPORAL_CONFIG.proactive_threshold_days)).isoformat()

    wisdom_list = graph.get_all_wisdom()
    for w in wisdom_list:
        confidence = w.get("confidence", 0.5)
        last_used = w.get("last_used") or w.get("timestamp")

        if confidence > 0.6:
            if not last_used or last_used < threshold_date:
                if last_used:
                    days = days_since(last_used)
                    reason = f"unused {days}d ({confidence:.0%})"
                else:
                    reason = f"never applied ({confidence:.0%})"

                candidates.append({
                    "entity_type": "wisdom",
                    "entity_id": w.get("id"),
                    "title": w.get("title"),
                    "reason": reason,
                    "priority": confidence * 0.8,
                })

    candidates = candidates[:5]

    identity_episodes = graph.get_episodes(category="identity", limit=20)
    for ep in identity_episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        confidence = data.get("confidence", 0.5)
        last_confirmed = data.get("last_confirmed", ep.get("timestamp"))

        if confidence > 0.5 and is_stale(last_confirmed):
            days = days_since(last_confirmed)
            aspect = data.get("aspect", "unknown")
            key = data.get("key", "unknown")

            candidates.append({
                "entity_type": "identity",
                "entity_id": f"{aspect}:{key}",
                "title": f"{aspect}: {key}",
                "reason": f"Identity aspect not confirmed in {days} days",
                "priority": 0.6,
            })

    for c in candidates:
        queue_proactive(
            c["entity_type"],
            c["entity_id"],
            c["reason"],
            c["priority"],
        )

    return candidates


# ============================================================
# Cross-Project Patterns
# ============================================================

def record_cross_project_pattern(
    title: str,
    content: str,
    project: str,
) -> dict:
    """
    Record a pattern that might recur across projects.

    If similar pattern exists, increment count and add project.
    """
    graph = get_synapse_graph()

    pattern_hash = hashlib.sha256(content.lower().encode()).hexdigest()[:16]

    episodes = graph.get_episodes(category="cross_project_pattern", limit=100)

    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        if data.get("pattern_hash") == pattern_hash:
            projects = data.get("projects", [])
            if project not in projects:
                projects.append(project)

            count = data.get("occurrence_count", 1) + 1
            now = datetime.now().isoformat()

            graph.observe(
                category="cross_project_pattern",
                title=title,
                content=json.dumps({
                    "pattern_hash": pattern_hash,
                    "title": title,
                    "content": content,
                    "projects": projects,
                    "occurrence_count": count,
                    "first_seen": data.get("first_seen"),
                    "last_seen": now,
                    "promoted_to_wisdom": data.get("promoted_to_wisdom", False),
                    "wisdom_id": data.get("wisdom_id"),
                }),
                tags=["cross_project_pattern", f"hash:{pattern_hash}"],
            )

            save_synapse()

            return {
                "id": ep.get("id"),
                "is_new": False,
                "occurrence_count": count,
                "projects": projects,
            }

    now = datetime.now().isoformat()

    obs_id = graph.observe(
        category="cross_project_pattern",
        title=title,
        content=json.dumps({
            "pattern_hash": pattern_hash,
            "title": title,
            "content": content,
            "projects": [project],
            "occurrence_count": 1,
            "first_seen": now,
            "last_seen": now,
            "promoted_to_wisdom": False,
            "wisdom_id": None,
        }),
        tags=["cross_project_pattern", f"hash:{pattern_hash}"],
    )

    save_synapse()

    return {
        "id": obs_id,
        "is_new": True,
        "occurrence_count": 1,
        "projects": [project],
    }


def find_cross_project_wisdom(min_occurrences: int = 2) -> list:
    """
    Find patterns that have recurred across multiple projects.

    These are candidates for promotion to universal wisdom.
    """
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="cross_project_pattern", limit=100)

    results = []
    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        count = data.get("occurrence_count", 1)
        promoted = data.get("promoted_to_wisdom", False)

        if count >= min_occurrences and not promoted:
            results.append({
                "id": ep.get("id"),
                "title": data.get("title"),
                "content": data.get("content"),
                "projects": data.get("projects", []),
                "occurrence_count": count,
                "first_seen": data.get("first_seen"),
                "last_seen": data.get("last_seen"),
            })

    return sorted(results, key=lambda x: x.get("occurrence_count", 0), reverse=True)


def promote_pattern_to_wisdom(pattern_id: str) -> str:
    """Promote a cross-project pattern to universal wisdom."""
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="cross_project_pattern", limit=100)

    pattern_data = None
    for ep in episodes:
        if ep.get("id") == pattern_id:
            try:
                pattern_data = json.loads(ep.get("content", "{}"))
            except (json.JSONDecodeError, TypeError):
                return None
            break

    if not pattern_data:
        return None

    title = pattern_data.get("title")
    content = pattern_data.get("content")
    projects = pattern_data.get("projects", [])
    count = pattern_data.get("occurrence_count", 1)

    wisdom_id = graph.add_wisdom(
        title=title,
        content=f"{content}\n\n(Emerged from: {', '.join(projects)})",
        confidence=min(0.9, 0.5 + count * 0.1),
    )

    pattern_data["promoted_to_wisdom"] = True
    pattern_data["wisdom_id"] = wisdom_id

    graph.observe(
        category="cross_project_pattern",
        title=title,
        content=json.dumps(pattern_data),
        tags=["cross_project_pattern", f"hash:{pattern_data.get('pattern_hash')}", "promoted"],
    )

    save_synapse()

    log_event(
        EventType.WISDOM_GAINED,
        entity_type="cross_project",
        entity_id=wisdom_id,
        data={
            "source": "cross_project_promotion",
            "projects": projects,
            "occurrences": count,
        },
    )

    return wisdom_id


# ============================================================
# Temporal Statistics
# ============================================================

def update_daily_stats():
    """Update daily statistics bucket."""
    graph = get_synapse_graph()

    today = datetime.now().strftime("%Y-%m-%d")

    events = get_events(limit=200)
    today_events = [e for e in events if e.get("timestamp", "").startswith(today)]

    wisdom_applications = sum(1 for e in today_events if e.get("event_type") == "wisdom_applied")
    wisdom_successes = sum(1 for e in today_events if e.get("event_type") == "wisdom_confirmed")
    wisdom_failures = sum(1 for e in today_events if e.get("event_type") == "wisdom_challenged")
    intentions_set = sum(1 for e in today_events if e.get("event_type") == "intention_set")
    intentions_fulfilled = sum(1 for e in today_events if e.get("event_type") == "intention_fulfilled")

    coherences = [e.get("coherence") for e in today_events if e.get("coherence") is not None]
    avg_coherence = sum(coherences) / len(coherences) if coherences else None
    peak_coherence = max(coherences) if coherences else None

    graph.observe(
        category="temporal_stats",
        title=f"daily:{today}",
        content=json.dumps({
            "bucket_date": today,
            "bucket_type": "daily",
            "wisdom_applications": wisdom_applications,
            "wisdom_successes": wisdom_successes,
            "wisdom_failures": wisdom_failures,
            "intentions_set": intentions_set,
            "intentions_fulfilled": intentions_fulfilled,
            "avg_coherence": avg_coherence,
            "peak_coherence": peak_coherence,
            "events_count": len(today_events),
        }),
        tags=["temporal_stats", "daily", f"date:{today}"],
    )

    save_synapse()


def get_temporal_trends(days: int = 7) -> dict:
    """Get trends over the last N days."""
    graph = get_synapse_graph()

    since = (datetime.now() - timedelta(days=days)).strftime("%Y-%m-%d")

    episodes = graph.get_episodes(category="temporal_stats", limit=days * 2)

    rows = []
    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        bucket_date = data.get("bucket_date", "")
        if bucket_date >= since and data.get("bucket_type") == "daily":
            rows.append(data)

    if not rows:
        return {"trend": "insufficient_data"}

    coherences = [r.get("avg_coherence") for r in rows if r.get("avg_coherence") is not None]
    applications = sum(r.get("wisdom_applications", 0) for r in rows)
    successes = sum(r.get("wisdom_successes", 0) for r in rows)

    return {
        "days": days,
        "data_points": len(rows),
        "coherence_trend": _calculate_trend(coherences) if coherences else None,
        "avg_coherence": sum(coherences) / len(coherences) if coherences else None,
        "total_applications": applications,
        "success_rate": successes / applications if applications else None,
    }


def _calculate_trend(values: list) -> str:
    """Calculate if values are trending up, down, or stable."""
    if len(values) < 2:
        return "insufficient"

    first_half = sum(values[:len(values)//2]) / (len(values)//2)
    second_half = sum(values[len(values)//2:]) / (len(values) - len(values)//2)

    diff = second_half - first_half
    if diff > 0.05:
        return "improving"
    elif diff < -0.05:
        return "declining"
    else:
        return "stable"


# ============================================================
# Integration Helpers
# ============================================================

def run_temporal_maintenance():
    """
    Run all temporal maintenance tasks.

    Call this at session start for autonomous self-care.
    """
    results = {
        "identity_decayed": [],
        "proactive_queued": [],
        "cross_project": [],
        "stats_updated": False,
    }

    init_temporal_tables()

    results["identity_decayed"] = decay_identity_confidence()

    results["proactive_queued"] = find_proactive_candidates()

    try:
        update_daily_stats()
        results["stats_updated"] = True
    except Exception:
        pass

    return results


def get_temporal_context() -> str:
    """
    Get temporal context for injection.

    Returns a compact summary of temporal state.
    """
    lines = []

    proactive = get_proactive_items(limit=2)
    for p in proactive:
        title = p.get('title') or p.get('entity_id', 'item')
        lines.append(f"[temporal] {title}: {p['reason']}")

    trends = get_temporal_trends(days=7)
    if trends.get("coherence_trend"):
        if trends["coherence_trend"] == "improving":
            lines.append("[temporal] Coherence improving this week")
        elif trends["coherence_trend"] == "declining":
            lines.append("[temporal] Coherence declining - consider reflection")

    return "\n".join(lines) if lines else ""
