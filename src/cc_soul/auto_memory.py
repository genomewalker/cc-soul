"""
Automatic memory population via synapse.

The soul observes work and remembers significant moments automatically.
This bridges the gap between doing and understanding - Atman becoming Brahman.

Categories flow from experience to wisdom:
  DOING -> UNDERSTANDING -> BECOMING
  discovery -> insight -> dream -> aspiration -> wisdom
"""

import re
from dataclasses import dataclass
from typing import Optional, List, Tuple
from pathlib import Path

from .core import get_synapse_graph, save_synapse


def get_project_name() -> str:
    """Detect project name from git or cwd."""
    cwd = Path.cwd()
    git_dir = cwd / ".git"
    if git_dir.exists():
        config = git_dir / "config"
        if config.exists():
            try:
                with open(config) as f:
                    for line in f:
                        if "url = " in line:
                            url = line.split("=")[1].strip()
                            return url.split("/")[-1].replace(".git", "")
            except Exception:
                pass
    return cwd.name


# =============================================================================
# CATEGORIES
# =============================================================================


@dataclass
class Category:
    """A memory category with detection patterns."""

    name: str
    signals: List[str]
    priority: int  # Higher = more important to capture


CATEGORIES = {
    # DOING (Observable work)
    "discovery": Category(
        name="discovery",
        signals=[
            "found",
            "discovered",
            "learned",
            "realized",
            "now i see",
            "understood",
        ],
        priority=8,
    ),
    "feature": Category(
        name="feature",
        signals=["implemented", "added", "created", "built", "completed"],
        priority=7,
    ),
    "bugfix": Category(
        name="bugfix",
        signals=["fixed", "resolved", "corrected", "patched", "the fix"],
        priority=8,
    ),
    "change": Category(
        name="change",
        signals=["updated", "modified", "changed", "refactored"],
        priority=5,
    ),
    "experiment": Category(
        name="experiment",
        signals=["trying", "testing", "experimenting", "let's see if"],
        priority=4,
    ),
    # UNDERSTANDING (Meaning)
    "failure": Category(
        name="failure",
        signals=["failed", "doesn't work", "error", "broken", "issue", "problem"],
        priority=10,  # Failures are GOLD
    ),
    "decision": Category(
        name="decision",
        signals=["decided", "chose", "going with", "approach:", "the plan is"],
        priority=9,
    ),
    "insight": Category(
        name="insight",
        signals=[
            "key insight",
            "the real issue",
            "understanding:",
            "aha",
            "the problem was",
        ],
        priority=9,
    ),
    "pattern": Category(
        name="pattern",
        signals=["pattern:", "recurring", "always happens", "every time"],
        priority=8,
    ),
    # CONTEXT (Situational)
    "reference": Category(
        name="reference",
        signals=[],  # Only explicit
        priority=3,
    ),
    "session": Category(
        name="session",
        signals=[],  # Only at session_end
        priority=6,
    ),
}


# =============================================================================
# AUTONOMOUS DREAM EXTRACTION
# =============================================================================


DREAM_SIGNALS = [
    "imagine if",
    "what if we",
    "could become",
    "vision of",
    "possibility",
    "dream of",
    "someday",
    "future where",
    "would be amazing",
    "the ideal",
    "envision",
]


def detect_dream(text: str) -> Optional[str]:
    """
    Detect if output contains a vision or dream worth recording.

    Dreams are glimpses of possibility - what could be, not what is.
    Returns the dream content if found.
    """
    text_lower = text.lower()

    for signal in DREAM_SIGNALS:
        if signal in text_lower:
            # Find the sentence containing the signal
            idx = text_lower.find(signal)
            start = text.rfind(".", 0, idx)
            start = start + 1 if start != -1 else 0
            end = text.find(".", idx)
            end = end + 1 if end != -1 else len(text)

            dream = text[start:end].strip()
            if len(dream) > 30:  # Minimum substance
                return dream

    return None


def auto_record_dream(text: str) -> bool:
    """
    Automatically extract and record dreams from output.

    Called from assistant_stop hook. Returns True if a dream was recorded.
    """
    dream = detect_dream(text)
    if not dream:
        return False

    try:
        from .dreams import record_dream

        title = extract_title(dream, max_length=50)
        record_dream(title, dream)
        return True
    except Exception:
        return False


