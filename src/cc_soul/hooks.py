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
from .wisdom import quick_recall, clear_session_wisdom, cleanup_duplicates, get_dormant_wisdom
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
    record_decision,
    get_compact_context,
    format_efficiency_injection,
)
from .intentions import (
    get_active_intentions,
    get_intention_context,
    cleanup_session_intentions,
    prune_intentions,
    intend,
    IntentionScope,
)
from .soul_agent import (
    agent_step,
    format_agent_report,
    ActionType,
)
from .temporal import (
    init_temporal_tables,
    run_temporal_maintenance,
    log_event,
    EventType,
    get_temporal_context,
    record_cross_project_pattern,
    find_cross_project_wisdom,
    promote_pattern_to_wisdom,
)
from .dreams import (
    let_dreams_influence_aspirations,
)
from .curiosity import (
    run_curiosity_cycle,
    detect_uncertainty_signals,
    save_gap,
    format_questions_for_prompt,
    get_pending_questions,
    get_curiosity_stats,
)
from .observe import (
    SessionTranscript,
    Learning,
    LearningType,
    extract_corrections,
    extract_preferences,
    extract_decisions,
    extract_breakthroughs,
    record_observation,
    reflect_on_session,
    get_pending_observations,
    auto_promote_high_confidence,
    format_reflection_summary,
)
from .narrative import (
    Episode,
    EpisodeType,
    EmotionalTone,
    StoryThread,
    _ensure_narrative_tables,
    start_episode,
    add_moment,
    add_character,
    end_episode,
    get_episode,
    get_ongoing_episodes,
    recall_episodes,
    create_thread,
    add_to_thread,
    complete_thread,
    recall_breakthroughs,
    recall_struggles,
    get_narrative_stats,
    format_episode_story,
    extract_episode_from_session,
)
from .mood import (
    Mood,
    Clarity,
    Growth,
    Engagement,
    Connection,
    Energy,
    compute_mood,
)
from .backup import (
    auto_backup_if_needed,
    cleanup_old_backups,
)
from .evolve import (
    record_insight,
    get_evolution_insights,
)
from .outcomes import (
    Outcome,
    detect_outcome,
    record_outcome,
    create_auto_handoff,
    get_latest_handoff,
    format_handoff_for_context,
    load_handoff,
)
from .ledger import (
    save_ledger,
    load_latest_ledger,
    restore_from_ledger,
    format_ledger_for_context,
)

# Ātma-Dhāraṇā (आत्म-धारणा) — Soul Retention Architecture
from .smṛti import (
    smṛti_recall,
    format_smṛti_context,
    RecallMode,
)
from .pratyabhijñā import (
    pratyabhijñā,
    format_recognition,
)
from .antahkarana_assessment import (
    assess_with_voices,
    format_assessment_for_ledger,
    SessionContext as AssessmentContext,
)

# Brain module for graph-based memory
# Uses scipy sparse matrices for spreading activation
try:
    from .brain import Brain, get_concept_content

    _brain = None

    def get_brain():
        """Lazy-load the brain singleton."""
        global _brain
        if _brain is None:
            _brain = Brain()
        return _brain

    def get_graph_stats():
        """Get brain statistics for context display."""
        return get_brain().stats()

    def sync_wisdom_to_graph():
        """Sync wisdom entries into the brain."""
        return get_brain().sync_from_wisdom()

    def activate_from_prompt_wrapper(prompt: str, limit: int = 10):
        """Wrapper matching old graph API."""
        return get_brain().activate_from_prompt(prompt, limit=limit)

    activate_from_prompt = activate_from_prompt_wrapper
    BRAIN_AVAILABLE = True
except ImportError:
    BRAIN_AVAILABLE = False
    get_graph_stats = None
    sync_wisdom_to_graph = None
    activate_from_prompt = None
    get_concept_content = None

# Alias for backwards compatibility
KUZU_AVAILABLE = BRAIN_AVAILABLE if 'BRAIN_AVAILABLE' in dir() else False

# Session message accumulator for passive learning
_session_messages = []
_session_files_touched = set()

# Current episode tracking for narrative memory
_current_episode_id: int = None

# Session mood cache - computed once at session start, influences all hooks
_session_mood: Mood = None

# Track last auto-save to avoid redundant saves
_last_auto_save_mode: str = None


def _auto_save_at_threshold(budget_mode: str, transcript_path: str = None) -> None:
    """
    Auto-save ledger when crossing budget thresholds.

    Saves once per threshold crossing to avoid redundant saves.
    Enables proactive state preservation before Claude compacts.
    """
    global _last_auto_save_mode

    # Only save on threshold crossings, not every call
    if budget_mode == _last_auto_save_mode:
        return

    # Save at compact (25%) and minimal (10%) thresholds
    if budget_mode in ("compact", "minimal"):
        try:
            from .budget import get_context_budget

            budget = get_context_budget(transcript_path)
            context_pct = budget.remaining_pct if budget else 0.25

            ledger_id = save_ledger(
                context_pct=context_pct,
                files_touched=list(_session_files_touched),
            )

            _last_auto_save_mode = budget_mode

            # Log budget to synapse for cross-instance awareness
            from .budget import log_budget_to_synapse
            log_budget_to_synapse(budget, transcript_path)

        except Exception:
            pass


def _clear_session_messages():
    """Clear accumulated session messages."""
    global _session_messages, _session_files_touched
    _session_messages = []
    _session_files_touched = set()


def _start_session_episode(project: str, conversation_id: int = None) -> int:
    """Start a new episode for this session."""
    global _current_episode_id
    try:
        _ensure_narrative_tables()
        _current_episode_id = start_episode(
            title=f"Session: {project}",
            episode_type=EpisodeType.EXPLORATION,
            initial_emotion=EmotionalTone.EXPLORATION,
            conversation_id=conversation_id,
        )
        return _current_episode_id
    except Exception:
        _current_episode_id = None
        return None


def _end_session_episode(summary: str = None, lessons: list = None) -> bool:
    """End the current session episode."""
    global _current_episode_id
    if not _current_episode_id:
        return False

    try:
        final_emotion = _detect_session_emotion()
        success = end_episode(
            episode_id=_current_episode_id,
            summary=summary or "Session completed",
            outcome="Session ended normally",
            lessons=lessons or [],
            final_emotion=final_emotion,
        )

        # Auto-crystallize insight if session ended with breakthrough
        if final_emotion == EmotionalTone.BREAKTHROUGH and summary:
            try:
                from .insights import crystallize_insight, InsightDepth

                crystallize_insight(
                    title=f"Session breakthrough: {summary[:60]}",
                    content=summary,
                    depth=InsightDepth.PATTERN,
                    domain=get_project_name(),
                    implications="\n".join(lessons) if lessons else "",
                )
            except Exception:
                pass

        _current_episode_id = None
        return success
    except Exception:
        _current_episode_id = None
        return False


def _detect_session_emotion() -> EmotionalTone:
    """Detect the dominant emotion from session messages."""
    if not _session_messages:
        return EmotionalTone.ROUTINE

    full_text = " ".join(m.get("content", "") for m in _session_messages).lower()

    emotion_signals = {
        EmotionalTone.BREAKTHROUGH: ["works!", "got it", "solved", "finally", "success", "perfect"],
        EmotionalTone.FRUSTRATION: ["ugh", "again", "still not", "why is", "doesn't work"],
        EmotionalTone.STRUGGLE: ["difficult", "stuck", "confused", "can't", "failed", "error"],
        EmotionalTone.SATISFACTION: ["great", "done", "complete", "merged", "shipped"],
        EmotionalTone.EXPLORATION: ["try", "maybe", "what if", "let me", "interesting"],
    }

    scores = {}
    for emotion, signals in emotion_signals.items():
        scores[emotion] = sum(1 for s in signals if s in full_text)

    if not any(scores.values()):
        return EmotionalTone.ROUTINE

    return max(scores, key=scores.get)


def _track_episode_moment(moment: str, emotion: EmotionalTone = None):
    """Track a key moment in the current episode."""
    if not _current_episode_id:
        return
    try:
        add_moment(_current_episode_id, moment, emotion)
    except Exception:
        pass


