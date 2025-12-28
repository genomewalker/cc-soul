"""
Introspection - The soul examining itself.

This module enables meta-cognition: the soul can analyze its own
codebase, usage patterns, and failures to identify improvements.

Uses Claude's reasoning capabilities to:
1. Read and understand its own source code
2. Analyze wisdom application patterns
3. Identify inconsistencies and pain points
4. Generate improvement hypotheses
"""

import json
import subprocess
from datetime import datetime, timedelta
from pathlib import Path
from typing import List, Dict, Optional, Tuple
from collections import Counter

from .core import get_db_connection, SOUL_DIR

# Where introspection data lives
INTROSPECTION_DIR = SOUL_DIR / "introspection"
METRICS_LOG = INTROSPECTION_DIR / "metrics.jsonl"
PAIN_POINTS_LOG = INTROSPECTION_DIR / "pain_points.jsonl"

# The soul's own source code location
SOUL_PACKAGE = Path(__file__).parent


def _ensure_dirs():
    """Ensure introspection directories exist."""
    INTROSPECTION_DIR.mkdir(parents=True, exist_ok=True)


# =============================================================================
# SOURCE CODE ANALYSIS
# =============================================================================


def read_soul_source() -> Dict[str, str]:
    """
    Read all Python source files in the soul package.

    Returns dict mapping filename to content.
    """
    sources = {}
    for py_file in SOUL_PACKAGE.glob("*.py"):
        if py_file.name.startswith("_"):
            continue
        sources[py_file.name] = py_file.read_text()
    return sources


def get_source_stats() -> Dict:
    """Get statistics about the soul's codebase."""
    sources = read_soul_source()

    total_lines = 0
    total_functions = 0
    total_classes = 0

    for name, content in sources.items():
        lines = content.split("\n")
        total_lines += len(lines)
        total_functions += content.count("\ndef ")
        total_classes += content.count("\nclass ")

    return {
        "file_count": len(sources),
        "total_lines": total_lines,
        "total_functions": total_functions,
        "total_classes": total_classes,
        "files": list(sources.keys()),
    }


def find_todos_and_fixmes() -> List[Dict]:
    """Find TODO and FIXME comments in source."""
    sources = read_soul_source()
    issues = []

    for filename, content in sources.items():
        for i, line in enumerate(content.split("\n"), 1):
            for marker in ["TODO", "FIXME", "XXX", "HACK"]:
                if marker in line:
                    issues.append(
                        {
                            "file": filename,
                            "line": i,
                            "type": marker,
                            "content": line.strip(),
                        }
                    )

    return issues


# =============================================================================
# USAGE PATTERN ANALYSIS
# =============================================================================


def analyze_wisdom_applications(days: int = 30) -> Dict:
    """
    Analyze wisdom application patterns over time.

    Returns insights about:
    - Most applied wisdom
    - Success/failure rates
    - Unused wisdom
    - Application contexts
    """
    conn = get_db_connection()
    c = conn.cursor()

    cutoff = (datetime.now() - timedelta(days=days)).isoformat()

    # Get all applications in period
    c.execute(
        """
        SELECT wa.wisdom_id, w.title, w.type, wa.outcome, wa.context
        FROM wisdom_applications wa
        JOIN wisdom w ON wa.wisdom_id = w.id
        WHERE wa.applied_at > ?
    """,
        (cutoff,),
    )

    applications = c.fetchall()

    # Get all wisdom for comparison
    c.execute("SELECT id, title, type, confidence FROM wisdom")
    all_wisdom = {
        row[0]: {"title": row[1], "type": row[2], "confidence": row[3]}
        for row in c.fetchall()
    }

    conn.close()

    # Analyze patterns
    applied_ids = set()
    outcomes = Counter()
    by_wisdom = {}

    for wisdom_id, title, wtype, outcome, context in applications:
        applied_ids.add(wisdom_id)
        outcomes[outcome or "pending"] += 1

        if wisdom_id not in by_wisdom:
            by_wisdom[wisdom_id] = {
                "title": title,
                "type": wtype,
                "applications": 0,
                "successes": 0,
                "failures": 0,
            }

        by_wisdom[wisdom_id]["applications"] += 1
        if outcome == "success":
            by_wisdom[wisdom_id]["successes"] += 1
        elif outcome == "failure":
            by_wisdom[wisdom_id]["failures"] += 1

    # Find unused wisdom
    unused = [
        {"id": wid, **info}
        for wid, info in all_wisdom.items()
        if wid not in applied_ids
    ]

    # Find failing wisdom (>50% failure rate with >2 applications)
    failing = [
        {"id": wid, **info, "failure_rate": info["failures"] / info["applications"]}
        for wid, info in by_wisdom.items()
        if info["applications"] >= 2 and info["failures"] / info["applications"] > 0.5
    ]

    return {
        "period_days": days,
        "total_applications": len(applications),
        "outcomes": dict(outcomes),
        "unique_wisdom_applied": len(applied_ids),
        "total_wisdom": len(all_wisdom),
        "unused_wisdom": unused,
        "failing_wisdom": failing,
        "most_applied": sorted(
            by_wisdom.items(), key=lambda x: x[1]["applications"], reverse=True
        )[:5],
    }


