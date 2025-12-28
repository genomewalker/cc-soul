"""
Token Efficiency - The soul as a compression layer.

This module helps Claude use fewer tokens while maintaining effectiveness by:
1. Problem fingerprinting - Recognize patterns, skip redundant exploration
2. File hints - Know where to look without reading everything
3. Decision memory - Recall past choices, don't re-debate
4. Compact context - Minimal tokens, maximum knowledge
"""

import hashlib
import json
from datetime import datetime
from typing import Dict, List, Optional, Tuple

from .core import get_db_connection, init_soul


def _ensure_efficiency_tables():
    """Ensure efficiency-related tables exist."""
    conn = get_db_connection()
    c = conn.cursor()

    # Problem fingerprints - recognize similar problems
    c.execute("""
        CREATE TABLE IF NOT EXISTS problem_fingerprints (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            fingerprint TEXT UNIQUE NOT NULL,
            problem_type TEXT,
            keywords TEXT,
            solution_pattern TEXT,
            file_hints TEXT,
            times_matched INTEGER DEFAULT 0,
            last_matched TEXT,
            created_at TEXT NOT NULL
        )
    """)

    # File hints - which files/lines matter for what
    c.execute("""
        CREATE TABLE IF NOT EXISTS file_hints (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_path TEXT NOT NULL,
            purpose TEXT,
            key_lines TEXT,
            key_functions TEXT,
            related_to TEXT,
            last_updated TEXT NOT NULL
        )
    """)

    # Decisions - architectural choices we've made
    c.execute("""
        CREATE TABLE IF NOT EXISTS decisions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            topic TEXT NOT NULL,
            decision TEXT NOT NULL,
            rationale TEXT,
            alternatives TEXT,
            context TEXT,
            made_at TEXT NOT NULL
        )
    """)

    conn.commit()
    conn.close()


# =============================================================================
# PROBLEM FINGERPRINTING - Recognize patterns, skip exploration
# =============================================================================


def fingerprint_problem(prompt: str) -> Optional[Dict]:
    """
    Check if this problem matches a known pattern.

    If matched, returns solution hints to skip exploration.
    Returns None if no match found.
    """
    _ensure_efficiency_tables()

    # Create fingerprint from keywords
    words = set(prompt.lower().split())
    # Remove common words
    stopwords = {
        "the",
        "a",
        "an",
        "is",
        "are",
        "was",
        "were",
        "be",
        "been",
        "being",
        "have",
        "has",
        "had",
        "do",
        "does",
        "did",
        "will",
        "would",
        "could",
        "should",
        "may",
        "might",
        "must",
        "shall",
        "can",
        "need",
        "dare",
        "ought",
        "used",
        "to",
        "of",
        "in",
        "for",
        "on",
        "with",
        "at",
        "by",
        "from",
        "as",
        "into",
        "through",
        "during",
        "before",
        "after",
        "above",
        "below",
        "between",
        "under",
        "again",
        "further",
        "then",
        "once",
        "here",
        "there",
        "when",
        "where",
        "why",
        "how",
        "all",
        "each",
        "few",
        "more",
        "most",
        "other",
        "some",
        "such",
        "no",
        "nor",
        "not",
        "only",
        "own",
        "same",
        "so",
        "than",
        "too",
        "very",
        "just",
        "and",
        "but",
        "if",
        "or",
        "because",
        "as",
        "until",
        "while",
        "although",
        "though",
        "after",
        "before",
        "when",
        "whenever",
        "where",
        "wherever",
        "whether",
        "which",
        "who",
        "whom",
        "whose",
        "what",
        "whatever",
        "i",
        "me",
        "my",
        "myself",
        "we",
        "our",
        "ours",
        "ourselves",
        "you",
        "your",
        "yours",
        "yourself",
        "yourselves",
        "he",
        "him",
        "his",
        "himself",
        "she",
        "her",
        "hers",
        "herself",
        "it",
        "its",
        "itself",
        "they",
        "them",
        "their",
        "theirs",
        "themselves",
        "this",
        "that",
        "these",
        "those",
        "am",
        "please",
        "help",
        "want",
        "like",
        "get",
        "make",
        "know",
        "think",
        "see",
        "come",
        "take",
        "find",
        "give",
        "tell",
        "work",
        "seem",
        "feel",
        "try",
        "leave",
        "call",
        "good",
        "new",
        "first",
        "last",
        "long",
        "great",
        "little",
        "own",
        "old",
        "right",
        "big",
        "high",
        "different",
        "small",
        "large",
        "next",
        "early",
        "young",
        "important",
        "few",
        "public",
        "bad",
        "same",
        "able",
    }

    keywords = sorted(words - stopwords)[:10]  # Top 10 meaningful words

    conn = get_db_connection()
    c = conn.cursor()

    # Look for matching fingerprints
    best_match = None
    best_score = 0

    c.execute(
        "SELECT id, keywords, problem_type, solution_pattern, file_hints FROM problem_fingerprints"
    )

    for row in c.fetchall():
        fp_id, fp_keywords, problem_type, solution_pattern, file_hints = row
        stored_keywords = set(json.loads(fp_keywords))

        # Calculate overlap
        overlap = len(set(keywords) & stored_keywords)
        score = overlap / max(len(keywords), len(stored_keywords), 1)

        if score > 0.5 and score > best_score:  # >50% keyword match
            best_match = {
                "id": fp_id,
                "problem_type": problem_type,
                "solution_pattern": solution_pattern,
                "file_hints": json.loads(file_hints) if file_hints else [],
                "match_score": score,
            }
            best_score = score

    if best_match:
        # Update match count
        c.execute(
            """
            UPDATE problem_fingerprints
            SET times_matched = times_matched + 1, last_matched = ?
            WHERE id = ?
        """,
            (datetime.now().isoformat(), best_match["id"]),
        )
        conn.commit()

    conn.close()
    return best_match


