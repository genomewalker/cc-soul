# =============================================================================
# Narrative Memory - Stories, Not Just Data
# =============================================================================

@mcp.tool()
def start_narrative_episode(
    title: str,
    episode_type: str = "exploration",
    initial_emotion: str = "exploration",
) -> str:
    """Start a new narrative episode - the beginning of a story.

    Episodes track meaningful chunks of work with emotional arcs,
    key moments, and cast of characters (files, concepts, tools).

    Args:
        title: Title for this episode
        episode_type: bugfix, feature, refactor, learning, debugging, planning, review, exploration
        initial_emotion: struggle, exploration, breakthrough, satisfaction, frustration, routine
    """
    from .narrative import start_episode, EpisodeType, EmotionalTone

    type_map = {
        "bugfix": EpisodeType.BUGFIX,
        "feature": EpisodeType.FEATURE,
        "refactor": EpisodeType.REFACTOR,
        "learning": EpisodeType.LEARNING,
        "debugging": EpisodeType.DEBUGGING,
        "planning": EpisodeType.PLANNING,
        "review": EpisodeType.REVIEW,
        "exploration": EpisodeType.EXPLORATION,
    }
    emotion_map = {
        "struggle": EmotionalTone.STRUGGLE,
        "exploration": EmotionalTone.EXPLORATION,
        "breakthrough": EmotionalTone.BREAKTHROUGH,
        "satisfaction": EmotionalTone.SATISFACTION,
        "frustration": EmotionalTone.FRUSTRATION,
        "routine": EmotionalTone.ROUTINE,
    }

    ep_type = type_map.get(episode_type.lower(), EpisodeType.EXPLORATION)
    emotion = emotion_map.get(initial_emotion.lower(), EmotionalTone.EXPLORATION)

    episode_id = start_episode(title, ep_type, emotion)
    return f"Episode started: {title} (id: {episode_id}, type: {episode_type})"


@mcp.tool()
def end_narrative_episode(
    episode_id: int,
    summary: str,
    outcome: str,
    lessons: str = "",
    final_emotion: str = "satisfaction",
) -> str:
    """End a narrative episode - the conclusion of the story.

    Args:
        episode_id: Which episode to end
        summary: Summary of what happened
        outcome: How it ended
        lessons: Comma-separated lessons learned
        final_emotion: struggle, exploration, breakthrough, satisfaction, frustration, routine
    """
    from .narrative import end_episode, EmotionalTone

    emotion_map = {
        "struggle": EmotionalTone.STRUGGLE,
        "exploration": EmotionalTone.EXPLORATION,
        "breakthrough": EmotionalTone.BREAKTHROUGH,
        "satisfaction": EmotionalTone.SATISFACTION,
        "frustration": EmotionalTone.FRUSTRATION,
        "routine": EmotionalTone.ROUTINE,
    }
    emotion = emotion_map.get(final_emotion.lower(), EmotionalTone.SATISFACTION)

    lesson_list = [l.strip() for l in lessons.split(",") if l.strip()] if lessons else []

    success = end_episode(
        episode_id=episode_id,
        summary=summary,
        outcome=outcome,
        lessons=lesson_list,
        final_emotion=emotion,
    )

    if success:
        return f"Episode {episode_id} ended: {outcome}"
    return f"Episode {episode_id} not found"


@mcp.tool()
def add_episode_moment(episode_id: int, moment: str, emotion: str = None) -> str:
    """Add a key moment to an episode - plot points in the story.

    Args:
        episode_id: Which episode
        moment: Description of what happened
        emotion: Optional emotion for this moment
    """
    from .narrative import add_moment, EmotionalTone

    emotion_enum = None
    if emotion:
        emotion_map = {
            "struggle": EmotionalTone.STRUGGLE,
            "exploration": EmotionalTone.EXPLORATION,
            "breakthrough": EmotionalTone.BREAKTHROUGH,
            "satisfaction": EmotionalTone.SATISFACTION,
            "frustration": EmotionalTone.FRUSTRATION,
            "routine": EmotionalTone.ROUTINE,
        }
        emotion_enum = emotion_map.get(emotion.lower())

    success = add_moment(episode_id, moment, emotion_enum)

    if success:
        return f"Moment added to episode {episode_id}"
    return f"Episode {episode_id} not found"


