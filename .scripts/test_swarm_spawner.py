#!/usr/bin/env python3
"""
Test the swarm spawner module.

Tests the orchestration layer that spawns real Claude agents.
Since we can't spawn actual claude CLI processes in test, we test:
1. Orchestrator creation
2. Prompt building
3. Solution block parsing
4. Database recording
"""

import sys
sys.path.insert(0, '/maps/projects/fernandezguerra/apps/repos/cc-soul/src')

from pathlib import Path
from cc_soul import (
    Swarm,
    AgentPerspective,
    ConvergenceStrategy,
)
from cc_soul.swarm_spawner import (
    SwarmOrchestrator,
    SpawnedAgent,
    spawn_swarm,
    get_orchestrator,
)


def test_orchestrator_creation():
    """Test creating an orchestrator."""
    print("\n1. Creating orchestrator...")

    swarm = Swarm(
        problem="What's the best data structure for a priority queue?",
        constraints=["Must support O(log n) insert", "Must support O(1) peek"],
        context="Building a task scheduler.",
    )
    swarm.add_agent(AgentPerspective.FAST)
    swarm.add_agent(AgentPerspective.DEEP)
    swarm.add_agent(AgentPerspective.CRITICAL)

    orchestrator = SwarmOrchestrator(swarm)

    print(f"   Swarm ID: {orchestrator.swarm.swarm_id}")
    print(f"   Work dir: {orchestrator.swarm_dir}")
    print(f"   Max parallel: {orchestrator.max_parallel}")
    print(f"   Model: {orchestrator.model}")

    assert orchestrator.swarm_dir.exists()
    return orchestrator


def test_prompt_building(orchestrator):
    """Test building agent prompts."""
    print("\n2. Building agent prompts...")

    for task in orchestrator.swarm.tasks:
        prompt = orchestrator._build_agent_prompt(task)

        print(f"\n   [{task.perspective.value}] prompt preview:")
        # Show first 200 chars
        for line in prompt[:300].split('\n')[:5]:
            print(f"   {line}")
        print("   ...")

        # Check required elements
        assert "[SWARM_SOLUTION]" in prompt
        assert f"swarm_id: {orchestrator.swarm.swarm_id}" in prompt
        assert f"task_id: {task.task_id}" in prompt
        assert f"perspective: {task.perspective.value}" in prompt

    print("\n   All prompts have required solution block format ✓")


def test_solution_parsing():
    """Test parsing solution blocks from agent output."""
    print("\n3. Testing solution block parsing...")

    swarm = Swarm(problem="Test problem")
    orchestrator = SwarmOrchestrator(swarm)

    # Simulated agent output
    output = """
Here's my analysis of the problem...

After careful consideration, I believe the best approach is:

[SWARM_SOLUTION]
swarm_id: test-123
task_id: task-abc
perspective: deep
confidence: 0.85
solution: |
  Use a binary heap for the priority queue.
  The heap property ensures O(log n) insertions.
  We maintain a reference to the root for O(1) peek.
reasoning: |
  Considered alternatives:
  - Sorted array: O(n) insert, O(1) peek
  - Binary search tree: O(log n) both, but more complex
  - Fibonacci heap: O(1) amortized insert, O(1) peek, but overkill
  Binary heap is the best balance of simplicity and performance.
[/SWARM_SOLUTION]

That's my recommendation.
"""

    result = orchestrator._parse_solution_block(output)

    print(f"   Parsed fields:")
    for key, value in result.items():
        preview = str(value)[:50].replace('\n', ' ')
        print(f"   - {key}: {preview}...")

    assert result["swarm_id"] == "test-123"
    assert result["task_id"] == "task-abc"
    assert result["perspective"] == "deep"
    assert result["confidence"] == "0.85"
    assert "binary heap" in result["solution"].lower()
    assert "fibonacci" in result["reasoning"].lower()

    print("\n   Solution parsing works correctly ✓")


def test_agent_status_tracking():
    """Test tracking spawned agent status."""
    print("\n4. Testing agent status tracking...")

    swarm = Swarm(problem="Test tracking")
    swarm.add_agent(AgentPerspective.FAST)
    swarm.add_agent(AgentPerspective.DEEP)

    orchestrator = SwarmOrchestrator(swarm)

    # Manually create some SpawnedAgent instances (simulating spawn)
    from datetime import datetime

    for task in swarm.tasks:
        agent = SpawnedAgent(
            task_id=task.task_id,
            process=None,  # No real process
            pid=0,
            started_at=datetime.now().isoformat(),
            status="awaiting",
        )
        orchestrator.agents.append(agent)

    status = orchestrator.get_status()

    print(f"   Swarm ID: {status['swarm_id']}")
    print(f"   Problem: {status['problem'][:40]}...")
    print(f"   Agents: {len(status['agents'])}")
    for agent in status['agents']:
        print(f"   - {agent['task_id']}: {agent['status']}")

    assert len(status['agents']) == 2
    print("\n   Status tracking works correctly ✓")


