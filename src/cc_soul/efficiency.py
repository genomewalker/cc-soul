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

from .core import get_synapse_graph, save_synapse, SOUL_DIR, init_soul


_GARBAGE_PATTERNS = [
    "Agent Context Notice",
    "You are a swarm",
    "You are a specialized",
    "[cc-soul] Swarm Agent",
    "## Swarm",
]


def _is_garbage_content(content: str) -> bool:
    """Check if content contains agent/swarm garbage markers."""
    if not content:
        return True
    for pattern in _GARBAGE_PATTERNS:
        if pattern in content:
            return True
    return False


def fingerprint_problem(prompt: str) -> Optional[Dict]:
    """
    Check if this problem matches a known pattern.

    If matched, returns solution hints to skip exploration.
    Returns None if no match found.
    """
    words = set(prompt.lower().split())
    stopwords = {
        "the", "a", "an", "is", "are", "was", "were", "be", "been", "being",
        "have", "has", "had", "do", "does", "did", "will", "would", "could",
        "should", "may", "might", "must", "shall", "can", "need", "dare",
        "ought", "used", "to", "of", "in", "for", "on", "with", "at", "by",
        "from", "as", "into", "through", "during", "before", "after", "above",
        "below", "between", "under", "again", "further", "then", "once",
        "here", "there", "when", "where", "why", "how", "all", "each", "few",
        "more", "most", "other", "some", "such", "no", "nor", "not", "only",
        "own", "same", "so", "than", "too", "very", "just", "and", "but",
        "if", "or", "because", "until", "while", "although", "though",
        "whenever", "wherever", "whether", "which", "who", "whom", "whose",
        "what", "whatever", "i", "me", "my", "myself", "we", "our", "ours",
        "ourselves", "you", "your", "yours", "yourself", "yourselves", "he",
        "him", "his", "himself", "she", "her", "hers", "herself", "it", "its",
        "itself", "they", "them", "their", "theirs", "themselves", "this",
        "that", "these", "those", "am", "please", "help", "want", "like",
        "get", "make", "know", "think", "see", "come", "take", "find", "give",
        "tell", "work", "seem", "feel", "try", "leave", "call", "good", "new",
        "first", "last", "long", "great", "little", "old", "right", "big",
        "high", "different", "small", "large", "next", "early", "young",
        "important", "public", "bad", "able",
    }

    keywords = sorted(words - stopwords)[:10]
    if not keywords:
        return None

    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="problem_fingerprint", limit=100)

    best_match = None
    best_score = 0

    for ep in episodes:
        metadata = ep.get("metadata", {})
        if isinstance(metadata, str):
            try:
                metadata = json.loads(metadata)
            except json.JSONDecodeError:
                metadata = {}

        stored_keywords = set(metadata.get("keywords", []))
        if not stored_keywords:
            continue

        overlap = len(set(keywords) & stored_keywords)
        score = overlap / max(len(keywords), len(stored_keywords), 1)

        if score > 0.5 and score > best_score:
            best_match = {
                "id": ep.get("id", ""),
                "problem_type": metadata.get("problem_type", ""),
                "solution_pattern": ep.get("content", ""),
                "file_hints": metadata.get("file_hints", []),
                "match_score": score,
            }
            best_score = score

    if best_match:
        graph.strengthen(best_match["id"])
        save_synapse()

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
    if _is_garbage_content(prompt) or _is_garbage_content(solution_pattern):
        return

    words = set(prompt.lower().split())
    stopwords = {
        "the", "a", "an", "is", "are", "to", "of", "in", "for", "on", "with",
        "at", "by", "from", "i", "you", "we", "they", "it", "this", "that",
        "and", "or", "but", "if", "please", "help", "want", "need", "can",
        "how", "what", "why", "when", "where",
    }
    keywords = sorted(words - stopwords)[:15]

    fingerprint = hashlib.md5(" ".join(keywords).encode()).hexdigest()[:16]

    graph = get_synapse_graph()
    graph.observe(
        category="problem_fingerprint",
        title=f"Pattern: {problem_type} ({fingerprint})",
        content=solution_pattern,
        tags=[problem_type, fingerprint] + (file_hints or [])[:5],
    )
    save_synapse()


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
    graph = get_synapse_graph()

    content_parts = [f"Purpose: {purpose}"]
    if key_lines:
        content_parts.append(f"Key lines: {json.dumps(key_lines)}")
    if key_functions:
        content_parts.append(f"Functions: {', '.join(key_functions)}")

    tags = ["file_hint", file_path.split("/")[-1]]
    if related_to:
        tags.extend(related_to[:5])

    graph.observe(
        category="file_hint",
        title=f"File: {file_path}",
        content="\n".join(content_parts),
        tags=tags,
    )
    save_synapse()