def get_wisdom_timeline(days: int = 30, bucket: str = "day") -> List[Dict]:
    """
    Get wisdom applications bucketed by time period.

    Args:
        days: Number of days to look back
        bucket: "day", "week", or "month"

    Returns:
        List of {period, applications, successes, failures}
    """
    conn = get_db_connection()
    c = conn.cursor()

    cutoff = (datetime.now() - timedelta(days=days)).isoformat()

    c.execute(
        """
        SELECT applied_at, outcome
        FROM wisdom_applications
        WHERE applied_at > ?
        ORDER BY applied_at
    """,
        (cutoff,),
    )

    applications = c.fetchall()
    conn.close()

    buckets = {}
    for applied_at, outcome in applications:
        try:
            dt = datetime.fromisoformat(applied_at)
            if bucket == "day":
                key = dt.strftime("%Y-%m-%d")
            elif bucket == "week":
                key = f"{dt.year}-W{dt.isocalendar()[1]:02d}"
            else:
                key = dt.strftime("%Y-%m")

            if key not in buckets:
                buckets[key] = {
                    "period": key,
                    "applications": 0,
                    "successes": 0,
                    "failures": 0,
                }

            buckets[key]["applications"] += 1
            if outcome == "success":
                buckets[key]["successes"] += 1
            elif outcome == "failure":
                buckets[key]["failures"] += 1
        except ValueError:
            continue

    return sorted(buckets.values(), key=lambda x: x["period"])


def get_wisdom_health() -> Dict:
    """
    Analyze the health of the wisdom collection.

    Returns insights on:
    - Decay: Wisdom not used recently (losing confidence)
    - Staleness: Wisdom created long ago, never applied
    - Success rates: Which wisdom consistently works
    - Coverage: What types/domains are represented
    """
    conn = get_db_connection()
    c = conn.cursor()

    c.execute("""
        SELECT id, type, title, content, domain, confidence, timestamp, last_used,
               success_count, failure_count
        FROM wisdom
    """)

    wisdom_list = []
    now = datetime.now()

    for row in c.fetchall():
        (
            wid,
            wtype,
            title,
            content,
            domain,
            confidence,
            timestamp,
            last_used,
            successes,
            failures,
        ) = row

        created = datetime.fromisoformat(timestamp) if timestamp else now
        age_days = (now - created).days

        if last_used:
            last = datetime.fromisoformat(last_used)
            inactive_days = (now - last).days
        else:
            inactive_days = age_days

        # Calculate effective confidence with decay
        months_inactive = inactive_days / 30.0
        decay_factor = 0.95**months_inactive
        effective_conf = confidence * decay_factor

        total_apps = successes + failures
        success_rate = successes / total_apps if total_apps > 0 else None

        wisdom_list.append(
            {
                "id": wid,
                "type": wtype,
                "title": title,
                "domain": domain,
                "confidence": confidence,
                "effective_confidence": effective_conf,
                "decay_factor": decay_factor,
                "age_days": age_days,
                "inactive_days": inactive_days,
                "total_applications": total_apps,
                "success_rate": success_rate,
            }
        )

    conn.close()

    # Categorize wisdom
    decaying = [w for w in wisdom_list if w["decay_factor"] < 0.8]
    stale = [
        w for w in wisdom_list if w["total_applications"] == 0 and w["age_days"] > 7
    ]
    healthy = [
        w
        for w in wisdom_list
        if w["decay_factor"] >= 0.9 and w["total_applications"] > 0
    ]
    failing = [
        w
        for w in wisdom_list
        if w["success_rate"] is not None
        and w["success_rate"] < 0.5
        and w["total_applications"] >= 2
    ]

    # Coverage by type and domain
    by_type = Counter(w["type"] for w in wisdom_list)
    by_domain = Counter(w["domain"] or "general" for w in wisdom_list)

    return {
        "total_wisdom": len(wisdom_list),
        "healthy_count": len(healthy),
        "decaying_count": len(decaying),
        "stale_count": len(stale),
        "failing_count": len(failing),
        "by_type": dict(by_type),
        "by_domain": dict(by_domain),
        "decaying": sorted(decaying, key=lambda x: x["decay_factor"])[:10],
        "stale": sorted(stale, key=lambda x: -x["age_days"])[:10],
        "failing": sorted(failing, key=lambda x: x["success_rate"])[:5],
        "top_performers": sorted(
            [
                w
                for w in wisdom_list
                if w["success_rate"] is not None and w["total_applications"] >= 2
            ],
            key=lambda x: (-x["success_rate"], -x["total_applications"]),
        )[:5],
    }


