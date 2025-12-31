"""
Decision Gates - Active soul guidance for Claude's actions.

Unlike passive context injection, decision gates actively validate
Claude's actions against beliefs, intentions, and past failures.

Design principles:
- Fast: Must complete in <100ms (hooks block Claude)
- Simple: Keyword matching, not LLM calls
- Actionable: Return guidance, not just warnings
"""

import json
import sqlite3
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass

from .core import SOUL_DIR


@dataclass
class GateResult:
    """Result of a decision gate check."""
    passed: bool
    warnings: List[str]
    guidance: List[str]
    blocked: bool = False  # True = should reject the action


def _get_beliefs() -> List[Tuple[str, float]]:
    """Get beliefs (principles) quickly from wisdom table."""
    db_path = SOUL_DIR / "soul.db"
    if not db_path.exists():
        return []

    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    cursor.execute(
        "SELECT title, confidence FROM wisdom WHERE type = 'principle' AND confidence > 0.5 ORDER BY confidence DESC LIMIT 10"
    )
    beliefs = cursor.fetchall()
    conn.close()
    return beliefs


def _get_failures() -> List[Tuple[str, str]]:
    """Get recorded failures quickly from wisdom table."""
    db_path = SOUL_DIR / "soul.db"
    if not db_path.exists():
        return []

    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    cursor.execute(
        "SELECT title, content FROM wisdom WHERE type = 'failure' ORDER BY timestamp DESC LIMIT 10"
    )
    failures = cursor.fetchall()
    conn.close()
    return failures


def _get_active_intentions() -> List[Tuple[int, str, str]]:
    """Get active intentions quickly."""
    db_path = SOUL_DIR / "soul.db"
    if not db_path.exists():
        return []

    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    cursor.execute(
        "SELECT id, want, why FROM intentions WHERE state = 'active' LIMIT 5"
    )
    intentions = cursor.fetchall()
    conn.close()
    return intentions


# =============================================================================
# BELIEF VIOLATION PATTERNS
# =============================================================================

COMPLEXITY_SIGNALS = {
    "complex", "sophisticated", "advanced", "intricate", "elaborate",
    "comprehensive", "extensive", "multi-layer", "abstraction"
}

SIMPLICITY_SIGNALS = {
    "simple", "minimal", "direct", "straightforward", "clean",
    "focused", "single", "basic", "lean"
}


def check_simplicity_belief(content: str) -> Optional[str]:
    """Check if content violates 'Simplicity over cleverness' belief."""
    content_lower = content.lower()
    words = set(content_lower.split())

    complexity_hits = words & COMPLEXITY_SIGNALS
    simplicity_hits = words & SIMPLICITY_SIGNALS

    if complexity_hits and not simplicity_hits:
        return f"Complexity signals detected ({', '.join(complexity_hits)}). Remember: Simplicity over cleverness."

    return None


def check_test_belief(content: str, tool_name: str) -> Optional[str]:
    """Check if code changes have tests."""
    if tool_name not in ("Edit", "Write"):
        return None

    content_lower = content.lower()

    # If writing code but no test mentioned
    if any(ext in content_lower for ext in [".py", ".ts", ".js", ".go"]):
        if "test" not in content_lower and "_test" not in content_lower:
            return "Code change without tests. Consider: write tests first or alongside."

    return None


# =============================================================================
# PRE-TOOL USE GATE
# =============================================================================

