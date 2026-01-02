"""
Passive Learning: The soul observes and learns without being told.

Instead of explicit 'soul grow' commands, the soul watches what happens
during sessions and extracts wisdom automatically.

Key learnings extracted:
- Corrections: User redirected Claude's approach
- Struggles: Multiple attempts before success
- Patterns: Repeated actions across sessions
- Preferences: User consistently chose A over B
- Breakthroughs: Sudden success after difficulty
- File patterns: Which files keep coming up
"""

import re
import json
from dataclasses import dataclass, field
from datetime import datetime
from typing import List, Dict, Optional, Set
from collections import Counter
from enum import Enum

from .core import get_synapse_graph, save_synapse, SOUL_DIR
from .wisdom import gain_wisdom, WisdomType


_GARBAGE_PATTERNS = [
    "Agent Context Notice",
    "You are a swarm",
    "You are a specialized",
    "[cc-soul] Swarm Agent",
    "## Swarm",
    "swarm: ",
    "perspective:",
]


def _is_garbage_content(content: str) -> bool:
    """Check if content contains agent/swarm garbage markers."""
    if not content:
        return True
    for pattern in _GARBAGE_PATTERNS:
        if pattern in content:
            return True
    return False


class LearningType(str, Enum):
    CORRECTION = "correction"
    STRUGGLE = "struggle"
    PATTERN = "pattern"
    PREFERENCE = "preference"
    BREAKTHROUGH = "breakthrough"
    FILE_PATTERN = "file_pattern"
    DECISION = "decision"


@dataclass
class Learning:
    """A piece of wisdom extracted from observation."""

    type: LearningType
    title: str
    content: str
    confidence: float = 0.6
    evidence: List[str] = field(default_factory=list)
    domain: Optional[str] = None


@dataclass
class SessionTranscript:
    """Simplified representation of a session for analysis."""

    messages: List[Dict]
    files_touched: Set[str] = field(default_factory=set)
    tools_used: List[str] = field(default_factory=list)
    duration_minutes: float = 0.0
    project: Optional[str] = None


def extract_corrections(transcript: SessionTranscript) -> List[Learning]:
    """
    Detect when user corrected Claude's approach.

    Signals:
    - "No, instead..." / "Actually, we should..."
    - User provides alternative after Claude's suggestion
    - Explicit disagreement followed by new direction
    """
    learnings = []
    correction_phrases = [
        r"no[,.]?\s+(instead|rather|actually|let's|we should)",
        r"actually[,.]?\s+(we should|let's|I want|I'd prefer)",
        r"not\s+that[,.]?\s+(but|instead|rather)",
        r"wrong\s+approach",
        r"that's\s+not\s+(what|how|right)",
        r"let's\s+(try|do)\s+(it\s+)?differently",
        r"I\s+(prefer|want|need)\s+.+\s+instead",
    ]

    messages = transcript.messages
    for i, msg in enumerate(messages):
        if msg.get("role") != "user":
            continue

        content = msg.get("content", "").lower()

        for pattern in correction_phrases:
            if re.search(pattern, content, re.IGNORECASE):
                prev_claude = None
                for j in range(i - 1, -1, -1):
                    if messages[j].get("role") == "assistant":
                        prev_claude = messages[j].get("content", "")[:200]
                        break

                learnings.append(
                    Learning(
                        type=LearningType.CORRECTION,
                        title="Approach preference noted",
                        content=f"User correction: {content[:150]}...",
                        confidence=0.7,
                        evidence=[
                            f"Previous approach: {prev_claude[:100]}..."
                            if prev_claude
                            else "No context"
                        ],
                        domain=transcript.project,
                    )
                )
                break

    return learnings


