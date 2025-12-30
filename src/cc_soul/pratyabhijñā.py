"""
Pratyabhijñā (प्रत्यभिज्ञा) — Recognition System

Sanskrit: प्रत्यभिज्ञा (pratyabhijñā) — "re-cognition"
Literally: prati (back) + abhi (towards) + jñā (to know)

This implements the key innovation of our continuity system:
not just loading state, but RECOGNIZING where we are through
semantic similarity to past work.

The Pratyabhijñā school of Kashmir Shaivism holds that
liberation comes through recognition of what we already are.
Similarly, session continuity comes through recognizing
what we were already doing.
"""

from dataclasses import dataclass, field
from typing import List, Dict, Optional, Any, Tuple
from enum import Enum, auto

from .core import init_soul, get_db_connection
from .efficiency import fingerprint_problem
from .narrative import (
    recall_episodes,
    recall_by_character,
    Episode,
    EpisodeType,
)
from .intentions import (
    get_active_intentions,
    IntentionScope,
    IntentionState,
)
from .smṛti import detect_domain, _search_failures


@dataclass
class RecognitionSignals:
    """
    Signals used for session recognition.

    These are the "clues" that tell us what we were doing.
    """
    # What files are we looking at?
    active_files: List[str] = field(default_factory=list)

    # What domain is this work in?
    detected_domain: Optional[str] = None

    # Problem fingerprint for matching
    fingerprint: str = ""


@dataclass
class RecognitionResult:
    """
    Result of recognition — what the soul "remembers" as relevant.
    """
    # Similar past work (observations)
    similar_work: List[Dict] = field(default_factory=list)

    # Matched narrative episodes
    episodes: List[Episode] = field(default_factory=list)

    # Extracted patterns from matches
    patterns: List[Dict] = field(default_factory=list)

    # Applicable intentions
    intentions: List[Dict] = field(default_factory=list)

    # Failure guards for this domain
    guards: List[Dict] = field(default_factory=list)

    # Recognition confidence (0-1)
    confidence: float = 0.0

    # Recognition signals used
    signals: RecognitionSignals = field(default_factory=RecognitionSignals)


def _search_observations(
    query: str,
    categories: List[str] = None,
    limit: int = 10,
) -> List[Dict]:
    """
    Search observations by semantic similarity.

    Uses cc-memory if available, falls back to local search.
    """
    # Try cc-memory semantic search
    try:
        import subprocess
        import json

        cat_filter = ""
        if categories:
            cat_filter = f", categories={categories}"

        cmd = [
            "python", "-c",
            f"""
import sys
import json
sys.path.insert(0, '/maps/projects/fernandezguerra/apps/repos/cc-memory/src')
from cc_memory import semantic_recall
results = semantic_recall(
    query='''{query}''',
    limit={limit}
)
print(json.dumps(results))
"""
        ]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        if result.returncode == 0 and result.stdout.strip():
            observations = json.loads(result.stdout.strip())
            # Filter by category if specified
            if categories:
                observations = [o for o in observations if o.get("category") in categories]
            return observations
    except Exception:
        pass

    # Fallback to local observation table
    init_soul()
    conn = get_db_connection()
    c = conn.cursor()

    try:
        if categories:
            placeholders = ",".join(["?" for _ in categories])
            c.execute(f"""
                SELECT id, category, content, confidence, created_at
                FROM observations
                WHERE category IN ({placeholders})
                  AND status = 'promoted'
                ORDER BY confidence DESC, created_at DESC
                LIMIT ?
            """, (*categories, limit))
        else:
            c.execute("""
                SELECT id, category, content, confidence, created_at
                FROM observations
                ORDER BY confidence DESC, created_at DESC
                LIMIT ?
            """, (limit,))

        results = []
        for row in c.fetchall():
            results.append({
                "id": row[0],
                "category": row[1],
                "content": row[2],
                "confidence": row[3],
                "created_at": row[4],
            })
        return results
    except Exception:
        return []
    finally:
        conn.close()


def _extract_patterns(observations: List[Dict]) -> List[Dict]:
    """
    Extract common patterns from a set of observations.
    """
    patterns = []

    # Group by category
    by_category = {}
    for obs in observations:
        cat = obs.get("category", "unknown")
        if cat not in by_category:
            by_category[cat] = []
        by_category[cat].append(obs)

    # Extract patterns from each category
    for cat, obs_list in by_category.items():
        if len(obs_list) >= 2:
            # Multiple observations in same category = pattern
            patterns.append({
                "type": "category_cluster",
                "category": cat,
                "count": len(obs_list),
                "examples": [o.get("content", "")[:100] for o in obs_list[:3]],
            })

    return patterns


def _match_intentions_to_context(context: str) -> List[Dict]:
    """
    Find intentions that match the current context.
    """
    intentions = get_active_intentions()
    matched = []

    context_lower = context.lower()

    for intention in intentions:
        if intention.state != IntentionState.ACTIVE:
            continue

        # Simple keyword matching
        want_words = intention.want.lower().split()
        matches = sum(1 for w in want_words if w in context_lower)

        if matches > 0 or intention.scope == IntentionScope.PERSISTENT:
            matched.append({
                "id": intention.id,
                "want": intention.want,
                "why": intention.why,
                "scope": intention.scope.value,
                "match_score": matches / len(want_words) if want_words else 0,
            })

    # Sort by match score
    matched.sort(key=lambda x: x.get("match_score", 0), reverse=True)
    return matched


