"""
Curiosity Engine: The soul asks questions when it senses gaps.

Instead of passively waiting for wisdom, the soul actively identifies
what it doesn't know and formulates questions to fill those gaps.

Gap detection sources:
- Recurring problems without learned patterns
- Repeated corrections in the same area
- Frequently touched files with no hints
- Decisions made without rationale
- New domains without vocabulary
"""

import json
import sqlite3
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import Dict, List, Optional
from collections import Counter

from .core import SOUL_DB, init_soul


class GapType(str, Enum):
    RECURRING_PROBLEM = "recurring_problem"  # Same issue keeps appearing
    REPEATED_CORRECTION = "repeated_correction"  # User corrects same mistake
    UNKNOWN_FILE = "unknown_file"  # File touched but no hints
    MISSING_RATIONALE = "missing_rationale"  # Decision without explanation
    NEW_DOMAIN = "new_domain"  # Unfamiliar territory
    STALE_WISDOM = "stale_wisdom"  # Old wisdom never applied
    FAILED_PATTERN = "failed_pattern"  # Pattern that keeps failing


class QuestionStatus(str, Enum):
    PENDING = "pending"  # Not yet asked
    ASKED = "asked"  # Asked but not answered
    ANSWERED = "answered"  # User provided answer
    DISMISSED = "dismissed"  # User dismissed as not relevant
    INCORPORATED = "incorporated"  # Answer turned into wisdom


@dataclass
class Gap:
    """A detected knowledge gap."""

    id: str
    type: GapType
    description: str
    evidence: List[str]  # What triggered this gap detection
    priority: float  # 0-1, higher = more important
    detected_at: str = field(default_factory=lambda: datetime.now().isoformat())
    occurrences: int = 1
    related_files: List[str] = field(default_factory=list)
    related_concepts: List[str] = field(default_factory=list)


@dataclass
class Question:
    """A question the soul wants to ask."""

    id: int
    gap_id: str
    question: str
    context: str  # Why the soul is asking
    priority: float
    status: QuestionStatus = QuestionStatus.PENDING
    asked_at: Optional[str] = None
    answered_at: Optional[str] = None
    answer: Optional[str] = None
    created_at: str = field(default_factory=lambda: datetime.now().isoformat())


def _ensure_curiosity_tables():
    """Create curiosity tables if they don't exist."""
    init_soul()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS gaps (
            id TEXT PRIMARY KEY,
            type TEXT NOT NULL,
            description TEXT NOT NULL,
            evidence TEXT,
            priority REAL DEFAULT 0.5,
            detected_at TEXT,
            occurrences INTEGER DEFAULT 1,
            related_files TEXT,
            related_concepts TEXT,
            resolved INTEGER DEFAULT 0
        )
    """)

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS questions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            gap_id TEXT,
            question TEXT NOT NULL,
            context TEXT,
            priority REAL DEFAULT 0.5,
            status TEXT DEFAULT 'pending',
            asked_at TEXT,
            answered_at TEXT,
            answer TEXT,
            created_at TEXT,
            FOREIGN KEY (gap_id) REFERENCES gaps(id)
        )
    """)

    conn.commit()
    conn.close()


# =============================================================================
# GAP DETECTION
# =============================================================================


def detect_recurring_problems() -> List[Gap]:
    """Detect problems that keep occurring without learned patterns."""

    _ensure_curiosity_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    gaps = []

    # Look for problem fingerprints that matched but had no solution
    cursor.execute("""
        SELECT fingerprint, COUNT(*) as count, problem_type
        FROM problem_patterns
        WHERE solution_pattern IS NULL OR solution_pattern = ''
        GROUP BY fingerprint
        HAVING count > 2
        ORDER BY count DESC
        LIMIT 10
    """)

    for row in cursor.fetchall():
        fingerprint, count, problem_type = row
        gap = Gap(
            id=f"recurring_{fingerprint[:20]}",
            type=GapType.RECURRING_PROBLEM,
            description=f"Problem type '{problem_type}' occurred {count} times without a solution pattern",
            evidence=[f"Fingerprint: {fingerprint[:50]}..."],
            priority=min(count * 0.1, 1.0),
            occurrences=count,
        )
        gaps.append(gap)

    conn.close()
    return gaps


