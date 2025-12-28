"""
Semantic vector search for Soul using LanceDB.

Provides embedding-based retrieval for wisdom entries,
enabling queries like "find wisdom relevant to this problem".
"""

from typing import List, Dict
import numpy as np

from .core import SOUL_DIR, get_db_connection

LANCE_DIR = SOUL_DIR / "vectors" / "lancedb"
MODEL_NAME = "all-MiniLM-L6-v2"

_model = None
_db = None


def _get_model():
    """Lazy-load the embedding model."""
    global _model
    if _model is None:
        from sentence_transformers import SentenceTransformer

        _model = SentenceTransformer(MODEL_NAME)
    return _model


def _get_db():
    """Lazy-load the LanceDB connection."""
    global _db
    if _db is None:
        import lancedb

        LANCE_DIR.mkdir(parents=True, exist_ok=True)
        _db = lancedb.connect(str(LANCE_DIR))
    return _db


def embed_text(text: str) -> np.ndarray:
    """Generate embedding for a text string."""
    model = _get_model()
    return model.encode(text, convert_to_numpy=True)


def embed_texts(texts: List[str]) -> np.ndarray:
    """Generate embeddings for multiple texts (batched)."""
    model = _get_model()
    return model.encode(texts, convert_to_numpy=True)


def init_wisdom_table():
    """Initialize or get the wisdom vectors table."""
    db = _get_db()

    try:
        return db.open_table("wisdom")
    except Exception:
        import pyarrow as pa

        schema = pa.schema(
            [
                pa.field("id", pa.string()),
                pa.field("title", pa.string()),
                pa.field("content", pa.string()),
                pa.field("type", pa.string()),
                pa.field("domain", pa.string()),
                pa.field("vector", pa.list_(pa.float32(), 384)),
            ]
        )
        return db.create_table("wisdom", schema=schema)


def index_wisdom(
    wisdom_id: str, title: str, content: str, wisdom_type: str, domain: str = None
):
    """Add or update a wisdom entry in the vector index."""
    table = init_wisdom_table()

    text = f"{title}: {content}"
    vector = embed_text(text)

    data = [
        {
            "id": wisdom_id,
            "title": title,
            "content": content,
            "type": wisdom_type,
            "domain": domain or "",
            "vector": vector.tolist(),
        }
    ]

    try:
        existing = table.search().where(f"id = '{wisdom_id}'").to_list()
        if existing:
            table.delete(f"id = '{wisdom_id}'")
    except Exception:
        pass

    table.add(data)


def search_wisdom(
    query: str, limit: int = 5, domain: str = None, wisdom_type: str = None
) -> List[Dict]:
    """
    Semantic search for relevant wisdom.

    Returns wisdom entries ranked by similarity to the query.
    """
    try:
        table = init_wisdom_table()
    except Exception:
        return []

    query_vector = embed_text(query)
    search = table.search(query_vector).limit(limit)

    if domain and wisdom_type:
        search = search.where(f"domain = '{domain}' AND type = '{wisdom_type}'")
    elif domain:
        search = search.where(f"domain = '{domain}'")
    elif wisdom_type:
        search = search.where(f"type = '{wisdom_type}'")

    results = search.to_list()

    return [
        {
            "id": r["id"],
            "title": r["title"],
            "content": r["content"],
            "type": r["type"],
            "domain": r["domain"],
            "score": float(1 - r["_distance"]),
        }
        for r in results
    ]


def reindex_all_wisdom():
    """Reindex all wisdom from SQLite into LanceDB."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute("SELECT id, type, title, content, domain FROM wisdom")
    rows = c.fetchall()
    conn.close()

    if not rows:
        print("No wisdom to index")
        return

    db = _get_db()
    try:
        db.drop_table("wisdom")
    except Exception:
        pass

    import pyarrow as pa

    schema = pa.schema(
        [
            pa.field("id", pa.string()),
            pa.field("title", pa.string()),
            pa.field("content", pa.string()),
            pa.field("type", pa.string()),
            pa.field("domain", pa.string()),
            pa.field("vector", pa.list_(pa.float32(), 384)),
        ]
    )
    table = db.create_table("wisdom", schema=schema)

    texts = [f"{row[2]}: {row[3]}" for row in rows]
    vectors = embed_texts(texts)

    data = [
        {
            "id": row[0],
            "title": row[2],
            "content": row[3],
            "type": row[1],
            "domain": row[4] or "",
            "vector": vectors[i].tolist(),
        }
        for i, row in enumerate(rows)
    ]

    table.add(data)
    print(f"Indexed {len(data)} wisdom entries")
