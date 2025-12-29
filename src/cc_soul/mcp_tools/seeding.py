# =============================================================================
# SOUL SEEDING
# =============================================================================

@mcp.tool()
def seed_soul(force: bool = False) -> str:
    """Seed the soul with foundational wisdom and beliefs.

    For new installations. Provides a starting identity with
    core beliefs and foundational wisdom.

    Args:
        force: If True, re-seed even if already seeded
    """
    try:
        from .seed import seed_soul as _seed, is_seeded

        if is_seeded() and not force:
            return "Soul already seeded. Use force=True to re-seed."

        result = _seed(force=force)

        if result.get("status") == "seeded":
            counts = result.get("counts", {})
            return (
                f"Soul seeded successfully!\n"
                f"  Beliefs: {counts.get('beliefs', 0)}\n"
                f"  Wisdom: {counts.get('wisdom', 0)}\n"
                f"  Vocabulary: {counts.get('vocabulary', 0)}"
            )
        else:
            return result.get("message", "Seeding completed.")

    except Exception as e:
        return f"Failed to seed soul: {e}"


@mcp.tool()
def is_soul_seeded() -> str:
    """Check if the soul has been seeded with foundational content."""
    try:
        from .seed import is_seeded

        if is_seeded():
            return "Soul has foundational content (seeded)."
        else:
            return "Soul is empty (not seeded). Run seed_soul() to initialize."

    except Exception as e:
        return f"Failed to check: {e}"