def learn_problem_pattern(
    prompt: str, problem_type: str, solution_pattern: str, file_hints: List[str] = None
):
    """
    Learn a new problem pattern for future matching.

    Args:
        prompt: The original problem description
        problem_type: Category (e.g., "performance", "bug", "feature")
        solution_pattern: Brief description of solution approach
        file_hints: Files that were relevant
    """
    _ensure_efficiency_tables()

    # Extract keywords
    words = set(prompt.lower().split())
    stopwords = {
        "the",
        "a",
        "an",
        "is",
        "are",
        "to",
        "of",
        "in",
        "for",
        "on",
        "with",
        "at",
        "by",
        "from",
        "i",
        "you",
        "we",
        "they",
        "it",
        "this",
        "that",
        "and",
        "or",
        "but",
        "if",
        "please",
        "help",
        "want",
        "need",
        "can",
        "how",
        "what",
        "why",
        "when",
        "where",
    }
    keywords = sorted(words - stopwords)[:15]

    # Create fingerprint hash
    fingerprint = hashlib.md5(" ".join(keywords).encode()).hexdigest()[:16]

    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        INSERT OR REPLACE INTO problem_fingerprints
        (fingerprint, problem_type, keywords, solution_pattern, file_hints, created_at)
        VALUES (?, ?, ?, ?, ?, ?)
    """,
        (
            fingerprint,
            problem_type,
            json.dumps(keywords),
            solution_pattern,
            json.dumps(file_hints or []),
            datetime.now().isoformat(),
        ),
    )

    conn.commit()
    conn.close()


# =============================================================================
# FILE HINTS - Know where to look without reading everything
# =============================================================================


def add_file_hint(
    file_path: str,
    purpose: str,
    key_lines: List[Tuple[int, int]] = None,
    key_functions: List[str] = None,
    related_to: List[str] = None,
):
    """
    Record hints about a file for future reference.

    Args:
        file_path: Path to the file
        purpose: One-line description of what the file does
        key_lines: List of (start, end) line ranges that matter
        key_functions: Names of important functions/classes
        related_to: Keywords this file is relevant for
    """
    _ensure_efficiency_tables()

    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        INSERT OR REPLACE INTO file_hints
        (file_path, purpose, key_lines, key_functions, related_to, last_updated)
        VALUES (?, ?, ?, ?, ?, ?)
    """,
        (
            file_path,
            purpose,
            json.dumps(key_lines or []),
            json.dumps(key_functions or []),
            json.dumps(related_to or []),
            datetime.now().isoformat(),
        ),
    )

    conn.commit()
    conn.close()


