"""
Migration Tool: Import legacy data into Synapse

Migrates from:
- soul.db (wisdom, identity, beliefs, conversations)
- brain.db (concepts, edges)
- Local .memory directories

To:
- soul.synapse (unified graph)
"""

import json
import sqlite3
from pathlib import Path
from typing import Optional
from datetime import datetime

CLAUDE_DIR = Path.home() / ".claude"
MIND_DIR = CLAUDE_DIR / "mind"
SOUL_DB = MIND_DIR / "soul.db"
BRAIN_DB = MIND_DIR / "brain.db"
SYNAPSE_PATH = MIND_DIR / "soul.synapse"


def migrate_soul_db(graph, verbose: bool = True) -> dict:
    """Migrate wisdom, identity, beliefs from soul.db."""
    stats = {"wisdom": 0, "identity": 0, "beliefs": 0, "conversations": 0, "skipped": 0}

    if not SOUL_DB.exists():
        if verbose:
            print(f"  soul.db not found at {SOUL_DB}")
        return stats

    conn = sqlite3.connect(SOUL_DB)
    conn.row_factory = sqlite3.Row

    # Migrate wisdom
    if verbose:
        print("  Migrating wisdom...")
    try:
        cursor = conn.execute("SELECT * FROM wisdom")
        for row in cursor:
            try:
                # Check if already exists by title
                existing = graph.search(row["title"], limit=1, threshold=0.95)
                if existing and any(row["title"].lower() in str(c).lower() for c, _ in existing):
                    stats["skipped"] += 1
                    continue

                graph.add_wisdom(
                    title=row["title"],
                    content=row["content"],
                    domain=row["domain"],
                    confidence=row["confidence"] or 0.7,
                )
                stats["wisdom"] += 1
            except Exception as e:
                if verbose:
                    print(f"    Warning: Failed to migrate wisdom '{row['title'][:30]}': {e}")
                stats["skipped"] += 1
    except sqlite3.OperationalError as e:
        if verbose:
            print(f"    No wisdom table: {e}")

    # Migrate identity
    if verbose:
        print("  Migrating identity...")
    try:
        cursor = conn.execute("SELECT * FROM identity")
        for row in cursor:
            try:
                aspect = f"{row['aspect']}:{row['key']}"
                graph.add_identity(aspect, row["value"], immutable=False)
                stats["identity"] += 1
            except Exception as e:
                if verbose:
                    print(f"    Warning: Failed to migrate identity '{row['aspect']}': {e}")
                stats["skipped"] += 1
    except sqlite3.OperationalError as e:
        if verbose:
            print(f"    No identity table: {e}")

    # Migrate beliefs
    if verbose:
        print("  Migrating beliefs...")
    try:
        cursor = conn.execute("SELECT * FROM beliefs")
        for row in cursor:
            try:
                graph.add_belief(row["belief"], row["strength"] or 0.8)
                stats["beliefs"] += 1
            except Exception as e:
                if verbose:
                    print(f"    Warning: Failed to migrate belief: {e}")
                stats["skipped"] += 1
    except sqlite3.OperationalError as e:
        if verbose:
            print(f"    No beliefs table: {e}")

    # Migrate conversations as episodes
    if verbose:
        print("  Migrating conversations...")
    try:
        cursor = conn.execute("SELECT * FROM conversations WHERE summary IS NOT NULL")
        for row in cursor:
            try:
                project = row["project"] or "unknown"
                summary = row["summary"] or ""
                if len(summary) < 10:
                    continue

                graph.observe(
                    category="session_ledger",
                    title=f"Session: {project}",
                    content=summary,
                    project=project,
                )
                stats["conversations"] += 1
            except Exception as e:
                if verbose:
                    print(f"    Warning: Failed to migrate conversation: {e}")
                stats["skipped"] += 1
    except sqlite3.OperationalError as e:
        if verbose:
            print(f"    No conversations table: {e}")

    conn.close()
    return stats


def migrate_brain_db(graph, verbose: bool = True) -> dict:
    """Migrate concepts from brain.db as Terms."""
    stats = {"concepts": 0, "edges": 0, "skipped": 0}

    if not BRAIN_DB.exists():
        if verbose:
            print(f"  brain.db not found at {BRAIN_DB}")
        return stats

    conn = sqlite3.connect(BRAIN_DB)
    conn.row_factory = sqlite3.Row

    # Build ID mapping for edges
    id_map = {}

    # Migrate concepts as Terms
    if verbose:
        print("  Migrating concepts...")
    try:
        cursor = conn.execute("SELECT * FROM concepts WHERE title IS NOT NULL")
        for row in cursor:
            try:
                title = row["title"]
                if not title or len(title) < 2:
                    continue

                # Check if already exists
                existing = graph.search(title, limit=1, threshold=0.9)
                if existing:
                    stats["skipped"] += 1
                    continue

                concept_type = row["type"] or "concept"
                domain = row["domain"]

                # Add as Term (vocabulary)
                node_id = graph.add_term(
                    term=title,
                    definition=f"Concept of type: {concept_type}",
                    domain=domain,
                )
                id_map[row["idx"]] = node_id
                stats["concepts"] += 1
            except Exception as e:
                if verbose:
                    print(f"    Warning: Failed to migrate concept '{row.get('title', '?')}': {e}")
                stats["skipped"] += 1
    except sqlite3.OperationalError as e:
        if verbose:
            print(f"    No concepts table: {e}")

    # Migrate edges
    if verbose:
        print("  Migrating edges...")
    try:
        cursor = conn.execute("SELECT * FROM edges WHERE weight > 0.3")
        for row in cursor:
            try:
                source_idx = row["source_idx"]
                target_idx = row["target_idx"]

                if source_idx in id_map and target_idx in id_map:
                    edge_type = row.get("edge_type", "similar") or "similar"
                    graph.connect(
                        id_map[source_idx],
                        id_map[target_idx],
                        edge_type,
                        row["weight"],
                    )
                    stats["edges"] += 1
            except Exception as e:
                stats["skipped"] += 1
    except sqlite3.OperationalError as e:
        if verbose:
            print(f"    No edges table: {e}")

    conn.close()
    return stats