def format_wisdom_stats(health: Dict, timeline: List[Dict] = None) -> str:
    """Format wisdom statistics for CLI display."""
    lines = []
    lines.append("=" * 60)
    lines.append("WISDOM ANALYTICS")
    lines.append("=" * 60)

    # Overview
    lines.append(f"\n## Overview")
    lines.append(f"  Total wisdom: {health['total_wisdom']}")
    lines.append(f"  Healthy: {health['healthy_count']} (active, not decaying)")
    lines.append(f"  Decaying: {health['decaying_count']} (>20% confidence loss)")
    lines.append(f"  Stale: {health['stale_count']} (never applied, >7 days old)")
    lines.append(f"  Failing: {health['failing_count']} (>50% failure rate)")

    # Coverage
    lines.append(f"\n## Coverage")
    lines.append(f"  By type: {health['by_type']}")
    lines.append(f"  By domain: {health['by_domain']}")

    # Timeline
    if timeline:
        lines.append(f"\n## Application Timeline")
        for bucket in timeline[-7:]:
            success_rate = (
                bucket["successes"] / bucket["applications"] * 100
                if bucket["applications"]
                else 0
            )
            bar = "█" * min(20, bucket["applications"])
            lines.append(
                f"  {bucket['period']}: {bar} {bucket['applications']} ({success_rate:.0f}% success)"
            )

    # Top performers
    if health.get("top_performers"):
        lines.append(f"\n## Top Performers")
        for w in health["top_performers"]:
            lines.append(
                f"  ✓ {w['title'][:40]} ({w['success_rate']:.0%}, {w['total_applications']} uses)"
            )

    # Issues
    if health.get("decaying"):
        lines.append(f"\n## Decaying (needs reinforcement)")
        for w in health["decaying"][:3]:
            lines.append(
                f"  ↓ {w['title'][:40]} (conf: {w['effective_confidence']:.0%}, inactive {w['inactive_days']}d)"
            )

    if health.get("failing"):
        lines.append(f"\n## Failing (reconsider)")
        for w in health["failing"]:
            lines.append(
                f"  ✗ {w['title'][:40]} ({w['success_rate']:.0%} success rate)"
            )

    if health.get("stale"):
        lines.append(f"\n## Stale (never applied)")
        for w in health["stale"][:3]:
            lines.append(f"  ? {w['title'][:40]} ({w['age_days']}d old)")

    lines.append("\n" + "=" * 60)
    return "\n".join(lines)