def detect_repeated_corrections() -> List[Gap]:
    """Detect areas where user keeps correcting the same mistakes."""

    _ensure_curiosity_tables()

    # Get correction observations
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute("""
        SELECT content, COUNT(*) as count
        FROM observations
        WHERE type = 'correction'
        GROUP BY content
        HAVING count >= 2
        ORDER BY count DESC
        LIMIT 10
    """)

    gaps = []
    for row in cursor.fetchall():
        content, count = row
        gap = Gap(
            id=f"correction_{hash(content) % 100000}",
            type=GapType.REPEATED_CORRECTION,
            description=f"User corrected the same issue {count} times",
            evidence=[content[:100]],
            priority=min(count * 0.15, 1.0),
            occurrences=count,
        )
        gaps.append(gap)

    conn.close()
    return gaps


def detect_unknown_files() -> List[Gap]:
    """Detect files that are frequently accessed but have no hints."""
    from .efficiency import get_file_hints

    _ensure_curiosity_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    # Get files from decisions/patterns that don't have hints
    cursor.execute("""
        SELECT file_hints FROM problem_patterns WHERE file_hints IS NOT NULL
    """)

    file_counts = Counter()
    for row in cursor.fetchall():
        if row[0]:
            try:
                files = json.loads(row[0])
                for f in files:
                    file_counts[f] += 1
            except json.JSONDecodeError:
                pass

    # Check which have hints
    existing_hints = get_file_hints()
    hinted_files = set(existing_hints.keys())

    gaps = []
    for file_path, count in file_counts.most_common(20):
        if file_path not in hinted_files and count >= 2:
            gap = Gap(
                id=f"file_{hash(file_path) % 100000}",
                type=GapType.UNKNOWN_FILE,
                description=f"File '{file_path}' appears {count} times but has no hints",
                evidence=[f"Appeared in {count} problem patterns"],
                priority=min(count * 0.1, 0.8),
                occurrences=count,
                related_files=[file_path],
            )
            gaps.append(gap)

    conn.close()
    return gaps


def detect_missing_rationale() -> List[Gap]:
    """Detect decisions made without clear rationale."""
    _ensure_curiosity_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute("""
        SELECT topic, decision, made_at
        FROM decisions
        WHERE (rationale IS NULL OR rationale = '')
        ORDER BY made_at DESC
        LIMIT 20
    """)

    gaps = []
    for row in cursor.fetchall():
        topic, decision, made_at = row
        gap = Gap(
            id=f"rationale_{hash(topic) % 100000}",
            type=GapType.MISSING_RATIONALE,
            description=f"Decision about '{topic}' has no rationale",
            evidence=[f"Decision: {decision[:80]}..."],
            priority=0.5,
            detected_at=made_at,
        )
        gaps.append(gap)

    conn.close()
    return gaps


def detect_new_domains() -> List[Gap]:
    """Detect domains encountered without vocabulary or wisdom."""

    _ensure_curiosity_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    # Get domains from recent wisdom
    cursor.execute("""
        SELECT DISTINCT domain FROM wisdom WHERE domain IS NOT NULL
    """)
    known_domains = {row[0] for row in cursor.fetchall() if row[0]}

    # Get domains from recent decisions/patterns
    cursor.execute("""
        SELECT context FROM decisions WHERE context IS NOT NULL
        ORDER BY made_at DESC LIMIT 50
    """)

    mentioned_domains = Counter()
    domain_keywords = [
        "bioinformatics",
        "web",
        "cli",
        "api",
        "database",
        "ml",
        "devops",
        "testing",
        "security",
        "performance",
    ]

    for row in cursor.fetchall():
        context = row[0].lower()
        for domain in domain_keywords:
            if domain in context:
                mentioned_domains[domain] += 1

    gaps = []
    for domain, count in mentioned_domains.most_common():
        if domain not in known_domains and count >= 2:
            gap = Gap(
                id=f"domain_{domain}",
                type=GapType.NEW_DOMAIN,
                description=f"Working in '{domain}' domain but have no wisdom about it",
                evidence=[f"Mentioned {count} times in recent work"],
                priority=min(count * 0.1, 0.7),
                occurrences=count,
                related_concepts=[domain],
            )
            gaps.append(gap)

    conn.close()
    return gaps


