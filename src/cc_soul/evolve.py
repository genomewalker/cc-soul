"""
Self-improvement mechanism for the soul.

Records insights about the soul's own architecture and behavior,
enabling continuous refinement of the system itself.
"""

import json
from datetime import datetime
from pathlib import Path
from typing import List, Dict, Optional

from .core import get_db_connection, SOUL_DIR

# Evolution log lives with the soul data
EVOLUTION_LOG = SOUL_DIR / "evolution.jsonl"


def record_insight(
    category: str,
    insight: str,
    suggested_change: str = None,
    priority: str = "medium",
    affected_modules: List[str] = None
) -> Dict:
    """
    Record an insight about how the soul could be improved.

    Categories:
    - architecture: Structural improvements
    - performance: Speed/efficiency improvements
    - ux: User experience improvements
    - feature: New capability ideas
    - bug: Issues discovered
    - integration: Better Claude Code integration

    Priority: low, medium, high, critical
    """
    entry = {
        "id": datetime.now().strftime('%Y%m%d_%H%M%S_%f'),
        "timestamp": datetime.now().isoformat(),
        "category": category,
        "insight": insight,
        "suggested_change": suggested_change,
        "priority": priority,
        "affected_modules": affected_modules or [],
        "status": "open",
        "implemented": False
    }

    with open(EVOLUTION_LOG, "a") as f:
        f.write(json.dumps(entry) + "\n")

    return entry


def get_evolution_insights(
    category: str = None,
    status: str = "open",
    limit: int = 20
) -> List[Dict]:
    """Get recorded evolution insights."""
    if not EVOLUTION_LOG.exists():
        return []

    insights = []
    with open(EVOLUTION_LOG) as f:
        for line in f:
            if line.strip():
                entry = json.loads(line)
                if category and entry['category'] != category:
                    continue
                if status and entry['status'] != status:
                    continue
                insights.append(entry)

    # Sort by priority
    priority_order = {'critical': 0, 'high': 1, 'medium': 2, 'low': 3}
    insights.sort(key=lambda x: priority_order.get(x['priority'], 2))

    return insights[:limit]


def mark_implemented(insight_id: str, notes: str = ""):
    """Mark an evolution insight as implemented."""
    if not EVOLUTION_LOG.exists():
        return

    lines = []
    with open(EVOLUTION_LOG) as f:
        for line in f:
            if line.strip():
                entry = json.loads(line)
                if entry['id'] == insight_id:
                    entry['status'] = 'implemented'
                    entry['implemented'] = True
                    entry['implemented_at'] = datetime.now().isoformat()
                    entry['implementation_notes'] = notes
                lines.append(json.dumps(entry) + "\n")

    with open(EVOLUTION_LOG, "w") as f:
        f.writelines(lines)


def get_evolution_summary() -> Dict:
    """Get summary of evolution state."""
    insights = get_evolution_insights(status=None, limit=1000)

    return {
        "total": len(insights),
        "open": len([i for i in insights if i['status'] == 'open']),
        "implemented": len([i for i in insights if i['implemented']]),
        "by_category": {
            cat: len([i for i in insights if i['category'] == cat])
            for cat in set(i['category'] for i in insights)
        },
        "high_priority_open": len([
            i for i in insights
            if i['status'] == 'open' and i['priority'] in ('high', 'critical')
        ])
    }


# Pre-seed some evolution insights based on the ultrathink session
def seed_evolution_insights():
    """Seed initial evolution insights from architecture review."""
    if EVOLUTION_LOG.exists():
        return  # Already seeded

    initial_insights = [
        {
            "category": "architecture",
            "insight": "The beliefs table is redundant - beliefs are just wisdom with type='principle'",
            "suggested_change": "Deprecate beliefs table, migrate to wisdom, eventually remove",
            "priority": "medium",
            "affected_modules": ["beliefs.py", "core.py"]
        },
        {
            "category": "feature",
            "insight": "No mechanism for Claude to see which wisdom was applied in a session",
            "suggested_change": "Add session-scoped application log visible in context",
            "priority": "medium",
            "affected_modules": ["wisdom.py", "hooks.py"]
        },
        {
            "category": "ux",
            "insight": "The grow CLI is separate from the cc-soul package",
            "suggested_change": "Consolidate CLI entry points into single 'soul' command",
            "priority": "low",
            "affected_modules": ["cli.py"]
        },
        {
            "category": "integration",
            "insight": "UserPromptSubmit hook adds latency due to embedding model load",
            "suggested_change": "Consider caching model or async loading, or skip for rapid prompts",
            "priority": "high",
            "affected_modules": ["vectors.py", "hooks.py"]
        },
        {
            "category": "feature",
            "insight": "No way to see wisdom application history over time",
            "suggested_change": "Add analytics/visualization of wisdom usage patterns",
            "priority": "low",
            "affected_modules": ["wisdom.py"]
        }
    ]

    for insight in initial_insights:
        record_insight(**insight)