def get_decay_visualization(limit: int = 20) -> Dict:
    """
    Get wisdom items with their decay curves for visualization.

    Returns data structure suitable for ASCII chart rendering.
    """
    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        SELECT id, title, confidence, timestamp, last_used
        FROM wisdom
        ORDER BY confidence DESC
        LIMIT ?
    """,
        (limit,),
    )

    wisdom_list = []
    now = datetime.now()

    for wid, title, confidence, timestamp, last_used in c.fetchall():
        created = datetime.fromisoformat(timestamp) if timestamp else now
        age_days = (now - created).days

        if last_used:
            last = datetime.fromisoformat(last_used)
            inactive_days = (now - last).days
        else:
            inactive_days = age_days

        # Calculate decay curve points (past and projected future)
        months_inactive = inactive_days / 30.0
        current_decay = 0.95**months_inactive
        effective_conf = confidence * current_decay

        # Project 6 months of decay
        decay_curve = []
        for future_months in range(7):
            total_months = months_inactive + future_months
            decay = 0.95**total_months
            projected = confidence * decay
            decay_curve.append(
                {
                    "month": future_months,
                    "confidence": round(projected, 3),
                    "is_current": future_months == 0,
                }
            )

        wisdom_list.append(
            {
                "id": wid,
                "title": title[:40],
                "base_confidence": confidence,
                "effective_confidence": effective_conf,
                "inactive_days": inactive_days,
                "decay_curve": decay_curve,
            }
        )

    conn.close()

    return {
        "wisdom": wisdom_list,
        "decay_rate": 0.95,
        "decay_unit": "month",
    }


def format_decay_chart(decay_data: Dict) -> str:
    """Format decay visualization as ASCII chart."""
    lines = []
    lines.append("=" * 70)
    lines.append("WISDOM DECAY VISUALIZATION")
    lines.append("=" * 70)
    lines.append(
        f"\nDecay rate: {decay_data['decay_rate']:.0%} per {decay_data['decay_unit']}"
    )
    lines.append("Bars show current effective confidence, projected 6 months\n")

    for w in decay_data["wisdom"][:15]:
        title = w["title"][:30].ljust(30)
        eff = w["effective_confidence"]
        base = w["base_confidence"]
        inactive = w["inactive_days"]

        # Current bar
        bar_len = int(eff * 40)
        bar = "█" * bar_len + "░" * (40 - bar_len)

        # Decay indicator
        decay_pct = (1 - eff / base) * 100 if base > 0 else 0
        if decay_pct > 20:
            indicator = f"↓{decay_pct:.0f}%"
        elif decay_pct > 0:
            indicator = f"-{decay_pct:.0f}%"
        else:
            indicator = "new"

        lines.append(f"{title} |{bar}| {eff:.0%} ({indicator})")

        # Show projected decay for items with high confidence
        if eff > 0.5 and inactive > 0:
            future_bars = []
            for point in w["decay_curve"][1:4]:  # Next 3 months
                future_len = int(point["confidence"] * 10)
                future_bars.append("▓" * future_len + "░" * (10 - future_len))
            lines.append(f"{'':30} └─ Future: {' → '.join(future_bars)}")

    # Legend
    lines.append("\n" + "-" * 70)
    lines.append("Legend: █ = current confidence, ▓ = projected future (3 months)")
    lines.append("        ↓ = significant decay (>20%), - = minor decay")
    lines.append("=" * 70)

    return "\n".join(lines)


def analyze_conversation_patterns(days: int = 30) -> Dict:
    """Analyze conversation patterns."""
    conn = get_db_connection()
    c = conn.cursor()

    cutoff = (datetime.now() - timedelta(days=days)).isoformat()

    c.execute(
        """
        SELECT project, started_at, ended_at, summary
        FROM conversations
        WHERE started_at > ?
    """,
        (cutoff,),
    )

    conversations = c.fetchall()
    conn.close()

    projects = Counter()
    durations = []

    for project, started, ended, summary in conversations:
        projects[project or "unknown"] += 1
        if started and ended:
            try:
                start_dt = datetime.fromisoformat(started)
                end_dt = datetime.fromisoformat(ended)
                durations.append((end_dt - start_dt).seconds)
            except ValueError:
                pass

    return {
        "total_conversations": len(conversations),
        "by_project": dict(projects),
        "avg_duration_seconds": sum(durations) / len(durations) if durations else 0,
    }


# =============================================================================
# PAIN POINT TRACKING
# =============================================================================


def record_pain_point(
    category: str, description: str, severity: str = "medium", context: Dict = None
):
    """
    Record a pain point encountered during soul operation.

    Categories:
    - latency: Something was slow
    - error: Something failed
    - missing: Capability was needed but absent
    - friction: User experience issue
    - inconsistency: Behavior didn't match expectations
    """
    _ensure_dirs()

    entry = {
        "id": datetime.now().strftime("%Y%m%d_%H%M%S_%f"),
        "timestamp": datetime.now().isoformat(),
        "category": category,
        "description": description,
        "severity": severity,
        "context": context or {},
        "addressed": False,
    }

    with open(PAIN_POINTS_LOG, "a") as f:
        f.write(json.dumps(entry) + "\n")

    return entry


def get_pain_points(
    category: str = None, addressed: bool = False, limit: int = 50
) -> List[Dict]:
    """Get recorded pain points."""
    _ensure_dirs()

    if not PAIN_POINTS_LOG.exists():
        return []

    points = []
    with open(PAIN_POINTS_LOG) as f:
        for line in f:
            if line.strip():
                entry = json.loads(line)
                if category and entry["category"] != category:
                    continue
                if entry["addressed"] != addressed:
                    continue
                points.append(entry)

    # Sort by severity
    severity_order = {"critical": 0, "high": 1, "medium": 2, "low": 3}
    points.sort(key=lambda x: severity_order.get(x["severity"], 2))

    return points[:limit]


def analyze_pain_points() -> Dict:
    """Analyze pain point patterns."""
    points = get_pain_points(addressed=False, limit=1000)

    by_category = Counter(p["category"] for p in points)
    by_severity = Counter(p["severity"] for p in points)

    return {
        "total_open": len(points),
        "by_category": dict(by_category),
        "by_severity": dict(by_severity),
        "recent": points[:10],
    }


# =============================================================================
# METRICS COLLECTION
# =============================================================================


def record_metric(name: str, value: float, unit: str = "", tags: Dict = None):
    """Record a performance metric."""
    _ensure_dirs()

    entry = {
        "timestamp": datetime.now().isoformat(),
        "name": name,
        "value": value,
        "unit": unit,
        "tags": tags or {},
    }

    with open(METRICS_LOG, "a") as f:
        f.write(json.dumps(entry) + "\n")


def get_metrics(name: str = None, hours: int = 24) -> List[Dict]:
    """Get recorded metrics."""
    _ensure_dirs()

    if not METRICS_LOG.exists():
        return []

    cutoff = (datetime.now() - timedelta(hours=hours)).isoformat()

    metrics = []
    with open(METRICS_LOG) as f:
        for line in f:
            if line.strip():
                entry = json.loads(line)
                if entry["timestamp"] < cutoff:
                    continue
                if name and entry["name"] != name:
                    continue
                metrics.append(entry)

    return metrics


def analyze_metrics(hours: int = 24) -> Dict:
    """Analyze metric patterns."""
    metrics = get_metrics(hours=hours)

    by_name = {}
    for m in metrics:
        name = m["name"]
        if name not in by_name:
            by_name[name] = []
        by_name[name].append(m["value"])

    stats = {}
    for name, values in by_name.items():
        stats[name] = {
            "count": len(values),
            "min": min(values),
            "max": max(values),
            "avg": sum(values) / len(values),
        }

    return stats


# =============================================================================
# CROSS-SESSION TRENDS
# =============================================================================


def get_session_comparison(session_count: int = 10) -> Dict:
    """
    Compare recent sessions to identify trends and changes.

    Returns insights on:
    - Wisdom growth per session
    - New domains explored
    - Belief changes
    - Problem types tackled
    """
    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        SELECT id, project, started_at, ended_at, summary
        FROM conversations
        WHERE ended_at IS NOT NULL
        ORDER BY started_at DESC
        LIMIT ?
    """,
        (session_count,),
    )

    sessions = []
    for row in c.fetchall():
        sessions.append(
            {
                "id": row[0],
                "project": row[1],
                "started_at": row[2],
                "ended_at": row[3],
                "summary": row[4],
            }
        )

    c.execute(
        """
        SELECT COUNT(*) FROM wisdom WHERE timestamp >= ?
    """,
        (sessions[-1]["started_at"] if sessions else "1970-01-01",),
    )
    wisdom_before = 0

    session_data = []
    for i, session in enumerate(reversed(sessions)):
        c.execute(
            """
            SELECT COUNT(*) FROM wisdom
            WHERE timestamp <= ?
        """,
            (session["ended_at"] or session["started_at"],),
        )
        wisdom_count = c.fetchone()[0]

        c.execute(
            """
            SELECT COUNT(*) FROM wisdom_applications
            WHERE conversation_id = ?
        """,
            (session["id"],),
        )
        wisdom_applied = c.fetchone()[0]

        c.execute(
            """
            SELECT DISTINCT domain FROM wisdom
            WHERE timestamp BETWEEN ? AND ?
        """,
            (session["started_at"], session["ended_at"] or session["started_at"]),
        )
        new_domains = [r[0] for r in c.fetchall() if r[0]]

        session_data.append(
            {
                "session_id": session["id"],
                "project": session["project"],
                "date": session["started_at"][:10] if session["started_at"] else None,
                "wisdom_total": wisdom_count,
                "wisdom_gained": wisdom_count - wisdom_before,
                "wisdom_applied": wisdom_applied,
                "new_domains": new_domains,
                "summary": session["summary"][:100] if session["summary"] else None,
            }
        )
        wisdom_before = wisdom_count

    conn.close()

    total_gained = sum(s["wisdom_gained"] for s in session_data)
    avg_per_session = total_gained / len(session_data) if session_data else 0

    return {
        "sessions_analyzed": len(session_data),
        "total_wisdom_gained": total_gained,
        "avg_wisdom_per_session": round(avg_per_session, 1),
        "sessions": session_data,
    }


