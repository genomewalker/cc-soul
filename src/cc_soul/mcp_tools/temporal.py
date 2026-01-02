# =============================================================================
# Temporal Dynamics - Time Shapes Memory
# =============================================================================

@mcp.tool()
def get_temporal_trends(days: int = 7) -> str:
    """Get temporal trends over the last N days.

    Shows how the soul has evolved: coherence trajectory, wisdom effectiveness,
    and activity patterns.

    Args:
        days: Number of days to analyze (default 7)
    """
    from .temporal import get_temporal_trends as _get_trends, init_temporal_tables

    init_temporal_tables()
    trends = _get_trends(days=days)

    if trends.get("trend") == "insufficient_data":
        return "Insufficient data for trends. Need more sessions to track patterns."

    lines = [f"Temporal Trends ({days} days)", ""]

    if trends.get("coherence_trend"):
        marker = {"improving": "[+]", "declining": "[-]", "stable": "[=]"}.get(
            trends["coherence_trend"], ""
        )
        lines.append(f"Coherence: {marker} {trends['coherence_trend']}")
        if trends.get("avg_coherence"):
            lines.append(f"  Average: {trends['avg_coherence']:.0%}")

    if trends.get("total_applications"):
        lines.append(f"Wisdom Applications: {trends['total_applications']}")
        if trends.get("success_rate"):
            lines.append(f"  Success Rate: {trends['success_rate']:.0%}")

    return "\n".join(lines)


@mcp.tool()
def get_event_timeline(event_type: str = None, limit: int = 20) -> str:
    """Get recent events from the unified soul timeline.

    Every significant soul event is logged: wisdom gained, beliefs revised,
    intentions set, coherence shifts.

    Args:
        event_type: Filter by type (e.g., "wisdom_gained", "belief_revised")
        limit: Maximum events to return
    """
    from .temporal import get_events, init_temporal_tables, EventType

    init_temporal_tables()

    et = None
    if event_type:
        try:
            et = EventType(event_type)
        except ValueError:
            return f"Unknown event type: {event_type}. Valid types: {[e.value for e in EventType]}"

    events = get_events(event_type=et, limit=limit)

    if not events:
        return "No events recorded yet."

    lines = ["Soul Event Timeline:", ""]
    for e in events:
        time_part = e["timestamp"].split("T")[1][:8] if "T" in e["timestamp"] else e["timestamp"]
        entity = f" [{e['entity_id'][:20]}]" if e.get("entity_id") else ""
        lines.append(f"  {time_part} {e['event_type']}{entity}")

    return "\n".join(lines)


@mcp.tool()
def get_proactive_suggestions(limit: int = 5) -> str:
    """Get proactive suggestions - things the soul thinks should be surfaced.

    The soul notices:
    - High-confidence wisdom not used recently
    - Stale identity aspects needing confirmation
    - Patterns worth revisiting

    Args:
        limit: Maximum suggestions
    """
    from .temporal import get_proactive_items, find_proactive_candidates, init_temporal_tables

    init_temporal_tables()

    find_proactive_candidates()
    items = get_proactive_items(limit=limit)

    if not items:
        return "No proactive suggestions right now."

    lines = ["Proactive Suggestions:", ""]
    for item in items:
        priority_bar = "*" * int(item["priority"] * 5)
        lines.append(f"  [{priority_bar}] {item['reason']}")
        lines.append(f"      -> {item['entity_type']}: {item['entity_id']}")

    return "\n".join(lines)


@mcp.tool()
def revise_belief(belief_id: str, reason: str, evidence: str = None, new_content: str = None) -> str:
    """Revise a belief based on new evidence.

    Beliefs should evolve when contradicted by experience.
    This tracks the revision history.

    Args:
        belief_id: Which belief to revise
        reason: Why we're revising
        evidence: What evidence prompted this
        new_content: New belief content (optional, for rewording)
    """
    from .temporal import revise_belief as _revise, init_temporal_tables

    init_temporal_tables()
    result = _revise(
        belief_id=belief_id,
        reason=reason,
        evidence=evidence,
        new_content=new_content,
    )

    if not result:
        return f"Belief {belief_id} not found"

    return (
        f"Belief revised:\n"
        f"  Old confidence: {result['old_confidence']:.0%}\n"
        f"  New confidence: {result['new_confidence']:.0%}\n"
        f"  Reason: {reason}"
    )


