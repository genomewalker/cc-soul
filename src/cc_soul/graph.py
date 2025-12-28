"""
Concept Graph: Ideas connect in webs, not isolated rows.

Uses Kuzu embedded graph database for native graph operations:
- Nodes: Concepts (wisdom, beliefs, vocabulary, files, decisions)
- Edges: Relationships with weights and decay
- Spreading activation: When one concept activates, related ones surface

Install: pip install cc-soul[graph]
"""

import json
import math
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from enum import Enum
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple, Any

from .core import SOUL_DIR, init_soul

# Kuzu is optional
try:
    import kuzu

    KUZU_AVAILABLE = True
except ImportError:
    KUZU_AVAILABLE = False


class ConceptType(str, Enum):
    WISDOM = "wisdom"
    BELIEF = "belief"
    TERM = "term"
    FILE = "file"
    DECISION = "decision"
    PATTERN = "pattern"
    FAILURE = "failure"


class RelationType(str, Enum):
    RELATED_TO = "related_to"  # General semantic similarity
    LED_TO = "led_to"  # Causal: A led to discovering B
    CONTRADICTS = "contradicts"  # Tension between concepts
    EVOLVED_FROM = "evolved_from"  # B is refinement of A
    REMINDED_BY = "reminded_by"  # Association: A reminds of B
    USED_WITH = "used_with"  # Co-occurrence in same context
    REQUIRES = "requires"  # Dependency: A needs B


@dataclass
class Concept:
    """A node in the concept graph."""

    id: str
    type: ConceptType
    title: str
    content: str
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
    evidence: str = ""


@dataclass
class ActivationResult:
    """Result of spreading activation."""

    primary: List[Tuple[Concept, float]]  # Direct matches with scores
    spread: List[Tuple[Concept, float]]  # Activated by spread
    paths: List[List[str]]  # How concepts connected
    unexpected: List[Tuple[Concept, float]]  # Surprising connections


GRAPH_DIR = SOUL_DIR / "graph"


def _ensure_graph_db():
    """Initialize Kuzu graph database."""
    if not KUZU_AVAILABLE:
        raise RuntimeError(
            "Kuzu not installed. Install with: pip install cc-soul[graph]"
        )

    GRAPH_DIR.mkdir(parents=True, exist_ok=True)
    db_path = str(GRAPH_DIR / "concepts")

    db = kuzu.Database(db_path)
    conn = kuzu.Connection(db)

    # Create schema if not exists
    try:
        conn.execute("""
            CREATE NODE TABLE IF NOT EXISTS Concept(
                id STRING PRIMARY KEY,
                type STRING,
                title STRING,
                content STRING,
                created_at STRING,
                last_activated STRING,
                activation_count INT64,
                metadata STRING
            )
        """)

        conn.execute("""
            CREATE REL TABLE IF NOT EXISTS RELATES(
                FROM Concept TO Concept,
                relation STRING,
                weight DOUBLE,
                created_at STRING,
                last_activated STRING,
                evidence STRING
            )
        """)
    except Exception:
        # Tables already exist
        pass

    return db, conn


def get_graph_connection():
    """Get a connection to the graph database."""
    if not KUZU_AVAILABLE:
        return None, None

    GRAPH_DIR.mkdir(parents=True, exist_ok=True)
    db_path = str(GRAPH_DIR / "concepts")

    db = kuzu.Database(db_path)
    conn = kuzu.Connection(db)
    return db, conn