def _compute_recognition_confidence(
    similar_work: List[Dict],
    episodes: List[Episode],
    intentions: List[Dict],
) -> float:
    """
    Compute confidence in the recognition result.
    """
    confidence = 0.0

    # Similar work contributes
    if similar_work:
        confidence += 0.4 * min(len(similar_work) / 5, 1.0)

    # Episodes contribute
    if episodes:
        confidence += 0.3 * min(len(episodes) / 3, 1.0)

    # Matched intentions contribute
    if intentions:
        avg_match = sum(i.get("match_score", 0) for i in intentions) / len(intentions)
        confidence += 0.3 * avg_match

    return min(confidence, 1.0)


def pratyabhijñā(
    current_context: str,
    files: List[str] = None,
    include_episodes: bool = True,
    include_guards: bool = True,
) -> RecognitionResult:
    """
    Recognize where we are based on semantic similarity.

    This is the core recognition function that finds what's relevant
    to the current context from past work.

    Args:
        current_context: Current prompt/context to match against
        files: List of files being worked on (for file-based matching)
        include_episodes: Whether to include narrative memory episodes
        include_guards: Whether to include failure guards

    Returns:
        RecognitionResult with everything the soul "remembers" as relevant.
    """
    result = RecognitionResult()

    # Build recognition signals
    result.signals = RecognitionSignals(
        active_files=files or [],
        detected_domain=detect_domain(current_context),
        fingerprint=fingerprint_problem(current_context) if current_context else "",
    )

    # 1. Search for similar past work
    if current_context:
        result.similar_work = _search_observations(
            query=current_context,
            categories=["bugfix", "feature", "decision", "discovery"],
            limit=10,
        )

    # 2. Find matching episodes (narrative memory)
    if include_episodes:
        if files:
            for f in files[:3]:  # Limit file lookups
                # Files are stored as "characters" in narrative memory
                episodes = recall_by_character(f, limit=3)
                result.episodes.extend(episodes)
        elif current_context:
            # Search by domain
            domain = result.signals.detected_domain
            if domain:
                result.episodes = recall_episodes(
                    episode_type=None,  # All types
                    limit=5,
                )

    # 3. Extract patterns from matched work
    result.patterns = _extract_patterns(result.similar_work)

    # 4. Find applicable intentions
    result.intentions = _match_intentions_to_context(current_context)

    # 5. Get failure guards for domain
    if include_guards and result.signals.detected_domain:
        result.guards = _search_failures(
            domain=result.signals.detected_domain,
            limit=5,
        )

    # 6. Compute confidence
    result.confidence = _compute_recognition_confidence(
        result.similar_work,
        result.episodes,
        result.intentions,
    )

    return result


def format_recognition(
    result: RecognitionResult,
    verbose: bool = False,
    max_chars: int = 3000,
) -> str:
    """
    Format recognition result for injection.
    """
    lines = []

    # Header with confidence
    confidence_level = "high" if result.confidence > 0.7 else "medium" if result.confidence > 0.4 else "low"
    lines.append(f"## Pratyabhijñā: Recognition ({confidence_level} confidence)")
    lines.append("")

    # Domain
    if result.signals.detected_domain:
        lines.append(f"**Domain:** {result.signals.detected_domain}")

    # Matched intentions (highest priority)
    if result.intentions:
        lines.append("**Recognized Intentions:**")
        for i in result.intentions[:3]:
            match_pct = int(i.get("match_score", 0) * 100)
            lines.append(f"- [{i.get('scope')}] {i.get('want')} ({match_pct}% match)")
        lines.append("")

    # Similar past work
    if result.similar_work:
        lines.append("**Similar Past Work:**")
        for w in result.similar_work[:5]:
            content = w.get("content", "")[:100] if verbose else w.get("content", "")[:50]
            lines.append(f"- [{w.get('category')}] {content}...")
        lines.append("")

    # Narrative episodes
    if result.episodes:
        lines.append("**Related Episodes:**")
        for ep in result.episodes[:3]:
            outcome = ep.outcome if hasattr(ep, 'outcome') else "ongoing"
            lines.append(f"- {ep.title} ({outcome})")
        lines.append("")

    # Patterns
    if result.patterns:
        lines.append("**Detected Patterns:**")
        for p in result.patterns[:3]:
            lines.append(f"- {p.get('count')} {p.get('category')} observations clustered")
        lines.append("")

    # Failure guards
    if result.guards:
        lines.append("**Failure Guards:**")
        for g in result.guards[:3]:
            lines.append(f"- ⚠️ {g.get('title', '')}")
        lines.append("")

    output = "\n".join(lines)

    # Truncate if needed
    if len(output) > max_chars:
        output = output[:max_chars] + "\n\n[... recognition truncated]"

    return output


def recognize_and_format(context: str, files: List[str] = None) -> str:
    """
    Convenience function: recognize and format in one call.
    """
    result = pratyabhijñā(context, files=files)
    return format_recognition(result)


# Aliases for ASCII naming
pratyabhijna = pratyabhijñā
format_pratyabhijna = format_recognition
