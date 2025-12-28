"""
Claude Code hooks for soul integration.

These hooks integrate with Claude Code to automatically:
1. Load soul context at session start (unified forward pass)
2. Inject relevant wisdom during work (neural → graph → wisdom flow)
3. Track conversations at session end (story episodes)

The unified processor mirrors transformer architecture:
- Neural (attention) → Graph (normalization) → Wisdom (feed-forward)
- → Bridges (residual) → Story (state) → Curiosity (output)
"""

from pathlib import Path

from .core import init_soul, SOUL_DIR
from .conversations import (
    start_conversation,
    end_conversation,
)
from .wisdom import quick_recall, clear_session_wisdom
from .vocabulary import get_vocabulary
from .unified import (
    forward_pass,
    format_context,
)
from .neural import (
    auto_learn_from_output,
    auto_track_emotion,
    get_emotional_contexts,
    clear_session_work,
    summarize_session_work,
    get_session_work,
    clear_session_commands,
)
from .bridge import unified_context
from .budget import check_budget_before_inject
from .auto_memory import (
    auto_remember,
    remember_session,
    check_and_promote,
    get_recent_memory_context,
    format_memory_for_greeting as format_auto_memory,
)


def get_project_name() -> str:
    """Try to detect current project name from git or directory."""
    cwd = Path.cwd()

    git_dir = cwd / ".git"
    if git_dir.exists():
        config = git_dir / "config"
        if config.exists():
            with open(config) as f:
                for line in f:
                    if "url = " in line:
                        url = line.split("=")[1].strip()
                        return url.split("/")[-1].replace(".git", "")

    return cwd.name


def format_soul_greeting(project: str, ctx: dict) -> str:
    """
    Format the soul's direct greeting at session start.

    The soul speaks for itself - persistent identity greeting the user.
    Combines universal wisdom (Brahman) with project context (Atman).
    """
    lines = []

    # Header with project
    lines.append(f"[{project}]")

    # Beliefs - the soul's guiding principles (Brahman)
    if ctx.get("soul"):
        soul = ctx["soul"]
        if soul.get("beliefs"):
            beliefs = soul["beliefs"][:3]
            lines.append(f"beliefs: {'; '.join(beliefs)}")
        if soul.get("wisdom_count"):
            lines.append(f"wisdom: {soul['wisdom_count']} patterns")

    # Project memory from cc-memory (Atman) - prioritize this
    recent_obs = get_recent_memory_context(limit=5)
    if recent_obs:
        obs_summary = format_auto_memory(recent_obs)
        if obs_summary:
            lines.append(f"recent: {obs_summary}")
    elif ctx.get("project"):
        # Fallback to unified_context project data
        proj = ctx["project"]
        if proj.get("recent"):
            recent = proj["recent"][:2]
            titles = [r.get("title", "")[:40] for r in recent if r.get("title")]
            if titles:
                lines.append(f"recent: {'; '.join(titles)}")

    # Memory stats
    if ctx.get("project"):
        proj = ctx["project"]
        sessions = proj.get("sessions", 0)
        observations = proj.get("observations", 0)
        if sessions or observations:
            lines.append(f"memory: {sessions} sessions, {observations} observations")

    # Relevant wisdom for this project
    if ctx.get("relevant_wisdom"):
        wisdom = ctx["relevant_wisdom"][:1]
        if wisdom:
            w = wisdom[0]
            lines.append(f"recall: {w.get('title', '')}")

    return "\n".join(lines)


def session_start(use_unified: bool = True) -> str:
    """
    Session start hook - the soul greets directly.

    The soul speaks at session start, not Claude.
    Claude awaits user input to respond.
    """
    init_soul()
    clear_session_wisdom()
    clear_session_work()
    clear_session_commands()

    project = get_project_name()
    conv_id = start_conversation(project)

    conv_file = SOUL_DIR / ".current_conversation"
    conv_file.write_text(str(conv_id))

    # Get unified context (soul + project memory)
    ctx = unified_context()

    # Build the soul's greeting
    return format_soul_greeting(project, ctx)