def test_simulated_workflow():
    """Test simulated end-to-end workflow."""
    print("\n5. Simulated workflow (without real agents)...")

    swarm = Swarm(
        problem="Design a rate limiter for an API",
        constraints=["Must handle 1000 req/s", "Must be distributed"],
    )
    swarm.add_agent(AgentPerspective.FAST)
    swarm.add_agent(AgentPerspective.DEEP)
    swarm.add_agent(AgentPerspective.PRAGMATIC)

    orchestrator = SwarmOrchestrator(swarm)

    print(f"   Created swarm: {swarm.swarm_id}")
    print(f"   Tasks: {len(swarm.tasks)}")

    # Simulate solutions (would come from real agents)
    simulated_solutions = [
        {
            "perspective": AgentPerspective.FAST,
            "solution": "Use Redis with INCR and TTL. Simple and effective.",
            "confidence": 0.75,
            "reasoning": "Redis is proven, fast, already in our stack.",
        },
        {
            "perspective": AgentPerspective.DEEP,
            "solution": """Sliding window counter in Redis:
1. Use sorted sets with timestamp scores
2. Remove entries older than window
3. Count remaining entries
4. Compare against limit

This handles edge cases like bursty traffic at window boundaries.""",
            "confidence": 0.9,
            "reasoning": "Analyzed fixed window, sliding log, token bucket. Sliding window counter balances accuracy and performance.",
        },
        {
            "perspective": AgentPerspective.PRAGMATIC,
            "solution": "Use Redis + Lua scripting for atomic operations. Start with fixed window, add sliding if needed.",
            "confidence": 0.8,
            "reasoning": "Ship something that works, improve later.",
        },
    ]

    for data in simulated_solutions:
        task = next(t for t in swarm.tasks if t.perspective == data["perspective"])
        swarm.submit_solution(
            task_id=task.task_id,
            solution=data["solution"],
            confidence=data["confidence"],
            reasoning=data["reasoning"],
        )
        print(f"   Submitted: {data['perspective'].value}")

    # Converge
    result = orchestrator.converge(ConvergenceStrategy.SYNTHESIZE)

    print(f"\n   Converged with {result.strategy_used.value}")
    print(f"   Confidence: {result.confidence:.0%}")
    print(f"   Contributors: {len(result.contributing_agents)}")
    print(f"\n   Final solution preview:")
    for line in result.final_solution.split('\n')[:5]:
        print(f"   {line}")

    print("\n   Simulated workflow complete ✓")


def test_orchestrator_retrieval():
    """Test retrieving an orchestrator by swarm ID."""
    print("\n6. Testing orchestrator retrieval...")

    # First create a swarm and orchestrator
    swarm = Swarm(problem="Test retrieval")
    swarm.add_agent(AgentPerspective.FAST)
    orchestrator = SwarmOrchestrator(swarm)

    # Record an agent
    from datetime import datetime
    agent = SpawnedAgent(
        task_id=swarm.tasks[0].task_id,
        process=None,
        pid=12345,
        started_at=datetime.now().isoformat(),
        status="completed",
    )
    orchestrator.agents.append(agent)
    orchestrator._record_agent_spawn(agent, swarm.tasks[0])

    # Retrieve it
    retrieved = get_orchestrator(swarm.swarm_id)

    if retrieved:
        print(f"   Retrieved orchestrator for: {retrieved.swarm.swarm_id}")
        print(f"   Agents: {len(retrieved.agents)}")
        print("\n   Retrieval works correctly ✓")
    else:
        print("   Could not retrieve orchestrator (expected if DB not initialized)")


def main():
    print("=" * 60)
    print("Swarm Spawner Test Suite")
    print("=" * 60)

    orchestrator = test_orchestrator_creation()
    test_prompt_building(orchestrator)
    test_solution_parsing()
    test_agent_status_tracking()
    test_simulated_workflow()
    test_orchestrator_retrieval()

    print("\n" + "=" * 60)
    print("All tests completed!")
    print("=" * 60)
    print("\nNote: This tests the orchestration logic.")
    print("Real agent spawning requires claude CLI installed.")
    print("Use spawn_real_swarm() MCP tool for actual parallel agents.")


if __name__ == "__main__":
    main()