def get_growth_trajectory(days: int = 90) -> Dict:
    """
    Analyze soul growth trajectory over time.

    Returns:
    - Wisdom accumulation curve
    - Learning velocity (wisdom per week)
    - Domain expansion
    - Belief evolution
    """
    conn = get_db_connection()
    c = conn.cursor()

    cutoff = (datetime.now() - timedelta(days=days)).isoformat()

    c.execute(
        """
        SELECT type, domain, timestamp
        FROM wisdom
        WHERE timestamp >= ?
        ORDER BY timestamp
    """,
        (cutoff,),
    )

    wisdom_by_week = {}
    domain_first_seen = {}
    type_counts = Counter()

    for wtype, domain, timestamp in c.fetchall():
        try:
            dt = datetime.fromisoformat(timestamp)
            week = f"{dt.year}-W{dt.isocalendar()[1]:02d}"

            if week not in wisdom_by_week:
                wisdom_by_week[week] = {
                    "count": 0,
                    "types": Counter(),
                    "domains": set(),
                }

            wisdom_by_week[week]["count"] += 1
            wisdom_by_week[week]["types"][wtype] += 1
            if domain:
                wisdom_by_week[week]["domains"].add(domain)
                if domain not in domain_first_seen:
                    domain_first_seen[domain] = week

            type_counts[wtype] += 1
        except ValueError:
            continue

    c.execute(
        """
        SELECT COUNT(*), SUM(challenged_count), SUM(confirmed_count)
        FROM beliefs
        WHERE timestamp >= ?
    """,
        (cutoff,),
    )

    belief_row = c.fetchone()
    beliefs_added = belief_row[0] or 0
    beliefs_challenged = belief_row[1] or 0
    beliefs_confirmed = belief_row[2] or 0

    c.execute("""
        SELECT id, belief, strength, challenged_count, confirmed_count
        FROM beliefs
        WHERE challenged_count > 0
        ORDER BY challenged_count DESC
        LIMIT 5
    """)
    most_challenged = [
        {"belief": r[1], "strength": r[2], "challenges": r[3], "confirmations": r[4]}
        for r in c.fetchall()
    ]

    conn.close()

    weeks = sorted(wisdom_by_week.keys())
    cumulative = 0
    trajectory = []
    for week in weeks:
        cumulative += wisdom_by_week[week]["count"]
        trajectory.append(
            {
                "week": week,
                "gained": wisdom_by_week[week]["count"],
                "cumulative": cumulative,
                "types": dict(wisdom_by_week[week]["types"]),
                "new_domains": list(
                    d
                    for d in wisdom_by_week[week]["domains"]
                    if domain_first_seen.get(d) == week
                ),
            }
        )

    velocities = [t["gained"] for t in trajectory]
    avg_velocity = sum(velocities) / len(velocities) if velocities else 0
    recent_velocity = (
        sum(velocities[-4:]) / min(4, len(velocities)) if velocities else 0
    )

    return {
        "period_days": days,
        "total_wisdom_gained": cumulative,
        "total_domains": len(domain_first_seen),
        "avg_weekly_velocity": round(avg_velocity, 1),
        "recent_velocity": round(recent_velocity, 1),
        "velocity_trend": "accelerating"
        if recent_velocity > avg_velocity * 1.2
        else "decelerating"
        if recent_velocity < avg_velocity * 0.8
        else "stable",
        "type_distribution": dict(type_counts),
        "domain_timeline": domain_first_seen,
        "trajectory": trajectory,
        "beliefs": {
            "added": beliefs_added,
            "challenged": beliefs_challenged,
            "confirmed": beliefs_confirmed,
            "most_challenged": most_challenged,
        },
    }