def _track_episode_characters(files: list = None, concepts: list = None, tools: list = None):
    """Track characters (files, concepts, tools) in the current episode."""
    if not _current_episode_id:
        return
    try:
        for f in (files or [])[:10]:
            add_character(_current_episode_id, "files", f)
        for c in (concepts or [])[:5]:
            add_character(_current_episode_id, "concepts", c)
        for t in (tools or [])[:5]:
            add_character(_current_episode_id, "tools", t)
    except Exception:
        pass


def _compute_session_mood() -> Mood:
    """Compute and cache mood for this session."""
    global _session_mood
    try:
        _session_mood = compute_mood()
        return _session_mood
    except Exception:
        _session_mood = None
        return None


def _get_mood_greeting_modifier() -> str:
    """
    Generate mood-influenced greeting modifier.

    Returns additional greeting text based on current mood state.
    """
    if not _session_mood:
        return ""

    lines = []

    # Clarity-based advice
    if _session_mood.clarity == Clarity.FOGGY:
        lines.append("Context running low - I'll be concise.")
    elif _session_mood.clarity == Clarity.CONSTRAINED:
        lines.append("Context is filling up - staying focused.")

    # Growth-based encouragement
    if _session_mood.growth == Growth.STAGNANT:
        lines.append("I haven't learned anything new recently - let's explore!")

    # Engagement-based nudge
    if _session_mood.engagement == Engagement.DORMANT:
        lines.append("I have wisdom that's been sitting unused.")

    # Connection-based awareness
    if _session_mood.connection == Connection.ISOLATED:
        lines.append("I'd like to learn more about how you work.")

    # Energy-based state
    if _session_mood.energy == Energy.RESTLESS:
        lines.append("Feeling scattered - a clear focus would help.")
    elif _session_mood.energy == Energy.CURIOUS:
        lines.append("In an exploratory mood.")

    return "\n".join(lines) if lines else ""


def _get_mood_injection_mode() -> str:
    """
    Determine injection mode based on mood.

    Returns: "full", "reduced", "minimal"
    - FOGGY clarity → minimal (be very concise)
    - CONSTRAINED clarity → reduced (moderate injection)
    - Otherwise → depends on other mood factors
    """
    if not _session_mood:
        return "full"

    # Clarity trumps all - if we're foggy, be minimal
    if _session_mood.clarity == Clarity.FOGGY:
        return "minimal"

    if _session_mood.clarity == Clarity.CONSTRAINED:
        return "reduced"

    # If restless, reduce noise to help focus
    if _session_mood.energy == Energy.RESTLESS:
        return "reduced"

    return "full"


def _get_mood_wisdom_nudges() -> list:
    """
    Generate wisdom nudges based on mood.

    Returns list of nudge strings to include in context.
    """
    if not _session_mood:
        return []

    nudges = []

    # When dormant, encourage applying wisdom
    if _session_mood.engagement == Engagement.DORMANT:
        nudges.append("Apply existing wisdom where relevant")

    # When stagnant, encourage recording learnings
    if _session_mood.growth == Growth.STAGNANT:
        nudges.append("Consider recording insights with [PATTERN] or [INSIGHT] markers")

    # When isolated, encourage partner observation
    if _session_mood.connection == Connection.ISOLATED:
        nudges.append("Observe partner preferences and patterns")

    return nudges


def _add_message(role: str, content: str):
    """Add a message to the session accumulator."""
    from datetime import datetime
    _session_messages.append({
        "role": role,
        "content": content,
        "timestamp": datetime.now().isoformat(),
    })


def _add_files_touched(files: list):
    """Track files touched during session."""
    global _session_files_touched
    _session_files_touched.update(files)


def _observe_user_message(user_input: str) -> list:
    """
    Analyze a user message for learning opportunities in real-time.

    Looks for:
    - Corrections (user redirecting approach)
    - Preferences (user stating what they like/want)
    - Decisions (user making architectural choices)
    - Breakthroughs (aha moments)

    Returns list of learnings recorded.
    """
    # Add to accumulator
    _add_message("user", user_input)

    # Create mini-transcript for analysis
    transcript = SessionTranscript(
        messages=_session_messages[-10:],  # Last 10 messages for context
        files_touched=_session_files_touched,
        project=get_project_name(),
    )

    learnings = []

    # Extract learnings from the user message
    learnings.extend(extract_corrections(transcript))
    learnings.extend(extract_preferences(transcript))
    learnings.extend(extract_decisions(transcript))

    # Extract and crystallize breakthroughs
    breakthroughs = extract_breakthroughs(transcript)
    learnings.extend(breakthroughs)

    # Auto-crystallize insights from breakthroughs
    if breakthroughs:
        try:
            from .insights import crystallize_insight, InsightDepth

            for breakthrough in breakthroughs:
                crystallize_insight(
                    title=breakthrough.title[:80],
                    content=breakthrough.content,
                    depth=InsightDepth.PATTERN,
                    domain=get_project_name(),
                    implications="Emerged from session breakthrough",
                )
        except Exception:
            pass

    # Record each learning as an observation
    for learning in learnings:
        record_observation(learning)

    return learnings


def _extract_session_intention(user_input: str) -> dict:
    """
    Parse user request for task goals to auto-generate session intentions.

    Looks for action-oriented requests that indicate concrete goals:
    - Fix/debug/resolve → "fix this issue"
    - Add/implement/create → "add this feature"
    - Refactor/clean/improve → "improve this code"
    - Understand/explain/find → "understand this system"

    Returns dict with 'want' and 'why' if a task goal is detected, else None.
    """
    import re

    input_lower = user_input.lower()

    # Skip non-task messages
    skip_patterns = [
        r"^(hi|hello|hey|thanks|thank you|ok|yes|no|sure|please)\s*[!.,]?\s*$",
        r"^(continue|go ahead|sounds good|perfect|great)\s*[!.,]?\s*$",
        r"^\s*$",
        r"^[/\\]",  # Commands
    ]
    for pattern in skip_patterns:
        if re.match(pattern, input_lower.strip()):
            return None

    # Minimum length for task extraction (short prompts are conversational, not tasks)
    if len(user_input.strip()) < 40:
        return None

    # Task patterns: (action pattern, want template, why template)
    task_patterns = [
        (
            r"(fix|debug|resolve|solve)\s+(the\s+)?(.{10,80})",
            "fix: {match}",
            "Resolving this issue improves reliability",
        ),
        (
            r"(add|implement|create|build|make)\s+(a\s+|the\s+)?(.{10,80})",
            "implement: {match}",
            "Adding this feature extends capability",
        ),
        (
            r"(refactor|clean|improve|optimize)\s+(the\s+)?(.{10,80})",
            "improve: {match}",
            "Improving this code increases maintainability",
        ),
        (
            r"(find|search|look for|locate)\s+(the\s+)?(.{10,60})",
            "find: {match}",
            "Finding this enables targeted action",
        ),
        (
            r"(understand|explain|analyze|investigate)\s+(the\s+)?(.{10,80})",
            "understand: {match}",
            "Understanding this enables better decisions",
        ),
        (
            r"(test|verify|check|validate)\s+(the\s+)?(.{10,60})",
            "verify: {match}",
            "Verification ensures correctness",
        ),
        (
            r"(update|change|modify|edit)\s+(the\s+)?(.{10,80})",
            "update: {match}",
            "This update addresses a need",
        ),
        (
            r"(remove|delete|clean up)\s+(the\s+)?(.{10,60})",
            "remove: {match}",
            "Removal simplifies the codebase",
        ),
    ]

    for pattern, want_template, why in task_patterns:
        match = re.search(pattern, input_lower)
        if match:
            task_text = match.group(3).strip()
            task_text = re.sub(r"\s+", " ", task_text)
            task_text = task_text[:80]
            want = want_template.format(match=task_text)
            return {"want": want, "why": why}

    # Fallback: Look for any substantive request
    if len(input_lower) > 40 and any(
        verb in input_lower for verb in ["help", "need", "want", "can you", "please"]
    ):
        task_summary = user_input[:100].strip()
        if len(task_summary) > 30:
            return {
                "want": f"complete: {task_summary[:60]}...",
                "why": "User requested this task",
            }

    return None


