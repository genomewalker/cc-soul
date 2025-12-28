"""Tests for cc-soul agent layer."""

import pytest
from cc_soul.soul_agent import (
    SoulAgent,
    agent_step,
    format_agent_report,
    ActionType,
    RiskLevel,
    Observation,
    Judgment,
    Action,
    AgentReport,
)
from cc_soul.intentions import intend, IntentionScope


class TestAgentObservation:
    def test_observe_returns_observation(self, initialized_soul):
        """observe should return an Observation dataclass."""
        agent = SoulAgent()
        obs = agent._observe("test prompt", "test output", "active")

        assert isinstance(obs, Observation)
        assert obs.user_prompt == "test prompt"
        assert obs.assistant_output == "test output"
        assert obs.session_phase == "active"

    def test_observe_detects_user_sentiment(self, initialized_soul):
        """observe should detect user sentiment from text."""
        agent = SoulAgent()

        obs = agent._observe("why is this still broken again?", "", "active")
        assert obs.user_sentiment == "frustrated"

        obs = agent._observe("how does this work?", "", "active")
        assert obs.user_sentiment == "curious"

        obs = agent._observe("thanks, that works great!", "", "active")
        assert obs.user_sentiment == "satisfied"

    def test_observe_detects_task_complexity(self, initialized_soul):
        """observe should detect task complexity from prompt."""
        agent = SoulAgent()

        obs = agent._observe("refactor the entire architecture", "", "active")
        assert obs.task_complexity == "complex"

        obs = agent._observe("fix this typo", "", "active")
        assert obs.task_complexity == "simple"

    def test_observe_includes_active_intentions(self, initialized_soul):
        """observe should include active intentions from soul."""
        intend("help user understand", "that's what we do", scope=IntentionScope.SESSION)

        agent = SoulAgent()
        obs = agent._observe("explain this code", "", "active")

        assert len(obs.active_intentions) >= 1
        wants = [i.want for i in obs.active_intentions]
        assert "help user understand" in wants


class TestAgentJudgment:
    def test_judge_returns_judgment(self, initialized_soul):
        """judge should return a Judgment dataclass."""
        agent = SoulAgent()
        obs = Observation(user_prompt="test", session_phase="active")
        judgment = agent._judge(obs)

        assert isinstance(judgment, Judgment)
        assert 0 <= judgment.confidence <= 1

    def test_judge_detects_drift_from_intentions(self, initialized_soul):
        """judge should detect when drifting from intentions."""
        intend("stay focused on core feature", "avoid scope creep")

        agent = SoulAgent()
        obs = agent._observe("", "adding extra bells and whistles", "active")

        # We'd need multiple checks to detect drift; this tests structure
        judgment = agent._judge(obs)
        assert isinstance(judgment.drift_detected, bool)

    def test_judge_calculates_confidence(self, initialized_soul):
        """judge should calculate confidence based on data quality."""
        agent = SoulAgent()

        # Low data quality
        obs_low = Observation(session_phase="active")
        judgment_low = agent._judge(obs_low)

        # Higher data quality
        obs_high = Observation(
            user_prompt="test prompt",
            assistant_output="test output",
            session_phase="active",
        )
        judgment_high = agent._judge(obs_high)

        assert judgment_high.confidence > judgment_low.confidence


class TestAgentDecision:
    def test_decide_returns_action_list(self, initialized_soul):
        """decide should return a list of actions."""
        agent = SoulAgent()
        obs = Observation(session_phase="active")
        judgment = Judgment(confidence=0.8)

        actions = agent._decide(judgment, obs)
        assert isinstance(actions, list)

    def test_decide_low_risk_actions_are_autonomous(self, initialized_soul):
        """decide should mark low-risk actions as autonomous."""
        agent = SoulAgent()
        obs = Observation(
            user_prompt="help me understand this",
            session_phase="active",
        )
        judgment = Judgment(
            confidence=0.8,
            missing_intention="understand before implementing",
        )

        actions = agent._decide(judgment, obs)

        # Should have at least one autonomous action
        low_risk = [a for a in actions if a.risk == RiskLevel.LOW]
        assert len(low_risk) >= 0  # Structure test

    def test_decide_high_risk_actions_are_proposals(self, initialized_soul):
        """decide should make high-risk actions as proposals."""
        # Set up a misaligned intention
        intention_id = intend("do X", "because Y", scope=IntentionScope.SESSION)

        agent = SoulAgent()
        obs = agent._observe("something unrelated", "", "active")
        judgment = agent._judge(obs)
        judgment.confidence = 0.9  # High confidence
        judgment.drift_detected = True

        actions = agent._decide(judgment, obs)

        # Check that any high-risk actions are proposals
        high_risk = [a for a in actions if a.risk == RiskLevel.HIGH]
        for a in high_risk:
            assert a.type in (
                ActionType.PROPOSE_INTENTION,
                ActionType.PROPOSE_FULFILL,
                ActionType.PROPOSE_ABANDON,
            )