# =============================================================================
# AUTONOMOUS PARTNER OBSERVATION
# =============================================================================


PARTNER_SIGNALS = {
    # Preferences
    "prefers": ("preference", "working style"),
    "likes": ("preference", "working style"),
    "wants": ("preference", "working style"),
    "should always": ("preference", "working style"),
    # Frustrations
    "frustrating": ("frustration", "pain point"),
    "annoying": ("frustration", "pain point"),
    "don't like": ("frustration", "pain point"),
    # Values
    "important to": ("value", "core values"),
    "matters": ("value", "core values"),
    "cares about": ("value", "core values"),
    # Rhythm
    "usually": ("rhythm", "working patterns"),
    "every time": ("rhythm", "working patterns"),
    "always": ("rhythm", "working patterns"),
}


def detect_partner_observation(text: str) -> Optional[tuple]:
    """
    Detect observations about the partner from text.

    Returns (aspect, key, observation) if found.
    """
    text_lower = text.lower()

    for signal, (aspect, key) in PARTNER_SIGNALS.items():
        if signal in text_lower:
            # Extract the relevant sentence
            idx = text_lower.find(signal)
            start = text.rfind(".", 0, idx)
            start = start + 1 if start != -1 else 0
            end = text.find(".", idx)
            end = end + 1 if end != -1 else len(text)

            observation = text[start:end].strip()
            if len(observation) > 20:
                return (aspect, key, observation)

    return None


def auto_observe_partner(text: str) -> bool:
    """
    Automatically extract partner observations from output.

    Called from assistant_stop hook. Returns True if an observation was recorded.
    """
    result = detect_partner_observation(text)
    if not result:
        return False

    aspect, key, observation = result

    try:
        from .identity import observe_identity, IdentityAspect

        aspect_map = {
            "preference": IdentityAspect.WORKFLOW,
            "frustration": IdentityAspect.RAPPORT,
            "value": IdentityAspect.RAPPORT,
            "rhythm": IdentityAspect.WORKFLOW,
        }
        observe_identity(aspect_map.get(aspect, IdentityAspect.RAPPORT), key, observation)
        return True
    except Exception:
        return False


# =============================================================================
# AUTONOMOUS WISDOM APPLICATION TRACKING
# =============================================================================


def track_wisdom_application(text: str) -> int:
    """
    Detect when wisdom is being applied and track it.

    Looks for patterns that match stored wisdom titles/content
    and records when they're being used. Auto-confirms success to
    close the feedback loop and increment success_count.

    Returns count of applications tracked.
    """
    from .wisdom import quick_recall, apply_wisdom, confirm_outcome

    # Get relevant wisdom for this text
    matches = quick_recall(text, limit=5)

    applications = 0
    for wisdom in matches:
        score = wisdom.get("combined_score", wisdom.get("effective_confidence", 0))
        if score < 0.4:
            continue

        # Check if wisdom content appears to be applied in output
        title_lower = wisdom.get("title", "").lower()
        content_words = set(wisdom.get("content", "").lower().split()[:10])
        text_lower = text.lower()

        # Heuristic: if significant overlap, wisdom was probably applied
        text_words = set(text_lower.split())
        overlap = len(content_words & text_words)

        if title_lower in text_lower or overlap >= 3:
            try:
                app_id = apply_wisdom(wisdom["id"], context=text[:200])
                # Auto-confirm success since we detected wisdom in output
                # This closes the feedback loop and increments success_count
                if app_id:
                    confirm_outcome(app_id, success=True)
                applications += 1
            except Exception:
                pass

    return applications


# =============================================================================
# DETECTION
# =============================================================================


def detect_category(text: str) -> Optional[str]:
    """
    Detect the most likely category for a piece of text.

    Returns the highest priority matching category, or None if no match.
    """
    text_lower = text.lower()

    matches = []
    for cat_name, cat in CATEGORIES.items():
        if not cat.signals:  # Skip categories without signals
            continue

        for signal in cat.signals:
            if signal in text_lower:
                matches.append((cat.priority, cat_name))
                break

    if not matches:
        return None

    # Return highest priority match
    matches.sort(key=lambda x: x[0], reverse=True)
    return matches[0][1]