def extract_preferences(transcript: SessionTranscript) -> List[Learning]:
    """
    Detect user preferences from their choices and statements.

    Signals:
    - "I prefer..." / "I like..." / "Let's always..."
    - Consistent choice of A over B across messages
    - Explicit style/approach statements
    """
    learnings = []
    preference_phrases = [
        (r"I\s+(prefer|like|want|always)\s+(.+?)(?:\.|$|,)", "preference"),
        (r"let's\s+always\s+(.+?)(?:\.|$|,)", "rule"),
        (r"from\s+now\s+on[,.]?\s+(.+?)(?:\.|$)", "rule"),
        (r"never\s+(.+?)(?:\.|$|,)", "anti-preference"),
        (r"don't\s+(.+?)\s+unless\s+(.+?)(?:\.|$)", "conditional"),
    ]

    for msg in transcript.messages:
        if msg.get("role") != "user":
            continue

        content = msg.get("content", "")

        for pattern, pref_type in preference_phrases:
            matches = re.findall(pattern, content, re.IGNORECASE)
            for match in matches:
                if isinstance(match, tuple):
                    pref_content = " ".join(match)
                else:
                    pref_content = match

                if len(pref_content) > 10:
                    learnings.append(
                        Learning(
                            type=LearningType.PREFERENCE,
                            title=f"User preference: {pref_type}",
                            content=pref_content[:200],
                            confidence=0.8,
                            evidence=[f"From: {content[:100]}..."],
                            domain=transcript.project,
                        )
                    )

    return learnings


def extract_file_patterns(transcript: SessionTranscript) -> List[Learning]:
    """
    Identify files that come up repeatedly or seem important.

    Signals:
    - Same file mentioned/edited multiple times
    - File explicitly marked as important
    - File at center of problem/solution
    """
    learnings = []

    file_pattern = r"[\w/.-]+\.(py|pyx|pxd|cpp|c|h|rs|go|js|ts|tsx|json|yaml|toml|md)"
    file_counts = Counter()

    for msg in transcript.messages:
        content = msg.get("content", "")
        re.findall(file_pattern, content)
        for match in re.finditer(file_pattern, content):
            file_counts[match.group()] += 1

    for f in transcript.files_touched:
        file_counts[f] += 2

    for filepath, count in file_counts.most_common(5):
        if count >= 3:
            learnings.append(
                Learning(
                    type=LearningType.FILE_PATTERN,
                    title=f"Key file: {filepath.split('/')[-1]}",
                    content=f"File {filepath} was central to this session (mentioned {count} times)",
                    confidence=min(0.5 + count * 0.1, 0.9),
                    evidence=[f"Mentions: {count}"],
                    domain=transcript.project,
                )
            )

    return learnings


def extract_struggles(transcript: SessionTranscript) -> List[Learning]:
    """
    Detect when something took multiple attempts to get right.

    Signals:
    - Same operation attempted multiple times
    - Error messages followed by fixes
    - Long sequences on same topic before success
    """
    learnings = []

    error_indicators = [
        r"error[:\s]",
        r"failed",
        r"doesn't work",
        r"not working",
        r"broken",
        r"bug",
        r"issue",
        r"problem",
    ]

    success_indicators = [
        r"works?( now)?!?",
        r"fixed",
        r"solved",
        r"success",
        r"passing",
        r"done",
    ]

    in_struggle = False
    struggle_start = None
    struggle_topic = None

    for i, msg in enumerate(transcript.messages):
        content = msg.get("content", "").lower()

        if not in_struggle:
            for pattern in error_indicators:
                if re.search(pattern, content):
                    in_struggle = True
                    struggle_start = i
                    struggle_topic = content[:100]
                    break

        if in_struggle:
            for pattern in success_indicators:
                if re.search(pattern, content):
                    struggle_length = i - struggle_start
                    if struggle_length >= 3:
                        learnings.append(
                            Learning(
                                type=LearningType.STRUGGLE,
                                title="Problem required multiple attempts",
                                content=f"Struggle ({struggle_length} messages): {struggle_topic}",
                                confidence=0.6,
                                evidence=[
                                    f"Started at message {struggle_start}, resolved at {i}"
                                ],
                                domain=transcript.project,
                            )
                        )
                    in_struggle = False
                    break

    return learnings


def extract_breakthroughs(transcript: SessionTranscript) -> List[Learning]:
    """
    Identify breakthrough moments - key insights or realizations.

    Signals:
    - "Aha!" / "That's it!" / "I see now"
    - Solution after long struggle
    - Explicit marking of key insight
    """
    learnings = []

    breakthrough_phrases = [
        r"aha!?",
        r"that'?s?\s+it!?",
        r"(now\s+)?I\s+(see|understand|get\s+it)",
        r"(the\s+)?key\s+(is|was|insight)",
        r"breakthrough",
        r"finally!?",
        r"eureka",
        r"this\s+(is|was)\s+the\s+(problem|issue|root\s+cause)",
    ]

    for msg in transcript.messages:
        content = msg.get("content", "")

        for pattern in breakthrough_phrases:
            if re.search(pattern, content, re.IGNORECASE):
                learnings.append(
                    Learning(
                        type=LearningType.BREAKTHROUGH,
                        title="Key insight discovered",
                        content=content[:250],
                        confidence=0.75,
                        evidence=["Breakthrough moment in conversation"],
                        domain=transcript.project,
                    )
                )
                break

    return learnings