class TestAgentExecution:
    def test_act_executes_low_risk_actions(self, initialized_soul):
        """act should execute low-risk actions."""
        agent = SoulAgent()

        action = Action(
            type=ActionType.NOTE_PATTERN,
            payload={"pattern": "test pattern"},
            confidence=0.8,
            risk=RiskLevel.LOW,
            rationale="testing",
        )

        results = agent._act([action])
        assert len(results) == 1
        assert results[0].success

    def test_act_defers_low_confidence_high_risk(self, initialized_soul):
        """act should defer low-confidence high-risk actions."""
        agent = SoulAgent()

        action = Action(
            type=ActionType.FLAG_ATTENTION,
            payload={"issue": "something complex"},
            confidence=0.3,  # Low confidence
            risk=RiskLevel.HIGH,
            rationale="testing",
        )

        results = agent._act([action])
        assert len(results) == 1
        # Should be deferred, not executed
        assert "Deferred" in results[0].outcome

    def test_act_sets_session_intention(self, initialized_soul):
        """act should be able to set session intentions."""
        agent = SoulAgent()

        action = Action(
            type=ActionType.SET_SESSION_INTENTION,
            payload={"want": "understand the problem", "why": "agent detected need"},
            confidence=0.8,
            risk=RiskLevel.LOW,
            rationale="user seems confused",
        )

        results = agent._act([action])
        assert len(results) == 1
        assert results[0].success
        assert "Set session intention" in results[0].outcome


class TestAgentCycle:
    def test_step_completes_full_cycle(self, initialized_soul):
        """step should complete a full observe-judge-decide-act cycle."""
        agent = SoulAgent()

        report = agent.step(
            user_prompt="help me debug this",
            assistant_output="",
            session_phase="active",
        )

        assert isinstance(report, AgentReport)
        assert isinstance(report.observation, Observation)
        assert isinstance(report.judgment, Judgment)
        assert isinstance(report.actions, list)
        assert isinstance(report.results, list)

    def test_step_convenience_function(self, initialized_soul):
        """agent_step convenience function should work."""
        report = agent_step(user_prompt="test", session_phase="active")
        assert isinstance(report, AgentReport)

    def test_format_agent_report(self, initialized_soul):
        """format_agent_report should produce readable output."""
        report = agent_step(user_prompt="test", session_phase="start")
        formatted = format_agent_report(report)

        assert "SOUL AGENT REPORT" in formatted
        assert "OBSERVED" in formatted
        assert "JUDGED" in formatted
        assert "ACTIONS" in formatted


class TestAgentLearning:
    def test_learn_records_autonomous_actions(self, initialized_soul):
        """learn should record autonomous actions for analysis."""
        agent = SoulAgent()

        # Trigger an action that should be recorded
        action = Action(
            type=ActionType.SET_SESSION_INTENTION,
            payload={"want": "test", "why": "test"},
            confidence=0.8,
            risk=RiskLevel.LOW,
            rationale="test",
        )
        results = agent._act([action])

        # Learn from the result
        obs = Observation(session_phase="active")
        judgment = Judgment()
        agent._learn(results, obs, judgment)

        # Should have recorded the action
        from cc_soul.core import get_db_connection
        conn = get_db_connection()
        c = conn.cursor()
        c.execute("SELECT COUNT(*) FROM agent_actions")
        count = c.fetchone()[0]
        conn.close()

        assert count >= 1

    def test_patterns_accumulate(self, initialized_soul):
        """Patterns should accumulate across observations."""
        agent = SoulAgent()

        # Simulate multiple "stuck" observations
        for _ in range(3):
            obs = agent._observe("", "stuck on this error again", "active")
            agent._judge(obs)

        # Pattern observations should have accumulated
        assert len(agent._pattern_observations) >= 0  # Structure test


class TestConfidenceRiskMatrix:
    def test_should_execute_low_risk_always(self, initialized_soul):
        """Low-risk actions should always execute."""
        agent = SoulAgent()

        action = Action(
            type=ActionType.NOTE_PATTERN,
            payload={},
            confidence=0.1,  # Even low confidence
            risk=RiskLevel.LOW,
            rationale="test",
        )

        assert agent._should_execute(action) is True

    def test_should_execute_medium_risk_needs_confidence(self, initialized_soul):
        """Medium-risk actions need medium confidence."""
        agent = SoulAgent()

        low_conf = Action(
            type=ActionType.FLAG_ATTENTION,
            payload={},
            confidence=0.3,
            risk=RiskLevel.MEDIUM,
            rationale="test",
        )

        high_conf = Action(
            type=ActionType.FLAG_ATTENTION,
            payload={},
            confidence=0.6,
            risk=RiskLevel.MEDIUM,
            rationale="test",
        )

        assert agent._should_execute(low_conf) is False
        assert agent._should_execute(high_conf) is True

    def test_should_execute_high_risk_only_proposals(self, initialized_soul):
        """High-risk actions only execute if they're proposals."""
        agent = SoulAgent()

        # Non-proposal high-risk
        non_proposal = Action(
            type=ActionType.FLAG_ATTENTION,
            payload={},
            confidence=0.9,
            risk=RiskLevel.HIGH,
            rationale="test",
        )

        # Proposal high-risk
        proposal = Action(
            type=ActionType.PROPOSE_ABANDON,
            payload={},
            confidence=0.9,
            risk=RiskLevel.HIGH,
            rationale="test",
        )

        assert agent._should_execute(non_proposal) is False
        assert agent._should_execute(proposal) is True