def add_concept(concept: Concept) -> str:
    """Add a concept node to the graph."""
    db, conn = _ensure_graph_db()

    # Check if exists
    result = conn.execute(f"MATCH (c:Concept {{id: '{concept.id}'}}) RETURN c.id")
    if result.has_next():
        # Update existing
        conn.execute(
            f"""
            MATCH (c:Concept {{id: '{concept.id}'}})
            SET c.title = $title,
                c.content = $content,
                c.last_activated = $last_activated,
                c.activation_count = c.activation_count + 1
        """,
            {
                "title": concept.title,
                "content": concept.content,
                "last_activated": datetime.now().isoformat(),
            },
        )
    else:
        # Insert new
        conn.execute(
            """
            CREATE (c:Concept {
                id: $id,
                type: $type,
                title: $title,
                content: $content,
                created_at: $created_at,
                last_activated: $last_activated,
                activation_count: $activation_count,
                metadata: $metadata
            })
        """,
            {
                "id": concept.id,
                "type": concept.type.value,
                "title": concept.title,
                "content": concept.content,
                "created_at": concept.created_at,
                "last_activated": concept.last_activated or concept.created_at,
                "activation_count": concept.activation_count,
                "metadata": json.dumps(concept.metadata),
            },
        )

    return concept.id


def add_edge(edge: Edge) -> bool:
    """Add a relationship between concepts."""
    db, conn = _ensure_graph_db()

    # Check if both concepts exist
    for cid in [edge.source_id, edge.target_id]:
        result = conn.execute(f"MATCH (c:Concept {{id: '{cid}'}}) RETURN c.id")
        if not result.has_next():
            return False

    # Check if edge exists
    result = conn.execute(f"""
        MATCH (a:Concept {{id: '{edge.source_id}'}})-[r:RELATES]->(b:Concept {{id: '{edge.target_id}'}})
        WHERE r.relation = '{edge.relation.value}'
        RETURN r.weight
    """)

    if result.has_next():
        # Strengthen existing edge
        conn.execute(f"""
            MATCH (a:Concept {{id: '{edge.source_id}'}})-[r:RELATES]->(b:Concept {{id: '{edge.target_id}'}})
            WHERE r.relation = '{edge.relation.value}'
            SET r.weight = r.weight + 0.1,
                r.last_activated = '{datetime.now().isoformat()}'
        """)
    else:
        # Create new edge
        conn.execute(f"""
            MATCH (a:Concept {{id: '{edge.source_id}'}}), (b:Concept {{id: '{edge.target_id}'}})
            CREATE (a)-[r:RELATES {{
                relation: '{edge.relation.value}',
                weight: {edge.weight},
                created_at: '{edge.created_at}',
                last_activated: '{edge.last_activated or edge.created_at}',
                evidence: '{edge.evidence}'
            }}]->(b)
        """)

    return True


def link_concepts(
    source_id: str,
    target_id: str,
    relation: RelationType,
    weight: float = 1.0,
    evidence: str = "",
) -> bool:
    """Convenience function to link two concepts."""
    edge = Edge(
        source_id=source_id,
        target_id=target_id,
        relation=relation,
        weight=weight,
        evidence=evidence,
    )
    return add_edge(edge)


def get_concept(concept_id: str) -> Optional[Concept]:
    """Get a concept by ID."""
    db, conn = get_graph_connection()
    if not conn:
        return None

    result = conn.execute(f"""
        MATCH (c:Concept {{id: '{concept_id}'}})
        RETURN c.id, c.type, c.title, c.content, c.created_at,
               c.last_activated, c.activation_count, c.metadata
    """)

    if result.has_next():
        row = result.get_next()
        return Concept(
            id=row[0],
            type=ConceptType(row[1]),
            title=row[2],
            content=row[3],
            created_at=row[4],
            last_activated=row[5],
            activation_count=row[6],
            metadata=json.loads(row[7]) if row[7] else {},
        )
    return None


