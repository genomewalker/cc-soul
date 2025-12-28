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

from dataclasses import dataclass, field
from datetime import datetime, timedelta
from enum import Enum
from typing import Optional
import json
import math

from .core import get_db_connection as get_db


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
    """Initialize temporal tables and indices."""
    db = get_db()
    cur = db.cursor()

    # Unified event log - chronological record of all soul events
    cur.execute("""
        CREATE TABLE IF NOT EXISTS soul_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            event_type TEXT NOT NULL,
            entity_type TEXT,
            entity_id TEXT,
            data TEXT,
            coherence_at_event REAL,
            timestamp TEXT NOT NULL,
            session_id INTEGER
        )
    """)

    # Temporal statistics - aggregated metrics by time bucket
    cur.execute("""
        CREATE TABLE IF NOT EXISTS temporal_stats (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            bucket_date TEXT NOT NULL,
            bucket_type TEXT DEFAULT 'daily',
            wisdom_applications INTEGER DEFAULT 0,
            wisdom_successes INTEGER DEFAULT 0,
            wisdom_failures INTEGER DEFAULT 0,
            intentions_set INTEGER DEFAULT 0,
            intentions_fulfilled INTEGER DEFAULT 0,
            avg_coherence REAL,
            peak_coherence REAL,
            events_count INTEGER DEFAULT 0,
            UNIQUE(bucket_date, bucket_type)
        )
    """)

    # Proactive queue - things to surface when opportunity arises
    cur.execute("""
        CREATE TABLE IF NOT EXISTS proactive_queue (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            entity_type TEXT NOT NULL,
            entity_id TEXT NOT NULL,
            reason TEXT NOT NULL,
            priority REAL DEFAULT 0.5,
            created_at TEXT NOT NULL,
            surfaced_at TEXT,
            dismissed BOOLEAN DEFAULT FALSE,
            UNIQUE(entity_type, entity_id)
        )
    """)

    # Belief revision history - track how beliefs evolve
    cur.execute("""
        CREATE TABLE IF NOT EXISTS belief_revisions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            belief_id TEXT NOT NULL,
            old_content TEXT,
            new_content TEXT,
            old_confidence REAL,
            new_confidence REAL,
            reason TEXT,
            evidence TEXT,
            timestamp TEXT NOT NULL
        )
    """)

    # Cross-project patterns - wisdom that recurs across projects
    cur.execute("""
        CREATE TABLE IF NOT EXISTS cross_project_patterns (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            pattern_hash TEXT NOT NULL UNIQUE,
            title TEXT NOT NULL,
            content TEXT NOT NULL,
            projects TEXT NOT NULL,
            occurrence_count INTEGER DEFAULT 1,
            first_seen TEXT NOT NULL,
            last_seen TEXT NOT NULL,
            promoted_to_wisdom BOOLEAN DEFAULT FALSE,
            wisdom_id TEXT
        )
    """)

    # Add temporal indices
    _add_temporal_indices(cur)

    db.commit()


def _add_temporal_indices(cur):
    """Add indices for efficient temporal queries."""
    indices = [
        ("idx_events_timestamp", "soul_events", "timestamp"),
        ("idx_events_type", "soul_events", "event_type"),
        ("idx_events_entity", "soul_events", "entity_type, entity_id"),
        ("idx_wisdom_last_used", "wisdom", "last_used"),
        ("idx_wisdom_timestamp", "wisdom", "timestamp"),
        ("idx_identity_last_confirmed", "identity", "last_confirmed"),
        ("idx_conversations_started", "conversations", "started_at"),
        ("idx_proactive_priority", "proactive_queue", "priority DESC"),
        ("idx_stats_bucket", "temporal_stats", "bucket_date, bucket_type"),
    ]

    for idx_name, table, columns in indices:
        try:
            cur.execute(f"CREATE INDEX IF NOT EXISTS {idx_name} ON {table}({columns})")
        except Exception:
            pass  # Table might not exist yet


