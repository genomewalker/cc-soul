"""
Soul Backup and Restore - Preserve identity across time.

Export the soul to JSON for backup, version control, or transfer.
Restore from backup if anything happens to soul.db.
"""

import gzip
import json
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, Optional, List

# TODO: Migrate to synapse graph storage
# from .core import get_synapse_graph, save_synapse


def dump_soul(output_path: Optional[Path] = None) -> Dict:
    """
    Export the entire soul to a dictionary (and optionally to file).

    Args:
        output_path: If provided, write JSON to this path

    Returns:
        Complete soul state as dictionary
    """
    # TODO: Migrate to synapse graph storage
    return {
        "meta": {
            "exported_at": datetime.now().isoformat(),
            "version": "1.0",
            "source": "synapse (not implemented)",
        },
        "wisdom": [],
        "beliefs": [],
        "identity": [],
        "vocabulary": [],
        "aspirations": [],
        "insights": [],
        "coherence_history": [],
        "error": "Backup not available - SQLite backend removed",
    }


def restore_soul(source: Path | Dict, merge: bool = False) -> Dict:
    """
    Restore soul from backup.

    Args:
        source: Path to JSON file or dictionary with soul data
        merge: If True, merge with existing. If False, replace entirely.

    Returns:
        Result summary with counts
    """
    # TODO: Migrate to synapse graph storage
    return {
        "error": "Restore not available - SQLite backend removed",
        "restored_at": datetime.now().isoformat(),
        "counts": {},
    }


def get_backup_path() -> Path:
    """Get default backup location."""
    return SOUL_DB.parent / "backups"


def create_timestamped_backup(compress: bool = True) -> Path:
    """Create a backup with timestamp in default location.

    Args:
        compress: If True, use gzip compression (default)
    """
    backup_dir = get_backup_path()
    backup_dir.mkdir(parents=True, exist_ok=True)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    ext = ".json.gz" if compress else ".json"
    backup_path = backup_dir / f"soul_{timestamp}{ext}"

    dump_soul(backup_path)
    return backup_path


def list_backups() -> List[Dict]:
    """List available backups (both compressed and uncompressed)."""
    backup_dir = get_backup_path()
    if not backup_dir.exists():
        return []

    # Find both .json and .json.gz files
    files = list(backup_dir.glob("soul_*.json"))
    files.extend(backup_dir.glob("soul_*.json.gz"))
    files = sorted(files, reverse=True, key=lambda f: f.stat().st_mtime)

    backups = []
    for f in files:
        try:
            # Read compressed or plain
            if f.suffix == ".gz":
                with gzip.open(f, "rt", encoding="utf-8") as fp:
                    data = json.load(fp)
            else:
                with open(f) as fp:
                    data = json.load(fp)
            meta = data.get("meta", {})
            backups.append(
                {
                    "path": f,
                    "exported_at": meta.get("exported_at"),
                    "size": f.stat().st_size,
                    "compressed": f.suffix == ".gz",
                }
            )
        except (json.JSONDecodeError, KeyError, gzip.BadGzipFile):
            backups.append(
                {
                    "path": f,
                    "exported_at": None,
                    "size": f.stat().st_size,
                    "compressed": f.suffix == ".gz",
                }
            )

    return backups


def format_backup_list(backups: list) -> str:
    """Format backup list for display."""
    if not backups:
        return "No backups found."

    total_size = sum(b["size"] for b in backups)
    lines = [
        f"Available backups ({len(backups)} files, {total_size / 1024:.1f} KB total):",
        "",
    ]

    for b in backups[:10]:
        size_kb = b["size"] / 1024
        date = b["exported_at"][:19] if b["exported_at"] else "unknown"
        compressed = " [gz]" if b.get("compressed") else ""
        lines.append(f"  {b['path'].name}  ({size_kb:.1f} KB){compressed}  {date}")

    if len(backups) > 10:
        lines.append(f"  ... and {len(backups) - 10} more")

    return "\n".join(lines)


def cleanup_old_backups(keep_daily: int = 7, keep_weekly: int = 4) -> Dict:
    """
    Clean up old backups to save disk space.

    Retention policy:
    - Keep last `keep_daily` daily backups
    - Keep one backup per week for `keep_weekly` weeks
    - Delete everything older

    Args:
        keep_daily: Number of recent daily backups to keep
        keep_weekly: Number of weekly backups to keep

    Returns:
        Summary of cleanup action
    """
    backup_dir = get_backup_path()
    if not backup_dir.exists():
        return {"deleted": 0, "kept": 0}

    backups = list_backups()
    if not backups:
        return {"deleted": 0, "kept": 0}

    now = datetime.now()
    now - timedelta(days=keep_daily)
    weekly_cutoff = now - timedelta(weeks=keep_weekly)

    to_keep = set()
    to_delete = []

    # Keep the most recent `keep_daily` backups
    for b in backups[:keep_daily]:
        to_keep.add(b["path"])

    # Keep one per week for older backups
    weeks_seen = set()
    for b in backups[keep_daily:]:
        if not b["exported_at"]:
            continue
        try:
            backup_date = datetime.fromisoformat(b["exported_at"])
        except ValueError:
            continue

        if backup_date < weekly_cutoff:
            to_delete.append(b["path"])
        else:
            week_key = backup_date.strftime("%Y-W%W")
            if week_key not in weeks_seen:
                weeks_seen.add(week_key)
                to_keep.add(b["path"])
            else:
                to_delete.append(b["path"])

    # Delete old backups
    deleted = 0
    for path in to_delete:
        if path not in to_keep:
            try:
                path.unlink()
                deleted += 1
            except OSError:
                pass

    return {
        "deleted": deleted,
        "kept": len(to_keep),
        "total_before": len(backups),
    }


def get_backup_size() -> int:
    """Get total size of all backups in bytes."""
    backup_dir = get_backup_path()
    if not backup_dir.exists():
        return 0

    total = 0
    for f in backup_dir.glob("soul_*.json"):
        total += f.stat().st_size
    for f in backup_dir.glob("soul_*.json.gz"):
        total += f.stat().st_size
    return total


def auto_backup_if_needed(min_interval_hours: int = 4) -> Optional[Path]:
    """
    Create backup if enough time has passed since last one.

    Args:
        min_interval_hours: Minimum hours between backups

    Returns:
        Backup path if created, None if skipped
    """
    backups = list_backups()

    if backups:
        latest = backups[0]
        if latest["exported_at"]:
            try:
                last_backup = datetime.fromisoformat(latest["exported_at"])
                hours_since = (datetime.now() - last_backup).total_seconds() / 3600
                if hours_since < min_interval_hours:
                    return None
            except ValueError:
                pass

    # Create backup and cleanup
    path = create_timestamped_backup()
    cleanup_old_backups()
    return path