def get_neighbors(
    concept_id: str,
    relation: RelationType = None,
    direction: str = "both",
    limit: int = 20,
) -> List[Tuple[Concept, Edge]]:
    """Get concepts connected to this one."""
    db, conn = get_graph_connection()
    if not conn:
        return []

    relation_filter = f"AND r.relation = '{relation.value}'" if relation else ""

    results = []

    # Outgoing edges
    if direction in ("out", "both"):
        query = f"""
            MATCH (a:Concept {{id: '{concept_id}'}})-[r:RELATES]->(b:Concept)
            WHERE 1=1 {relation_filter}
            RETURN b.id, b.type, b.title, b.content, b.created_at,
                   b.last_activated, b.activation_count, b.metadata,
                   r.relation, r.weight, r.created_at, r.last_activated, r.evidence
            ORDER BY r.weight DESC
            LIMIT {limit}
        """
        result = conn.execute(query)
        while result.has_next():
            row = result.get_next()
            concept = Concept(
                id=row[0],
                type=ConceptType(row[1]),
                title=row[2],
                content=row[3],
                created_at=row[4],
                last_activated=row[5],
                activation_count=row[6],
                metadata=json.loads(row[7]) if row[7] else {},
            )
            edge = Edge(
                source_id=concept_id,
                target_id=row[0],
                relation=RelationType(row[8]),
                weight=row[9],
                created_at=row[10],
                last_activated=row[11],
                evidence=row[12] or "",
            )
            results.append((concept, edge))

    # Incoming edges
    if direction in ("in", "both"):
        query = f"""
            MATCH (a:Concept)-[r:RELATES]->(b:Concept {{id: '{concept_id}'}})
            WHERE 1=1 {relation_filter}
            RETURN a.id, a.type, a.title, a.content, a.created_at,
                   a.last_activated, a.activation_count, a.metadata,
                   r.relation, r.weight, r.created_at, r.last_activated, r.evidence
            ORDER BY r.weight DESC
            LIMIT {limit}
        """
        result = conn.execute(query)
        while result.has_next():
            row = result.get_next()
            concept = Concept(
                id=row[0],
                type=ConceptType(row[1]),
                title=row[2],
                content=row[3],
                created_at=row[4],
                last_activated=row[5],
                activation_count=row[6],
                metadata=json.loads(row[7]) if row[7] else {},
            )
            edge = Edge(
                source_id=row[0],
                target_id=concept_id,
                relation=RelationType(row[8]),
                weight=row[9],
                created_at=row[10],
                last_activated=row[11],
                evidence=row[12] or "",
            )
            results.append((concept, edge))

    return results


def search_concepts(
    query: str, concept_type: ConceptType = None, limit: int = 10
) -> List[Concept]:
    """Search concepts by title or content."""
    db, conn = get_graph_connection()
    if not conn:
        return []

    type_filter = f"AND c.type = '{concept_type.value}'" if concept_type else ""
    query_lower = query.lower()

    result = conn.execute(f"""
        MATCH (c:Concept)
        WHERE (toLower(c.title) CONTAINS '{query_lower}'
               OR toLower(c.content) CONTAINS '{query_lower}')
        {type_filter}
        RETURN c.id, c.type, c.title, c.content, c.created_at,
               c.last_activated, c.activation_count, c.metadata
        LIMIT {limit}
    """)

    concepts = []
    while result.has_next():
        row = result.get_next()
        concepts.append(
            Concept(
                id=row[0],
                type=ConceptType(row[1]),
                title=row[2],
                content=row[3],
                created_at=row[4],
                last_activated=row[5],
                activation_count=row[6],
                metadata=json.loads(row[7]) if row[7] else {},
            )
        )

    return concepts