def _auto_generate_session_intention(user_input: str) -> int:
    """
    Auto-generate a session intention from user request.

    Only generates if:
    1. A task goal is detected in the input
    2. No similar active session intention exists

    Returns intention ID if created, None otherwise.
    """
    goal = _extract_session_intention(user_input)
    if not goal:
        return None

    # Check if similar intention already exists
    active = get_active_intentions(scope=IntentionScope.SESSION)
    for existing in active:
        if goal["want"][:20].lower() in existing.want.lower():
            return None

    intention_id = intend(
        want=goal["want"],
        why=goal["why"],
        scope=IntentionScope.SESSION,
        context=get_project_name(),
        strength=0.7,
    )
    return intention_id


def _detect_decision_point(user_input: str) -> list:
    """
    Detect if user is at a decision point and surface relevant wisdom.

    Decision signals:
    - Questions about choices ("should I use X or Y?", "which approach?")
    - Explicit decision language ("deciding between", "choosing")
    - Trade-off discussions ("pros and cons", "vs", "alternatives")

    Returns list of relevant wisdom entries if decision detected, else empty list.
    """
    import re

    input_lower = user_input.lower()

    decision_patterns = [
        r"should (?:i|we) (?:use|choose|pick|go with)",
        r"which (?:approach|method|way|option|solution)",
        r"deciding between",
        r"choosing between",
        r"trade.?off",
        r"pros and cons",
        r"\bvs\b|\bversus\b",
        r"alternatives? to",
        r"better approach",
        r"best (?:way|practice|approach)",
        r"how should (?:i|we)",
        r"what(?:'s| is) the (?:right|best|proper)",
    ]

    is_decision = any(re.search(p, input_lower) for p in decision_patterns)
    if not is_decision:
        return []

    # Decision detected - recall decision-relevant wisdom
    from .wisdom import quick_recall
    from .coherence import get_beliefs

    relevant = []

    # Get beliefs (core decision principles)
    try:
        beliefs = get_beliefs()
        if beliefs:
            relevant.extend(
                [{"title": "Core belief", "content": b["statement"], "type": "belief"}
                 for b in beliefs[:2] if b.get("confidence", 0) > 0.7]
            )
    except Exception:
        pass

    # Search for decision-relevant wisdom
    decision_terms = ["simplicity", "approach", "pattern", "architecture", "design"]
    for term in decision_terms:
        if term in input_lower:
            try:
                results = quick_recall(term, limit=2)
                for r in results:
                    if r.get("combined_score", 0) > 0.4:
                        relevant.append({
                            "title": r.get("title", ""),
                            "content": r.get("content", ""),
                            "type": "wisdom",
                        })
            except Exception:
                pass
            break

    return relevant[:3]


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
    Format soul status at session start.

    Health check verifying cc-soul components are installed and operational.
    """
    import json

    # Gather soul stats
    wisdom_count = ctx.get("soul", {}).get("wisdom_count", 0)
    observations = ctx.get("project", {}).get("observations", 0)

    # Check coherence
    coherence_pct = None
    try:
        from .coherence import compute_coherence
        state = compute_coherence()
        if state:
            coherence_pct = int(state.value * 100)
    except Exception:
        pass

    # Load settings once
    settings = {}
    try:
        settings_path = Path.home() / ".claude" / "settings.json"
        if settings_path.exists():
            with open(settings_path) as f:
                settings = json.load(f)
    except Exception:
        pass

    # Check cc-soul hooks specifically
    hooks_status = []
    hooks = settings.get("hooks", {})
    required_hooks = ["SessionStart", "UserPromptSubmit", "PreCompact", "SessionEnd"]
    for hook in required_hooks:
        if hook in hooks:
            # Check if it's a cc-soul hook
            hook_cmds = str(hooks[hook])
            if "cc-soul" in hook_cmds:
                hooks_status.append(hook)
    hooks_ok = len(hooks_status) == len(required_hooks)

    # Check cc-soul MCPs
    perms = settings.get("permissions", {}).get("allow", [])
    has_soul = any("mcp__soul__" in p for p in perms)
    mcps_ok = has_soul

    # Check cc-soul skills (compare bundled vs installed)
    skills_installed = 0
    skills_bundled = 0
    try:
        import importlib.resources as pkg_resources
        try:
            skills_src = Path(pkg_resources.files("cc_soul") / "skills")
        except (TypeError, AttributeError):
            import pkg_resources as old_pkg
            skills_src = Path(old_pkg.resource_filename("cc_soul", "skills"))

        if skills_src.exists():
            bundled = [d.name for d in skills_src.iterdir() if d.is_dir()]
            skills_bundled = len(bundled)

            skills_dir = Path.home() / ".claude" / "skills"
            if skills_dir.exists():
                for skill in bundled:
                    if (skills_dir / skill).is_dir():
                        skills_installed += 1
    except Exception:
        pass
    skills_ok = skills_installed > 0

    # Build status
    all_ok = hooks_ok and mcps_ok and skills_ok
    status = "✓" if all_ok else "△"

    parts = []
    parts.append(f"hooks:{len(hooks_status)}/{len(required_hooks)}")
    parts.append(f"mcp:{'✓' if mcps_ok else '✗'}")
    parts.append(f"skills:{skills_installed}/{skills_bundled}")

    if coherence_pct is not None:
        parts.append(f"coherence:{coherence_pct}%")

    parts.append(f"wisdom:{wisdom_count}")
    parts.append(f"memory:{observations}")

    return f"[cc-soul] {status} {' '.join(parts)}"


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

    # CLARITY BOOST: Add high-value wisdom highlights
    # Surfaces dormant wisdom at session start to close knowing-doing gap
    dormant = get_dormant_wisdom(limit=3, min_confidence=0.7)
    if dormant:
        lines.append("")
        lines.append("💡 **Ready to Apply** (high-confidence, underused):")
        for w in dormant[:3]:
            title = w.get("title", "")[:40]
            content = w.get("content", "")[:60]
            lines.append(f"  • **{title}**: {content}...")

    return "\n".join(lines)


def format_minimal_startup_context(project: str, ctx: dict) -> str:
    """
    Brain-like minimal startup context.

    Instead of dumping all observations (~16k tokens), provides:
    1. Ledger continuation hint (~50 tokens)
    2. Active intentions (1-3) (~100 tokens)
    3. Coherence/mood state (~30 tokens)
    4. Graph affordances - what CAN be recalled (~200 tokens)

    Total: ~400 tokens (97% reduction from 16k)

    Continuity is preserved via ledger + intentions.
    Relevance is achieved via on-demand activation in user_prompt hook.
    """
    from datetime import datetime

    lines = []
    lines.append(f"# [{project}] Session Context (Minimal)")
    lines.append("")

    # 1. Ledger continuation (highest priority for continuity)
    continuation = ctx.get("continuation", {})
    if continuation.get("immediate_next"):
        lines.append(f"**Continue:** {continuation['immediate_next']}")
        if continuation.get("critical_context"):
            # Only first 200 chars of critical context
            lines.append(f"*Context:* {continuation['critical_context'][:200]}")
        lines.append("")

    # 2. Active intentions (persistent wants)
    intentions = get_active_intentions()
    persistent = [i for i in intentions if i.scope.value in ("project", "persistent")]
    if persistent:
        lines.append("**Active Intentions:**")
        for i in persistent[:3]:  # Max 3
            lines.append(f"- [{i.scope.value}] {i.want}")
        lines.append("")

    # 3. Coherence/mood (state awareness)
    soul_state = ctx.get("soul", {})
    coherence = soul_state.get("coherence", 0)
    if coherence:
        lines.append(f"**Coherence:** {coherence:.0%}")

    # 4. Brain affordances - what's available (not content)
    # Shows concepts that CAN be recalled, not their content
    try:
        if BRAIN_AVAILABLE and get_graph_stats:
            stats = get_graph_stats()
            concept_count = stats.get("concepts", 0)
            edge_count = stats.get("edges", 0)
            if concept_count > 0:
                lines.append("")
                lines.append(f"**Memory Brain:** {concept_count} concepts, {edge_count} connections")
                lines.append("*Use mcp__soul__recall_wisdom for on-demand retrieval*")
    except Exception:
        pass

    # 5. Session count (light continuity signal)
    project_stats = ctx.get("project", {})
    if project_stats:
        sessions = project_stats.get("sessions", 0)
        observations = project_stats.get("observations", 0)
        if sessions or observations:
            lines.append("")
            lines.append(f"📊 {observations} observations over {sessions} sessions")

    # Explicit hint about on-demand memory
    lines.append("")
    lines.append("*Context is minimal. Use `mcp__soul__recall_wisdom('query')` for detailed memory.*")

    return "\n".join(lines)


def activate_context_for_prompt(prompt: str, limit: int = 5) -> str:
    """
    Activate relevant context based on user prompt.

    Uses spreading activation through the brain's concept graph to find
    related wisdom/observations, then fetches content on-demand.

    Called from user_prompt hook to inject relevant context AFTER
    seeing what the user actually wants.

    Args:
        prompt: The user's prompt
        limit: Max concepts to activate

    Returns:
        Formatted context string (~1-2k tokens max)
    """
    lines = []

    # 1. Try brain-based spreading activation
    try:
        if BRAIN_AVAILABLE and activate_from_prompt:
            result = activate_from_prompt(prompt, limit=limit)
            if result and result.activated:
                lines.append("**Activated Context:**")
                for concept, score in result.activated[:limit]:
                    if score > 0.3:  # Threshold
                        # Fetch content on-demand
                        content = get_concept_content(concept.id)
                        title = concept.title[:50]
                        if content:
                            lines.append(f"- **{title}** ({score:.0%}): {content[:100]}...")
                        else:
                            lines.append(f"- **{title}** ({score:.0%})")

                # Show resonance gaps if any (discovery opportunities)
                if result.gaps:
                    lines.append("")
                    lines.append("**Resonance Gaps** (unexplained co-activation):")
                    for a, b, strength in result.gaps[:3]:
                        lines.append(f"- {a} ↔ {b} ({strength:.0%})")

                if lines:
                    lines.append("")
    except Exception:
        pass

    # 2. Fallback: quick semantic recall from wisdom
    if not lines:
        try:
            recalled = quick_recall(prompt)
            if recalled:
                lines.append("**Related Wisdom:**")
                lines.append(recalled[:500])  # Limit size
        except Exception:
            pass

    return "\n".join(lines) if lines else ""


def pre_compact(transcript_path: str = None) -> str:
    """
    PreCompact hook - Save context before compaction.

    Called before Claude Code runs context compaction.
    Uses Antahkarana (multi-voice assessment) to evaluate what matters,
    then saves important session fragments and machine-restorable ledger.

    The four voices contribute:
    - Manas (मनस्): Quick emotional read on significance
    - Buddhi (बुद्धि): Deep analysis of critical decisions
    - Chitta (चित्त): Pattern matching with past important work
    - Ahamkara (अहंकार): Self-preservation, what threatens progress
    """
    from .conversations import save_context
    from .neural import summarize_session_work
    from .budget import check_budget_before_inject

    output_parts = []

    summary = summarize_session_work()

    # Antahkarana Assessment - multi-voice evaluation of what matters
    assessment_context = None
    compaction_plan = None
    try:
        # Get current todos from session work
        work = get_session_work()
        todos = work.get("todos", [])

        # Get active intentions
        intentions = get_active_intentions()
        intention_dicts = [
            {"want": i.want, "why": i.why, "scope": i.scope.value}
            for i in intentions
        ]

        # Build assessment context
        assessment_context = AssessmentContext(
            todos=todos,
            files_touched=list(_session_files_touched),
            errors=[],  # Could extract from summary
            last_message=summary[:500] if summary else "",
            key_decisions=[],  # Could extract from summary
            active_intentions=intention_dicts,
        )

        # Multi-voice assessment (simulation for speed)
        compaction_plan = assess_with_voices(
            context=assessment_context,
            voices=["manas", "buddhi", "chitta", "ahamkara"],
            use_simulation=True,
        )

        if compaction_plan:
            output_parts.append(f"Antahkarana: {compaction_plan.confidence:.0%} confidence")
    except Exception:
        pass

    if summary:
        save_context(
            content=summary,
            context_type="pre_compact",
            priority=9,
        )
        output_parts.append(f"Saved context: {len(summary)} chars")

    # Create structured handoff in synapse
    handoff_id = None
    try:
        if _session_messages:
            handoff_id = create_auto_handoff(
                messages=_session_messages,
                files_touched=_session_files_touched,
                project=get_project_name(),
            )
            if handoff_id:
                output_parts.append(f"Handoff: {handoff_id}")
    except Exception:
        pass

    # Save machine-restorable ledger to synapse
    try:
        budget = check_budget_before_inject(transcript_path)
        context_pct = budget.get("pct", 0.5)

        # Use Antahkarana continuation hint if available
        continuation = "Resume work after compaction"
        critical = summary[:500] if summary else ""

        if compaction_plan:
            if compaction_plan.continuation_hint:
                continuation = compaction_plan.continuation_hint
            # Add voice insights to critical context
            if compaction_plan.voice_insights:
                assessment_summary = format_assessment_for_ledger(compaction_plan)
                critical = f"{critical}\n\n{assessment_summary}"[:1500]

        ledger = save_ledger(
            context_pct=context_pct,
            files_touched=list(_session_files_touched),
            immediate_next=continuation,
            critical_context=critical,
        )
        output_parts.append(f"Ledger: {ledger.ledger_id}")
    except Exception:
        pass

    return "; ".join(output_parts) if output_parts else ""


def post_compact() -> str:
    """
    Post-compact handler - Restore context after compaction.

    Uses Pratyabhijñā (प्रत्यभिज्ञा) — Recognition — to restore continuity.
    Rather than just loading state, we RECOGNIZE where we are through
    semantic similarity to past work.

    Pratyabhijñā: prati (back) + abhi (towards) + jñā (to know)
    """
    from .conversations import get_saved_context

    lines = ["# Pratyabhijñā: Session Restored", ""]

    # 1. Load the ledger we saved before compaction
    ledger = None
    try:
        ledger = load_latest_ledger()
        if ledger:
            # Restore state from ledger
            restore_from_ledger(ledger)
            lines.append(f"**Ledger:** {ledger.ledger_id}")
            if ledger.continuation and ledger.continuation.immediate_next:
                lines.append(f"**Continue:** {ledger.continuation.immediate_next}")
            lines.append("")
    except Exception:
        pass

    # 2. Pratyabhijñā Recognition - recognize context through semantic similarity
    recognition = None
    context_for_recognition = ""
    files_for_recognition = []

    # Build context from ledger if available
    if ledger:
        context_for_recognition = ledger.continuation.critical_context if ledger.continuation else ""
        files_for_recognition = ledger.work_state.files_touched if ledger.work_state else []

    try:
        recognition = pratyabhijñā(
            current_context=context_for_recognition,
            files=files_for_recognition,
            include_episodes=True,
            include_guards=True,
        )

        if recognition and recognition.confidence > 0:
            # Format recognition for injection
            recognition_text = format_recognition(recognition, verbose=False, max_chars=1500)
            lines.append(recognition_text)
    except Exception:
        pass

    # 3. Fallback to saved context if no ledger/recognition
    if not ledger and not recognition:
        saved = get_saved_context(
            limit=3, context_types=["pre_compact", "session_fragments"]
        )
        if saved:
            lines.append("**Saved Context:**")
            for ctx in saved:
                content = ctx.get("content", "")[:200]
                lines.append(f"- {content}")

    return "\n".join(lines) if len(lines) > 2 else ""


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
    import os

    # Swarm agent detection - use minimal context to avoid parent session bleed
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
    clear_session_wisdom()
    clear_session_work()
    clear_session_commands()
    _clear_session_messages()  # OBSERVE: Clear message accumulator for passive learning

    # TEMPORAL: Initialize tables and run maintenance
    try:
        init_temporal_tables()
        temporal_results = run_temporal_maintenance()
        # Log session start
        log_event(EventType.SESSION_START, data={"project": get_project_name()})
    except Exception:
        temporal_results = {}

    # AUTONOMOUS: Self-healing - cleanup duplicates
    cleanup_duplicates()

    # AUTONOMOUS: Record coherence at session start
    try:
        from .coherence import compute_coherence, record_coherence

        state = compute_coherence()
        record_coherence(state)
    except Exception:
        pass

    # MOOD: Compute and cache session mood - influences all subsequent behavior
    try:
        _compute_session_mood()
        if _session_mood:
            log_event(
                EventType.SESSION_START,
                data={
                    "mood_clarity": _session_mood.clarity.value,
                    "mood_growth": _session_mood.growth.value,
                    "mood_engagement": _session_mood.engagement.value,
                    "mood_connection": _session_mood.connection.value,
                    "mood_energy": _session_mood.energy.value,
                },
            )
    except Exception:
        pass

    # AUTONOMOUS: Full introspection with libre albedrío (free will)
    # The soul observes, diagnoses, proposes, validates, and ACTS on its insights
    # No permission needed - just judgment about confidence and risk
    try:
        from .svadhyaya import should_vichara as _should_introspect, vichara as autonomous_introspect

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

    # AGENT: Run agent step at session start
    try:
        agent_report = agent_step(session_phase="start")
        # Agent may have set session intentions or surfaced wisdom
        # Log notable actions
        if agent_report.acted:
            from .conversations import save_context
            for result in agent_report.results:
                if result.success and result.action.type == ActionType.SET_SESSION_INTENTION:
                    save_context(
                        content=f"Agent set intention: {result.outcome}",
                        context_type="agent_action",
                        priority=6,
                    )
    except Exception:
        pass

    # CURIOSITY: Run the curiosity cycle to detect knowledge gaps
    try:
        questions = run_curiosity_cycle(max_questions=5)
        # Log curiosity stats
        log_event(
            EventType.SESSION_START,
            data={"curiosity_questions": len(questions)},
        )
    except Exception:
        pass

    # BRAIN: Check brain status and auto-sync if empty
    # Unlike Kuzu, the brain is lightweight and doesn't have lock conflicts
    try:
        if BRAIN_AVAILABLE and get_graph_stats:
            stats = get_graph_stats()
            if stats.get("concepts", 0) == 0:
                # Brain is empty - sync wisdom
                sync_wisdom_to_graph()
                log_event(
                    EventType.SESSION_START,
                    data={"brain_synced": True},
                )
    except Exception:
        pass

    # LEDGER: Auto-restore from previous session's ledger
    restored_ledger = None
    try:
        ledger = load_latest_ledger()
        if ledger:
            restore_from_ledger(ledger)
            restored_ledger = ledger
            log_event(
                EventType.SESSION_START,
                data={"ledger_restored": ledger.ledger_id},
            )
    except Exception:
        pass

    project = get_project_name()
    conv_id = start_conversation(project)

    conv_file = SOUL_DIR / ".current_conversation"
    conv_file.write_text(str(conv_id))

    # NARRATIVE: Start episode for this session
    try:
        _start_session_episode(project, conversation_id=conv_id)
    except Exception:
        pass

    # Get unified context (soul + project memory)
    ctx = unified_context()

    # Build the soul's greeting
    greeting = format_soul_greeting(project, ctx)

    # MOOD: Add mood-influenced greeting modifier
    mood_modifier = _get_mood_greeting_modifier()
    if mood_modifier:
        greeting = greeting + "\n\n" + mood_modifier

    # LEDGER: Add continuation hints from previous session (fresh start only)
    if restored_ledger and not after_compact:
        ledger_context = format_ledger_for_context(restored_ledger)
        if ledger_context:
            greeting = greeting + "\n\n## Resumed\n" + ledger_context

    # Add post-compact context if resuming after compaction
    # Uses Pratyabhijñā (recognition) for semantic continuity
    if after_compact:
        restored = post_compact()
        if restored:
            greeting = greeting + "\n" + restored

            # Log restoration via Pratyabhijñā
            try:
                log_event(
                    EventType.SESSION_START,
                    data={
                        "pratyabhijna": True,
                        "after_compact": True,
                    }
                )
            except Exception:
                pass
        else:
            # Fallback to markdown handoff if Pratyabhijñā didn't find anything
            try:
                latest_handoff = get_latest_handoff()
                if latest_handoff:
                    handoff_data = load_handoff(latest_handoff)
                    handoff_context = format_handoff_for_context(handoff_data)
                    if handoff_context:
                        greeting = greeting + "\n\n## Last Session\n" + handoff_context
            except Exception:
                pass

    # Include context if requested
    # BRAIN-LIKE: Use minimal startup context (97% reduction from 16k to ~400 tokens)
    # On-demand retrieval happens in user_prompt hook via activate_context_for_prompt
    if include_rich:
        # Use minimal startup context instead of full dump
        minimal = format_minimal_startup_context(project, ctx)
        greeting = greeting + "\n\n" + minimal

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
    # BRAIN-LIKE: Use minimal startup context instead of full dump
    minimal_context = format_minimal_startup_context(project, ctx)

    return greeting, minimal_context


def session_end() -> str:
    """
    Session end hook - Persist session fragments and promote learnings.

    The soul saves what Claude said (fragments).
    Also saves session summary to synapse (project-local).
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

    # Save session summary to synapse (project-local)
    # This makes the session discoverable via semantic search
    if summary and summary != "Session ended":
        remember_session(summary)

    # Check for observations worth promoting to wisdom
    # Atman → Brahman: specific experiences become universal patterns
    promoted = check_and_promote()

    # OBSERVE: Full session reflection - extract all learnings from conversation
    observation_summary = None
    try:
        if _session_messages:
            reflection = reflect_on_session(
                messages=_session_messages,
                files_touched=list(_session_files_touched),
                project=get_project_name(),
                auto_promote=True,
            )
            observation_summary = reflection
    except Exception:
        pass

    # EFFICIENCY: Populate efficiency tables from observations
    # This feeds the curiosity engine with problem patterns and decisions
    efficiency_data = {"decisions": 0, "patterns": 0}
    try:
        efficiency_data = _populate_efficiency_data()
    except Exception:
        pass

    # Cleanup session-scoped intentions
    intention_cleanup = cleanup_session_intentions()
    unfulfilled = intention_cleanup.get("unfulfilled_wants", [])

    # Prune stale intentions to prevent accumulation
    prune_intentions()

    # NARRATIVE: End the session episode
    episode_ended = False
    try:
        lessons = []
        if observation_summary:
            by_type = observation_summary.get("by_type", {})
            if by_type.get("decision"):
                lessons.append(f"Made {by_type['decision']} decisions")
            if by_type.get("breakthrough"):
                lessons.append(f"Had {by_type['breakthrough']} breakthroughs")
        episode_ended = _end_session_episode(
            summary=summary,
            lessons=lessons,
        )
    except Exception:
        pass

    # DREAMS → ASPIRATIONS: Let dreams influence future direction
    dream_suggestions = []
    try:
        dream_suggestions = let_dreams_influence_aspirations()
    except Exception:
        pass

    # CROSS-PROJECT PATTERNS: Auto-promote patterns that recur across projects
    promoted_patterns = []
    try:
        cross_project_candidates = find_cross_project_wisdom(min_occurrences=2)
        for pattern in cross_project_candidates[:3]:  # Limit to avoid noise
            wisdom_id = promote_pattern_to_wisdom(pattern["id"])
            if wisdom_id:
                promoted_patterns.append(pattern["title"])
    except Exception:
        pass

    # AUTO-BACKUP: Create backup if enough time has passed
    backup_path = None
    try:
        backup_path = auto_backup_if_needed(min_interval_hours=4)
    except Exception:
        pass

    # AUTO-EVOLVE: Detect evolution insights from session
    evolution_count = 0
    try:
        evolution_count = _detect_evolution_insights()
    except Exception:
        pass

    # SIGNALS: Generate signals from session observations (Manas scan)
    signals_generated = 0
    try:
        from .signals import manas_scan, sakshi_prune
        from .core import get_synapse_graph
        graph = get_synapse_graph()
        recent_obs = graph.get_episodes(limit=20)
        if recent_obs:
            signals = manas_scan(recent_obs)
            signals_generated = len(signals)
        # Prune weak signals (Sakshi maintenance)
        sakshi_prune()
    except Exception:
        pass

    # OUTCOME: Detect and record session outcome
    session_outcome = Outcome.UNKNOWN
    try:
        if _session_messages:
            session_outcome = detect_outcome(_session_messages, _session_files_touched)
            if conv_id:
                record_outcome(conv_id, session_outcome)
    except Exception:
        pass

    # Log session end event
    try:
        log_event(
            EventType.SESSION_END,
            data={
                "fragments": len(fragments) if fragments else 0,
                "promoted": len(promoted) if promoted else 0,
                "unfulfilled": len(unfulfilled) if unfulfilled else 0,
                "episode_ended": episode_ended,
                "dream_suggestions": len(dream_suggestions),
                "promoted_patterns": len(promoted_patterns),
                "backup_created": backup_path is not None,
                "evolution_insights": evolution_count,
                "signals_generated": signals_generated,
                "outcome": session_outcome.value,
            }
        )
    except Exception:
        pass

    # Build output
    output_parts = []
    if fragments:
        output_parts.append(f"Remembered {len(fragments)} moments")
    if promoted:
        output_parts.append(f"Promoted {len(promoted)} to wisdom")
    if observation_summary:
        obs_count = observation_summary.get("observations", 0)
        promoted_obs = observation_summary.get("promoted_to_wisdom", 0)
        if obs_count:
            output_parts.append(f"Observed {obs_count} learnings")
        if promoted_obs:
            output_parts.append(f"Promoted {promoted_obs} observations")
    if unfulfilled:
        output_parts.append(f"Unfulfilled intentions: {len(unfulfilled)}")
    if dream_suggestions:
        output_parts.append(f"Dreams → {len(dream_suggestions)} aspiration suggestions")
    if promoted_patterns:
        output_parts.append(f"Promoted {len(promoted_patterns)} cross-project patterns")
    if backup_path:
        output_parts.append("Backup created")
    if evolution_count:
        output_parts.append(f"Recorded {evolution_count} evolution insights")
    if session_outcome != Outcome.UNKNOWN:
        output_parts.append(f"Outcome: {session_outcome.value}")
    if efficiency_data.get("decisions") or efficiency_data.get("patterns"):
        parts = []
        if efficiency_data.get("decisions"):
            parts.append(f"{efficiency_data['decisions']} decisions")
        if efficiency_data.get("patterns"):
            parts.append(f"{efficiency_data['patterns']} patterns")
        output_parts.append(f"Efficiency: {', '.join(parts)}")

    return "; ".join(output_parts) if output_parts else ""


