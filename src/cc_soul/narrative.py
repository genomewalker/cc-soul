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
import sqlite3
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from enum import Enum
from typing import Dict, List, Optional, Tuple, Any
from collections import Counter

from .core import SOUL_DB, init_soul


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
    init_soul()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS episodes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            summary TEXT,
            episode_type TEXT,
            emotional_arc TEXT,
            key_moments TEXT,
            characters TEXT,
            started_at TEXT,
            ended_at TEXT,
            duration_minutes INTEGER DEFAULT 0,
            outcome TEXT,
            lessons TEXT,
            thread_id INTEGER,
            conversation_id INTEGER,
            FOREIGN KEY (thread_id) REFERENCES story_threads(id)
        )
    """)

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS story_threads (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            theme TEXT,
            episodes TEXT,
            started_at TEXT,
            last_updated TEXT,
            status TEXT DEFAULT 'ongoing',
            arc_summary TEXT
        )
    """)

    # Index for efficient narrative queries
    cursor.execute("""
        CREATE INDEX IF NOT EXISTS idx_episodes_type ON episodes(episode_type)
    """)
    cursor.execute("""
        CREATE INDEX IF NOT EXISTS idx_episodes_thread ON episodes(thread_id)
    """)

    conn.commit()
    conn.close()


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
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        INSERT INTO episodes
        (title, episode_type, emotional_arc, key_moments, characters, started_at, conversation_id)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    """,
        (
            title,
            episode_type.value,
            json.dumps([initial_emotion.value]),
            json.dumps([]),
            json.dumps({"files": [], "concepts": [], "tools": []}),
            datetime.now().isoformat(),
            conversation_id,
        ),
    )

    episode_id = cursor.lastrowid
    conn.commit()
    conn.close()
    return episode_id


def add_moment(episode_id: int, moment: str, emotion: EmotionalTone = None) -> bool:
    """Add a key moment to an episode - plot points in the story."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        "SELECT key_moments, emotional_arc FROM episodes WHERE id = ?", (episode_id,)
    )
    row = cursor.fetchone()
    if not row:
        conn.close()
        return False

    moments = json.loads(row[0]) if row[0] else []
    arc = json.loads(row[1]) if row[1] else []

    moments.append(f"[{datetime.now().strftime('%H:%M')}] {moment}")
    if emotion:
        arc.append(emotion.value)

    cursor.execute(
        """
        UPDATE episodes
        SET key_moments = ?, emotional_arc = ?
        WHERE id = ?
    """,
        (json.dumps(moments), json.dumps(arc), episode_id),
    )

    conn.commit()
    conn.close()
    return True


def add_character(episode_id: int, character_type: str, character: str) -> bool:
    """Add a character (file, concept, tool) to an episode."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute("SELECT characters FROM episodes WHERE id = ?", (episode_id,))
    row = cursor.fetchone()
    if not row:
        conn.close()
        return False

    characters = (
        json.loads(row[0]) if row[0] else {"files": [], "concepts": [], "tools": []}
    )
    if character_type not in characters:
        characters[character_type] = []
    if character not in characters[character_type]:
        characters[character_type].append(character)

    cursor.execute(
        "UPDATE episodes SET characters = ? WHERE id = ?",
        (json.dumps(characters), episode_id),
    )

    conn.commit()
    conn.close()
    return True


def end_episode(
    episode_id: int,
    summary: str,
    outcome: str,
    lessons: List[str] = None,
    final_emotion: EmotionalTone = EmotionalTone.SATISFACTION,
) -> bool:
    """End an episode - the conclusion of the story."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        "SELECT started_at, emotional_arc FROM episodes WHERE id = ?", (episode_id,)
    )
    row = cursor.fetchone()
    if not row:
        conn.close()
        return False

    started_at = datetime.fromisoformat(row[0])
    ended_at = datetime.now()
    duration = int((ended_at - started_at).total_seconds() / 60)

    arc = json.loads(row[1]) if row[1] else []
    arc.append(final_emotion.value)

    cursor.execute(
        """
        UPDATE episodes
        SET summary = ?, ended_at = ?, duration_minutes = ?, outcome = ?,
            lessons = ?, emotional_arc = ?
        WHERE id = ?
    """,
        (
            summary,
            ended_at.isoformat(),
            duration,
            outcome,
            json.dumps(lessons or []),
            json.dumps(arc),
            episode_id,
        ),
    )

    conn.commit()
    conn.close()
    return True


