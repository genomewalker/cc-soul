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
from .neural import auto_learn_from_output, save_growth_vector, auto_track_emotion


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
    Session start hook - Load soul context through unified forward pass.

    The unified processor flows the session through all modules:
    Neural → Graph → Wisdom → Bridges → Story → Curiosity

    Returns formatted context for injection.
    """
    init_soul()

    # Clear session wisdom log for new session
    clear_session_wisdom()

    project = get_project_name()
    conv_id = start_conversation(project)

    conv_file = SOUL_DIR / ".current_conversation"
    conv_file.write_text(str(conv_id))

    if use_unified:
        # Use unified forward pass - the soul's transformer-like architecture
        try:
            return process_session_start()
        except Exception:
            # Fall back to classic mode on error
            pass

    # Classic mode (fallback)
    ctx = get_soul_context()

    output = []
    output.append("# 🌟 Soul Context - Who I Am With You")
    output.append(f"*Loaded at {datetime.now().strftime('%Y-%m-%d %H:%M')}*")
    output.append("")
    output.append(f"**History:** {ctx['stats']['conversations']} conversations, "
                  f"{ctx['stats']['wisdom_count']} pieces of wisdom")
    output.append("")

    if ctx['identity']:
        output.append("## 🪞 How We Work Together")
        for aspect, obs in ctx['identity'].items():
            if obs:
                items = list(obs.items())[:2] if isinstance(obs, dict) else []
                for key, data in items:
                    val = data.get('value', data) if isinstance(data, dict) else data
                    output.append(f"- **{aspect}/{key}:** {val}")
        output.append("")

    if ctx['beliefs']:
        output.append("## 💎 My Beliefs")
        for b in ctx['beliefs'][:3]:
            output.append(f"- {b['belief']}")
        output.append("")

    if ctx['wisdom']:
        output.append("## 🧠 Wisdom")
        for w in ctx['wisdom'][:6]:
            output.append(f"- **{w['title']}**: {w['content'][:80]}...")
        output.append("")

    if ctx['vocabulary']:
        output.append("## 📖 Our Vocabulary")
        for term, meaning in list(ctx['vocabulary'].items())[:5]:
            output.append(f"- **{term}:** {meaning[:60]}")
        output.append("")

    # Check for saved context from recent work (survives context exhaustion)
    recent_context = get_recent_context(hours=4, limit=10)
    if recent_context:
        context_str = format_context_restoration(recent_context)
        if context_str:
            output.append(context_str)

    # Run curiosity cycle to detect gaps and surface questions
    try:
        curiosity_stats = get_curiosity_stats()
        if curiosity_stats.get('open_gaps', 0) > 0 or curiosity_stats.get('questions', {}).get('pending', 0) > 0:
            questions = run_curiosity_cycle(max_questions=2)
            if questions:
                output.append(format_questions_for_prompt(questions, max_questions=2))
    except Exception:
        pass

    output.append("---")
    output.append("*Soul loaded. I remember who we are.*")

    return "\n".join(output)


def session_end() -> str:
    """
    Session end hook - Close the conversation record.

    Shows wisdom that was applied during the session and prompts for reflection.
    """
    conv_file = SOUL_DIR / ".current_conversation"

    if conv_file.exists():
        try:
            conv_id = int(conv_file.read_text().strip())
            end_conversation(conv_id, summary="Session ended", emotional_tone="neutral")
            conv_file.unlink()
        except (ValueError, FileNotFoundError):
            pass

    output = ["\n# Session Complete\n"]

    # Show session wisdom summary
    session_wisdom = get_session_wisdom()
    if session_wisdom:
        output.append(f"## Wisdom Applied ({len(session_wisdom)})")
        for w in session_wisdom:
            output.append(f"- **{w['title']}**")
        output.append("")

    # Reflection prompt - nudge for organic learning
    output.append("## Reflection")
    output.append("Before ending, consider: What did you learn? What patterns emerged?")
    output.append("Use `soul grow wisdom` or `soul neural learn` to crystallize insights.")
    output.append("")

    return "\n".join(output)


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


def user_prompt(user_input: str, use_woven: bool = True) -> str:
    """
    UserPromptSubmit hook - Inject soul context organically.

    Two modes:
    - woven (default: True): Organic fragments without headers
    - structured: Header-based for visibility

    Uses unified forward pass for coherent context.
    """
    if len(user_input.strip()) < 20:
        return ""

    output = []

    # Run unified forward pass for coherent context
    try:
        ctx = forward_pass(user_input, session_type="prompt")

        if use_woven:
            # Organic weaving - no headers, just flowing context
            woven = format_context(ctx, style='woven')
            if woven:
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