def spreading_activation(
    seed_ids: List[str],
    max_depth: int = 3,
    decay_factor: float = 0.5,
    threshold: float = 0.1,
    limit: int = 20,
) -> ActivationResult:
    """
    Spreading activation from seed concepts.

    Activation spreads outward from seeds, decaying with distance.
    This surfaces related concepts that might not be directly matched
    but are connected through the graph.

    Args:
        seed_ids: Starting concept IDs
        max_depth: How many hops to spread
        decay_factor: Activation multiplier per hop (0.5 = halve each step)
        threshold: Minimum activation to include
        limit: Maximum concepts to return

    Returns:
        ActivationResult with primary, spread, and unexpected concepts
    """
    db, conn = get_graph_connection()
    if not conn:
        return ActivationResult([], [], [], [])

    # Track activation levels
    activation: Dict[str, float] = {}
    paths: Dict[str, List[str]] = {}
    visited: Set[str] = set()

    # Initialize seeds with full activation
    for seed_id in seed_ids:
        activation[seed_id] = 1.0
        paths[seed_id] = [seed_id]

    # Spread activation
    current_layer = set(seed_ids)

    for depth in range(max_depth):
        next_layer = set()
        current_decay = decay_factor ** (depth + 1)

        for node_id in current_layer:
            if node_id in visited:
                continue
            visited.add(node_id)

            node_activation = activation.get(node_id, 0)
            if node_activation < threshold:
                continue

            # Get neighbors
            neighbors = get_neighbors(node_id, direction="both", limit=10)

            for neighbor, edge in neighbors:
                # Calculate spread activation
                spread = node_activation * edge.weight * current_decay

                # Time decay: reduce activation for stale edges
                if edge.last_activated:
                    try:
                        last_act = datetime.fromisoformat(edge.last_activated)
                        days_ago = (datetime.now() - last_act).days
                        time_decay = 0.95 ** (days_ago / 30)  # Decay per month
                        spread *= time_decay
                    except (ValueError, TypeError):
                        pass

                if spread >= threshold:
                    if neighbor.id not in activation:
                        activation[neighbor.id] = 0
                        paths[neighbor.id] = paths[node_id] + [neighbor.id]

                    activation[neighbor.id] = max(activation[neighbor.id], spread)
                    next_layer.add(neighbor.id)

        current_layer = next_layer

    # Collect results
    primary = []
    spread_results = []

    for concept_id, score in sorted(activation.items(), key=lambda x: -x[1])[:limit]:
        concept = get_concept(concept_id)
        if not concept:
            continue

        if concept_id in seed_ids:
            primary.append((concept, score))
        else:
            spread_results.append((concept, score))

    # Find unexpected connections (high activation but semantically distant)
    unexpected = []
    for concept, score in spread_results:
        path = paths.get(concept.id, [])
        if len(path) >= 3:  # At least 2 hops away
            unexpected.append((concept, score))

    return ActivationResult(
        primary=primary,
        spread=spread_results[: limit - len(primary)],
        paths=[paths[c.id] for c, _ in spread_results if c.id in paths],
        unexpected=unexpected[:5],
    )


def activate_from_prompt(prompt: str, limit: int = 10) -> ActivationResult:
    """
    Activate concepts relevant to a prompt.

    First searches for matching concepts, then spreads activation
    to find related ones.
    """
    # Find seed concepts by keyword matching
    words = prompt.lower().split()
    seeds = set()

    for word in words:
        if len(word) > 3:  # Skip short words
            matches = search_concepts(word, limit=3)
            for m in matches:
                seeds.add(m.id)

    if not seeds:
        return ActivationResult([], [], [], [])

    return spreading_activation(
        seed_ids=list(seeds), max_depth=3, decay_factor=0.6, limit=limit
    )


def format_activation_result(result: ActivationResult) -> str:
    """Format activation result for display."""
    lines = []

    if result.primary:
        lines.append("## Direct Matches")
        for concept, score in result.primary[:5]:
            lines.append(f"  [{score:.0%}] {concept.title}")

    if result.spread:
        lines.append("\n## Connected Concepts")
        for concept, score in result.spread[:5]:
            lines.append(f"  [{score:.0%}] {concept.title} ({concept.type.value})")

    if result.unexpected:
        lines.append("\n## Unexpected Connections")
        for concept, score in result.unexpected[:3]:
            lines.append(f"  [{score:.0%}] {concept.title}")

    return "\n".join(lines) if lines else "No concepts activated"


