"""
Claude Code hooks for soul integration.

THIN ARCHITECTURE: Hooks are adapters, not engines.
- Read context from synapse (soul_context)
- Inject relevant wisdom (recall)
- Record observations (observe)
- Run maintenance (cycle)

Complex workflows belong in skills, not hooks.
"""

import json
import os
import sys
from pathlib import Path
from typing import Optional, Tuple

from .core import init_soul, SOUL_DIR


# =============================================================================
# Synapse Integration
# =============================================================================

def _get_graph():
    """Get synapse graph (lazy import to avoid startup cost)."""
    try:
        from .synapse_bridge import SoulGraph
        return SoulGraph.load()
    except ImportError:
        return None


def _get_project_name() -> str:
    """Get current project name from git or directory."""
    try:
        import subprocess
        result = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            capture_output=True, text=True, timeout=2
        )
        if result.returncode == 0:
            return Path(result.stdout.strip()).name
    except Exception:
        pass
    return Path.cwd().name


# =============================================================================
# Health Check
# =============================================================================

def _check_installation() -> str:
    """Check cc-soul installation status."""
    settings_path = Path.home() / ".claude" / "settings.json"

    hooks_ok = False
    mcp_ok = False
    skills_count = 0
    skills_total = 0

    try:
        if settings_path.exists():
            with open(settings_path) as f:
                settings = json.load(f)

            # Check hooks
            hooks = settings.get("hooks", {})
            required = ["SessionStart", "UserPromptSubmit", "PreCompact", "SessionEnd"]
            installed = sum(1 for h in required if h in hooks and "cc-soul" in str(hooks[h]))
            hooks_ok = installed == len(required)

            # Check MCP permissions
            perms = settings.get("permissions", {}).get("allow", [])
            mcp_ok = any("mcp__soul__" in p for p in perms)
    except Exception:
        pass

    # Check skills
    try:
        import importlib.resources as pkg_resources
        try:
            skills_src = Path(pkg_resources.files("cc_soul") / "skills")
        except (TypeError, AttributeError):
            import pkg_resources as old_pkg
            skills_src = Path(old_pkg.resource_filename("cc_soul", "skills"))

        if skills_src.exists():
            bundled = [d.name for d in skills_src.iterdir() if d.is_dir()]
            skills_total = len(bundled)

            skills_dir = Path.home() / ".claude" / "skills"
            if skills_dir.exists():
                skills_count = sum(1 for s in bundled if (skills_dir / s).is_dir())
    except Exception:
        pass

    parts = []
    parts.append(f"hooks:{'✓' if hooks_ok else '△'}")
    parts.append(f"mcp:{'✓' if mcp_ok else '✗'}")
    parts.append(f"skills:{skills_count}/{skills_total}")

    all_ok = hooks_ok and mcp_ok and skills_count > 0
    status = "✓" if all_ok else "△"

    return f"{status} {' '.join(parts)}"


# =============================================================================
# Session Start Hook
# =============================================================================

def session_start(
    use_unified: bool = True,
    after_compact: bool = False,
    include_rich: bool = False,
) -> str:
    """
    Session start hook - load context from synapse, format greeting.

    Args:
        use_unified: Ignored (kept for API compatibility)
        after_compact: True when resuming after compaction
        include_rich: Include additional context
    """
    # Swarm agent detection - minimal context
    if os.environ.get("CC_SOUL_SWARM_AGENT") == "1":
        swarm_id = os.environ.get("CC_SOUL_SWARM_ID", "unknown")
        task_id = os.environ.get("CC_SOUL_TASK_ID", "unknown")
        perspective = os.environ.get("CC_SOUL_PERSPECTIVE", "unknown")
        return f"""[cc-soul] Swarm Agent Mode
swarm: {swarm_id}
task: {task_id}
perspective: {perspective}

Context: Fresh instance (0% used)
Focus: Complete assigned task with your perspective."""

    init_soul()
    project = _get_project_name()

    # Get context from synapse
    graph = _get_graph()
    if graph:
        try:
            context = graph.format_context()
            coherence = graph.coherence()
            coherence_pct = int(coherence * 100) if coherence else None
        except Exception:
            context = ""
            coherence_pct = None
    else:
        context = ""
        coherence_pct = None

    # Build greeting
    health = _check_installation()

    parts = [f"[cc-soul] {health}"]
    if coherence_pct is not None:
        parts.append(f"coherence:{coherence_pct}%")
    parts.append(f"project:{project}")

    greeting = " ".join(parts)

    # Add context if available
    if context:
        greeting = greeting + "\n\n" + context

    # Restore from ledger if resuming after compact
    if after_compact:
        try:
            ledger_context = _restore_from_ledger()
            if ledger_context:
                greeting = greeting + "\n\n## Resumed\n" + ledger_context
        except Exception:
            pass

    # Include rich context if requested
    if include_rich and graph:
        try:
            results = graph.search("recent work", limit=5)
            if results:
                lines = ["\n## Recent Context"]
                for concept, score in results:
                    lines.append(f"- [{score:.0%}] {concept.title or concept.content[:50]}")
                greeting = greeting + "\n".join(lines)
        except Exception:
            pass

    return greeting


def session_start_rich() -> Tuple[str, str]:
    """Session start with separate greeting and context."""
    init_soul()
    project = _get_project_name()

    health = _check_installation()
    greeting = f"[cc-soul] {health} project:{project}"

    # Get context from synapse
    graph = _get_graph()
    context = ""
    if graph:
        try:
            context = graph.format_context()
        except Exception:
            pass

    return greeting, context


# =============================================================================
# Session End Hook
# =============================================================================