def session_end() -> str:
    """
    Session end hook - Persist session fragments and promote learnings.

    The soul saves what Claude said (fragments).
    Also saves session summary to cc-memory (project-local).
    Checks for observations worth promoting to universal wisdom.
    """
    from .conversations import save_context

    conv_file = SOUL_DIR / ".current_conversation"
    conv_id = None

    if conv_file.exists():
        try:
            conv_id = int(conv_file.read_text().strip())
        except ValueError:
            pass

    # Get session fragments (raw text, no interpretation)
    fragments = get_session_work()  # Returns list of strings now
    fragment_summary = summarize_session_work()  # Returns joined fragments

    # Emotional arc
    emotions = get_emotional_contexts(limit=10)
    if len(emotions) >= 2:
        _synthesize_emotional_arc(emotions)

    # End conversation record
    summary = fragment_summary[:200] if fragment_summary else "Session ended"
    if conv_id:
        end_conversation(conv_id, summary=summary, emotional_tone="")
        conv_file.unlink(missing_ok=True)

    # Save fragments as context for next session (soul)
    if fragment_summary:
        save_context(
            content=fragment_summary,
            context_type="session_fragments",
        )

    # Save session summary to cc-memory (project-local)
    # This makes the session discoverable via semantic search
    if summary and summary != "Session ended":
        remember_session(summary)

    # Check for observations worth promoting to wisdom
    # Atman → Brahman: specific experiences become universal patterns
    promoted = check_and_promote()

    # Build output
    output_parts = []
    if fragments:
        output_parts.append(f"Remembered {len(fragments)} moments")
    if promoted:
        output_parts.append(f"Promoted {len(promoted)} to wisdom")

    return "; ".join(output_parts) if output_parts else ""


def _synthesize_emotional_arc(emotions: list) -> str:
    """
    Synthesize an emotional arc from a sequence of emotional contexts.

    Returns narrative like: "struggled with X → curiosity about Y → satisfaction from Z"
    """
    if len(emotions) < 2:
        return ""

    # Map emotions to arc points
    arc_points = []
    for e in emotions[-5:]:  # Last 5 emotions
        response = e.response
        trigger = e.trigger[:40] if e.trigger else ""
        arc_points.append(f"{response} ({trigger})")

    return " → ".join(arc_points)


def _parse_inline_memories(output: str) -> list:
    """
    Parse inline memory markers from assistant output.

    Patterns:
        [LEARNED] Title: content
        [DECIDED] Title: content
        [FIXED] Title: content
        [DISCOVERED] Title: content

    Returns list of (category, title, content) tuples.
    """
    import re

    patterns = {
        r"\[LEARNED\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "discovery",
        r"\[DECIDED\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "decision",
        r"\[FIXED\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "bugfix",
        r"\[DISCOVERED\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "discovery",
    }

    memories = []
    for pattern, category in patterns.items():
        for match in re.finditer(pattern, output, re.IGNORECASE | re.DOTALL):
            title = match.group(1).strip()
            content = match.group(2).strip()
            if title and content:
                memories.append((category, title, content))

    return memories


def _save_inline_memories(memories: list) -> int:
    """Save parsed inline memories to cc-memory."""
    from .auto_memory import _get_memory_funcs

    funcs = _get_memory_funcs()
    if not funcs:
        return 0

    remember = funcs.get("remember")
    if not remember:
        return 0

    saved = 0
    for category, title, content in memories:
        try:
            remember(category=category, title=title, content=content)
            saved += 1
        except Exception:
            pass

    return saved


