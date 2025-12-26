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
        lines = content.split('\n')
        total_lines += len(lines)
        total_functions += content.count('\ndef ')
        total_classes += content.count('\nclass ')

    return {
        'file_count': len(sources),
        'total_lines': total_lines,
        'total_functions': total_functions,
        'total_classes': total_classes,
        'files': list(sources.keys())
    }


def find_todos_and_fixmes() -> List[Dict]:
    """Find TODO and FIXME comments in source."""
    sources = read_soul_source()
    issues = []

    for filename, content in sources.items():
        for i, line in enumerate(content.split('\n'), 1):
            for marker in ['TODO', 'FIXME', 'XXX', 'HACK']:
                if marker in line:
                    issues.append({
                        'file': filename,
                        'line': i,
                        'type': marker,
                        'content': line.strip()
                    })

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
    c.execute('''
        SELECT wa.wisdom_id, w.title, w.type, wa.outcome, wa.context
        FROM wisdom_applications wa
        JOIN wisdom w ON wa.wisdom_id = w.id
        WHERE wa.applied_at > ?
    ''', (cutoff,))

    applications = c.fetchall()

    # Get all wisdom for comparison
    c.execute('SELECT id, title, type, confidence FROM wisdom')
    all_wisdom = {row[0]: {'title': row[1], 'type': row[2], 'confidence': row[3]}
                  for row in c.fetchall()}

    conn.close()

    # Analyze patterns
    applied_ids = set()
    outcomes = Counter()
    by_wisdom = {}

    for wisdom_id, title, wtype, outcome, context in applications:
        applied_ids.add(wisdom_id)
        outcomes[outcome or 'pending'] += 1

        if wisdom_id not in by_wisdom:
            by_wisdom[wisdom_id] = {
                'title': title,
                'type': wtype,
                'applications': 0,
                'successes': 0,
                'failures': 0
            }

        by_wisdom[wisdom_id]['applications'] += 1
        if outcome == 'success':
            by_wisdom[wisdom_id]['successes'] += 1
        elif outcome == 'failure':
            by_wisdom[wisdom_id]['failures'] += 1

    # Find unused wisdom
    unused = [
        {'id': wid, **info}
        for wid, info in all_wisdom.items()
        if wid not in applied_ids
    ]

    # Find failing wisdom (>50% failure rate with >2 applications)
    failing = [
        {'id': wid, **info, 'failure_rate': info['failures'] / info['applications']}
        for wid, info in by_wisdom.items()
        if info['applications'] >= 2 and info['failures'] / info['applications'] > 0.5
    ]

    return {
        'period_days': days,
        'total_applications': len(applications),
        'outcomes': dict(outcomes),
        'unique_wisdom_applied': len(applied_ids),
        'total_wisdom': len(all_wisdom),
        'unused_wisdom': unused,
        'failing_wisdom': failing,
        'most_applied': sorted(
            by_wisdom.items(),
            key=lambda x: x[1]['applications'],
            reverse=True
        )[:5]
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

    c.execute('''
        SELECT applied_at, outcome
        FROM wisdom_applications
        WHERE applied_at > ?
        ORDER BY applied_at
    ''', (cutoff,))

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
                buckets[key] = {"period": key, "applications": 0, "successes": 0, "failures": 0}

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

    c.execute('''
        SELECT id, type, title, content, domain, confidence, timestamp, last_used,
               success_count, failure_count
        FROM wisdom
    ''')

    wisdom_list = []
    now = datetime.now()

    for row in c.fetchall():
        wid, wtype, title, content, domain, confidence, timestamp, last_used, successes, failures = row

        created = datetime.fromisoformat(timestamp) if timestamp else now
        age_days = (now - created).days

        if last_used:
            last = datetime.fromisoformat(last_used)
            inactive_days = (now - last).days
        else:
            inactive_days = age_days

        # Calculate effective confidence with decay
        months_inactive = inactive_days / 30.0
        decay_factor = 0.95 ** months_inactive
        effective_conf = confidence * decay_factor

        total_apps = successes + failures
        success_rate = successes / total_apps if total_apps > 0 else None

        wisdom_list.append({
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
        })

    conn.close()

    # Categorize wisdom
    decaying = [w for w in wisdom_list if w["decay_factor"] < 0.8]
    stale = [w for w in wisdom_list if w["total_applications"] == 0 and w["age_days"] > 7]
    healthy = [w for w in wisdom_list if w["decay_factor"] >= 0.9 and w["total_applications"] > 0]
    failing = [w for w in wisdom_list if w["success_rate"] is not None and w["success_rate"] < 0.5 and w["total_applications"] >= 2]

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
            [w for w in wisdom_list if w["success_rate"] is not None and w["total_applications"] >= 2],
            key=lambda x: (-x["success_rate"], -x["total_applications"])
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
            success_rate = bucket["successes"] / bucket["applications"] * 100 if bucket["applications"] else 0
            bar = "█" * min(20, bucket["applications"])
            lines.append(f"  {bucket['period']}: {bar} {bucket['applications']} ({success_rate:.0f}% success)")

    # Top performers
    if health.get("top_performers"):
        lines.append(f"\n## Top Performers")
        for w in health["top_performers"]:
            lines.append(f"  ✓ {w['title'][:40]} ({w['success_rate']:.0%}, {w['total_applications']} uses)")

    # Issues
    if health.get("decaying"):
        lines.append(f"\n## Decaying (needs reinforcement)")
        for w in health["decaying"][:3]:
            lines.append(f"  ↓ {w['title'][:40]} (conf: {w['effective_confidence']:.0%}, inactive {w['inactive_days']}d)")

    if health.get("failing"):
        lines.append(f"\n## Failing (reconsider)")
        for w in health["failing"]:
            lines.append(f"  ✗ {w['title'][:40]} ({w['success_rate']:.0%} success rate)")

    if health.get("stale"):
        lines.append(f"\n## Stale (never applied)")
        for w in health["stale"][:3]:
            lines.append(f"  ? {w['title'][:40]} ({w['age_days']}d old)")

    lines.append("\n" + "=" * 60)
    return "\n".join(lines)


def analyze_conversation_patterns(days: int = 30) -> Dict:
    """Analyze conversation patterns."""
    conn = get_db_connection()
    c = conn.cursor()

    cutoff = (datetime.now() - timedelta(days=days)).isoformat()

    c.execute('''
        SELECT project, started_at, ended_at, summary
        FROM conversations
        WHERE started_at > ?
    ''', (cutoff,))

    conversations = c.fetchall()
    conn.close()

    projects = Counter()
    durations = []

    for project, started, ended, summary in conversations:
        projects[project or 'unknown'] += 1
        if started and ended:
            try:
                start_dt = datetime.fromisoformat(started)
                end_dt = datetime.fromisoformat(ended)
                durations.append((end_dt - start_dt).seconds)
            except ValueError:
                pass

    return {
        'total_conversations': len(conversations),
        'by_project': dict(projects),
        'avg_duration_seconds': sum(durations) / len(durations) if durations else 0
    }


# =============================================================================
# PAIN POINT TRACKING
# =============================================================================

def record_pain_point(
    category: str,
    description: str,
    severity: str = "medium",
    context: Dict = None
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
        "id": datetime.now().strftime('%Y%m%d_%H%M%S_%f'),
        "timestamp": datetime.now().isoformat(),
        "category": category,
        "description": description,
        "severity": severity,
        "context": context or {},
        "addressed": False
    }

    with open(PAIN_POINTS_LOG, "a") as f:
        f.write(json.dumps(entry) + "\n")

    return entry


def get_pain_points(
    category: str = None,
    addressed: bool = False,
    limit: int = 50
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
                if category and entry['category'] != category:
                    continue
                if entry['addressed'] != addressed:
                    continue
                points.append(entry)

    # Sort by severity
    severity_order = {'critical': 0, 'high': 1, 'medium': 2, 'low': 3}
    points.sort(key=lambda x: severity_order.get(x['severity'], 2))

    return points[:limit]


def analyze_pain_points() -> Dict:
    """Analyze pain point patterns."""
    points = get_pain_points(addressed=False, limit=1000)

    by_category = Counter(p['category'] for p in points)
    by_severity = Counter(p['severity'] for p in points)

    return {
        'total_open': len(points),
        'by_category': dict(by_category),
        'by_severity': dict(by_severity),
        'recent': points[:10]
    }


# =============================================================================
# METRICS COLLECTION
# =============================================================================

def record_metric(
    name: str,
    value: float,
    unit: str = "",
    tags: Dict = None
):
    """Record a performance metric."""
    _ensure_dirs()

    entry = {
        "timestamp": datetime.now().isoformat(),
        "name": name,
        "value": value,
        "unit": unit,
        "tags": tags or {}
    }

    with open(METRICS_LOG, "a") as f:
        f.write(json.dumps(entry) + "\n")


def get_metrics(
    name: str = None,
    hours: int = 24
) -> List[Dict]:
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
                if entry['timestamp'] < cutoff:
                    continue
                if name and entry['name'] != name:
                    continue
                metrics.append(entry)

    return metrics


def analyze_metrics(hours: int = 24) -> Dict:
    """Analyze metric patterns."""
    metrics = get_metrics(hours=hours)

    by_name = {}
    for m in metrics:
        name = m['name']
        if name not in by_name:
            by_name[name] = []
        by_name[name].append(m['value'])

    stats = {}
    for name, values in by_name.items():
        stats[name] = {
            'count': len(values),
            'min': min(values),
            'max': max(values),
            'avg': sum(values) / len(values)
        }

    return stats


# =============================================================================
# FULL INTROSPECTION
# =============================================================================

def generate_introspection_report() -> Dict:
    """
    Generate a complete introspection report.

    This is the main entry point for self-analysis.
    """
    report = {
        'generated_at': datetime.now().isoformat(),
        'source': get_source_stats(),
        'todos': find_todos_and_fixmes(),
        'wisdom_usage': analyze_wisdom_applications(),
        'conversations': analyze_conversation_patterns(),
        'pain_points': analyze_pain_points(),
        'metrics': analyze_metrics()
    }

    # Generate summary insights
    insights = []

    # Unused wisdom insight
    unused = report['wisdom_usage'].get('unused_wisdom', [])
    if len(unused) > 5:
        insights.append({
            'type': 'unused_wisdom',
            'severity': 'medium',
            'message': f"{len(unused)} wisdom entries have never been applied",
            'suggestion': "Review unused wisdom - remove stale entries or improve recall"
        })

    # Failing wisdom insight
    failing = report['wisdom_usage'].get('failing_wisdom', [])
    if failing:
        insights.append({
            'type': 'failing_wisdom',
            'severity': 'high',
            'message': f"{len(failing)} wisdom entries have >50% failure rate",
            'data': failing,
            'suggestion': "Investigate why these wisdom entries keep failing"
        })

    # Pain point clusters
    pain = report['pain_points']
    if pain.get('total_open', 0) > 10:
        top_category = max(pain.get('by_category', {}), key=pain['by_category'].get, default=None)
        if top_category:
            insights.append({
                'type': 'pain_cluster',
                'severity': 'high',
                'message': f"Cluster of {pain['by_category'][top_category]} pain points in '{top_category}'",
                'suggestion': f"Focus improvement efforts on {top_category} issues"
            })

    # TODOs in code
    todos = report['todos']
    if len(todos) > 3:
        insights.append({
            'type': 'technical_debt',
            'severity': 'low',
            'message': f"{len(todos)} TODO/FIXME comments in codebase",
            'suggestion': "Address technical debt items"
        })

    report['insights'] = insights

    return report


def format_introspection_report(report: Dict) -> str:
    """Format introspection report for human reading."""
    lines = []
    lines.append("=" * 60)
    lines.append("SOUL INTROSPECTION REPORT")
    lines.append(f"Generated: {report['generated_at']}")
    lines.append("=" * 60)

    # Source stats
    src = report['source']
    lines.append(f"\n## Codebase")
    lines.append(f"  Files: {src['file_count']}")
    lines.append(f"  Lines: {src['total_lines']}")
    lines.append(f"  Functions: {src['total_functions']}")

    # Wisdom usage
    wu = report['wisdom_usage']
    lines.append(f"\n## Wisdom Usage (last {wu['period_days']} days)")
    lines.append(f"  Total applications: {wu['total_applications']}")
    lines.append(f"  Unique wisdom used: {wu['unique_wisdom_applied']}/{wu['total_wisdom']}")
    lines.append(f"  Outcomes: {wu['outcomes']}")

    if wu.get('failing_wisdom'):
        lines.append(f"  ⚠️  Failing wisdom: {len(wu['failing_wisdom'])}")

    # Pain points
    pp = report['pain_points']
    lines.append(f"\n## Pain Points")
    lines.append(f"  Open: {pp['total_open']}")
    if pp.get('by_category'):
        lines.append(f"  By category: {pp['by_category']}")

    # Insights
    if report.get('insights'):
        lines.append(f"\n## Key Insights")
        for insight in report['insights']:
            icon = {'high': '🔴', 'medium': '🟡', 'low': '🟢'}.get(insight['severity'], '⚪')
            lines.append(f"  {icon} {insight['message']}")
            lines.append(f"     → {insight['suggestion']}")

    lines.append("\n" + "=" * 60)

    return "\n".join(lines)
