# =============================================================================
# Backup - Soul Preservation
# =============================================================================

@mcp.tool()
def backup_soul(output_path: str = None) -> str:
    """Create a backup of the soul.

    Args:
        output_path: Optional path for the backup. If not provided, creates
                     a timestamped backup in ~/.claude/mind/backups/
    """
    from .backup import dump_soul, create_timestamped_backup
    from pathlib import Path

    if output_path:
        result = dump_soul(Path(output_path))
        if "error" in result:
            return f"Error: {result['error']}"
        return f"Soul backed up to: {output_path}"
    else:
        path = create_timestamped_backup()
        return f"Backup created: {path}"


@mcp.tool()
def restore_backup(backup_path: str, merge: bool = False) -> str:
    """Restore soul from a backup file.

    Args:
        backup_path: Path to the backup JSON file
        merge: If True, merge with existing soul. If False, replace entirely.
    """
    from .backup import restore_soul
    from pathlib import Path

    result = restore_soul(Path(backup_path), merge=merge)

    if "error" in result:
        return f"Error: {result['error']}"

    counts = result.get("counts", {})
    summary = ", ".join(f"{k}: {v}" for k, v in counts.items())
    mode = "merged" if merge else "replaced"
    return f"Soul {mode} from backup. Restored: {summary}"


@mcp.tool()
def list_backups() -> str:
    """List available soul backups."""
    from .backup import list_backups as _list_backups, format_backup_list

    backups = _list_backups()
    return format_backup_list(backups)