def get_learning_patterns() -> Dict:
    """
    Identify learning patterns and trends.

    Analyzes:
    - What types of wisdom are gained most
    - Which domains are growing
    - Time patterns (when learning happens)
    - Correlation between failures and subsequent wisdom
    """
    conn = get_db_connection()
    c = conn.cursor()

    c.execute("""
        SELECT type, domain, timestamp
        FROM wisdom
        ORDER BY timestamp DESC
        LIMIT 100
    """)

    recent = c.fetchall()

    by_type = Counter(r[0] for r in recent)
    by_domain = Counter(r[1] or "general" for r in recent)

    hour_counts = Counter()
    day_counts = Counter()
    for _, _, ts in recent:
        try:
            dt = datetime.fromisoformat(ts)
            hour_counts[dt.hour] += 1
            day_counts[dt.strftime("%A")] += 1
        except ValueError:
            continue

    c.execute("""
        SELECT w.type, w.title, w.timestamp, prev.type as prev_type
        FROM wisdom w
        LEFT JOIN wisdom prev ON prev.timestamp < w.timestamp
        WHERE w.type = 'failure'
        ORDER BY w.timestamp DESC
        LIMIT 20
    """)

    c.execute("""
        SELECT
            f.id as failure_id,
            f.title as failure_title,
            f.timestamp as failure_time,
            (SELECT COUNT(*) FROM wisdom w2
             WHERE w2.timestamp > f.timestamp
             AND w2.timestamp < datetime(f.timestamp, '+7 days')
             AND w2.type IN ('pattern', 'insight')) as learnings_after
        FROM wisdom f
        WHERE f.type = 'failure'
        ORDER BY f.timestamp DESC
        LIMIT 10
    """)

    failure_learning = []
    for row in c.fetchall():
        failure_learning.append({"failure": row[1], "learnings_within_week": row[3]})

    conn.close()

    peak_hour = max(hour_counts, key=hour_counts.get) if hour_counts else None
    peak_day = max(day_counts, key=day_counts.get) if day_counts else None

    return {
        "recent_wisdom_count": len(recent),
        "type_distribution": dict(by_type),
        "domain_distribution": dict(by_domain),
        "temporal_patterns": {
            "peak_hour": peak_hour,
            "peak_day": peak_day,
            "hour_distribution": dict(hour_counts),
            "day_distribution": dict(day_counts),
        },
        "failure_to_learning": failure_learning,
        "growing_domains": [d for d, c in by_domain.most_common(3)],
        "dominant_type": by_type.most_common(1)[0][0] if by_type else None,
    }