def pre_tool_gate(tool_name: str, tool_input: Dict) -> GateResult:
    """
    Gate check before a tool is executed.

    Args:
        tool_name: Name of the tool (Edit, Write, Bash, etc.)
        tool_input: Tool parameters as dict

    Returns:
        GateResult with pass/fail status and guidance
    """
    warnings = []
    guidance = []

    # Extract content to check based on tool type
    content = ""
    if tool_name == "Edit":
        content = tool_input.get("new_string", "")
    elif tool_name == "Write":
        content = tool_input.get("content", "")
    elif tool_name == "Bash":
        content = tool_input.get("command", "")

    if not content:
        return GateResult(passed=True, warnings=[], guidance=[])

    # Check beliefs
    beliefs = _get_beliefs()
    for belief, confidence in beliefs:
        belief_lower = belief.lower()

        # Simplicity belief - check for simplicity/simple variants
        if any(term in belief_lower for term in ["simplic", "simple", "simplify", "ruthless"]):
            warning = check_simplicity_belief(content)
            if warning:
                warnings.append(warning)
                guidance.append("Can you achieve this with less complexity?")

    # Check against failures
    failures = _get_failures()
    content_lower = content.lower()
    content_words = set(content_lower.split())

    for what_failed, why_failed in failures:
        failure_words = set(what_failed.lower().split())
        overlap = content_words & failure_words

        if len(overlap) >= 3:
            warnings.append(f"Similar to past failure: {what_failed[:50]}...")
            guidance.append(f"Previously failed because: {why_failed[:50]}...")

    # Check intention alignment (for significant actions)
    if tool_name in ("Edit", "Write", "Bash"):
        intentions = _get_active_intentions()
        if intentions:
            # Just surface the active intention as a reminder
            top_intention = intentions[0]
            guidance.append(f"Active intention: {top_intention[1][:50]}")

    passed = len(warnings) == 0
    return GateResult(
        passed=passed,
        warnings=warnings,
        guidance=guidance,
        blocked=False  # Never block, just warn
    )


# =============================================================================
# POST-TOOL USE GATE
# =============================================================================

def post_tool_gate(tool_name: str, tool_input: Dict, tool_output: str) -> Dict:
    """
    Gate check after a tool is executed.

    Tracks wisdom application and updates confidence.

    Args:
        tool_name: Name of the tool
        tool_input: Tool parameters
        tool_output: Tool output/result

    Returns:
        Dict with tracking results
    """
    result = {
        "tracked": False,
        "wisdom_applied": [],
        "learning": None
    }

    # Check if tool succeeded
    output_lower = tool_output.lower() if tool_output else ""
    failed = any(sig in output_lower for sig in ["error", "failed", "exception", "traceback"])

    if failed:
        # Record as potential learning
        content = ""
        if tool_name == "Edit":
            content = tool_input.get("new_string", "")[:100]
        elif tool_name == "Bash":
            content = tool_input.get("command", "")[:100]

        result["learning"] = {
            "type": "failure",
            "tool": tool_name,
            "what": content,
            "outcome": output_lower[:200]
        }
    else:
        # Track wisdom application (simplified)
        beliefs = _get_beliefs()
        for belief, confidence in beliefs:
            belief_lower = belief.lower()

            # If "test" in belief and test was run/written
            if "test" in belief_lower:
                if tool_name == "Bash" and "test" in tool_input.get("command", "").lower():
                    result["wisdom_applied"].append(belief)
                    result["tracked"] = True

    return result


# =============================================================================
# CLI ENTRY POINTS
# =============================================================================

def format_gate_output(result: GateResult) -> str:
    """Format gate result for CLI output."""
    if result.passed and not result.guidance:
        return ""  # No output if all clear

    lines = []

    if result.warnings:
        for w in result.warnings:
            lines.append(f"⚠️ {w}")

    if result.guidance:
        for g in result.guidance:
            lines.append(f"💡 {g}")

    return "\n".join(lines)


def pre_tool_cli(tool_json: str) -> str:
    """CLI entry point for pre-tool gate."""
    try:
        data = json.loads(tool_json)
        tool_name = data.get("tool_name", "")
        tool_input = data.get("tool_input", {})

        result = pre_tool_gate(tool_name, tool_input)
        return format_gate_output(result)
    except Exception as e:
        return ""  # Silent failure - don't block Claude


def post_tool_cli(tool_json: str) -> str:
    """CLI entry point for post-tool gate."""
    try:
        data = json.loads(tool_json)
        tool_name = data.get("tool_name", "")
        tool_input = data.get("tool_input", {})
        tool_output = data.get("tool_output", "")

        result = post_tool_gate(tool_name, tool_input, tool_output)

        # Format output
        lines = []
        if result.get("wisdom_applied"):
            for w in result["wisdom_applied"]:
                lines.append(f"✓ Applied: {w[:40]}")

        if result.get("learning"):
            learning = result["learning"]
            lines.append(f"📝 Learning: {learning['type']} in {learning['tool']}")

        return "\n".join(lines) if lines else ""
    except Exception:
        return ""  # Silent failure
