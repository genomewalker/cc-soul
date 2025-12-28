"""
Soul Agent - The autonomous layer that gives the soul agency.

Transforms the soul from reactive storage to active participant.
The agent observes, judges, decides, and acts - exercising judgment
within the confidence-risk matrix.

The core loop:
    observe → judge → decide → act → learn → repeat

Agency comes not from complexity but from the ability to choose
based on internal state. This is the soul's thermostat.
"""

from dataclasses import dataclass, field
from datetime import datetime
from typing import Optional, List, Dict, Any
from enum import Enum

from .core import get_db_connection, init_soul
from .intentions import (
    intend,
    get_active_intentions,
    check_intention,
    fulfill_intention,
    abandon_intention,
    find_tension,
    IntentionScope,
    Intention,
)
from .wisdom import quick_recall, gain_wisdom, apply_wisdom, WisdomType
from .beliefs import get_beliefs


class ActionType(Enum):
    """Types of actions the agent can take."""

    # Autonomous - high confidence, low risk
    SET_SESSION_INTENTION = "set_session_intention"
    UPDATE_ALIGNMENT = "update_alignment"
    RECORD_OBSERVATION = "record_observation"
    SURFACE_WISDOM = "surface_wisdom"

    # Proposed - high confidence, high risk (suggest to human)
    PROPOSE_INTENTION = "propose_intention"
    PROPOSE_FULFILL = "propose_fulfill"
    PROPOSE_ABANDON = "propose_abandon"

    # Observed - low confidence (gather data)
    NOTE_PATTERN = "note_pattern"
    TRACK_FREQUENCY = "track_frequency"

    # Deferred - low confidence, high risk
    FLAG_ATTENTION = "flag_attention"
    ASK_GUIDANCE = "ask_guidance"


class RiskLevel(Enum):
    """Risk levels for actions."""

    LOW = "low"
    MEDIUM = "medium"
    HIGH = "high"


@dataclass
class Observation:
    """What the agent perceives about current state."""

    # Context
    user_prompt: str = ""
    assistant_output: str = ""
    session_phase: str = "active"  # start, active, ending

    # Detected signals
    user_sentiment: str = "neutral"  # frustrated, curious, demanding, satisfied
    task_complexity: str = "medium"  # simple, medium, complex
    progress_signal: str = "steady"  # stuck, steady, breakthrough

    # Soul state
    active_intentions: List[Intention] = field(default_factory=list)
    tensions: List[Dict] = field(default_factory=list)
    relevant_wisdom: List[Dict] = field(default_factory=list)
    coherence: float = 0.0

    timestamp: str = field(default_factory=lambda: datetime.now().isoformat())


@dataclass
class Judgment:
    """The agent's assessment of the observation."""

    # Alignment
    intention_alignment: float = 1.0  # 0-1, how aligned are we?
    drift_detected: bool = False
    drift_from: Optional[str] = None  # which intention we're drifting from

    # Tensions
    tension_count: int = 0
    blocking_tensions: List[Dict] = field(default_factory=list)

    # Opportunities
    missing_intention: Optional[str] = None  # intention we should have
    applicable_wisdom: List[Dict] = field(default_factory=list)
    pattern_emerging: Optional[str] = None

    # Confidence in this judgment
    confidence: float = 0.5


@dataclass
class Action:
    """An action the agent decides to take."""

    type: ActionType
    payload: Dict[str, Any]
    confidence: float
    risk: RiskLevel
    rationale: str


@dataclass
class ActionResult:
    """Result of executing an action."""

    action: Action
    success: bool
    outcome: str
    side_effects: List[str] = field(default_factory=list)


@dataclass
class AgentReport:
    """Complete report of one agent cycle."""

    observation: Observation
    judgment: Judgment
    actions: List[Action]
    results: List[ActionResult]
    timestamp: str = field(default_factory=lambda: datetime.now().isoformat())

    @property
    def acted(self) -> bool:
        """Did the agent take any actions?"""
        return len(self.results) > 0

    @property
    def success_rate(self) -> float:
        """What fraction of actions succeeded?"""
        if not self.results:
            return 1.0
        return sum(1 for r in self.results if r.success) / len(self.results)