def get_file_hints(prompt: str, limit: int = 5) -> List[Dict]:
    """
    Get file hints relevant to a prompt.

    Returns files that might be relevant, with hints about what to focus on.
    """
    _ensure_efficiency_tables()

    prompt_lower = prompt.lower()
    prompt_words = set(prompt_lower.split())

    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        "SELECT file_path, purpose, key_lines, key_functions, related_to FROM file_hints"
    )

    scored = []
    for row in c.fetchall():
        file_path, purpose, key_lines, key_functions, related_to = row

        # Score by relevance
        score = 0
        related = json.loads(related_to) if related_to else []

        for keyword in related:
            if keyword.lower() in prompt_lower:
                score += 2

        # Check if file path mentioned
        if file_path.lower() in prompt_lower:
            score += 5

        # Check purpose overlap
        purpose_words = set(purpose.lower().split()) if purpose else set()
        score += len(prompt_words & purpose_words)

        if score > 0:
            scored.append(
                {
                    "file": file_path,
                    "purpose": purpose,
                    "key_lines": json.loads(key_lines) if key_lines else [],
                    "key_functions": json.loads(key_functions) if key_functions else [],
                    "relevance": score,
                }
            )

    conn.close()

    return sorted(scored, key=lambda x: -x["relevance"])[:limit]


# =============================================================================
# DECISION MEMORY - Don't re-debate past choices
# =============================================================================


