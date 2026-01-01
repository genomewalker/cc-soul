#!/usr/bin/env python3
"""
Migrate claude-mem observations to cc-memory and LanceDB.

claude-mem stores:
  - Observations in ~/.claude-mem/claude-mem.db (SQLite)
  - Vectors in ~/.claude-mem/vector-db/chroma.sqlite3 (Chroma)

cc-soul stores:
  - Project observations in .cc-memory/memory.db (SQLite)
  - Universal wisdom in ~/.claude/mind/soul.db (SQLite)
  - Vectors in ~/.claude/mind/vectors/lancedb (LanceDB)

This script:
1. Reads all observations from claude-mem
2. Groups by project
3. Imports into cc-memory (project) or cc-soul (universal wisdom)
4. Re-embeds into LanceDB for semantic search
"""

import sqlite3
import json
from pathlib import Path
from datetime import datetime
import sys

CLAUDE_MEM_DB = Path.home() / ".claude-mem" / "claude-mem.db"
SOUL_DIR = Path.home() / ".claude" / "mind"
LANCE_DIR = SOUL_DIR / "vectors" / "lancedb"
MODEL_NAME = "all-MiniLM-L6-v2"


def get_claude_mem_stats():
    """Get statistics from claude-mem database."""
    conn = sqlite3.connect(CLAUDE_MEM_DB)
    conn.row_factory = sqlite3.Row
    cur = conn.cursor()

    cur.execute("SELECT COUNT(*) FROM observations")
    total = cur.fetchone()[0]

    cur.execute("SELECT project, COUNT(*) as cnt FROM observations GROUP BY project ORDER BY cnt DESC")
    by_project = [(row['project'], row['cnt']) for row in cur.fetchall()]

    cur.execute("SELECT type, COUNT(*) as cnt FROM observations GROUP BY type ORDER BY cnt DESC")
    by_type = [(row['type'], row['cnt']) for row in cur.fetchall()]

    conn.close()
    return total, by_project, by_type


def export_observations(project_filter=None, limit=None):
    """Export observations from claude-mem."""
    conn = sqlite3.connect(CLAUDE_MEM_DB)
    conn.row_factory = sqlite3.Row
    cur = conn.cursor()

    query = "SELECT * FROM observations"
    params = []

    if project_filter:
        query += " WHERE project = ?"
        params.append(project_filter)

    query += " ORDER BY created_at_epoch ASC"

    if limit:
        query += f" LIMIT {limit}"

    cur.execute(query, params)
    rows = cur.fetchall()
    conn.close()

    return [dict(row) for row in rows]


def transform_to_cc_memory(obs):
    """Transform claude-mem observation to cc-memory format."""
    # Build content from available fields
    content_parts = []

    if obs.get('subtitle'):
        content_parts.append(obs['subtitle'])

    if obs.get('narrative'):
        content_parts.append(obs['narrative'])

    if obs.get('facts'):
        try:
            facts = json.loads(obs['facts'])
            if facts:
                content_parts.append("\n**Facts:**")
                for fact in facts:
                    content_parts.append(f"- {fact}")
        except (json.JSONDecodeError, TypeError):
            if obs['facts']:
                content_parts.append(obs['facts'])

    # Add file context
    files_read = []
    files_modified = []

    if obs.get('files_read'):
        try:
            files_read = json.loads(obs['files_read'])
        except (json.JSONDecodeError, TypeError):
            pass

    if obs.get('files_modified'):
        try:
            files_modified = json.loads(obs['files_modified'])
        except (json.JSONDecodeError, TypeError):
            pass

    if files_read or files_modified:
        content_parts.append("\n**Files:**")
        for f in files_read:
            content_parts.append(f"- Read: {f}")
        for f in files_modified:
            content_parts.append(f"- Modified: {f}")

    content = "\n".join(content_parts) if content_parts else obs.get('text', '')

    # Extract tags from concepts
    tags = ["migrated-from-claude-mem"]
    if obs.get('concepts'):
        try:
            concepts = json.loads(obs['concepts'])
            tags.extend(concepts)
        except (json.JSONDecodeError, TypeError):
            pass

    # Map type
    category = obs.get('type', 'discovery')

    return {
        'category': category,
        'title': obs.get('title', 'Untitled'),
        'content': content,
        'tags': tags,
        'tokens_read': obs.get('discovery_tokens', 0),
        'tokens_work': 0,
        'source_id': f"claude-mem:{obs['id']}",
        'created_at': obs.get('created_at'),
        'project': obs.get('project', 'unknown'),
    }


