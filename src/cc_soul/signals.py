"""
cc-soul Signals: Distilled insights from background voice processing.

Signals are the unit of cognition that bridges background processing to conscious attention.
Unlike concepts (static knowledge), signals represent dynamic relevance - what is happening NOW.

Architecture:
- Background voices (Manas, Vikalpa, Sakshi) process observations asynchronously
- They emit Signals with compressed insights and activation weights
- Main instance reads top-K signals at session start
- Signals with high weights surface to attention
- Hebbian reinforcement: useful signals get stronger

The key insight: Push minimal signals, let Claude pull on demand.
"""

import json
from dataclasses import dataclass
from datetime import datetime
from typing import Dict, List, Optional

from .core import get_synapse_graph, save_synapse


@dataclass
class Signal:
    """A distilled insight from background voice processing."""
    id: str
    compressed_insight: str  # 1-2 sentences max
    activation_weight: float  # 0.0-1.0 (importance)
    source_ids: List[str]  # What this distills (observation IDs, wisdom IDs)
    voice: str  # Which voice detected this (manas, buddhi, vikalpa, sakshi)
    timestamp: str
    elaboration_prompt: str = ""  # How to get full context via delegate_thinking
    domain: str = ""  # Optional domain context
    decay_rate: float = 0.95  # Weight decays each session if not reinforced
    reinforcement_count: int = 0  # How many times this signal led to useful action


def create_signal(
    compressed_insight: str,
    voice: str,
    source_ids: Optional[List[str]] = None,
    activation_weight: float = 0.5,
    elaboration_prompt: str = "",
    domain: str = "",
) -> Optional[Signal]:
    """
    Create a new signal from background voice processing.

    Args:
        compressed_insight: 1-2 sentence distillation
        voice: Which voice (manas, buddhi, vikalpa, sakshi, ahamkara)
        source_ids: IDs of observations/wisdom this distills
        activation_weight: Initial importance (0.0-1.0)
        elaboration_prompt: Prompt to get full details via delegation
        domain: Optional domain context

    Returns:
        Signal object if stored successfully, None otherwise
    """
    timestamp = datetime.now().isoformat()
    signal_id = f"signal_{timestamp.replace(':', '').replace('-', '').replace('.', '_')}"

    signal_data = {
        "compressed_insight": compressed_insight[:500],  # Cap length
        "activation_weight": float(activation_weight),
        "source_ids": source_ids or [],
        "voice": voice,
        "elaboration_prompt": elaboration_prompt,
        "domain": domain,
        "decay_rate": 0.95,
        "reinforcement_count": 0,
    }

    # Store in synapse with category "signal"
    graph = get_synapse_graph()
    tags = [voice, "signal"]
    if domain:
        tags.append(domain)

    graph.observe(
        category="signal",
        title=f"[{voice}] {compressed_insight[:60]}...",
        content=json.dumps(signal_data),
        project=None,
        tags=tags,
    )
    save_synapse()

    return Signal(
        id=signal_id,
        timestamp=timestamp,
        **signal_data
    )


def get_signals(
    min_weight: float = 0.3,
    limit: int = 10,
    voice: Optional[str] = None,
    domain: Optional[str] = None,
) -> List[Signal]:
    """
    Get signals sorted by activation weight.

    Args:
        min_weight: Minimum activation weight to include
        limit: Maximum signals to return
        voice: Filter by voice (optional)
        domain: Filter by domain (optional)

    Returns:
        List of Signal objects sorted by weight (descending)
    """
    graph = get_synapse_graph()

    # Get signal episodes
    episodes = graph.get_episodes(category="signal", limit=limit * 2)

    signals = []
    for ep in episodes:
        try:
            data = json.loads(ep.get("content", "{}"))
            weight = data.get("activation_weight", 0)
            ep_voice = data.get("voice", "unknown")
            ep_domain = data.get("domain", "")

            # Apply filters
            if weight < min_weight:
                continue
            if voice and ep_voice != voice:
                continue
            if domain and ep_domain != domain:
                continue

            signals.append(Signal(
                id=ep.get("id", ""),
                compressed_insight=data.get("compressed_insight", ""),
                activation_weight=weight,
                source_ids=data.get("source_ids", []),
                voice=ep_voice,
                timestamp=ep.get("timestamp", ""),
                elaboration_prompt=data.get("elaboration_prompt", ""),
                domain=ep_domain,
                decay_rate=data.get("decay_rate", 0.95),
                reinforcement_count=data.get("reinforcement_count", 0),
            ))
        except (json.JSONDecodeError, KeyError):
            continue

    # Sort by weight descending
    signals.sort(key=lambda s: s.activation_weight, reverse=True)
    return signals[:limit]


