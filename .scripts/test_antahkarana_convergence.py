#!/usr/bin/env python3
"""
Test Antahkarana/Swarm convergence system end-to-end.
Verifies attribute names and the full flow from creation to harmonization.
"""
from cc_soul.convergence import (
    Antahkarana,
    InnerVoice,
    awaken_antahkarana,
    get_antahkarana,
    ConvergenceStrategy,
    VoiceTask,
    VoiceSolution,
    SamvadaResult,
)

def test_voice_task_attributes():
    """VoiceTask should use 'perspective', not 'voice'."""
    task = VoiceTask(
        task_id='test-001',
        problem='Test problem',
        perspective=InnerVoice.BUDDHI
    )
    assert hasattr(task, 'perspective'), "VoiceTask missing 'perspective' attribute"
    assert not hasattr(task, 'voice'), "VoiceTask has deprecated 'voice' attribute"
    assert task.perspective == InnerVoice.BUDDHI
    print("✓ VoiceTask attributes correct")

def test_voice_solution_attributes():
    """VoiceSolution should use 'perspective' and 'confidence'."""
    sol = VoiceSolution(
        task_id='test-001',
        agent_id='agent-001',
        perspective=InnerVoice.MANAS,
        solution='Test solution',
        confidence=0.85,
        reasoning='Test reasoning',
        timestamp='2025-01-01'
    )
    assert hasattr(sol, 'perspective'), "VoiceSolution missing 'perspective' attribute"
    assert hasattr(sol, 'confidence'), "VoiceSolution missing 'confidence' attribute"
    assert not hasattr(sol, 'voice'), "VoiceSolution has deprecated 'voice' attribute"
    assert not hasattr(sol, 'shraddha'), "VoiceSolution has deprecated 'shraddha' attribute"
    assert sol.perspective == InnerVoice.MANAS
    assert sol.confidence == 0.85
    print("✓ VoiceSolution attributes correct")

def test_samvada_result_attributes():
    """SamvadaResult should use 'strategy_used', 'final_solution', and 'confidence'."""
    # Create antahkarana and submit insights
    ant = awaken_antahkarana(
        problem='Test problem',
        voices=[InnerVoice.MANAS, InnerVoice.BUDDHI]
    )
    for task in ant.tasks:
        ant.submit_insight(
            task_id=task.task_id,
            solution=f'Insight from {task.perspective.value}',
            confidence=0.8,
            reasoning=f'Reasoning from {task.perspective.value}'
        )

    result = ant.harmonize(ConvergenceStrategy.SANKHYA)

    assert hasattr(result, 'strategy_used'), "SamvadaResult missing 'strategy_used'"
    assert hasattr(result, 'final_solution'), "SamvadaResult missing 'final_solution'"
    assert hasattr(result, 'confidence'), "SamvadaResult missing 'confidence'"
    assert not hasattr(result, 'pramana_used'), "SamvadaResult has deprecated 'pramana_used'"
    assert not hasattr(result, 'wisdom'), "SamvadaResult has deprecated 'wisdom'"
    assert not hasattr(result, 'shraddha'), "SamvadaResult has deprecated 'shraddha'"
    print("✓ SamvadaResult attributes correct")

def test_full_convergence_flow():
    """Test complete convergence flow from awaken to harmonize."""
    # 1. Awaken with multiple voices
    ant = awaken_antahkarana(
        problem='What is the most elegant error handling pattern?',
        voices=[InnerVoice.MANAS, InnerVoice.BUDDHI, InnerVoice.AHAMKARA]
    )
    assert len(ant.tasks) == 3
    print(f"✓ Created antahkarana {ant.antahkarana_id} with 3 voices")

    # 2. Submit insights from each voice
    insights = [
        ('Quick: try/except everywhere', 0.6),
        ('Deep: handle at boundaries only', 0.9),
        ('Critical: exceptions leak implementation', 0.7),
    ]
    for i, task in enumerate(ant.tasks):
        solution, conf = insights[i]
        ant.submit_insight(
            task_id=task.task_id,
            solution=solution,
            confidence=conf,
            reasoning=f'Reasoning from {task.perspective.value}'
        )
    assert len(ant.insights) == 3
    print(f"✓ Submitted 3 insights")

    # 3. Harmonize with different strategies
    for strategy in [ConvergenceStrategy.SANKHYA, ConvergenceStrategy.SAMVADA]:
        result = ant.harmonize(strategy)
        assert result.strategy_used == strategy
        assert 0 <= result.confidence <= 1
        assert len(result.final_solution) > 0
        print(f"✓ Harmonized with {strategy.value}: confidence={result.confidence:.0%}")

    # 4. Verify retrieval
    retrieved = get_antahkarana(ant.antahkarana_id)
    assert retrieved is not None
    assert retrieved.antahkarana_id == ant.antahkarana_id
    print(f"✓ Retrieved antahkarana by ID")

if __name__ == '__main__':
    print("Testing Antahkarana Convergence System\n")
    test_voice_task_attributes()
    test_voice_solution_attributes()
    test_samvada_result_attributes()
    test_full_convergence_flow()
    print("\n✓ All tests passed!")