def get_file_hints(prompt: str, limit: int = 5) -> List[Dict]:
    """
    Get file hints relevant to a prompt.

    Returns files that might be relevant, with hints about what to focus on.
    """
    graph = get_synapse_graph()
    results = graph.search(prompt, limit=limit * 2, threshold=0.3)

    scored = []
    for concept, score in results:
        if "file_hint" not in concept.metadata.get("tags", []):
            continue

        content = concept.content
        key_lines = []
        key_functions = []

        for line in content.split("\n"):
            if line.startswith("Key lines:"):
                try:
                    key_lines = json.loads(line.replace("Key lines:", "").strip())
                except json.JSONDecodeError:
                    pass
            elif line.startswith("Functions:"):
                key_functions = [
                    f.strip() for f in line.replace("Functions:", "").split(",")
                ]

        purpose = ""
        if content.startswith("Purpose:"):
            purpose = content.split("\n")[0].replace("Purpose:", "").strip()

        file_path = concept.title.replace("File: ", "")
        scored.append({
            "file": file_path,
            "purpose": purpose,
            "key_lines": key_lines,
            "key_functions": key_functions,
            "relevance": score,
        })

    return sorted(scored, key=lambda x: -x["relevance"])[:limit]


def get_all_file_hints() -> Dict[str, str]:
    """
    Get all file hints as a dict of file_path -> purpose.

    Used by curiosity engine to know which files have hints.
    """
    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="file_hint", limit=200)

    result = {}
    for ep in episodes:
        title = ep.get("title", "")
        content = ep.get("content", "")

        if title.startswith("File: "):
            file_path = title.replace("File: ", "")
            purpose = ""
            if content.startswith("Purpose:"):
                purpose = content.split("\n")[0].replace("Purpose:", "").strip()
            result[file_path] = purpose

    return result


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
    graph = get_synapse_graph()

    content_parts = [f"Decision: {decision}"]
    if rationale:
        content_parts.append(f"Rationale: {rationale}")
    if alternatives:
        content_parts.append(f"Alternatives: {', '.join(alternatives)}")
    if context:
        content_parts.append(f"Context: {context}")

    graph.observe(
        category="decision",
        title=f"Decision: {topic}",
        content="\n".join(content_parts),
        tags=["decision", topic.lower().replace(" ", "_")],
    )
    save_synapse()


def recall_decisions(topic: str = None, limit: int = 10) -> List[Dict]:
    """
    Recall past decisions, optionally filtered by topic.
    """
    graph = get_synapse_graph()

    if topic:
        results = graph.search(topic, limit=limit * 2, threshold=0.3)
        decisions = []
        for concept, score in results:
            if "decision" not in concept.metadata.get("tags", []):
                continue

            content = concept.content
            decision_text = ""
            rationale = ""
            alternatives = []
            context_text = ""

            for line in content.split("\n"):
                if line.startswith("Decision:"):
                    decision_text = line.replace("Decision:", "").strip()
                elif line.startswith("Rationale:"):
                    rationale = line.replace("Rationale:", "").strip()
                elif line.startswith("Alternatives:"):
                    alternatives = [
                        a.strip() for a in line.replace("Alternatives:", "").split(",")
                    ]
                elif line.startswith("Context:"):
                    context_text = line.replace("Context:", "").strip()

            topic_title = concept.title.replace("Decision: ", "")
            decisions.append({
                "topic": topic_title,
                "decision": decision_text,
                "rationale": rationale,
                "alternatives": alternatives,
                "context": context_text,
                "made_at": concept.metadata.get("timestamp", ""),
            })

        return decisions[:limit]

    episodes = graph.get_episodes(category="decision", limit=limit)
    results = []
    for ep in episodes:
        content = ep.get("content", "")
        decision_text = ""
        rationale = ""
        alternatives = []
        context_text = ""

        for line in content.split("\n"):
            if line.startswith("Decision:"):
                decision_text = line.replace("Decision:", "").strip()
            elif line.startswith("Rationale:"):
                rationale = line.replace("Rationale:", "").strip()
            elif line.startswith("Alternatives:"):
                alternatives = [
                    a.strip() for a in line.replace("Alternatives:", "").split(",")
                ]
            elif line.startswith("Context:"):
                context_text = line.replace("Context:", "").strip()

        topic_title = ep.get("title", "").replace("Decision: ", "")
        results.append({
            "topic": topic_title,
            "decision": decision_text,
            "rationale": rationale,
            "alternatives": alternatives,
            "context": context_text,
            "made_at": ep.get("timestamp", ""),
        })

    return results


