"""
Context-Aware Task Optimization - Metacognition for context window management.

Enables Claude to reason about its own constraints and optimize task execution
based on remaining context budget. Inspired by Continuous-Claude-v3's ability
to parallelize tasks when nearing context limits.

Key insight: Claude can reason about its constraints when given the right signals.
This module provides those signals and strategies.

Context Pressure Levels:
    0-40%  used → RELAXED   (no optimization needed)
    40-60% used → NORMAL    (start tracking, plan ahead)
    60-75% used → OPTIMIZE  (identify parallelizable tasks, bundle work)
    75-90% used → COMPRESS  (complete critical items, defer rest)
    90%+   used → EMERGENCY (handoff now, save state)
"""

import json
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from pathlib import Path
from typing import List, Dict, Optional, Any

from .budget import get_context_budget, ContextBudget


class ContextPressure(Enum):
    """Context pressure levels."""
    RELAXED = "relaxed"      # 0-40% used, plenty of room
    NORMAL = "normal"        # 40-60% used, start planning
    OPTIMIZE = "optimize"    # 60-75% used, parallelize now
    COMPRESS = "compress"    # 75-90% used, complete and handoff
    EMERGENCY = "emergency"  # 90%+ used, handoff immediately


@dataclass
class TaskItem:
    """A task item from TodoWrite or progress file."""
    id: str
    content: str
    status: str  # pending, in_progress, completed
    dependencies: List[str] = field(default_factory=list)
    parallelizable: bool = True
    estimated_tokens: int = 0  # rough estimate of context this task needs


@dataclass
class TaskGraph:
    """Understanding of current tasks and their relationships."""
    items: List[TaskItem] = field(default_factory=list)
    pending: List[TaskItem] = field(default_factory=list)
    in_progress: List[TaskItem] = field(default_factory=list)
    completed: List[TaskItem] = field(default_factory=list)
    parallelizable: List[TaskItem] = field(default_factory=list)
    sequential: List[TaskItem] = field(default_factory=list)


@dataclass
class OptimizationStrategy:
    """Recommended strategy based on context pressure."""
    pressure: ContextPressure
    remaining_pct: float
    remaining_tokens: int

    # Recommendations
    should_parallelize: bool
    parallel_tasks: List[TaskItem]
    should_defer: List[TaskItem]
    should_complete_first: List[TaskItem]

    # Handoff preparation
    prepare_handoff: bool
    handoff_urgency: str  # none, low, medium, high, critical

    # Message to inject into Claude's context
    guidance: str

    @property
    def needs_action(self) -> bool:
        """Does this strategy require immediate action?"""
        return self.pressure in (
            ContextPressure.OPTIMIZE,
            ContextPressure.COMPRESS,
            ContextPressure.EMERGENCY
        )


def get_pressure_level(budget: ContextBudget) -> ContextPressure:
    """Determine context pressure level from budget."""
    used_pct = 1 - budget.remaining_pct

    if used_pct < 0.40:
        return ContextPressure.RELAXED
    elif used_pct < 0.60:
        return ContextPressure.NORMAL
    elif used_pct < 0.75:
        return ContextPressure.OPTIMIZE
    elif used_pct < 0.90:
        return ContextPressure.COMPRESS
    else:
        return ContextPressure.EMERGENCY


def parse_todo_list(todo_json: str) -> TaskGraph:
    """Parse TodoWrite JSON into a TaskGraph."""
    graph = TaskGraph()

    try:
        todos = json.loads(todo_json) if isinstance(todo_json, str) else todo_json
    except (json.JSONDecodeError, TypeError):
        return graph

    if not isinstance(todos, list):
        return graph

    for i, todo in enumerate(todos):
        if not isinstance(todo, dict):
            continue

        item = TaskItem(
            id=str(i),
            content=todo.get("content", ""),
            status=todo.get("status", "pending"),
        )

        # Detect dependencies from content
        content_lower = item.content.lower()
        if any(word in content_lower for word in ["after", "then", "once", "following"]):
            item.parallelizable = False
        if any(word in content_lower for word in ["first", "before", "prerequisite"]):
            item.parallelizable = False

        graph.items.append(item)

        if item.status == "pending":
            graph.pending.append(item)
            if item.parallelizable:
                graph.parallelizable.append(item)
            else:
                graph.sequential.append(item)
        elif item.status == "in_progress":
            graph.in_progress.append(item)
        elif item.status == "completed":
            graph.completed.append(item)

    return graph