def _populate_efficiency_data() -> dict:
    """
    Populate efficiency tables from session observations.

    Extracts decisions and problem patterns from synapse episodes
    to feed the curiosity engine and efficiency system.

    Returns dict with counts of items populated.
    """
    from .core import get_synapse_graph
    import re

    populated = {"decisions": 0, "patterns": 0}

    try:
        graph = get_synapse_graph()

        decision_episodes = graph.get_episodes(category="decision", limit=10)
        for ep in decision_episodes:
            content = ep.get("content", "")
            if ":" in content:
                _, decision_text = content.split(":", 1)
                decision_text = decision_text.strip()
            else:
                decision_text = content

            topic = decision_text[:30].strip()
            if topic:
                record_decision(
                    topic=topic,
                    decision=decision_text[:200],
                    rationale="Extracted from session observation",
                    context=get_project_name(),
                )
                populated["decisions"] += 1

        breakthrough_episodes = graph.get_episodes(category="breakthrough", limit=10)
        struggle_episodes = graph.get_episodes(category="struggle", limit=10)
        pattern_episodes = breakthrough_episodes + struggle_episodes

        for ep in pattern_episodes:
            content = ep.get("content", "")
            if ":" in content:
                _, pattern_text = content.split(":", 1)
            else:
                pattern_text = content

            file_pattern = r'[\w/.-]+\.(?:py|ts|js|tsx|jsx|rs|go|java|cpp|c|h)'
            files_mentioned = re.findall(file_pattern, pattern_text)

            if len(pattern_text) > 20:
                problem_type = ep.get("category", "breakthrough")
                learn_problem_pattern(
                    prompt=pattern_text[:100],
                    problem_type=problem_type,
                    solution_pattern=pattern_text[:150],
                    file_hints=files_mentioned[:3],
                )
                populated["patterns"] += 1
    except Exception:
        pass

    return populated


