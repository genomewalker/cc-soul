"""
Synapse Bridge: Connects cc-soul to cc-synapse Rust graph.

This module provides the same interface as graph_sqlite.py but uses
the synapse Rust library for graph operations. This gives us:
- 10x faster vector search
- Real semantic embeddings (sentence-transformers)
- Binary persistence instead of SQLite
- Unified graph for all soul operations

Usage:
    from cc_soul.synapse_bridge import SoulGraph

    graph = SoulGraph.load()  # Loads from ~/.claude/mind/soul.synapse
    graph.add_wisdom("title", "content", domain="python")
    results = graph.search("query")
"""

from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from pathlib import Path
from typing import Dict, List, Optional, Tuple

try:
    from synapse import Soul
    SYNAPSE_AVAILABLE = True
except ImportError:
    SYNAPSE_AVAILABLE = False
    Soul = None

from .core import SOUL_DIR


# Compatibility types (match graph_sqlite.py interface)
class ConceptType(str, Enum):
    WISDOM = "wisdom"
    BELIEF = "belief"
    TERM = "term"
    FILE = "file"
    DECISION = "decision"
    PATTERN = "pattern"
    FAILURE = "failure"


@dataclass
class Concept:
    """A node in the concept graph (compatibility with graph_sqlite)."""
    id: str
    type: ConceptType
    title: str
    content: str = ""
    created_at: str = field(default_factory=lambda: datetime.now().isoformat())
    last_activated: str = None
    activation_count: int = 0
    metadata: Dict = field(default_factory=dict)


SYNAPSE_PATH = SOUL_DIR / "soul.synapse"


