"""
SQLite-based Concept Graph: Replaces Kuzu with pure SQLite.

Uses recursive CTEs for spreading activation - no external graph dependencies.
Simpler, more portable, and doesn't hang on shared filesystems.

Install: No extra dependencies (uses stdlib sqlite3)
"""

import json
import sqlite3
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

from .core import SOUL_DIR


class ConceptType(str, Enum):
    WISDOM = "wisdom"
    BELIEF = "belief"
    TERM = "term"
    FILE = "file"
    DECISION = "decision"
    PATTERN = "pattern"
    FAILURE = "failure"


class RelationType(str, Enum):
    RELATED_TO = "related_to"
    LED_TO = "led_to"
    CONTRADICTS = "contradicts"
    EVOLVED_FROM = "evolved_from"
    REMINDED_BY = "reminded_by"
    USED_WITH = "used_with"
    REQUIRES = "requires"


@dataclass
class Concept:
    """A node in the concept graph."""
    id: str
    type: ConceptType
    title: str
    content: str = ""  # Fetched on demand, not stored in graph
    created_at: str = field(default_factory=lambda: datetime.now().isoformat())
    last_activated: str = None
    activation_count: int = 0
    metadata: Dict = field(default_factory=dict)


@dataclass
class Edge:
    """A relationship between concepts."""
    source_id: str
    target_id: str
    relation: RelationType
    weight: float = 1.0
    created_at: str = field(default_factory=lambda: datetime.now().isoformat())
    last_activated: str = None


@dataclass
class ActivationResult:
    """Result of spreading activation."""
    primary: List[Tuple[Concept, float]]
    spread: List[Tuple[Concept, float]]
    paths: List[List[str]]
    unexpected: List[Tuple[Concept, float]]


GRAPH_DB = SOUL_DIR / "graph.db"


def _get_conn() -> sqlite3.Connection:
    """Get SQLite connection with schema initialized."""
    SOUL_DIR.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(GRAPH_DB))
    conn.row_factory = sqlite3.Row

    # Create schema
    conn.executescript("""
        CREATE TABLE IF NOT EXISTS concepts (
            id TEXT PRIMARY KEY,
            type TEXT NOT NULL,
            title TEXT NOT NULL,
            domain TEXT,
            last_activated TEXT,
            activation_count INTEGER DEFAULT 0
        );

        CREATE TABLE IF NOT EXISTS edges (
            source_id TEXT NOT NULL,
            target_id TEXT NOT NULL,
            relation TEXT NOT NULL,
            weight REAL DEFAULT 1.0,
            created_at TEXT,
            last_activated TEXT,
            PRIMARY KEY (source_id, target_id, relation),
            FOREIGN KEY (source_id) REFERENCES concepts(id),
            FOREIGN KEY (target_id) REFERENCES concepts(id)
        );

        CREATE INDEX IF NOT EXISTS idx_edges_source ON edges(source_id);
        CREATE INDEX IF NOT EXISTS idx_edges_target ON edges(target_id);
        CREATE INDEX IF NOT EXISTS idx_concepts_title ON concepts(title);
    """)
    conn.commit()
    return conn


def add_concept(concept: Concept) -> str:
    """Add a concept node (upsert)."""
    conn = _get_conn()
    domain = concept.metadata.get("domain", "")

    conn.execute("""
        INSERT INTO concepts (id, type, title, domain, last_activated, activation_count)
        VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
            title = excluded.title,
            domain = excluded.domain,
            last_activated = excluded.last_activated,
            activation_count = excluded.activation_count
    """, (concept.id, concept.type.value, concept.title, domain,
          concept.last_activated, concept.activation_count))
    conn.commit()
    conn.close()
    return concept.id


def add_edge(source_id: str, target_id: str, relation: RelationType, weight: float = 1.0) -> bool:
    """Add an edge (upsert)."""
    conn = _get_conn()
    now = datetime.now().isoformat()

    conn.execute("""
        INSERT INTO edges (source_id, target_id, relation, weight, created_at, last_activated)
        VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(source_id, target_id, relation) DO UPDATE SET
            weight = excluded.weight,
            last_activated = excluded.last_activated
    """, (source_id, target_id, relation.value, weight, now, now))
    conn.commit()
    conn.close()
    return True


