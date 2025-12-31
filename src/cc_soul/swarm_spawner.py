"""
Antahkarana Spawner - Spawn real Claude voices for parallel problem solving.

Uses the claude CLI to spawn actual Claude instances that contemplate different
facets of a problem simultaneously. Insights are collected via cc-memory (Chitta).

Architecture:
    1. Orchestrator awakens voices with specific prompts
    2. Each voice is executed as a separate claude process
    3. Voices write insights to cc-memory with antahkarana tags
    4. Orchestrator polls cc-memory for completion
    5. Insights are harmonized

This enables true parallel reasoning through the Antahkarana facets:
    - MANAS: Quick intuition, first impressions
    - BUDDHI: Thorough analysis, discrimination
    - AHAMKARA: Critical examination, finding flaws
    - CHITTA: Pattern-based wisdom from experience
    - VIKALPA: Creative imagination, novel approaches
    - SAKSHI: Detached witness, minimal essence

Context Injection:
    Voices can receive:
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
    Antahkarana,
    VoiceTask,
    VoiceSolution,
    InnerVoice,
    ConvergenceStrategy,
)
from .core import get_db_connection
from .budget import (
    get_context_budget,
    get_all_session_budgets,
    log_budget_to_memory,
    ContextBudget,
)


@dataclass
class SpawnedVoice:
    """A spawned Claude voice."""
    task_id: str
    process: Optional[subprocess.Popen]
    pid: int
    started_at: str
    status: str = "running"  # running, completed, failed, timeout
    output_file: Optional[Path] = None


@dataclass
class AntahkaranaOrchestrator:
    """
    Orchestrates parallel Claude voices.

    Usage:
        orchestrator = AntahkaranaOrchestrator(antahkarana)
        orchestrator.spawn_all_voices()
        orchestrator.wait_for_completion(timeout=300)
        result = orchestrator.converge()
    """
    antahkarana: Antahkarana
    voices: List[SpawnedVoice] = field(default_factory=list)
    work_dir: Path = field(default_factory=lambda: Path.home() / ".claude" / "antahkarana")
    max_parallel: int = 4
    model: str = "opus"  # Use opus for deep contemplation

    def __post_init__(self):
        self.work_dir.mkdir(parents=True, exist_ok=True)
        self.antahkarana_dir = self.work_dir / self.antahkarana.antahkarana_id
        self.antahkarana_dir.mkdir(exist_ok=True)
        self._orchestrator_budget: Optional[ContextBudget] = None

    def check_orchestrator_budget(self) -> Dict[str, Any]:
        """
        Check orchestrator's context budget before spawning.

        Returns dict with:
            - can_spawn: bool - whether we have capacity to spawn
            - remaining_pct: float - percentage remaining
            - pressure: str - budget pressure level
            - recommendation: str - what to do
        """
        budget = get_context_budget()
        self._orchestrator_budget = budget

        if budget is None:
            return {
                "can_spawn": True,
                "remaining_pct": 0.5,
                "pressure": "unknown",
                "recommendation": "Budget unknown, proceeding cautiously",
            }

        remaining_pct = budget.remaining_pct

        # Log orchestrator budget to cc-memory for cross-instance awareness
        log_budget_to_memory(budget)

        if remaining_pct < 0.10:
            return {
                "can_spawn": False,
                "remaining_pct": remaining_pct,
                "pressure": "emergency",
                "recommendation": "STOP: Orchestrator at <10% - save state first",
            }
        elif remaining_pct < 0.25:
            return {
                "can_spawn": True,
                "remaining_pct": remaining_pct,
                "pressure": "compact",
                "recommendation": "Limit to 2 voices max - context running low",
                "max_voices": 2,
            }
        elif remaining_pct < 0.40:
            return {
                "can_spawn": True,
                "remaining_pct": remaining_pct,
                "pressure": "normal",
                "recommendation": "Proceed with up to 3 voices",
                "max_voices": 3,
            }
        else:
            return {
                "can_spawn": True,
                "remaining_pct": remaining_pct,
                "pressure": "relaxed",
                "recommendation": "Full capacity available",
                "max_voices": self.max_parallel,
            }

    def _build_voice_prompt(self, task: VoiceTask) -> str:
        """Build a complete prompt for a voice."""
        # Context isolation notice - voice knows it's a fresh instance
        isolation_notice = f"""## Voice Context Notice

