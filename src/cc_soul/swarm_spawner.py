"""
Swarm Spawner - Spawn real Claude agents for parallel problem solving.

Uses the claude CLI to spawn actual Claude instances that work on different
perspectives of a problem simultaneously. Results are collected via cc-memory.

Architecture:
    1. Orchestrator creates tasks with specific prompts
    2. Each task is executed as a separate claude process
    3. Agents write solutions to cc-memory with swarm tags
    4. Orchestrator polls cc-memory for completion
    5. Solutions are converged

This enables true parallel reasoning:
    - FAST agent: Quick intuition, first principles
    - DEEP agent: Thorough analysis, edge cases
    - CRITICAL agent: Find flaws, devil's advocate
    - NOVEL agent: Creative, unconventional approaches

Context Injection:
    Agents can receive:
    - File contents: Actual code they're analyzing
    - Memory context: Relevant past decisions/discoveries from cc-memory
"""

import subprocess
import json
import time
import os
from pathlib import Path
from dataclasses import dataclass, field
from datetime import datetime
from typing import List, Dict, Optional, Any
from concurrent.futures import ThreadPoolExecutor, as_completed

from .convergence import (
    Swarm,
    AgentTask,
    AgentSolution,
    AgentPerspective,
    ConvergenceStrategy,
)
from .core import get_db_connection


@dataclass
class SpawnedAgent:
    """A spawned Claude agent."""
    task_id: str
    process: Optional[subprocess.Popen]
    pid: int
    started_at: str
    status: str = "running"  # running, completed, failed, timeout
    output_file: Optional[Path] = None