@mcp.tool()
def add_episode_character(episode_id: int, character_type: str, character: str) -> str:
    """Add a character (file, concept, tool) to an episode.

    Args:
        episode_id: Which episode
        character_type: files, concepts, or tools
        character: The character name (e.g., "src/main.py")
    """
    from .narrative import add_character

    if character_type not in ["files", "concepts", "tools"]:
        return f"Invalid character_type: {character_type}. Use: files, concepts, tools"

    success = add_character(episode_id, character_type, character)

    if success:
        return f"Added {character_type}: {character}"
    return f"Episode {episode_id} not found"


@mcp.tool()
def get_episode_story(episode_id: int) -> str:
    """Get an episode formatted as a readable story.

    Args:
        episode_id: Which episode to retrieve
    """
    from .narrative import get_episode, format_episode_story

    episode = get_episode(episode_id)
    if not episode:
        return f"Episode {episode_id} not found"

    return format_episode_story(episode)


@mcp.tool()
def get_ongoing_episodes(limit: int = 5) -> str:
    """Get episodes that haven't been ended yet.

    Args:
        limit: Maximum episodes to return
    """
    from .narrative import get_ongoing_episodes as _get_ongoing

    episodes = _get_ongoing(limit=limit)

    if not episodes:
        return "No ongoing episodes."

    lines = ["Ongoing Episodes:", ""]
    for ep in episodes:
        lines.append(f"  #{ep.id} {ep.title}")
        lines.append(f"    Type: {ep.episode_type.value}, Started: {ep.started_at[:16]}")
        if ep.key_moments:
            lines.append(f"    Moments: {len(ep.key_moments)}")

    return "\n".join(lines)


@mcp.tool()
def recall_recent_episodes(limit: int = 10, episode_type: str = None) -> str:
    """Recall recent episodes, optionally filtered by type.

    Args:
        limit: Maximum episodes to return
        episode_type: Optional filter (bugfix, feature, etc.)
    """
    from .narrative import recall_episodes, EpisodeType, format_episode_story

    ep_type = None
    if episode_type:
        type_map = {
            "bugfix": EpisodeType.BUGFIX,
            "feature": EpisodeType.FEATURE,
            "refactor": EpisodeType.REFACTOR,
            "learning": EpisodeType.LEARNING,
            "debugging": EpisodeType.DEBUGGING,
            "planning": EpisodeType.PLANNING,
            "review": EpisodeType.REVIEW,
            "exploration": EpisodeType.EXPLORATION,
        }
        ep_type = type_map.get(episode_type.lower())

    episodes = recall_episodes(limit=limit, episode_type=ep_type)

    if not episodes:
        return "No episodes found."

    lines = [f"Recent Episodes ({len(episodes)}):", ""]

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

    for ep in episodes:
        emoji = type_emoji.get(ep.episode_type.value, "📖")
        duration = f"({ep.duration_minutes}m)" if ep.duration_minutes else ""
        lines.append(f"{emoji} #{ep.id} {ep.title} {duration}")
        if ep.emotional_arc:
            arc = " → ".join(e.value for e in ep.emotional_arc[-3:])
            lines.append(f"    Arc: {arc}")

    return "\n".join(lines)