def get_concept(concept_id: str) -> Optional[Concept]:
    """Get a concept by ID."""
    conn = _get_conn()
    row = conn.execute("SELECT * FROM concepts WHERE id = ?", (concept_id,)).fetchone()
    conn.close()

    if not row:
        return None

    return Concept(
        id=row["id"],
        type=ConceptType(row["type"]),
        title=row["title"],
        content="",  # Fetch on demand
        last_activated=row["last_activated"],
        activation_count=row["activation_count"] or 0,
        metadata={"domain": row["domain"]} if row["domain"] else {},
    )


def get_neighbors(concept_id: str, direction: str = "both", limit: int = 20) -> List[Tuple[Concept, Edge]]:
    """Get connected concepts."""
    conn = _get_conn()
    results = []

    if direction in ("out", "both"):
        rows = conn.execute("""
            SELECT c.*, e.relation, e.weight, e.created_at, e.last_activated
            FROM edges e
            JOIN concepts c ON e.target_id = c.id
            WHERE e.source_id = ?
            ORDER BY e.weight DESC
            LIMIT ?
        """, (concept_id, limit)).fetchall()

        for row in rows:
            concept = Concept(
                id=row["id"], type=ConceptType(row["type"]), title=row["title"],
                last_activated=row["last_activated"],
                activation_count=row["activation_count"] or 0,
                metadata={"domain": row["domain"]} if row["domain"] else {},
            )
            edge = Edge(
                source_id=concept_id, target_id=row["id"],
                relation=RelationType(row["relation"]),
                weight=row["weight"] or 1.0,
                created_at=row["created_at"],
                last_activated=row[7],  # e.last_activated
            )
            results.append((concept, edge))

    if direction in ("in", "both"):
        rows = conn.execute("""
            SELECT c.*, e.relation, e.weight, e.created_at, e.last_activated
            FROM edges e
            JOIN concepts c ON e.source_id = c.id
            WHERE e.target_id = ?
            ORDER BY e.weight DESC
            LIMIT ?
        """, (concept_id, limit)).fetchall()

        for row in rows:
            concept = Concept(
                id=row["id"], type=ConceptType(row["type"]), title=row["title"],
                last_activated=row["last_activated"],
                activation_count=row["activation_count"] or 0,
                metadata={"domain": row["domain"]} if row["domain"] else {},
            )
            edge = Edge(
                source_id=row["id"], target_id=concept_id,
                relation=RelationType(row["relation"]),
                weight=row["weight"] or 1.0,
                created_at=row["created_at"],
                last_activated=row[7],
            )
            results.append((concept, edge))

    conn.close()
    return results


def search_concepts(query: str, limit: int = 10) -> List[Concept]:
    """Search concepts by title."""
    conn = _get_conn()
    rows = conn.execute("""
        SELECT * FROM concepts
        WHERE title LIKE ?
        ORDER BY activation_count DESC
        LIMIT ?
    """, (f"%{query}%", limit)).fetchall()
    conn.close()

    return [
        Concept(
            id=row["id"], type=ConceptType(row["type"]), title=row["title"],
            last_activated=row["last_activated"],
            activation_count=row["activation_count"] or 0,
            metadata={"domain": row["domain"]} if row["domain"] else {},
        )
        for row in rows
    ]


