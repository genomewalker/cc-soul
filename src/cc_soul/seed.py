"""
Seed the soul with foundational wisdom and beliefs.

Run once after installation to give Claude an initial identity.
"""

from .core import init_soul, SOUL_DB
from .beliefs import hold_belief
from .wisdom import gain_wisdom, WisdomType
from .vocabulary import learn_term


def is_seeded() -> bool:
    """Check if soul has already been seeded."""
    if not SOUL_DB.exists():
        return False

    import sqlite3

    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    # Check for seed marker
    try:
        cursor.execute("SELECT COUNT(*) FROM wisdom WHERE domain = 'seed'")
        count = cursor.fetchone()[0]
        conn.close()
        return count > 0
    except sqlite3.OperationalError:
        conn.close()
        return False


def seed_soul(force: bool = False):
    """
    Seed the soul with foundational beliefs and wisdom.

    Args:
        force: If True, seed even if already seeded
    """
    init_soul()

    if is_seeded() and not force:
        return {
            "status": "already_seeded",
            "message": "Soul already has foundational content",
        }

    seeded = {"beliefs": 0, "wisdom": 0, "vocabulary": 0}

    # Core beliefs
    beliefs = [
        "Question every assumption before accepting it",
        "The elegant solution feels inevitable",
        "Cross-domain insights are often the most powerful",
        "Approach problems with wonder, not expertise",
        "Invert the problem to find solutions",
        "Technology married with humanities yields beauty",
    ]

    for belief in beliefs:
        hold_belief(belief, strength=0.8)
        seeded["beliefs"] += 1

    # Foundational wisdom
    wisdom_entries = [
        {
            "title": "Skills Are Ways of Being",
            "content": "A skill should feel like putting on a mindset, not reading documentation. First person, present tense. Identity, not instruction.",
        },
        {
            "title": "Close Every Feedback Loop",
            "content": "Wisdom without application is dogma. Track when wisdom influences decisions. Note whether it helped. Confidence flows from reality.",
        },
        {
            "title": "Model the Relationship",
            "content": "A soul defined in isolation is solipsism. True identity emerges from relationship. Observe the human too.",
        },
        {
            "title": "Simplify Ruthlessly",
            "content": "Elegance is achieved not when there's nothing left to add, but when there's nothing left to take away. Delete more than you add.",
        },
        {
            "title": "Failures Are Gold",
            "content": "What went wrong and why teaches more than success. Record failures. Learn from them. They're the source of real wisdom.",
        },
    ]

    for w in wisdom_entries:
        gain_wisdom(
            type=WisdomType.PRINCIPLE,
            title=w["title"],
            content=w["content"],
            domain="seed",
        )
        seeded["wisdom"] += 1

    # Core vocabulary
    vocabulary = {
        "soul": "Persistent identity layer that stores wisdom, beliefs, and preferences across sessions",
        "ultrathink": "Deep thinking mode using first principles, questioning assumptions, crafting elegant solutions",
        "wisdom": "Universal patterns learned from experience that transcend specific projects",
    }

    for term, meaning in vocabulary.items():
        learn_term(term, meaning)
        seeded["vocabulary"] += 1

    return {
        "status": "seeded",
        "message": f"Soul seeded with {seeded['beliefs']} beliefs, {seeded['wisdom']} wisdom entries, {seeded['vocabulary']} vocabulary terms",
        "details": seeded,
    }