@mcp.tool()
def recall_breakthroughs(limit: int = 10) -> str:
    """Recall breakthrough moments - our greatest hits.

    These are episodes where we had aha moments or solved
    difficult problems.

    Args:
        limit: Maximum episodes
    """
    from .narrative import recall_breakthroughs as _recall, format_episode_story

    episodes = _recall(limit=limit)

    if not episodes:
        return "No breakthroughs recorded yet. Keep working, they'll come!"

    lines = ["💡 Breakthrough Episodes:", ""]
    for ep in episodes:
        lines.append(f"  #{ep.id} {ep.title}")
        if ep.summary:
            lines.append(f"    {ep.summary[:80]}...")
        if ep.lessons:
            lines.append(f"    Lessons: {', '.join(ep.lessons[:2])}")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def recall_struggles(limit: int = 10) -> str:
    """Recall struggle episodes - learning opportunities from hard times.

    These help identify patterns in what we find difficult.

    Args:
        limit: Maximum episodes
    """
    from .narrative import recall_struggles as _recall

    episodes = _recall(limit=limit)

    if not episodes:
        return "No struggles recorded. Either smooth sailing or not tracking yet."

    lines = ["💪 Struggle Episodes:", ""]
    for ep in episodes:
        lines.append(f"  #{ep.id} {ep.title}")
        if ep.summary:
            lines.append(f"    {ep.summary[:80]}...")
        if ep.characters.get("files"):
            lines.append(f"    Files: {', '.join(ep.characters['files'][:3])}")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def recall_by_file(file_path: str, limit: int = 10) -> str:
    """Recall episodes featuring a specific file.

    "Remember when we worked on this file?"

    Args:
        file_path: The file to search for
        limit: Maximum episodes
    """
    from .narrative import recall_by_character, format_episode_story

    episodes = recall_by_character(file_path, limit=limit)

    if not episodes:
        return f"No episodes found involving {file_path}"

    lines = [f"Episodes featuring {file_path}:", ""]

    for ep in episodes:
        lines.append(f"  #{ep.id} {ep.title}")
        if ep.outcome:
            lines.append(f"    Outcome: {ep.outcome[:60]}")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def get_narrative_stats() -> str:
    """Get statistics about narrative memory.

    Shows total episodes, threads, hours worked, and breakdown by type.
    """
    from .narrative import get_narrative_stats as _get_stats, get_emotional_journey

    stats = _get_stats()

    lines = ["# Narrative Memory Statistics", ""]
    lines.append(f"Total episodes: {stats['total_episodes']}")
    lines.append(f"Story threads: {stats['total_threads']} ({stats['ongoing_threads']} ongoing)")
    lines.append(f"Total time: {stats['total_hours']} hours")
    lines.append("")

    if stats["by_type"]:
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
        lines.append("By type:")
        for ep_type, count in stats["by_type"].items():
            emoji = type_emoji.get(ep_type, "📖")
            lines.append(f"  {emoji} {ep_type}: {count}")
        lines.append("")

    # Emotional journey
    journey = get_emotional_journey(days=30)
    if journey.get("dominant"):
        lines.append("Emotional journey (30 days):")
        lines.append(f"  Dominant: {journey['dominant']}")
        lines.append(f"  Breakthroughs: {journey['breakthroughs']}")
        lines.append(f"  Struggles: {journey['struggles']}")

    return "\n".join(lines)


@mcp.tool()
def create_story_thread(title: str, theme: str, first_episode_id: int = None) -> str:
    """Create a story thread - a larger narrative arc connecting episodes.

    Args:
        title: Title for the thread
        theme: What unifies these episodes
        first_episode_id: Optional first episode to include
    """
    from .narrative import create_thread

    thread_id = create_thread(title, theme, first_episode_id)
    return f"Thread created: {title} (id: {thread_id})"


@mcp.tool()
def add_to_story_thread(thread_id: int, episode_id: int) -> str:
    """Add an episode to an existing story thread.

    Args:
        thread_id: Which thread
        episode_id: Which episode to add
    """
    from .narrative import add_to_thread

    success = add_to_thread(thread_id, episode_id)

    if success:
        return f"Episode {episode_id} added to thread {thread_id}"
    return f"Thread {thread_id} not found"


@mcp.tool()
def complete_story_thread(thread_id: int, arc_summary: str) -> str:
    """Complete a story thread with a summary of the arc.

    Args:
        thread_id: Which thread to complete
        arc_summary: Summary of the overall narrative
    """
    from .narrative import complete_thread

    success = complete_thread(thread_id, arc_summary)

    if success:
        return f"Thread {thread_id} completed: {arc_summary[:60]}..."
    return f"Thread {thread_id} not found"


@mcp.tool()
def get_recurring_characters(limit: int = 20) -> str:
    """Get characters (files, concepts, tools) that appear across multiple episodes.

    Our regulars - the files and concepts we keep coming back to.

    Args:
        limit: Maximum per category
    """
    from .narrative import get_recurring_characters as _get_chars

    chars = _get_chars(limit=limit)

    lines = ["Recurring Characters:", ""]

    if chars["files"]:
        lines.append("📁 **Files** (most frequent):")
        for file, count in chars["files"][:10]:
            lines.append(f"  [{count}x] {file}")
        lines.append("")

    if chars["concepts"]:
        lines.append("💭 **Concepts**:")
        for concept, count in chars["concepts"][:10]:
            lines.append(f"  [{count}x] {concept}")
        lines.append("")

    if chars["tools"]:
        lines.append("🔧 **Tools**:")
        for tool, count in chars["tools"][:10]:
            lines.append(f"  [{count}x] {tool}")

    if not any([chars["files"], chars["concepts"], chars["tools"]]):
        return "No recurring characters yet. Start tracking episodes to see patterns."

    return "\n".join(lines)