def load_progress_file(project_root: Path = None) -> Optional[Dict]:
    """Load claude-progress.json if it exists."""
    if project_root is None:
        project_root = Path.cwd()

    progress_file = project_root / "claude-progress.json"
    if not progress_file.exists():
        return None

    try:
        return json.loads(progress_file.read_text())
    except (json.JSONDecodeError, OSError):
        return None


def save_progress_file(progress: Dict, project_root: Path = None) -> bool:
    """Save progress to claude-progress.json."""
    if project_root is None:
        project_root = Path.cwd()

    progress_file = project_root / "claude-progress.json"
    progress["lastUpdated"] = datetime.now().isoformat()

    try:
        progress_file.write_text(json.dumps(progress, indent=2))
        return True
    except OSError:
        return False


def analyze_tasks(
    todo_json: str = "",
    progress_file: Dict = None,
    budget: ContextBudget = None,
) -> OptimizationStrategy:
    """
    Analyze current tasks and context budget to produce optimization strategy.

    This is the core function that enables metacognitive task management.
    """
    # Get context budget
    if budget is None:
        budget = get_context_budget()

    if budget is None:
        # Can't determine budget, return conservative strategy
        return OptimizationStrategy(
            pressure=ContextPressure.NORMAL,
            remaining_pct=0.5,
            remaining_tokens=60000,
            should_parallelize=False,
            parallel_tasks=[],
            should_defer=[],
            should_complete_first=[],
            prepare_handoff=False,
            handoff_urgency="none",
            guidance="Context budget unknown - proceeding conservatively.",
        )

    pressure = get_pressure_level(budget)

    # Parse task graph
    graph = parse_todo_list(todo_json)

    # Build strategy based on pressure
    strategy = OptimizationStrategy(
        pressure=pressure,
        remaining_pct=budget.remaining_pct,
        remaining_tokens=budget.remaining,
        should_parallelize=False,
        parallel_tasks=[],
        should_defer=[],
        should_complete_first=[],
        prepare_handoff=False,
        handoff_urgency="none",
        guidance="",
    )

    # RELAXED: No optimization needed
    if pressure == ContextPressure.RELAXED:
        strategy.guidance = (
            f"Context: {int(budget.remaining_pct * 100)}% remaining "
            f"({budget.remaining:,} tokens). No optimization needed."
        )
        return strategy

    # NORMAL: Start planning
    if pressure == ContextPressure.NORMAL:
        strategy.should_complete_first = graph.in_progress[:3]
        strategy.guidance = (
            f"Context: {int(budget.remaining_pct * 100)}% remaining. "
            f"Plan ahead - {len(graph.pending)} tasks pending."
        )
        return strategy

    # OPTIMIZE: Parallelize now
    if pressure == ContextPressure.OPTIMIZE:
        strategy.should_parallelize = len(graph.parallelizable) > 1
        strategy.parallel_tasks = graph.parallelizable[:4]  # Max 4 parallel
        strategy.should_complete_first = graph.in_progress
        strategy.should_defer = graph.sequential
        strategy.handoff_urgency = "low"

        parallel_names = [t.content[:30] for t in strategy.parallel_tasks]
        strategy.guidance = (
            f"⚡ CONTEXT: {int(budget.remaining_pct * 100)}% remaining - OPTIMIZE MODE\n"
            f"Parallelize these independent tasks using Task tool:\n"
            + "\n".join(f"  • {name}" for name in parallel_names)
            + f"\nComplete before context exhaustion. {len(strategy.should_defer)} tasks can be deferred."
        )
        return strategy

    # COMPRESS: Complete critical, defer rest
    if pressure == ContextPressure.COMPRESS:
        strategy.should_parallelize = True
        strategy.parallel_tasks = graph.parallelizable[:2]  # Just 2 in parallel
        strategy.should_complete_first = graph.in_progress[:2]
        strategy.should_defer = graph.pending[2:]
        strategy.prepare_handoff = True
        strategy.handoff_urgency = "high"

        strategy.guidance = (
            f"🔶 CONTEXT: {int(budget.remaining_pct * 100)}% remaining - COMPRESS MODE\n"
            f"Complete {len(strategy.should_complete_first)} critical tasks, defer {len(strategy.should_defer)}.\n"
            f"Prepare handoff document now. Bundle remaining work."
        )
        return strategy

    # EMERGENCY: Handoff immediately
    if pressure == ContextPressure.EMERGENCY:
        strategy.should_defer = graph.pending
        strategy.prepare_handoff = True
        strategy.handoff_urgency = "critical"

        strategy.guidance = (
            f"🔴 CONTEXT: {int(budget.remaining_pct * 100)}% remaining - EMERGENCY\n"
            f"STOP new work. Save state immediately.\n"
            f"Create handoff document with:\n"
            f"  • Current status of {len(graph.in_progress)} in-progress tasks\n"
            f"  • {len(graph.pending)} pending tasks for next session\n"
            f"  • Key decisions and context to preserve"
        )
        return strategy

    return strategy