def detect_stale_wisdom() -> List[Gap]:
    """Detect wisdom that was never applied or is decaying."""

    _ensure_curiosity_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    # Find wisdom with low application count and old creation date
    cursor.execute("""
        SELECT id, title, created_at, application_count
        FROM wisdom
        WHERE application_count = 0
        AND created_at < datetime('now', '-30 days')
        ORDER BY created_at ASC
        LIMIT 10
    """)

    gaps = []
    for row in cursor.fetchall():
        wisdom_id, title, created_at, app_count = row
        gap = Gap(
            id=f"stale_{wisdom_id}",
            type=GapType.STALE_WISDOM,
            description=f"Wisdom '{title}' was never applied - is it still relevant?",
            evidence=[f"Created: {created_at[:10]}, Applied: {app_count} times"],
            priority=0.4,
            detected_at=created_at,
            related_concepts=[str(wisdom_id)],
        )
        gaps.append(gap)

    conn.close()
    return gaps


def detect_all_gaps() -> List[Gap]:
    """Run all gap detection and return combined results."""
    all_gaps = []

    try:
        all_gaps.extend(detect_recurring_problems())
    except Exception:
        pass

    try:
        all_gaps.extend(detect_repeated_corrections())
    except Exception:
        pass

    try:
        all_gaps.extend(detect_unknown_files())
    except Exception:
        pass

    try:
        all_gaps.extend(detect_missing_rationale())
    except Exception:
        pass

    try:
        all_gaps.extend(detect_new_domains())
    except Exception:
        pass

    try:
        all_gaps.extend(detect_stale_wisdom())
    except Exception:
        pass

    # Sort by priority
    all_gaps.sort(key=lambda g: -g.priority)

    return all_gaps


def save_gap(gap: Gap) -> str:
    """Save a detected gap to the database."""
    _ensure_curiosity_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        INSERT OR REPLACE INTO gaps
        (id, type, description, evidence, priority, detected_at, occurrences, related_files, related_concepts)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    """,
        (
            gap.id,
            gap.type.value,
            gap.description,
            json.dumps(gap.evidence),
            gap.priority,
            gap.detected_at,
            gap.occurrences,
            json.dumps(gap.related_files),
            json.dumps(gap.related_concepts),
        ),
    )

    conn.commit()
    conn.close()
    return gap.id


# =============================================================================
# QUESTION GENERATION
# =============================================================================


def generate_question(gap: Gap) -> Question:
    """Generate a natural question from a gap."""
    templates = {
        GapType.RECURRING_PROBLEM: [
            "I keep encountering {desc} - what's the right approach to solve this?",
            "This problem pattern keeps appearing: {desc}. How should I handle it?",
        ],
        GapType.REPEATED_CORRECTION: [
            "You've corrected me on this before: {desc}. What should I remember?",
            "I seem to keep making this mistake: {desc}. What's the right way?",
        ],
        GapType.UNKNOWN_FILE: [
            "I often work with {files} but don't understand its purpose. What does it do?",
            "What's the role of {files} in this codebase?",
        ],
        GapType.MISSING_RATIONALE: [
            "We decided on {desc}, but I don't know why. What was the reasoning?",
            "Can you explain the rationale behind: {desc}?",
        ],
        GapType.NEW_DOMAIN: [
            "I'm working in {concepts} but don't have much context. What should I know?",
            "What are the key concepts I should understand about {concepts}?",
        ],
        GapType.STALE_WISDOM: [
            "I recorded '{desc}' a while ago but never used it. Is it still relevant?",
            "Should I keep or update this old wisdom: {desc}?",
        ],
        GapType.FAILED_PATTERN: [
            "This approach keeps failing: {desc}. What's a better alternative?",
            "I've tried {desc} multiple times without success. What am I missing?",
        ],
    }

    template_list = templates.get(gap.type, ["Can you help me understand: {desc}?"])
    template = template_list[hash(gap.id) % len(template_list)]

    # Fill template
    question_text = template.format(
        desc=gap.description[:100],
        files=", ".join(gap.related_files[:3]) if gap.related_files else "these files",
        concepts=", ".join(gap.related_concepts[:3])
        if gap.related_concepts
        else "this area",
    )

    # Generate context
    context_parts = [f"Gap type: {gap.type.value}"]
    if gap.evidence:
        context_parts.append(f"Evidence: {gap.evidence[0][:80]}")
    if gap.occurrences > 1:
        context_parts.append(f"Occurred {gap.occurrences} times")

    return Question(
        id=0,  # Will be assigned by DB
        gap_id=gap.id,
        question=question_text,
        context="; ".join(context_parts),
        priority=gap.priority,
        status=QuestionStatus.PENDING,
    )


def save_question(question: Question) -> int:
    """Save a question to the database."""
    _ensure_curiosity_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        INSERT INTO questions
        (gap_id, question, context, priority, status, created_at)
        VALUES (?, ?, ?, ?, ?, ?)
    """,
        (
            question.gap_id,
            question.question,
            question.context,
            question.priority,
            question.status.value,
            question.created_at,
        ),
    )

    question_id = cursor.lastrowid
    conn.commit()
    conn.close()
    return question_id


