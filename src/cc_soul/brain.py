"""
cc-brain: Brain-like memory with content-addressed concepts and dual spreading.

Architecture:
- Content-addressed identity: same title = same concept (deduplicated)
- Types become tags: a concept can be wisdom AND belief AND term
- Semantic spreading: via embeddings (implicit edges, no storage)
- Hebbian spreading: via co-activation edges (explicit, stored)

The key insight: semantic similarity doesn't need stored edges.
Embeddings provide O(1) similarity. Only Hebbian (learned) connections need storage.

Performance: O(k) for semantic search, O(edges) for Hebbian spreading
"""

import hashlib
import sqlite3
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

import numpy as np
from scipy import sparse

from .core import SOUL_DIR


def _content_id(title: str) -> str:
    """Generate content-addressed ID from title."""
    normalized = title.lower().strip()[:100]
    return hashlib.sha256(normalized.encode()).hexdigest()[:16]


@dataclass
class Concept:
    """A node in the brain graph - content-addressed by title."""
    id: str  # Content-addressed hash of title
    idx: int  # Numeric index for matrix operations
    title: str
    types: Set[str] = field(default_factory=set)  # Tags: wisdom, belief, term
    domain: str = ""
    activation_count: int = 0
    last_activated: Optional[str] = None

    def add_type(self, type_tag: str):
        """Add a type tag to this concept."""
        self.types.add(type_tag)


@dataclass
class ActivationResult:
    """Result of spreading activation."""
    activated: List[Tuple[Concept, float]]  # (concept, activation_level)
    semantic: List[Tuple[Concept, float]]  # From embedding similarity
    hebbian: List[Tuple[Concept, float]]  # From co-activation edges
    unexpected: List[Tuple[Concept, float]]  # Serendipitous connections
    gaps: List[Tuple[str, str, float]] = field(default_factory=list)  # Resonance gaps


BRAIN_DB = SOUL_DIR / "brain.db"


