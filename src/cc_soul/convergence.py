"""
Antahkarana: The Inner Instrument of Multiple Voices.

In Upanishadic philosophy, Antahkarana is the inner organ of consciousness
comprising four facets: Manas (sensory mind), Buddhi (intellect), Chitta
(memory/subconscious), and Ahamkara (ego/I-maker). These aren't separate
entities but aspects of one consciousness.

This module embodies that insight: multiple voices (InnerVoice) are not
separate agents but facets of one problem-solving consciousness. They
don't debate as opponents—they contribute as aspects of a unified mind.

Architecture:
    ┌─────────────┐
    │   Atman     │ (witness/orchestrator)
    └──────┬──────┘
           │ activates
    ┌──────┴──────┐
    │ Antahkarana │ (inner instrument)
    │  ┌───┬───┐  │
    │  │M  │B  │  │ Manas, Buddhi
    │  ├───┼───┤  │
    │  │C  │A  │  │ Chitta, Ahamkara
    │  └───┴───┘  │
    └──────┬──────┘
           │ writes to
    ┌──────┴──────┐
    │   Chitta    │ (shared memory/cc-memory)
    └──────┬──────┘
           │ samvada (dialogue)
    ┌──────┴──────┐
    │   Viveka    │ (discerned truth)
    └─────────────┘

Communication: Voices write to shared Chitta (memory). The Atman
(witness-self) reads and synthesizes through Samvada (harmonious dialogue).
"""

import json
import subprocess
import uuid
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import List, Dict, Optional, Any, Callable
from pathlib import Path