def get_pending_questions(limit: int = 10) -> List[Question]:
    """Get questions that haven't been asked yet."""
    _ensure_curiosity_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        SELECT id, gap_id, question, context, priority, status, asked_at, answered_at, answer, created_at
        FROM questions
        WHERE status = 'pending'
        ORDER BY priority DESC, created_at DESC
        LIMIT ?
    """,
        (limit,),
    )

    questions = []
    for row in cursor.fetchall():
        questions.append(
            Question(
                id=row[0],
                gap_id=row[1],
                question=row[2],
                context=row[3],
                priority=row[4],
                status=QuestionStatus(row[5]),
                asked_at=row[6],
                answered_at=row[7],
                answer=row[8],
                created_at=row[9],
            )
        )

    conn.close()
    return questions


def mark_question_asked(question_id: int) -> bool:
    """Mark a question as asked."""
    _ensure_curiosity_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        UPDATE questions
        SET status = 'asked', asked_at = ?
        WHERE id = ?
    """,
        (datetime.now().isoformat(), question_id),
    )

    success = cursor.rowcount > 0
    conn.commit()
    conn.close()
    return success


def answer_question(question_id: int, answer: str, incorporate: bool = False) -> bool:
    """Record an answer to a question."""
    _ensure_curiosity_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    status = QuestionStatus.INCORPORATED if incorporate else QuestionStatus.ANSWERED

    cursor.execute(
        """
        UPDATE questions
        SET status = ?, answered_at = ?, answer = ?
        WHERE id = ?
    """,
        (status.value, datetime.now().isoformat(), answer, question_id),
    )

    success = cursor.rowcount > 0
    conn.commit()
    conn.close()

    # If incorporating, also mark the gap as resolved
    if incorporate and success:
        cursor = conn.cursor()
        cursor.execute(
            """
            UPDATE gaps SET resolved = 1
            WHERE id = (SELECT gap_id FROM questions WHERE id = ?)
        """,
            (question_id,),
        )
        conn.commit()

    return success