def migrate_local_memory(graph, paths: list[Path], verbose: bool = True) -> dict:
    """Migrate local .memory directories."""
    stats = {"sessions": 0, "skipped": 0}

    for memory_dir in paths:
        if not memory_dir.exists():
            continue

        db_path = memory_dir / "memory.db"
        if not db_path.exists():
            continue

        project_name = memory_dir.parent.name
        if verbose:
            print(f"  Migrating {project_name}/.memory...")

        try:
            conn = sqlite3.connect(db_path)
            conn.row_factory = sqlite3.Row

            # Check for sessions with summaries
            try:
                cursor = conn.execute("SELECT * FROM sessions WHERE summary IS NOT NULL")
                for row in cursor:
                    summary = row["summary"]
                    if not summary or len(summary) < 20:
                        continue

                    graph.observe(
                        category="session_ledger",
                        title=f"Session: {project_name}",
                        content=summary,
                        project=project_name,
                    )
                    stats["sessions"] += 1
            except sqlite3.OperationalError:
                pass

            conn.close()
        except Exception as e:
            if verbose:
                print(f"    Warning: Failed to migrate {memory_dir}: {e}")
            stats["skipped"] += 1

    return stats


def find_local_memory_dirs() -> list[Path]:
    """Find all .memory directories in common locations."""
    dirs = []

    # Check common project locations
    search_paths = [
        Path.home() / "repos",
        Path("/maps/projects/fernandezguerra/apps/repos"),
        Path("/maps/projects/caeg/people/kbd606/scratch"),
    ]

    for base in search_paths:
        if base.exists():
            for memory_dir in base.rglob(".memory"):
                if memory_dir.is_dir():
                    dirs.append(memory_dir)

    return dirs


def migrate_all(verbose: bool = True, dry_run: bool = False) -> dict:
    """Run full migration."""
    from .synapse_bridge import SoulGraph

    report = {
        "soul_db": {},
        "brain_db": {},
        "local_memory": {},
        "total_migrated": 0,
        "total_skipped": 0,
    }

    if verbose:
        print("=== Soul Migration ===")
        print()

    # Load or create synapse graph
    if verbose:
        print(f"Loading synapse from {SYNAPSE_PATH}...")

    graph = SoulGraph.load(SYNAPSE_PATH)
    initial_count = len(graph)

    if verbose:
        print(f"  Initial nodes: {initial_count}")
        print()

    # Migrate soul.db
    if verbose:
        print("Migrating soul.db...")
    if not dry_run:
        report["soul_db"] = migrate_soul_db(graph, verbose)
    else:
        report["soul_db"] = {"dry_run": True}

    # Migrate brain.db
    if verbose:
        print()
        print("Migrating brain.db...")
    if not dry_run:
        report["brain_db"] = migrate_brain_db(graph, verbose)
    else:
        report["brain_db"] = {"dry_run": True}

    # Find and migrate local .memory directories
    if verbose:
        print()
        print("Finding local .memory directories...")
    local_dirs = find_local_memory_dirs()
    if verbose:
        print(f"  Found {len(local_dirs)} directories")

    if not dry_run and local_dirs:
        report["local_memory"] = migrate_local_memory(graph, local_dirs, verbose)
    else:
        report["local_memory"] = {"directories": len(local_dirs), "dry_run": dry_run}

    # Save
    if not dry_run:
        if verbose:
            print()
            print("Saving synapse...")
        graph.save()

    # Calculate totals
    final_count = len(graph)
    report["total_migrated"] = final_count - initial_count

    for key in ["soul_db", "brain_db", "local_memory"]:
        if isinstance(report[key], dict):
            report["total_skipped"] += report[key].get("skipped", 0)

    if verbose:
        print()
        print("=== Migration Complete ===")
        print()
        print(f"Initial nodes: {initial_count}")
        print(f"Final nodes: {final_count}")
        print(f"Migrated: {report['total_migrated']}")
        print(f"Skipped (duplicates): {report['total_skipped']}")
        print()

        if report["soul_db"]:
            print("soul.db:")
            for k, v in report["soul_db"].items():
                print(f"  {k}: {v}")

        if report["brain_db"]:
            print("brain.db:")
            for k, v in report["brain_db"].items():
                print(f"  {k}: {v}")

        if report["local_memory"]:
            print("local .memory:")
            for k, v in report["local_memory"].items():
                print(f"  {k}: {v}")

    return report


def main():
    """CLI entry point."""
    import argparse

    parser = argparse.ArgumentParser(description="Migrate legacy data to synapse")
    parser.add_argument("--dry-run", action="store_true", help="Show what would be migrated")
    parser.add_argument("--quiet", action="store_true", help="Minimal output")

    args = parser.parse_args()

    migrate_all(verbose=not args.quiet, dry_run=args.dry_run)


if __name__ == "__main__":
    main()