def import_to_cc_memory(observations, project_dir, dry_run=True):
    """Import observations to cc-memory database."""
    db_path = Path(project_dir) / ".cc-memory" / "memory.db"

    if not db_path.parent.exists():
        if dry_run:
            print(f"  Would create: {db_path.parent}")
        else:
            db_path.parent.mkdir(parents=True, exist_ok=True)

    if dry_run:
        print(f"  Would import {len(observations)} observations to {db_path}")
        return len(observations)

    conn = sqlite3.connect(db_path)
    cur = conn.cursor()

    # Ensure table exists (cc-memory schema)
    cur.execute('''
        CREATE TABLE IF NOT EXISTS observations (
            id TEXT PRIMARY KEY,
            category TEXT NOT NULL,
            title TEXT NOT NULL,
            content TEXT NOT NULL,
            tags TEXT,
            tokens_read INTEGER DEFAULT 0,
            tokens_work INTEGER DEFAULT 0,
            timestamp TEXT NOT NULL
        )
    ''')

    imported = 0
    skipped = 0

    for obs in observations:
        obs_id = obs['source_id']

        # Check if already imported
        cur.execute("SELECT id FROM observations WHERE id = ?", (obs_id,))
        if cur.fetchone():
            skipped += 1
            continue

        cur.execute('''
            INSERT INTO observations (id, category, title, content, tags, tokens_read, tokens_work, timestamp)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ''', (
            obs_id,
            obs['category'],
            obs['title'],
            obs['content'],
            json.dumps(obs['tags']),
            obs['tokens_read'],
            obs['tokens_work'],
            obs['created_at'],
        ))
        imported += 1

    conn.commit()
    conn.close()

    return imported, skipped


def index_to_lancedb(observations, batch_size=100, dry_run=True):
    """Index observations into LanceDB for semantic search."""
    if dry_run:
        print(f"  Would index {len(observations)} observations to LanceDB")
        return len(observations)

    try:
        import lancedb
        import pyarrow as pa
        from sentence_transformers import SentenceTransformer
    except ImportError as e:
        print(f"Error: Missing dependencies for LanceDB indexing: {e}")
        print("Install with: pip install lancedb sentence-transformers pyarrow")
        return 0

    print(f"Loading embedding model ({MODEL_NAME})...")
    model = SentenceTransformer(MODEL_NAME)

    LANCE_DIR.mkdir(parents=True, exist_ok=True)
    db = lancedb.connect(str(LANCE_DIR))

    schema = pa.schema([
        pa.field("id", pa.string()),
        pa.field("title", pa.string()),
        pa.field("content", pa.string()),
        pa.field("type", pa.string()),
        pa.field("project", pa.string()),
        pa.field("vector", pa.list_(pa.float32(), 384)),
    ])

    try:
        table = db.open_table("observations")
    except Exception:
        table = db.create_table("observations", schema=schema)

    indexed = 0
    for i in range(0, len(observations), batch_size):
        batch = observations[i:i + batch_size]
        texts = [f"{obs['title']}: {obs['content'][:500]}" for obs in batch]
        vectors = model.encode(texts, convert_to_numpy=True)

        data = [{
            "id": obs['source_id'],
            "title": obs['title'],
            "content": obs['content'][:1000],
            "type": obs['category'],
            "project": obs['project'],
            "vector": vectors[j].tolist(),
        } for j, obs in enumerate(batch)]

        table.add(data)
        indexed += len(batch)
        print(f"  Indexed {indexed}/{len(observations)}")

    return indexed