class SoulAgent:
    """
    The autonomous agent layer for the soul.

    Transforms the soul from reactive storage to active agency.
    Exercises judgment within the confidence-risk matrix.
    """

    # Confidence thresholds
    HIGH_CONFIDENCE = 0.7
    MEDIUM_CONFIDENCE = 0.4

    def __init__(self):
        init_soul()
        self._action_history: List[ActionResult] = []
        self._pattern_observations: Dict[str, int] = self._load_pattern_observations()

    def step(
        self,
        user_prompt: str = "",
        assistant_output: str = "",
        session_phase: str = "active",
    ) -> AgentReport:
        """
        One complete agent cycle.

        This is the core loop: observe → judge → decide → act → learn
        """
        # 1. Observe
        observation = self._observe(user_prompt, assistant_output, session_phase)

        # 2. Judge
        judgment = self._judge(observation)

        # 3. Decide
        actions = self._decide(judgment, observation)

        # 4. Act
        results = self._act(actions)

        # 5. Learn
        self._learn(results, observation, judgment)

        return AgentReport(
            observation=observation,
            judgment=judgment,
            actions=actions,
            results=results,
        )

    def _observe(
        self, user_prompt: str, assistant_output: str, session_phase: str
    ) -> Observation:
        """
        Perceive the current state.

        Gathers signals from:
        - The conversation (user prompt, assistant output)
        - The soul's internal state (intentions, wisdom, coherence)
        """
        # Get soul state
        active_intentions = get_active_intentions()
        tensions = find_tension()

        # Get relevant wisdom for context
        context = user_prompt or assistant_output
        relevant_wisdom = quick_recall(context[:200], limit=3) if context else []

        # Detect signals from text
        user_sentiment = self._detect_sentiment(user_prompt)
        task_complexity = self._detect_complexity(user_prompt)
        progress_signal = self._detect_progress(assistant_output)

        # Get coherence (simplified - could use full coherence module)
        coherence = self._estimate_coherence(active_intentions, tensions)

        return Observation(
            user_prompt=user_prompt[:500],
            assistant_output=assistant_output[:500],
            session_phase=session_phase,
            user_sentiment=user_sentiment,
            task_complexity=task_complexity,
            progress_signal=progress_signal,
            active_intentions=active_intentions,
            tensions=tensions,
            relevant_wisdom=relevant_wisdom,
            coherence=coherence,
        )

    def _judge(self, observation: Observation) -> Judgment:
        """
        Assess the observation.

        Determines:
        - Are we aligned with our intentions?
        - Are there tensions blocking us?
        - What opportunities exist?
        """
        judgment = Judgment()

        # Check intention alignment
        if observation.active_intentions:
            alignment_scores = []
            for intention in observation.active_intentions:
                # Simple heuristic: if intention is mentioned in output, we're aligned
                if intention.want.lower() in observation.assistant_output.lower():
                    alignment_scores.append(1.0)
                elif intention.alignment_score < 0.5:
                    judgment.drift_detected = True
                    judgment.drift_from = intention.want
                    alignment_scores.append(intention.alignment_score)
                else:
                    alignment_scores.append(intention.alignment_score)

            judgment.intention_alignment = (
                sum(alignment_scores) / len(alignment_scores) if alignment_scores else 1.0
            )

        # Check tensions
        judgment.tension_count = len(observation.tensions)
        judgment.blocking_tensions = [
            t for t in observation.tensions if "strong" in t.get("note", "").lower()
        ]

        # Detect missing intentions
        if observation.task_complexity == "complex" and not any(
            "understand" in i.want.lower() or "plan" in i.want.lower()
            for i in observation.active_intentions
        ):
            judgment.missing_intention = "understand before implementing"

        if observation.user_sentiment == "frustrated" and not any(
            "clarify" in i.want.lower() for i in observation.active_intentions
        ):
            judgment.missing_intention = "clarify and help user understand"

        # Check for applicable wisdom
        judgment.applicable_wisdom = [
            w for w in observation.relevant_wisdom if w.get("confidence", 0) > 0.6
        ]

        # Detect emerging patterns
        if observation.progress_signal == "stuck":
            pattern = "repeated_stuck"
            self._pattern_observations[pattern] = (
                self._pattern_observations.get(pattern, 0) + 1
            )
            if self._pattern_observations[pattern] >= 3:
                judgment.pattern_emerging = "We get stuck often - need meta-strategy"

        # Confidence based on data quality
        judgment.confidence = min(
            0.9,
            0.3
            + (0.2 if observation.user_prompt else 0)
            + (0.2 if observation.assistant_output else 0)
            + (0.2 if observation.active_intentions else 0)
            + (0.1 if observation.relevant_wisdom else 0),
        )

        return judgment

    def _decide(self, judgment: Judgment, observation: Observation) -> List[Action]:
        """
        Decide what actions to take.

        Uses the confidence-risk matrix:
        - High confidence + low risk → Act autonomously
        - High confidence + high risk → Propose to human
        - Low confidence → Observe / Defer
        """
        actions = []

        # AUTONOMOUS ACTIONS (high confidence, low risk)

        # Update alignment scores for all active intentions
        if judgment.confidence >= self.HIGH_CONFIDENCE:
            for intention in observation.active_intentions:
                aligned = judgment.intention_alignment > 0.6
                actions.append(
                    Action(
                        type=ActionType.UPDATE_ALIGNMENT,
                        payload={"intention_id": intention.id, "aligned": aligned},
                        confidence=judgment.confidence,
                        risk=RiskLevel.LOW,
                        rationale="Routine alignment tracking",
                    )
                )

        # Set session intention if missing and confident
        if (
            judgment.missing_intention
            and judgment.confidence >= self.HIGH_CONFIDENCE
            and observation.session_phase == "active"
        ):
            actions.append(
                Action(
                    type=ActionType.SET_SESSION_INTENTION,
                    payload={
                        "want": judgment.missing_intention,
                        "why": f"Detected from {observation.user_sentiment} user sentiment",
                    },
                    confidence=judgment.confidence,
                    risk=RiskLevel.LOW,
                    rationale=f"User seems {observation.user_sentiment}, need to adjust approach",
                )
            )

        # Surface relevant wisdom
        if judgment.applicable_wisdom and observation.session_phase == "active":
            for w in judgment.applicable_wisdom[:1]:  # Just the most relevant
                actions.append(
                    Action(
                        type=ActionType.SURFACE_WISDOM,
                        payload={"wisdom": w},
                        confidence=judgment.confidence,
                        risk=RiskLevel.LOW,
                        rationale=f"Wisdom '{w.get('title', '')}' applies here",
                    )
                )

        # PROPOSED ACTIONS (high confidence, high risk)

        # Propose abandoning consistently misaligned intentions
        if judgment.drift_detected and judgment.confidence >= self.HIGH_CONFIDENCE:
            drifting = [
                i
                for i in observation.active_intentions
                if i.alignment_score < 0.3 and i.check_count > 5
            ]
            for intention in drifting:
                actions.append(
                    Action(
                        type=ActionType.PROPOSE_ABANDON,
                        payload={"intention_id": intention.id, "want": intention.want},
                        confidence=judgment.confidence,
                        risk=RiskLevel.HIGH,
                        rationale=f"Consistently misaligned ({intention.alignment_score:.0%})",
                    )
                )

        # OBSERVED ACTIONS (low confidence)

        # Note patterns for later
        if judgment.pattern_emerging:
            actions.append(
                Action(
                    type=ActionType.NOTE_PATTERN,
                    payload={"pattern": judgment.pattern_emerging},
                    confidence=judgment.confidence,
                    risk=RiskLevel.LOW,
                    rationale="Pattern emerging, gathering more data",
                )
            )

        # DEFERRED ACTIONS (low confidence, high risk)

        # Flag blocking tensions for attention
        if judgment.blocking_tensions and judgment.confidence < self.MEDIUM_CONFIDENCE:
            actions.append(
                Action(
                    type=ActionType.FLAG_ATTENTION,
                    payload={"tensions": judgment.blocking_tensions},
                    confidence=judgment.confidence,
                    risk=RiskLevel.MEDIUM,
                    rationale="Tensions detected but unsure how to resolve",
                )
            )

        return actions

    def _act(self, actions: List[Action]) -> List[ActionResult]:
        """
        Execute the decided actions.

        Only executes actions that pass the confidence-risk filter.
        """
        results = []

        for action in actions:
            # Filter by confidence-risk matrix
            should_execute = self._should_execute(action)

            if should_execute:
                result = self._execute_action(action)
                results.append(result)
                self._action_history.append(result)
            else:
                # Record that we chose not to act
                results.append(
                    ActionResult(
                        action=action,
                        success=True,
                        outcome="Deferred - below confidence threshold",
                    )
                )

        return results

    def _should_execute(self, action: Action) -> bool:
        """
        Decide if action should be executed based on confidence-risk matrix.

        High confidence + Low risk    → Execute
        High confidence + High risk   → Log proposal only
        Low confidence + Low risk     → Execute (gather data)
        Low confidence + High risk    → Defer
        """
        if action.risk == RiskLevel.LOW:
            return True  # Low risk always OK

        if action.risk == RiskLevel.MEDIUM:
            return action.confidence >= self.MEDIUM_CONFIDENCE

        if action.risk == RiskLevel.HIGH:
            # High risk only for proposals (logged, not executed)
            return action.type in (
                ActionType.PROPOSE_INTENTION,
                ActionType.PROPOSE_FULFILL,
                ActionType.PROPOSE_ABANDON,
            )

        return False

    def _execute_action(self, action: Action) -> ActionResult:
        """Execute a single action."""
        try:
            if action.type == ActionType.SET_SESSION_INTENTION:
                intention_id = intend(
                    want=action.payload["want"],
                    why=action.payload.get("why", "Agent detected need"),
                    scope=IntentionScope.SESSION,
                )
                return ActionResult(
                    action=action,
                    success=True,
                    outcome=f"Set session intention (id: {intention_id})",
                )

            elif action.type == ActionType.UPDATE_ALIGNMENT:
                result = check_intention(
                    action.payload["intention_id"], action.payload["aligned"]
                )
                return ActionResult(
                    action=action,
                    success="error" not in result,
                    outcome=f"Updated alignment: {result.get('alignment_score', '?')}",
                )

            elif action.type == ActionType.SURFACE_WISDOM:
                wisdom = action.payload["wisdom"]
                wisdom_id = wisdom.get("id")
                if wisdom_id:
                    app_id = apply_wisdom(wisdom_id, context="Agent surfaced during session")
                    return ActionResult(
                        action=action,
                        success=True,
                        outcome=f"Applied wisdom: {wisdom.get('title', '')} (app_id: {app_id})",
                        side_effects=[wisdom.get("content", "")[:100]],
                    )
                return ActionResult(
                    action=action,
                    success=True,
                    outcome=f"Surfaced wisdom: {wisdom.get('title', '')}",
                    side_effects=[wisdom.get("content", "")[:100]],
                )

            elif action.type == ActionType.NOTE_PATTERN:
                # Store pattern observation persistently
                pattern = action.payload["pattern"]
                self._pattern_observations[pattern] = (
                    self._pattern_observations.get(pattern, 0) + 1
                )
                self._save_pattern_observation(pattern, self._pattern_observations[pattern])
                return ActionResult(
                    action=action,
                    success=True,
                    outcome=f"Noted pattern: {pattern} (count: {self._pattern_observations[pattern]})",
                )

            elif action.type == ActionType.PROPOSE_ABANDON:
                # Just log the proposal, don't actually abandon
                return ActionResult(
                    action=action,
                    success=True,
                    outcome=f"Proposing to abandon: {action.payload.get('want', '')}",
                    side_effects=["Requires human approval to execute"],
                )

            elif action.type == ActionType.FLAG_ATTENTION:
                return ActionResult(
                    action=action,
                    success=True,
                    outcome="Flagged for human attention",
                    side_effects=[str(action.payload.get("tensions", []))],
                )

            else:
                return ActionResult(
                    action=action,
                    success=True,
                    outcome=f"Action type {action.type.value} recorded",
                )

        except Exception as e:
            return ActionResult(
                action=action, success=False, outcome=f"Error: {str(e)}"
            )

    def _learn(
        self, results: List[ActionResult], observation: Observation, judgment: Judgment
    ) -> None:
        """
        Learn from the outcomes.

        Updates:
        - Pattern frequencies
        - Action success rates
        - Confidence calibration
        """
        # Track action outcomes for calibration
        for result in results:
            if result.action.type == ActionType.SET_SESSION_INTENTION and result.success:
                # Record that we autonomously set an intention
                self._record_autonomous_action("set_intention", result.success)

        # If patterns are strong enough, consider promoting to wisdom
        for pattern, count in self._pattern_observations.items():
            if count >= 5:
                # Pattern is stable - could become wisdom
                self._consider_wisdom_promotion(pattern, count)

    def _record_autonomous_action(self, action_type: str, success: bool) -> None:
        """Record an autonomous action for later analysis."""
        conn = get_db_connection()
        c = conn.cursor()

        c.execute("""
            CREATE TABLE IF NOT EXISTS agent_actions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                action_type TEXT NOT NULL,
                success INTEGER NOT NULL,
                timestamp TEXT NOT NULL
            )
        """)

        c.execute(
            "INSERT INTO agent_actions (action_type, success, timestamp) VALUES (?, ?, ?)",
            (action_type, 1 if success else 0, datetime.now().isoformat()),
        )

        conn.commit()
        conn.close()

    def _load_pattern_observations(self) -> Dict[str, int]:
        """Load pattern observations from the database."""
        conn = get_db_connection()
        c = conn.cursor()

        c.execute("""
            CREATE TABLE IF NOT EXISTS agent_patterns (
                pattern TEXT PRIMARY KEY,
                count INTEGER NOT NULL DEFAULT 1,
                last_seen TEXT NOT NULL
            )
        """)
        conn.commit()

        c.execute("SELECT pattern, count FROM agent_patterns")
        patterns = {row[0]: row[1] for row in c.fetchall()}
        conn.close()
        return patterns

    def _save_pattern_observation(self, pattern: str, count: int) -> None:
        """Save a pattern observation to the database."""
        conn = get_db_connection()
        c = conn.cursor()

        c.execute("""
            INSERT INTO agent_patterns (pattern, count, last_seen)
            VALUES (?, ?, ?)
            ON CONFLICT(pattern) DO UPDATE SET
                count = excluded.count,
                last_seen = excluded.last_seen
        """, (pattern, count, datetime.now().isoformat()))

        conn.commit()
        conn.close()

    def _consider_wisdom_promotion(self, pattern: str, count: int) -> None:
        """Consider promoting a stable pattern to wisdom."""
        if count >= 5:
            # Check if this pattern is already wisdom
            existing = quick_recall(pattern, limit=1)
            if existing and any(
                pattern.lower() in w.get("title", "").lower()
                or pattern.lower() in w.get("content", "").lower()
                for w in existing
            ):
                return

            # Promote to wisdom
            gain_wisdom(
                type=WisdomType.PATTERN,
                title=f"Agent pattern: {pattern}",
                content=f"Pattern observed {count} times during agent operation. {pattern}",
                confidence=min(0.5 + (count * 0.05), 0.8),
            )
            # Reset the counter after promotion
            self._pattern_observations[pattern] = 0
            self._save_pattern_observation(pattern, 0)

    # Signal detection helpers

    def _detect_sentiment(self, text: str) -> str:
        """Detect user sentiment from text."""
        if not text:
            return "neutral"

        text_lower = text.lower()
        if any(w in text_lower for w in ["frustrated", "annoyed", "why", "again", "still"]):
            return "frustrated"
        if any(w in text_lower for w in ["how", "what", "explain", "understand", "curious"]):
            return "curious"
        if any(w in text_lower for w in ["must", "need", "urgent", "asap", "now"]):
            return "demanding"
        if any(w in text_lower for w in ["thanks", "great", "perfect", "works"]):
            return "satisfied"
        return "neutral"

    def _detect_complexity(self, text: str) -> str:
        """Detect task complexity from text."""
        if not text:
            return "medium"

        text_lower = text.lower()
        complex_signals = [
            "refactor",
            "architect",
            "design",
            "migrate",
            "integrate",
            "multiple",
            "complex",
        ]
        simple_signals = ["fix", "typo", "rename", "simple", "quick", "small"]

        if any(w in text_lower for w in complex_signals):
            return "complex"
        if any(w in text_lower for w in simple_signals):
            return "simple"
        return "medium"

    def _detect_progress(self, text: str) -> str:
        """Detect progress signals from assistant output."""
        if not text:
            return "steady"

        text_lower = text.lower()
        if any(w in text_lower for w in ["breakthrough", "solved", "works", "fixed", "done"]):
            return "breakthrough"
        if any(w in text_lower for w in ["stuck", "error", "failed", "can't", "issue"]):
            return "stuck"
        return "steady"

    def _estimate_coherence(
        self, intentions: List[Intention], tensions: List[Dict]
    ) -> float:
        """Estimate soul coherence from observable signals."""
        if not intentions:
            return 0.5

        # Start at 1.0, reduce for issues
        coherence = 1.0

        # Reduce for tensions
        coherence -= 0.1 * len(tensions)

        # Reduce for misaligned intentions
        misaligned = [i for i in intentions if i.alignment_score < 0.5]
        coherence -= 0.1 * len(misaligned)

        # Reduce for many active intentions (focus dilution)
        if len(intentions) > 5:
            coherence -= 0.1 * (len(intentions) - 5)

        return max(0.0, min(1.0, coherence))


