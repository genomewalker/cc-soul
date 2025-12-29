# =============================================================================
# Observation Tools (Passive Learning)
# =============================================================================

@mcp.tool()
def get_observations(limit: int = 20) -> str:
    """Get pending observations not yet converted to wisdom.

    Observations are learnings extracted from session analysis:
    - Corrections: User redirected approach
    - Preferences: User preferences stated
    - Decisions: Architectural choices made
    - Struggles: Problems that took multiple attempts
    - Breakthroughs: Key insight moments
    - File patterns: Important files identified

    These are staged - they become wisdom when confirmed or promoted.

    Args:
        limit: Max observations to return
    """
    from .observe import get_pending_observations

    observations = get_pending_observations(limit=limit)

    if not observations:
        return "No pending observations. The soul is watching and learning."

    type_emoji = {
        "correction": "🔄",
        "preference": "👤",
        "pattern": "🔁",
        "struggle": "💪",
        "breakthrough": "💡",
        "file_pattern": "📁",
        "decision": "⚖️",
    }

    lines = [f"Pending observations ({len(observations)}):", ""]

    for obs in observations:
        emoji = type_emoji.get(obs["type"], "•")
        conf = obs.get("confidence", 0)
        lines.append(f"{emoji} #{obs['id']} [{conf:.0%}] {obs['content'][:60]}")

    lines.append("")
    lines.append("Use promote_observation(id) to convert to wisdom")

    return "\n".join(lines)


@mcp.tool()
def promote_observation(observation_id: int) -> str:
    """Promote an observation to permanent wisdom.

    Observations with high confidence are auto-promoted.
    Use this to manually promote observations you find valuable.

    Args:
        observation_id: The observation ID to promote
    """
    from .observe import promote_observation_to_wisdom

    wisdom_id = promote_observation_to_wisdom(observation_id)

    if wisdom_id:
        return f"Observation #{observation_id} → Wisdom #{wisdom_id}"
    return f"Observation #{observation_id} not found or already promoted"


@mcp.tool()
def get_observation_stats() -> str:
    """Get statistics about passive learning observations.

    Shows how many observations have been extracted and
    how many have been promoted to wisdom.
    """
    from .observe import get_pending_observations, _ensure_observation_tables
    from .core import get_db_connection

    _ensure_observation_tables()
    conn = get_db_connection()
    c = conn.cursor()

    # Total observations
    c.execute("SELECT COUNT(*) FROM session_observations")
    total = c.fetchone()[0]

    # Pending (not promoted)
    c.execute("SELECT COUNT(*) FROM session_observations WHERE converted_to_wisdom IS NULL")
    pending = c.fetchone()[0]

    # Promoted
    promoted = total - pending

    # By type
    c.execute("""
        SELECT observation_type, COUNT(*)
        FROM session_observations
        GROUP BY observation_type
        ORDER BY COUNT(*) DESC
    """)
    by_type = c.fetchall()

    conn.close()

    lines = ["# Observation Statistics", ""]
    lines.append(f"Total observations: {total}")
    lines.append(f"Pending review: {pending}")
    lines.append(f"Promoted to wisdom: {promoted}")

    if by_type:
        lines.append("")
        lines.append("By type:")
        type_emoji = {
            "correction": "🔄",
            "preference": "👤",
            "pattern": "🔁",
            "struggle": "💪",
            "breakthrough": "💡",
            "file_pattern": "📁",
            "decision": "⚖️",
        }
        for obs_type, count in by_type:
            emoji = type_emoji.get(obs_type, "•")
            lines.append(f"  {emoji} {obs_type}: {count}")

    return "\n".join(lines)


@mcp.tool()
def reflect_now() -> str:
    """Trigger immediate session reflection.

    Analyzes the current session's messages to extract learnings:
    - Corrections, preferences, decisions
    - Struggles and breakthroughs
    - File patterns

    Normally runs automatically at session end, but you can
    trigger it manually to capture learnings mid-session.
    """
    from .hooks import _session_messages, _session_files_touched, get_project_name
    from .observe import reflect_on_session, format_reflection_summary

    if not _session_messages:
        return "No messages to reflect on. Start working first!"

    reflection = reflect_on_session(
        messages=_session_messages,
        files_touched=list(_session_files_touched),
        project=get_project_name(),
        auto_promote=True,
    )

    return format_reflection_summary(reflection)
