# =============================================================================
# Orchestration - Spawning Real Claude Voices
#
# When the Antahkarana awakens with real voices, each voice becomes a separate
# Claude process. They contemplate independently in Chitta (cc-memory), then
# harmonize their insights.
# =============================================================================

@mcp.tool()
def spawn_real_antahkarana(
    problem: str,
    voices: str = "manas,buddhi,ahamkara",
    timeout: int = 300,
    wait: bool = False,
) -> str:
    """Spawn real Claude voices to contemplate a problem.

    Unlike awaken_antahkarana (simulation), this spawns actual Claude CLI
    processes. Each voice runs independently and stores insights in Chitta.

    Args:
        problem: The problem to contemplate
        voices: Comma-separated voices (manas,buddhi,chitta,ahamkara,vikalpa,sakshi)
        timeout: Max seconds to wait for voices (if wait=True)
        wait: Whether to wait for completion
    """
    from .convergence import InnerVoice
    from .swarm_spawner import spawn_swarm

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

    voice_list = [
        voice_map[v.strip().lower()]
        for v in voices.split(",")
        if v.strip().lower() in voice_map
    ]

    if not voice_list:
        voice_list = [InnerVoice.MANAS, InnerVoice.BUDDHI, InnerVoice.AHAMKARA]

    result = spawn_swarm(
        problem=problem,
        perspectives=voice_list,
        wait=wait,
        timeout=timeout,
    )

    lines = [
        f"## Antahkarana Awakened: {result['swarm_id']}",
        f"Voices: {result['agents_spawned']}",
        f"Work dir: {result['status']['work_dir']}",
    ]

    if "completion" in result:
        lines.append("")
        lines.append("Completion:")
        lines.append(f"  Completed: {len(result['completion']['completed'])}")
        lines.append(f"  Failed: {len(result['completion']['failed'])}")
        lines.append(f"  Timeout: {len(result['completion']['timeout'])}")
        lines.append(f"  Elapsed: {result['completion']['elapsed']:.1f}s")

    if "converged" in result:
        lines.append("")
        lines.append("Harmonized:")
        lines.append(f"  Pramana: {result['converged']['strategy']}")
        lines.append(f"  Shraddha: {result['converged']['confidence']:.0%}")
        lines.append(f"  Wisdom: {result['converged']['solution'][:200]}...")

    return "\n".join(lines)


# Backward compatibility alias


@mcp.tool()
def spawn_real_swarm(
    problem: str,
    perspectives: str = "fast,deep,critical",
    timeout: int = 300,
    wait: bool = False,
) -> str:
    """Spawn real agents (alias for spawn_real_antahkarana).

    Args:
        problem: The problem to solve
        perspectives: Comma-separated perspectives
        timeout: Max seconds to wait
        wait: Whether to wait for completion
    """
    return spawn_real_antahkarana(problem, perspectives, timeout, wait)


@mcp.tool()
def get_orchestrator_status(antahkarana_id: str) -> str:
    """Get status of a real Antahkarana orchestrator.

    Args:
        antahkarana_id: The Antahkarana ID
    """
    from .swarm_spawner import get_orchestrator

    orch = get_orchestrator(antahkarana_id)
    if not orch:
        return f"Orchestrator not found: {antahkarana_id}"

    status = orch.get_status()

    lines = [
        f"## Orchestrator: {status['swarm_id']}",
        f"Problem: {status['problem']}",
        f"Work dir: {status['work_dir']}",
        "",
        f"Voices ({len(status['agents'])}):",
    ]

    for agent in status["agents"]:
        lines.append(f"  - {agent['task_id']}: {agent['status']} (pid: {agent['pid']})")

    lines.append(f"\nInsights collected: {status['solutions']}")

    return "\n".join(lines)