@mcp.tool()
def get_belief_history(belief_id: str) -> str:
    """Get revision history for a belief.

    Shows how a belief has evolved over time.

    Args:
        belief_id: The belief to examine
    """
    from .temporal import get_belief_history as _get_history, init_temporal_tables

    init_temporal_tables()
    history = _get_history(belief_id)

    if not history:
        return f"No revision history for belief {belief_id}"

    lines = [f"Revision History for {belief_id}:", ""]
    for h in history:
        date = h["timestamp"].split("T")[0]
        lines.append(f"  {date}: {h['old_confidence']:.0%} -> {h['new_confidence']:.0%}")
        lines.append(f"    Reason: {h['reason']}")

    return "\n".join(lines)


@mcp.tool()
def promote_cross_project_pattern(pattern_id: str) -> str:
    """Promote a cross-project pattern to universal wisdom.

    Once a pattern has proven itself across projects, crystallize it
    as wisdom that applies everywhere.

    Args:
        pattern_id: The pattern to promote
    """
    from .temporal import promote_pattern_to_wisdom, init_temporal_tables

    init_temporal_tables()
    wisdom_id = promote_pattern_to_wisdom(pattern_id)

    if not wisdom_id:
        return f"Pattern {pattern_id} not found"

    return f"Pattern promoted to wisdom: {wisdom_id}"


@mcp.tool()
def run_temporal_maintenance() -> str:
    """Run temporal maintenance - the soul's self-care routine.

    Automatically:
    - Decays stale identity aspects
    - Finds things worth surfacing proactively
    - Updates daily statistics
    """
    from .temporal import run_temporal_maintenance as _run, init_temporal_tables

    init_temporal_tables()
    results = _run()

    lines = ["Temporal Maintenance Complete:", ""]

    if results["identity_decayed"]:
        lines.append(f"  Identity aspects decayed: {len(results['identity_decayed'])}")
        for d in results["identity_decayed"][:3]:
            lines.append(f"    - {d['aspect']}: {d['old_confidence']:.0%} -> {d['new_confidence']:.0%}")

    if results["proactive_queued"]:
        lines.append(f"  Proactive items queued: {len(results['proactive_queued'])}")

    if results["stats_updated"]:
        lines.append("  Daily stats updated +")

    return "\n".join(lines) if len(lines) > 2 else "No maintenance needed."


@mcp.tool()
def confirm_identity_aspect(aspect: str, key: str) -> str:
    """Confirm an identity observation, strengthening it.

    Called when behavior validates an identity aspect.
    Strengthens confidence using diminishing returns.

    Args:
        aspect: The aspect category
        key: The specific key within the aspect
    """
    from .temporal import confirm_identity, init_temporal_tables

    init_temporal_tables()
    new_confidence = confirm_identity(aspect, key)

    if new_confidence is None:
        return f"Identity aspect {aspect}:{key} not found"

    return f"Identity confirmed: {aspect}:{key} -> {new_confidence:.0%}"


@mcp.tool()
def get_stale_aspects() -> str:
    """Get identity aspects that haven't been confirmed recently.

    Stale aspects might need re-observation or might be outdated.
    """
    from .temporal import is_stale, days_since, init_temporal_tables
    from ..core import get_synapse_graph
    import json

    init_temporal_tables()
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

        if confidence > 0.3 and is_stale(last_confirmed):
            stale.append({
                "aspect": data.get("aspect", "unknown"),
                "key": data.get("key", "unknown"),
                "value": data.get("value", "")[:50],
                "confidence": confidence,
                "days_stale": days_since(last_confirmed),
            })

    if not stale:
        return "No stale identity aspects. All observations are recent."

    lines = ["Stale Identity Aspects (need confirmation):", ""]
    for s in stale[:10]:
        lines.append(f"  {s['aspect']}: {s['key']} ({s['days_stale']} days)")
        lines.append(f"    Current confidence: {s['confidence']:.0%}")

    return "\n".join(lines)