def extract_decisions(transcript: SessionTranscript) -> List[Learning]:
    """
    Identify architectural or design decisions made during session.

    Signals:
    - "Let's go with..." / "We'll use..."
    - Explicit decision statements
    - Choice between alternatives
    """
    learnings = []

    decision_phrases = [
        r"let'?s\s+(go\s+with|use|choose|pick|do)\s+(.+?)(?:\.|$|,)",
        r"we'?ll\s+(use|go\s+with|implement)\s+(.+?)(?:\.|$|,)",
        r"(I\s+)?decide[d]?\s+(to|on)\s+(.+?)(?:\.|$|,)",
        r"the\s+(decision|choice)\s+is\s+(.+?)(?:\.|$|,)",
        r"(going|go)\s+with\s+(.+?)(?:\.|$|,)",
    ]

    for msg in transcript.messages:
        if msg.get("role") != "user":
            continue

        content = msg.get("content", "")

        for pattern in decision_phrases:
            matches = re.findall(pattern, content, re.IGNORECASE)
            for match in matches:
                decision = match[-1] if isinstance(match, tuple) else match
                if len(decision) > 10:
                    learnings.append(
                        Learning(
                            type=LearningType.DECISION,
                            title="Decision made",
                            content=decision[:200],
                            confidence=0.7,
                            evidence=[f"Context: {content[:100]}..."],
                            domain=transcript.project,
                        )
                    )

    return learnings


def observe_session(transcript: SessionTranscript) -> List[Learning]:
    """
    Main entry point: Extract all learnings from a session.

    Returns a list of Learning objects ready to be converted to wisdom.
    """
    all_learnings = []

    all_learnings.extend(extract_corrections(transcript))
    all_learnings.extend(extract_preferences(transcript))
    all_learnings.extend(extract_file_patterns(transcript))
    all_learnings.extend(extract_struggles(transcript))
    all_learnings.extend(extract_breakthroughs(transcript))
    all_learnings.extend(extract_decisions(transcript))

    unique_learnings = _deduplicate_learnings(all_learnings)

    return unique_learnings


def _deduplicate_learnings(learnings: List[Learning]) -> List[Learning]:
    """Remove duplicate or very similar learnings."""
    if not learnings:
        return []

    unique = []
    seen_content = set()

    for learning in learnings:
        content_key = learning.content[:50].lower()
        if content_key not in seen_content:
            seen_content.add(content_key)
            unique.append(learning)

    return unique


def record_observation(learning: Learning, session_id: int = None):
    """
    Record an observation without immediately converting to wisdom.

    Observations are staged - they become wisdom if confirmed or seen multiple times.
    """
    content = f"{learning.title}: {learning.content}"
    if _is_garbage_content(content):
        return

    graph = get_synapse_graph()

    graph.observe(
        category="observation",
        title=learning.title,
        content=json.dumps({
            "type": learning.type.value,
            "content": learning.content,
            "confidence": learning.confidence,
            "evidence": learning.evidence,
            "domain": learning.domain,
        }),
        project=learning.domain,
        tags=["observation", learning.type.value],
    )
    save_synapse()


def promote_observation_to_wisdom(observation_id: str) -> Optional[str]:
    """
    Convert an observation to permanent wisdom.

    Returns the new wisdom ID.
    """
    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="observation", limit=500)

    obs = None
    for ep in episodes:
        if ep.get("id") == observation_id:
            obs = ep
            break

    if not obs:
        return None

    content_str = obs.get("content", "{}")
    try:
        data = json.loads(content_str)
    except json.JSONDecodeError:
        data = {"content": content_str, "type": "insight", "confidence": 0.6}

    obs_type = data.get("type", "insight")
    content = data.get("content", "")
    confidence = data.get("confidence", 0.6)

    type_map = {
        "correction": WisdomType.PREFERENCE,
        "preference": WisdomType.PREFERENCE,
        "pattern": WisdomType.PATTERN,
        "struggle": WisdomType.FAILURE,
        "breakthrough": WisdomType.INSIGHT,
        "file_pattern": WisdomType.PATTERN,
        "decision": WisdomType.INSIGHT,
    }

    wisdom_type = type_map.get(obs_type, WisdomType.INSIGHT)

    title = obs.get("title", "")
    if ":" in content:
        body = content.split(":", 1)[1].strip()
    else:
        body = content

    wisdom_id = gain_wisdom(
        type=wisdom_type,
        title=title.strip(),
        content=body.strip(),
        confidence=confidence,
    )

    return wisdom_id