@dataclass
class SwarmOrchestrator:
    """
    Orchestrates parallel Claude agents.

    Usage:
        orchestrator = SwarmOrchestrator(swarm)
        orchestrator.spawn_all_agents()
        orchestrator.wait_for_completion(timeout=300)
        result = orchestrator.converge()
    """
    swarm: Swarm
    agents: List[SpawnedAgent] = field(default_factory=list)
    work_dir: Path = field(default_factory=lambda: Path.home() / ".claude" / "swarms")
    max_parallel: int = 4
    model: str = "sonnet"  # Use faster model for agents

    def __post_init__(self):
        self.work_dir.mkdir(parents=True, exist_ok=True)
        self.swarm_dir = self.work_dir / self.swarm.swarm_id
        self.swarm_dir.mkdir(exist_ok=True)

    def _build_agent_prompt(self, task: AgentTask) -> str:
        """Build a complete prompt for an agent."""
        # Context isolation notice - agent knows it's a fresh instance
        isolation_notice = f"""## Agent Context Notice

You are a swarm agent: {task.task_id}
Perspective: {task.perspective.value}
Swarm: {self.swarm.swarm_id}

IMPORTANT: You are a fresh Claude instance with your own context window.
- Your context starts at ~0%, not the parent session's context
- Ignore any cc-soul context budget from other sessions
- You have full context available for this task
- Work independently - do not try to coordinate with other agents

"""
        # Base prompt with perspective guidance
        base_prompt = task.to_prompt()

        # Instructions to use cc-memory for context and solution storage
        memory_instructions = f"""

## Context Retrieval

You have access to cc-memory. Use it to get relevant context:
1. Run `mem-recall` with queries related to the problem
2. Check for past decisions, patterns, or relevant observations
3. Use this context to inform your solution

## Output Instructions

When you have your solution, save it to cc-memory:

1. Use the `mem-remember` tool with:
   - category: "swarm-solution"
   - title: "{task.task_id}: [your short title]"
   - content: Your complete solution with reasoning
   - tags: ["swarm:{self.swarm.swarm_id}", "task:{task.task_id}", "perspective:{task.perspective.value}"]

2. Include in your content:
   - confidence: 0.0-1.0
   - solution: Your complete solution
   - reasoning: Why this approach

The orchestrator will find your solution via the swarm tag.
"""
        return isolation_notice + base_prompt + memory_instructions

    def _create_agent_script(self, task: AgentTask) -> Path:
        """Create a script file for the agent to execute."""
        script_path = self.swarm_dir / f"{task.task_id}.prompt"
        prompt = self._build_agent_prompt(task)
        script_path.write_text(prompt)
        return script_path

    def spawn_agent(self, task: AgentTask) -> SpawnedAgent:
        """Spawn a single Claude agent for a task."""
        prompt_file = self._create_agent_script(task)
        output_file = self.swarm_dir / f"{task.task_id}.output"

        # Build claude command
        # Full session with MCP tools - agents can use cc-memory
        cmd = [
            "claude",
            "--model", self.model,
            "--dangerously-skip-permissions",  # Non-interactive mode
        ]

        # Read prompt from file
        prompt = prompt_file.read_text()

        try:
            # Agent-isolated environment - prevent parent session context bleed
            agent_env = os.environ.copy()
            agent_env["CC_SOUL_SWARM_AGENT"] = "1"
            agent_env["CC_SOUL_SWARM_ID"] = self.swarm.swarm_id
            agent_env["CC_SOUL_TASK_ID"] = task.task_id
            agent_env["CC_SOUL_PERSPECTIVE"] = task.perspective.value

            # Spawn as background process
            process = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=open(output_file, 'w'),
                stderr=subprocess.PIPE,
                text=True,
                cwd=str(Path.cwd()),
                env=agent_env,
            )

            # Send prompt
            process.stdin.write(prompt)
            process.stdin.close()

            agent = SpawnedAgent(
                task_id=task.task_id,
                process=process,
                pid=process.pid,
                started_at=datetime.now().isoformat(),
                output_file=output_file,
            )

            self.agents.append(agent)

            # Record in database
            self._record_agent_spawn(agent, task)

            return agent

        except FileNotFoundError:
            # claude CLI not found - try alternative approach
            return self._spawn_via_script(task, output_file)

    def _spawn_via_script(self, task: AgentTask, output_file: Path) -> SpawnedAgent:
        """Fallback: spawn via Python script that uses the API directly."""
        script_content = f'''#!/usr/bin/env python3
"""Agent worker for swarm task {task.task_id}"""
import sys
sys.path.insert(0, '/maps/projects/fernandezguerra/apps/repos/cc-soul/src')

from cc_soul.convergence import get_swarm

# This agent would normally use Claude API directly
# For now, we'll create a placeholder that the orchestrator can detect

swarm = get_swarm("{self.swarm.swarm_id}")
if swarm:
    # Mark task as needing manual solution
    print("Agent spawned for task: {task.task_id}")
    print("Perspective: {task.perspective.value}")
    print("Awaiting solution submission...")
'''

        script_path = self.swarm_dir / f"{task.task_id}.worker.py"
        script_path.write_text(script_content)
        script_path.chmod(0o755)

        # For now, just record that we need this agent's work
        agent = SpawnedAgent(
            task_id=task.task_id,
            process=None,
            pid=0,
            started_at=datetime.now().isoformat(),
            status="awaiting",  # Needs manual or API-based solution
            output_file=output_file,
        )

        self.agents.append(agent)
        self._record_agent_spawn(agent, task)

        return agent

    def spawn_all_agents(self, parallel: bool = True) -> List[SpawnedAgent]:
        """Spawn all agents, optionally in parallel."""
        if parallel:
            with ThreadPoolExecutor(max_workers=self.max_parallel) as executor:
                futures = {
                    executor.submit(self.spawn_agent, task): task
                    for task in self.swarm.tasks
                }
                for future in as_completed(futures):
                    try:
                        future.result()
                    except Exception as e:
                        task = futures[future]
                        print(f"Failed to spawn agent for {task.task_id}: {e}")
        else:
            for task in self.swarm.tasks:
                self.spawn_agent(task)

        return self.agents

    def _record_agent_spawn(self, agent: SpawnedAgent, task: AgentTask):
        """Record agent spawn in database."""
        conn = get_db_connection()
        c = conn.cursor()

        c.execute("""
            CREATE TABLE IF NOT EXISTS swarm_agents (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                swarm_id TEXT NOT NULL,
                task_id TEXT NOT NULL,
                pid INTEGER,
                status TEXT DEFAULT 'running',
                started_at TEXT NOT NULL,
                completed_at TEXT,
                output_file TEXT
            )
        """)

        c.execute(
            """INSERT INTO swarm_agents
               (swarm_id, task_id, pid, status, started_at, output_file)
               VALUES (?, ?, ?, ?, ?, ?)""",
            (
                self.swarm.swarm_id,
                agent.task_id,
                agent.pid,
                agent.status,
                agent.started_at,
                str(agent.output_file) if agent.output_file else None,
            )
        )

        conn.commit()
        conn.close()

    def check_agent_status(self, agent: SpawnedAgent) -> str:
        """Check if an agent has completed."""
        # First check if solution exists in cc-memory
        if self._query_cc_memory_for_solution(agent.task_id):
            return "completed"

        if agent.process:
            # Check if process is still running
            poll = agent.process.poll()
            if poll is None:
                return "running"
            elif poll == 0:
                return "completed"
            else:
                return "failed"
        else:
            # No process - check if solution exists in swarm
            for sol in self.swarm.solutions:
                if sol.task_id == agent.task_id:
                    return "completed"
            return agent.status

    def wait_for_completion(
        self,
        timeout: int = 300,
        poll_interval: int = 5,
    ) -> Dict[str, Any]:
        """Wait for all agents to complete."""
        start_time = time.time()
        completed = []
        failed = []

        while time.time() - start_time < timeout:
            all_done = True

            for agent in self.agents:
                if agent.status in ("completed", "failed"):
                    continue

                status = self.check_agent_status(agent)
                agent.status = status

                if status == "running":
                    all_done = False
                elif status == "completed":
                    completed.append(agent.task_id)
                    self._collect_agent_result(agent)
                elif status == "failed":
                    failed.append(agent.task_id)

            if all_done:
                break

            time.sleep(poll_interval)

        # Timeout remaining agents
        for agent in self.agents:
            if agent.status == "running":
                agent.status = "timeout"
                if agent.process:
                    agent.process.terminate()

        return {
            "completed": completed,
            "failed": failed,
            "timeout": [a.task_id for a in self.agents if a.status == "timeout"],
            "elapsed": time.time() - start_time,
        }

    def _collect_agent_result(self, agent: SpawnedAgent):
        """Collect result from cc-memory (agent stored solution there)."""
        solution_data = self._query_cc_memory_for_solution(agent.task_id)
        if solution_data:
            self.swarm.submit_solution(
                task_id=agent.task_id,
                solution=solution_data.get("solution", solution_data.get("content", "")),
                confidence=float(solution_data.get("confidence", 0.7)),
                reasoning=solution_data.get("reasoning", ""),
            )
        elif agent.output_file and agent.output_file.exists():
            # Fallback: check output file for SWARM_SOLUTION block
            output = agent.output_file.read_text()
            solution_data = self._parse_solution_block(output)
            if solution_data:
                self.swarm.submit_solution(
                    task_id=agent.task_id,
                    solution=solution_data.get("solution", ""),
                    confidence=float(solution_data.get("confidence", 0.7)),
                    reasoning=solution_data.get("reasoning", ""),
                )

    def _query_cc_memory_for_solution(self, task_id: str) -> Optional[Dict]:
        """Query cc-memory for a swarm solution by task_id."""
        import re

        # Try cc-memory first
        try:
            from .bridge import is_memory_available, find_project_dir

            if is_memory_available():
                from cc_memory import memory as cc_memory

                project_dir = find_project_dir()
                results = cc_memory.recall(project_dir, f"task:{task_id}", limit=5)

                for r in results:
                    content = r.get("content", "")
                    title = r.get("title", "")
                    tags = r.get("tags", "")

                    # Check if this matches our task
                    if f"task:{task_id}" in (tags + title + content):
                        confidence_match = re.search(r'confidence:\s*([\d.]+)', content)
                        confidence = float(confidence_match.group(1)) if confidence_match else 0.7

                        return {
                            "content": content,
                            "confidence": confidence,
                            "solution": content,
                            "reasoning": "",
                        }
        except Exception:
            pass

        return None

    def _parse_solution_block(self, output: str) -> Optional[Dict]:
        """Parse [SWARM_SOLUTION] block from agent output."""
        import re

        pattern = r'\[SWARM_SOLUTION\](.*?)\[/SWARM_SOLUTION\]'
        match = re.search(pattern, output, re.DOTALL)

        if not match:
            return None

        block = match.group(1).strip()
        result = {}

        # Parse YAML-like format
        current_key = None
        current_value = []

        for line in block.split('\n'):
            if line.startswith('  ') and current_key:
                # Continuation of multi-line value
                current_value.append(line.strip())
            elif ':' in line:
                # Save previous key if exists
                if current_key and current_value:
                    result[current_key] = '\n'.join(current_value)

                # New key
                parts = line.split(':', 1)
                current_key = parts[0].strip()
                value = parts[1].strip() if len(parts) > 1 else ""

                if value == '|':
                    # Multi-line value follows
                    current_value = []
                else:
                    result[current_key] = value
                    current_key = None
                    current_value = []

        # Save last key
        if current_key and current_value:
            result[current_key] = '\n'.join(current_value)

        return result

    def converge(
        self,
        strategy: ConvergenceStrategy = ConvergenceStrategy.SYNTHESIZE,
    ):
        """Converge all collected solutions."""
        return self.swarm.converge(strategy)

    def get_status(self) -> Dict[str, Any]:
        """Get current swarm status."""
        return {
            "swarm_id": self.swarm.swarm_id,
            "problem": self.swarm.problem[:100],
            "agents": [
                {
                    "task_id": a.task_id,
                    "pid": a.pid,
                    "status": a.status,
                    "started_at": a.started_at,
                }
                for a in self.agents
            ],
            "solutions": len(self.swarm.solutions),
            "work_dir": str(self.swarm_dir),
        }