class Brain:
    """
    Brain-like memory with content-addressed concepts and dual spreading.

    Key innovation: concepts are content-addressed (same title = same concept).
    Spreading activation has two paths:
    1. Semantic: embedding similarity (implicit, computed on-demand)
    2. Hebbian: co-activation edges (explicit, stored)
    """

    def __init__(self, db_path: Path = BRAIN_DB):
        self.db_path = db_path
        self._id_to_idx: Dict[str, int] = {}
        self._idx_to_id: Dict[int, str] = {}
        self._title_to_id: Dict[str, str] = {}  # For dedup lookup
        self._concepts: Dict[str, Concept] = {}
        self._size = 0

        # Sparse matrix for Hebbian (co-activation) edges only
        self._weights: Optional[sparse.lil_matrix] = None

        self._init_db()
        self._load_graph()

    def _init_db(self):
        """Initialize SQLite persistence with schema migration."""
        SOUL_DIR.mkdir(parents=True, exist_ok=True)
        conn = sqlite3.connect(str(self.db_path))

        # Create tables if they don't exist
        conn.executescript("""
            CREATE TABLE IF NOT EXISTS concepts (
                id TEXT PRIMARY KEY,
                idx INTEGER UNIQUE,
                title TEXT,
                types TEXT,
                domain TEXT,
                activation_count INTEGER DEFAULT 0,
                last_activated TEXT
            );
            CREATE TABLE IF NOT EXISTS edges (
                source_idx INTEGER,
                target_idx INTEGER,
                weight REAL DEFAULT 1.0,
                edge_type TEXT DEFAULT 'hebbian',
                last_activated TEXT,
                PRIMARY KEY (source_idx, target_idx)
            );
            CREATE INDEX IF NOT EXISTS idx_edges_source ON edges(source_idx);
            CREATE INDEX IF NOT EXISTS idx_concepts_title ON concepts(title);
        """)

        # Schema migration: add types column if missing (migrate from old 'type')
        cursor = conn.execute("PRAGMA table_info(concepts)")
        columns = {row[1] for row in cursor.fetchall()}

        if "types" not in columns and "type" in columns:
            conn.execute("ALTER TABLE concepts ADD COLUMN types TEXT")
            conn.execute("UPDATE concepts SET types = type WHERE types IS NULL")
            conn.commit()
        elif "types" not in columns:
            conn.execute("ALTER TABLE concepts ADD COLUMN types TEXT")
            conn.commit()

        # Migration: add edge_type column if missing
        cursor = conn.execute("PRAGMA table_info(edges)")
        edge_columns = {row[1] for row in cursor.fetchall()}

        if "edge_type" not in edge_columns:
            conn.execute("ALTER TABLE edges ADD COLUMN edge_type TEXT DEFAULT 'hebbian'")
            conn.commit()

        conn.close()

    def _load_graph(self):
        """Load graph from SQLite into sparse matrices."""
        conn = sqlite3.connect(str(self.db_path))
        conn.row_factory = sqlite3.Row

        # Check schema - handle migration from old 'type' to new 'types'
        cursor = conn.execute("PRAGMA table_info(concepts)")
        columns = {row[1] for row in cursor.fetchall()}
        has_types = "types" in columns
        has_type = "type" in columns

        # Load concepts
        for row in conn.execute("SELECT * FROM concepts ORDER BY idx"):
            # Handle both old and new schema
            if has_types:
                types_str = row["types"] or ""
            elif has_type:
                types_str = row["type"] or "concept"
            else:
                types_str = "concept"

            types = set(types_str.split(",")) if types_str else {"concept"}

            concept = Concept(
                id=row["id"], idx=row["idx"], title=row["title"],
                types=types, domain=row["domain"] or "",
                activation_count=row["activation_count"] or 0,
                last_activated=row["last_activated"]
            )
            self._concepts[concept.id] = concept
            self._id_to_idx[concept.id] = concept.idx
            self._idx_to_id[concept.idx] = concept.id
            self._title_to_id[concept.title.lower().strip()[:100]] = concept.id

        self._size = len(self._concepts)
        if self._size == 0:
            self._size = 1  # Minimum size

        # Only Hebbian edges - semantic similarity is computed on-demand
        self._weights = sparse.lil_matrix((self._size, self._size), dtype=np.float32)

        # Check if edge_type column exists
        cursor = conn.execute("PRAGMA table_info(edges)")
        edge_columns = {row[1] for row in cursor.fetchall()}
        has_edge_type = "edge_type" in edge_columns

        # Load edges (all edges in old schema, only hebbian in new)
        if has_edge_type:
            query = "SELECT * FROM edges WHERE edge_type = 'hebbian' OR edge_type IS NULL"
        else:
            query = "SELECT * FROM edges"

        for row in conn.execute(query):
            i, j = row["source_idx"], row["target_idx"]
            if i < self._size and j < self._size:
                self._weights[i, j] = row["weight"] or 1.0

        conn.close()

    def _ensure_capacity(self, min_size: int):
        """Grow matrices if needed."""
        if min_size <= self._size:
            return

        new_size = max(min_size, self._size * 2, 100)
        new_weights = sparse.lil_matrix((new_size, new_size), dtype=np.float32)

        if self._weights is not None:
            new_weights[:self._size, :self._size] = self._weights

        self._weights = new_weights
        self._size = new_size

    def add_concept(self, title: str, type_tag: str = "concept", domain: str = "", source_id: str = None) -> Concept:
        """
        Add a concept to the brain (content-addressed).

        If a concept with the same title exists, adds the type tag to it.
        This enables deduplication: wisdom_X and belief_X with same title → one concept.

        Args:
            title: The concept title (used for content-addressing)
            type_tag: Tag like 'wisdom', 'belief', 'term'
            domain: Optional domain context
            source_id: Original ID from source system (for reference only)
        """
        normalized_title = title.lower().strip()[:100]

        # Check for existing concept with same title
        if normalized_title in self._title_to_id:
            existing_id = self._title_to_id[normalized_title]
            existing = self._concepts[existing_id]
            existing.add_type(type_tag)
            # Update domain if provided and not set
            if domain and not existing.domain:
                existing.domain = domain
            self._persist_concept(existing)
            return existing

        # New concept - content-addressed ID
        concept_id = _content_id(title)
        idx = len(self._concepts)
        self._ensure_capacity(idx + 1)

        concept = Concept(
            id=concept_id, idx=idx, title=title,
            types={type_tag}, domain=domain
        )
        self._concepts[concept_id] = concept
        self._id_to_idx[concept_id] = idx
        self._idx_to_id[idx] = concept_id
        self._title_to_id[normalized_title] = concept_id

        self._persist_concept(concept)
        return concept

    def _persist_concept(self, concept: Concept):
        """Persist concept to database."""
        conn = sqlite3.connect(str(self.db_path))
        types_str = ",".join(sorted(concept.types))
        conn.execute("""
            INSERT OR REPLACE INTO concepts (id, idx, title, types, domain, activation_count, last_activated)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        """, (concept.id, concept.idx, concept.title, types_str, concept.domain,
              concept.activation_count, concept.last_activated))
        conn.commit()
        conn.close()

    def connect(self, source_id: str, target_id: str, weight: float = 1.0):
        """Create or strengthen a Hebbian connection."""
        if source_id not in self._id_to_idx or target_id not in self._id_to_idx:
            return
        if source_id == target_id:
            return  # No self-loops

        i = self._id_to_idx[source_id]
        j = self._id_to_idx[target_id]

        self._weights[i, j] = weight

        conn = sqlite3.connect(str(self.db_path))
        now = datetime.now().isoformat()
        conn.execute("""
            INSERT OR REPLACE INTO edges (source_idx, target_idx, weight, edge_type, last_activated)
            VALUES (?, ?, ?, 'hebbian', ?)
        """, (i, j, float(weight), now))
        conn.commit()
        conn.close()

    def spread(
        self,
        seed_ids: List[str],
        depth: int = 2,
        decay: float = 0.5,
        threshold: float = 0.1,
        limit: int = 20,
    ) -> ActivationResult:
        """
        Dual-path spreading activation.

        Path 1 (Semantic): Use embeddings to find semantically similar concepts.
        Path 2 (Hebbian): Propagate through co-activation edges.

        This is more powerful than either alone because:
        - Semantic finds concepts you're asking about
        - Hebbian finds concepts you should also know
        """
        if not seed_ids or self._size == 0:
            return ActivationResult([], [], [], [])

        # Collect seed concepts
        seed_concepts = []
        seed_indices = set()
        for seed_id in seed_ids:
            if seed_id in self._concepts:
                seed_concepts.append(self._concepts[seed_id])
                seed_indices.add(self._id_to_idx[seed_id])

        if not seed_concepts:
            return ActivationResult([], [], [], [])

        # PATH 1: Semantic spreading (via embeddings)
        semantic_results = self._semantic_spread(seed_concepts, limit=limit // 2)

        # PATH 2: Hebbian spreading (via co-activation edges)
        hebbian_results = self._hebbian_spread(seed_ids, depth, decay, threshold, limit=limit // 2)

        # Merge results, avoiding duplicates
        seen = set()
        all_activated = []

        for concept, score in semantic_results + hebbian_results:
            if concept.id not in seen:
                seen.add(concept.id)
                all_activated.append((concept, score))

        # Sort by score and limit
        all_activated.sort(key=lambda x: -x[1])
        all_activated = all_activated[:limit]

        # Identify unexpected activations (not seeds, high activation)
        unexpected = [
            (c, s) for c, s in all_activated
            if c.idx not in seed_indices and s > 0.3
        ][:5]

        # Update activation counts
        self._update_activation_counts([c.id for c, _ in all_activated])

        # Detect resonance gaps
        gaps = self._detect_gaps(all_activated, threshold=0.25)

        return ActivationResult(
            activated=all_activated,
            semantic=semantic_results,
            hebbian=hebbian_results,
            unexpected=unexpected,
            gaps=gaps[:5]
        )

    def _semantic_spread(self, seed_concepts: List[Concept], limit: int = 10) -> List[Tuple[Concept, float]]:
        """Find semantically similar concepts via embeddings."""
        try:
            from .vectors import search_wisdom
        except ImportError:
            return []

        results = []
        seen_titles = {c.title.lower() for c in seed_concepts}

        for seed in seed_concepts[:3]:  # Limit seeds to avoid overload
            try:
                hits = search_wisdom(seed.title, limit=limit)
                for hit in hits:
                    title = hit.get("title", "")
                    if title.lower() not in seen_titles:
                        seen_titles.add(title.lower())
                        # Find or create concept
                        normalized = title.lower().strip()[:100]
                        if normalized in self._title_to_id:
                            concept = self._concepts[self._title_to_id[normalized]]
                            score = hit.get("score", 0.5)
                            results.append((concept, score))
            except Exception:
                pass

        results.sort(key=lambda x: -x[1])
        return results[:limit]

    def _hebbian_spread(
        self,
        seed_ids: List[str],
        depth: int,
        decay: float,
        threshold: float,
        limit: int
    ) -> List[Tuple[Concept, float]]:
        """Spread activation through Hebbian (co-activation) edges."""
        if self._weights is None or self._weights.nnz == 0:
            return []

        W = self._weights.T.tocsr()

        # Initialize activation
        activation = np.zeros(self._size, dtype=np.float32)
        for seed_id in seed_ids:
            if seed_id in self._id_to_idx:
                idx = self._id_to_idx[seed_id]
                count = self._concepts[seed_id].activation_count
                activation[idx] = 1.0 + 0.1 * min(count, 50)

        seed_indices = {self._id_to_idx[s] for s in seed_ids if s in self._id_to_idx}

        # Spread
        all_activated = activation.copy()
        for _ in range(depth):
            activation = decay * (W @ activation)
            all_activated = np.maximum(all_activated, activation)

        # Collect non-seed activations above threshold
        results = []
        for idx in np.argsort(all_activated)[::-1][:limit * 2]:
            if idx in seed_indices:
                continue
            if all_activated[idx] < threshold:
                continue
            if idx not in self._idx_to_id:
                continue

            concept = self._concepts[self._idx_to_id[idx]]
            results.append((concept, float(all_activated[idx])))

        return results[:limit]

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
        if len(indices) < 2:
            return

        conn = sqlite3.connect(str(self.db_path))
        now = datetime.now().isoformat()

        for i in indices:
            for j in indices:
                if i != j:
                    current = self._weights[i, j]
                    new_weight = min(float(current) + strength, 2.0)  # Cap at 2.0
                    self._weights[i, j] = new_weight

                    conn.execute("""
                        INSERT OR REPLACE INTO edges (source_idx, target_idx, weight, edge_type, last_activated)
                        VALUES (?, ?, ?, 'hebbian', ?)
                    """, (i, j, float(new_weight), now))

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
        Activate concepts relevant to a prompt using dual-path spreading.

        1. Check signals from background voice processing
        2. Find seed concepts via embedding search and keyword matching
        3. Spread activation through both semantic and Hebbian paths
        4. Strengthen Hebbian edges for co-activated concepts
        """
        seeds = set()

        # Stage 0: Signal-derived seeds (from background voice processing)
        try:
            from .signals import get_signals, match_signals_to_prompt
            signals = get_signals(min_weight=0.4, limit=5)
            matched = match_signals_to_prompt(prompt, signals)
            for sig in matched:
                # Add source concept IDs as seeds
                for source_id in sig.source_ids:
                    if source_id in self._concepts:
                        seeds.add(source_id)
        except Exception:
            pass

        # Stage 1: Semantic seeding via embeddings
        try:
            from .vectors import search_wisdom
            hits = search_wisdom(prompt, limit=5)
            for hit in hits:
                title = hit.get("title", "")
                normalized = title.lower().strip()[:100]
                if normalized in self._title_to_id:
                    seeds.add(self._title_to_id[normalized])
        except Exception:
            pass

        # Supplement with keyword matches
        words = prompt.lower().split()
        for word in words:
            if len(word) > 4:
                for concept in self.search(word, limit=2):
                    seeds.add(concept.id)

        if not seeds:
            return ActivationResult([], [], [], [])

        # Stage 2: Dual-path spreading
        result = self.spread(list(seeds), depth=2, decay=0.6, limit=limit)

        # Stage 3: Hebbian learning - strengthen co-activated pairs
        if len(result.activated) >= 2:
            self.hebbian_learn([c.id for c, _ in result.activated])

        return result

    def stats(self) -> Dict:
        """Get brain statistics."""
        n_concepts = len(self._concepts)
        n_edges = self._weights.nnz if self._weights is not None else 0

        # Count concepts by type
        type_counts = {}
        multi_type = 0
        for c in self._concepts.values():
            if len(c.types) > 1:
                multi_type += 1
            for t in c.types:
                type_counts[t] = type_counts.get(t, 0) + 1

        return {
            "concepts": n_concepts,
            "hebbian_edges": n_edges,
            "multi_type_concepts": multi_type,
            "types": type_counts,
            "density": n_edges / (n_concepts ** 2) if n_concepts > 0 else 0
        }

    def sync_from_wisdom(self, rebuild: bool = False):
        """
        Sync wisdom entries into the brain using content-addressed concepts.

        Key change: concepts with same title are merged, not duplicated.
        No auto_link - semantic similarity comes from embeddings.
        """
        from .wisdom import recall_wisdom
        from .beliefs import get_beliefs
        from .vocabulary import get_vocabulary

        if rebuild:
            self._clear_all()

        # Add wisdom (content-addressed - duplicates merge automatically)
        for entry in recall_wisdom(limit=500):
            title = entry.get("title", "")[:100]
            if title:
                self.add_concept(
                    title=title,
                    type_tag="wisdom",
                    domain=entry.get("domain", ""),
                    source_id=entry.get("id", "")
                )

        # Add beliefs (will merge with wisdom if same title)
        for belief in get_beliefs():
            title = belief.get("belief", "")[:100]
            if title:
                self.add_concept(
                    title=title,
                    type_tag="belief",
                    source_id=belief.get("id", "")
                )

        # Add vocabulary
        for term, meaning in get_vocabulary().items():
            if term:
                self.add_concept(title=term, type_tag="term")

        # No auto_link - semantic similarity is computed on-demand via embeddings
        return self.stats()

    def _clear_all(self):
        """Clear all concepts and edges for rebuild."""
        conn = sqlite3.connect(str(self.db_path))
        conn.execute("DELETE FROM concepts")
        conn.execute("DELETE FROM edges")
        conn.commit()
        conn.close()

        self._concepts.clear()
        self._id_to_idx.clear()
        self._idx_to_id.clear()
        self._title_to_id.clear()
        self._size = 1
        self._weights = sparse.lil_matrix((self._size, self._size), dtype=np.float32)


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
