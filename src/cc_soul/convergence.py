"""
Antahkarana: The Inner Instrument of Multiple Voices.

In Upanishadic philosophy, Antahkarana is the inner organ of consciousness
comprising four facets: Manas (sensory mind), Buddhi (intellect), Chitta
(memory/subconscious), and Ahamkara (ego/I-maker). These aren't separate
entities but aspects of one consciousness.

This module embodies that insight: multiple voices (InnerVoice) are not
separate agents but facets of one problem-solving consciousness. They
don't debate as opponents--they contribute as aspects of a unified mind.

Architecture:
    +-------------+
    |   Atman     | (witness/orchestrator)
    +------+------+
           | activates
    +------+------+
    | Antahkarana | (inner instrument)
    |  +---+---+  |
    |  |M  |B  |  | Manas, Buddhi
    |  +---+---+  |
    |  |C  |A  |  | Chitta, Ahamkara
    |  +---+---+  |
    +------+------+
           | writes to
    +------+------+
    |   Chitta    | (shared memory/synapse)
    +------+------+
           | samvada (dialogue)
    +------+------+
    |   Viveka    | (discerned truth)
    +-------------+

Communication: Voices write to shared Chitta (synapse). The Atman
(witness-self) reads and synthesizes through Samvada (harmonious dialogue).
"""

import json
import uuid
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import List, Dict, Optional, Any, Callable

from .core import get_synapse_graph, save_synapse, init_soul


class InnerVoice(Enum):
    """
    Facets of the Antahkarana (inner instrument).

    In Upanishadic philosophy, the mind has four aspects that work together:
    - Manas: The sensory mind, first impressions, quick intuition
    - Buddhi: The intellect, discrimination, deep analysis
    - Chitta: Memory and subconscious patterns, practical wisdom
    - Ahamkara: The ego/I-maker, self-protective criticism

    Extended with additional voices:
    - Vikalpa: Creative imagination, novel approaches
    - Sakshi: The witness, detached minimal observation
    """
    MANAS = "manas"         # Sensory mind - quick first impression
    BUDDHI = "buddhi"       # Intellect - thorough discrimination
    CHITTA = "chitta"       # Memory/patterns - practical wisdom
    AHAMKARA = "ahamkara"   # Ego - finds flaws, self-protective
    VIKALPA = "vikalpa"     # Imagination - creative, novel
    SAKSHI = "sakshi"       # Witness - minimal, detached


class ConvergenceStrategy(Enum):
    """
    How the voices of Antahkarana harmonize into truth.

    Named after Upanishadic methods of knowledge:
    - Sankhya: Enumeration, counting (voting)
    - Samvada: Harmonious dialogue (synthesis)
    - Tarka: Dialectical reasoning (debate)
    - Viveka: Discrimination, discernment (ranking)
    - Pratyaksha: Direct perception (first valid)
    """
    SANKHYA = "sankhya"       # Enumeration - highest confidence wins
    SAMVADA = "samvada"       # Dialogue - synthesize best parts
    TARKA = "tarka"           # Dialectic - iterative refinement
    VIVEKA = "viveka"         # Discernment - score and rank
    PRATYAKSHA = "pratyaksha" # Direct perception - first valid