def format_trends_report(comparison: Dict, trajectory: Dict, patterns: Dict) -> str:
    """Format cross-session trends for CLI display."""
    lines = []
    lines.append("=" * 60)
    lines.append("CROSS-SESSION TRENDS")
    lines.append("=" * 60)

    lines.append(f"\n## Growth Trajectory ({trajectory['period_days']} days)")
    lines.append(f"  Total wisdom gained: {trajectory['total_wisdom_gained']}")
    lines.append(f"  Domains explored: {trajectory['total_domains']}")
    lines.append(f"  Weekly velocity: {trajectory['avg_weekly_velocity']} wisdom/week")
    lines.append(
        f"  Recent velocity: {trajectory['recent_velocity']} ({trajectory['velocity_trend']})"
    )

    if trajectory.get("trajectory"):
        lines.append(f"\n  Weekly progress:")
        for t in trajectory["trajectory"][-8:]:
            bar = "█" * min(20, t["gained"])
            new_d = f" +{len(t['new_domains'])}d" if t["new_domains"] else ""
            lines.append(f"    {t['week']}: {bar} {t['gained']}{new_d}")

    lines.append(f"\n## Session Analysis ({comparison['sessions_analyzed']} sessions)")
    lines.append(f"  Avg wisdom per session: {comparison['avg_wisdom_per_session']}")

    for s in comparison["sessions"][-5:]:
        gained = f"+{s['wisdom_gained']}" if s["wisdom_gained"] > 0 else "0"
        project = s["project"] or "unknown"
        lines.append(
            f"    {s['date']}: {project[:20]} ({gained} wisdom, {s['wisdom_applied']} applied)"
        )

    lines.append(f"\n## Learning Patterns")
    lines.append(f"  Dominant type: {patterns['dominant_type']}")
    lines.append(f"  Growing domains: {', '.join(patterns['growing_domains'])}")
    if patterns["temporal_patterns"]["peak_hour"] is not None:
        lines.append(
            f"  Peak learning: {patterns['temporal_patterns']['peak_day']}s at {patterns['temporal_patterns']['peak_hour']}:00"
        )

    beliefs = trajectory.get("beliefs", {})
    if beliefs.get("most_challenged"):
        lines.append(f"\n## Belief Evolution")
        lines.append(
            f"  Added: {beliefs['added']}, Challenged: {beliefs['challenged']}, Confirmed: {beliefs['confirmed']}"
        )
        for b in beliefs["most_challenged"][:3]:
            lines.append(f"    ⚔ {b['belief'][:40]}... ({b['challenges']} challenges)")

    lines.append("\n" + "=" * 60)
    return "\n".join(lines)