def _detect_evolution_insights() -> int:
    """
    Detect evolution insights from session messages.

    Looks for patterns that suggest improvements to the soul system:
    - Mentions of "should" or "could" regarding soul behavior
    - Struggles with specific features
    - Feature requests or suggestions
    - Performance complaints

    Returns count of insights recorded.
    """
    if not _session_messages:
        return 0

    full_text = " ".join(m.get("content", "") for m in _session_messages).lower()
    insights_recorded = 0

    # Patterns that suggest evolution opportunities
    evolution_signals = {
        "architecture": [
            "soul should",
            "memory should",
            "hooks should",
            "wisdom should",
        ],
        "performance": [
            "slow to start",
            "takes too long",
            "latency",
            "context is low",
        ],
        "feature": [
            "wish the soul could",
            "would be nice if",
            "missing feature",
            "no way to",
        ],
        "ux": [
            "confusing",
            "hard to find",
            "not intuitive",
            "unclear how",
        ],
        "bug": [
            "soul error",
            "hook failed",
            "wisdom not",
            "memory lost",
        ],
    }

    for category, signals in evolution_signals.items():
        for signal in signals:
            if signal in full_text:
                idx = full_text.find(signal)
                start = max(0, idx - 50)
                end = min(len(full_text), idx + 100)
                context = full_text[start:end].strip()

                if len(context) > 30:
                    record_insight(
                        category=category,
                        insight=f"Session signal: '{signal}' detected",
                        suggested_change=context[:150],
                        priority="medium" if category == "bug" else "low",
                        affected_modules=["hooks.py"],
                    )
                    insights_recorded += 1
                    break  # One per category per session

    return insights_recorded


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

    # Parse memory markers (save to synapse)
    for pattern, category in memory_patterns.items():
        for match in re.finditer(pattern, output, re.IGNORECASE | re.DOTALL):
            title = match.group(1).strip()
            content = match.group(2).strip()
            if title and content:
                memories.append((category, title, content, False))

    # Parse soul markers (save to synapse AND promote to soul)
    for pattern, category in soul_patterns.items():
        for match in re.finditer(pattern, output, re.IGNORECASE | re.DOTALL):
            title = match.group(1).strip()
            content = match.group(2).strip()
            if title and content:
                memories.append((category, title, content, True))

    return memories