def spawn_swarm(
    problem: str,
    perspectives: List[AgentPerspective] = None,
    constraints: List[str] = None,
    wait: bool = True,
    timeout: int = 300,
) -> Dict[str, Any]:
    """
    High-level function to spawn a swarm and optionally wait for results.

    Args:
        problem: The problem to solve
        perspectives: List of perspectives to include
        constraints: Problem constraints
        wait: Whether to wait for completion
        timeout: Max seconds to wait

    Returns:
        Dict with swarm_id, status, and optionally converged result
    """
    from .convergence import spawn_parallel_agents

    # Create swarm
    swarm = spawn_parallel_agents(
        problem=problem,
        perspectives=perspectives,
        constraints=constraints,
    )

    # Create orchestrator
    orchestrator = SwarmOrchestrator(swarm)

    # Spawn agents
    orchestrator.spawn_all_agents()

    result = {
        "swarm_id": swarm.swarm_id,
        "agents_spawned": len(orchestrator.agents),
        "status": orchestrator.get_status(),
    }

    if wait:
        # Wait for completion
        completion = orchestrator.wait_for_completion(timeout=timeout)
        result["completion"] = completion

        # Converge if we have solutions
        if swarm.solutions:
            converged = orchestrator.converge()
            result["converged"] = {
                "strategy": converged.strategy_used.value,
                "solution": converged.final_solution,
                "confidence": converged.confidence,
                "contributors": len(converged.contributing_agents),
            }

    return result


