# =============================================================================
# Antahkarana - The Inner Instrument (Multi-Agent Convergence)
#
# In Upanishadic philosophy, Antahkarana is the inner organ of consciousness
# comprising facets: Manas (sensory mind), Buddhi (intellect), Chitta (memory),
# and Ahamkara (ego). Here we spawn real Claude agents as these voices.
# =============================================================================

@mcp.tool()
def awaken_antahkarana(
    problem: str,
    voices: str = "manas,buddhi,ahamkara",
    timeout: int = 300,
    wait: bool = False,
) -> str:
    """Awaken the Antahkarana - spawn Claude voices to contemplate a problem.

    Each voice becomes a separate Claude process that runs independently
    and stores insights in Chitta (cc-memory). After all voices complete,
    harmonize their insights into unified wisdom.

    Args:
        problem: The problem to contemplate
        voices: Comma-separated voices (manas,buddhi,chitta,ahamkara,vikalpa,sakshi)
        timeout: Max seconds to wait for voices (if wait=True)
        wait: Whether to wait for completion

    Voice meanings:
        manas: Sensory mind - quick first impressions, intuitive responses
        buddhi: Intellect - deep analysis, discrimination, thorough reasoning
        chitta: Memory/patterns - practical wisdom from experience
        ahamkara: Ego - self-protective criticism, finding flaws
        vikalpa: Imagination - creative, novel, unconventional approaches
        sakshi: Witness - detached, minimal, essential truth
    """
    from .convergence import InnerVoice
    from .swarm_spawner import spawn_antahkarana

    voice_map = {
        "manas": InnerVoice.MANAS,
        "buddhi": InnerVoice.BUDDHI,
        "chitta": InnerVoice.CHITTA,
        "ahamkara": InnerVoice.AHAMKARA,
        "vikalpa": InnerVoice.VIKALPA,
        "sakshi": InnerVoice.SAKSHI,
    }

    voice_list = [
        voice_map[v.strip().lower()]
        for v in voices.split(",")
        if v.strip().lower() in voice_map
    ]

    if not voice_list:
        voice_list = [InnerVoice.MANAS, InnerVoice.BUDDHI, InnerVoice.AHAMKARA]

    result = spawn_antahkarana(
        problem=problem,
        voices=voice_list,
        wait=wait,
        timeout=timeout,
    )

    lines = [
        f"## Antahkarana Awakened: {result['antahkarana_id']}",
        f"Voices: {result['voices_spawned']}",
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


@mcp.tool()
def get_antahkarana_status(antahkarana_id: str) -> str:
    """Get status of an Antahkarana.

    Args:
        antahkarana_id: The Antahkarana ID
    """
    from .swarm_spawner import get_orchestrator

    orch = get_orchestrator(antahkarana_id)
    if not orch:
        return f"Antahkarana not found: {antahkarana_id}"

    status = orch.get_status()

    lines = [
        f"## Antahkarana: {status['antahkarana_id']}",
        f"Problem: {status['problem']}",
        f"Work dir: {status['work_dir']}",
        "",
        f"Voices ({len(status['voices'])}):",
    ]

    for voice in status["voices"]:
        lines.append(f"  - {voice['task_id']}: {voice['status']} (pid: {voice['pid']})")

    lines.append(f"\nInsights collected: {status['insights']}")

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
        return f"Antahkarana not found: {antahkarana_id}"

    result = orch.wait_for_completion(timeout=timeout)

    lines = [
        f"## Polling Complete: {antahkarana_id}",
        f"Elapsed: {result['elapsed']:.1f}s",
        f"Completed: {result['completed']}",
        f"Failed: {result['failed']}",
        f"Timeout: {result['timeout']}",
    ]

    if orch.antahkarana.insights:
        lines.append("")
        lines.append("Insights collected:")
        for sol in orch.antahkarana.insights:
            lines.append(f"  - {sol.perspective.value}: {sol.confidence:.0%}")

    return "\n".join(lines)


@mcp.tool()
def harmonize_antahkarana(antahkarana_id: str, pramana: str = "samvada") -> str:
    """Harmonize insights from the inner voices.

    After voices complete contemplation, harmonize their insights into
    unified wisdom using a convergence method (pramana).

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
    from .convergence import ConvergenceStrategy
    from .swarm_spawner import get_orchestrator

    orch = get_orchestrator(antahkarana_id)
    if not orch:
        return f"Antahkarana not found: {antahkarana_id}"

    if not orch.antahkarana.insights:
        return "No insights to harmonize. Poll voices first."

    strategy_map = {
        "sankhya": ConvergenceStrategy.SANKHYA,
        "samvada": ConvergenceStrategy.SAMVADA,
        "tarka": ConvergenceStrategy.TARKA,
        "viveka": ConvergenceStrategy.VIVEKA,
        "pratyaksha": ConvergenceStrategy.PRATYAKSHA,
    }

    strat = strategy_map.get(pramana.lower(), ConvergenceStrategy.SAMVADA)
    result = orch.converge(strat)

    return f"""## Harmonized: {antahkarana_id}

Pramana: {result.strategy_used.value}
Shraddha: {result.confidence:.0%}
Contributing voices: {len(result.contributing_voices)}

### Wisdom

{result.final_solution}

### Notes

{result.synthesis_notes}"""


@mcp.tool()
def list_antahkarana_insights(antahkarana_id: str) -> str:
    """List all insights for an Antahkarana from Chitta (cc-memory).

    Voices store their insights in cc-memory with Antahkarana tags.
    This retrieves all insights for a given Antahkarana.

    Args:
        antahkarana_id: The Antahkarana ID to query
    """
    from .swarm_spawner import get_antahkarana_insights

    insights = get_antahkarana_insights(antahkarana_id)

    if not insights:
        return f"No insights found for Antahkarana: {antahkarana_id}\n\nVoices may still be contemplating."

    lines = [f"## Antahkarana Insights: {antahkarana_id}", f"Found {len(insights)} insights", ""]

    for sol in insights:
        lines.extend([
            f"### {sol['task_id']} ({sol['perspective']})",
            f"Shraddha: {sol['confidence']:.0%}",
            f"Observation ID: #{sol['observation_id']}",
            "",
            sol['insight'][:300] + ("..." if len(sol['insight']) > 300 else ""),
            "",
        ])

    return "\n".join(lines)


@mcp.tool()
def list_antahkaranas(limit: int = 5) -> str:
    """List active Antahkaranas (inner instruments).

    Args:
        limit: Maximum to return
    """
    from .swarm_spawner import list_active_antahkaranas

    antahkaranas = list_active_antahkaranas(limit)

    if not antahkaranas:
        return "No active Antahkaranas."

    lines = ["Active Antahkaranas:", ""]
    for a in antahkaranas:
        lines.append(f"  {a['antahkarana_id']}: {a['problem'][:50]}... ({a['voices']} voices)")

    return "\n".join(lines)