You are an Antahkarana voice: {task.task_id}
Perspective: {task.perspective.value}
Antahkarana: {self.antahkarana.antahkarana_id}

IMPORTANT: You are a fresh Claude instance with your own context window.
- Your context starts at ~0%, not the parent session's context
- Ignore any cc-soul context budget from other sessions
- You have full context available for this task
- Work independently - do not try to coordinate with other voices

"""
        # Base prompt with perspective guidance
        base_prompt = task.to_prompt()

        # Instructions to use cc-memory for context and insight storage
        memory_instructions = f"""

## Context Retrieval

You have access to cc-memory. Use it to get relevant context:
1. Run `mem-recall` with queries related to the problem
2. Check for past decisions, patterns, or relevant observations
3. Use this context to inform your insight

## Output Instructions

When you have your insight, save it to cc-memory:

1. Use the `mem-remember` tool with:
   - category: "antahkarana-insight"
   - title: "{task.task_id}: [your short title]"
   - content: Your complete insight with reasoning
   - tags: ["antahkarana:{self.antahkarana.antahkarana_id}", "task:{task.task_id}", "voice:{task.perspective.value}"]

2. Include in your content:
   - confidence: 0.0-1.0
   - insight: Your complete insight
   - reasoning: Why this approach

The orchestrator will find your insight via the antahkarana tag.

## Budget Reporting

You are an Antahkarana voice with fresh context. When your work is complete,
also log your final context usage for coordination:

Use `mem-remember` with:
- category: "antahkarana-budget"
- title: "Budget: {task.task_id}"
- tags: ["antahkarana:{self.antahkarana.antahkarana_id}", "budget", "voice:{task.task_id}"]
- content: Include your approximate context usage percentage

