# =============================================================================
# Antahkarana - The Inner Instrument (Multi-Agent Convergence)
#
# In Upanishadic philosophy, Antahkarana is the inner organ of consciousness
# comprising facets: Manas (sensory mind), Buddhi (intellect), Chitta (memory),
# and Ahamkara (ego).
# =============================================================================

@mcp.tool()
def awaken_antahkarana(
    problem: str,
    voices: str = "manas,buddhi,ahamkara",
    constraints: str = "",
) -> str:
    """Awaken the Antahkarana - the inner instrument of consciousness.

    Invokes multiple inner voices to contemplate a problem. Each voice
    approaches it from its unique nature, then insights harmonize.

    Args:
        problem: The problem to contemplate
        voices: Comma-separated voices (manas, buddhi, chitta, ahamkara, vikalpa, sakshi)
        constraints: Comma-separated constraints for the problem

    Voice meanings:
        manas: Sensory mind - quick first impressions, intuitive responses
        buddhi: Intellect - deep analysis, discrimination, thorough reasoning
        chitta: Memory/patterns - practical wisdom from experience
        ahamkara: Ego - self-protective criticism, finding flaws
        vikalpa: Imagination - creative, novel, unconventional approaches
        sakshi: Witness - detached, minimal, essential truth
    """
    from .convergence import Antahkarana, InnerVoice

    voice_map = {
        "manas": InnerVoice.MANAS,
        "buddhi": InnerVoice.BUDDHI,
        "chitta": InnerVoice.CHITTA,
        "ahamkara": InnerVoice.AHAMKARA,
        "vikalpa": InnerVoice.VIKALPA,
        "sakshi": InnerVoice.SAKSHI,
        # Backward compat
        "fast": InnerVoice.MANAS,
        "deep": InnerVoice.BUDDHI,
        "pragmatic": InnerVoice.CHITTA,
        "critical": InnerVoice.AHAMKARA,
        "novel": InnerVoice.VIKALPA,
        "minimal": InnerVoice.SAKSHI,
    }

    constraint_list = [c.strip() for c in constraints.split(",") if c.strip()]

    antahkarana = Antahkarana(problem=problem, constraints=constraint_list)

    for v in voices.split(","):
        v = v.strip().lower()
        if v in voice_map:
            antahkarana.add_voice(voice_map[v])

    antahkarana.activate_all()

    return f"""Antahkarana awakened: {antahkarana.antahkarana_id}

Problem: {problem[:80]}...
Voices: {len(antahkarana.tasks)} ({voices})

To submit insights, use: submit_insight
To harmonize, use: harmonize_antahkarana"""


# Backward compatibility alias


@mcp.tool()
def create_swarm(
    problem: str,
    perspectives: str = "fast,deep,critical",
    constraints: str = "",
) -> str:
    """Create a swarm of agents (alias for awaken_antahkarana).

    Args:
        problem: The problem statement to solve
        perspectives: Comma-separated perspectives (fast, deep, critical, novel, pragmatic, minimal)
        constraints: Comma-separated constraints for the problem
    """
    return awaken_antahkarana(problem, perspectives, constraints)


@mcp.tool()
def submit_insight(
    antahkarana_id: str,
    task_index: int,
    insight: str,
    shraddha: float = 0.7,
    reasoning: str = "",
) -> str:
    """Submit an insight from an inner voice.

    Args:
        antahkarana_id: The Antahkarana ID
        task_index: Which voice task (0-based index)
        insight: The contemplated insight
        shraddha: Confidence/faith level 0.0-1.0
        reasoning: Why this insight emerged
    """
    from .convergence import get_antahkarana

    antahkarana = get_antahkarana(antahkarana_id)
    if not antahkarana:
        return f"Antahkarana not found: {antahkarana_id}"

    if task_index >= len(antahkarana.tasks):
        return f"Invalid task index. Antahkarana has {len(antahkarana.tasks)} voices."

    task = antahkarana.tasks[task_index]
    sol = antahkarana.submit_insight(
        task_id=task.task_id,
        solution=insight,
        confidence=shraddha,
        reasoning=reasoning,
    )

    return f"Insight submitted from {sol.perspective.value} ({sol.confidence:.0%} shraddha)"


# Backward compatibility alias


@mcp.tool()
def submit_swarm_solution(
    swarm_id: str,
    task_index: int,
    solution: str,
    confidence: float = 0.7,
    reasoning: str = "",
) -> str:
    """Submit a solution for a swarm task (alias for submit_insight).

    Args:
        swarm_id: The swarm ID
        task_index: Which task (0-based index)
        solution: The proposed solution
        confidence: Confidence level 0.0-1.0
        reasoning: Why this solution works
    """
    return submit_insight(swarm_id, task_index, solution, confidence, reasoning)


