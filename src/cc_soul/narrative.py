"""
Narrative Memory: Stories, not just data.

Human memory works through narrative - we remember stories, sequences of events,
emotional arcs. This module gives the soul narrative memory:

- Episodes: Meaningful chunks of work with beginning, middle, end
- Story threads: Connected episodes forming larger narratives
- Emotional arcs: How work felt (struggle -> breakthrough -> satisfaction)
- Cast of characters: Files, concepts, patterns that appear across episodes
- Story-based recall: "Remember when we..." instead of keyword search
"""

import json
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from enum import Enum
from typing import Dict, List, Optional, Tuple, Any
from collections import Counter

# TODO: Migrate to synapse graph storage
# from .core import get_synapse_graph, save_synapse


class EmotionalTone(str, Enum):
    STRUGGLE = "struggle"  # Hard problem, frustration
    EXPLORATION = "exploration"  # Discovery, curiosity
    BREAKTHROUGH = "breakthrough"  # Aha moment, success
    SATISFACTION = "satisfaction"  # Completed well
    FRUSTRATION = "frustration"  # Blocked, failed
    ROUTINE = "routine"  # Normal work, no strong emotion


class EpisodeType(str, Enum):
    BUGFIX = "bugfix"
    FEATURE = "feature"
    REFACTOR = "refactor"
    LEARNING = "learning"
    DEBUGGING = "debugging"
    PLANNING = "planning"
    REVIEW = "review"
    EXPLORATION = "exploration"


@dataclass
class Episode:
    """A meaningful chunk of work - a complete story with arc."""

    id: int
    title: str
    summary: str
    episode_type: EpisodeType
    emotional_arc: List[EmotionalTone]  # How the work felt over time
    key_moments: List[str]  # Pivotal moments in the story
    characters: Dict[str, List[str]]  # files, concepts, tools involved
    started_at: str
    ended_at: Optional[str] = None
    duration_minutes: int = 0
    outcome: str = ""  # How it ended
    lessons: List[str] = field(default_factory=list)
    thread_id: Optional[int] = None  # Part of larger narrative
    conversation_id: Optional[int] = None


@dataclass
class StoryThread:
    """A connected series of episodes forming a larger narrative."""

    id: int
    title: str
    theme: str  # What unifies these episodes
    episodes: List[int]  # Episode IDs in order
    started_at: str
    last_updated: str
    status: str = "ongoing"  # ongoing, completed, abandoned
    arc_summary: str = ""  # The overarching narrative


def _ensure_narrative_tables():
    """Create narrative tables if they don't exist."""
    # TODO: Migrate to synapse graph storage
    pass


# =============================================================================
# EPISODE CAPTURE
# =============================================================================


def start_episode(
    title: str,
    episode_type: EpisodeType,
    initial_emotion: EmotionalTone = EmotionalTone.EXPLORATION,
    conversation_id: int = None,
) -> int:
    """Start a new episode - the beginning of a story."""
    # TODO: Migrate to synapse graph storage
    return 0


def add_moment(episode_id: int, moment: str, emotion: EmotionalTone = None) -> bool:
    """Add a key moment to an episode - plot points in the story."""
    # TODO: Migrate to synapse graph storage
    return False


def add_character(episode_id: int, character_type: str, character: str) -> bool:
    """Add a character (file, concept, tool) to an episode."""
    # TODO: Migrate to synapse graph storage
    return False


def end_episode(
    episode_id: int,
    summary: str,
    outcome: str,
    lessons: List[str] = None,
    final_emotion: EmotionalTone = EmotionalTone.SATISFACTION,
) -> bool:
    """End an episode - the conclusion of the story."""
    # TODO: Migrate to synapse graph storage
    return False


def get_episode(episode_id: int) -> Optional[Episode]:
    """Get a complete episode by ID."""
    # TODO: Migrate to synapse graph storage
    return None


def get_ongoing_episodes(limit: int = 5) -> List[Episode]:
    """Get episodes that haven't been ended yet."""
    # TODO: Migrate to synapse graph storage
    return []