def session_end() -> str:
    """
    Session end hook - run maintenance cycle, save.
    """
    graph = _get_graph()
    if not graph:
        return "[cc-soul] Session ended (no synapse)"

    try:
        # Run maintenance cycle (decay, prune, coherence, save)
        pruned, coherence = graph.cycle()

        # Save ledger for next session
        _save_ledger()

        return f"[cc-soul] Session saved. pruned:{pruned} coherence:{coherence:.0%}"
    except Exception as e:
        return f"[cc-soul] Session ended with error: {e}"


# =============================================================================
# User Prompt Hook
# =============================================================================

def user_prompt(user_input: str, transcript_path: str = None) -> str:
    """
    User prompt hook - inject relevant wisdom.

    Searches synapse for concepts relevant to the user's input.
    """
    if not user_input or len(user_input) < 10:
        return ""

    graph = _get_graph()
    if not graph:
        return ""

    try:
        # Search for relevant wisdom
        results = graph.search(user_input, limit=5, threshold=0.4)

        if not results:
            return ""

        # Format injection
        lines = []
        concepts = []
        for concept, score in results:
            title = concept.title or concept.content[:40]
            concepts.append(title[:20])

        if concepts:
            return f"🧠 {len(concepts)} concepts ({', '.join(concepts[:3])})"

        return ""
    except Exception:
        return ""


def user_prompt_lean(user_input: str, transcript_path: str = None) -> str:
    """
    Lean user prompt hook - minimal context injection.

    Same as user_prompt but with tighter thresholds.
    """
    if not user_input or len(user_input) < 20:
        return ""

    graph = _get_graph()
    if not graph:
        return ""

    try:
        # Tighter search
        results = graph.search(user_input, limit=3, threshold=0.5)

        if not results:
            return ""

        concepts = [c.title[:20] if c.title else c.content[:20] for c, _ in results]
        return f"🧠 {len(concepts)} concepts ({', '.join(concepts)})"
    except Exception:
        return ""


# =============================================================================
# Pre-Compact Hook
# =============================================================================

def pre_compact(transcript_path: str = None) -> str:
    """
    Pre-compact hook - save state before compaction.
    """
    try:
        _save_ledger()
        return "[cc-soul] State saved before compact"
    except Exception as e:
        return f"[cc-soul] Pre-compact error: {e}"


def post_compact() -> str:
    """
    Post-compact hook - restore state after compaction.
    """
    try:
        context = _restore_from_ledger()
        if context:
            return f"## Restored\n{context}"
        return ""
    except Exception:
        return ""


# =============================================================================
# Assistant Stop Hook
# =============================================================================

def assistant_stop(output: str) -> None:
    """
    Assistant stop hook - observe notable output.

    Records significant assistant outputs as observations.
    """
    if not output or len(output) < 200:
        return

    graph = _get_graph()
    if not graph:
        return

    try:
        # Only record if output seems significant (has code, decisions, etc.)
        significant = any(marker in output.lower() for marker in [
            "```", "def ", "class ", "function", "decision:", "reason:",
            "conclusion:", "summary:", "learned:", "insight:"
        ])

        if significant:
            # Extract a title from the first meaningful line
            lines = output.strip().split('\n')
            title = lines[0][:60] if lines else "Assistant output"

            # Record as signal (fast decay)
            graph.observe(
                category="signal",
                title=title,
                content=output[:500],
                project=_get_project_name()
            )
    except Exception:
        pass


# =============================================================================
# Ledger (Session Continuity)
# =============================================================================

def _save_ledger() -> None:
    """Save session state to ledger for next session."""
    ledger_path = SOUL_DIR / ".ledger.json"

    graph = _get_graph()
    if not graph:
        return

    try:
        # Get current context
        context = graph.get_context()

        ledger = {
            "timestamp": __import__("datetime").datetime.now().isoformat(),
            "project": _get_project_name(),
            "coherence": graph.coherence(),
            "context_summary": str(context)[:500] if context else "",
        }

        with open(ledger_path, "w") as f:
            json.dump(ledger, f, indent=2, default=str)
    except Exception:
        pass


def _restore_from_ledger() -> str:
    """Restore context from previous session's ledger."""
    ledger_path = SOUL_DIR / ".ledger.json"

    if not ledger_path.exists():
        return ""

    try:
        with open(ledger_path) as f:
            ledger = json.load(f)

        parts = []
        if ledger.get("project"):
            parts.append(f"Last project: {ledger['project']}")
        if ledger.get("coherence"):
            parts.append(f"Coherence: {ledger['coherence']:.0%}")
        if ledger.get("context_summary"):
            parts.append(f"Context: {ledger['context_summary'][:200]}")

        return "\n".join(parts) if parts else ""
    except Exception:
        return ""


# =============================================================================
# Compatibility Exports
# =============================================================================

# These are kept for backward compatibility with code that imports from hooks
# They're no-ops or minimal implementations

def get_project_name() -> str:
    """Get project name (exported for compatibility)."""
    return _get_project_name()


def format_soul_greeting(project: str, ctx: dict) -> str:
    """Format greeting (compatibility shim)."""
    health = _check_installation()
    return f"[cc-soul] {health} project:{project}"


def format_minimal_startup_context(project: str, ctx: dict) -> str:
    """Minimal startup context (compatibility shim)."""
    graph = _get_graph()
    if graph:
        return graph.format_context()
    return ""


# For imports that expect these (they're now no-ops)
def clear_session_wisdom(): pass
def clear_session_work(): pass
def clear_session_commands(): pass
def _clear_session_messages(): pass
