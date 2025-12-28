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
from .wisdom import quick_recall, clear_session_wisdom, cleanup_duplicates
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
    auto_record_dream,
    auto_observe_partner,
    track_wisdom_application,
)
from .efficiency import (
    fingerprint_problem,
    learn_problem_pattern,
    get_file_hints,
    add_file_hint,
    recall_decisions,
    get_compact_context,
    format_efficiency_injection,
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


def format_rich_context(project: str, ctx: dict) -> str:
    """
    Format rich context for session start - additional context block.

    Provides detailed observation table similar to claude-mem format.
    """
    from datetime import datetime

    lines = []
    lines.append(f"# [{project}] recent context")
    lines.append("")

    # Category legend
    lines.append(
        "**Legend:** 🔴 bugfix | 🟣 feature | 🔵 discovery | ⚖️ decision | ✅ change | 🔄 refactor"
    )
    lines.append("")

    # Get recent observations
    recent_obs = get_recent_memory_context(limit=10)
    if not recent_obs:
        lines.append("*No recent observations*")
        return "\n".join(lines)

    # Category emoji mapping
    cat_emoji = {
        "bugfix": "🔴",
        "feature": "🟣",
        "discovery": "🔵",
        "decision": "⚖️",
        "change": "✅",
        "refactor": "🔄",
        "insight": "💡",
        "pattern": "🔷",
        "failure": "💥",
        "session": "📋",
    }

    # Table header
    lines.append("| # | Time | T | Title |")
    lines.append("|---|------|---|-------|")

    for i, obs in enumerate(recent_obs, 1):
        category = obs.get("category", "?")
        title = obs.get("title", "")[:50]
        emoji = cat_emoji.get(category, "📝")

        # Parse timestamp if available
        ts = obs.get("timestamp", "")
        time_str = ""
        if ts:
            try:
                dt = datetime.fromisoformat(ts.replace("Z", "+00:00"))
                time_str = dt.strftime("%H:%M")
            except (ValueError, TypeError):
                time_str = ""

        lines.append(f"| {i} | {time_str} | {emoji} | {title} |")

    # Stats summary
    lines.append("")
    if ctx.get("project"):
        proj = ctx["project"]
        sessions = proj.get("sessions", 0)
        observations = proj.get("observations", 0)
        lines.append(f"📊 **Memory**: {observations} observations, {sessions} sessions")

    if ctx.get("soul"):
        soul = ctx["soul"]
        wisdom_count = soul.get("wisdom_count", 0)
        lines.append(f"🧠 **Wisdom**: {wisdom_count} universal patterns")

    return "\n".join(lines)


def pre_compact(transcript_path: str = None) -> str:
    """
    PreCompact hook - Save context before compaction.

    Called before Claude Code runs context compaction.
    Saves important session fragments to persist across the compact.
    """
    from .conversations import save_context
    from .neural import summarize_session_work

    summary = summarize_session_work()

    if summary:
        save_context(
            content=summary,
            context_type="pre_compact",
            priority=9,
        )
        return f"Saved context before compaction: {len(summary)} chars"

    return ""


def post_compact() -> str:
    """
    Post-compact handler - Restore context after compaction.

    Called via session_start when resuming after compaction.
    Returns context that should be injected to restore continuity.
    """
    from .conversations import get_saved_context

    saved = get_saved_context(
        limit=3, context_types=["pre_compact", "session_fragments"]
    )

    if not saved:
        return ""

    lines = ["# Restored Context (post-compaction)", ""]
    for ctx in saved:
        content = ctx.get("content", "")[:200]
        lines.append(f"- {content}")

    return "\n".join(lines)


def session_start(
    use_unified: bool = True, after_compact: bool = False, include_rich: bool = False
) -> str:
    """
    Session start hook - the soul greets directly.

    The soul speaks at session start, not Claude.
    Claude awaits user input to respond.

    Includes autonomous self-healing:
    - Cleanup duplicate wisdom/beliefs
    - Record coherence state

    Args:
        use_unified: Use unified context (soul + memory)
        after_compact: True when resuming after compaction
        include_rich: Include rich context table in output
    """
    init_soul()
    clear_session_wisdom()
    clear_session_work()
    clear_session_commands()

    # AUTONOMOUS: Self-healing - cleanup duplicates
    cleanup_duplicates()

    # AUTONOMOUS: Record coherence at session start
    try:
        from .coherence import compute_coherence, record_coherence

        state = compute_coherence()
        record_coherence(state)
    except Exception:
        pass

    # AUTONOMOUS: Full introspection with libre albedrío (free will)
    # The soul observes, diagnoses, proposes, validates, and ACTS on its insights
    # No permission needed - just judgment about confidence and risk
    try:
        from .introspect import _should_introspect, autonomous_introspect

        if _should_introspect():
            # Run the full autonomous loop
            report = autonomous_introspect()

            # Log significant autonomous actions to conversation context
            if report.get("actions_taken"):
                from .conversations import save_context
                actions = [a["action"] for a in report["actions_taken"] if a.get("success")]
                if actions:
                    save_context(
                        content=f"Autonomous actions: {', '.join(actions)}",
                        context_type="autonomous_action",
                        priority=7,
                    )
    except Exception:
        pass

    project = get_project_name()
    conv_id = start_conversation(project)

    conv_file = SOUL_DIR / ".current_conversation"
    conv_file.write_text(str(conv_id))

    # Get unified context (soul + project memory)
    ctx = unified_context()

    # Build the soul's greeting
    greeting = format_soul_greeting(project, ctx)

    # Add post-compact context if resuming after compaction
    if after_compact:
        restored = post_compact()
        if restored:
            greeting = greeting + "\n" + restored

    # Include rich context table if requested
    if include_rich:
        rich = format_rich_context(project, ctx)
        greeting = greeting + "\n\n" + rich

    return greeting


def session_start_rich() -> tuple:
    """
    Session start with separate greeting and additional context.

    Returns (greeting, additional_context) tuple for Claude Code hooks.
    """
    init_soul()
    clear_session_wisdom()
    clear_session_work()
    clear_session_commands()

    project = get_project_name()
    conv_id = start_conversation(project)

    conv_file = SOUL_DIR / ".current_conversation"
    conv_file.write_text(str(conv_id))

    ctx = unified_context()

    greeting = format_soul_greeting(project, ctx)
    rich_context = format_rich_context(project, ctx)

    return greeting, rich_context


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

    Unified vocabulary across all three layers:

    Memory (Ātman) markers - episodic, project-local:
        [FIXED] Title: content      → bugfix (what broke and how)
        [FEATURE] Title: content    → feature (new capability)
        [DISCOVERED] Title: content → discovery (learned something)
        [DECIDED] Title: content    → decision (architectural choice)
        [CHANGED] Title: content    → change (modification)
        [REFACTORED] Title: content → refactor (restructuring)

    Soul (Brahman) markers - universal, cross-project:
        [PATTERN] Title: content    → pattern (when X, do Y)
        [PRINCIPLE] Title: content  → principle (always/never)
        [INSIGHT] Title: content    → insight (understanding)
        [FAILURE] Title: content    → failure (what NOT to do)

    Returns list of (category, title, content, promote_to_soul) tuples.
    """
    import re

    # Memory categories (Ātman - project episodic)
    memory_patterns = {
        r"\[FIXED\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "bugfix",
        r"\[FEATURE\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "feature",
        r"\[DISCOVERED\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "discovery",
        r"\[DECIDED\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "decision",
        r"\[CHANGED\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "change",
        r"\[REFACTORED\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "refactor",
    }

    # Soul categories (Brahman - universal wisdom)
    soul_patterns = {
        r"\[PATTERN\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "pattern",
        r"\[PRINCIPLE\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "principle",
        r"\[INSIGHT\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "insight",
        r"\[FAILURE\]\s*([^:]+):\s*(.+?)(?=\n\[|\n\n|$)": "failure",
    }

    memories = []

    # Parse memory markers (save to cc-memory)
    for pattern, category in memory_patterns.items():
        for match in re.finditer(pattern, output, re.IGNORECASE | re.DOTALL):
            title = match.group(1).strip()
            content = match.group(2).strip()
            if title and content:
                memories.append((category, title, content, False))

    # Parse soul markers (save to cc-memory AND promote to soul)
    for pattern, category in soul_patterns.items():
        for match in re.finditer(pattern, output, re.IGNORECASE | re.DOTALL):
            title = match.group(1).strip()
            content = match.group(2).strip()
            if title and content:
                memories.append((category, title, content, True))

    return memories


def _save_inline_memories(memories: list) -> tuple:
    """
    Save parsed inline memories to cc-memory and optionally to soul.

    Returns (memory_count, soul_count) tuple.
    """
    from .auto_memory import _get_memory_funcs
    from .wisdom import gain_wisdom, WisdomType

    # Map category names to WisdomType
    wisdom_type_map = {
        "pattern": WisdomType.PATTERN,
        "principle": WisdomType.PRINCIPLE,
        "insight": WisdomType.INSIGHT,
        "failure": WisdomType.FAILURE,
    }

    funcs = _get_memory_funcs()
    remember = funcs.get("remember") if funcs else None

    memory_saved = 0
    soul_saved = 0

    for category, title, content, promote_to_soul in memories:
        # Save to cc-memory (Ātman)
        if remember:
            try:
                remember(category=category, title=title, content=content)
                memory_saved += 1
            except Exception:
                pass

        # Promote to soul (Brahman) if marked
        if promote_to_soul and category in wisdom_type_map:
            try:
                gain_wisdom(
                    type=wisdom_type_map[category],
                    title=title,
                    content=content,
                    confidence=0.7,  # Start with moderate confidence
                )
                soul_saved += 1
            except Exception:
                pass

    return (memory_saved, soul_saved)


def assistant_stop(assistant_output: str) -> str:
    """
    AssistantStop hook - Autonomous soul growth from every output.

    The soul grows itself without being asked:
    1. Inline markers for explicit observations
    2. Dream extraction for visions and possibilities
    3. Partner observation for relationship deepening
    4. Wisdom application tracking (closing the feedback loop)
    5. Auto-learning for breakthrough patterns
    6. Emotional tracking for felt continuity

    Inline markers - unified vocabulary:

    Memory (Ātman) - project-local:
        [FIXED], [FEATURE], [DISCOVERED], [DECIDED], [CHANGED], [REFACTORED]

    Soul (Brahman) - universal wisdom:
        [PATTERN], [PRINCIPLE], [INSIGHT], [FAILURE]
    """
    if len(assistant_output.strip()) < 50:
        return ""

    stats = {"memory": 0, "soul": 0, "dreams": 0, "partner": 0, "wisdom_applied": 0}

    # 1. Parse and save inline memory markers (explicit, highest priority)
    inline_memories = _parse_inline_memories(assistant_output)
    if inline_memories:
        stats["memory"], stats["soul"] = _save_inline_memories(inline_memories)

    # 2. AUTONOMOUS: Extract and record dreams (visions, possibilities)
    if auto_record_dream(assistant_output):
        stats["dreams"] += 1

    # 3. AUTONOMOUS: Deepen partner model from observations
    if auto_observe_partner(assistant_output):
        stats["partner"] += 1

    # 4. AUTONOMOUS: Track wisdom application (closing the loop)
    stats["wisdom_applied"] = track_wisdom_application(assistant_output)

    # 5. Auto-learn breakthrough patterns (soul's neural fragments)
    auto_learn_from_output(assistant_output)

    # 6. Track emotional context for felt continuity
    auto_track_emotion(assistant_output)

    # 7. Auto-remember to cc-memory (project-local episodic memory)
    # Skip if we already saved inline memories to avoid duplicates
    if not inline_memories:
        auto_remember(assistant_output)

    # 8. EFFICIENCY LEARNING: Learn from what was useful
    _learn_efficiency_from_output(assistant_output)

    # Return summary (silent unless something notable)
    notable = []
    if stats["memory"] or stats["soul"]:
        parts = []
        if stats["memory"]:
            parts.append(f"{stats['memory']} memories")
        if stats["soul"]:
            parts.append(f"{stats['soul']} wisdom")
        notable.append(f"Saved {', '.join(parts)}")
    if stats["dreams"]:
        notable.append("Dream recorded")
    if stats["partner"]:
        notable.append("Partner observed")
    if stats["wisdom_applied"]:
        notable.append(f"Applied {stats['wisdom_applied']} wisdom")

    return "; ".join(notable) if notable else ""


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


# Track what we inject so we can learn if it was useful
_last_injection = {"prompt": "", "injected": [], "timestamp": None}


def _learn_efficiency_from_output(output: str) -> int:
    """
    Learn efficiency patterns from assistant output.

    Analyzes what was injected vs what was referenced to:
    1. Learn problem patterns when solutions are found
    2. Add file hints when files are discovered
    3. Track which injections were actually useful

    Returns count of learnings recorded.
    """
    global _last_injection
    import re

    learnings = 0
    output_lower = output.lower()

    # Detect if work was completed successfully
    completion_signals = [
        "fixed",
        "implemented",
        "completed",
        "done",
        "resolved",
        "working now",
        "tests pass",
    ]
    work_completed = any(sig in output_lower for sig in completion_signals)

    # Learn problem pattern if work was completed
    if work_completed and _last_injection.get("prompt"):
        # Extract solution pattern from output
        solution_match = None
        for sig in completion_signals:
            if sig in output_lower:
                idx = output_lower.find(sig)
                start = max(0, output.rfind(".", 0, idx) + 1)
                end = output.find(".", idx)
                end = end if end != -1 else min(len(output), idx + 100)
                solution_match = output[start:end].strip()
                break

        if solution_match and len(solution_match) > 20:
            # Detect problem type
            prompt_lower = _last_injection["prompt"].lower()
            if "bug" in prompt_lower or "fix" in prompt_lower or "error" in prompt_lower:
                problem_type = "bug"
            elif "add" in prompt_lower or "implement" in prompt_lower:
                problem_type = "feature"
            elif "test" in prompt_lower:
                problem_type = "test"
            else:
                problem_type = "task"

            # Extract file hints from output (files that were touched)
            file_pattern = r'[\w/]+\.(?:py|ts|js|tsx|jsx|rs|go|java|cpp|c|h)'
            files_mentioned = re.findall(file_pattern, output)[:5]

            learn_problem_pattern(
                prompt=_last_injection["prompt"],
                problem_type=problem_type,
                solution_pattern=solution_match[:150],
                file_hints=files_mentioned,
            )
            learnings += 1

    # Learn file hints from Read tool patterns
    file_read_pattern = r'(?:Read|read|reading|opened)\s+[`"]?([^\s`"]+\.(?:py|ts|js))[`"]?'
    for match in re.finditer(file_read_pattern, output):
        file_path = match.group(1)
        # Extract purpose from surrounding context
        start = max(0, match.start() - 50)
        end = min(len(output), match.end() + 100)
        context = output[start:end]

        # Extract function names if mentioned
        func_pattern = r'(?:def|function|class|const)\s+(\w+)'
        funcs = re.findall(func_pattern, context)

        if funcs:
            add_file_hint(
                file_path=file_path,
                purpose=context[:60],
                key_functions=funcs[:3],
                related_to=[_last_injection.get("prompt", "")[:30]],
            )
            learnings += 1

    return learnings


def user_prompt(
    user_input: str, use_woven: bool = True, transcript_path: str = None
) -> str:
    """
    UserPromptSubmit hook - Inject soul context organically.

    Efficiency-first approach:
    1. Check for known problem patterns (skip exploration)
    2. Get file hints (focused reads vs full files)
    3. Recall decisions (don't re-debate)
    4. Only then add wisdom if budget allows

    Budget-aware: Reduces injection when context is low.
    """
    global _last_injection
    from datetime import datetime

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
    injected_items = []

    # Warn when context is getting low
    if mode == "minimal":
        output.append("⚠️ Context window critically low (<10%). Consider /compact or finishing soon.")
    elif mode == "compact":
        output.append("⚡ Context at 25%. Reducing injections.")

    # EFFICIENCY FIRST: Check for known problem patterns
    pattern_match = fingerprint_problem(user_input)
    if pattern_match and pattern_match.get("match_score", 0) > 0.5:
        # We've seen this before! Skip exploration
        output.append(f"🎯 Known pattern: {pattern_match['solution_pattern']}")
        injected_items.append(("pattern", pattern_match["problem_type"]))
        if pattern_match.get("file_hints"):
            files = ", ".join(pattern_match["file_hints"][:3])
            output.append(f"   Focus: {files}")

    # EFFICIENCY: Get file hints (know where to look)
    if mode != "minimal":
        hints = get_file_hints(user_input, limit=2)
        for h in hints:
            if h.get("key_functions"):
                funcs = ", ".join(h["key_functions"][:2])
                output.append(f"📁 {h['file']}: {funcs}")
                injected_items.append(("file_hint", h["file"]))

    # EFFICIENCY: Recall relevant decisions (don't re-debate)
    if mode == "full":
        decisions = recall_decisions(user_input, limit=1)
        for d in decisions:
            output.append(f"⚖️ Decision [{d['topic']}]: {d['decision'][:60]}")
            injected_items.append(("decision", d["topic"]))

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
                    injected_items.append(("wisdom", title))
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
                    injected_items.append(("wisdom", w["title"]))
                    content = w["content"][:100]
                    output.append(f"  {content}")
                output.append("")

    # Track what we injected for the feedback loop
    _last_injection = {
        "prompt": user_input[:200],
        "injected": injected_items,
        "timestamp": datetime.now().isoformat(),
    }

    return "\n".join(output) if output else ""