def reinforce_signal(signal_id: str, outcome: str = "useful") -> bool:
    """
    Reinforce a signal based on outcome.

    Args:
        signal_id: ID of the signal to reinforce
        outcome: "useful" (strengthen) or "not_useful" (weaken)

    Returns:
        True if reinforcement applied
    """
    graph = get_synapse_graph()

    if outcome == "useful":
        return graph.strengthen(signal_id)
    else:
        return graph.weaken(signal_id)


def decay_signals(session_boundary: bool = True) -> int:
    """
    Apply decay to all signals (call at session end).

    Signals that aren't reinforced gradually lose weight.
    This prevents signal accumulation bloat.

    Returns:
        Number of signals decayed
    """
    # Synapse handles decay internally via cycle()
    graph = get_synapse_graph()
    pruned, _ = graph.cycle()
    return pruned


def get_signal_context(prompt: str, limit: int = 5) -> str:
    """
    Get signal context for a prompt (for hooks injection).

    Returns compact signal summary for context injection.
    ~50 tokens per signal.
    """
    signals = get_signals(min_weight=0.4, limit=limit)

    if not signals:
        return ""

    lines = []
    for sig in signals:
        weight_pct = int(sig.activation_weight * 100)
        # Ultra-compact: voice icon + insight + weight
        voice_icon = {
            "manas": "M",
            "buddhi": "B",
            "vikalpa": "V",
            "sakshi": "S",
            "ahamkara": "A",
        }.get(sig.voice, "?")

        lines.append(f"[{voice_icon}] {sig.compressed_insight[:60]} ({weight_pct}%)")

    return "\n".join(lines)


def match_signals_to_prompt(prompt: str, signals: List[Signal]) -> List[Signal]:
    """
    Filter signals that are relevant to the current prompt.

    Uses simple keyword matching. More sophisticated matching
    would use embeddings.
    """
    prompt_lower = prompt.lower()
    words = set(w for w in prompt_lower.split() if len(w) > 4)

    matched = []
    for sig in signals:
        insight_lower = sig.compressed_insight.lower()
        # Check for word overlap
        overlap = sum(1 for w in words if w in insight_lower)
        if overlap > 0 or sig.activation_weight > 0.7:
            matched.append(sig)

    return matched


# ============================================================
# Background Voice Signal Generators
# ============================================================

def manas_scan(observations: List[Dict]) -> List[Signal]:
    """
    Manas (quick mind) scans new observations for patterns.

    Called at session end by background hook.
    Uses haiku model for speed.
    """
    signals = []

    # Group observations by category
    by_category = {}
    for obs in observations:
        cat = obs.get("category", "other")
        by_category.setdefault(cat, []).append(obs)

    # Generate signals for patterns
    for cat, obs_list in by_category.items():
        if len(obs_list) >= 3:
            # Pattern detected: multiple observations in same category
            titles = [o.get("title", "")[:30] for o in obs_list[:3]]
            signal = create_signal(
                compressed_insight=f"Pattern: {len(obs_list)} {cat} observations today. {', '.join(titles)}",
                voice="manas",
                source_ids=[o.get("id", "") for o in obs_list],
                activation_weight=0.6,
                elaboration_prompt=f"Analyze the pattern in these {cat} observations: {titles}",
            )
            if signal:
                signals.append(signal)

    return signals


def vikalpa_gaps(resonance_gaps: List[Dict]) -> List[Signal]:
    """
    Vikalpa (imagination) processes resonance gaps.

    Resonance gaps are unexpected activations - concepts that
    fired together despite no obvious connection.
    """
    signals = []

    for gap in resonance_gaps:
        concept_a = gap.get("concept_a", "")
        concept_b = gap.get("concept_b", "")
        strength = gap.get("strength", 0.5)

        if strength > 0.4:
            signal = create_signal(
                compressed_insight=f"Unexpected connection: '{concept_a}' <-> '{concept_b}'",
                voice="vikalpa",
                source_ids=[],
                activation_weight=0.5 + strength * 0.3,
                elaboration_prompt=f"Explore the connection between '{concept_a}' and '{concept_b}'. What insight does this reveal?",
            )
            if signal:
                signals.append(signal)

    return signals


def sakshi_prune() -> int:
    """
    Sakshi (witness) prunes weak signals.

    Called periodically (daily/session-end) to prevent bloat.
    Removes signals with weight < 0.2 and no reinforcement.

    Returns:
        Number of signals pruned
    """
    # Synapse handles pruning internally via cycle()
    graph = get_synapse_graph()
    pruned, _ = graph.cycle()
    return pruned
