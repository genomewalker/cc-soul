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