def record_decision(
    topic: str,
    decision: str,
    rationale: str = "",
    alternatives: List[str] = None,
    context: str = "",
):
    """
    Record an architectural or design decision.

    Args:
        topic: What the decision is about
        decision: What was decided
        rationale: Why this choice was made
        alternatives: Other options considered
        context: Additional context
    """
    _ensure_efficiency_tables()

    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        INSERT INTO decisions (topic, decision, rationale, alternatives, context, made_at)
        VALUES (?, ?, ?, ?, ?, ?)
    """,
        (
            topic,
            decision,
            rationale,
            json.dumps(alternatives or []),
            context,
            datetime.now().isoformat(),
        ),
    )

    conn.commit()
    conn.close()


def recall_decisions(topic: str = None, limit: int = 10) -> List[Dict]:
    """
    Recall past decisions, optionally filtered by topic.
    """
    _ensure_efficiency_tables()

    conn = get_db_connection()
    c = conn.cursor()

    if topic:
        search = f"%{topic}%"
        c.execute(
            """
            SELECT topic, decision, rationale, alternatives, context, made_at
            FROM decisions
            WHERE topic LIKE ? OR decision LIKE ? OR context LIKE ?
            ORDER BY made_at DESC
            LIMIT ?
        """,
            (search, search, search, limit),
        )
    else:
        c.execute(
            """
            SELECT topic, decision, rationale, alternatives, context, made_at
            FROM decisions
            ORDER BY made_at DESC
            LIMIT ?
        """,
            (limit,),
        )

    results = []
    for row in c.fetchall():
        results.append(
            {
                "topic": row[0],
                "decision": row[1],
                "rationale": row[2],
                "alternatives": json.loads(row[3]) if row[3] else [],
                "context": row[4],
                "made_at": row[5],
            }
        )

    conn.close()
    return results


# =============================================================================
# COMPACT CONTEXT - Minimal tokens, maximum knowledge
# =============================================================================


def get_compact_context(project: str = None) -> str:
    """
    Get a token-efficient context summary.

    Instead of loading full soul context, returns a compressed version
    optimized for minimum tokens while preserving key information.
    """
    init_soul()
    conn = get_db_connection()
    c = conn.cursor()

    lines = []
    lines.append("# Soul (compact)")

    # Top 3 beliefs (single line each)
    c.execute("SELECT belief FROM beliefs ORDER BY strength DESC LIMIT 3")
    beliefs = [row[0] for row in c.fetchall()]
    if beliefs:
        lines.append(f"Beliefs: {' | '.join(b[:40] for b in beliefs)}")

    # Top 5 wisdom (title only)
    c.execute("""
        SELECT title FROM wisdom
        ORDER BY confidence * (0.95 * (julianday('now') - julianday(COALESCE(last_used, timestamp))) / 30) DESC
        LIMIT 5
    """)
    wisdom = [row[0] for row in c.fetchall()]
    if wisdom:
        lines.append(f"Wisdom: {', '.join(w[:30] for w in wisdom)}")

    # Vocabulary (term: meaning, compressed)
    c.execute("SELECT term, meaning FROM vocabulary ORDER BY usage_count DESC LIMIT 5")
    vocab = c.fetchall()
    if vocab:
        vocab_str = ", ".join(f"{t}={m[:20]}" for t, m in vocab)
        lines.append(f"Vocab: {vocab_str}")

    # Recent decisions
    c.execute("SELECT topic, decision FROM decisions ORDER BY made_at DESC LIMIT 3")
    decisions = c.fetchall()
    if decisions:
        dec_str = "; ".join(f"{t}: {d[:30]}" for t, d in decisions)
        lines.append(f"Decisions: {dec_str}")

    # File hints if project specified
    if project:
        c.execute(
            """
            SELECT file_path, purpose FROM file_hints
            WHERE related_to LIKE ?
            LIMIT 3
        """,
            (f"%{project}%",),
        )
        hints = c.fetchall()
        if hints:
            hints_str = ", ".join(f"{f.split('/')[-1]}: {p[:25]}" for f, p in hints)
            lines.append(f"Key files: {hints_str}")

    conn.close()

    return "\n".join(lines)


def format_efficiency_injection(prompt: str) -> str:
    """
    Generate an efficiency-optimized injection for a user prompt.

    This replaces verbose context loading with targeted hints.
    """
    output = []

    # Check problem fingerprint
    match = fingerprint_problem(prompt)
    if match and match["match_score"] > 0.6:
        output.append(f"[Pattern: {match['problem_type']}] {match['solution_pattern']}")
        if match["file_hints"]:
            output.append(f"Focus: {', '.join(match['file_hints'][:3])}")

    # Get file hints
    hints = get_file_hints(prompt, limit=3)
    if hints:
        for h in hints:
            if h["key_lines"]:
                ranges = ", ".join(f"L{s}-{e}" for s, e in h["key_lines"][:2])
                output.append(f"→ {h['file']}: {h['purpose'][:40]} ({ranges})")
            elif h["key_functions"]:
                funcs = ", ".join(h["key_functions"][:3])
                output.append(f"→ {h['file']}: {funcs}")

    # Check relevant decisions
    decisions = recall_decisions(prompt, limit=2)
    if decisions:
        for d in decisions:
            output.append(f"Decision [{d['topic']}]: {d['decision'][:50]}")

    if output:
        return "## 🎯 Efficiency Hints\n" + "\n".join(output)
    return ""


def get_token_stats() -> Dict:
    """
    Get statistics about potential token savings.
    """
    _ensure_efficiency_tables()

    conn = get_db_connection()
    c = conn.cursor()

    c.execute("SELECT COUNT(*), SUM(times_matched) FROM problem_fingerprints")
    fp_count, fp_matches = c.fetchone()

    c.execute("SELECT COUNT(*) FROM file_hints")
    hints_count = c.fetchone()[0]

    c.execute("SELECT COUNT(*) FROM decisions")
    decisions_count = c.fetchone()[0]

    conn.close()

    # Estimate savings (rough)
    # Each fingerprint match saves ~500 tokens of exploration
    # Each file hint saves ~200 tokens of full file read
    # Each decision recall saves ~100 tokens of re-debate
    estimated_savings = (
        (fp_matches or 0) * 500 + hints_count * 50 + decisions_count * 30
    )

    return {
        "problem_patterns": fp_count or 0,
        "pattern_matches": fp_matches or 0,
        "file_hints": hints_count,
        "decisions": decisions_count,
        "estimated_tokens_saved": estimated_savings,
    }
