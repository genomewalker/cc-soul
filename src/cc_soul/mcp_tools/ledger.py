# =============================================================================
# Ledger - Session State Preservation
# =============================================================================


@mcp.tool()
def save_session_ledger(
    immediate_next: str = "",
    deferred: str = "",
    critical_context: str = "",
) -> str:
    """Save current session state to ledger for restoration.

    Captures soul state (coherence, intentions, mood), work state,
    and continuation hints. Use before /clear or when crossing
    context budget thresholds.

    Args:
        immediate_next: What should happen next when session resumes
        deferred: Comma-separated list of deferred tasks
        critical_context: Important context that must survive restoration
    """
    from ..ledger import save_ledger

    deferred_list = [d.strip() for d in deferred.split(",") if d.strip()]

    ledger = save_ledger(
        immediate_next=immediate_next,
        deferred=deferred_list,
        critical_context=critical_context,
    )

    return (
        f"Ledger saved: {ledger.ledger_id}\n"
        f"  Project: {ledger.project}\n"
        f"  Coherence: {ledger.soul_state.coherence:.0%}\n"
        f"  Intentions: {len(ledger.soul_state.active_intentions)}\n"
        f"  Continue: {immediate_next or '(none)'}"
    )


@mcp.tool()
def load_session_ledger(project: str = "") -> str:
    """Load the most recent session ledger for restoration.

    Returns the latest ledger state including soul state, work state,
    and continuation hints.

    Args:
        project: Project name to filter by (defaults to current project)
    """
    from ..ledger import load_latest_ledger, format_ledger_for_context

    ledger = load_latest_ledger(project if project else None)

    if not ledger:
        return "No ledger found for this project."

    return format_ledger_for_context(ledger)


@mcp.tool()
def show_ledger_history(limit: int = 5) -> str:
    """Show recent session ledgers.

    Args:
        limit: Maximum ledgers to show
    """
    from ..ledger import _call_cc_memory_recall, _get_project_name
    import json

    project = _get_project_name()
    ledgers = _call_cc_memory_recall(
        query=f"session ledger checkpoint {project}",
        category="session_ledger",
        limit=limit,
    )

    if not ledgers:
        return f"No ledgers found for project: {project}"

    lines = [f"## Ledger History ({project})\n"]
    for entry in ledgers:
        try:
            content = entry.get("content", "{}")
            data = json.loads(content) if isinstance(content, str) else content
            lines.append(
                f"- **{data.get('ledger_id', '?')}** ({entry.get('timestamp', '?')[:16]})\n"
                f"  Coherence: {data.get('soul_state', {}).get('coherence', 0):.0%}, "
                f"Continue: {data.get('continuation', {}).get('immediate_next', '-')[:50]}"
            )
        except (json.JSONDecodeError, KeyError, TypeError):
            lines.append(f"- Entry {entry.get('id', '?')} (parse error)")

    return "\n".join(lines)


@mcp.tool()
def restore_from_ledger(ledger_id: str = "") -> str:
    """Restore soul state from a specific ledger.

    Restores intentions and provides continuation context.

    Args:
        ledger_id: Specific ledger ID to restore (defaults to most recent)
    """
    from ..ledger import load_latest_ledger, restore_from_ledger as do_restore

    ledger = load_latest_ledger()

    if not ledger:
        return "No ledger found to restore from."

    if ledger_id and ledger.ledger_id != ledger_id:
        return f"Ledger {ledger_id} not found. Latest is {ledger.ledger_id}."

    result = do_restore(ledger)

    return (
        f"Restored from ledger: {ledger.ledger_id}\n"
        f"  Intentions restored: {result['intentions']}\n"
        f"  Previous coherence: {result['coherence']:.0%}\n"
        f"  Continue: {result['continuation'] or '(none)'}"
    )