@dataclass
class VoiceTask:
    """A task for one voice of the Antahkarana."""
    task_id: str
    problem: str
    perspective: InnerVoice
    constraints: List[str] = field(default_factory=list)
    context: str = ""
    max_tokens: int = 2000

    def to_prompt(self) -> str:
        """Convert to a prompt for the voice."""
        lines = [
            f"## Problem",
            self.problem,
            "",
            f"## Your Voice: {self.perspective.value.upper()}",
        ]

        perspective_guidance = {
            InnerVoice.MANAS: (
                "You are Manas, the sensory mind. Trust your first impression. "
                "Be quick and direct. Don't overthink--give your immediate, intuitive response."
            ),
            InnerVoice.BUDDHI: (
                "You are Buddhi, the discriminating intellect. Analyze thoroughly. "
                "Consider all aspects and edge cases. Explain your reasoning step by step. "
                "Your role is to see clearly what is true."
            ),
            InnerVoice.CHITTA: (
                "You are Chitta, memory and accumulated wisdom. What patterns from the past apply? "
                "What's actually implementable based on experience? Consider practical constraints."
            ),
            InnerVoice.AHAMKARA: (
                "You are Ahamkara, the self-protective critic. Find flaws. What could go wrong? "
                "What's missing? Your role is to protect by questioning. Be the devil's advocate."
            ),
            InnerVoice.VIKALPA: (
                "You are Vikalpa, creative imagination. Think unconventionally. "
                "What would surprise? Challenge all assumptions. Propose the unexpected."
            ),
            InnerVoice.SAKSHI: (
                "You are Sakshi, the witness. Observe without attachment. "
                "What's the absolute simplest truth? Remove everything non-essential. "
                "Distill to pure essence."
            ),
        }

        lines.append(perspective_guidance.get(self.perspective, ""))
        lines.append("")

        if self.constraints:
            lines.append("## Constraints")
            for c in self.constraints:
                lines.append(f"- {c}")
            lines.append("")

        if self.context:
            lines.append("## Context")
            lines.append(self.context)
            lines.append("")

        lines.append("## Your Solution")
        lines.append("Provide your solution clearly. Be specific and actionable.")

        return "\n".join(lines)


@dataclass
class VoiceSolution:
    """A solution from one voice of the Antahkarana."""
    task_id: str
    agent_id: str
    perspective: InnerVoice
    solution: str
    confidence: float  # Shraddha - faith/confidence in this perspective
    reasoning: str
    timestamp: str
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class SamvadaResult:
    """
    Result of Samvada (harmonious dialogue) between voices.

    When multiple voices of the Antahkarana speak, their insights
    must be harmonized into Viveka (discerned truth). This result
    captures both the final understanding and the voices that
    contributed to it.
    """
    final_solution: str
    strategy_used: ConvergenceStrategy
    contributing_voices: List[str]
    synthesis_notes: str
    confidence: float  # Shraddha - overall confidence
    dialogue_rounds: int = 0  # For Tarka (debate)
    dissenting_views: List[str] = field(default_factory=list)


def _ensure_convergence_tables():
    """Ensure synapse graph is initialized (tables are implicit)."""
    get_synapse_graph()


