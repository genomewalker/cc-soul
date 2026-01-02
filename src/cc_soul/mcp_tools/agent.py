# =============================================================================
# Soul Agent - Autonomous Agency
# =============================================================================

@mcp.tool()
def soul_agent_step(
    user_prompt: str = "",
    assistant_output: str = "",
    session_phase: str = "active",
) -> str:
    """Run one agent cycle - the soul exercises judgment.

    The agent observes, judges, decides, and acts within its confidence-risk matrix.
    Low-risk actions are taken autonomously; high-risk actions are proposed.

    Args:
        user_prompt: What the user said (optional)
        assistant_output: What the assistant produced (optional)
        session_phase: Where in the session: start, active, ending
    """
    from .soul_agent import agent_step, format_agent_report

    report = agent_step(user_prompt, assistant_output, session_phase)
    return format_agent_report(report)


@mcp.tool()
def get_agent_actions() -> str:
    """Get history of autonomous actions the agent has taken.

    Shows what the agent has done without asking, providing
    transparency into its autonomous decision-making.
    """
    from ..core import get_synapse_graph
    import json

    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="agent_action", limit=20)

    if not episodes:
        return "No autonomous actions recorded yet."

    lines = ["Recent Autonomous Actions:", ""]
    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            data = {}

        action_type = data.get("action_type", ep.get("title", "unknown"))
        success = data.get("success", True)
        timestamp = data.get("timestamp", ep.get("timestamp", ""))

        status = "+" if success else "-"
        time_part = timestamp.split("T")[1][:8] if "T" in timestamp else timestamp
        lines.append(f"  [{status}] {time_part} - {action_type}")

    return "\n".join(lines)


@mcp.tool()
def get_agent_patterns() -> str:
    """Get emerging patterns the agent has observed.

    The agent tracks recurring signals and patterns that might
    become wisdom once they're stable enough.
    """
    from .soul_agent import SoulAgent

    agent = SoulAgent()
    patterns = agent._pattern_observations

    if not patterns:
        return "No patterns observed yet."

    lines = ["Emerging Patterns:", ""]
    for pattern, count in sorted(patterns.items(), key=lambda x: -x[1]):
        stability = "stable" if count >= 5 else "forming"
        lines.append(f"  [{count}] {pattern} ({stability})")

    return "\n".join(lines)
