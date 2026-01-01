"""
cc-brain: Brain-like memory with scipy sparse matrices.

Core operations:
- Spreading activation via sparse matrix multiplication
- Hebbian learning (fire together, wire together)
- Temporal decay (synaptic pruning)
- Pattern completion from partial cues

Performance: O(edges) for activation, O(1) neighbor lookup
"""

import json
import sqlite3
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

import numpy as np
from scipy import sparse

from .core import SOUL_DIR


@dataclass
class Concept:
    """A node in the brain graph."""
    id: str
    idx: int  # Numeric index for matrix operations
    title: str
    type: str = "concept"
    domain: str = ""
    activation_count: int = 0
    last_activated: Optional[str] = None


@dataclass
class ActivationResult:
    """Result of spreading activation."""
    activated: List[Tuple[Concept, float]]  # (concept, activation_level)
    paths: List[List[str]]  # Activation paths
    unexpected: List[Tuple[Concept, float]]  # Serendipitous connections
    gaps: List[Tuple[str, str, float]] = field(default_factory=list)  # Resonance gaps: (a, b, strength)


BRAIN_DB = SOUL_DIR / "brain.db"


class Brain:
    """
    Brain-like memory using sparse matrices.

    The adjacency matrix A represents connections.
    Spreading activation: activation = A^depth @ seeds
    Hebbian learning: A[i,j] += activation[i] * activation[j]
    """

    def __init__(self, db_path: Path = BRAIN_DB):
        self.db_path = db_path
        self._id_to_idx: Dict[str, int] = {}
        self._idx_to_id: Dict[int, str] = {}
        self._concepts: Dict[str, Concept] = {}
        self._size = 0

        # Sparse matrices - use lil for construction, csr for operations
        self._adj: Optional[sparse.lil_matrix] = None
        self._weights: Optional[sparse.lil_matrix] = None

        self._init_db()
        self._load_graph()

    def _init_db(self):
        """Initialize SQLite persistence."""
        SOUL_DIR.mkdir(parents=True, exist_ok=True)
        conn = sqlite3.connect(str(self.db_path))
        conn.executescript("""
            CREATE TABLE IF NOT EXISTS concepts (
                id TEXT PRIMARY KEY,
                idx INTEGER UNIQUE,
                title TEXT,
                type TEXT,
                domain TEXT,
                activation_count INTEGER DEFAULT 0,
                last_activated TEXT
            );
            CREATE TABLE IF NOT EXISTS edges (
                source_idx INTEGER,
                target_idx INTEGER,
                weight REAL DEFAULT 1.0,
                last_activated TEXT,
                PRIMARY KEY (source_idx, target_idx)
            );
            CREATE INDEX IF NOT EXISTS idx_edges_source ON edges(source_idx);
        """)
        conn.commit()
        conn.close()

    def _load_graph(self):
        """Load graph from SQLite into sparse matrices."""
        conn = sqlite3.connect(str(self.db_path))
        conn.row_factory = sqlite3.Row

        # Load concepts
        for row in conn.execute("SELECT * FROM concepts ORDER BY idx"):
            concept = Concept(
                id=row["id"], idx=row["idx"], title=row["title"],
                type=row["type"] or "concept", domain=row["domain"] or "",
                activation_count=row["activation_count"] or 0,
                last_activated=row["last_activated"]
            )
            self._concepts[concept.id] = concept
            self._id_to_idx[concept.id] = concept.idx
            self._idx_to_id[concept.idx] = concept.id

        self._size = len(self._concepts)
        if self._size == 0:
            self._size = 1  # Minimum size

        # Initialize sparse matrices
        self._adj = sparse.lil_matrix((self._size, self._size), dtype=np.float32)
        self._weights = sparse.lil_matrix((self._size, self._size), dtype=np.float32)

        # Load edges
        for row in conn.execute("SELECT * FROM edges"):
            i, j = row["source_idx"], row["target_idx"]
            if i < self._size and j < self._size:
                self._adj[i, j] = 1.0
                self._weights[i, j] = row["weight"] or 1.0

        conn.close()

    def _ensure_capacity(self, min_size: int):
        """Grow matrices if needed."""
        if min_size <= self._size:
            return

        new_size = max(min_size, self._size * 2, 100)

        # Resize matrices
        new_adj = sparse.lil_matrix((new_size, new_size), dtype=np.float32)
        new_weights = sparse.lil_matrix((new_size, new_size), dtype=np.float32)

        if self._adj is not None:
            new_adj[:self._size, :self._size] = self._adj
            new_weights[:self._size, :self._size] = self._weights

        self._adj = new_adj
        self._weights = new_weights
        self._size = new_size

    def add_concept(self, id: str, title: str, type: str = "concept", domain: str = "") -> Concept:
        """Add a concept to the brain."""
        if id in self._concepts:
            return self._concepts[id]

        idx = len(self._concepts)
        self._ensure_capacity(idx + 1)

        concept = Concept(id=id, idx=idx, title=title, type=type, domain=domain)
        self._concepts[id] = concept
        self._id_to_idx[id] = idx
        self._idx_to_id[idx] = id

        # Persist
        conn = sqlite3.connect(str(self.db_path))
        conn.execute("""
            INSERT OR REPLACE INTO concepts (id, idx, title, type, domain, activation_count)
            VALUES (?, ?, ?, ?, ?, 0)
        """, (id, idx, title, type, domain))
        conn.commit()
        conn.close()

        return concept

    def connect(self, source_id: str, target_id: str, weight: float = 1.0):
        """Create or strengthen a connection."""
        if source_id not in self._id_to_idx or target_id not in self._id_to_idx:
            return

        i = self._id_to_idx[source_id]
        j = self._id_to_idx[target_id]

        self._adj[i, j] = 1.0
        self._weights[i, j] = weight

        # Persist
        conn = sqlite3.connect(str(self.db_path))
        now = datetime.now().isoformat()
        conn.execute("""
            INSERT OR REPLACE INTO edges (source_idx, target_idx, weight, last_activated)
            VALUES (?, ?, ?, ?)
        """, (i, j, weight, now))
        conn.commit()
        conn.close()

    def spread(
        self,
        seed_ids: List[str],
        depth: int = 3,
        decay: float = 0.5,
        threshold: float = 0.1,
        limit: int = 20,
    ) -> ActivationResult:
        """
        Spreading activation - the core brain operation.

        activation[t+1] = decay * W.T @ activation[t]

        W stores edges as W[source, target], so we transpose to get
        activation flowing FROM sources TO targets.
        """
        if not seed_ids or self._size == 0:
            return ActivationResult([], [], [])

        # Transpose: W[i,j] = source->target, but we need target<-source for multiplication
        W = self._weights.T.tocsr()

        # Initialize activation vector
        activation = np.zeros(self._size, dtype=np.float32)
        for seed_id in seed_ids:
            if seed_id in self._id_to_idx:
                idx = self._id_to_idx[seed_id]
                # Potentiation: frequently activated concepts fire easier
                count = self._concepts[seed_id].activation_count
                activation[idx] = 1.0 + 0.1 * min(count, 50)

        seed_indices = {self._id_to_idx[s] for s in seed_ids if s in self._id_to_idx}

        # Spread activation
        all_activated = activation.copy()
        for d in range(depth):
            activation = decay * (W @ activation)
            all_activated = np.maximum(all_activated, activation)

        # Get top-k activated concepts
        top_indices = np.argsort(all_activated)[::-1][:limit]

        activated = []
        unexpected = []

        for idx in top_indices:
            if all_activated[idx] < threshold:
                continue
            if idx not in self._idx_to_id:
                continue

            concept = self._concepts[self._idx_to_id[idx]]
            score = float(all_activated[idx])

            activated.append((concept, score))

            # Unexpected = high activation but not a seed
            if idx not in seed_indices and score > 0.3:
                unexpected.append((concept, score))

        # Update activation counts (Hebbian trace)
        self._update_activation_counts([c.id for c, _ in activated])

        # Detect resonance gaps: strongly co-activated pairs with no edge
        gaps = self._detect_gaps(activated, threshold=0.25)

        return ActivationResult(
            activated=activated,
            paths=[],  # Could track paths with more complex algorithm
            unexpected=unexpected[:5],
            gaps=gaps[:5]
        )

    def _detect_gaps(
        self,
        activated: List[Tuple[Concept, float]],
        threshold: float = 0.25
    ) -> List[Tuple[str, str, float]]:
        """
        Detect resonance gaps: pairs of concepts that both activated strongly
        but have no direct edge between them.

        These gaps are discovery opportunities - unexplained co-activation
        suggests a missing conceptual bridge.
        """
        gaps = []
        strong = [(c, s) for c, s in activated if s >= threshold]

        for i, (c1, s1) in enumerate(strong):
            for c2, s2 in strong[i+1:]:
                idx1 = self._id_to_idx.get(c1.id)
                idx2 = self._id_to_idx.get(c2.id)
                if idx1 is None or idx2 is None:
                    continue

                # Check if there's a direct edge
                has_edge = (
                    self._weights[idx1, idx2] > 0 or
                    self._weights[idx2, idx1] > 0
                )
                if not has_edge:
                    resonance = s1 * s2
                    gaps.append((c1.id, c2.id, resonance))

        gaps.sort(key=lambda x: -x[2])
        return gaps

    def _update_activation_counts(self, concept_ids: List[str]):
        """Update activation counts for Hebbian learning."""
        conn = sqlite3.connect(str(self.db_path))
        now = datetime.now().isoformat()

        for cid in concept_ids:
            if cid in self._concepts:
                self._concepts[cid].activation_count += 1
                self._concepts[cid].last_activated = now
                conn.execute("""
                    UPDATE concepts
                    SET activation_count = activation_count + 1, last_activated = ?
                    WHERE id = ?
                """, (now, cid))

        conn.commit()
        conn.close()

    def hebbian_learn(self, activated_ids: List[str], strength: float = 0.05):
        """
        Hebbian learning: strengthen connections between co-activated concepts.

        'Neurons that fire together, wire together.'
        """
        indices = [self._id_to_idx[cid] for cid in activated_ids if cid in self._id_to_idx]

        conn = sqlite3.connect(str(self.db_path))
        now = datetime.now().isoformat()

        for i in indices:
            for j in indices:
                if i != j:
                    current = self._weights[i, j]
                    new_weight = min(current + strength, 2.0)  # Cap at 2.0
                    self._weights[i, j] = new_weight
                    self._adj[i, j] = 1.0

                    conn.execute("""
                        INSERT OR REPLACE INTO edges (source_idx, target_idx, weight, last_activated)
                        VALUES (?, ?, ?, ?)
                    """, (i, j, new_weight, now))

        conn.commit()
        conn.close()

    def prune(self, decay: float = 0.1, min_weight: float = 0.15):
        """
        Synaptic pruning: decay unused connections.

        Connections not reinforced fade over time.
        """
        conn = sqlite3.connect(str(self.db_path))

        # Decay all weights slightly
        self._weights = self._weights.multiply(1.0 - decay)

        # Remove very weak connections
        mask = self._weights.toarray() >= min_weight
        self._weights = sparse.lil_matrix(self._weights.toarray() * mask)
        self._adj = sparse.lil_matrix(mask.astype(np.float32))

        # Update database
        conn.execute("DELETE FROM edges WHERE weight < ?", (min_weight,))
        conn.commit()
        conn.close()

    def search(self, query: str, limit: int = 10) -> List[Concept]:
        """Search concepts by title."""
        query_lower = query.lower()
        matches = []

        for concept in self._concepts.values():
            if query_lower in concept.title.lower():
                matches.append(concept)

        # Sort by activation count (more activated = more relevant)
        matches.sort(key=lambda c: c.activation_count, reverse=True)
        return matches[:limit]

    def activate_from_prompt(self, prompt: str, limit: int = 10) -> ActivationResult:
        """
        Activate concepts relevant to a prompt.

        Two-stage architecture:
        1. Semantic seeding: Use embeddings to find semantically similar wisdom
        2. Associative spreading: Propagate activation through the graph

        Embeddings find what you're asking about.
        Spreading finds what you should also know.
        """
        seeds = set()

        # Stage 1: Semantic seeding (if embeddings available)
        try:
            from .vectors import search_wisdom
            semantic_hits = search_wisdom(prompt, limit=5)
            for hit in semantic_hits:
                seeds.add(f"wisdom_{hit.get('id', '')}")
        except Exception:
            pass  # Embeddings not available, use keyword fallback

        # Fallback/supplement: keyword matches
        words = prompt.lower().split()
        for word in words:
            if len(word) > 4:
                for concept in self.search(word, limit=2):
                    seeds.add(concept.id)

        if not seeds:
            return ActivationResult([], [], [])

        # Stage 2: Associative spreading
        result = self.spread(list(seeds), depth=3, decay=0.6, limit=limit)

        # Hebbian: strengthen connections between co-activated
        self.hebbian_learn([c.id for c, _ in result.activated])

        return result

    def stats(self) -> Dict:
        """Get brain statistics."""
        n_concepts = len(self._concepts)
        n_edges = self._adj.nnz if self._adj is not None else 0
        return {
            "concepts": n_concepts,
            "edges": n_edges,
            "density": n_edges / (n_concepts ** 2) if n_concepts > 0 else 0
        }

    def sync_from_wisdom(self):
        """Sync wisdom entries into the brain."""
        from .wisdom import recall_wisdom
        from .beliefs import get_beliefs
        from .vocabulary import get_vocabulary

        # Add wisdom
        for entry in recall_wisdom(limit=500):
            self.add_concept(
                id=f"wisdom_{entry.get('id', '')}",
                title=entry.get("title", "")[:100],
                type="wisdom",
                domain=entry.get("domain", "")
            )

        # Add beliefs
        for belief in get_beliefs():
            self.add_concept(
                id=f"belief_{belief.get('id', '')}",
                title=belief.get("belief", "")[:100],
                type="belief"
            )

        # Add vocabulary
        for term, meaning in get_vocabulary().items():
            self.add_concept(id=f"term_{term}", title=term, type="term")

        # Auto-link by title similarity
        self._auto_link()

        return self.stats()

    def _auto_link(self):
        """Create edges between semantically similar concepts."""
        concepts = list(self._concepts.values())

        for i, c1 in enumerate(concepts):
            words1 = set(c1.title.lower().split())
            for c2 in concepts[i+1:]:
                words2 = set(c2.title.lower().split())
                overlap = {w for w in (words1 & words2) if len(w) > 4}

                if overlap:
                    weight = len(overlap) / max(len(words1), len(words2))
                    if weight >= 0.2:
                        self.connect(c1.id, c2.id, weight)
                        self.connect(c2.id, c1.id, weight)  # Bidirectional


def get_brain() -> Brain:
    """Get the global brain instance."""
    return Brain()


def get_concept_content(concept_id: str) -> str:
    """Fetch content from source (not stored in brain)."""
    from .wisdom import get_wisdom_by_id

    if concept_id.startswith("wisdom_"):
        wisdom_id = concept_id[7:]
        wisdom = get_wisdom_by_id(wisdom_id)
        return wisdom.get("content", "") if wisdom else ""
    elif concept_id.startswith("belief_"):
        from .beliefs import get_belief_by_id
        belief = get_belief_by_id(concept_id[7:])
        return belief.get("belief", "") if belief else ""
    elif concept_id.startswith("term_"):
        from .vocabulary import get_vocabulary
        return get_vocabulary().get(concept_id[5:], "")
    return ""