def recall_episodes(limit: int = 10, episode_type: EpisodeType = None) -> List[Episode]:
    """Recall recent episodes, optionally filtered by type."""
    # TODO: Migrate to synapse graph storage
    return []


# =============================================================================
# STORY THREADS
# =============================================================================


def create_thread(title: str, theme: str, first_episode_id: int = None) -> int:
    """Create a new story thread - a larger narrative arc."""
    # TODO: Migrate to synapse graph storage
    return 0


def add_to_thread(thread_id: int, episode_id: int) -> bool:
    """Add an episode to an existing story thread."""
    # TODO: Migrate to synapse graph storage
    return False


def complete_thread(thread_id: int, arc_summary: str) -> bool:
    """Complete a story thread with a summary of the arc."""
    # TODO: Migrate to synapse graph storage
    return False


def get_thread(thread_id: int) -> Optional[StoryThread]:
    """Get a story thread with all its episodes."""
    # TODO: Migrate to synapse graph storage
    return None


# =============================================================================
# NARRATIVE RECALL - Story-based memory retrieval
# =============================================================================


def recall_by_emotion(emotion: EmotionalTone, limit: int = 10) -> List[Episode]:
    """Recall episodes by emotional tone - 'remember when we struggled with...'"""
    # TODO: Migrate to synapse graph storage
    return []


def recall_by_character(character: str, limit: int = 10) -> List[Episode]:
    """Recall episodes featuring a character (file, concept, tool)."""
    # TODO: Migrate to synapse graph storage
    return []


def recall_by_type(episode_type: EpisodeType, limit: int = 10) -> List[Episode]:
    """Recall episodes by type - 'remember our debugging sessions'."""
    # TODO: Migrate to synapse graph storage
    return []


def recall_breakthroughs(limit: int = 10) -> List[Episode]:
    """Recall breakthrough moments - our greatest hits."""
    return recall_by_emotion(EmotionalTone.BREAKTHROUGH, limit)


def recall_struggles(limit: int = 10) -> List[Episode]:
    """Recall struggles - learning opportunities from hard times."""
    return recall_by_emotion(EmotionalTone.STRUGGLE, limit)


def get_recurring_characters(limit: int = 20) -> Dict[str, List[Tuple[str, int]]]:
    """Get characters that appear across multiple episodes - our regulars."""
    # TODO: Migrate to synapse graph storage
    return {"files": [], "concepts": [], "tools": []}


def get_emotional_journey(days: int = 30) -> Dict[str, Any]:
    """Get the emotional journey over time - our arc."""
    # TODO: Migrate to synapse graph storage
    return {
        "distribution": {},
        "dominant": None,
        "total_episodes": 0,
        "breakthroughs": 0,
        "struggles": 0,
    }


def format_episode_story(episode: Episode) -> str:
    """Format an episode as a readable story."""
    lines = []

    # Title and type
    type_emoji = {
        "bugfix": "🐛",
        "feature": "✨",
        "refactor": "🔄",
        "learning": "📚",
        "debugging": "🔍",
        "planning": "📋",
        "review": "👀",
        "exploration": "🗺️",
    }
    emoji = type_emoji.get(episode.episode_type.value, "📖")
    lines.append(f"## {emoji} {episode.title}")
    lines.append("")

    # Emotional arc
    if episode.emotional_arc:
        arc_str = " → ".join(e.value for e in episode.emotional_arc)
        lines.append(f"**Arc:** {arc_str}")

    # Duration
    if episode.duration_minutes:
        lines.append(f"**Duration:** {episode.duration_minutes} minutes")
    lines.append("")

    # Summary
    if episode.summary:
        lines.append(episode.summary)
        lines.append("")

    # Key moments
    if episode.key_moments:
        lines.append("**Key Moments:**")
        for moment in episode.key_moments[:5]:
            lines.append(f"- {moment}")
        lines.append("")

    # Cast of characters
    if episode.characters:
        chars = episode.characters
        if chars.get("files"):
            lines.append(f"**Files:** {', '.join(chars['files'][:5])}")
        if chars.get("concepts"):
            lines.append(f"**Concepts:** {', '.join(chars['concepts'][:5])}")

    # Outcome
    if episode.outcome:
        lines.append(f"\n**Outcome:** {episode.outcome}")

    # Lessons
    if episode.lessons:
        lines.append("\n**Lessons learned:**")
        for lesson in episode.lessons:
            lines.append(f"- {lesson}")

    return "\n".join(lines)


