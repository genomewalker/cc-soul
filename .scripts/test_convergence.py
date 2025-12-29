#!/usr/bin/env python3
"""
Test the multi-agent convergence system.

Simulates multiple agents working on a problem and converging their solutions.
"""

import sys
sys.path.insert(0, '/maps/projects/fernandezguerra/apps/repos/cc-soul/src')

from cc_soul import (
    Swarm,
    AgentPerspective,
    ConvergenceStrategy,
    spawn_parallel_agents,
    list_active_swarms,
)


def test_create_swarm():
    """Test creating a swarm with multiple perspectives."""
    print("\n1. Creating a swarm...")

    swarm = Swarm(
        problem="How should we implement caching for the API endpoints?",
        constraints=["Must be thread-safe", "Should work with Redis", "Max latency 10ms"],
        context="We have a Python FastAPI backend with PostgreSQL database.",
    )

    print(f"   Swarm ID: {swarm.swarm_id}")
    print(f"   Problem: {swarm.problem[:50]}...")

    return swarm


def test_add_agents(swarm):
    """Test adding agents with different perspectives."""
    print("\n2. Adding agents with different perspectives...")

    swarm.add_agent(AgentPerspective.FAST)
    swarm.add_agent(AgentPerspective.DEEP)
    swarm.add_agent(AgentPerspective.CRITICAL)
    swarm.add_agent(AgentPerspective.PRAGMATIC)

    print(f"   Added {len(swarm.tasks)} agents")
    for task in swarm.tasks:
        print(f"   - {task.perspective.value}: {task.task_id}")

    return swarm


def test_spawn_agents(swarm):
    """Test spawning agents (creates placeholders for real agent work)."""
    print("\n3. Spawning agents...")

    agent_ids = swarm.spawn_all()
    print(f"   Spawned {len(agent_ids)} agents")

    return agent_ids


def test_submit_solutions(swarm):
    """Simulate agents submitting their solutions."""
    print("\n4. Simulating agent solutions...")

    solutions_data = [
        {
            "perspective": AgentPerspective.FAST,
            "solution": "Use Redis with a simple key-value cache. Set TTL to 60 seconds. Use @cache decorator on endpoints.",
            "confidence": 0.7,
            "reasoning": "Simple is fast. Start here and optimize later.",
        },
        {
            "perspective": AgentPerspective.DEEP,
            "solution": """Implement a multi-layer caching strategy:

1. L1: In-memory LRU cache (per-process, 1000 items max)
2. L2: Redis cluster with read replicas
3. Cache invalidation via pub/sub on write operations
4. Use cache-aside pattern with async warming

Consider:
- Serialization format: MessagePack for speed
- Key generation: SHA256 of normalized request
- TTL: Adaptive based on hit rate""",
            "confidence": 0.85,
            "reasoning": "Thorough analysis of scaling requirements and failure modes.",
        },
        {
            "perspective": AgentPerspective.CRITICAL,
            "solution": """Concerns to address:

1. Cache stampede: Multiple threads hitting cold cache simultaneously
   → Solution: Probabilistic early expiration or mutex per key

2. Stale data: Serving outdated responses after DB updates
   → Solution: Event-driven invalidation, not just TTL

3. Memory pressure: Unbounded caches can cause OOM
   → Solution: Memory limits + eviction policy

4. Serialization bugs: Complex objects may not serialize cleanly
   → Solution: Explicit schema validation on cache entries""",
            "confidence": 0.75,
            "reasoning": "Identified potential failure modes that need handling.",
        },
        {
            "perspective": AgentPerspective.PRAGMATIC,
            "solution": """Week 1: Implement basic Redis caching with @cache decorator
Week 2: Add cache invalidation on writes
Week 3: Measure hit rate, tune TTL
Week 4: Add L1 in-memory layer if needed

Use existing libraries: redis-py, cachetools for L1
Don't build from scratch - use proven patterns.""",
            "confidence": 0.8,
            "reasoning": "Incremental approach that can ship quickly and improve iteratively.",
        },
    ]

    for data in solutions_data:
        # Find matching task
        task = next(t for t in swarm.tasks if t.perspective == data["perspective"])
        swarm.submit_solution(
            task_id=task.task_id,
            solution=data["solution"],
            confidence=data["confidence"],
            reasoning=data["reasoning"],
        )
        print(f"   Submitted: {data['perspective'].value} ({data['confidence']:.0%} confidence)")

    return swarm


def test_converge_vote(swarm):
    """Test voting convergence."""
    print("\n5. Converging with VOTE strategy...")

    result = swarm.converge(ConvergenceStrategy.VOTE)
    print(f"   Strategy: {result.strategy_used.value}")
    print(f"   Winner: {result.contributing_agents[0]}")
    print(f"   Confidence: {result.confidence:.0%}")
    print(f"   Notes: {result.synthesis_notes}")

    return result


def test_converge_synthesize(swarm):
    """Test synthesis convergence."""
    print("\n6. Converging with SYNTHESIZE strategy...")

    result = swarm.converge(ConvergenceStrategy.SYNTHESIZE)
    print(f"   Strategy: {result.strategy_used.value}")
    print(f"   Contributors: {len(result.contributing_agents)}")
    print(f"   Confidence: {result.confidence:.0%}")
    print(f"   Notes: {result.synthesis_notes}")
    print(f"\n   Final solution preview:")
    for line in result.final_solution.split('\n')[:10]:
        print(f"   {line}")
    if len(result.final_solution.split('\n')) > 10:
        print("   ...")

    return result


def test_converge_debate(swarm):
    """Test debate convergence."""
    print("\n7. Converging with DEBATE strategy...")

    result = swarm.converge(ConvergenceStrategy.DEBATE)
    print(f"   Strategy: {result.strategy_used.value}")
    print(f"   Debate rounds: {result.debate_rounds}")
    print(f"   Dissenting views: {len(result.dissenting_views)}")

    return result


def test_quick_spawn():
    """Test the quick spawn helper."""
    print("\n8. Testing spawn_parallel_agents() helper...")

    swarm = spawn_parallel_agents(
        problem="What's the best way to handle authentication?",
        perspectives=[AgentPerspective.FAST, AgentPerspective.NOVEL],
        constraints=["Must support OAuth2", "Should be stateless"],
    )

    print(f"   Created swarm: {swarm.swarm_id}")
    print(f"   Tasks: {len(swarm.tasks)}")

    return swarm


def test_list_swarms():
    """Test listing active swarms (antahkaranas)."""
    print("\n9. Listing active antahkaranas...")

    minds = list_active_swarms(limit=5)
    print(f"   Found {len(minds)} antahkaranas:")
    for m in minds:
        # Handle both old and new key names
        mind_id = m.get('antahkarana_id') or m.get('swarm_id')
        insights = m.get('insights') or m.get('solutions', 0)
        print(f"   - {mind_id}: {m['problem'][:40]}... ({insights} insights)")


def main():
    print("=" * 60)
    print("Multi-Agent Convergence Test Suite")
    print("=" * 60)

    # Create and configure swarm
    swarm = test_create_swarm()
    swarm = test_add_agents(swarm)

    # Spawn agents (creates tasks)
    test_spawn_agents(swarm)

    # Simulate agent work
    test_submit_solutions(swarm)

    # Test different convergence strategies
    test_converge_vote(swarm)
    test_converge_synthesize(swarm)
    test_converge_debate(swarm)

    # Test helper function
    test_quick_spawn()

    # List all swarms
    test_list_swarms()

    print("\n" + "=" * 60)
    print("All tests completed!")
    print("=" * 60)


if __name__ == "__main__":
    main()