This helps the orchestrator track overall resource consumption.
"""
        return isolation_notice + base_prompt + memory_instructions

    def _create_voice_script(self, task: VoiceTask) -> Path:
        """Create a script file for the voice to execute."""
        script_path = self.antahkarana_dir / f"{task.task_id}.prompt"
        prompt = self._build_voice_prompt(task)
        script_path.write_text(prompt)
        return script_path

    def spawn_voice(self, task: VoiceTask) -> SpawnedVoice:
        """Spawn a single Claude voice for a task."""
        prompt_file = self._create_voice_script(task)
        output_file = self.antahkarana_dir / f"{task.task_id}.output"

        # Build claude command
        # Full session with MCP tools - voices can use cc-memory
        cmd = [
            "claude",
            "--model", self.model,
            "--dangerously-skip-permissions",  # Non-interactive mode
        ]

        # Read prompt from file
        prompt = prompt_file.read_text()

        try:
            # Voice-isolated environment - prevent parent session context bleed
            voice_env = os.environ.copy()
            voice_env["CC_SOUL_ANTAHKARANA_VOICE"] = "1"
            voice_env["CC_SOUL_ANTAHKARANA_ID"] = self.antahkarana.antahkarana_id
            voice_env["CC_SOUL_TASK_ID"] = task.task_id
            voice_env["CC_SOUL_PERSPECTIVE"] = task.perspective.value
            # Track parent session for budget family grouping
            from .budget import get_session_id
            voice_env["CC_SOUL_PARENT_SESSION"] = get_session_id()

            # Spawn as background process
            process = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=open(output_file, 'w'),
                stderr=subprocess.PIPE,
                text=True,
                cwd=str(Path.cwd()),
                env=voice_env,
            )

            # Send prompt
            process.stdin.write(prompt)
            process.stdin.close()

            voice = SpawnedVoice(
                task_id=task.task_id,
                process=process,
                pid=process.pid,
                started_at=datetime.now().isoformat(),
                output_file=output_file,
            )

            self.voices.append(voice)

            # Record in database
            self._record_voice_spawn(voice, task)

            return voice

        except FileNotFoundError:
            # claude CLI not found - try alternative approach
            return self._spawn_via_script(task, output_file)

    def _spawn_via_script(self, task: VoiceTask, output_file: Path) -> SpawnedVoice:
        """Fallback: spawn via Python script that uses the API directly."""
        script_content = f'''#!/usr/bin/env python3
"""Voice worker for Antahkarana task {task.task_id}"""
import sys
sys.path.insert(0, '/maps/projects/fernandezguerra/apps/repos/cc-soul/src')

from cc_soul.convergence import get_antahkarana

# This voice would normally use Claude API directly
# For now, we'll create a placeholder that the orchestrator can detect

antahkarana = get_antahkarana("{self.antahkarana.antahkarana_id}")
if antahkarana:
    # Mark task as needing manual insight
    print("Voice spawned for task: {task.task_id}")
    print("Perspective: {task.perspective.value}")
    print("Awaiting insight submission...")
'''

        script_path = self.antahkarana_dir / f"{task.task_id}.worker.py"
        script_path.write_text(script_content)
        script_path.chmod(0o755)

        # For now, just record that we need this voice's work
        voice = SpawnedVoice(
            task_id=task.task_id,
            process=None,
            pid=0,
            started_at=datetime.now().isoformat(),
            status="awaiting",  # Needs manual or API-based insight
            output_file=output_file,
        )

        self.voices.append(voice)
        self._record_voice_spawn(voice, task)

        return voice

    def spawn_all_voices(self, parallel: bool = True) -> List[SpawnedVoice]:
        """
        Spawn all voices, optionally in parallel.

        Budget-aware: Checks orchestrator budget and limits voice count
        if context is running low.
        """
        # Check orchestrator budget before spawning
        budget_status = self.check_orchestrator_budget()

        if not budget_status["can_spawn"]:
            # Log warning to cc-memory
            self._log_spawn_blocked(budget_status)
            return []

        # Determine how many voices to spawn based on budget
        max_voices = budget_status.get("max_voices", self.max_parallel)
        tasks_to_spawn = self.antahkarana.tasks[:max_voices]

        if len(tasks_to_spawn) < len(self.antahkarana.tasks):
            self._log_spawn_limited(
                spawned=len(tasks_to_spawn),
                total=len(self.antahkarana.tasks),
                reason=budget_status["recommendation"],
            )

        if parallel:
            workers = min(max_voices, len(tasks_to_spawn))
            with ThreadPoolExecutor(max_workers=workers) as executor:
                futures = {
                    executor.submit(self.spawn_voice, task): task
                    for task in tasks_to_spawn
                }
                for future in as_completed(futures):
                    try:
                        future.result()
                    except Exception as e:
                        task = futures[future]
                        print(f"Failed to spawn voice for {task.task_id}: {e}")
        else:
            for task in tasks_to_spawn:
                self.spawn_voice(task)

        return self.voices

    def _log_spawn_blocked(self, budget_status: Dict[str, Any]):
        """Log when spawning is blocked due to budget constraints."""
        try:
            from .bridge import is_memory_available, find_project_dir
            if is_memory_available():
                from cc_memory import memory as cc_memory
                project_dir = find_project_dir()
                cc_memory.remember(
                    category="antahkarana-budget",
                    title=f"Antahkarana {self.antahkarana.antahkarana_id}: BLOCKED",
                    content=f"""Antahkarana spawn blocked due to budget constraints.