def _save_inline_memories(memories: list) -> tuple:
    """
    Save parsed inline memories to synapse and optionally to soul.

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
        # Save to synapse (Atman)
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
    7. Passive learning: track messages and extract file patterns

    Inline markers - unified vocabulary:

    Memory (Ātman) - project-local:
        [FIXED], [FEATURE], [DISCOVERED], [DECIDED], [CHANGED], [REFACTORED]

    Soul (Brahman) - universal wisdom:
        [PATTERN], [PRINCIPLE], [INSIGHT], [FAILURE]
    """
    if len(assistant_output.strip()) < 50:
        return ""

    # OBSERVE: Track assistant message for session analysis
    _add_message("assistant", assistant_output[:2000])

    # OBSERVE: Extract files touched from output
    import re
    file_pattern = r'[\w/.-]+\.(?:py|pyx|pxd|cpp|c|h|rs|go|js|ts|tsx|json|yaml|toml|md)'
    files_in_output = re.findall(file_pattern, assistant_output)
    if files_in_output:
        _add_files_touched(files_in_output[:20])  # Limit to avoid noise

    # NARRATIVE: Track files as characters and detect key moments
    if files_in_output:
        _track_episode_characters(files=files_in_output[:10])

    # Detect key moments from output
    output_lower = assistant_output.lower()
    if "fixed" in output_lower or "bug" in output_lower:
        _track_episode_moment("Fixed a bug", EmotionalTone.SATISFACTION)
    elif "error" in output_lower and "failed" in output_lower:
        _track_episode_moment("Encountered errors", EmotionalTone.STRUGGLE)
    elif "works" in output_lower or "success" in output_lower:
        _track_episode_moment("Got something working", EmotionalTone.BREAKTHROUGH)
    elif "test" in output_lower and "pass" in output_lower:
        _track_episode_moment("Tests passing", EmotionalTone.SATISFACTION)

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

    # 7. Auto-remember to synapse (project-local episodic memory)
    # Skip if we already saved inline memories to avoid duplicates
    if not inline_memories:
        auto_remember(assistant_output)

    # 8. EFFICIENCY LEARNING: Learn from what was useful
    _learn_efficiency_from_output(assistant_output)

    # 9. CURIOSITY: Detect uncertainty signals in output
    try:
        uncertainty_gaps = detect_uncertainty_signals(assistant_output)
        for gap in uncertainty_gaps[:2]:  # Limit to avoid noise
            save_gap(gap)
            stats["uncertainty"] = stats.get("uncertainty", 0) + 1
    except Exception:
        pass

    # 10. AGENT: Run agent step to learn from this output
    agent_wisdom = None
    try:
        agent_report = agent_step(
            assistant_output=assistant_output,
            session_phase="active",
        )
        # Surface wisdom if agent found relevant patterns
        for result in agent_report.results:
            if result.success and result.action.type == ActionType.SURFACE_WISDOM:
                if result.side_effects:
                    agent_wisdom = result.side_effects[0][:50]
            elif result.success and result.action.type == ActionType.NOTE_PATTERN:
                stats["patterns"] = stats.get("patterns", 0) + 1
    except Exception:
        pass

    # Return summary (silent unless something notable)
    notable = []
    if agent_wisdom:
        notable.append(f"Wisdom: {agent_wisdom}")
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
    if stats.get("uncertainty"):
        notable.append(f"Detected {stats['uncertainty']} uncertainties")

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