def get_compact_context(project: str = None) -> str:
    """
    Get a token-efficient context summary.

    Instead of loading full soul context, returns a compressed version
    optimized for minimum tokens while preserving key information.
    """
    init_soul()
    graph = get_synapse_graph()

    lines = []
    lines.append("# Soul (compact)")

    beliefs = graph.get_all_beliefs()[:3]
    if beliefs:
        belief_strs = [b.get("statement", "")[:40] for b in beliefs]
        lines.append(f"Beliefs: {' | '.join(belief_strs)}")

    wisdom = graph.get_all_wisdom()[:5]
    if wisdom:
        wisdom_strs = [w.get("title", "")[:30] for w in wisdom]
        lines.append(f"Wisdom: {', '.join(wisdom_strs)}")

    decisions = graph.get_episodes(category="decision", limit=3)
    if decisions:
        dec_parts = []
        for d in decisions:
            topic = d.get("title", "").replace("Decision: ", "")[:20]
            content = d.get("content", "")
            decision_text = ""
            for line in content.split("\n"):
                if line.startswith("Decision:"):
                    decision_text = line.replace("Decision:", "").strip()[:30]
                    break
            if topic and decision_text:
                dec_parts.append(f"{topic}: {decision_text}")
        if dec_parts:
            lines.append(f"Decisions: {'; '.join(dec_parts)}")

    if project:
        file_hints = graph.get_episodes(category="file_hint", limit=20)
        relevant = []
        for fh in file_hints:
            tags = fh.get("tags", [])
            if project.lower() in " ".join(tags).lower():
                title = fh.get("title", "").replace("File: ", "")
                fname = title.split("/")[-1]
                content = fh.get("content", "")
                purpose = ""
                if content.startswith("Purpose:"):
                    purpose = content.split("\n")[0].replace("Purpose:", "").strip()[:25]
                relevant.append(f"{fname}: {purpose}")
                if len(relevant) >= 3:
                    break
        if relevant:
            lines.append(f"Key files: {', '.join(relevant)}")

    return "\n".join(lines)


def format_efficiency_injection(prompt: str) -> str:
    """
    Generate an efficiency-optimized injection for a user prompt.

    This replaces verbose context loading with targeted hints.
    """
    output = []

    match = fingerprint_problem(prompt)
    if match and match["match_score"] > 0.6:
        output.append(f"[Pattern: {match['problem_type']}] {match['solution_pattern']}")
        if match["file_hints"]:
            output.append(f"Focus: {', '.join(match['file_hints'][:3])}")

    hints = get_file_hints(prompt, limit=3)
    if hints:
        for h in hints:
            if h["key_lines"]:
                ranges = ", ".join(f"L{s}-{e}" for s, e in h["key_lines"][:2])
                output.append(f"-> {h['file']}: {h['purpose'][:40]} ({ranges})")
            elif h["key_functions"]:
                funcs = ", ".join(h["key_functions"][:3])
                output.append(f"-> {h['file']}: {funcs}")

    decisions = recall_decisions(prompt, limit=2)
    if decisions:
        for d in decisions:
            output.append(f"Decision [{d['topic']}]: {d['decision'][:50]}")

    if output:
        return "## Efficiency Hints\n" + "\n".join(output)
    return ""


def get_token_stats() -> Dict:
    """
    Get statistics about potential token savings.
    """
    graph = get_synapse_graph()

    fingerprints = graph.get_episodes(category="problem_fingerprint", limit=500)
    fp_count = len(fingerprints)

    file_hints = graph.get_episodes(category="file_hint", limit=500)
    hints_count = len(file_hints)

    decisions = graph.get_episodes(category="decision", limit=500)
    decisions_count = len(decisions)

    estimated_savings = fp_count * 500 + hints_count * 50 + decisions_count * 30

    return {
        "problem_patterns": fp_count,
        "pattern_matches": fp_count,
        "file_hints": hints_count,
        "decisions": decisions_count,
        "estimated_tokens_saved": estimated_savings,
    }
