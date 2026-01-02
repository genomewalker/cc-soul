"""
Smṛti (स्मृति) — Intelligent Recall System

Sanskrit: स्मृति (smṛti) — "that which is remembered"

Unlike static ledger loading, Smṛti implements intelligent recall
that understands what's relevant through semantic similarity,
concept activation, and contextual pattern matching.

This is the core of our Upanishadic continuity architecture,
complementing Pratyabhijñā (recognition) and Antahkarana (assessment).
"""

from dataclasses import dataclass, field
from typing import List, Dict, Optional, Any
from enum import Enum, auto

from .core import init_soul, get_synapse_graph, save_synapse, SOUL_DIR
from .ledger import (
    SessionLedger,
    load_latest_ledger,
    format_ledger_for_context,
)
from .intentions import get_active_intentions, IntentionScope
from .efficiency import fingerprint_problem, recall_decisions


def _semantic_search_wisdom(query: str, domain: str = None, limit: int = 5) -> List[Dict]:
    """Semantic search wrapper - uses synapse vector search."""
    try:
        from .vectors import search_wisdom
        return search_wisdom(query, limit=limit, domain=domain)
    except ImportError:
        return _get_domain_wisdom(domain, limit) if domain else []


def _get_domain_wisdom(domain: str, limit: int = 5) -> List[Dict]:
    """Get wisdom entries filtered by domain (non-semantic)."""
    graph = get_synapse_graph()
    all_wisdom = graph.get_all_wisdom()

    results = []
    for w in all_wisdom:
        w_domain = w.get("domain", "")
        if w_domain == domain or w_domain == "universal":
            results.append({
                "id": w.get("id", ""),
                "type": "wisdom",
                "title": w.get("title", ""),
                "content": w.get("content", ""),
                "domain": w_domain,
                "confidence": w.get("confidence", 0.8),
            })
            if len(results) >= limit:
                break

    results.sort(key=lambda x: x.get("confidence", 0), reverse=True)
    return results[:limit]


class RecallMode(Enum):
    """Different modes trigger different recall strategies."""
    STARTUP = auto()      # Fresh session, light context
    RESUME = auto()       # After clear/compact, full restoration
    SEMANTIC = auto()     # During work, problem-relevant recall
    REACTIVE = auto()     # After error, failure pattern recall


@dataclass
class ContextBundle:
    """
    Bundle of recalled context for session injection.

    This is what Smṛti returns after intelligent recall.
    """
    wisdom: List[Dict] = field(default_factory=list)
    guards: List[Dict] = field(default_factory=list)  # Failure patterns
    continuation: Optional[Dict] = None
    intentions: List[Dict] = field(default_factory=list)
    activated_concepts: List[str] = field(default_factory=list)
    decisions: List[Dict] = field(default_factory=list)
    ledger: Optional[SessionLedger] = None

    # Metadata
    mode: RecallMode = RecallMode.STARTUP
    relevance_score: float = 0.0


def detect_domain(context: str) -> Optional[str]:
    """
    Detect the domain of work from context.

    Returns domain string for filtering wisdom/failures.
    """
    context_lower = context.lower()

    domain_keywords = {
        "python": ["python", ".py", "def ", "class ", "import ", "pip", "pytest"],
        "typescript": ["typescript", ".ts", "interface ", "type ", "npm", "tsx"],
        "rust": ["rust", ".rs", "fn ", "impl ", "cargo", "mut "],
        "architecture": ["architecture", "design", "pattern", "refactor", "module"],
        "testing": ["test", "pytest", "jest", "coverage", "mock", "assertion"],
        "debugging": ["bug", "error", "fix", "debug", "trace", "stacktrace"],
        "performance": ["performance", "optimize", "cache", "latency", "memory"],
    }

    for domain, keywords in domain_keywords.items():
        if any(kw in context_lower for kw in keywords):
            return domain

    return None