def spreading_activation(
    seed_ids: List[str],
    max_depth: int = 3,
    decay_factor: float = 0.5,
    threshold: float = 0.1,
    limit: int = 20,
) -> ActivationResult:
    """
    Brain-like spreading activation.

    Properties:
    1. Sparse activation - only seeds and connected nodes activate
    2. Distance decay - activation weakens with each hop (like signal attenuation)
    3. Temporal decay - stale connections are weaker (like synaptic pruning)
    4. Frequency boost - often-activated concepts fire easier (like potentiation)
    5. Competitive inhibition - limited slots (like attention bottleneck)
    """
    if not seed_ids:
        return ActivationResult([], [], [], [])

    conn = _get_conn()
    placeholders = ",".join("?" for _ in seed_ids)

    # Brain-like spreading with temporal decay and frequency boost
    rows = conn.execute(f"""
        WITH RECURSIVE spread(id, activation, depth, path) AS (
            -- Seeds: boost by sqrt(activation_count) to model potentiation
            SELECT
                id,
                1.0 * (1.0 + 0.1 * MIN(activation_count, 100)),
                0,
                id
            FROM concepts
            WHERE id IN ({placeholders})

            UNION ALL

            -- Spread with brain-like properties
            SELECT
                CASE WHEN e.source_id = s.id THEN e.target_id ELSE e.source_id END,
                s.activation * e.weight * ? * (
                    -- Temporal decay: connections not used in 30 days decay
                    CASE
                        WHEN e.last_activated IS NULL THEN 0.5
                        WHEN julianday('now') - julianday(e.last_activated) > 30 THEN 0.3
                        WHEN julianday('now') - julianday(e.last_activated) > 7 THEN 0.7
                        ELSE 1.0
                    END
                ),
                s.depth + 1,
                s.path || ' -> ' || CASE WHEN e.source_id = s.id THEN e.target_id ELSE e.source_id END
            FROM spread s
            JOIN edges e ON (e.source_id = s.id OR e.target_id = s.id)
            JOIN concepts c ON c.id = CASE WHEN e.source_id = s.id THEN e.target_id ELSE e.source_id END
            WHERE s.depth < ?
              AND s.activation * e.weight * ? >= ?
              -- Avoid loops
              AND s.path NOT LIKE '%' || (CASE WHEN e.source_id = s.id THEN e.target_id ELSE e.source_id END) || '%'
        )
        SELECT c.*, MAX(s.activation) as activation, MIN(s.depth) as depth, s.path
        FROM spread s
        JOIN concepts c ON s.id = c.id
        GROUP BY c.id
        ORDER BY activation DESC
        LIMIT ?
    """, (*seed_ids, decay_factor, max_depth, decay_factor, threshold, limit)).fetchall()

    # Update activation counts (Hebbian: fire together, wire together)
    now = datetime.now().isoformat()
    for row in rows:
        conn.execute("""
            UPDATE concepts
            SET activation_count = activation_count + 1, last_activated = ?
            WHERE id = ?
        """, (now, row["id"]))
    conn.commit()
    conn.close()

    primary = []
    spread_results = []
    paths = []

    for row in rows:
        concept = Concept(
            id=row["id"], type=ConceptType(row["type"]), title=row["title"],
            last_activated=row["last_activated"],
            activation_count=row["activation_count"] or 0,
            metadata={"domain": row["domain"]} if row["domain"] else {},
        )
        activation = row["activation"]
        path = row["path"]

        if row["id"] in seed_ids:
            primary.append((concept, activation))
        else:
            spread_results.append((concept, activation))
            if path:
                paths.append(path.split(" -> "))

    # Unexpected = high activation but distant (2+ hops) - serendipitous connections
    unexpected = []
    for concept, activation in spread_results:
        for path in paths:
            if concept.id in path and len(path) >= 3:
                unexpected.append((concept, activation))
                break

    return ActivationResult(
        primary=primary,
        spread=spread_results[:limit - len(primary)],
        paths=paths[:10],
        unexpected=unexpected[:5],
    )


def activate_from_prompt(prompt: str, limit: int = 10) -> ActivationResult:
    """Activate concepts relevant to a prompt."""
    words = prompt.lower().split()
    seeds = set()

    for word in words:
        if len(word) > 3:
            matches = search_concepts(word, limit=3)
            for m in matches:
                seeds.add(m.id)

    if not seeds:
        return ActivationResult([], [], [], [])

    return spreading_activation(list(seeds), max_depth=3, decay_factor=0.6, limit=limit)


def get_graph_stats() -> Dict:
    """Get graph statistics."""
    conn = _get_conn()
    nodes = conn.execute("SELECT COUNT(*) FROM concepts").fetchone()[0]
    edges = conn.execute("SELECT COUNT(*) FROM edges").fetchone()[0]
    conn.close()
    return {"nodes": nodes, "edges": edges}


def sync_wisdom_to_graph():
    """Sync wisdom entries to the concept graph."""
    from .wisdom import recall_wisdom
    from .vocabulary import get_vocabulary
    from .beliefs import get_beliefs

    # Sync wisdom
    wisdom_entries = recall_wisdom(limit=500)
    for entry in wisdom_entries:
        concept = Concept(
            id=f"wisdom_{entry.get('id', '')}",
            type=ConceptType.WISDOM,
            title=entry.get("title", "")[:100],
            metadata={"domain": entry.get("domain", "")},
        )
        add_concept(concept)

    # Sync vocabulary
    vocab = get_vocabulary()
    for term, meaning in vocab.items():
        concept = Concept(
            id=f"term_{term}",
            type=ConceptType.TERM,
            title=term,
        )
        add_concept(concept)

    # Sync beliefs
    beliefs = get_beliefs()
    for belief in beliefs:
        concept = Concept(
            id=f"belief_{belief.get('id', '')}",
            type=ConceptType.BELIEF,
            title=belief.get("belief", "")[:100],
        )
        add_concept(concept)

    # Auto-link related concepts
    _auto_link_concepts()

    stats = get_graph_stats()
    return f"Synced {stats['nodes']} concepts with {stats['edges']} relationships"