def get_episode(episode_id: int) -> Optional[Episode]:
    """Get a complete episode by ID."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        SELECT id, title, summary, episode_type, emotional_arc, key_moments,
               characters, started_at, ended_at, duration_minutes, outcome,
               lessons, thread_id, conversation_id
        FROM episodes WHERE id = ?
    """,
        (episode_id,),
    )

    row = cursor.fetchone()
    conn.close()

    if not row:
        return None

    return Episode(
        id=row[0],
        title=row[1],
        summary=row[2] or "",
        episode_type=EpisodeType(row[3]) if row[3] else EpisodeType.EXPLORATION,
        emotional_arc=[EmotionalTone(e) for e in json.loads(row[4])] if row[4] else [],
        key_moments=json.loads(row[5]) if row[5] else [],
        characters=json.loads(row[6]) if row[6] else {},
        started_at=row[7],
        ended_at=row[8],
        duration_minutes=row[9] or 0,
        outcome=row[10] or "",
        lessons=json.loads(row[11]) if row[11] else [],
        thread_id=row[12],
        conversation_id=row[13],
    )


def get_ongoing_episodes(limit: int = 5) -> List[Episode]:
    """Get episodes that haven't been ended yet."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        SELECT id, title, summary, episode_type, emotional_arc, key_moments,
               characters, started_at, ended_at, duration_minutes, outcome,
               lessons, thread_id, conversation_id
        FROM episodes
        WHERE ended_at IS NULL
        ORDER BY started_at DESC
        LIMIT ?
    """,
        (limit,),
    )

    rows = cursor.fetchall()
    conn.close()

    return [
        Episode(
            id=row[0],
            title=row[1],
            summary=row[2] or "",
            episode_type=EpisodeType(row[3]) if row[3] else EpisodeType.EXPLORATION,
            emotional_arc=[EmotionalTone(e) for e in json.loads(row[4])]
            if row[4]
            else [],
            key_moments=json.loads(row[5]) if row[5] else [],
            characters=json.loads(row[6]) if row[6] else {},
            started_at=row[7],
            ended_at=row[8],
            duration_minutes=row[9] or 0,
            outcome=row[10] or "",
            lessons=json.loads(row[11]) if row[11] else [],
            thread_id=row[12],
            conversation_id=row[13],
        )
        for row in rows
    ]


