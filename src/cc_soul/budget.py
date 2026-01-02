"""
Context Budget Tracking - Know when to save, when to compact.

The soul reads the conversation transcript to track token usage.
No external dependencies - works for any user.

Key insight: Every assistant message in the transcript contains
usage data (input_tokens, output_tokens, cache tokens). We can
calculate remaining context capacity from this.
"""

import json
from pathlib import Path
from dataclasses import dataclass
from typing import Optional, Dict, List
from datetime import datetime

from .core import SOUL_DIR


# Default context window size (Claude's typical limit)
DEFAULT_CONTEXT_SIZE = 200_000

# Use full context window (matches Claude Code's /context display)
USABLE_FRACTION = 1.0

# Thresholds for action
COMPACT_THRESHOLD = 0.25  # Switch to compact mode at 25% remaining
URGENT_THRESHOLD = 0.10  # Trigger urgent save at 10% remaining
EMERGENCY_THRESHOLD = 0.05  # Trigger proactive compaction at 5% remaining


@dataclass
class ContextBudget:
    """Current context budget status."""

    total_tokens: int
    input_tokens: int
    output_tokens: int
    cache_tokens: int
    context_size: int
    usable_size: int
    remaining: int
    remaining_pct: float
    should_compact: bool
    should_urgent_save: bool
    should_emergency_compact: bool
    message_count: int
    timestamp: str


def get_context_budget(transcript_path: str = None) -> Optional[ContextBudget]:
    """
    Calculate current context budget from transcript.

    Args:
        transcript_path: Path to the session transcript (JSONL file).
                        If None, tries to find current session.

    Returns:
        ContextBudget with usage stats, or None if can't read transcript.
    """
    if transcript_path is None:
        transcript_path = _find_current_transcript()

    if not transcript_path or not Path(transcript_path).exists():
        return None

    try:
        return _parse_transcript(transcript_path)
    except Exception:
        return None


def _find_current_transcript() -> Optional[str]:
    """Try to find the current session's transcript."""
    # Check if we have a stored transcript path
    current_file = SOUL_DIR / ".current_transcript"
    if current_file.exists():
        path = current_file.read_text().strip()
        if Path(path).exists():
            return path
    return None


def _parse_transcript(path: str) -> ContextBudget:
    """
    Parse JSONL transcript and calculate budget.

    Key insight: Each API call's usage shows tokens for THAT call.
    The LAST assistant message's input_tokens represents the full
    context sent to Claude, which is our best estimate of current usage.

    Total context = input + output + cache_creation + cache_read.
    Cache tokens DO count against context window - they're just
    processed more efficiently. We track them separately for visibility.
    """
    last_input = 0
    last_output = 0
    last_cache_create = 0
    last_cache_read = 0
    message_count = 0
    context_size = DEFAULT_CONTEXT_SIZE

    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            try:
                entry = json.loads(line)
            except json.JSONDecodeError:
                continue

            # Look for assistant messages with usage data
            if entry.get("type") == "assistant":
                message = entry.get("message", {})
                usage = message.get("usage", {})

                if usage:
                    # Track the LATEST usage (best estimate of current context)
                    last_input = usage.get("input_tokens", 0)
                    last_output = usage.get("output_tokens", 0)
                    last_cache_create = usage.get("cache_creation_input_tokens", 0)
                    last_cache_read = usage.get("cache_read_input_tokens", 0)
                    message_count += 1

    # Total context = all token types
    # Matches statusline.sh formula: TOKENS=$((IN + OUT + CC + CR))
    total_context = last_input + last_output + last_cache_create + last_cache_read

    usable_size = int(context_size * USABLE_FRACTION)
    remaining = max(0, usable_size - total_context)
    remaining_pct = remaining / usable_size if usable_size > 0 else 0

    return ContextBudget(
        total_tokens=total_context,
        input_tokens=last_input,
        output_tokens=last_output,
        cache_tokens=last_cache_create + last_cache_read,
        context_size=context_size,
        usable_size=usable_size,
        remaining=remaining,
        remaining_pct=remaining_pct,
        should_compact=remaining_pct < COMPACT_THRESHOLD,
        should_urgent_save=remaining_pct < URGENT_THRESHOLD,
        should_emergency_compact=remaining_pct < EMERGENCY_THRESHOLD,
        message_count=message_count,
        timestamp=datetime.now().isoformat(),
    )


def save_transcript_path(path: str):
    """Save current transcript path for later reference."""
    current_file = SOUL_DIR / ".current_transcript"
    current_file.parent.mkdir(parents=True, exist_ok=True)
    current_file.write_text(path)