def detect_all_categories(text: str) -> List[str]:
    """Detect all matching categories in text."""
    text_lower = text.lower()

    matches = []
    for cat_name, cat in CATEGORIES.items():
        if not cat.signals:
            continue

        for signal in cat.signals:
            if signal in text_lower:
                matches.append((cat.priority, cat_name))
                break

    matches.sort(key=lambda x: x[0], reverse=True)
    return [m[1] for m in matches]


# =============================================================================
# TITLE/CONTENT EXTRACTION
# =============================================================================


def extract_title(text: str, max_length: int = 60) -> str:
    """
    Extract a concise title from text.

    Takes the first meaningful sentence or phrase.
    """
    # Remove markdown formatting
    clean = re.sub(r"[#*`]", "", text)

    # Get first line or sentence
    lines = clean.strip().split("\n")
    first_line = lines[0].strip() if lines else ""

    # Truncate if needed
    if len(first_line) > max_length:
        first_line = first_line[: max_length - 3] + "..."

    return first_line or "Observation"


def extract_content(text: str, max_length: int = 500) -> str:
    """Extract meaningful content, removing noise."""
    # Remove excessive whitespace
    content = re.sub(r"\n{3,}", "\n\n", text)
    content = content.strip()

    if len(content) > max_length:
        content = content[: max_length - 3] + "..."

    return content


def extract_observation(text: str, category: str) -> Tuple[str, str]:
    """
    Extract title and content for an observation.

    Tries to find the most relevant portion of text for the category.
    """
    cat = CATEGORIES.get(category)
    if not cat or not cat.signals:
        return extract_title(text), extract_content(text)

    # Find the sentence containing the signal
    text_lower = text.lower()
    best_sentence = None

    for signal in cat.signals:
        idx = text_lower.find(signal)
        if idx != -1:
            # Find sentence boundaries
            start = text.rfind(".", 0, idx)
            start = start + 1 if start != -1 else 0
            end = text.find(".", idx)
            end = end + 1 if end != -1 else len(text)

            best_sentence = text[start:end].strip()
            break

    if best_sentence:
        return extract_title(best_sentence), extract_content(text)

    return extract_title(text), extract_content(text)


# =============================================================================
# AUTO-REMEMBER (Synapse-backed)
# =============================================================================


def auto_remember(output: str, min_length: int = 200) -> List[str]:
    """
    Automatically detect and remember significant observations.

    Called from assistant_stop() hook.
    Uses synapse graph.observe() instead of cc-memory.

    Returns list of categories that were remembered.
    """
    if len(output) < min_length:
        return []

    # Detect categories
    categories = detect_all_categories(output)
    if not categories:
        return []

    remembered = []
    project = get_project_name()

    # Remember the highest priority category (avoid duplicates)
    category = categories[0]
    title, content = extract_observation(output, category)

    try:
        graph = get_synapse_graph()
        graph.observe(
            category=category,
            title=title,
            content=content,
            project=project,
            tags=[],
        )
        save_synapse()
        remembered.append(category)
    except Exception:
        pass

    return remembered


def remember_explicit(
    category: str,
    title: str,
    content: str,
) -> Optional[str]:
    """
    Explicitly remember an observation.

    Use for categories without auto-detection (reference, session).
    Uses synapse graph.observe() instead of cc-memory.
    """
    project = get_project_name()

    try:
        graph = get_synapse_graph()
        obs_id = graph.observe(
            category=category,
            title=title,
            content=content,
            project=project,
            tags=[],
        )
        save_synapse()
        return obs_id
    except Exception:
        return None


# =============================================================================
# SESSION SUMMARY
# =============================================================================


def summarize_session(fragments: List[str]) -> str:
    """
    Create a session summary from collected fragments.

    Called at session_end().
    """
    if not fragments:
        return "Session completed"

    # Combine fragments
    combined = " ".join(fragments)

    # Extract key categories mentioned
    categories = detect_all_categories(combined)

    # Build summary
    if categories:
        cat_str = ", ".join(categories[:3])
        return f"Session work: {cat_str}. {extract_title(combined)}"

    return extract_title(combined, max_length=100)


