"""
Fast Migration: Uses synapse's Rust HashEmbedder instead of sentence-transformers.

Much faster - no Python ML model loading.
Trade-off: Hash-based similarity instead of semantic similarity.
Can re-embed later with sentence-transformers if needed.
"""

import json
import sqlite3
from pathlib import Path

CLAUDE_DIR = Path.home() / ".claude"
MIND_DIR = CLAUDE_DIR / "mind"
SOUL_DB = MIND_DIR / "soul.db"
BRAIN_DB = MIND_DIR / "brain.db"
SYNAPSE_PATH = MIND_DIR / "soul.synapse"


def migrate_fast(verbose: bool = True) -> dict:
    """Fast migration using Rust graph directly (no Python embeddings)."""
    # Import synapse directly - uses HashEmbedder
    from synapse import SynapseGraph

    report = {
        "wisdom": 0,
        "identity": 0,
        "beliefs": 0,
        "conversations": 0,
        "concepts": 0,
        "skipped": 0,
    }

    if verbose:
        print("=== Fast Migration (Rust HashEmbedder) ===")
        print()

    # Load synapse graph
    if SYNAPSE_PATH.exists():
        graph = SynapseGraph.load(str(SYNAPSE_PATH))
        if verbose:
            print(f"Loaded existing graph: {len(graph)} nodes")
    else:
        graph = SynapseGraph()
        if verbose:
            print("Created new graph")

    initial_count = len(graph)

    # Migrate soul.db
    if SOUL_DB.exists():
        if verbose:
            print(f"\nMigrating soul.db...")

        conn = sqlite3.connect(SOUL_DB)
        conn.row_factory = sqlite3.Row

        # Wisdom
        try:
            cursor = conn.execute("SELECT * FROM wisdom")
            for row in cursor:
                try:
                    graph.add_wisdom(
                        row["title"] or "Untitled",
                        row["content"] or "",
                        row["domain"],
                        row["confidence"] or 0.7,
                        None,  # No pre-computed vector - use HashEmbedder
                    )
                    report["wisdom"] += 1
                except Exception as e:
                    report["skipped"] += 1
        except sqlite3.OperationalError:
            pass

        if verbose:
            print(f"  Wisdom: {report['wisdom']}")

        # Identity
        try:
            cursor = conn.execute("SELECT * FROM identity")
            for row in cursor:
                try:
                    aspect = f"{row['aspect']}:{row['key']}"
                    graph.add_identity(aspect, row["value"] or "", False)
                    report["identity"] += 1
                except:
                    report["skipped"] += 1
        except sqlite3.OperationalError:
            pass

        if verbose:
            print(f"  Identity: {report['identity']}")

        # Beliefs
        try:
            cursor = conn.execute("SELECT * FROM beliefs")
            for row in cursor:
                try:
                    graph.add_belief(row["belief"], row["strength"] or 0.8)
                    report["beliefs"] += 1
                except:
                    report["skipped"] += 1
        except sqlite3.OperationalError:
            pass

        if verbose:
            print(f"  Beliefs: {report['beliefs']}")

        # Conversations as episodes
        try:
            cursor = conn.execute("SELECT * FROM conversations WHERE summary IS NOT NULL AND LENGTH(summary) > 20")
            for row in cursor:
                try:
                    project = row["project"] or "unknown"
                    graph.add_episode(
                        "session_ledger",
                        f"Session: {project}",
                        row["summary"],
                        project,
                        None,
                        None,
                    )
                    report["conversations"] += 1
                except:
                    report["skipped"] += 1
        except sqlite3.OperationalError:
            pass

        if verbose:
            print(f"  Conversations: {report['conversations']}")

        conn.close()

    # Migrate brain.db
    if BRAIN_DB.exists():
        if verbose:
            print(f"\nMigrating brain.db...")

        conn = sqlite3.connect(BRAIN_DB)
        conn.row_factory = sqlite3.Row

        try:
            cursor = conn.execute("SELECT * FROM concepts WHERE title IS NOT NULL AND LENGTH(title) > 1")
            for row in cursor:
                try:
                    graph.add_term(
                        row["title"],
                        f"Concept: {row['type'] or 'general'}",
                        row["domain"],
                        None,
                    )
                    report["concepts"] += 1
                except:
                    report["skipped"] += 1
        except sqlite3.OperationalError:
            pass

        if verbose:
            print(f"  Concepts: {report['concepts']}")

        conn.close()

    # Save
    if verbose:
        print(f"\nSaving to {SYNAPSE_PATH}...")
    graph.save(str(SYNAPSE_PATH))

    final_count = len(graph)
    migrated = final_count - initial_count

    if verbose:
        print()
        print("=== Migration Complete ===")
        print(f"Initial: {initial_count} nodes")
        print(f"Final: {final_count} nodes")
        print(f"Migrated: {migrated}")
        print(f"Skipped: {report['skipped']}")

    report["total_migrated"] = migrated
    return report


def main():
    """CLI entry point."""
    import argparse
    import time

    parser = argparse.ArgumentParser(description="Fast migration using Rust HashEmbedder")
    parser.add_argument("--quiet", action="store_true", help="Minimal output")

    args = parser.parse_args()

    start = time.time()
    migrate_fast(verbose=not args.quiet)
    elapsed = time.time() - start

    if not args.quiet:
        print(f"\nCompleted in {elapsed:.1f}s")


if __name__ == "__main__":
    main()