def get_graph_stats() -> Dict:
    """Get statistics about the concept graph."""
    try:
        db, conn = _ensure_graph_db()
    except RuntimeError:
        return {"available": False}

    result = conn.execute("MATCH (c:Concept) RETURN count(c)")
    node_count = result.get_next()[0] if result.has_next() else 0

    result = conn.execute("MATCH ()-[r:RELATES]->() RETURN count(r)")
    edge_count = result.get_next()[0] if result.has_next() else 0

    result = conn.execute("""
        MATCH (c:Concept)
        RETURN c.type, count(c)
        ORDER BY count(c) DESC
    """)
    by_type = {}
    while result.has_next():
        row = result.get_next()
        by_type[row[0]] = row[1]

    result = conn.execute("""
        MATCH ()-[r:RELATES]->()
        RETURN r.relation, count(r)
        ORDER BY count(r) DESC
    """)
    by_relation = {}
    while result.has_next():
        row = result.get_next()
        by_relation[row[0]] = row[1]

    return {
        "available": True,
        "nodes": node_count,
        "edges": edge_count,
        "by_type": by_type,
        "by_relation": by_relation,
    }


# =============================================================================
# SYNC WITH EXISTING SOUL DATA
# =============================================================================


def sync_wisdom_to_graph():
    """
    Sync existing wisdom entries to the concept graph.

    Creates concept nodes for each wisdom entry and infers
    relationships based on domain and keyword overlap.
    """
    from .wisdom import recall_wisdom
    from .vocabulary import get_vocabulary
    from .beliefs import get_beliefs

    db, conn = _ensure_graph_db()

    # Sync wisdom
    wisdom_entries = recall_wisdom(limit=1000)
    for w in wisdom_entries:
        concept = Concept(
            id=f"wisdom_{w['id']}",
            type=ConceptType.WISDOM if w["type"] != "failure" else ConceptType.FAILURE,
            title=w["title"],
            content=w["content"],
            metadata={"domain": w.get("domain"), "confidence": w.get("confidence")},
        )
        add_concept(concept)

    # Sync vocabulary
    vocab = get_vocabulary()
    for term, meaning in vocab.items():
        concept = Concept(
            id=f"term_{term}", type=ConceptType.TERM, title=term, content=meaning
        )
        add_concept(concept)

    # Sync beliefs
    beliefs = get_beliefs()
    for b in beliefs:
        concept = Concept(
            id=f"belief_{b['id']}",
            type=ConceptType.BELIEF,
            title=b["belief"][:50],
            content=b["belief"],
        )
        add_concept(concept)

    # Get fresh connection for inference (original may be stale after add_concept calls)
    _, fresh_conn = _ensure_graph_db()
    _infer_relationships(fresh_conn)

    return get_graph_stats()


def _infer_relationships(conn):
    """Infer relationships between concepts based on content similarity."""
    # Get all concepts
    result = conn.execute("""
        MATCH (c:Concept)
        RETURN c.id, c.title, c.content, c.type
    """)

    concepts = []
    while result.has_next():
        row = result.get_next()
        concepts.append(
            {
                "id": row[0],
                "title": row[1],
                "content": row[2],
                "type": row[3],
                "words": set((row[1] + " " + row[2]).lower().split()),
            }
        )

    # Find overlaps
    for i, c1 in enumerate(concepts):
        for c2 in concepts[i + 1 :]:
            overlap = len(c1["words"] & c2["words"])
            union = len(c1["words"] | c2["words"])

            if union > 0:
                jaccard = overlap / union
                if jaccard > 0.2:  # >20% word overlap
                    link_concepts(
                        c1["id"],
                        c2["id"],
                        RelationType.RELATED_TO,
                        weight=jaccard,
                        evidence=f"Keyword overlap: {jaccard:.0%}",
                    )


def auto_link_new_concept(concept_id: str):
    """Automatically link a new concept to related existing ones."""
    concept = get_concept(concept_id)
    if not concept:
        return

    words = set((concept.title + " " + concept.content).lower().split())

    # Search for related concepts
    for word in list(words)[:10]:  # Limit to avoid too many searches
        if len(word) > 4:
            matches = search_concepts(word, limit=5)
            for match in matches:
                if match.id != concept_id:
                    # Calculate similarity
                    match_words = set(
                        (match.title + " " + match.content).lower().split()
                    )
                    overlap = len(words & match_words)
                    if overlap >= 2:
                        link_concepts(
                            concept_id,
                            match.id,
                            RelationType.RELATED_TO,
                            weight=min(overlap * 0.1, 1.0),
                        )