def get_swarm_solutions(swarm_id: str) -> List[Dict[str, Any]]:
    """
    Get all solutions for a swarm from cc-memory.

    Args:
        swarm_id: The swarm ID to query

    Returns:
        List of solution dicts with task_id, content, confidence
    """
    import re
    solutions = []

    # Try cc-memory first (project-local observations)
    try:
        from .bridge import is_memory_available, find_project_dir

        if is_memory_available():
            from cc_memory import memory as cc_memory

            project_dir = find_project_dir()

            # Search for swarm solutions using semantic search
            results = cc_memory.recall(project_dir, f"swarm:{swarm_id}", limit=20)

            for r in results:
                content = r.get("content", "")
                title = r.get("title", "")
                tags = r.get("tags", "")

                # Check if this is actually a swarm solution
                if f"swarm:{swarm_id}" not in (tags + title + content):
                    continue

                # Extract task_id
                task_match = re.search(r'task:([^\s,\]]+)', tags + title)
                task_id = task_match.group(1) if task_match else title.split(':')[0] if ':' in title else 'unknown'

                # Extract perspective
                perspective_match = re.search(r'perspective:(\w+)', tags + title)
                perspective = perspective_match.group(1) if perspective_match else 'unknown'

                # Extract confidence
                confidence_match = re.search(r'confidence:\s*([\d.]+)', content)
                confidence = float(confidence_match.group(1)) if confidence_match else 0.7

                solutions.append({
                    "observation_id": r.get("id"),
                    "task_id": task_id,
                    "perspective": perspective,
                    "content": content,
                    "confidence": confidence,
                    "created_at": r.get("timestamp"),
                })
    except Exception:
        pass

    # Fallback: check soul database (for simulated swarms)
    if not solutions:
        try:
            conn = get_db_connection()
            c = conn.cursor()

            c.execute("""
                SELECT id, title, content, created_at FROM wisdom
                WHERE type = 'swarm_solution'
                AND content LIKE ?
                ORDER BY created_at DESC
            """, (f'%swarm:{swarm_id}%',))

            for row in c.fetchall():
                obs_id, title, content, created_at = row

                task_match = re.search(r'task:([^\s,\]]+)', content)
                task_id = task_match.group(1) if task_match else 'unknown'

                perspective_match = re.search(r'perspective:(\w+)', content)
                perspective = perspective_match.group(1) if perspective_match else 'unknown'

                confidence_match = re.search(r'confidence:\s*([\d.]+)', content)
                confidence = float(confidence_match.group(1)) if confidence_match else 0.7

                solutions.append({
                    "observation_id": obs_id,
                    "task_id": task_id,
                    "perspective": perspective,
                    "content": content,
                    "confidence": confidence,
                    "created_at": created_at,
                })

            conn.close()
        except Exception:
            pass

    return solutions


def get_orchestrator(swarm_id: str) -> Optional[SwarmOrchestrator]:
    """Get an existing orchestrator by swarm ID."""
    from .convergence import get_swarm

    swarm = get_swarm(swarm_id)
    if not swarm:
        return None

    orchestrator = SwarmOrchestrator(swarm)

    # Load agents from database
    conn = get_db_connection()
    c = conn.cursor()

    try:
        c.execute(
            """SELECT task_id, pid, status, started_at, output_file
               FROM swarm_agents WHERE swarm_id = ?""",
            (swarm_id,)
        )

        for row in c.fetchall():
            agent = SpawnedAgent(
                task_id=row[0],
                process=None,  # Can't recover process handle
                pid=row[1],
                status=row[2],
                started_at=row[3],
                output_file=Path(row[4]) if row[4] else None,
            )
            orchestrator.agents.append(agent)

    except Exception:
        pass
    finally:
        conn.close()

    return orchestrator