def _activate_concepts(context: str, limit: int = 10) -> List[str]:
    """
    Activate concepts from the concept graph based on context.

    Uses spreading activation to find related concepts.
    """
    try:
        from .graph import activate_from_prompt, KUZU_AVAILABLE
        if KUZU_AVAILABLE and activate_from_prompt:
            activated = activate_from_prompt(context, limit=limit)
            return [c.get("id", "") for c in activated if c.get("id")]
    except ImportError:
        pass

    # Fallback: extract key terms
    words = context.lower().split()
    key_terms = [w for w in words if len(w) > 4 and w.isalpha()]
    return key_terms[:limit]


def _search_failures(domain: Optional[str] = None, limit: int = 5) -> List[Dict]:
    """Search for failure patterns (guards) in the given domain."""
    graph = get_synapse_graph()
    all_failures = graph.get_all_failures()

    results = []
    for f in all_failures:
        f_domain = f.get("domain", "")
        if domain is None or f_domain == domain:
            results.append({
                "id": f.get("id", ""),
                "title": f.get("what_failed", f.get("title", "")),
                "content": f.get("why_it_failed", f.get("content", "")),
                "domain": f_domain,
                "confidence": f.get("confidence", 0.8),
                "created_at": f.get("created_at", ""),
            })

    results.sort(key=lambda x: (x.get("confidence", 0), x.get("created_at", "")), reverse=True)
    return results[:limit]


def smṛti_recall(
    context: str,
    mode: RecallMode = RecallMode.STARTUP,
    include_ledger: bool = True,
) -> ContextBundle:
    """
    Intelligent recall based on semantic relevance.

    Unlike static ledger loading, this:
    1. Activates concept graph from prompt
    2. Recalls semantically relevant wisdom
    3. Surfaces applicable patterns
    4. Includes failure guards for domain
    5. Loads continuation if resuming

    Args:
        context: Current context/prompt for relevance matching
        mode: Recall mode (startup, resume, semantic, reactive)
        include_ledger: Whether to include ledger state

    Returns:
        ContextBundle with all relevant recalled context
    """
    bundle = ContextBundle(mode=mode)

    # 1. Detect domain for filtering
    domain = detect_domain(context)

    # 2. Activate concepts from current context
    if context:
        bundle.activated_concepts = _activate_concepts(context)

    # 3. Semantic search on wisdom (unless minimal startup)
    if mode != RecallMode.STARTUP or context:
        try:
            wisdom_results = _semantic_search_wisdom(
                query=context if context else "general patterns",
                domain=domain,
                limit=5,
            )
            bundle.wisdom = wisdom_results
        except Exception:
            if domain:
                bundle.wisdom = _get_domain_wisdom(domain, limit=5)

    # 4. Find failure patterns (guards)
    if mode in (RecallMode.REACTIVE, RecallMode.RESUME):
        bundle.guards = _search_failures(domain=domain, limit=5)
    elif mode == RecallMode.SEMANTIC and domain:
        bundle.guards = _search_failures(domain=domain, limit=3)

    # 5. Load ledger and continuation if resuming
    if include_ledger and mode in (RecallMode.RESUME, RecallMode.STARTUP):
        ledger = load_latest_ledger()
        if ledger:
            bundle.ledger = ledger
            bundle.continuation = {
                "immediate_next": ledger.continuation.immediate_next,
                "deferred": ledger.continuation.deferred,
                "critical_context": ledger.continuation.critical_context,
            }

    # 6. Get active intentions that persist
    try:
        intentions = get_active_intentions()
        bundle.intentions = [
            {
                "id": i.id,
                "want": i.want,
                "why": i.why,
                "scope": i.scope.value,
            }
            for i in intentions
            if i.scope in (IntentionScope.PROJECT, IntentionScope.PERSISTENT)
        ]
    except Exception:
        pass

    # 7. Recall relevant decisions
    if context:
        bundle.decisions = recall_decisions(context, limit=3)

    # 8. Compute relevance score
    bundle.relevance_score = _compute_relevance_score(bundle)

    return bundle