@mcp.tool()
def poll_antahkarana_voices(antahkarana_id: str, timeout: int = 60) -> str:
    """Wait for Antahkarana voices to complete contemplation.

    Args:
        antahkarana_id: The Antahkarana ID
        timeout: Max seconds to wait
    """
    from .swarm_spawner import get_orchestrator

    orch = get_orchestrator(antahkarana_id)
    if not orch:
        return f"Orchestrator not found: {antahkarana_id}"

    result = orch.wait_for_completion(timeout=timeout)

    lines = [
        f"## Polling Complete: {antahkarana_id}",
        f"Elapsed: {result['elapsed']:.1f}s",
        f"Completed: {result['completed']}",
        f"Failed: {result['failed']}",
        f"Timeout: {result['timeout']}",
    ]

    if orch.swarm.insights:
        lines.append("")
        lines.append("Insights collected:")
        for sol in orch.swarm.insights:
            lines.append(f"  - {sol.perspective.value}: {sol.confidence:.0%}")

    return "\n".join(lines)


# Backward compatibility alias


@mcp.tool()
def poll_swarm_agents(swarm_id: str, timeout: int = 60) -> str:
    """Wait for swarm agents (alias for poll_antahkarana_voices).

    Args:
        swarm_id: The swarm ID
        timeout: Max seconds to wait
    """
    return poll_antahkarana_voices(swarm_id, timeout)


@mcp.tool()
def harmonize_real_antahkarana(antahkarana_id: str, pramana: str = "samvada") -> str:
    """Harmonize insights from real Antahkarana voices.

    Args:
        antahkarana_id: The Antahkarana ID
        pramana: Convergence method (sankhya, samvada, tarka, viveka)
    """
    from .convergence import ConvergenceStrategy
    from .swarm_spawner import get_orchestrator

    orch = get_orchestrator(antahkarana_id)
    if not orch:
        return f"Orchestrator not found: {antahkarana_id}"

    if not orch.swarm.insights:
        return "No insights to harmonize. Poll voices first."

    strategy_map = {
        "sankhya": ConvergenceStrategy.SANKHYA,
        "samvada": ConvergenceStrategy.SAMVADA,
        "tarka": ConvergenceStrategy.TARKA,
        "viveka": ConvergenceStrategy.VIVEKA,
        # Backward compat
        "vote": ConvergenceStrategy.SANKHYA,
        "synthesize": ConvergenceStrategy.SAMVADA,
        "debate": ConvergenceStrategy.TARKA,
        "rank": ConvergenceStrategy.VIVEKA,
    }

    strat = strategy_map.get(pramana.lower(), ConvergenceStrategy.SAMVADA)
    result = orch.converge(strat)

    return f"""## Harmonized: {antahkarana_id}

Pramana: {result.pramana_used.value}
Shraddha: {result.shraddha:.0%}
Contributing voices: {len(result.contributing_voices)}

### Wisdom

{result.wisdom}

### Notes

{result.synthesis_notes}"""


# Backward compatibility alias


@mcp.tool()
def converge_real_swarm(swarm_id: str, strategy: str = "synthesize") -> str:
    """Converge real swarm (alias for harmonize_real_antahkarana).

    Args:
        swarm_id: The swarm ID
        strategy: Convergence strategy
    """
    return harmonize_real_antahkarana(swarm_id, strategy)


@mcp.tool()
def list_antahkarana_insights(antahkarana_id: str) -> str:
    """List all insights for an Antahkarana from Chitta (cc-memory).

    Voices store their insights in cc-memory with Antahkarana tags.
    This retrieves all insights for a given Antahkarana.

    Args:
        antahkarana_id: The Antahkarana ID to query
    """
    from .swarm_spawner import get_swarm_solutions

    insights = get_swarm_solutions(antahkarana_id)

    if not insights:
        return f"No insights found for Antahkarana: {antahkarana_id}\n\nVoices may still be contemplating."

    lines = [f"## Antahkarana Insights: {antahkarana_id}", f"Found {len(insights)} insights", ""]

    for sol in insights:
        lines.extend([
            f"### {sol['task_id']} ({sol['perspective']})",
            f"Shraddha: {sol['confidence']:.0%}",
            f"Observation ID: #{sol['observation_id']}",
            "",
            sol['solution'][:300] + ("..." if len(sol['solution']) > 300 else ""),
            "",
        ])

    return "\n".join(lines)


# Backward compatibility alias


@mcp.tool()
def list_swarm_solutions(swarm_id: str) -> str:
    """List swarm solutions (alias for list_antahkarana_insights).

    Args:
        swarm_id: The swarm ID to query
    """
    return list_antahkarana_insights(swarm_id)
