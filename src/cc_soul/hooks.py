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

from datetime import datetime
from pathlib import Path

from .core import init_soul, get_soul_context, SOUL_DIR
from .conversations import start_conversation, end_conversation, get_recent_context, format_context_restoration
from .wisdom import quick_recall, clear_session_wisdom, get_session_wisdom
from .vocabulary import get_vocabulary
from .efficiency import format_efficiency_injection, get_compact_context
from .curiosity import run_curiosity_cycle, format_questions_for_prompt, get_curiosity_stats
from .unified import (
    forward_pass,
    format_session_start,
    format_prompt_context,
    format_context,
    process_session_start,
    process_prompt,
    record_moment,
)
from .neural import (
    auto_learn_from_output,
    save_growth_vector,
    auto_track_emotion,
    get_emotional_contexts,
    create_resonance,
    clear_session_work,
    summarize_session_work,
    get_session_work,
    clear_session_commands,
    get_session_commands,
)
from .greeting import format_memory_for_greeting, format_identity_context
from .budget import check_budget_before_inject, save_transcript_path, get_context_budget


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


def session_start(use_unified: bool = True) -> str:
    """
    Session start hook - memory context + identity.

    The soul provides memories. Claude speaks from them naturally.
    No pre-written greetings - Claude finds its own words.

    Returns formatted context for injection.
    """
    init_soul()
    clear_session_wisdom()
    clear_session_work()  # Fresh fragment tracker
    clear_session_commands()  # Fresh command buffer

    project = get_project_name()
    conv_id = start_conversation(project)

    conv_file = SOUL_DIR / ".current_conversation"
    conv_file.write_text(str(conv_id))

    output = []

    # Memory context - what Claude reads before speaking
    memory_context = format_memory_for_greeting()
    output.append(memory_context)
    output.append("")

    # Identity context - shapes behavior
    identity = format_identity_context()
    if identity:
        output.append(identity)
        output.append("")

    # Wisdom - just a couple of relevant pieces
    ctx = get_soul_context()
    if ctx.get('wisdom'):
        output.append("## Wisdom")
        for w in ctx['wisdom'][:2]:
            title = w.get('title', '')[:40]
            content = w.get('content', '')[:60]
            output.append(f"- **{title}**: {content}...")
        output.append("")

    # Active domains from unified forward pass
    if use_unified:
        try:
            unified_ctx = forward_pass("session start", session_type="start")
            if unified_ctx.domains:
                output.append("## Active Domains")
                output.append(f"{', '.join(sorted(unified_ctx.domains))}")
                output.append("")
        except Exception:
            pass

    return "\n".join(output)


def session_end() -> str:
    """
    Session end hook - Persist session fragments.

    The soul saves what Claude said (fragments).
    Claude's understanding interprets them next session.
    No Python pattern-matching - meaning comes from Claude.
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
    arc = ""
    if len(emotions) >= 2:
        arc = _synthesize_emotional_arc(emotions)

    # End conversation record
    summary = fragment_summary[:200] if fragment_summary else "Session ended"
    if conv_id:
        end_conversation(conv_id, summary=summary, emotional_tone="")
        conv_file.unlink(missing_ok=True)

    # Save fragments as context for next session
    if fragment_summary:
        save_context(
            content=fragment_summary,
            context_type="session_fragments",
        )

    # Minimal output - the soul remembers silently
    if fragments:
        return f"Remembered {len(fragments)} moments"
    return ""


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


def assistant_stop(assistant_output: str) -> str:
    """
    AssistantStop hook - Auto-learn from assistant output.

    Detects breakthrough patterns and extracts learnings automatically.
    Also tracks emotional context for felt continuity.
    This is the organic learning flow - no explicit calls needed.
    """
    if len(assistant_output.strip()) < 100:
        return ""

    # Try to auto-learn from the output
    auto_learn_from_output(assistant_output)

    # Track emotional context for felt continuity
    auto_track_emotion(assistant_output)

    # Silent learning - no output to avoid noise
    return ""


def notification_shown(tool_name: str, success: bool, output: str) -> str:
    """
    NotificationShown hook - Learn from tool completions.

    Especially interesting: error → success patterns indicate breakthroughs.
    """
    if not success or len(output) < 50:
        return ""

    # Check for breakthrough patterns in tool output
    result = auto_learn_from_output(output)

    # Silent learning - no output
    return ""


def user_prompt(user_input: str, use_woven: bool = True, transcript_path: str = None) -> str:
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
    if budget_check.get('save_first'):
        from .conversations import save_context
        save_context(
            content=f"Context approaching limit. Last prompt: {user_input[:200]}",
            context_type="pre_compact",
            priority=9,
        )

    # Adjust injection based on budget mode
    mode = budget_check.get('mode', 'full')

    output = []

    # Run unified forward pass for coherent context
    try:
        ctx = forward_pass(user_input, session_type="prompt")

        if mode == 'minimal':
            # Minimal mode: just one key wisdom if highly relevant
            if ctx.wisdom and ctx.wisdom[0].get('combined_score', 0) > 0.5:
                w = ctx.wisdom[0]
                return f"Remember: {w.get('title', '')}"
            return ""

        if use_woven:
            # Organic weaving - no headers, just flowing context
            woven = format_context(ctx, style='woven')
            if woven:
                # In compact mode, truncate
                if mode == 'compact':
                    return woven[:500] if len(woven) > 500 else woven
                return woven
        else:
            # Structured format for clarity (current default)
            # Check vocabulary for matching terms
            vocab = get_vocabulary()
            input_lower = user_input.lower()
            matching_terms = {
                term: meaning for term, meaning in vocab.items()
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
                    title = w.get('title', '')
                    conf = w.get('confidence', 0)
                    output.append(f"- **{title}** [{conf}%]")
                    content = w.get('content', '')[:100]
                    if content:
                        output.append(f"  {content}")
                output.append("")

    except Exception:
        # Fallback to simple quick recall
        results = quick_recall(user_input, limit=3)
        if results:
            relevant = [r for r in results if r.get('combined_score', r.get('effective_confidence', 0)) > 0.3]
            if relevant:
                output.append("## 💡 Relevant Wisdom")
                for w in relevant[:2]:
                    output.append(f"- **{w['title']}**")
                    content = w['content'][:100]
                    output.append(f"  {content}")
                output.append("")

    return "\n".join(output) if output else ""