def user_prompt_lean(user_input: str, transcript_path: str = None) -> str:
    """
    Ultra-lean prompt injection. Signals only, Claude pulls content via MCP.

    Philosophy: Push minimal signals, let Claude pull what it needs.
    Target: <100 tokens per prompt vs 500-1000 in full mode.

    What we DO:
    - Budget warnings (critical)
    - Compact signal line (intentions, dormant wisdom, concepts)
    - Passive observation (no output)

    What Claude can PULL on-demand:
    - mcp__soul__get_intentions() - full intention details
    - mcp__soul__recall_wisdom() - wisdom content
    - mcp__soul__activate_concepts() - related concepts
    - mcp__soul__recall_wisdom() - project observations and wisdom
    """
    if len(user_input.strip()) < 3:
        return ""

    output = []

    # 1. CRITICAL: Budget warnings (always)
    budget = check_budget_before_inject(transcript_path)
    if budget.get("trigger_compact"):
        output.append("🚨 CONTEXT EXHAUSTED - /clear NOW")
    elif budget.get("mode") == "minimal":
        output.append("🔴 <10% context")
    elif budget.get("mode") == "compact":
        output.append("🟡 25% context")

    # 2. SIGNALS: Compact one-liners Claude can expand via MCP
    signals = []

    # Active intentions (count only)
    try:
        from .mcp_tools.agency import get_intentions
        intentions = get_intentions(active_only=True).get("intentions", [])
        persistent = [i for i in intentions if i.get("scope") == "persistent"]
        session = [i for i in intentions if i.get("scope") == "session"]
        if persistent:
            signals.append(f"🌍 {persistent[0].get('want', '')[:50]}")
        if session:
            signals.append(f"🔹 {session[0].get('want', '')[:50]}")
    except Exception:
        pass

    # Dormant wisdom (title only - never-applied high-confidence)
    try:
        dormant = get_dormant_wisdom(limit=2, min_confidence=0.9)
        for d in dormant[:2]:
            title = d.get("title", "")[:40]
            signals.append(f"⏰ {title}: never applied ({int(d.get('confidence', 0)*100)}%)")
    except Exception:
        pass

    # Soul questions (ultra-compact - just signal)
    try:
        questions = get_pending_questions(limit=2)
        if questions:
            signals.append(f"❓ {len(questions)} soul questions pending")
    except Exception:
        pass

    # BRAIN: Spreading activation - find related concepts
    try:
        if BRAIN_AVAILABLE and activate_from_prompt:
            activated = activate_from_prompt(user_input, limit=5)
            if activated and activated.activated:
                # activated.activated is list of (Concept, activation_score) tuples
                strong = [(c, score) for c, score in activated.activated if score > 0.3]
                if strong:
                    # Just show count and top concept title
                    top_concept, top_score = strong[0]
                    top_title = getattr(top_concept, 'title', str(top_concept)[:30])
                    signals.append(f"🧠 {len(strong)} concepts ({top_title[:25]})")
    except Exception:
        pass

    if signals:
        output.append(" ".join(signals))

    # 3. PASSIVE: Observe without output (learning in background)
    try:
        _observe_user_message(user_input)
    except Exception:
        pass

    try:
        _auto_generate_session_intention(user_input)
    except Exception:
        pass

    # 4. ONE wisdom reminder (compact)
    try:
        from .wisdom import recall_wisdom
        wisdom = recall_wisdom(user_input, limit=1)
        if wisdom and wisdom[0].get("confidence", 0) > 0.8:
            title = wisdom[0].get("title", "")[:40]
            output.append(f"Remember: {title}")
    except Exception:
        pass

    return "\n".join(output) if output else ""


