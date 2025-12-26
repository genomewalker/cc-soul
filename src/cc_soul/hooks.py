"""
Claude Code hooks for soul integration.

These hooks integrate with Claude Code to automatically:
1. Load soul context at session start
2. Inject relevant wisdom during work
3. Track conversations at session end
"""

from datetime import datetime
from pathlib import Path

from .core import init_soul, get_soul_context, SOUL_DIR
from .conversations import start_conversation, end_conversation
from .wisdom import quick_recall, clear_session_wisdom, get_session_wisdom


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


def session_start() -> str:
    """
    Session start hook - Load soul context.

    Returns formatted context for injection.
    """
    init_soul()

    # Clear session wisdom log for new session
    clear_session_wisdom()

    project = get_project_name()
    conv_id = start_conversation(project)

    conv_file = SOUL_DIR / ".current_conversation"
    conv_file.write_text(str(conv_id))

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

    output.append("---")
    output.append("*Soul loaded. I remember who we are.*")

    return "\n".join(output)


def session_end() -> str:
    """
    Session end hook - Close the conversation record.

    Shows wisdom that was applied during the session.
    """
    conv_file = SOUL_DIR / ".current_conversation"

    if conv_file.exists():
        try:
            conv_id = int(conv_file.read_text().strip())
            end_conversation(conv_id, summary="Session ended", emotional_tone="neutral")
            conv_file.unlink()
        except (ValueError, FileNotFoundError):
            pass

    # Show session wisdom summary
    output = ["\n# 🌙 Session Complete\n"]

    session_wisdom = get_session_wisdom()
    if session_wisdom:
        output.append(f"## Wisdom Applied This Session ({len(session_wisdom)})")
        for w in session_wisdom:
            output.append(f"- **{w['title']}**")
            if w.get('context'):
                output.append(f"  Context: {w['context'][:60]}...")
        output.append("")

    return "\n".join(output)


def user_prompt(user_input: str) -> str:
    """
    UserPromptSubmit hook - Inject relevant wisdom for the task at hand.

    Uses quick_recall (keyword-based) instead of semantic_recall to avoid
    loading the embedding model on every prompt. This reduces latency from
    ~2s to ~50ms.
    """
    if len(user_input.strip()) < 20:
        return ""

    results = quick_recall(user_input, limit=3)

    if not results:
        return ""

    relevant = [r for r in results if r.get('combined_score', r.get('effective_confidence', 0)) > 0.3]

    if not relevant:
        return ""

    output = []
    output.append("## 💡 Relevant Wisdom")
    output.append("")

    for w in relevant[:2]:
        conf = w.get('effective_confidence', w.get('confidence', 0))
        type_icon = {
            'pattern': '🔄',
            'principle': '💎',
            'failure': '⚠️',
            'insight': '💡',
            'preference': '👤',
            'term': '📖'
        }.get(w['type'], '•')

        output.append(f"- {type_icon} **{w['title']}** [{conf:.0%}]")
        content = w['content'][:120] + "..." if len(w['content']) > 120 else w['content']
        output.append(f"  {content}")
        output.append("")

    return "\n".join(output)