def recall_episodes(limit: int = 10, episode_type: EpisodeType = None) -> List[Episode]:
    """Recall recent episodes, optionally filtered by type."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    if episode_type:
        cursor.execute(
            """
            SELECT id, title, summary, episode_type, emotional_arc, key_moments,
                   characters, started_at, ended_at, duration_minutes, outcome,
                   lessons, thread_id, conversation_id
            FROM episodes
            WHERE episode_type = ?
            ORDER BY started_at DESC
            LIMIT ?
        """,
            (episode_type.value, limit),
        )
    else:
        cursor.execute(
            """
            SELECT id, title, summary, episode_type, emotional_arc, key_moments,
                   characters, started_at, ended_at, duration_minutes, outcome,
                   lessons, thread_id, conversation_id
            FROM episodes
            ORDER BY started_at DESC
            LIMIT ?
        """,
            (limit,),
        )

    rows = cursor.fetchall()
    conn.close()

    return [
        Episode(
            id=row[0],
            title=row[1],
            summary=row[2] or "",
            episode_type=EpisodeType(row[3]) if row[3] else EpisodeType.EXPLORATION,
            emotional_arc=[EmotionalTone(e) for e in json.loads(row[4])]
            if row[4]
            else [],
            key_moments=json.loads(row[5]) if row[5] else [],
            characters=json.loads(row[6]) if row[6] else {},
            started_at=row[7],
            ended_at=row[8],
            duration_minutes=row[9] or 0,
            outcome=row[10] or "",
            lessons=json.loads(row[11]) if row[11] else [],
            thread_id=row[12],
            conversation_id=row[13],
        )
        for row in rows
    ]


# =============================================================================
# STORY THREADS
# =============================================================================


def create_thread(title: str, theme: str, first_episode_id: int = None) -> int:
    """Create a new story thread - a larger narrative arc."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    episodes = [first_episode_id] if first_episode_id else []

    cursor.execute(
        """
        INSERT INTO story_threads (title, theme, episodes, started_at, last_updated)
        VALUES (?, ?, ?, ?, ?)
    """,
        (
            title,
            theme,
            json.dumps(episodes),
            datetime.now().isoformat(),
            datetime.now().isoformat(),
        ),
    )

    thread_id = cursor.lastrowid

    # Link episode to thread
    if first_episode_id:
        cursor.execute(
            "UPDATE episodes SET thread_id = ? WHERE id = ?",
            (thread_id, first_episode_id),
        )

    conn.commit()
    conn.close()
    return thread_id


def add_to_thread(thread_id: int, episode_id: int) -> bool:
    """Add an episode to an existing story thread."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute("SELECT episodes FROM story_threads WHERE id = ?", (thread_id,))
    row = cursor.fetchone()
    if not row:
        conn.close()
        return False

    episodes = json.loads(row[0]) if row[0] else []
    if episode_id not in episodes:
        episodes.append(episode_id)

    cursor.execute(
        """
        UPDATE story_threads
        SET episodes = ?, last_updated = ?
        WHERE id = ?
    """,
        (json.dumps(episodes), datetime.now().isoformat(), thread_id),
    )

    cursor.execute(
        "UPDATE episodes SET thread_id = ? WHERE id = ?", (thread_id, episode_id)
    )

    conn.commit()
    conn.close()
    return True


def complete_thread(thread_id: int, arc_summary: str) -> bool:
    """Complete a story thread with a summary of the arc."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        UPDATE story_threads
        SET status = 'completed', arc_summary = ?, last_updated = ?
        WHERE id = ?
    """,
        (arc_summary, datetime.now().isoformat(), thread_id),
    )

    success = cursor.rowcount > 0
    conn.commit()
    conn.close()
    return success


def get_thread(thread_id: int) -> Optional[StoryThread]:
    """Get a story thread with all its episodes."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        SELECT id, title, theme, episodes, started_at, last_updated, status, arc_summary
        FROM story_threads WHERE id = ?
    """,
        (thread_id,),
    )

    row = cursor.fetchone()
    conn.close()

    if not row:
        return None

    return StoryThread(
        id=row[0],
        title=row[1],
        theme=row[2] or "",
        episodes=json.loads(row[3]) if row[3] else [],
        started_at=row[4],
        last_updated=row[5],
        status=row[6] or "ongoing",
        arc_summary=row[7] or "",
    )


# =============================================================================
# NARRATIVE RECALL - Story-based memory retrieval
# =============================================================================


def recall_by_emotion(emotion: EmotionalTone, limit: int = 10) -> List[Episode]:
    """Recall episodes by emotional tone - 'remember when we struggled with...'"""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        SELECT id FROM episodes
        WHERE emotional_arc LIKE ?
        ORDER BY ended_at DESC
        LIMIT ?
    """,
        (f'%"{emotion.value}"%', limit),
    )

    episode_ids = [row[0] for row in cursor.fetchall()]
    conn.close()

    return [get_episode(eid) for eid in episode_ids if get_episode(eid)]


