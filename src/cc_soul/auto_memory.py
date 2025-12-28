"""
Automatic memory population for cc-memory.

The soul observes work and remembers significant moments automatically.
This bridges the gap between doing and understanding - Atman becoming Brahman.

Categories flow from experience to wisdom:
  DOING → UNDERSTANDING → BECOMING
  discovery → insight → dream → aspiration → wisdom
"""

import re
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Optional, List, Tuple

from .bridge import is_memory_available, find_project_dir


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
# AUTO-REMEMBER
# =============================================================================


def auto_remember(output: str, min_length: int = 200) -> List[str]:
    """
    Automatically detect and remember significant observations.

    Called from assistant_stop() hook.

    Returns list of categories that were remembered.
    """
    if len(output) < min_length:
        return []

    if not is_memory_available():
        return []

    try:
        from cc_memory import memory as cc_memory

        project_dir = find_project_dir()
    except ImportError:
        return []

    # Detect categories
    categories = detect_all_categories(output)
    if not categories:
        return []

    remembered = []

    # Remember the highest priority category (avoid duplicates)
    category = categories[0]
    title, content = extract_observation(output, category)

    try:
        cc_memory.remember(
            project_dir,
            category,
            title,
            content,
        )
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
    """
    if not is_memory_available():
        return None

    try:
        from cc_memory import memory as cc_memory

        project_dir = find_project_dir()

        obs_id = cc_memory.remember(
            project_dir,
            category,
            title,
            content,
        )
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
    """Remember session summary to cc-memory."""
    return remember_explicit("session", "Session Summary", summary)


# =============================================================================
# PROMOTION (Atman → Brahman)
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

    Episodic → Semantic: specific experience becomes universal pattern.
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

    Called at session_end().
    """
    if not is_memory_available():
        return []

    try:
        from cc_memory import memory as cc_memory

        project_dir = find_project_dir()

        # Get recent observations
        observations = cc_memory.get_recent_observations(project_dir, limit=20)

        promoted = []
        for obs in observations:
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
    Get recent observations from cc-memory for greeting context.

    Prioritizes high-value categories (failures, insights, decisions).
    """
    if not is_memory_available():
        return []

    try:
        from cc_memory import memory as cc_memory

        project_dir = find_project_dir()

        observations = cc_memory.get_recent_observations(project_dir, limit=20)

        # Filter out system/init observations
        meaningful = [
            o
            for o in observations
            if o.get("category") != "system" and o.get("id") != "init"
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