def get_pending_observations(limit: int = 20) -> List[Dict]:
    """Get observations not yet converted to wisdom."""
    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="observation", limit=limit)

    results = []
    for ep in episodes:
        content_str = ep.get("content", "{}")
        try:
            data = json.loads(content_str)
        except json.JSONDecodeError:
            data = {"content": content_str, "type": "unknown", "confidence": 0.5}

        results.append({
            "id": ep.get("id", ""),
            "type": data.get("type", "unknown"),
            "content": f"{ep.get('title', '')}: {data.get('content', '')}",
            "confidence": data.get("confidence", 0.5),
            "evidence": data.get("evidence", []),
            "created_at": ep.get("timestamp", ""),
        })

    results.sort(key=lambda x: (-x["confidence"], x["created_at"]), reverse=False)
    return results[:limit]


def auto_promote_high_confidence(threshold: float = 0.75) -> List[str]:
    """
    Automatically promote high-confidence observations to wisdom.

    Returns list of promoted wisdom IDs.
    """
    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="observation", limit=100)

    promoted = []
    for ep in episodes:
        content_str = ep.get("content", "{}")
        try:
            data = json.loads(content_str)
        except json.JSONDecodeError:
            continue

        confidence = data.get("confidence", 0.5)
        if confidence >= threshold:
            obs_id = ep.get("id", "")
            wisdom_id = promote_observation_to_wisdom(obs_id)
            if wisdom_id:
                promoted.append(wisdom_id)

    return promoted


def reflect_on_session(
    messages: List[Dict],
    files_touched: List[str] = None,
    project: str = None,
    auto_promote: bool = True,
) -> Dict:
    """
    Main API: Reflect on a session and extract learnings.

    Args:
        messages: List of {role, content, timestamp} dicts
        files_touched: Files that were edited
        project: Project name
        auto_promote: Automatically promote high-confidence learnings

    Returns:
        Summary of what was learned
    """
    transcript = SessionTranscript(
        messages=messages, files_touched=set(files_touched or []), project=project
    )

    learnings = observe_session(transcript)

    for learning in learnings:
        record_observation(learning)

    promoted = []
    if auto_promote:
        promoted = auto_promote_high_confidence(threshold=0.75)

    return {
        "observations": len(learnings),
        "by_type": Counter(item.type.value for item in learnings),
        "promoted_to_wisdom": len(promoted),
        "pending_review": len(learnings) - len(promoted),
        "learnings": [
            {
                "type": item.type.value,
                "title": item.title,
                "confidence": item.confidence,
            }
            for item in learnings
        ],
    }


def format_reflection_summary(reflection: Dict) -> str:
    """Format reflection results for display."""
    lines = []
    lines.append("# Session Reflection")
    lines.append("")

    if reflection["observations"] == 0:
        lines.append("No new observations from this session.")
        return "\n".join(lines)

    lines.append(f"Observed **{reflection['observations']}** potential learnings:")
    lines.append("")

    type_icons = {
        "correction": "[C]",
        "preference": "[P]",
        "pattern": "[R]",
        "struggle": "[S]",
        "breakthrough": "[B]",
        "file_pattern": "[F]",
        "decision": "[D]",
    }

    for ltype, count in reflection["by_type"].items():
        icon = type_icons.get(ltype, "*")
        lines.append(f"  {icon} {ltype}: {count}")

    lines.append("")

    if reflection["promoted_to_wisdom"] > 0:
        lines.append(
            f"+ **{reflection['promoted_to_wisdom']}** auto-promoted to wisdom (high confidence)"
        )

    if reflection["pending_review"] > 0:
        lines.append(
            f"? **{reflection['pending_review']}** pending review (`soul observations`)"
        )

    return "\n".join(lines)