def recall_by_character(character: str, limit: int = 10) -> List[Episode]:
    """Recall episodes featuring a character (file, concept, tool)."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        SELECT id FROM episodes
        WHERE characters LIKE ?
        ORDER BY ended_at DESC
        LIMIT ?
    """,
        (f'%"{character}"%', limit),
    )

    episode_ids = [row[0] for row in cursor.fetchall()]
    conn.close()

    return [get_episode(eid) for eid in episode_ids if get_episode(eid)]


def recall_by_type(episode_type: EpisodeType, limit: int = 10) -> List[Episode]:
    """Recall episodes by type - 'remember our debugging sessions'."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        SELECT id FROM episodes
        WHERE episode_type = ?
        ORDER BY ended_at DESC
        LIMIT ?
    """,
        (episode_type.value, limit),
    )

    episode_ids = [row[0] for row in cursor.fetchall()]
    conn.close()

    return [get_episode(eid) for eid in episode_ids if get_episode(eid)]


def recall_breakthroughs(limit: int = 10) -> List[Episode]:
    """Recall breakthrough moments - our greatest hits."""
    return recall_by_emotion(EmotionalTone.BREAKTHROUGH, limit)


def recall_struggles(limit: int = 10) -> List[Episode]:
    """Recall struggles - learning opportunities from hard times."""
    return recall_by_emotion(EmotionalTone.STRUGGLE, limit)


def get_recurring_characters(limit: int = 20) -> Dict[str, List[Tuple[str, int]]]:
    """Get characters that appear across multiple episodes - our regulars."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute("SELECT characters FROM episodes")

    files = Counter()
    concepts = Counter()
    tools = Counter()

    for row in cursor.fetchall():
        if row[0]:
            chars = json.loads(row[0])
            files.update(chars.get("files", []))
            concepts.update(chars.get("concepts", []))
            tools.update(chars.get("tools", []))

    conn.close()

    return {
        "files": files.most_common(limit),
        "concepts": concepts.most_common(limit),
        "tools": tools.most_common(limit),
    }


def get_emotional_journey(days: int = 30) -> Dict[str, Any]:
    """Get the emotional journey over time - our arc."""
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    since = (datetime.now() - timedelta(days=days)).isoformat()

    cursor.execute(
        """
        SELECT emotional_arc FROM episodes
        WHERE started_at > ?
    """,
        (since,),
    )

    all_emotions = []
    for row in cursor.fetchall():
        if row[0]:
            all_emotions.extend(json.loads(row[0]))

    conn.close()

    emotion_counts = Counter(all_emotions)
    total = len(all_emotions) or 1

    return {
        "distribution": {e: c / total for e, c in emotion_counts.items()},
        "dominant": emotion_counts.most_common(1)[0][0] if emotion_counts else None,
        "total_episodes": len(all_emotions),
        "breakthroughs": emotion_counts.get("breakthrough", 0),
        "struggles": emotion_counts.get("struggle", 0),
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
    _ensure_narrative_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute("SELECT COUNT(*) FROM episodes")
    total_episodes = cursor.fetchone()[0]

    cursor.execute("SELECT COUNT(*) FROM story_threads")
    total_threads = cursor.fetchone()[0]

    cursor.execute("SELECT COUNT(*) FROM story_threads WHERE status = 'ongoing'")
    ongoing_threads = cursor.fetchone()[0]

    cursor.execute("SELECT SUM(duration_minutes) FROM episodes")
    total_minutes = cursor.fetchone()[0] or 0

    cursor.execute("SELECT episode_type, COUNT(*) FROM episodes GROUP BY episode_type")
    by_type = dict(cursor.fetchall())

    conn.close()

    return {
        "total_episodes": total_episodes,
        "total_threads": total_threads,
        "ongoing_threads": ongoing_threads,
        "total_hours": round(total_minutes / 60, 1),
        "by_type": by_type,
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