@mcp.tool()
def harmonize_antahkarana(antahkarana_id: str, pramana: str = "samvada") -> str:
    """Harmonize insights from the inner voices.

    Args:
        antahkarana_id: The Antahkarana ID
        pramana: Convergence method (sankhya, samvada, tarka, viveka, pratyaksha)

    Pramana (means of knowledge):
        sankhya: Enumeration - highest shraddha wins
        samvada: Dialogue - synthesize wisdom from all voices
        tarka: Dialectic - iterative refinement through challenge
        viveka: Discernment - score and rank by criteria
        pratyaksha: Direct perception - first valid insight
    """
    from .convergence import get_antahkarana, ConvergenceStrategy

    antahkarana = get_antahkarana(antahkarana_id)
    if not antahkarana:
        return f"Antahkarana not found: {antahkarana_id}"

    if not antahkarana.insights:
        return "No insights to harmonize. Submit insights first."

    strategy_map = {
        "sankhya": ConvergenceStrategy.SANKHYA,
        "samvada": ConvergenceStrategy.SAMVADA,
        "tarka": ConvergenceStrategy.TARKA,
        "viveka": ConvergenceStrategy.VIVEKA,
        "pratyaksha": ConvergenceStrategy.PRATYAKSHA,
        # Backward compat
        "vote": ConvergenceStrategy.SANKHYA,
        "synthesize": ConvergenceStrategy.SAMVADA,
        "debate": ConvergenceStrategy.TARKA,
        "rank": ConvergenceStrategy.VIVEKA,
        "first_valid": ConvergenceStrategy.PRATYAKSHA,
    }

    strat = strategy_map.get(pramana.lower(), ConvergenceStrategy.SAMVADA)
    result = antahkarana.harmonize(strat)

    lines = [
        f"## Harmonized Wisdom ({result.strategy_used.value})",
        "",
        result.final_solution,
        "",
        f"---",
        f"Shraddha: {result.confidence:.0%}",
        f"Contributing voices: {len(result.contributing_voices)}",
        f"Notes: {result.synthesis_notes}",
    ]

    if result.dissenting_views:
        lines.append("")
        lines.append("Dissenting views:")
        for view in result.dissenting_views[:2]:
            lines.append(f"  - {view[:80]}...")

    return "\n".join(lines)


# Backward compatibility alias


@mcp.tool()
def converge_swarm(swarm_id: str, strategy: str = "synthesize") -> str:
    """Converge swarm solutions (alias for harmonize_antahkarana).

    Args:
        swarm_id: The swarm ID
        strategy: Convergence strategy (vote, synthesize, debate, rank, first_valid)
    """
    return harmonize_antahkarana(swarm_id, strategy)


@mcp.tool()
def list_antahkaranas(limit: int = 5) -> str:
    """List active Antahkaranas (inner instruments).

    Args:
        limit: Maximum to return
    """
    from .convergence import list_active_antahkaranas

    minds = list_active_antahkaranas(limit)

    if not minds:
        return "No active Antahkaranas."

    lines = ["Active Antahkaranas:", ""]
    for m in minds:
        lines.append(f"  {m['antahkarana_id']}: {m['problem'][:50]}... ({m['insights']} insights)")

    return "\n".join(lines)


# Backward compatibility alias


@mcp.tool()
def list_swarms(limit: int = 5) -> str:
    """List active swarms (alias for list_antahkaranas).

    Args:
        limit: Maximum swarms to return
    """
    return list_antahkaranas(limit)


@mcp.tool()
def get_antahkarana_status(antahkarana_id: str) -> str:
    """Get status of an Antahkarana.

    Args:
        antahkarana_id: The Antahkarana ID
    """
    from .convergence import get_antahkarana

    antahkarana = get_antahkarana(antahkarana_id)
    if not antahkarana:
        return f"Antahkarana not found: {antahkarana_id}"

    lines = [
        f"## Antahkarana: {antahkarana.antahkarana_id}",
        "",
        f"Problem: {antahkarana.problem[:100]}",
        "",
        f"Voices ({len(antahkarana.tasks)}):",
    ]

    for i, task in enumerate(antahkarana.tasks):
        has_insight = any(s.task_id == task.task_id for s in antahkarana.insights)
        status = "✓" if has_insight else "contemplating"
        lines.append(f"  {i}. [{task.perspective.value}] {status}")

    if antahkarana.insights:
        lines.append("")
        lines.append(f"Insights ({len(antahkarana.insights)}):")
        for sol in antahkarana.insights:
            lines.append(f"  - {sol.perspective.value}: {sol.confidence:.0%} shraddha")

    return "\n".join(lines)


# Backward compatibility alias


@mcp.tool()
def get_swarm_status(swarm_id: str) -> str:
    """Get status of a swarm (alias for get_antahkarana_status).

    Args:
        swarm_id: The swarm ID
    """
    return get_antahkarana_status(swarm_id)


# =============================================================================
# Real Antahkarana Orchestration - Spawning Claude Voices
#
# When the Antahkarana awakens with real voices, each voice becomes a separate
# Claude process. They contemplate independently in Chitta (cc-memory), then
# harmonize their insights.
# =============================================================================