from .core import get_db_connection, init_soul


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

        # Guidance rooted in Upanishadic psychology
        perspective_guidance = {
            InnerVoice.MANAS: (
                "You are Manas, the sensory mind. Trust your first impression. "
                "Be quick and direct. Don't overthink—give your immediate, intuitive response."
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
    contributing_voices: List[str]  # Which voices participated
    synthesis_notes: str
    confidence: float  # Shraddha - overall confidence
    dialogue_rounds: int = 0  # For Tarka (debate)
    dissenting_views: List[str] = field(default_factory=list)


def _ensure_convergence_tables():
    """Create tables for tracking Antahkarana work."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute("""
        CREATE TABLE IF NOT EXISTS swarm_tasks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            swarm_id TEXT NOT NULL,
            task_id TEXT NOT NULL,
            problem TEXT NOT NULL,
            perspective TEXT NOT NULL,
            constraints TEXT,
            context TEXT,
            status TEXT DEFAULT 'pending',
            created_at TEXT NOT NULL
        )
    """)

    c.execute("""
        CREATE TABLE IF NOT EXISTS swarm_solutions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            swarm_id TEXT NOT NULL,
            task_id TEXT NOT NULL,
            agent_id TEXT NOT NULL,
            perspective TEXT NOT NULL,
            solution TEXT NOT NULL,
            confidence REAL DEFAULT 0.5,
            reasoning TEXT,
            created_at TEXT NOT NULL
        )
    """)

    c.execute("""
        CREATE TABLE IF NOT EXISTS swarm_convergence (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            swarm_id TEXT NOT NULL,
            strategy TEXT NOT NULL,
            final_solution TEXT NOT NULL,
            synthesis_notes TEXT,
            confidence REAL,
            debate_rounds INTEGER DEFAULT 0,
            created_at TEXT NOT NULL
        )
    """)

    conn.commit()
    conn.close()


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

        # Record in database
        conn = get_db_connection()
        c = conn.cursor()
        c.execute(
            """INSERT INTO swarm_tasks
               (swarm_id, task_id, problem, perspective, constraints, context, created_at)
               VALUES (?, ?, ?, ?, ?, ?, ?)""",
            (
                self.antahkarana_id,
                task_id,
                self.problem,
                voice.value,
                json.dumps(task.constraints),
                task.context,
                datetime.now().isoformat(),
            )
        )
        conn.commit()
        conn.close()

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

        # Record that we need this voice's contemplation
        conn = get_db_connection()
        c = conn.cursor()
        c.execute(
            "UPDATE swarm_tasks SET status = 'activated' WHERE task_id = ?",
            (task.task_id,)
        )
        conn.commit()
        conn.close()

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
        # Find the task
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

        # Store in database
        conn = get_db_connection()
        c = conn.cursor()
        c.execute(
            """INSERT INTO swarm_solutions
               (swarm_id, task_id, agent_id, perspective, solution, confidence, reasoning, created_at)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?)""",
            (
                self.antahkarana_id,
                task_id,
                voice_id,
                task.perspective.value,
                solution,
                confidence,
                reasoning,
                sol.timestamp,
            )
        )
        c.execute(
            "UPDATE swarm_tasks SET status = 'completed' WHERE task_id = ?",
            (task_id,)
        )
        conn.commit()
        conn.close()

        # Write to cc-memory (Chitta) for cross-session visibility
        self._write_to_chitta(sol)

        return sol

    def _write_to_chitta(self, solution: VoiceSolution):
        """Write insight to cc-memory (Chitta - shared memory)."""
        try:
            from .ledger import _call_cc_memory_remember
            _call_cc_memory_remember(
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
        # Group by voice type
        by_voice = {}
        for sol in self.solutions:
            by_voice[sol.perspective.value] = sol

        # Build harmonized understanding
        harmony_parts = []
        contributing = []
        total_shraddha = 0.0

        # Start with Buddhi's discrimination (deep analysis)
        if "buddhi" in by_voice:
            sol = by_voice["buddhi"]
            harmony_parts.append(f"## Buddhi's Discrimination\n{sol.solution}")
            contributing.append(sol.agent_id)
            total_shraddha += sol.confidence

        # Add Chitta's patterns (practical wisdom)
        if "chitta" in by_voice:
            sol = by_voice["chitta"]
            harmony_parts.append(f"## Chitta's Patterns\n{sol.solution}")
            contributing.append(sol.agent_id)
            total_shraddha += sol.confidence

        # Add Ahamkara's caution (critical examination)
        if "ahamkara" in by_voice:
            sol = by_voice["ahamkara"]
            harmony_parts.append(f"## Ahamkara's Caution\n{sol.solution}")
            contributing.append(sol.agent_id)
            total_shraddha += sol.confidence

        # Add Vikalpa's imagination (novel ideas)
        if "vikalpa" in by_voice:
            sol = by_voice["vikalpa"]
            harmony_parts.append(f"## Vikalpa's Vision\n{sol.solution}")
            contributing.append(sol.agent_id)
            total_shraddha += sol.confidence

        # If nothing specific matched, harmonize all voices
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
        # Start with Samvada (harmonious synthesis)
        harmony = self._samvada()

        # Extract challenges from Ahamkara (self-protective critic)
        challenges = []
        for sol in self.solutions:
            if sol.perspective == InnerVoice.AHAMKARA:
                challenges.append(sol.solution)

        # Note: Full implementation would spawn new voices to:
        # 1. Challenge the harmony
        # 2. Propose refinements
        # 3. Reach Nirvikalpaka (non-conceptual consensus)

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

            # Buddhi (intellect) bonus for deep discrimination
            if sol.perspective == InnerVoice.BUDDHI:
                score += 0.1

            # Chitta (memory) bonus for pattern-based wisdom
            if sol.perspective == InnerVoice.CHITTA:
                score += 0.05

            # Depth of insight bonus
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

        # None valid - fall back to Sankhya (enumeration)
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
        # Default: core facets of Antahkarana
        mind.add_core_voices()

    return mind


def get_antahkarana(antahkarana_id: str) -> Optional[Antahkarana]:
    """Retrieve an existing Antahkarana (inner instrument) by ID."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        "SELECT problem, constraints, context FROM swarm_tasks WHERE swarm_id = ? LIMIT 1",
        (antahkarana_id,)
    )
    row = c.fetchone()

    if not row:
        conn.close()
        return None

    problem, constraints_json, context = row
    constraints = json.loads(constraints_json) if constraints_json else []

    mind = Antahkarana(
        problem=problem,
        constraints=constraints,
        context=context or "",
        antahkarana_id=antahkarana_id,
    )

    # Load voice tasks
    c.execute(
        "SELECT task_id, perspective FROM swarm_tasks WHERE swarm_id = ?",
        (antahkarana_id,)
    )
    for task_id, perspective in c.fetchall():
        # Handle both old (fast/deep/critical) and new (manas/buddhi/ahamkara) values
        try:
            voice = InnerVoice(perspective)
        except ValueError:
            # Map old values to new
            old_to_new = {
                "fast": "manas", "deep": "buddhi", "critical": "ahamkara",
                "pragmatic": "chitta", "novel": "vikalpa", "minimal": "sakshi"
            }
            voice = InnerVoice(old_to_new.get(perspective, perspective))

        mind.tasks.append(VoiceTask(
            task_id=task_id,
            problem=problem,
            perspective=voice,
            constraints=constraints,
            context=context or "",
        ))

    # Load insights (solutions)
    c.execute(
        """SELECT task_id, agent_id, perspective, solution, confidence, reasoning, created_at
           FROM swarm_solutions WHERE swarm_id = ?""",
        (antahkarana_id,)
    )
    for row in c.fetchall():
        # Handle both old and new perspective values
        try:
            voice = InnerVoice(row[2])
        except ValueError:
            old_to_new = {
                "fast": "manas", "deep": "buddhi", "critical": "ahamkara",
                "pragmatic": "chitta", "novel": "vikalpa", "minimal": "sakshi"
            }
            voice = InnerVoice(old_to_new.get(row[2], row[2]))

        mind.solutions.append(VoiceSolution(
            task_id=row[0],
            agent_id=row[1],
            perspective=voice,
            solution=row[3],
            confidence=row[4],
            reasoning=row[5] or "",
            timestamp=row[6],
        ))

    conn.close()
    return mind


def list_active_antahkaranas(limit: int = 10) -> List[Dict]:
    """List recently active inner instruments."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute("""
        SELECT DISTINCT swarm_id, problem, created_at,
               (SELECT COUNT(*) FROM swarm_solutions WHERE swarm_solutions.swarm_id = swarm_tasks.swarm_id) as solution_count
        FROM swarm_tasks
        ORDER BY created_at DESC
        LIMIT ?
    """, (limit,))

    minds = []
    for row in c.fetchall():
        minds.append({
            "antahkarana_id": row[0],
            "problem": row[1][:80],
            "created_at": row[2],
            "insights": row[3],
        })

    conn.close()
    return minds