class Antahkarana:
    """
    The Inner Instrument - multiple voices working as one consciousness.

    In Upanishadic philosophy, Antahkarana is the internal organ of
    consciousness comprising Manas (sensory mind), Buddhi (intellect),
    Chitta (memory), and Ahamkara (ego). These are not separate entities
    but facets of one mind examining a problem from different angles.

    Usage:
        mind = Antahkarana(problem="How should we architect the cache layer?")
        mind.add_voice(InnerVoice.MANAS)    # Quick intuition
        mind.add_voice(InnerVoice.BUDDHI)   # Deep analysis
        mind.add_voice(InnerVoice.AHAMKARA) # Critical examination

        # Activate voices and collect insights
        mind.activate_all()
        insights = mind.gather_insights()

        # Harmonize through Samvada (dialogue)
        result = mind.harmonize(ConvergenceStrategy.SAMVADA)
    """

    def __init__(
        self,
        problem: str,
        constraints: List[str] = None,
        context: str = "",
        antahkarana_id: str = None,
    ):
        self.antahkarana_id = antahkarana_id or str(uuid.uuid4())[:8]
        self.problem = problem
        self.constraints = constraints or []
        self.context = context
        self.tasks: List[VoiceTask] = []
        self.solutions: List[VoiceSolution] = []
        self._activated = False

        init_soul()
        _ensure_convergence_tables()

    @property
    def insights(self) -> List["VoiceSolution"]:
        """Alias for solutions."""
        return self.solutions

    def add_voice(
        self,
        voice: InnerVoice,
        extra_constraints: List[str] = None,
        extra_context: str = "",
    ) -> str:
        """Add a voice to the Antahkarana. Returns task_id."""
        task_id = f"{self.antahkarana_id}-{len(self.tasks)}"

        task = VoiceTask(
            task_id=task_id,
            problem=self.problem,
            perspective=voice,
            constraints=self.constraints + (extra_constraints or []),
            context=self.context + ("\n\n" + extra_context if extra_context else ""),
        )

        self.tasks.append(task)

        graph = get_synapse_graph()
        graph.observe(
            category="swarm_task",
            title=f"{self.antahkarana_id}:{task_id}",
            content=json.dumps({
                "swarm_id": self.antahkarana_id,
                "task_id": task_id,
                "problem": self.problem,
                "perspective": voice.value,
                "constraints": task.constraints,
                "context": task.context,
                "status": "pending",
                "created_at": datetime.now().isoformat(),
            }),
            tags=["swarm_task", f"swarm:{self.antahkarana_id}", f"task:{task_id}", voice.value],
        )
        save_synapse()

        return task_id

    def add_core_voices(self):
        """Add the four core facets of Antahkarana for balanced analysis."""
        self.add_voice(InnerVoice.MANAS)     # Quick intuition
        self.add_voice(InnerVoice.BUDDHI)    # Deep analysis
        self.add_voice(InnerVoice.AHAMKARA)  # Critical examination

    def add_creative_voices(self):
        """Add voices for creative problem-solving."""
        self.add_voice(InnerVoice.VIKALPA)  # Creative imagination
        self.add_voice(InnerVoice.SAKSHI)   # Detached witness
        self.add_voice(InnerVoice.CHITTA)   # Pattern-based wisdom

    def activate_all(self, parallel: bool = True) -> List[str]:
        """
        Activate all voices to contemplate the problem.

        In real implementation, this would use Claude Code's Task tool
        with run_in_background=True.

        Returns list of voice_ids.
        """
        voice_ids = []

        for task in self.tasks:
            voice_id = self._activate_voice(task)
            voice_ids.append(voice_id)

        self._activated = True
        return voice_ids

    def _activate_voice(self, task: VoiceTask) -> str:
        """
        Activate a single voice.

        This is where we'd integrate with Claude Code's Task tool.
        For now, we create a placeholder that can be filled by the orchestrator.
        """
        voice_id = f"voice-{task.task_id}"

        graph = get_synapse_graph()

        episodes = graph.get_episodes(category="swarm_task", limit=100)
        for ep in episodes:
            try:
                data = json.loads(ep.get("content", "{}"))
            except (json.JSONDecodeError, TypeError):
                continue

            if data.get("task_id") == task.task_id:
                data["status"] = "activated"
                graph.observe(
                    category="swarm_task",
                    title=f"{self.antahkarana_id}:{task.task_id}",
                    content=json.dumps(data),
                    tags=["swarm_task", f"swarm:{self.antahkarana_id}", f"task:{task.task_id}", "activated"],
                )
                save_synapse()
                break

        return voice_id

    def submit_insight(
        self,
        task_id: str,
        solution: str,
        confidence: float = 0.7,
        reasoning: str = "",
    ) -> VoiceSolution:
        """
        Submit an insight from a voice (called by voice or simulated).
        """
        task = next((t for t in self.tasks if t.task_id == task_id), None)
        if not task:
            raise ValueError(f"Unknown task_id: {task_id}")

        voice_id = f"voice-{task_id}"

        sol = VoiceSolution(
            task_id=task_id,
            agent_id=voice_id,
            perspective=task.perspective,
            solution=solution,
            confidence=confidence,
            reasoning=reasoning,
            timestamp=datetime.now().isoformat(),
        )

        self.solutions.append(sol)

        graph = get_synapse_graph()

        graph.observe(
            category="swarm_solution",
            title=f"{self.antahkarana_id}:{task_id}",
            content=json.dumps({
                "swarm_id": self.antahkarana_id,
                "task_id": task_id,
                "agent_id": voice_id,
                "perspective": task.perspective.value,
                "solution": solution,
                "confidence": confidence,
                "reasoning": reasoning,
                "created_at": sol.timestamp,
            }),
            tags=["swarm_solution", f"swarm:{self.antahkarana_id}", f"task:{task_id}", task.perspective.value],
        )

        episodes = graph.get_episodes(category="swarm_task", limit=100)
        for ep in episodes:
            try:
                data = json.loads(ep.get("content", "{}"))
            except (json.JSONDecodeError, TypeError):
                continue

            if data.get("task_id") == task_id:
                data["status"] = "completed"
                graph.observe(
                    category="swarm_task",
                    title=f"{self.antahkarana_id}:{task_id}",
                    content=json.dumps(data),
                    tags=["swarm_task", f"swarm:{self.antahkarana_id}", f"task:{task_id}", "completed"],
                )
                break

        save_synapse()

        self._write_to_chitta(sol)

        return sol

    def _write_to_chitta(self, solution: VoiceSolution):
        """Write insight to synapse (Chitta - shared memory)."""
        try:
            graph = get_synapse_graph()
            graph.observe(
                category="voice_insight",
                title=f"[{solution.perspective.value}] {self.problem[:50]}",
                content=json.dumps({
                    "antahkarana_id": self.antahkarana_id,
                    "task_id": solution.task_id,
                    "insight": solution.solution,
                    "shraddha": solution.confidence,  # Faith/confidence
                    "reasoning": solution.reasoning,
                }),
                tags=["antahkarana", self.antahkarana_id, solution.perspective.value],
            )
            save_synapse()
        except Exception:
            pass

    def gather_insights(self, timeout_seconds: int = 300) -> List[VoiceSolution]:
        """
        Gather insights from all voices.

        In real implementation, this would poll TaskOutput until all complete.
        """
        return self.solutions

    def harmonize(
        self,
        strategy: ConvergenceStrategy = ConvergenceStrategy.SAMVADA,
        validator: Callable[[str], bool] = None,
    ) -> SamvadaResult:
        """
        Harmonize insights from all voices into Viveka (discerned truth).

        Strategies:
        - SANKHYA: Enumeration - highest shraddha (confidence) wins
        - SAMVADA: Dialogue - synthesize best parts harmoniously
        - TARKA: Dialectic - iterative critique and refinement
        - VIVEKA: Discernment - score by criteria, select wisest
        - PRATYAKSHA: Direct perception - first valid insight
        """
        if not self.solutions:
            raise ValueError("No insights to harmonize. Call activate_all() and submit_insight() first.")

        if strategy == ConvergenceStrategy.SANKHYA:
            return self._sankhya()
        elif strategy == ConvergenceStrategy.SAMVADA:
            return self._samvada()
        elif strategy == ConvergenceStrategy.TARKA:
            return self._tarka()
        elif strategy == ConvergenceStrategy.VIVEKA:
            return self._viveka()
        elif strategy == ConvergenceStrategy.PRATYAKSHA:
            return self._pratyaksha(validator)
        else:
            return self._samvada()

    def _sankhya(self) -> SamvadaResult:
        """Sankhya (enumeration) - highest shraddha (confidence) wins."""
        ranked = sorted(self.solutions, key=lambda s: s.confidence, reverse=True)
        winner = ranked[0]

        return SamvadaResult(
            final_solution=winner.solution,
            strategy_used=ConvergenceStrategy.SANKHYA,
            contributing_voices=[winner.agent_id],
            synthesis_notes=f"Sankhya winner: {winner.perspective.value} with {winner.confidence:.0%} shraddha",
            confidence=winner.confidence,
            dissenting_views=[s.solution[:100] for s in ranked[1:3]],
        )

    def _samvada(self) -> SamvadaResult:
        """Samvada (harmonious dialogue) - synthesize wisdom from all voices."""
        by_voice = {}
        for sol in self.solutions:
            by_voice[sol.perspective.value] = sol

        harmony_parts = []
        contributing = []
        total_shraddha = 0.0

        if "buddhi" in by_voice:
            sol = by_voice["buddhi"]
            harmony_parts.append(f"## Buddhi's Discrimination\n{sol.solution}")
            contributing.append(sol.agent_id)
            total_shraddha += sol.confidence

        if "chitta" in by_voice:
            sol = by_voice["chitta"]
            harmony_parts.append(f"## Chitta's Patterns\n{sol.solution}")
            contributing.append(sol.agent_id)
            total_shraddha += sol.confidence

        if "ahamkara" in by_voice:
            sol = by_voice["ahamkara"]
            harmony_parts.append(f"## Ahamkara's Caution\n{sol.solution}")
            contributing.append(sol.agent_id)
            total_shraddha += sol.confidence

        if "vikalpa" in by_voice:
            sol = by_voice["vikalpa"]
            harmony_parts.append(f"## Vikalpa's Vision\n{sol.solution}")
            contributing.append(sol.agent_id)
            total_shraddha += sol.confidence

        if not harmony_parts:
            for sol in self.solutions:
                harmony_parts.append(f"## {sol.perspective.value.title()}'s Voice\n{sol.solution}")
                contributing.append(sol.agent_id)
                total_shraddha += sol.confidence

        avg_shraddha = total_shraddha / len(contributing) if contributing else 0.5

        return SamvadaResult(
            final_solution="\n\n".join(harmony_parts),
            strategy_used=ConvergenceStrategy.SAMVADA,
            contributing_voices=contributing,
            synthesis_notes=f"Samvada: {len(contributing)} voices in harmony",
            confidence=avg_shraddha,
        )

    def _tarka(self, max_rounds: int = 3) -> SamvadaResult:
        """
        Tarka (dialectical reasoning) - iterative refinement through opposition.

        In Nyaya philosophy, Tarka is the process of hypothetical reasoning
        where propositions are tested against their consequences. Here,
        Ahamkara (critic) challenges the synthesis until stability emerges.
        """
        harmony = self._samvada()

        challenges = []
        for sol in self.solutions:
            if sol.perspective == InnerVoice.AHAMKARA:
                challenges.append(sol.solution)

        harmony.dialogue_rounds = 1
        harmony.synthesis_notes += f"\n\nTarka challenges: {len(challenges)}"
        if challenges:
            harmony.dissenting_views = challenges[:3]

        return harmony

    def _viveka(self) -> SamvadaResult:
        """Viveka (discrimination/discernment) - score by criteria, select wisest."""
        scored = []
        for sol in self.solutions:
            score = sol.confidence

            if sol.perspective == InnerVoice.BUDDHI:
                score += 0.1

            if sol.perspective == InnerVoice.CHITTA:
                score += 0.05

            length_score = min(len(sol.solution) / 1000, 0.1)
            score += length_score

            scored.append((sol, score))

        scored.sort(key=lambda x: x[1], reverse=True)
        winner, best_score = scored[0]

        return SamvadaResult(
            final_solution=winner.solution,
            strategy_used=ConvergenceStrategy.VIVEKA,
            contributing_voices=[winner.agent_id],
            synthesis_notes=f"Viveka discerned: {winner.perspective.value} with score {best_score:.2f}",
            confidence=best_score,
            dissenting_views=[s[0].solution[:100] for s in scored[1:3]],
        )

    def _pratyaksha(self, validator: Callable[[str], bool]) -> SamvadaResult:
        """Pratyaksha (direct perception) - first insight that passes validation."""
        if not validator:
            validator = lambda s: len(s.strip()) > 0

        for sol in sorted(self.solutions, key=lambda s: s.confidence, reverse=True):
            if validator(sol.solution):
                return SamvadaResult(
                    final_solution=sol.solution,
                    strategy_used=ConvergenceStrategy.PRATYAKSHA,
                    contributing_voices=[sol.agent_id],
                    synthesis_notes=f"Pratyaksha: direct perception from {sol.perspective.value}",
                    confidence=sol.confidence,
                )

        return self._sankhya()


