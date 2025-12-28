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
from typing import Optional, List, Dict
from datetime import datetime

from .core import SOUL_DIR


# Default context window size (Claude's typical limit)
DEFAULT_CONTEXT_SIZE = 200_000

# Usable portion (Claude Code reserves ~39% for system/cache)
USABLE_FRACTION = 0.61

# Thresholds for action
COMPACT_THRESHOLD = 0.25  # Switch to compact mode at 25% remaining
URGENT_THRESHOLD = 0.10  # Trigger urgent save at 10% remaining


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
        - mode: str - 'full', 'compact', or 'minimal'
        - budget: int - max tokens to inject
        - save_first: bool - whether to save context urgently
    """
    budget = get_context_budget(transcript_path)

    if budget is None:
        # Can't read, be conservative
        return {
            "inject": True,
            "mode": "compact",
            "budget": 500,
            "save_first": False,
        }

    if budget.should_urgent_save:
        return {
            "inject": True,
            "mode": "minimal",
            "budget": 100,
            "save_first": True,
        }
    elif budget.should_compact:
        return {
            "inject": True,
            "mode": "compact",
            "budget": 500,
            "save_first": False,
        }
    else:
        return {
            "inject": True,
            "mode": "full",
            "budget": 2000,
            "save_first": False,
        }