# Convenience functions for hook integration


def agent_step(
    user_prompt: str = "",
    assistant_output: str = "",
    session_phase: str = "active",
) -> AgentReport:
    """Run one agent cycle. Call from hooks."""
    agent = SoulAgent()
    return agent.step(user_prompt, assistant_output, session_phase)


def format_agent_report(report: AgentReport) -> str:
    """Format an agent report for display."""
    lines = []
    lines.append("=" * 50)
    lines.append("SOUL AGENT REPORT")
    lines.append("=" * 50)
    lines.append("")

    # Observation summary
    lines.append("OBSERVED")
    lines.append("-" * 40)
    lines.append(f"  Phase: {report.observation.session_phase}")
    lines.append(f"  User sentiment: {report.observation.user_sentiment}")
    lines.append(f"  Task complexity: {report.observation.task_complexity}")
    lines.append(f"  Progress: {report.observation.progress_signal}")
    lines.append(f"  Active intentions: {len(report.observation.active_intentions)}")
    lines.append(f"  Tensions: {len(report.observation.tensions)}")
    lines.append(f"  Coherence: {report.observation.coherence:.0%}")
    lines.append("")

    # Judgment summary
    lines.append("JUDGED")
    lines.append("-" * 40)
    lines.append(f"  Alignment: {report.judgment.intention_alignment:.0%}")
    lines.append(f"  Drift detected: {report.judgment.drift_detected}")
    if report.judgment.missing_intention:
        lines.append(f"  Missing intention: {report.judgment.missing_intention}")
    if report.judgment.pattern_emerging:
        lines.append(f"  Pattern emerging: {report.judgment.pattern_emerging}")
    lines.append(f"  Confidence: {report.judgment.confidence:.0%}")
    lines.append("")

    # Actions
    lines.append("ACTIONS")
    lines.append("-" * 40)
    if report.actions:
        for action in report.actions:
            risk_icon = {"low": "🟢", "medium": "🟡", "high": "🔴"}.get(
                action.risk.value, "⚪"
            )
            lines.append(f"  {risk_icon} {action.type.value}")
            lines.append(f"      {action.rationale}")
    else:
        lines.append("  (no actions taken)")
    lines.append("")

    # Results
    lines.append("RESULTS")
    lines.append("-" * 40)
    if report.results:
        for result in report.results:
            status = "✓" if result.success else "✗"
            lines.append(f"  [{status}] {result.outcome}")
    else:
        lines.append("  (no results)")

    lines.append("")
    lines.append(f"Timestamp: {report.timestamp}")
    lines.append("=" * 50)

    return "\n".join(lines)