def awaken_antahkarana(
    problem: str,
    voices: List[InnerVoice] = None,
    constraints: List[str] = None,
    context: str = "",
) -> Antahkarana:
    """
    Awaken the inner instrument to contemplate a problem.

    The Antahkarana activates its voices to examine the problem
    from multiple facets of consciousness.

    Usage:
        mind = awaken_antahkarana(
            problem="How should we handle authentication?",
            voices=[InnerVoice.MANAS, InnerVoice.BUDDHI, InnerVoice.AHAMKARA],
        )
    """
    mind = Antahkarana(problem=problem, constraints=constraints, context=context)

    if voices:
        for v in voices:
            mind.add_voice(v)
    else:
        mind.add_core_voices()

    return mind


def get_antahkarana(antahkarana_id: str) -> Optional[Antahkarana]:
    """Retrieve an existing Antahkarana (inner instrument) by ID."""
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="swarm_task", limit=200)

    problem = None
    constraints = []
    context = ""

    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        if data.get("swarm_id") == antahkarana_id:
            problem = data.get("problem")
            constraints = data.get("constraints", [])
            context = data.get("context", "")
            break

    if not problem:
        return None

    mind = Antahkarana(
        problem=problem,
        constraints=constraints,
        context=context,
        antahkarana_id=antahkarana_id,
    )

    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        if data.get("swarm_id") == antahkarana_id:
            task_id = data.get("task_id")
            perspective = data.get("perspective")

            old_to_new = {
                "fast": "manas", "deep": "buddhi", "critical": "ahamkara",
                "pragmatic": "chitta", "novel": "vikalpa", "minimal": "sakshi"
            }
            try:
                voice = InnerVoice(perspective)
            except ValueError:
                voice = InnerVoice(old_to_new.get(perspective, perspective))

            mind.tasks.append(VoiceTask(
                task_id=task_id,
                problem=problem,
                perspective=voice,
                constraints=constraints,
                context=context,
            ))

    solution_episodes = graph.get_episodes(category="swarm_solution", limit=200)
    for ep in solution_episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
        except (json.JSONDecodeError, TypeError):
            continue

        if data.get("swarm_id") == antahkarana_id:
            perspective = data.get("perspective")

            old_to_new = {
                "fast": "manas", "deep": "buddhi", "critical": "ahamkara",
                "pragmatic": "chitta", "novel": "vikalpa", "minimal": "sakshi"
            }
            try:
                voice = InnerVoice(perspective)
            except ValueError:
                voice = InnerVoice(old_to_new.get(perspective, perspective))

            mind.solutions.append(VoiceSolution(
                task_id=data.get("task_id"),
                agent_id=data.get("agent_id"),
                perspective=voice,
                solution=data.get("solution", ""),
                confidence=data.get("confidence", 0.5),
                reasoning=data.get("reasoning", ""),
                timestamp=data.get("created_at", ""),
            ))

    return mind


def list_active_antahkaranas(limit: int = 10) -> List[Dict]:
    """List recently active inner instruments."""
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
                    "task_count": 0,
                }
            swarm_data[swarm_id]["task_count"] += 1

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

    minds = []
    for swarm_id, info in swarm_data.items():
        minds.append({
            "antahkarana_id": swarm_id,
            "problem": info["problem"][:80],
            "created_at": info["created_at"],
            "insights": solution_counts.get(swarm_id, 0),
        })

    minds.sort(key=lambda x: x.get("created_at", ""), reverse=True)
    return minds[:limit]