def get_injection_budget(budget: ContextBudget = None) -> int:
    """
    Determine how many tokens the soul should inject.

    Returns a budget that decreases as context fills up:
    - Full context: inject up to 2000 tokens
    - Compact mode: inject up to 500 tokens
    - Urgent mode: inject up to 100 tokens (just essentials)
    """
    if budget is None:
        budget = get_context_budget()

    if budget is None:
        # Can't read budget, be conservative
        return 500

    if budget.should_urgent_save:
        return 100  # Bare minimum
    elif budget.should_compact:
        return 500  # Compact essentials
    else:
        return 2000  # Full context


def format_budget_status(budget: ContextBudget) -> str:
    """Format budget for display."""
    pct = int(budget.remaining_pct * 100)
    bar_width = 20
    filled = int(pct * bar_width / 100)
    bar = "█" * filled + "░" * (bar_width - filled)

    status = "OK"
    if budget.should_urgent_save:
        status = "URGENT"
    elif budget.should_compact:
        status = "COMPACT"

    return f"[{bar}] {pct}% remaining ({budget.remaining:,} tokens) [{status}]"


# Convenience functions for hooks


def check_budget_before_inject(transcript_path: str = None) -> Dict:
    """
    Check budget before injecting soul context.

    Returns dict with:
        - inject: bool - whether to inject at all
        - mode: str - 'full', 'compact', 'minimal', or 'emergency'
        - budget: int - max tokens to inject
        - save_first: bool - whether to save context urgently
        - trigger_compact: bool - whether to trigger proactive /compact
        - remaining_pct: float - percentage remaining (0-1)
    """
    import os

    # Swarm agents have fresh context - minimal injection for focused task
    if os.environ.get("CC_SOUL_SWARM_AGENT") == "1":
        return {
            "inject": True,
            "mode": "minimal",
            "budget": 200,
            "save_first": False,
            "trigger_compact": False,
            "remaining_pct": 1.0,
            "swarm_agent": True,
        }

    budget = get_context_budget(transcript_path)

    if budget is None:
        # Can't read, be conservative
        return {
            "inject": True,
            "mode": "compact",
            "budget": 500,
            "save_first": False,
            "trigger_compact": False,
            "remaining_pct": 0.5,
        }

    if budget.should_emergency_compact:
        # EMERGENCY: <5% remaining - trigger proactive compaction
        return {
            "inject": True,
            "mode": "emergency",
            "budget": 50,
            "save_first": True,
            "trigger_compact": True,
            "remaining_pct": budget.remaining_pct,
        }
    elif budget.should_urgent_save:
        return {
            "inject": True,
            "mode": "minimal",
            "budget": 100,
            "save_first": True,
            "trigger_compact": False,
            "remaining_pct": budget.remaining_pct,
        }
    elif budget.should_compact:
        return {
            "inject": True,
            "mode": "compact",
            "budget": 500,
            "save_first": False,
            "trigger_compact": False,
            "remaining_pct": budget.remaining_pct,
        }
    else:
        return {
            "inject": True,
            "mode": "full",
            "budget": 2000,
            "save_first": False,
            "trigger_compact": False,
            "remaining_pct": budget.remaining_pct,
        }


# Multi-instance budget tracking via cc-memory


def get_session_id() -> str:
    """Get unique identifier for this Claude session."""
    import os

    # Use transcript path hash or process ID
    current_file = SOUL_DIR / ".current_transcript"
    if current_file.exists():
        path = current_file.read_text().strip()
        # Use last part of path as session ID
        return Path(path).stem[:12]

    # Fallback to process ID
    return f"pid_{os.getpid()}"


def get_parent_session_id() -> Optional[str]:
    """Get parent session ID if this is a spawned child session."""
    import os
    return os.environ.get("CC_SOUL_PARENT_SESSION")


def log_budget_to_memory(
    budget: ContextBudget = None,
    transcript_path: str = None,
    force: bool = False,
) -> Optional[str]:
    """
    Log current budget status to cc-memory for cross-instance awareness.

    Only logs on state transitions (RELAXED→COMPACT→EMERGENCY) to avoid
    flooding memory with redundant budget entries. Use force=True to
    always log regardless of state change.

    Returns observation ID if logged, None if skipped or unavailable.
    """
    try:
        from .bridge import is_memory_available

        if not is_memory_available():
            return None

        from cc_memory import memory as cc_memory
        from .bridge import find_project_dir

        if budget is None:
            budget = get_context_budget(transcript_path)

        if budget is None:
            return None

        session_id = get_session_id()
        parent_id = get_parent_session_id()
        pct = int(budget.remaining_pct * 100)

        # Determine pressure level
        if budget.should_urgent_save:
            pressure = "EMERGENCY"
        elif budget.should_compact:
            pressure = "COMPACT"
        elif budget.remaining_pct < 0.40:
            pressure = "NORMAL"
        else:
            pressure = "RELAXED"

        # Only log on state transitions to reduce noise
        if not force:
            last_pressure = _get_last_logged_pressure(session_id)
            if last_pressure == pressure:
                return None  # No state change, skip logging
            _save_last_logged_pressure(session_id, pressure)

        # Log to cc-memory with budget tag
        parent_line = f"\nParent: {parent_id}" if parent_id else ""
        content = f"""Session {session_id}: {pct}% remaining ({budget.remaining:,} tokens)
Pressure: {pressure}
Messages: {budget.message_count}
Timestamp: {budget.timestamp}{parent_line}"""

        project_dir = find_project_dir()
        obs_id = cc_memory.remember(
            category="budget",
            title=f"Budget: {pct}% ({pressure})",
            content=content,
            tags=["budget", f"session:{session_id}", pressure.lower()],
            project_dir=project_dir,
        )

        return obs_id

    except Exception:
        return None