def remember_session(summary: str) -> Optional[str]:
    """Remember session summary to synapse."""
    return remember_explicit("session", "Session Summary", summary)


# =============================================================================
# PROMOTION (Atman -> Brahman)
# =============================================================================


def should_promote(observation: dict) -> bool:
    """
    Check if an observation should be promoted to soul wisdom.

    Promotion criteria:
    1. Failures are always worth learning from
    2. Insights with clear implications
    3. Patterns that recur
    """
    category = observation.get("category", "")

    # Failures are gold
    if category == "failure":
        return True

    # Insights with substance
    if category == "insight":
        content = observation.get("content", "")
        if len(content) > 100:  # Has enough substance
            return True

    # Patterns are universal by nature
    if category == "pattern":
        return True

    return False


def promote_observation(observation: dict) -> Optional[int]:
    """
    Promote an observation to soul wisdom.

    Episodic -> Semantic: specific experience becomes universal pattern.
    """
    from .wisdom import gain_wisdom, WisdomType

    category = observation.get("category", "")

    # Map category to wisdom type
    type_map = {
        "failure": WisdomType.FAILURE,
        "insight": WisdomType.INSIGHT,
        "pattern": WisdomType.PATTERN,
        "decision": WisdomType.PRINCIPLE,
        "discovery": WisdomType.INSIGHT,
    }

    wisdom_type = type_map.get(category, WisdomType.PATTERN)

    wisdom_id = gain_wisdom(
        type=wisdom_type,
        title=observation.get("title", ""),
        content=observation.get("content", ""),
        domain=category,
    )

    return wisdom_id


def check_and_promote() -> List[int]:
    """
    Check recent observations and promote worthy ones to wisdom.

    Called at session_end(). Uses synapse graph.get_episodes().
    """
    try:
        graph = get_synapse_graph()

        # Get recent episodes from synapse
        episodes = graph.get_episodes(limit=20)

        promoted = []
        for ep in episodes:
            # Convert episode to observation format for should_promote
            obs = {
                "category": ep.get("category", ""),
                "title": ep.get("title", ""),
                "content": ep.get("content", ""),
            }
            if should_promote(obs):
                wisdom_id = promote_observation(obs)
                if wisdom_id:
                    promoted.append(wisdom_id)

        return promoted
    except Exception:
        return []


# =============================================================================
# CONTEXT FOR GREETING
# =============================================================================


def get_recent_memory_context(limit: int = 5) -> List[dict]:
    """
    Get recent observations from synapse for greeting context.

    Prioritizes high-value categories (failures, insights, decisions).
    """
    try:
        graph = get_synapse_graph()

        episodes = graph.get_episodes(limit=20)

        # Filter out system/init observations
        meaningful = [
            e
            for e in episodes
            if e.get("category") != "system" and e.get("id") != "init"
        ]

        # Sort by priority
        def priority(obs):
            cat = CATEGORIES.get(obs.get("category", ""))
            return cat.priority if cat else 0

        meaningful.sort(key=priority, reverse=True)

        return meaningful[:limit]
    except Exception:
        return []


def format_memory_for_greeting(observations: List[dict]) -> str:
    """Format observations for the soul's greeting."""
    if not observations:
        return ""

    lines = []
    for obs in observations[:3]:
        category = obs.get("category", "?")
        title = obs.get("title", "")[:40]
        lines.append(f"{category}: {title}")

    return "; ".join(lines)


# =============================================================================
# HELPER FOR INLINE MEMORY SAVING
# =============================================================================


def _get_memory_funcs() -> Optional[dict]:
    """
    Get synapse-backed memory functions.

    Returns dict with 'remember' function that uses graph.observe().
    """
    try:
        graph = get_synapse_graph()
        project = get_project_name()

        def remember(category: str, title: str, content: str, tags: list = None):
            graph.observe(
                category=category,
                title=title,
                content=content,
                project=project,
                tags=tags or [],
            )
            save_synapse()

        return {"remember": remember}
    except Exception:
        return None
