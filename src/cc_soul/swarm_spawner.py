"""
Antahkarana Spawner - Spawn real Claude voices for parallel problem solving.

Uses the claude CLI to spawn actual Claude instances that contemplate different
facets of a problem simultaneously. Insights are collected via synapse graph.

Architecture:
    1. Orchestrator awakens voices with specific prompts
    2. Each voice is executed as a separate claude process
    3. Voices write insights to synapse with antahkarana tags
    4. Orchestrator polls synapse for completion
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
    - Memory context: Relevant past decisions/discoveries from synapse
"""

import subprocess
import json
import time
import os
import re
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
from .core import get_synapse_graph, save_synapse, SOUL_DIR
from .budget import (
    get_context_budget,
    get_all_session_budgets,
    log_budget_to_synapse,
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

        log_budget_to_synapse(budget)

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
        base_prompt = task.to_prompt()

        memory_instructions = f"""

## Output Instructions

When you have your insight, output it in this format:

[ANTAHKARANA_INSIGHT]
task_id: {task.task_id}
perspective: {task.perspective.value}
confidence: 0.0-1.0
insight: |
  Your complete insight with reasoning
reasoning: |
  Why this approach makes sense
[/ANTAHKARANA_INSIGHT]

The orchestrator will collect your insight from this block.
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

        cmd = [
            "claude",
            "--model", self.model,
            "--dangerously-skip-permissions",
        ]

        prompt = prompt_file.read_text()

        try:
            voice_env = os.environ.copy()
            voice_env["CC_SOUL_ANTAHKARANA_VOICE"] = "1"
            voice_env["CC_SOUL_ANTAHKARANA_ID"] = self.antahkarana.antahkarana_id
            voice_env["CC_SOUL_TASK_ID"] = task.task_id
            voice_env["CC_SOUL_PERSPECTIVE"] = task.perspective.value
            from .budget import get_session_id
            voice_env["CC_SOUL_PARENT_SESSION"] = get_session_id()

            process = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=open(output_file, 'w'),
                stderr=subprocess.PIPE,
                text=True,
                cwd=str(Path.cwd()),
                env=voice_env,
            )

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

            self._record_voice_spawn(voice, task)

            return voice

        except FileNotFoundError:
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

        voice = SpawnedVoice(
            task_id=task.task_id,
            process=None,
            pid=0,
            started_at=datetime.now().isoformat(),
            status="awaiting",
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
        budget_status = self.check_orchestrator_budget()

        if not budget_status["can_spawn"]:
            self._log_spawn_blocked(budget_status)
            return []

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
            graph = get_synapse_graph()
            graph.observe(
                category="antahkarana_budget",
                title=f"Antahkarana {self.antahkarana.antahkarana_id}: BLOCKED",
                content=f"""Antahkarana spawn blocked due to budget constraints.
Remaining: {budget_status['remaining_pct']*100:.0f}%
Pressure: {budget_status['pressure']}
Recommendation: {budget_status['recommendation']}
Problem: {self.antahkarana.problem[:200]}""",
                tags=["antahkarana", "budget", "blocked", f"antahkarana:{self.antahkarana.antahkarana_id}"],
            )
            save_synapse()
        except Exception:
            pass

    def _log_spawn_limited(self, spawned: int, total: int, reason: str):
        """Log when spawning is limited due to budget constraints."""
        try:
            graph = get_synapse_graph()
            graph.observe(
                category="antahkarana_budget",
                title=f"Antahkarana {self.antahkarana.antahkarana_id}: LIMITED",
                content=f"""Antahkarana spawn limited due to budget constraints.
Spawned: {spawned}/{total} voices
Reason: {reason}
Problem: {self.antahkarana.problem[:200]}""",
                tags=["antahkarana", "budget", "limited", f"antahkarana:{self.antahkarana.antahkarana_id}"],
            )
            save_synapse()
        except Exception:
            pass

    def _record_voice_spawn(self, voice: SpawnedVoice, task: VoiceTask):
        """Record voice spawn in synapse graph."""
        graph = get_synapse_graph()

        graph.observe(
            category="antahkarana_voice",
            title=f"{self.antahkarana.antahkarana_id}:{voice.task_id}",
            content=json.dumps({
                "antahkarana_id": self.antahkarana.antahkarana_id,
                "task_id": voice.task_id,
                "pid": voice.pid,
                "status": voice.status,
                "started_at": voice.started_at,
                "completed_at": None,
                "output_file": str(voice.output_file) if voice.output_file else None,
            }),
            tags=["antahkarana_voice", f"antahkarana:{self.antahkarana.antahkarana_id}", f"task:{voice.task_id}"],
        )

        save_synapse()

    def check_voice_status(self, voice: SpawnedVoice) -> str:
        """Check if a voice has completed."""
        if self._query_synapse_for_insight(voice.task_id):
            return "completed"

        if voice.process:
            poll = voice.process.poll()
            if poll is None:
                return "running"
            elif poll == 0:
                return "completed"
            else:
                return "failed"
        else:
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
        """Collect result from synapse or output file."""
        insight_data = self._query_synapse_for_insight(voice.task_id)
        if insight_data:
            self.antahkarana.submit_insight(
                task_id=voice.task_id,
                solution=insight_data.get("insight", insight_data.get("content", "")),
                confidence=float(insight_data.get("confidence", 0.7)),
                reasoning=insight_data.get("reasoning", ""),
            )
        elif voice.output_file and voice.output_file.exists():
            output = voice.output_file.read_text()
            insight_data = self._parse_insight_block(output)
            if insight_data:
                self.antahkarana.submit_insight(
                    task_id=voice.task_id,
                    solution=insight_data.get("insight", ""),
                    confidence=float(insight_data.get("confidence", 0.7)),
                    reasoning=insight_data.get("reasoning", ""),
                )

    def _query_synapse_for_insight(self, task_id: str) -> Optional[Dict]:
        """Query synapse for an antahkarana insight by task_id."""
        try:
            graph = get_synapse_graph()

            episodes = graph.get_episodes(category="antahkarana_insight", limit=100)

            for ep in episodes:
                tags = ep.get("tags", [])
                if f"task:{task_id}" in tags:
                    content = ep.get("content", "")
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
        pattern = r'\[ANTAHKARANA_INSIGHT\](.*?)\[/ANTAHKARANA_INSIGHT\]'
        match = re.search(pattern, output, re.DOTALL)

        if not match:
            return None

        block = match.group(1).strip()
        result = {}

        current_key = None
        current_value = []

        for line in block.split('\n'):
            if line.startswith('  ') and current_key:
                current_value.append(line.strip())
            elif ':' in line:
                if current_key and current_value:
                    result[current_key] = '\n'.join(current_value)

                parts = line.split(':', 1)
                current_key = parts[0].strip()
                value = parts[1].strip() if len(parts) > 1 else ""

                if value == '|':
                    current_value = []
                else:
                    result[current_key] = value
                    current_key = None
                    current_value = []

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

        budget_info = self.get_antahkarana_budget_status()
        status["budget"] = budget_info

        return status

    def get_antahkarana_budget_status(self) -> Dict[str, Any]:
        """
        Get cumulative budget status for all voices in this Antahkarana.

        Queries synapse for budget reports from spawned voices.
        """
        result = {
            "orchestrator_remaining_pct": None,
            "voice_reports": [],
            "total_voices": len(self.voices),
            "voices_reported": 0,
        }

        if self._orchestrator_budget:
            result["orchestrator_remaining_pct"] = self._orchestrator_budget.remaining_pct

        try:
            graph = get_synapse_graph()

            episodes = graph.get_episodes(category="antahkarana_budget", limit=50)

            for ep in episodes:
                tags = ep.get("tags", [])
                if f"antahkarana:{self.antahkarana.antahkarana_id}" not in tags:
                    continue

                content = ep.get("content", "")
                title = ep.get("title", "")

                task_match = re.search(r'voice:([^\s,\]]+)', str(tags))
                if not task_match:
                    task_match = re.search(r'Budget:\s*(\w+)', title)

                task_id = task_match.group(1) if task_match else "unknown"

                pct_match = re.search(r'(\d+)%', content)
                pct = int(pct_match.group(1)) if pct_match else None

                result["voice_reports"].append({
                    "task_id": task_id,
                    "remaining_pct": pct / 100 if pct else None,
                    "timestamp": ep.get("timestamp", ep.get("created_at")),
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

    antahkarana = create_antahkarana(
        problem=problem,
        voices=voices,
        constraints=constraints,
    )

    orchestrator = AntahkaranaOrchestrator(antahkarana)

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

    orchestrator.spawn_all_voices()

    result = {
        "antahkarana_id": antahkarana.antahkarana_id,
        "voices_spawned": len(orchestrator.voices),
        "status": orchestrator.get_status(),
    }

    if wait:
        completion = orchestrator.wait_for_completion(timeout=timeout)
        result["completion"] = completion

        if antahkarana.solutions:
            converged = orchestrator.converge()
            result["converged"] = {
                "strategy": converged.strategy_used.value,
                "solution": converged.final_solution,
                "confidence": converged.confidence,
                "contributors": len(converged.contributing_voices),
            }

        result["final_budget"] = orchestrator.get_antahkarana_budget_status()

    return result


def get_antahkarana_insights(antahkarana_id: str) -> List[Dict[str, Any]]:
    """
    Get all insights for an Antahkarana from synapse.

    Args:
        antahkarana_id: The Antahkarana ID to query

    Returns:
        List of insight dicts with task_id, content, confidence
    """
    insights = []

    try:
        graph = get_synapse_graph()

        episodes = graph.get_episodes(category="antahkarana_insight", limit=100)

        for ep in episodes:
            tags = ep.get("tags", [])
            if f"antahkarana:{antahkarana_id}" not in tags:
                continue

            content = ep.get("content", "")
            title = ep.get("title", "")
            obs_id = ep.get("id", "")

            task_id = title.split(':')[0].strip() if ':' in title else 'unknown'

            perspective_match = re.search(r'(?:voice|perspective):(\w+)', str(tags))
            perspective = perspective_match.group(1) if perspective_match else 'unknown'

            confidence_match = re.search(r'\*?\*?[Cc]onfidence:?\*?\*?\s*([\d.]+)', content)
            confidence = float(confidence_match.group(1)) if confidence_match else 0.7

            insights.append({
                "observation_id": obs_id,
                "task_id": task_id,
                "perspective": perspective,
                "insight": content,
                "content": content,
                "confidence": confidence,
                "created_at": ep.get("timestamp", ep.get("created_at")),
            })
    except Exception:
        pass

    if not insights:
        try:
            graph = get_synapse_graph()

            episodes = graph.get_episodes(category="swarm_solution", limit=100)

            for ep in episodes:
                try:
                    data = json.loads(ep.get("content", "{}"))
                except (json.JSONDecodeError, TypeError):
                    continue

                if data.get("swarm_id") == antahkarana_id:
                    insights.append({
                        "observation_id": ep.get("id"),
                        "task_id": data.get("task_id", "unknown"),
                        "perspective": data.get("perspective", "unknown"),
                        "content": data.get("solution", ""),
                        "confidence": data.get("confidence", 0.7),
                        "created_at": data.get("created_at"),
                    })

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

    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="antahkarana_voice", limit=100)

    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        if data.get("antahkarana_id") == antahkarana_id:
            voice = SpawnedVoice(
                task_id=data.get("task_id"),
                process=None,
                pid=data.get("pid", 0),
                status=data.get("status", "unknown"),
                started_at=data.get("started_at", ""),
                output_file=Path(data["output_file"]) if data.get("output_file") else None,
            )
            orchestrator.voices.append(voice)

    return orchestrator


@dataclass
class DelegatedTask:
    """A task delegated to a sub-Claude instance for context preservation."""
    task_id: str
    prompt: str
    summary: Optional[str] = None
    full_result_id: Optional[str] = None  # ID in synapse
    status: str = "pending"  # pending, running, completed, failed
    started_at: Optional[str] = None
    completed_at: Optional[str] = None


def delegate_task(
    prompt: str,
    task_id: Optional[str] = None,
    model: str = "sonnet",
    timeout: int = 180,
    wait: bool = True,
) -> Dict[str, Any]:
    """
    Delegate a heavy task to a sub-Claude instance.

    The sub-Claude does full work, stores complete output in synapse,
    and returns a distilled summary. This preserves main Claude's context.

    Pattern (Upanishadic model):
    1. Main Claude detects heavy task
    2. Spawns sub-Claude with this function
    3. Sub-Claude does full work, stores in synapse
    4. Sub-Claude returns distilled summary
    5. Main Claude continues with summary + can query synapse for details

    Args:
        prompt: The task/question for sub-Claude
        task_id: Optional ID (generated if not provided)
        model: Model to use (sonnet for speed, opus for depth)
        timeout: Max seconds to wait (if wait=True)
        wait: If True, wait for completion. If False, return immediately.

    Returns:
        Dict with task_id, summary, full_result_id, status
    """
    import uuid

    if not task_id:
        task_id = f"delegate-{uuid.uuid4().hex[:8]}"

    work_dir = Path.home() / ".claude" / "delegated"
    work_dir.mkdir(parents=True, exist_ok=True)

    delegation_prompt = f"""## Delegated Task

You are a sub-Claude instance handling a delegated task. Your job:
1. Do the full work requested below
2. Store your COMPLETE output (the orchestrator will extract it)
3. Return a DISTILLED summary

IMPORTANT: The orchestrator has limited context. Your summary should be
concise (max 500 words) but capture the essential answer/result.

## Task ID: {task_id}

## Request
{prompt}

## Output Protocol

When complete, output ONLY a distilled summary (the last thing you output):
Start with "## Summary" and provide a concise answer.

The orchestrator will see only this summary.
"""

    prompt_file = work_dir / f"{task_id}.prompt"
    prompt_file.write_text(delegation_prompt)

    output_file = work_dir / f"{task_id}.output"

    task = DelegatedTask(
        task_id=task_id,
        prompt=prompt,
        started_at=datetime.now().isoformat(),
    )

    cmd = ["claude", "--model", model, "--dangerously-skip-permissions"]

    try:
        env = os.environ.copy()
        env["CC_SOUL_DELEGATED_TASK"] = "1"
        env["CC_SOUL_TASK_ID"] = task_id

        process = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=open(output_file, 'w'),
            stderr=subprocess.PIPE,
            text=True,
            cwd=str(Path.cwd()),
            env=env,
        )

        process.stdin.write(delegation_prompt)
        process.stdin.close()

        task.status = "running"

        _record_delegated_task(task)

        if not wait:
            return {
                "task_id": task_id,
                "status": "running",
                "message": "Task delegated. Use get_delegated_result() to check status.",
            }

        try:
            process.wait(timeout=timeout)
            task.status = "completed" if process.returncode == 0 else "failed"
        except subprocess.TimeoutExpired:
            process.terminate()
            task.status = "timeout"

        task.completed_at = datetime.now().isoformat()

        if output_file.exists():
            output = output_file.read_text()
            task.summary = _extract_summary(output)
            task.full_result_id = _store_result_in_synapse(task_id, output)

        _update_delegated_task(task)

        return {
            "task_id": task_id,
            "status": task.status,
            "summary": task.summary or "(no summary extracted)",
            "full_result_id": task.full_result_id,
            "elapsed": (datetime.fromisoformat(task.completed_at) -
                       datetime.fromisoformat(task.started_at)).total_seconds()
                       if task.completed_at and task.started_at else None,
        }

    except FileNotFoundError:
        return {
            "task_id": task_id,
            "status": "error",
            "error": "claude CLI not found",
        }
    except Exception as e:
        return {
            "task_id": task_id,
            "status": "error",
            "error": str(e),
        }


def _extract_summary(output: str) -> Optional[str]:
    """Extract the summary section from sub-Claude output."""
    match = re.search(r'## Summary\s*\n(.*?)(?=\n##|\Z)', output, re.DOTALL)
    if match:
        return match.group(1).strip()[:2000]

    if len(output) > 500:
        return f"(No explicit summary. Last output:)\n{output[-500:]}"

    return output.strip() if output else None


def _store_result_in_synapse(task_id: str, output: str) -> Optional[str]:
    """Store the full result in synapse."""
    try:
        graph = get_synapse_graph()
        obs_id = graph.observe(
            category="delegated_result",
            title=f"Result: {task_id}",
            content=output[:10000],  # Limit content size
            tags=["delegated", f"task:{task_id}"],
        )
        save_synapse()
        return obs_id
    except Exception:
        return None


def _record_delegated_task(task: DelegatedTask):
    """Record delegated task in synapse graph."""
    graph = get_synapse_graph()

    graph.observe(
        category="delegated_task",
        title=f"delegated:{task.task_id}",
        content=json.dumps({
            "task_id": task.task_id,
            "prompt": task.prompt[:1000],
            "status": task.status,
            "started_at": task.started_at,
            "completed_at": task.completed_at,
            "summary": task.summary,
            "full_result_id": task.full_result_id,
        }),
        tags=["delegated_task", f"task:{task.task_id}"],
    )

    save_synapse()


def _update_delegated_task(task: DelegatedTask):
    """Update delegated task status in synapse graph."""
    graph = get_synapse_graph()

    graph.observe(
        category="delegated_task",
        title=f"delegated:{task.task_id}",
        content=json.dumps({
            "task_id": task.task_id,
            "prompt": task.prompt[:1000] if task.prompt else "",
            "status": task.status,
            "started_at": task.started_at,
            "completed_at": task.completed_at,
            "summary": task.summary,
            "full_result_id": task.full_result_id,
        }),
        tags=["delegated_task", f"task:{task.task_id}", "updated"],
    )

    save_synapse()


def get_delegated_result(task_id: str) -> Dict[str, Any]:
    """
    Get the result of a delegated task.

    Args:
        task_id: The task ID to check

    Returns:
        Dict with status, summary, and full_result_id if available
    """
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="delegated_task", limit=100)

    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        if data.get("task_id") == task_id:
            return {
                "task_id": task_id,
                "status": data.get("status"),
                "summary": data.get("summary"),
                "full_result_id": data.get("full_result_id"),
                "started_at": data.get("started_at"),
                "completed_at": data.get("completed_at"),
            }

    return {"task_id": task_id, "status": "not_found"}


def get_full_result(task_id: str) -> Optional[str]:
    """
    Get the full result content from synapse for a delegated task.

    Use this when the summary isn't enough and you need full details.

    Args:
        task_id: The task ID

    Returns:
        Full content string or None
    """
    try:
        graph = get_synapse_graph()

        episodes = graph.get_episodes(category="delegated_result", limit=100)

        for ep in episodes:
            tags = ep.get("tags", [])
            if f"task:{task_id}" in tags:
                return ep.get("content")
    except Exception:
        pass

    return None


def list_active_antahkaranas(limit: int = 10) -> List[Dict]:
    """List active Antahkarana orchestrators."""
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="swarm_task", limit=limit * 10)

    swarm_data = {}
    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        swarm_id = data.get("swarm_id")
        if swarm_id:
            if swarm_id not in swarm_data:
                swarm_data[swarm_id] = {
                    "problem": data.get("problem", ""),
                    "created_at": data.get("created_at", ""),
                }

    solution_episodes = graph.get_episodes(category="swarm_solution", limit=limit * 10)
    solution_counts = {}
    for ep in solution_episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        swarm_id = data.get("swarm_id")
        if swarm_id:
            solution_counts[swarm_id] = solution_counts.get(swarm_id, 0) + 1

    antahkaranas = []
    for swarm_id, info in swarm_data.items():
        antahkaranas.append({
            "antahkarana_id": swarm_id,
            "problem": info["problem"][:80] if info["problem"] else "",
            "created_at": info["created_at"],
            "voices": solution_counts.get(swarm_id, 0),
        })

    antahkaranas.sort(key=lambda x: x.get("created_at", ""), reverse=True)
    return antahkaranas[:limit]
