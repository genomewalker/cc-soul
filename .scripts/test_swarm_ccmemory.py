#!/usr/bin/env python3
"""
Test swarm spawner with cc-memory integration.

Tests that:
1. Agent prompts include cc-memory instructions
2. get_swarm_solutions returns empty for non-existent swarm
3. Spawn command doesn't use --print flag
4. Orchestrator can detect solutions from cc-memory (if available)
"""

import sys
sys.path.insert(0, '/maps/projects/fernandezguerra/apps/repos/cc-soul/src')

from cc_soul.convergence import (
    Swarm, AgentTask, AgentPerspective, spawn_parallel_agents
)
from cc_soul.swarm_spawner import (
    SwarmOrchestrator, get_swarm_solutions
)


def test_agent_prompt_includes_ccmemory():
    """Test that agent prompts include cc-memory instructions."""
    swarm = spawn_parallel_agents(
        problem="Test problem",
        perspectives=[AgentPerspective.FAST],
    )

    orchestrator = SwarmOrchestrator(swarm)
    task = swarm.tasks[0]
    prompt = orchestrator._build_agent_prompt(task)

    # Check for mem-recall instructions
    assert "mem-recall" in prompt, "Prompt should include mem-recall instructions"

    # Check for mem-remember instructions
    assert "mem-remember" in prompt, "Prompt should include mem-remember instructions"

    # Check for swarm tag format
    assert f"swarm:{swarm.swarm_id}" in prompt, "Prompt should include swarm tag"

    # Check for task tag format
    assert f"task:{task.task_id}" in prompt, "Prompt should include task tag"

    print("✓ Agent prompts include cc-memory instructions")


def test_get_swarm_solutions_empty():
    """Test querying non-existent swarm returns empty list."""
    solutions = get_swarm_solutions("nonexistent-swarm-id-12345")
    assert solutions == [], "Should return empty list for non-existent swarm"
    print("✓ get_swarm_solutions returns empty for non-existent swarm")


def test_command_no_print_flag():
    """Test that spawn command doesn't use --print flag."""
    swarm = spawn_parallel_agents(
        problem="Test problem",
        perspectives=[AgentPerspective.FAST],
    )

    orchestrator = SwarmOrchestrator(swarm)

    # Check the spawn_agent method source doesn't have --print
    import inspect
    source = inspect.getsource(orchestrator.spawn_agent)

    assert "--print" not in source, "spawn_agent should not use --print flag"
    print("✓ Spawn command does not use --print flag")


def test_ccmemory_integration_available():
    """Test that cc-memory integration is properly set up."""
    from cc_soul.bridge import is_memory_available

    available = is_memory_available()
    if available:
        print("✓ cc-memory is available and integrated")
    else:
        print("⚠ cc-memory not installed (swarm solutions will use fallback)")


def test_query_method_uses_ccmemory():
    """Test that _query_cc_memory_for_solution uses cc-memory API."""
    import inspect
    from cc_soul.swarm_spawner import SwarmOrchestrator

    source = inspect.getsource(SwarmOrchestrator._query_cc_memory_for_solution)

    # Should use bridge functions
    assert "is_memory_available" in source, "Should check if memory is available"
    assert "cc_memory.recall" in source, "Should use cc_memory.recall"

    print("✓ Query method uses cc-memory API")


def test_get_swarm_solutions_uses_ccmemory():
    """Test that get_swarm_solutions uses cc-memory API."""
    import inspect
    from cc_soul.swarm_spawner import get_swarm_solutions

    source = inspect.getsource(get_swarm_solutions)

    # Should use bridge functions
    assert "is_memory_available" in source, "Should check if memory is available"
    assert "cc_memory.recall" in source, "Should use cc_memory.recall"

    print("✓ get_swarm_solutions uses cc-memory API")


def main():
    """Run all tests."""
    print("=" * 60)
    print("Testing Swarm Spawner CC-Memory Integration")
    print("=" * 60)
    print()

    tests = [
        test_agent_prompt_includes_ccmemory,
        test_get_swarm_solutions_empty,
        test_command_no_print_flag,
        test_ccmemory_integration_available,
        test_query_method_uses_ccmemory,
        test_get_swarm_solutions_uses_ccmemory,
    ]

    passed = 0
    failed = 0
    warnings = 0

    for test in tests:
        try:
            test()
            passed += 1
        except AssertionError as e:
            print(f"✗ {test.__name__}: {e}")
            failed += 1
        except Exception as e:
            print(f"✗ {test.__name__}: Unexpected error: {e}")
            failed += 1

    print()
    print("=" * 60)
    print(f"Results: {passed} passed, {failed} failed")
    print("=" * 60)

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