def _get_last_logged_pressure(session_id: str) -> Optional[str]:
    """Get the last logged pressure level for a session."""
    pressure_file = SOUL_DIR / ".budget_pressure" / f"{session_id}.txt"
    if pressure_file.exists():
        return pressure_file.read_text().strip()
    return None


def _save_last_logged_pressure(session_id: str, pressure: str):
    """Save the current pressure level for a session."""
    pressure_dir = SOUL_DIR / ".budget_pressure"
    pressure_dir.mkdir(parents=True, exist_ok=True)
    pressure_file = pressure_dir / f"{session_id}.txt"
    pressure_file.write_text(pressure)


def get_all_session_budgets() -> List[Dict]:
    """
    Get budget status of all active sessions via cc-memory.

    Enables cross-instance awareness for swarm coordination.
    """
    try:
        from .bridge import is_memory_available

        if not is_memory_available():
            return []

        from cc_memory import memory as cc_memory
        from .bridge import find_project_dir

        project_dir = find_project_dir()

        # Query recent budget observations
        results = cc_memory.recall(
            query="budget remaining tokens pressure",
            category="budget",
            limit=10,
            project_dir=project_dir,
        )

        # Parse budget info from observations
        sessions = []
        for obs in results:
            content = obs.get("content", "")
            title = obs.get("title", "")

            # Extract percentage from title
            import re
            match = re.search(r"(\d+)%", title)
            if match:
                pct = int(match.group(1))
                # Extract session ID from tags or content
                session_id = "unknown"
                tags = obs.get("tags") or []
                for tag in tags:
                    if isinstance(tag, str) and tag.startswith("session:"):
                        session_id = tag.replace("session:", "")
                        break
                # Fallback: extract from content "Session {id}: ..."
                if session_id == "unknown":
                    content_match = re.search(r"Session ([^:]+):", content)
                    if content_match:
                        session_id = content_match.group(1).strip()
                # Extract parent session if present
                parent_id = None
                parent_match = re.search(r"Parent: (\S+)", content)
                if parent_match:
                    parent_id = parent_match.group(1)
                sessions.append({
                    "session_id": session_id,
                    "parent_id": parent_id,
                    "remaining_pct": pct / 100,
                    "pressure": "EMERGENCY" if pct < 10 else "COMPACT" if pct < 25 else "NORMAL",
                    "timestamp": obs.get("timestamp", ""),
                })

        return sessions

    except Exception:
        return []


def get_budget_warning() -> Optional[str]:
    """
    Get a warning message for the current session family (self + children).

    Only shows:
    - Current session's status
    - Child sessions (spawned voices/agents with this session as parent)
    """
    current_session = get_session_id()
    sessions = get_all_session_budgets()

    # Deduplicate: keep only most recent per session_id
    latest_by_session = {}
    for session in sessions:
        sid = session.get("session_id", "unknown")
        ts = session.get("timestamp", "")
        if sid not in latest_by_session or ts > latest_by_session[sid]["timestamp"]:
            latest_by_session[sid] = session

    # Filter: only current session or its children
    warnings = []
    for session in latest_by_session.values():
        sid = session.get("session_id", "")
        parent = session.get("parent_id")
        pct = session.get("remaining_pct", 1.0)

        # Only show current session family
        is_self = sid == current_session
        is_child = parent == current_session
        if not (is_self or is_child):
            continue

        # Skip healthy or exhausted sessions
        if pct >= 0.25 or pct == 0:
            continue

        label = "self" if is_self else "child"
        if pct < 0.10:
            warnings.append(f"⚠️ {label} ({sid[:8]}): EMERGENCY ({int(pct*100)}%)")
        else:
            warnings.append(f"📊 {label} ({sid[:8]}): COMPACT ({int(pct*100)}%)")

    if warnings:
        return "\n".join(warnings)
    return None