def _compute_relevance_score(bundle: ContextBundle) -> float:
    """Compute how relevant the recalled context is."""
    score = 0.0

    # Wisdom contributes
    if bundle.wisdom:
        score += 0.3 * min(len(bundle.wisdom) / 5, 1.0)

    # Continuation is very relevant for resume
    if bundle.continuation and bundle.continuation.get("immediate_next"):
        score += 0.3

    # Intentions show ongoing work
    if bundle.intentions:
        score += 0.2 * min(len(bundle.intentions) / 3, 1.0)

    # Activated concepts show semantic matching
    if bundle.activated_concepts:
        score += 0.2 * min(len(bundle.activated_concepts) / 10, 1.0)

    return min(score, 1.0)


def format_smṛti_context(
    bundle: ContextBundle,
    max_tokens: int = 2000,
    verbose: bool = False,
) -> str:
    """
    Format recalled context for injection.

    Adapts to available budget and context mode.
    """
    lines = []

    # Header based on mode
    if bundle.mode == RecallMode.RESUME:
        lines.append("## Smṛti: Restored Context")
    elif bundle.mode == RecallMode.SEMANTIC:
        lines.append("## Smṛti: Relevant Context")
    elif bundle.mode == RecallMode.REACTIVE:
        lines.append("## Smṛti: Failure Guards")
    else:
        lines.append("## Smṛti: Session Context")
    lines.append("")

    # Continuation (highest priority)
    if bundle.continuation and bundle.continuation.get("immediate_next"):
        lines.append(f"**Continue:** {bundle.continuation['immediate_next']}")
        if verbose and bundle.continuation.get("critical_context"):
            lines.append(f"*Context:* {bundle.continuation['critical_context'][:200]}")
        lines.append("")

    # Active intentions
    if bundle.intentions:
        lines.append("**Active Intentions:**")
        for i in bundle.intentions[:3]:
            lines.append(f"- [{i.get('scope', '?')}] {i.get('want', '')}")
        lines.append("")

    # Wisdom
    if bundle.wisdom:
        lines.append("**Relevant Wisdom:**")
        for w in bundle.wisdom[:3]:
            title = w.get("title", "")
            if verbose:
                content = w.get("content", "")[:100]
                lines.append(f"- **{title}**: {content}...")
            else:
                lines.append(f"- {title}")
        lines.append("")

    # Failure guards (if reactive or resume mode)
    if bundle.guards:
        lines.append("**Failure Guards:**")
        for g in bundle.guards[:2]:
            lines.append(f"- {g.get('title', '')}")
        lines.append("")

    # Decisions
    if bundle.decisions:
        lines.append("**Recent Decisions:**")
        for d in bundle.decisions[:3]:
            lines.append(f"- {d.get('content', '')[:80]}")
        lines.append("")

    # Ledger summary if available
    if bundle.ledger:
        lines.append(f"**Coherence:** {bundle.ledger.soul_state.coherence:.0%}")
        if bundle.ledger.work_state.todos:
            pending = [t for t in bundle.ledger.work_state.todos if t.get("status") != "completed"]
            if pending:
                lines.append(f"**Pending todos:** {len(pending)}")
        lines.append("")

    result = "\n".join(lines)

    # Truncate if over budget
    if len(result) > max_tokens * 4:  # Rough char estimate
        result = result[:max_tokens * 4] + "\n\n[... truncated]"

    return result


# Aliases for Sanskrit naming consistency
smriti_recall = smṛti_recall
format_smriti_context = format_smṛti_context


def quick_recall(prompt: str) -> str:
    """
    Quick semantic recall for prompt injection.

    Returns formatted context string ready for injection.
    """
    bundle = smṛti_recall(prompt, mode=RecallMode.SEMANTIC, include_ledger=False)
    return format_smṛti_context(bundle, verbose=False)


def full_recall(prompt: str = "") -> ContextBundle:
    """
    Full recall for session resume.

    Returns complete ContextBundle with all state.
    """
    return smṛti_recall(prompt, mode=RecallMode.RESUME, include_ledger=True)