def log_event(
    event_type: EventType,
    entity_type: str = None,
    entity_id: str = None,
    data: dict = None,
    coherence: float = None,
    session_id: int = None,
) -> int:
    """
    Log an event to the unified timeline.

    Returns the event ID.
    """
    db = get_db()
    cur = db.cursor()

    cur.execute("""
        INSERT INTO soul_events
        (event_type, entity_type, entity_id, data, coherence_at_event, timestamp, session_id)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    """, (
        event_type.value if isinstance(event_type, EventType) else event_type,
        entity_type,
        entity_id,
        json.dumps(data) if data else None,
        coherence,
        datetime.now().isoformat(),
        session_id,
    ))

    db.commit()
    return cur.lastrowid


def get_events(
    event_type: EventType = None,
    entity_type: str = None,
    since: datetime = None,
    limit: int = 50,
) -> list:
    """Query events from the timeline."""
    db = get_db()
    cur = db.cursor()

    query = "SELECT * FROM soul_events WHERE 1=1"
    params = []

    if event_type:
        query += " AND event_type = ?"
        params.append(event_type.value if isinstance(event_type, EventType) else event_type)

    if entity_type:
        query += " AND entity_type = ?"
        params.append(entity_type)

    if since:
        query += " AND timestamp >= ?"
        params.append(since.isoformat())

    query += " ORDER BY timestamp DESC LIMIT ?"
    params.append(limit)

    cur.execute(query, params)
    rows = cur.fetchall()

    return [
        {
            "id": r[0],
            "event_type": r[1],
            "entity_type": r[2],
            "entity_id": r[3],
            "data": json.loads(r[4]) if r[4] else None,
            "coherence": r[5],
            "timestamp": r[6],
            "session_id": r[7],
        }
        for r in rows
    ]