def get_narrative_stats() -> Dict[str, Any]:
    """Get statistics about our narrative memory."""
    # TODO: Migrate to synapse graph storage
    return {
        "total_episodes": 0,
        "total_threads": 0,
        "ongoing_threads": 0,
        "total_hours": 0.0,
        "by_type": {},
    }


# =============================================================================
# AUTOMATIC EPISODE EXTRACTION FROM SESSIONS
# =============================================================================


def extract_episode_from_session(
    messages: List[Dict], project: str = None
) -> Optional[Episode]:
    """
    Extract an episode from a session transcript.

    Analyzes the conversation to identify:
    - What type of work was done
    - The emotional arc (based on language patterns)
    - Key moments (commits, errors, breakthroughs)
    - Characters (files, concepts mentioned)
    """
    if not messages:
        return None

    # Combine all assistant and user messages
    full_text = " ".join(
        m.get("content", "") for m in messages if isinstance(m.get("content"), str)
    )
    full_text_lower = full_text.lower()

    # Detect episode type
    type_signals = {
        EpisodeType.BUGFIX: ["fix", "bug", "error", "issue", "broken"],
        EpisodeType.FEATURE: ["add", "implement", "create", "new feature"],
        EpisodeType.REFACTOR: ["refactor", "cleanup", "reorganize", "restructure"],
        EpisodeType.DEBUGGING: ["debug", "investigate", "trace", "log"],
        EpisodeType.LEARNING: ["understand", "learn", "explain", "how does"],
        EpisodeType.PLANNING: ["plan", "design", "architect", "strategy"],
        EpisodeType.REVIEW: ["review", "check", "verify", "test"],
    }

    detected_type = EpisodeType.EXPLORATION
    max_score = 0
    for ep_type, signals in type_signals.items():
        score = sum(1 for s in signals if s in full_text_lower)
        if score > max_score:
            max_score = score
            detected_type = ep_type

    # Detect emotional arc
    emotions = []
    emotion_signals = {
        EmotionalTone.STRUGGLE: [
            "difficult",
            "stuck",
            "confused",
            "can't",
            "failed",
            "error",
        ],
        EmotionalTone.BREAKTHROUGH: [
            "works!",
            "got it",
            "solved",
            "finally",
            "success",
        ],
        EmotionalTone.FRUSTRATION: ["ugh", "again", "still not", "why is"],
        EmotionalTone.SATISFACTION: ["perfect", "great", "done", "complete", "merged"],
        EmotionalTone.EXPLORATION: ["try", "maybe", "what if", "let me"],
    }

    for emotion, signals in emotion_signals.items():
        if any(s in full_text_lower for s in signals):
            emotions.append(emotion)

    if not emotions:
        emotions = [EmotionalTone.ROUTINE]

    # Extract file mentions
    import re

    file_pattern = r"[\w/.-]+\.(py|js|ts|tsx|jsx|go|rs|cpp|c|h|md|json|yaml|yml|toml)"
    files = list(set(re.findall(file_pattern, full_text)))[:10]

    # Extract key moments (git commits, test results, etc.)
    moments = []
    commit_pattern = r"commit[ed]?\s+[a-f0-9]{7,}"
    for match in re.findall(commit_pattern, full_text_lower):
        moments.append(f"Commit: {match}")

    if "test" in full_text_lower and "pass" in full_text_lower:
        moments.append("Tests passed")
    if "error" in full_text_lower:
        moments.append("Encountered errors")

    # Create the episode
    title = f"{detected_type.value.title()}: {project or 'Work Session'}"

    return Episode(
        id=0,  # Will be assigned by DB
        title=title,
        summary="",  # To be filled when ending
        episode_type=detected_type,
        emotional_arc=emotions,
        key_moments=moments,
        characters={"files": files, "concepts": [], "tools": []},
        started_at=datetime.now().isoformat(),
    )