def promote_to_soul_wisdom(observations, min_confidence=0.7, dry_run=True):
    """Promote high-value observations to cc-soul universal wisdom."""
    soul_db = SOUL_DIR / "soul.db"

    if not soul_db.exists():
        print(f"  Soul database not found at {soul_db}")
        return 0

    candidates = [obs for obs in observations if obs['category'] in ('decision', 'discovery')]

    if dry_run:
        print(f"  Would promote {len(candidates)} observations to soul wisdom")
        return len(candidates)

    conn = sqlite3.connect(soul_db)
    cur = conn.cursor()

    promoted = 0
    for obs in candidates:
        wisdom_id = f"migrated:{obs['source_id']}"

        cur.execute("SELECT id FROM wisdom WHERE id = ?", (wisdom_id,))
        if cur.fetchone():
            continue

        cur.execute('''
            INSERT INTO wisdom (id, type, title, content, domain, source_project, confidence, timestamp)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ''', (
            wisdom_id,
            'pattern',
            obs['title'],
            obs['content'][:2000],
            obs['project'],
            'claude-mem',
            min_confidence,
            obs['created_at'],
        ))
        promoted += 1

    conn.commit()
    conn.close()
    return promoted


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Migrate claude-mem to cc-memory/cc-soul")
    parser.add_argument("--stats", action="store_true", help="Show statistics only")
    parser.add_argument("--project", help="Filter by project name")
    parser.add_argument("--target-dir", help="Target project directory for import")
    parser.add_argument("--limit", type=int, help="Limit number of observations")
    parser.add_argument("--dry-run", action="store_true", default=True, help="Dry run (default)")
    parser.add_argument("--execute", action="store_true", help="Actually perform migration")
    parser.add_argument("--export-json", help="Export to JSON file instead of importing")
    parser.add_argument("--index-vectors", action="store_true", help="Also index to LanceDB")
    parser.add_argument("--promote-wisdom", action="store_true", help="Promote decisions/discoveries to soul wisdom")
    args = parser.parse_args()

    if not CLAUDE_MEM_DB.exists():
        print(f"Error: claude-mem database not found at {CLAUDE_MEM_DB}")
        sys.exit(1)

    if args.stats:
        total, by_project, by_type = get_claude_mem_stats()
        print(f"\n=== claude-mem Statistics ===")
        print(f"Total observations: {total:,}")
        print(f"\nBy project:")
        for proj, cnt in by_project[:15]:
            print(f"  {proj}: {cnt:,}")
        if len(by_project) > 15:
            print(f"  ... and {len(by_project) - 15} more projects")
        print(f"\nBy type:")
        for typ, cnt in by_type:
            print(f"  {typ}: {cnt:,}")
        return

    # Export observations
    print(f"Exporting from claude-mem...")
    observations = export_observations(args.project, args.limit)
    print(f"Found {len(observations)} observations")

    if not observations:
        print("No observations to migrate")
        return

    # Transform
    print("Transforming to cc-memory format...")
    transformed = [transform_to_cc_memory(obs) for obs in observations]

    if args.export_json:
        with open(args.export_json, 'w') as f:
            json.dump(transformed, f, indent=2)
        print(f"Exported to {args.export_json}")
        return

    # Group by project
    by_project = {}
    for obs in transformed:
        proj = obs['project']
        if proj not in by_project:
            by_project[proj] = []
        by_project[proj].append(obs)

    print(f"\nObservations by project:")
    for proj, obs_list in sorted(by_project.items(), key=lambda x: -len(x[1])):
        print(f"  {proj}: {len(obs_list)}")

    dry_run = not args.execute

    if args.target_dir:
        if dry_run:
            print(f"\n[DRY RUN] Would import to {args.target_dir}")
        result = import_to_cc_memory(transformed, args.target_dir, dry_run=dry_run)
        if not dry_run:
            imported, skipped = result
            print(f"Imported: {imported}, Skipped (already exists): {skipped}")

    if args.index_vectors:
        print("\nIndexing to LanceDB...")
        indexed = index_to_lancedb(transformed, dry_run=dry_run)
        if not dry_run:
            print(f"Indexed {indexed} observations to LanceDB")

    if args.promote_wisdom:
        print("\nPromoting to soul wisdom...")
        promoted = promote_to_soul_wisdom(transformed, dry_run=dry_run)
        if not dry_run:
            print(f"Promoted {promoted} observations to soul wisdom")

    if not args.target_dir and not args.index_vectors and not args.promote_wisdom:
        print("\nUsage examples:")
        print("  # Import to specific project")
        print("  python migrate_claude_mem.py --project cc-soul --target-dir /path/to/cc-soul --execute")
        print("")
        print("  # Index all to LanceDB for semantic search")
        print("  python migrate_claude_mem.py --index-vectors --execute")
        print("")
        print("  # Promote decisions/discoveries to soul wisdom")
        print("  python migrate_claude_mem.py --promote-wisdom --execute")
        print("")
        print("  # Full migration: import + vectors + wisdom")
        print("  python migrate_claude_mem.py --project cc-soul --target-dir . --index-vectors --promote-wisdom --execute")


if __name__ == "__main__":
    main()