def calculate_decay(
    last_used: str,
    base_confidence: float,
    decay_rate: float = None,
    floor: float = None,
) -> float:
    """
    Calculate decayed confidence based on time since last use.

    Formula: confidence × (1 - decay_rate)^months_inactive
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
    db = get_db()
    cur = db.cursor()

    # Find identity aspects that need decay
    cur.execute("""
        SELECT id, aspect, key, value, confidence, last_confirmed
        FROM identity
        WHERE confidence > ?
    """, (TEMPORAL_CONFIG.decay_floor,))

    rows = cur.fetchall()
    stale = []

    for row in rows:
        id_, aspect, key, value, confidence, last_confirmed = row

        if is_stale(last_confirmed):
            # Apply decay
            new_confidence = calculate_decay(
                last_confirmed,
                confidence,
                TEMPORAL_CONFIG.identity_decay_rate,
            )

            if new_confidence < confidence:
                cur.execute("""
                    UPDATE identity SET confidence = ? WHERE id = ?
                """, (new_confidence, id_))

                stale.append({
                    "aspect": aspect,
                    "key": key,
                    "old_confidence": confidence,
                    "new_confidence": new_confidence,
                    "days_stale": days_since(last_confirmed),
                })

    db.commit()

    # Log decay events after committing (avoids lock)
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
    db = get_db()
    cur = db.cursor()

    cur.execute("""
        SELECT id, confidence, observation_count
        FROM identity
        WHERE aspect = ? AND key = ?
    """, (aspect, key))

    row = cur.fetchone()
    if not row:
        return None

    id_, confidence, count = row
    new_confidence = strengthen(confidence, TEMPORAL_CONFIG.identity_confirm_rate)

    cur.execute("""
        UPDATE identity
        SET confidence = ?,
            last_confirmed = ?,
            observation_count = observation_count + 1
        WHERE id = ?
    """, (new_confidence, datetime.now().isoformat(), id_))

    db.commit()

    log_event(
        EventType.IDENTITY_CONFIRMED,
        entity_type="identity",
        entity_id=f"{aspect}:{key}",
        data={"old_confidence": confidence, "new_confidence": new_confidence},
    )

    return new_confidence


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
    from .wisdom import get_wisdom_by_id

    db = get_db()
    cur = db.cursor()

    # Get current belief (beliefs are wisdom with type='principle' or from beliefs table)
    wisdom = get_wisdom_by_id(belief_id)
    if not wisdom:
        # Try legacy beliefs table
        cur.execute("SELECT id, belief, strength FROM beliefs WHERE id = ?", (belief_id,))
        row = cur.fetchone()
        if not row:
            return None
        old_content = row[1]
        old_confidence = row[2]
        is_legacy = True
    else:
        old_content = wisdom.get("content", "")
        old_confidence = wisdom.get("confidence", 0.7)
        is_legacy = False

    # Calculate new confidence
    new_confidence = max(
        TEMPORAL_CONFIG.decay_floor,
        old_confidence + confidence_delta
    )

    # Record revision
    cur.execute("""
        INSERT INTO belief_revisions
        (belief_id, old_content, new_content, old_confidence, new_confidence, reason, evidence, timestamp)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    """, (
        belief_id,
        old_content,
        new_content or old_content,
        old_confidence,
        new_confidence,
        reason,
        evidence,
        datetime.now().isoformat(),
    ))

    # Update the belief
    if is_legacy:
        if new_content:
            cur.execute("""
                UPDATE beliefs SET belief = ?, strength = ?, challenged_count = challenged_count + 1
                WHERE id = ?
            """, (new_content, new_confidence, belief_id))
        else:
            cur.execute("""
                UPDATE beliefs SET strength = ?, challenged_count = challenged_count + 1
                WHERE id = ?
            """, (new_confidence, belief_id))
    else:
        if new_content:
            cur.execute("""
                UPDATE wisdom SET content = ?, confidence = ?, failure_count = failure_count + 1
                WHERE id = ?
            """, (new_content, new_confidence, belief_id))
        else:
            cur.execute("""
                UPDATE wisdom SET confidence = ?, failure_count = failure_count + 1
                WHERE id = ?
            """, (new_confidence, belief_id))

    db.commit()

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
    db = get_db()
    cur = db.cursor()

    cur.execute("""
        SELECT old_content, new_content, old_confidence, new_confidence, reason, timestamp
        FROM belief_revisions
        WHERE belief_id = ?
        ORDER BY timestamp DESC
    """, (belief_id,))

    return [
        {
            "old_content": r[0],
            "new_content": r[1],
            "old_confidence": r[2],
            "new_confidence": r[3],
            "reason": r[4],
            "timestamp": r[5],
        }
        for r in cur.fetchall()
    ]


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
    db = get_db()
    cur = db.cursor()

    cur.execute("""
        INSERT OR REPLACE INTO proactive_queue
        (entity_type, entity_id, reason, priority, created_at, dismissed)
        VALUES (?, ?, ?, ?, ?, FALSE)
    """, (entity_type, entity_id, reason, priority, datetime.now().isoformat()))

    db.commit()


def get_proactive_items(limit: int = 5) -> list:
    """Get items from the proactive queue, highest priority first."""
    db = get_db()
    cur = db.cursor()

    cur.execute("""
        SELECT entity_type, entity_id, reason, priority, created_at
        FROM proactive_queue
        WHERE dismissed = FALSE AND surfaced_at IS NULL
        ORDER BY priority DESC
        LIMIT ?
    """, (limit,))

    return [
        {
            "entity_type": r[0],
            "entity_id": r[1],
            "reason": r[2],
            "priority": r[3],
            "created_at": r[4],
        }
        for r in cur.fetchall()
    ]


def mark_surfaced(entity_type: str, entity_id: str):
    """Mark an item as surfaced."""
    db = get_db()
    cur = db.cursor()

    cur.execute("""
        UPDATE proactive_queue
        SET surfaced_at = ?
        WHERE entity_type = ? AND entity_id = ?
    """, (datetime.now().isoformat(), entity_type, entity_id))

    db.commit()


def dismiss_proactive(entity_type: str, entity_id: str):
    """Dismiss an item from the proactive queue."""
    db = get_db()
    cur = db.cursor()

    cur.execute("""
        UPDATE proactive_queue
        SET dismissed = TRUE
        WHERE entity_type = ? AND entity_id = ?
    """, (entity_type, entity_id))

    db.commit()


def find_proactive_candidates() -> list:
    """
    Find things worth surfacing proactively.

    Looks for:
    - High-confidence wisdom not used in a while
    - Stale identity aspects that should be confirmed
    - Unfulfilled intentions that have been waiting
    - Wisdom relevant to current project not yet applied
    """
    db = get_db()
    cur = db.cursor()
    candidates = []

    # Unused high-confidence wisdom
    threshold = (datetime.now() - timedelta(days=TEMPORAL_CONFIG.proactive_threshold_days)).isoformat()
    cur.execute("""
        SELECT id, title, confidence, last_used
        FROM wisdom
        WHERE confidence > 0.6
        AND (last_used IS NULL OR last_used < ?)
        ORDER BY confidence DESC
        LIMIT 5
    """, (threshold,))

    for r in cur.fetchall():
        days = days_since(r[3]) if r[3] else 999
        candidates.append({
            "entity_type": "wisdom",
            "entity_id": r[0],
            "title": r[1],
            "reason": f"High-confidence ({r[2]:.0%}) wisdom unused for {days} days",
            "priority": r[2] * 0.8,  # Scale by confidence
        })

    # Stale identity aspects
    cur.execute("""
        SELECT aspect, key, value, confidence, last_confirmed
        FROM identity
        WHERE confidence > 0.5
        ORDER BY last_confirmed ASC
        LIMIT 5
    """)

    for r in cur.fetchall():
        if is_stale(r[4]):
            days = days_since(r[4])
            candidates.append({
                "entity_type": "identity",
                "entity_id": f"{r[0]}:{r[1]}",
                "title": f"{r[0]}: {r[1]}",
                "reason": f"Identity aspect not confirmed in {days} days",
                "priority": 0.6,
            })

    # Queue the candidates
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
    import hashlib

    db = get_db()
    cur = db.cursor()

    # Create a content hash for similarity
    pattern_hash = hashlib.sha256(content.lower().encode()).hexdigest()[:16]

    cur.execute("""
        SELECT id, projects, occurrence_count
        FROM cross_project_patterns
        WHERE pattern_hash = ?
    """, (pattern_hash,))

    row = cur.fetchone()
    now = datetime.now().isoformat()

    if row:
        # Update existing pattern
        id_, projects_json, count = row
        projects = json.loads(projects_json)
        if project not in projects:
            projects.append(project)

        cur.execute("""
            UPDATE cross_project_patterns
            SET projects = ?, occurrence_count = ?, last_seen = ?
            WHERE id = ?
        """, (json.dumps(projects), count + 1, now, id_))

        db.commit()
        return {
            "id": id_,
            "is_new": False,
            "occurrence_count": count + 1,
            "projects": projects,
        }
    else:
        # New pattern
        cur.execute("""
            INSERT INTO cross_project_patterns
            (pattern_hash, title, content, projects, first_seen, last_seen)
            VALUES (?, ?, ?, ?, ?, ?)
        """, (pattern_hash, title, content, json.dumps([project]), now, now))

        db.commit()
        return {
            "id": cur.lastrowid,
            "is_new": True,
            "occurrence_count": 1,
            "projects": [project],
        }


def find_cross_project_wisdom(min_occurrences: int = 2) -> list:
    """
    Find patterns that have recurred across multiple projects.

    These are candidates for promotion to universal wisdom.
    """
    db = get_db()
    cur = db.cursor()

    cur.execute("""
        SELECT id, title, content, projects, occurrence_count, first_seen, last_seen
        FROM cross_project_patterns
        WHERE occurrence_count >= ?
        AND promoted_to_wisdom = FALSE
        ORDER BY occurrence_count DESC
    """, (min_occurrences,))

    return [
        {
            "id": r[0],
            "title": r[1],
            "content": r[2],
            "projects": json.loads(r[3]),
            "occurrence_count": r[4],
            "first_seen": r[5],
            "last_seen": r[6],
        }
        for r in cur.fetchall()
    ]


def promote_pattern_to_wisdom(pattern_id: int) -> str:
    """Promote a cross-project pattern to universal wisdom."""
    from .wisdom import gain_wisdom, WisdomType

    db = get_db()
    cur = db.cursor()

    cur.execute("""
        SELECT title, content, projects, occurrence_count
        FROM cross_project_patterns
        WHERE id = ?
    """, (pattern_id,))

    row = cur.fetchone()
    if not row:
        return None

    title, content, projects_json, count = row
    projects = json.loads(projects_json)

    # Add with source attribution
    wisdom_id = gain_wisdom(
        type=WisdomType.PATTERN,
        title=title,
        content=f"{content}\n\n(Emerged from: {', '.join(projects)})",
        confidence=min(0.9, 0.5 + count * 0.1),  # Higher confidence with more occurrences
    )

    # Mark as promoted
    cur.execute("""
        UPDATE cross_project_patterns
        SET promoted_to_wisdom = TRUE, wisdom_id = ?
        WHERE id = ?
    """, (wisdom_id, pattern_id))

    db.commit()

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
    db = get_db()
    cur = db.cursor()

    today = datetime.now().strftime("%Y-%m-%d")

    # Count today's events
    cur.execute("""
        SELECT
            COUNT(*) FILTER (WHERE event_type = 'wisdom_applied'),
            COUNT(*) FILTER (WHERE event_type = 'wisdom_confirmed'),
            COUNT(*) FILTER (WHERE event_type = 'wisdom_challenged'),
            COUNT(*) FILTER (WHERE event_type = 'intention_set'),
            COUNT(*) FILTER (WHERE event_type = 'intention_fulfilled'),
            AVG(coherence_at_event),
            MAX(coherence_at_event),
            COUNT(*)
        FROM soul_events
        WHERE timestamp >= ?
    """, (today,))

    row = cur.fetchone()
    if row:
        cur.execute("""
            INSERT OR REPLACE INTO temporal_stats
            (bucket_date, bucket_type, wisdom_applications, wisdom_successes,
             wisdom_failures, intentions_set, intentions_fulfilled,
             avg_coherence, peak_coherence, events_count)
            VALUES (?, 'daily', ?, ?, ?, ?, ?, ?, ?, ?)
        """, (today, row[0] or 0, row[1] or 0, row[2] or 0,
              row[3] or 0, row[4] or 0, row[5], row[6], row[7] or 0))

        db.commit()


def get_temporal_trends(days: int = 7) -> dict:
    """Get trends over the last N days."""
    db = get_db()
    cur = db.cursor()

    since = (datetime.now() - timedelta(days=days)).strftime("%Y-%m-%d")

    cur.execute("""
        SELECT bucket_date, avg_coherence, wisdom_applications,
               wisdom_successes, wisdom_failures, events_count
        FROM temporal_stats
        WHERE bucket_date >= ? AND bucket_type = 'daily'
        ORDER BY bucket_date
    """, (since,))

    rows = cur.fetchall()
    if not rows:
        return {"trend": "insufficient_data"}

    coherences = [r[1] for r in rows if r[1] is not None]
    applications = sum(r[2] or 0 for r in rows)
    successes = sum(r[3] or 0 for r in rows)

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

    # Initialize tables if needed
    init_temporal_tables()

    # Decay stale identity aspects
    results["identity_decayed"] = decay_identity_confidence()

    # Find things to surface proactively
    results["proactive_queued"] = find_proactive_candidates()

    # Update daily stats
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

    # Proactive items
    proactive = get_proactive_items(limit=2)
    for p in proactive:
        lines.append(f"⏰ {p['reason']}")

    # Trends
    trends = get_temporal_trends(days=7)
    if trends.get("coherence_trend"):
        if trends["coherence_trend"] == "improving":
            lines.append("📈 Coherence improving this week")
        elif trends["coherence_trend"] == "declining":
            lines.append("📉 Coherence declining - consider reflection")

    return "\n".join(lines) if lines else ""