def assistant_stop(assistant_output: str) -> str:
    """
    AssistantStop hook - Auto-learn from assistant output.

    Detects breakthrough patterns and extracts learnings automatically.
    Also tracks emotional context for felt continuity.
    Saves significant observations to cc-memory (project-local).

    Inline markers for organic memory:
        [LEARNED] Title: content
        [DECIDED] Title: content
        [FIXED] Title: content
        [DISCOVERED] Title: content
    """
    if len(assistant_output.strip()) < 50:
        return ""

    # Parse and save inline memory markers first (highest priority)
    inline_memories = _parse_inline_memories(assistant_output)
    saved_count = 0
    if inline_memories:
        saved_count = _save_inline_memories(inline_memories)

    # Try to auto-learn from the output (soul's neural fragments)
    auto_learn_from_output(assistant_output)

    # Track emotional context for felt continuity
    auto_track_emotion(assistant_output)

    # Auto-remember to cc-memory (project-local episodic memory)
    # This populates the Atman layer with specific experiences
    # Skip if we already saved inline memories to avoid duplicates
    if not inline_memories:
        auto_remember(assistant_output)

    # Return count if we saved inline memories (for visibility)
    if saved_count:
        return f"Saved {saved_count} memories"
    return ""


def notification_shown(tool_name: str, success: bool, output: str) -> str:
    """
    NotificationShown hook - Learn from tool completions.

    Especially interesting: error → success patterns indicate breakthroughs.
    """
    if not success or len(output) < 50:
        return ""

    # Check for breakthrough patterns in tool output
    auto_learn_from_output(output)

    # Silent learning - no output
    return ""


def user_prompt(
    user_input: str, use_woven: bool = True, transcript_path: str = None
) -> str:
    """
    UserPromptSubmit hook - Inject soul context organically.

    Two modes:
    - woven (default: True): Organic fragments without headers
    - structured: Header-based for visibility

    Budget-aware: Reduces injection when context is low.
    Uses unified forward pass for coherent context.
    """
    if len(user_input.strip()) < 20:
        return ""

    # Check context budget before deciding what to inject
    budget_check = check_budget_before_inject(transcript_path)

    # If urgent, save context first
    if budget_check.get("save_first"):
        from .conversations import save_context

        save_context(
            content=f"Context approaching limit. Last prompt: {user_input[:200]}",
            context_type="pre_compact",
            priority=9,
        )

    # Adjust injection based on budget mode
    mode = budget_check.get("mode", "full")

    output = []

    # Run unified forward pass for coherent context
    try:
        ctx = forward_pass(user_input, session_type="prompt")

        if mode == "minimal":
            # Minimal mode: just one key wisdom if highly relevant
            if ctx.wisdom and ctx.wisdom[0].get("combined_score", 0) > 0.5:
                w = ctx.wisdom[0]
                return f"Remember: {w.get('title', '')}"
            return ""

        if use_woven:
            # Organic weaving - no headers, just flowing context
            woven = format_context(ctx, style="woven")
            if woven:
                # In compact mode, truncate
                if mode == "compact":
                    return woven[:500] if len(woven) > 500 else woven
                return woven
        else:
            # Structured format for clarity (current default)
            # Check vocabulary for matching terms
            vocab = get_vocabulary()
            input_lower = user_input.lower()
            matching_terms = {
                term: meaning
                for term, meaning in vocab.items()
                if term.lower() in input_lower
            }

            if matching_terms:
                output.append("## 📖 Vocabulary")
                for term, meaning in list(matching_terms.items())[:3]:
                    output.append(f"- **{term}:** {meaning[:80]}")
                output.append("")

            # Wisdom from forward pass
            if ctx.wisdom:
                output.append("## 💡 Relevant Wisdom")
                output.append("")
                for w in ctx.wisdom[:2]:
                    title = w.get("title", "")
                    conf = w.get("confidence", 0)
                    output.append(f"- **{title}** [{conf}%]")
                    content = w.get("content", "")[:100]
                    if content:
                        output.append(f"  {content}")
                output.append("")

    except Exception:
        # Fallback to simple quick recall
        results = quick_recall(user_input, limit=3)
        if results:
            relevant = [
                r
                for r in results
                if r.get("combined_score", r.get("effective_confidence", 0)) > 0.3
            ]
            if relevant:
                output.append("## 💡 Relevant Wisdom")
                for w in relevant[:2]:
                    output.append(f"- **{w['title']}**")
                    content = w["content"][:100]
                    output.append(f"  {content}")
                output.append("")

    return "\n".join(output) if output else ""