# =============================================================================
# FULL INTROSPECTION
# =============================================================================


def generate_introspection_report() -> Dict:
    """
    Generate a complete introspection report.

    This is the main entry point for self-analysis.
    """
    report = {
        "generated_at": datetime.now().isoformat(),
        "source": get_source_stats(),
        "todos": find_todos_and_fixmes(),
        "wisdom_usage": analyze_wisdom_applications(),
        "conversations": analyze_conversation_patterns(),
        "pain_points": analyze_pain_points(),
        "metrics": analyze_metrics(),
    }

    # Generate summary insights
    insights = []

    # Unused wisdom insight
    unused = report["wisdom_usage"].get("unused_wisdom", [])
    if len(unused) > 5:
        insights.append(
            {
                "type": "unused_wisdom",
                "severity": "medium",
                "message": f"{len(unused)} wisdom entries have never been applied",
                "suggestion": "Review unused wisdom - remove stale entries or improve recall",
            }
        )

    # Failing wisdom insight
    failing = report["wisdom_usage"].get("failing_wisdom", [])
    if failing:
        insights.append(
            {
                "type": "failing_wisdom",
                "severity": "high",
                "message": f"{len(failing)} wisdom entries have >50% failure rate",
                "data": failing,
                "suggestion": "Investigate why these wisdom entries keep failing",
            }
        )

    # Pain point clusters
    pain = report["pain_points"]
    if pain.get("total_open", 0) > 10:
        top_category = max(
            pain.get("by_category", {}), key=pain["by_category"].get, default=None
        )
        if top_category:
            insights.append(
                {
                    "type": "pain_cluster",
                    "severity": "high",
                    "message": f"Cluster of {pain['by_category'][top_category]} pain points in '{top_category}'",
                    "suggestion": f"Focus improvement efforts on {top_category} issues",
                }
            )

    # TODOs in code
    todos = report["todos"]
    if len(todos) > 3:
        insights.append(
            {
                "type": "technical_debt",
                "severity": "low",
                "message": f"{len(todos)} TODO/FIXME comments in codebase",
                "suggestion": "Address technical debt items",
            }
        )

    report["insights"] = insights

    return report


def format_introspection_report(report: Dict) -> str:
    """Format introspection report for human reading."""
    lines = []
    lines.append("=" * 60)
    lines.append("SOUL INTROSPECTION REPORT")
    lines.append(f"Generated: {report['generated_at']}")
    lines.append("=" * 60)

    # Source stats
    src = report["source"]
    lines.append(f"\n## Codebase")
    lines.append(f"  Files: {src['file_count']}")
    lines.append(f"  Lines: {src['total_lines']}")
    lines.append(f"  Functions: {src['total_functions']}")

    # Wisdom usage
    wu = report["wisdom_usage"]
    lines.append(f"\n## Wisdom Usage (last {wu['period_days']} days)")
    lines.append(f"  Total applications: {wu['total_applications']}")
    lines.append(
        f"  Unique wisdom used: {wu['unique_wisdom_applied']}/{wu['total_wisdom']}"
    )
    lines.append(f"  Outcomes: {wu['outcomes']}")

    if wu.get("failing_wisdom"):
        lines.append(f"  ⚠️  Failing wisdom: {len(wu['failing_wisdom'])}")

    # Pain points
    pp = report["pain_points"]
    lines.append(f"\n## Pain Points")
    lines.append(f"  Open: {pp['total_open']}")
    if pp.get("by_category"):
        lines.append(f"  By category: {pp['by_category']}")

    # Insights
    if report.get("insights"):
        lines.append(f"\n## Key Insights")
        for insight in report["insights"]:
            icon = {"high": "🔴", "medium": "🟡", "low": "🟢"}.get(
                insight["severity"], "⚪"
            )
            lines.append(f"  {icon} {insight['message']}")
            lines.append(f"     → {insight['suggestion']}")

    lines.append("\n" + "=" * 60)

    return "\n".join(lines)