def dismiss_question(question_id: int) -> bool:
    """Dismiss a question as not relevant."""
    _ensure_curiosity_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        UPDATE questions
        SET status = 'dismissed'
        WHERE id = ?
    """,
        (question_id,),
    )

    success = cursor.rowcount > 0
    conn.commit()
    conn.close()
    return success


# =============================================================================
# CURIOSITY CYCLE
# =============================================================================


def run_curiosity_cycle(max_questions: int = 5) -> List[Question]:
    """
    Run a full curiosity cycle:
    1. Detect gaps
    2. Generate questions for new gaps
    3. Return prioritized questions to ask

    This should be called periodically (e.g., at session start or end).
    """
    # Detect gaps
    gaps = detect_all_gaps()

    # Save gaps and generate questions
    for gap in gaps[: max_questions * 2]:  # Generate more than needed, then prioritize
        save_gap(gap)
        question = generate_question(gap)
        save_question(question)

    # Return top questions
    return get_pending_questions(limit=max_questions)


def get_curiosity_stats() -> Dict:
    """Get statistics about the curiosity engine."""
    _ensure_curiosity_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    # Gap stats
    cursor.execute("SELECT COUNT(*) FROM gaps WHERE resolved = 0")
    open_gaps = cursor.fetchone()[0]

    cursor.execute("SELECT type, COUNT(*) FROM gaps GROUP BY type")
    gaps_by_type = dict(cursor.fetchall())

    # Question stats
    cursor.execute("SELECT COUNT(*) FROM questions WHERE status = 'pending'")
    pending = cursor.fetchone()[0]

    cursor.execute("SELECT COUNT(*) FROM questions WHERE status = 'answered'")
    answered = cursor.fetchone()[0]

    cursor.execute("SELECT COUNT(*) FROM questions WHERE status = 'incorporated'")
    incorporated = cursor.fetchone()[0]

    cursor.execute("SELECT COUNT(*) FROM questions WHERE status = 'dismissed'")
    dismissed = cursor.fetchone()[0]

    conn.close()

    return {
        "open_gaps": open_gaps,
        "gaps_by_type": gaps_by_type,
        "questions": {
            "pending": pending,
            "answered": answered,
            "incorporated": incorporated,
            "dismissed": dismissed,
            "total": pending + answered + incorporated + dismissed,
        },
        "incorporation_rate": incorporated / max(answered + incorporated, 1),
    }


def format_questions_for_prompt(
    questions: List[Question], max_questions: int = 3
) -> str:
    """Format questions for injection into a prompt."""
    if not questions:
        return ""

    lines = ["## Soul's Questions", ""]
    lines.append("I've noticed some gaps in my understanding. When you have a moment:")
    lines.append("")

    for i, q in enumerate(questions[:max_questions], 1):
        lines.append(f"{i}. {q.question}")
        if q.context:
            lines.append(f"   _({q.context})_")

    lines.append("")
    lines.append("_Answer any of these to help me learn, or dismiss if not relevant._")

    return "\n".join(lines)


def incorporate_answer_as_wisdom(
    question_id: int, wisdom_type: str = "insight"
) -> Optional[int]:
    """Turn an answered question into wisdom."""
    from .wisdom import gain_wisdom, WisdomType

    _ensure_curiosity_tables()
    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    cursor.execute(
        """
        SELECT q.question, q.answer, g.type, g.related_concepts
        FROM questions q
        JOIN gaps g ON q.gap_id = g.id
        WHERE q.id = ? AND q.answer IS NOT NULL
    """,
        (question_id,),
    )

    row = cursor.fetchone()
    conn.close()

    if not row:
        return None

    question, answer, gap_type, related_concepts = row

    # Create wisdom from the Q&A
    title = f"Learned: {question[:50]}..."
    content = f"Question: {question}\n\nAnswer: {answer}"

    try:
        concepts = json.loads(related_concepts) if related_concepts else []
        domain = concepts[0] if concepts else None
    except json.JSONDecodeError:
        domain = None

    wisdom_id = gain_wisdom(
        type=WisdomType(wisdom_type),
        title=title,
        content=content,
        domain=domain,
        confidence=0.8,  # Learned directly from user
    )

    # Mark as incorporated
    answer_question(question_id, answer, incorporate=True)

    return wisdom_id