def _auto_link_concepts():
    """Create edges between semantically related concepts based on title similarity."""
    conn = _get_conn()
    concepts = conn.execute("SELECT id, title FROM concepts").fetchall()

    # Simple word-based similarity
    for i, c1 in enumerate(concepts):
        words1 = set(c1["title"].lower().split())
        for c2 in concepts[i+1:]:
            words2 = set(c2["title"].lower().split())
            overlap = words1 & words2
            # Link if they share significant words
            significant = {w for w in overlap if len(w) > 4}
            if significant:
                weight = len(significant) / max(len(words1), len(words2))
                if weight >= 0.2:
                    add_edge(c1["id"], c2["id"], RelationType.RELATED_TO, weight)

    conn.close()


def get_concept_content(concept_id: str) -> str:
    """Fetch content from authoritative source (not graph)."""
    from .wisdom import get_wisdom_by_id

    if concept_id.startswith("wisdom_"):
        wisdom_id = concept_id[7:]
        wisdom = get_wisdom_by_id(wisdom_id)
        return wisdom.get("content", "") if wisdom else ""
    elif concept_id.startswith("belief_"):
        from .beliefs import get_belief_by_id
        belief_id = concept_id[7:]
        belief = get_belief_by_id(belief_id)
        return belief.get("belief", "") if belief else ""
    elif concept_id.startswith("term_"):
        from .vocabulary import get_vocabulary
        term = concept_id[5:]
        vocab = get_vocabulary()
        return vocab.get(term, "")
    return ""


def rebuild_graph():
    """Rebuild the graph from scratch."""
    conn = _get_conn()
    conn.execute("DELETE FROM edges")
    conn.execute("DELETE FROM concepts")
    conn.commit()
    conn.close()
    return sync_wisdom_to_graph()


def strengthen_connection(source_id: str, target_id: str, amount: float = 0.1):
    """
    Hebbian learning: strengthen connections between co-activated concepts.

    'Neurons that fire together, wire together.'
    """
    conn = _get_conn()
    now = datetime.now().isoformat()

    # Update existing edge or create new one
    conn.execute("""
        INSERT INTO edges (source_id, target_id, relation, weight, created_at, last_activated)
        VALUES (?, ?, 'used_with', ?, ?, ?)
        ON CONFLICT(source_id, target_id, relation) DO UPDATE SET
            weight = MIN(edges.weight + ?, 2.0),
            last_activated = ?
    """, (source_id, target_id, amount, now, now, amount, now))
    conn.commit()
    conn.close()


def weaken_unused_connections(days_threshold: int = 60, decay: float = 0.1):
    """
    Synaptic pruning: weaken connections not used in a while.

    Connections that aren't reinforced decay over time.
    """
    conn = _get_conn()

    conn.execute("""
        UPDATE edges
        SET weight = MAX(weight - ?, 0.1)
        WHERE julianday('now') - julianday(last_activated) > ?
    """, (decay, days_threshold))

    # Prune very weak connections
    conn.execute("DELETE FROM edges WHERE weight < 0.15")
    conn.commit()
    conn.close()


def co_activate(concept_ids: List[str]):
    """
    Record co-activation of concepts (Hebbian learning).

    When concepts fire together, strengthen their connections.
    """
    for i, id1 in enumerate(concept_ids):
        for id2 in concept_ids[i+1:]:
            strengthen_connection(id1, id2, amount=0.05)


def format_activation_for_context(result: ActivationResult, include_content: bool = False) -> str:
    """Format activation result for context injection."""
    lines = []

    if result.primary:
        lines.append("**Directly activated:**")
        for concept, score in result.primary[:5]:
            content = ""
            if include_content:
                content = f": {get_concept_content(concept.id)[:100]}..."
            lines.append(f"- [{concept.type.value}] {concept.title} ({score:.2f}){content}")

    if result.spread:
        lines.append("\n**Spread activation:**")
        for concept, score in result.spread[:5]:
            lines.append(f"- {concept.title} ({score:.2f})")

    if result.unexpected:
        lines.append("\n**Unexpected connections:**")
        for concept, score in result.unexpected[:3]:
            lines.append(f"- {concept.title} (serendipity: {score:.2f})")

    return "\n".join(lines) if lines else "No activations"


# Compatibility aliases for graph.py migration
GRAPH_AVAILABLE = True  # Always available (pure SQLite)