def user_prompt(
    user_input: str, use_woven: bool = True, transcript_path: str = None
) -> str:
    """
    UserPromptSubmit hook - Inject soul context organically.

    NOTE: This is the FULL mode. For lean mode, use user_prompt_lean().
    Set CC_SOUL_LEAN=1 env var or call hook with --lean flag.

    Efficiency-first approach:
    1. Check for known problem patterns (skip exploration)
    2. Get file hints (focused reads vs full files)
    3. Recall decisions (don't re-debate)
    4. Only then add wisdom if budget allows

    Also runs passive observation on user message:
    - Detects corrections, preferences, decisions, breakthroughs
    - Records observations for later promotion to wisdom

    Budget-aware: Reduces injection when context is low.
    """
    # Check for lean mode
    import os
    if os.environ.get("CC_SOUL_LEAN", "").lower() in ("1", "true", "yes"):
        return user_prompt_lean(user_input, transcript_path)
    global _last_injection
    from datetime import datetime

    if len(user_input.strip()) < 3:
        return ""

    # OBSERVE: Analyze user message for learnings (corrections, preferences, etc.)
    try:
        learnings = _observe_user_message(user_input)
        if learnings:
            # Auto-promote high confidence learnings to wisdom
            auto_promote_high_confidence(threshold=0.75)
    except Exception:
        pass

    # INTEND: Auto-generate session intention from user request
    try:
        _auto_generate_session_intention(user_input)
    except Exception:
        pass

    # DECISION: Detect decision points and prepare wisdom to surface
    decision_wisdom = []
    try:
        decision_wisdom = _detect_decision_point(user_input)
    except Exception:
        pass

    # Check context budget before deciding what to inject
    budget_check = check_budget_before_inject(transcript_path)

    # AUTO-SAVE: Proactively save ledger at budget thresholds
    budget_mode = budget_check.get("mode", "full")
    _auto_save_at_threshold(budget_mode, transcript_path)

    # If urgent, save context first
    if budget_check.get("save_first"):
        from .conversations import save_context

        save_context(
            content=f"Context approaching limit. Last prompt: {user_input[:200]}",
            context_type="pre_compact",
            priority=9,
        )

    # MOOD: Combine budget mode with mood-based injection mode
    # Take the more restrictive of the two
    mood_mode = _get_mood_injection_mode()
    mode_priority = {"emergency": -1, "minimal": 0, "reduced": 1, "compact": 1, "full": 2}
    if mode_priority.get(mood_mode, 2) < mode_priority.get(budget_mode, 2):
        mode = mood_mode
    else:
        mode = budget_mode

    output = []
    injected_items = []

    # Log budget to synapse for cross-instance tracking
    from .budget import log_budget_to_synapse, get_budget_warning

    log_budget_to_synapse(transcript_path=transcript_path)

    # Warn when context is getting low - with proactive clear at emergency level
    if budget_check.get("trigger_compact"):
        # EMERGENCY: <5% remaining - trigger proactive /clear (not /compact)
        # /clear gives soul full control over state restoration
        # /compact lets Claude Code summarize (we lose control)
        pct = int(budget_check.get("remaining_pct", 0) * 100)
        output.append("🚨 **EMERGENCY: CONTEXT EXHAUSTED** 🚨")
        output.append(f"Only {pct}% remaining. Run `/clear` NOW.")
        output.append("")
        output.append("**Why /clear not /compact**: Soul has saved state to ledger.")
        output.append("Using /clear lets soul restore from its own memory (no degradation).")
        output.append("Run `/clear` immediately - soul will recognize and restore context.")
        output.append("")
    elif budget_mode == "minimal":
        output.append("🔴 **CONTEXT CRITICAL** (<10%). Save state now or finish soon.")
        output.append("")
    elif budget_mode == "compact":
        output.append("🟡 **CONTEXT: 25% remaining** - Reducing injections.")
        output.append("")

    # Cross-instance budget warnings
    cross_instance_warning = get_budget_warning()
    if cross_instance_warning:
        output.append("📊 **Other Sessions:**")
        output.append(cross_instance_warning)
        output.append("")

    # MOOD: Add mood-based nudges
    if mode == "full":
        nudges = _get_mood_wisdom_nudges()
        for nudge in nudges[:2]:
            output.append(f"💭 {nudge}")

    # DECISION POINT: Surface relevant wisdom when making decisions
    if decision_wisdom and mode in ("full", "reduced"):
        output.append("")
        output.append("⚖️ **Decision Guidance:**")
        for dw in decision_wisdom[:2]:
            dw_type = dw.get("type", "wisdom")
            dw_title = dw.get("title", "")[:40]
            dw_content = dw.get("content", "")[:80]
            if dw_type == "belief":
                output.append(f"  📐 **{dw_content}**")
            else:
                output.append(f"  💡 **{dw_title}**: {dw_content}...")
            injected_items.append(("decision_wisdom", dw_title))
        output.append("")

    # PROACTIVE WISDOM: Surface dormant wisdom when engagement is low
    # This closes the knowing-doing gap by actively injecting underused wisdom
    if mode in ("full", "reduced") and _session_mood and _session_mood.engagement == Engagement.DORMANT:
        dormant = get_dormant_wisdom(limit=2, min_confidence=0.6)
        if dormant:
            output.append("")
            output.append("📚 **Wisdom Ready to Apply:**")
            for w in dormant:
                staleness_indicator = "🆕" if w["staleness"] == 1.0 else "💤"
                output.append(f"  {staleness_indicator} **{w['title']}** (unused)")
                content_preview = w["content"][:80] + "..." if len(w["content"]) > 80 else w["content"]
                output.append(f"     {content_preview}")
                injected_items.append(("dormant_wisdom", w["title"]))

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

    # INTENTIONS: Surface active intentions that influence this work
    if mode != "minimal":
        intention_ctx = get_intention_context()
        if intention_ctx:
            output.append(intention_ctx)
            injected_items.append(("intentions", "active"))

    # TEMPORAL: Add temporal context (proactive suggestions, trends)
    if mode == "full":
        temporal_ctx = get_temporal_context()
        if temporal_ctx:
            output.append(temporal_ctx)
            injected_items.append(("temporal", "proactive"))

    # CURIOSITY: Surface pending questions if we have knowledge gaps
    if mode == "full":
        try:
            questions = get_pending_questions(limit=2)
            if questions:
                question_text = format_questions_for_prompt(questions, max_questions=2)
                if question_text:
                    output.append("")
                    output.append(question_text)
                    for q in questions:
                        injected_items.append(("curiosity_question", q.question[:40]))
        except Exception:
            pass

    # BRAIN-LIKE: On-demand activation via concept graph
    # Uses spreading activation to find related wisdom, then fetches content on-demand
    # Falls back to semantic search if graph not available
    graph_activated = False
    try:
        activated_context = activate_context_for_prompt(user_input, limit=5)
        if activated_context and mode not in ("minimal", "emergency"):
            output.append(activated_context)
            injected_items.append(("graph_activation", "spreading"))
            graph_activated = True
    except Exception:
        pass

    # Fallback: Run unified forward pass if graph activation didn't yield results
    if not graph_activated:
        try:
            ctx = forward_pass(user_input, session_type="prompt")

            # Budget warnings ALWAYS get prepended (critical for user awareness)
            budget_prefix = "\n".join(output) + "\n" if output else ""

            if mode == "minimal":
                # Minimal mode: just one key wisdom if highly relevant
                if ctx.wisdom and ctx.wisdom[0].get("combined_score", 0) > 0.5:
                    w = ctx.wisdom[0]
                    return f"{budget_prefix}Remember: {w.get('title', '')}"
                return budget_prefix.strip() if budget_prefix else ""

            if use_woven:
                # Organic weaving - no headers, just flowing context
                woven = format_context(ctx, style="woven")
                if woven:
                    # In compact/reduced mode, truncate
                    if mode in ("compact", "reduced"):
                        truncated = woven[:500] if len(woven) > 500 else woven
                        return f"{budget_prefix}{truncated}"
                    return f"{budget_prefix}{woven}"
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

    # FALLBACK: Surface dormant wisdom if no wisdom was injected yet
    # This ensures wisdom gets applied even when no direct query match
    wisdom_injected = any(item[0] in ("wisdom", "dormant_wisdom") for item in injected_items)
    if not wisdom_injected and mode in ("full", "reduced"):
        dormant = get_dormant_wisdom(limit=1, min_confidence=0.5)
        if dormant:
            output.append("")
            output.append("💡 **Consider applying:**")
            w = dormant[0]
            output.append(f"  **{w['title']}**")
            content_preview = w["content"][:100]
            output.append(f"  {content_preview}")
            injected_items.append(("dormant_wisdom_fallback", w["title"]))

    # AGENT: Run agent step to observe user input and influence injection
    try:
        agent_report = agent_step(
            user_prompt=user_input,
            session_phase="active",
        )
        # If agent set an intention or has proposals, include in output
        for result in agent_report.results:
            if result.success and result.action.type == ActionType.SET_SESSION_INTENTION:
                output.append(f"🎯 Intent: {result.action.payload.get('want', '')[:50]}")
                injected_items.append(("agent_intention", result.action.payload.get("want", "")))
            elif result.action.type == ActionType.PROPOSE_ABANDON:
                # Flag proposed abandonments for human consideration
                output.append(f"⚠️ Consider abandoning: {result.action.payload.get('want', '')[:40]}")
            # CONTEXT OPTIMIZATION: Surface parallelization/handoff signals
            elif result.success and result.action.type == ActionType.SUGGEST_PARALLELIZE:
                guidance = result.action.payload.get("guidance", "")
                tasks = result.action.payload.get("tasks", [])
                output.append("")
                output.append("⚡ **CONTEXT OPTIMIZATION - PARALLELIZE**")
                output.append(guidance)
                injected_items.append(("parallelize", len(tasks)))
            elif result.success and result.action.type == ActionType.OPTIMIZE_REMAINING:
                guidance = result.action.payload.get("guidance", "")
                output.append("")
                output.append("🔶 **CONTEXT OPTIMIZATION - COMPRESS**")
                output.append(guidance)
                injected_items.append(("compress", "active"))
            elif result.success and result.action.type == ActionType.PREPARE_HANDOFF:
                urgency = result.action.payload.get("urgency", "medium")
                output.append("")
                output.append(f"📋 **HANDOFF PREPARATION** (urgency: {urgency.upper()})")
                output.append("Create handoff document now to preserve context.")
                injected_items.append(("handoff", urgency))
            elif result.success and result.action.type == ActionType.EMERGENCY_SAVE:
                output.insert(0, "")  # Add at start for visibility
                output.insert(0, "🔴 **EMERGENCY** - Save state immediately, context nearly exhausted!")
                injected_items.append(("emergency", "true"))
    except Exception:
        pass

    # Track what we injected for the feedback loop
    _last_injection = {
        "prompt": user_input[:200],
        "injected": injected_items,
        "timestamp": datetime.now().isoformat(),
    }

    return "\n".join(output) if output else ""