class SoulGraph:
    """
    Synapse-backed soul graph.

    Drop-in replacement for graph_sqlite operations, backed by Rust.
    """

    def __init__(self, path: Optional[Path] = None):
        self._path = path or SYNAPSE_PATH
        if not SYNAPSE_AVAILABLE:
            raise ImportError(
                "synapse not installed. Run: cd cc-synapse/synapse-py && maturin develop"
            )
        self._soul = Soul(str(self._path))

    @classmethod
    def load(cls, path: Optional[Path] = None) -> "SoulGraph":
        """Load or create soul graph."""
        return cls(path)

    def save(self) -> None:
        """Save to disk."""
        self._soul.save(str(self._path))

    # --- Graph operations ---

    def add_wisdom(
        self,
        title: str,
        content: str,
        domain: Optional[str] = None,
        confidence: float = 0.8,
    ) -> str:
        """Add wisdom to the graph."""
        return self._soul.grow_wisdom(title, content, domain, confidence)

    def add_belief(self, statement: str, strength: float = 0.9) -> str:
        """Add an immutable belief."""
        return self._soul.hold_belief(statement, strength)

    def set_intention(
        self,
        want: str,
        why: str,
        scope: str = "session",
        strength: float = 0.8,
    ) -> str:
        """Set an intention (concrete want)."""
        return self._soul.set_intention(want, why, scope, strength)

    def get_intentions(self) -> List[Dict]:
        """Get active intentions."""
        return self._soul.get_intentions()

    def add_failure(
        self,
        what_failed: str,
        why_it_failed: str,
        domain: Optional[str] = None,
    ) -> str:
        """Record a failure (gold for learning)."""
        return self._soul.record_failure(what_failed, why_it_failed, domain)

    def observe(
        self,
        category: str,
        title: str,
        content: str,
        project: Optional[str] = None,
        tags: Optional[List[str]] = None,
    ) -> str:
        """
        Record an observation (episode).

        Replaces cc-memory's mem-remember. Categories:
        - bugfix, decision: slow decay (2%/day)
        - discovery, feature: medium decay (5%/day)
        - session_ledger, signal: fast decay (15%/day)
        """
        return self._soul.observe(category, title, content, project, tags)

    def get_episodes(
        self,
        category: Optional[str] = None,
        project: Optional[str] = None,
        limit: int = 50,
    ) -> List[Dict]:
        """Get episodes, optionally filtered."""
        return self._soul.get_episodes(category, project, limit)

    def search(
        self,
        query: str,
        limit: int = 10,
        threshold: float = 0.3,
    ) -> List[Tuple[Concept, float]]:
        """
        Semantic search across all nodes.

        Returns list of (Concept, similarity_score) tuples.
        """
        results = self._soul.search(query, limit, threshold)
        concepts = []
        for id, score, payload in results:
            concept = Concept(
                id=id,
                type=ConceptType.WISDOM,  # Default, could be refined
                title=payload.get("title", ""),
                content=payload.get("content", ""),
                metadata=payload,
            )
            concepts.append((concept, score))
        return concepts

    def get_context(self, query: Optional[str] = None) -> Dict:
        """
        Get context for hook injection.

        Returns dict with beliefs, intentions, wisdom, failures, coherence.
        """
        return self._soul.get_context(query)

    def format_context(self, query: Optional[str] = None) -> str:
        """Format context as string for hook injection."""
        return self._soul.format_context(query)

    # --- Maintenance ---

    def cycle(self) -> Tuple[int, float]:
        """Run maintenance cycle (decay + prune + coherence)."""
        return self._soul.cycle()

    def coherence(self) -> float:
        """Get current coherence (τₖ)."""
        return self._soul.coherence()

    def strengthen(self, node_id: str) -> bool:
        """Positive feedback on a node."""
        return self._soul.strengthen(node_id)

    def weaken(self, node_id: str) -> bool:
        """Negative feedback on a node."""
        return self._soul.weaken(node_id)

    # --- Statistics ---

    def __len__(self) -> int:
        return len(self._soul)

    def get_all_wisdom(self) -> List[Dict]:
        """Get all wisdom entries."""
        return self._soul.get_wisdom()

    def get_all_beliefs(self) -> List[Dict]:
        """Get all beliefs."""
        return self._soul.get_beliefs()

    def get_all_failures(self) -> List[Dict]:
        """Get all failures."""
        return self._soul.get_failures()

    # --- Antahkarana Voices (Rust implementation) ---

    def consult_voice(
        self,
        voice_name: str,
        query: str,
        limit: int = 10,
    ) -> List[Tuple[str, float, Dict]]:
        """
        Consult a specific Antahkarana voice.

        Voices: manas, buddhi, ahamkara, chitta, vikalpa, sakshi
        Each voice sees the graph differently based on attention weights.
        """
        return self._soul.consult_voice(voice_name, query, limit)

    def chorus_query(
        self,
        query: str,
        limit: int = 10,
    ) -> List[Tuple[str, float, List[str], Dict]]:
        """Query through all voices and harmonize results."""
        return self._soul.chorus_query(query, limit)

    def harmonize(self) -> Dict:
        """Get harmony report from all voices."""
        return self._soul.harmonize()

    def get_voices(self) -> List[Dict]:
        """Get descriptions of the six Antahkarana voices."""
        return self._soul.get_voices()

    # --- Dynamics (Rust implementation) ---

    def tick_dynamics(self) -> Dict:
        """Tick the dynamics engine (decay, triggers)."""
        return self._soul.tick_dynamics()

    def learning_feedback(self, node_id: str, success: bool) -> bool:
        """Provide learning feedback (strengthen/weaken based on outcome)."""
        return self._soul.learning_feedback(node_id, success)

    def snapshot(self) -> int:
        """Create snapshot for rollback."""
        return self._soul.snapshot()

    def rollback(self, snapshot_id: int) -> bool:
        """Rollback to snapshot."""
        return self._soul.rollback(snapshot_id)


# Convenience functions (match graph_sqlite.py API)
def activate_concepts(prompt: str, limit: int = 10) -> List[Tuple[Concept, float]]:
    """
    Spreading activation from a prompt.

    Uses synapse semantic search instead of graph traversal.
    """
    graph = SoulGraph.load()
    return graph.search(prompt, limit=limit)


def add_concept(
    id: str,
    type: ConceptType,
    title: str,
    domain: Optional[str] = None,
) -> None:
    """Add a concept (compatibility shim)."""
    graph = SoulGraph.load()
    if type == ConceptType.BELIEF:
        graph.add_belief(title)
    elif type == ConceptType.FAILURE:
        graph.add_failure(title, "")
    else:
        graph.add_wisdom(title, "", domain=domain)
    graph.save()


def link_concepts(
    source_id: str,
    target_id: str,
    relation: str,
    weight: float = 1.0,
) -> None:
    """
    Link concepts (compatibility shim).

    Note: Synapse uses vector similarity, not explicit edges.
    This function is a no-op but maintained for API compatibility.
    """
    pass  # Synapse uses vector similarity instead of explicit edges