def format_strategy_for_injection(strategy: OptimizationStrategy) -> str:
    """Format strategy as context to inject into Claude's prompt."""
    if not strategy.needs_action:
        return ""  # No injection needed for relaxed/normal

    lines = []
    lines.append("=" * 60)
    lines.append("CONTEXT OPTIMIZATION SIGNAL")
    lines.append("=" * 60)
    lines.append("")
    lines.append(strategy.guidance)
    lines.append("")

    if strategy.should_parallelize and strategy.parallel_tasks:
        lines.append("PARALLELIZE (use Task tool):")
        for task in strategy.parallel_tasks:
            lines.append(f"  → {task.content}")
        lines.append("")

    if strategy.should_defer:
        lines.append(f"DEFER ({len(strategy.should_defer)} tasks for next session)")
        lines.append("")

    if strategy.prepare_handoff:
        lines.append(f"HANDOFF URGENCY: {strategy.handoff_urgency.upper()}")
        lines.append("Create handoff document before continuing.")

    lines.append("=" * 60)

    return "\n".join(lines)


def get_optimization_signal(
    todo_json: str = "",
    transcript_path: str = None,
) -> Optional[str]:
    """
    Get optimization signal to inject into prompt.

    Returns None if no optimization needed, otherwise returns
    formatted guidance for Claude.
    """
    budget = get_context_budget(transcript_path)
    strategy = analyze_tasks(todo_json=todo_json, budget=budget)

    if not strategy.needs_action:
        return None

    return format_strategy_for_injection(strategy)


# Integration with soul agent
def get_context_observation(transcript_path: str = None) -> Dict[str, Any]:
    """
    Get context-aware observation for soul agent.

    Adds context budget awareness to the agent's observation.
    Also logs budget to cc-memory for cross-instance awareness.
    """
    from .budget import log_budget_to_memory

    budget = get_context_budget(transcript_path)

    if budget is None:
        return {
            "context_known": False,
            "pressure": "unknown",
            "remaining_pct": None,
            "remaining_tokens": None,
        }

    pressure = get_pressure_level(budget)

    # Log to cc-memory for cross-instance tracking
    log_budget_to_memory(budget, transcript_path)

    return {
        "context_known": True,
        "pressure": pressure.value,
        "remaining_pct": budget.remaining_pct,
        "remaining_tokens": budget.remaining,
        "should_compact": budget.should_compact,
        "should_urgent_save": budget.should_urgent_save,
        "message_count": budget.message_count,
    }


# Progress file helpers for multi-session work
def update_progress_with_session(
    summary: str,
    completed: List[str] = None,
    project_root: Path = None,
) -> bool:
    """Update progress file with session results."""
    progress = load_progress_file(project_root) or {
        "objective": "",
        "created": datetime.now().isoformat(),
        "features": [],
        "sessionLog": [],
    }

    # Add session log entry
    progress["sessionLog"].append({
        "date": datetime.now().isoformat(),
        "summary": summary,
    })

    # Mark completed features
    if completed:
        for feature in progress.get("features", []):
            if feature.get("description") in completed:
                feature["status"] = "done"

    return save_progress_file(progress, project_root)
