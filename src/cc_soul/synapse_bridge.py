"""
Synapse Bridge: Connects cc-soul to C++ synapse graph.

Uses the C++ MCP backend for graph operations:
- 10x faster vector search
- Real semantic embeddings (sentence-transformers via ONNX)
- Binary persistence
- 5-tool architecture: soul_context, grow, observe, recall, cycle

Usage:
    from cc_soul.synapse_bridge import SoulGraph

    graph = SoulGraph.load()
    graph.add_wisdom("title", "content", domain="python")
    results = graph.search("query")
"""

from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# C++ MCP client - the only backend
try:
    import sys
    sys.path.insert(0, str(Path(__file__).parent.parent.parent.parent / "cc-synapse" / "synapse" / "python"))
    from synapse_mcp import Soul
    SYNAPSE_AVAILABLE = True
    SYNAPSE_BACKEND = "cpp-mcp"
except ImportError:
    SYNAPSE_AVAILABLE = False
    SYNAPSE_BACKEND = None
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


# Path for synapse data
SYNAPSE_PATH = SOUL_DIR / "synapse"


class SoulGraph:
    """
    Synapse-backed soul graph.

    Uses C++ MCP backend with 5-tool architecture:
    - soul_context: Get state for hook injection
    - grow: Add wisdom, beliefs, failures, aspirations, dreams, terms
    - observe: Record episodes (replaces cc-memory)
    - recall: Semantic search
    - cycle: Maintenance (decay, prune, coherence, save)
    """

    def __init__(self, path: Optional[Path] = None, use_project: bool = False):
        if not SYNAPSE_AVAILABLE:
            raise ImportError(
                "synapse not installed. Build synapse (cc-synapse/synapse) first."
            )

        if path:
            self._path = path
            self._soul = Soul(str(self._path))
        else:
            self._soul = Soul(use_project=use_project)
            self._path = self._soul._path

        self._backend = SYNAPSE_BACKEND

    @classmethod
    def load(cls, path: Optional[Path] = None) -> "SoulGraph":
        """Load or create soul graph."""
        return cls(path)

    def save(self) -> None:
        """Save to disk."""
        self._soul.save(str(self._path))

    # --- Graph operations (grow) ---

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

    def add_failure(
        self,
        what_failed: str,
        why_it_failed: str,
        domain: Optional[str] = None,
    ) -> str:
        """Record a failure (gold for learning)."""
        return self._soul.record_failure(what_failed, why_it_failed, domain)

    def add_aspiration(
        self,
        direction: str,
        why: str,
        timeframe: Optional[str] = None,
        confidence: float = 0.7,
    ) -> str:
        """Add an aspiration (direction of growth)."""
        return self._soul.aspire(direction, why, timeframe, confidence)

    def add_dream(
        self,
        vision: str,
        inspiration: Optional[str] = None,
        confidence: float = 0.6,
    ) -> str:
        """Add a dream (exploratory vision)."""
        return self._soul.dream(vision, inspiration, confidence)

    def add_term(
        self,
        term: str,
        definition: str,
        domain: Optional[str] = None,
        examples: Optional[List[str]] = None,
    ) -> str:
        """Add a vocabulary term."""
        return self._soul.learn_term(term, definition, domain, examples)

    # --- Observe (record episodes) ---

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

    # --- Search (recall) ---

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

    # --- Context (soul_context) ---

    def get_context(self, query: Optional[str] = None) -> Dict:
        """
        Get context for hook injection.

        Returns dict with coherence, statistics, relevant wisdom.
        """
        return self._soul.get_context(query)

    def format_context(self, query: Optional[str] = None) -> str:
        """Format context as string for hook injection."""
        return self._soul.format_context(query)

    # --- Maintenance (cycle) ---

    def cycle(self) -> Tuple[int, float]:
        """Run maintenance cycle (decay + prune + coherence)."""
        return self._soul.cycle()

    def coherence(self) -> float:
        """Get current coherence (τₖ)."""
        return self._soul.coherence()

    def tick_dynamics(self) -> Dict:
        """Tick the dynamics engine (decay, triggers)."""
        return self._soul.tick_dynamics()

    def snapshot(self) -> int:
        """Create snapshot for rollback."""
        return self._soul.snapshot()


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