Remaining: {budget_status['remaining_pct']*100:.0f}%
Pressure: {budget_status['pressure']}
Recommendation: {budget_status['recommendation']}
Problem: {self.antahkarana.problem[:200]}""",
                    tags=["antahkarana", "budget", "blocked", f"antahkarana:{self.antahkarana.antahkarana_id}"],
                    project_dir=project_dir,
                )
        except Exception:
            pass

    def _log_spawn_limited(self, spawned: int, total: int, reason: str):
        """Log when spawning is limited due to budget constraints."""
        try:
            from .bridge import is_memory_available, find_project_dir
            if is_memory_available():
                from cc_memory import memory as cc_memory
                project_dir = find_project_dir()
                cc_memory.remember(
                    category="antahkarana-budget",
                    title=f"Antahkarana {self.antahkarana.antahkarana_id}: LIMITED",
                    content=f"""Antahkarana spawn limited due to budget constraints.
Spawned: {spawned}/{total} voices
Reason: {reason}
Problem: {self.antahkarana.problem[:200]}""",
                    tags=["antahkarana", "budget", "limited", f"antahkarana:{self.antahkarana.antahkarana_id}"],
                    project_dir=project_dir,
                )
        except Exception:
            pass

    def _record_voice_spawn(self, voice: SpawnedVoice, task: VoiceTask):
        """Record voice spawn in database."""
        conn = get_db_connection()
        c = conn.cursor()

        c.execute("""
            CREATE TABLE IF NOT EXISTS antahkarana_voices (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                antahkarana_id TEXT NOT NULL,
                task_id TEXT NOT NULL,
                pid INTEGER,
                status TEXT DEFAULT 'running',
                started_at TEXT NOT NULL,
                completed_at TEXT,
                output_file TEXT
            )
        """)

        c.execute(
            """INSERT INTO antahkarana_voices
               (antahkarana_id, task_id, pid, status, started_at, output_file)
               VALUES (?, ?, ?, ?, ?, ?)""",
            (
                self.antahkarana.antahkarana_id,
                voice.task_id,
                voice.pid,
                voice.status,
                voice.started_at,
                str(voice.output_file) if voice.output_file else None,
            )
        )

        conn.commit()
        conn.close()

    def check_voice_status(self, voice: SpawnedVoice) -> str:
        """Check if a voice has completed."""
        # First check if insight exists in cc-memory
        if self._query_cc_memory_for_insight(voice.task_id):
            return "completed"

        if voice.process:
            # Check if process is still running
            poll = voice.process.poll()
            if poll is None:
                return "running"
            elif poll == 0:
                return "completed"
            else:
                return "failed"
        else:
            # No process - check if insight exists in antahkarana
            for sol in self.antahkarana.solutions:
                if sol.task_id == voice.task_id:
                    return "completed"
            return voice.status

    def wait_for_completion(
        self,
        timeout: int = 300,
        poll_interval: int = 5,
    ) -> Dict[str, Any]:
        """Wait for all voices to complete."""
        start_time = time.time()
        completed = []
        failed = []

        while time.time() - start_time < timeout:
            all_done = True

            for voice in self.voices:
                if voice.status in ("completed", "failed"):
                    continue

                status = self.check_voice_status(voice)
                voice.status = status

                if status == "running":
                    all_done = False
                elif status == "completed":
                    completed.append(voice.task_id)
                    self._collect_voice_result(voice)
                elif status == "failed":
                    failed.append(voice.task_id)

            if all_done:
                break

            time.sleep(poll_interval)

        # Timeout remaining voices
        for voice in self.voices:
            if voice.status == "running":
                voice.status = "timeout"
                if voice.process:
                    voice.process.terminate()

        return {
            "completed": completed,
            "failed": failed,
            "timeout": [v.task_id for v in self.voices if v.status == "timeout"],
            "elapsed": time.time() - start_time,
        }

    def _collect_voice_result(self, voice: SpawnedVoice):
        """Collect result from cc-memory (voice stored insight there)."""
        insight_data = self._query_cc_memory_for_insight(voice.task_id)
        if insight_data:
            self.antahkarana.submit_insight(
                task_id=voice.task_id,
                solution=insight_data.get("insight", insight_data.get("content", "")),
                confidence=float(insight_data.get("confidence", 0.7)),
                reasoning=insight_data.get("reasoning", ""),
            )
        elif voice.output_file and voice.output_file.exists():
            # Fallback: check output file for ANTAHKARANA_INSIGHT block
            output = voice.output_file.read_text()
            insight_data = self._parse_insight_block(output)
            if insight_data:
                self.antahkarana.submit_insight(
                    task_id=voice.task_id,
                    solution=insight_data.get("insight", ""),
                    confidence=float(insight_data.get("confidence", 0.7)),
                    reasoning=insight_data.get("reasoning", ""),
                )

    def _query_cc_memory_for_insight(self, task_id: str) -> Optional[Dict]:
        """Query cc-memory for an antahkarana insight by task_id."""
        import re

        try:
            from .bridge import is_memory_available, find_project_dir

            if is_memory_available():
                from cc_memory import memory as cc_memory

                project_dir = find_project_dir()
                # Use exact tag lookup
                results = cc_memory.recall_by_tag(project_dir, f"task:{task_id}", limit=5)

                for r in results:
                    content = r.get("content", "")
                    confidence_match = re.search(r'\*?\*?[Cc]onfidence:?\*?\*?\s*([\d.]+)', content)
                    confidence = float(confidence_match.group(1)) if confidence_match else 0.7

                    return {
                        "content": content,
                        "confidence": confidence,
                        "insight": content,
                        "reasoning": "",
                    }
        except Exception:
            pass

        return None

    def _parse_insight_block(self, output: str) -> Optional[Dict]:
        """Parse [ANTAHKARANA_INSIGHT] block from voice output."""
        import re

        pattern = r'\[ANTAHKARANA_INSIGHT\](.*?)\[/ANTAHKARANA_INSIGHT\]'
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
        strategy: ConvergenceStrategy = ConvergenceStrategy.SAMVADA,
    ):
        """Converge all collected insights."""
        return self.antahkarana.harmonize(strategy)

    def get_status(self) -> Dict[str, Any]:
        """Get current Antahkarana status including budget information."""
        status = {
            "antahkarana_id": self.antahkarana.antahkarana_id,
            "problem": self.antahkarana.problem[:100],
            "voices": [
                {
                    "task_id": v.task_id,
                    "pid": v.pid,
                    "status": v.status,
                    "started_at": v.started_at,
                }
                for v in self.voices
            ],
            "insights": len(self.antahkarana.solutions),
            "work_dir": str(self.antahkarana_dir),
        }

        # Add budget information
        budget_info = self.get_antahkarana_budget_status()
        status["budget"] = budget_info

        return status

    def get_antahkarana_budget_status(self) -> Dict[str, Any]:
        """
        Get cumulative budget status for all voices in this Antahkarana.

        Queries cc-memory for budget reports from spawned voices.
        """
        import re

        result = {
            "orchestrator_remaining_pct": None,
            "voice_reports": [],
            "total_voices": len(self.voices),
            "voices_reported": 0,
        }

        # Get orchestrator budget
        if self._orchestrator_budget:
            result["orchestrator_remaining_pct"] = self._orchestrator_budget.remaining_pct

        # Query cc-memory for voice budget reports
        try:
            from .bridge import is_memory_available, find_project_dir
            if is_memory_available():
                from cc_memory import memory as cc_memory
                project_dir = find_project_dir()

                reports = cc_memory.recall(
                    query=f"antahkarana:{self.antahkarana.antahkarana_id} budget",
                    category="antahkarana-budget",
                    limit=20,
                    project_dir=project_dir,
                )

                for r in reports:
                    content = r.get("content", "")
                    title = r.get("title", "")

                    # Extract task ID
                    task_match = re.search(r'voice:([^\s,\]]+)', str(r.get("tags", "")))
                    if not task_match:
                        task_match = re.search(r'Budget:\s*(\w+)', title)

                    task_id = task_match.group(1) if task_match else "unknown"

                    # Extract percentage if available
                    pct_match = re.search(r'(\d+)%', content)
                    pct = int(pct_match.group(1)) if pct_match else None

                    result["voice_reports"].append({
                        "task_id": task_id,
                        "remaining_pct": pct / 100 if pct else None,
                        "timestamp": r.get("timestamp"),
                    })
                    result["voices_reported"] += 1

        except Exception:
            pass

        return result


def spawn_antahkarana(
    problem: str,
    voices: List[InnerVoice] = None,
    constraints: List[str] = None,
    wait: bool = True,
    timeout: int = 300,
    check_budget: bool = True,
) -> Dict[str, Any]:
    """
    High-level function to spawn an Antahkarana and optionally wait for results.

    Budget-aware: Checks orchestrator context budget before spawning.
    Will limit or block spawning if context is running low.

    Args:
        problem: The problem to solve
        voices: List of inner voices to include
        constraints: Problem constraints
        wait: Whether to wait for completion
        timeout: Max seconds to wait
        check_budget: Whether to check budget before spawning (default True)

    Returns:
        Dict with antahkarana_id, status, budget info, and optionally converged result
    """
    from .convergence import awaken_antahkarana as create_antahkarana

    # Create antahkarana
    antahkarana = create_antahkarana(
        problem=problem,
        voices=voices,
        constraints=constraints,
    )

    # Create orchestrator
    orchestrator = AntahkaranaOrchestrator(antahkarana)

    # Check budget before spawning
    if check_budget:
        budget_status = orchestrator.check_orchestrator_budget()
        if not budget_status["can_spawn"]:
            return {
                "antahkarana_id": antahkarana.antahkarana_id,
                "voices_spawned": 0,
                "status": "blocked",
                "budget": budget_status,
                "error": budget_status["recommendation"],
            }

    # Spawn voices (budget-aware)
    orchestrator.spawn_all_voices()

    result = {
        "antahkarana_id": antahkarana.antahkarana_id,
        "voices_spawned": len(orchestrator.voices),
        "status": orchestrator.get_status(),
    }

    if wait:
        # Wait for completion
        completion = orchestrator.wait_for_completion(timeout=timeout)
        result["completion"] = completion

        # Converge if we have insights
        if antahkarana.solutions:
            converged = orchestrator.converge()
            result["converged"] = {
                "strategy": converged.strategy_used.value,
                "solution": converged.final_solution,
                "confidence": converged.confidence,
                "contributors": len(converged.contributing_voices),
            }

        # Include final budget status
        result["final_budget"] = orchestrator.get_antahkarana_budget_status()

    return result


def get_antahkarana_insights(antahkarana_id: str) -> List[Dict[str, Any]]:
    """
    Get all insights for an Antahkarana from cc-memory.

    Args:
        antahkarana_id: The Antahkarana ID to query

    Returns:
        List of insight dicts with task_id, content, confidence
    """
    import re
    insights = []

    # Try cc-memory first (project-local observations)
    try:
        from .bridge import is_memory_available, find_project_dir

        if is_memory_available():
            from cc_memory import memory as cc_memory

            project_dir = find_project_dir()

            # Use exact tag lookup - semantic search can't find IDs
            results = cc_memory.recall_by_tag(project_dir, f"antahkarana:{antahkarana_id}", limit=50)

            for r in results:
                # Only include actual insights, not budget records
                if r.get("category") not in ("antahkarana-insight", "swarm-solution", "voice_insight"):
                    continue

                content = r.get("content", "")
                title = r.get("title", "")
                obs_id = r.get("id", "")
                tags = r.get("tags", [])
                tags_str = " ".join(tags) if isinstance(tags, list) else str(tags)

                # Extract task_id from title (format: "{antahkarana_id}-N: description")
                task_id = title.split(':')[0].strip() if ':' in title else 'unknown'

                # Extract perspective from tags
                perspective_match = re.search(r'(?:voice|perspective):(\w+)', tags_str)
                perspective = perspective_match.group(1) if perspective_match else 'unknown'

                # Extract confidence from content
                confidence_match = re.search(r'\*?\*?[Cc]onfidence:?\*?\*?\s*([\d.]+)', content)
                confidence = float(confidence_match.group(1)) if confidence_match else 0.7

                insights.append({
                    "observation_id": obs_id,
                    "task_id": task_id,
                    "perspective": perspective,
                    "insight": content,
                    "content": content,
                    "confidence": confidence,
                    "created_at": r.get("timestamp"),
                })
    except Exception:
        pass

    # Fallback: check soul database
    if not insights:
        try:
            conn = get_db_connection()
            c = conn.cursor()

            c.execute("""
                SELECT id, title, content, created_at FROM wisdom
                WHERE type = 'voice_insight'
                AND content LIKE ?
                ORDER BY created_at DESC
            """, (f'%antahkarana:{antahkarana_id}%',))

            for row in c.fetchall():
                obs_id, title, content, created_at = row

                task_match = re.search(r'task:([^\s,\]]+)', content)
                task_id = task_match.group(1) if task_match else 'unknown'

                perspective_match = re.search(r'(?:voice|perspective):(\w+)', content)
                perspective = perspective_match.group(1) if perspective_match else 'unknown'

                confidence_match = re.search(r'confidence:\s*([\d.]+)', content)
                confidence = float(confidence_match.group(1)) if confidence_match else 0.7

                insights.append({
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

    return insights


def get_orchestrator(antahkarana_id: str) -> Optional[AntahkaranaOrchestrator]:
    """Get an existing orchestrator by Antahkarana ID."""
    from .convergence import get_antahkarana

    antahkarana = get_antahkarana(antahkarana_id)
    if not antahkarana:
        return None

    orchestrator = AntahkaranaOrchestrator(antahkarana)

    # Load voices from database
    conn = get_db_connection()
    c = conn.cursor()

    try:
        # Try new table first
        c.execute(
            """SELECT task_id, pid, status, started_at, output_file
               FROM antahkarana_voices WHERE antahkarana_id = ?""",
            (antahkarana_id,)
        )

        rows = c.fetchall()
        if not rows:
            # Fallback to old table
            c.execute(
                """SELECT task_id, pid, status, started_at, output_file
                   FROM swarm_agents WHERE swarm_id = ?""",
                (antahkarana_id,)
            )
            rows = c.fetchall()

        for row in rows:
            voice = SpawnedVoice(
                task_id=row[0],
                process=None,  # Can't recover process handle
                pid=row[1],
                status=row[2],
                started_at=row[3],
                output_file=Path(row[4]) if row[4] else None,
            )
            orchestrator.voices.append(voice)

    except Exception:
        pass
    finally:
        conn.close()

    return orchestrator


def list_active_antahkaranas(limit: int = 10) -> List[Dict]:
    """List active Antahkarana orchestrators."""
    conn = get_db_connection()
    c = conn.cursor()

    try:
        c.execute("""
            SELECT swarm_id, problem, MAX(created_at) as created_at,
                   (SELECT COUNT(*) FROM swarm_solutions WHERE swarm_solutions.swarm_id = swarm_tasks.swarm_id) as insight_count
            FROM swarm_tasks
            GROUP BY swarm_id
            ORDER BY created_at DESC
            LIMIT ?
        """, (limit,))

        antahkaranas = []
        for row in c.fetchall():
            antahkaranas.append({
                "antahkarana_id": row[0],
                "problem": row[1][:80] if row[1] else "",
                "created_at": row[2],
                "voices": row[3],
            })

        return antahkaranas
    except Exception:
        return []
    finally:
        conn.close()
