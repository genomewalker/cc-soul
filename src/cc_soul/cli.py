"""
Command-line interface for cc-soul.
"""

import sys
import shutil
import argparse
from pathlib import Path

from .core import init_soul, summarize_soul, get_soul_context
from .wisdom import (
    recall_wisdom,
    get_pending_applications,
    get_session_wisdom,
    gain_wisdom,
    WisdomType,
)
from .beliefs import get_beliefs, hold_belief
from .vocabulary import learn_term
from .identity import observe_identity, IdentityAspect
from .hooks import session_start, session_end, user_prompt, assistant_stop
from .conversations import (
    save_context,
    get_saved_context,
    get_recent_context,
    format_context_restoration,
)
from .vectors import reindex_all_wisdom
from .evolve import (
    get_evolution_insights,
    get_evolution_summary,
    seed_evolution_insights,
)
from .seed import seed_soul
from .mood import compute_mood, format_mood_display, get_mood_reflection
from .bridge import (
    is_memory_available,
    get_project_memory,
    promote_observation,
    unified_context,
    format_unified_context,
    get_project_signals,
    detect_wisdom_candidates,
)
from .introspect import (
    generate_introspection_report,
    format_introspection_report,
    analyze_pain_points,
    get_wisdom_timeline,
    get_wisdom_health,
    format_wisdom_stats,
    get_session_comparison,
    get_growth_trajectory,
    get_learning_patterns,
    format_trends_report,
    get_decay_visualization,
    format_decay_chart,
)
from .improve import (
    diagnose,
    suggest_improvements,
    get_proposals,
    get_improvement_stats,
)
from .ultrathink import (
    enter_ultrathink,
    exit_ultrathink,
    format_ultrathink_context,
    record_discovery,
    commit_session_learnings,
)
from .efficiency import (
    learn_problem_pattern,
    add_file_hint,
    record_decision,
    recall_decisions,
    get_compact_context,
    format_efficiency_injection,
    get_token_stats,
)
from .observe import (
    get_pending_observations,
    promote_observation_to_wisdom,
    auto_promote_high_confidence,
)
from .backup import (
    dump_soul,
    restore_soul,
    create_timestamped_backup,
    list_backups,
    format_backup_list,
)

# Graph is optional (requires kuzu)
try:
    from .graph import (
        KUZU_AVAILABLE,
        get_graph_stats,
        sync_wisdom_to_graph,
        search_concepts,
        activate_from_prompt,
        format_activation_result,
        get_neighbors,
        link_concepts,
        RelationType,
    )
except ImportError:
    KUZU_AVAILABLE = False

from .curiosity import (
    detect_all_gaps,
    run_curiosity_cycle,
    get_pending_questions,
    get_curiosity_stats,
    answer_question,
    dismiss_question,
    incorporate_answer_as_wisdom,
    format_questions_for_prompt,
)

from .narrative import (
    get_episode,
    recall_breakthroughs,
    recall_struggles,
    recall_by_type,
    recall_by_character,
    get_recurring_characters,
    get_emotional_journey,
    format_episode_story,
    get_narrative_stats,
    EpisodeType,
)

from .neural import (
    create_trigger,
    find_triggers,
    activate_with_bridges,
    create_bridge,
    get_trigger_stats,
    sync_wisdom_to_triggers,
    format_neural_context,
    save_growth_vector,
    get_growth_vectors,
    auto_learn_from_output,
    create_resonance,
    find_resonance,
    get_resonance_stats,
    get_emotional_contexts,
)


def cmd_summary(args):
    """Show soul summary."""
    init_soul()
    print(summarize_soul())


def cmd_seed(args):
    """Seed the soul with foundational beliefs and wisdom."""
    result = seed_soul(force=args.force)

    if result["status"] == "already_seeded":
        print("Soul already seeded. Use --force to reseed.")
        return

    print("=" * 50)
    print("SOUL SEEDED")
    print("=" * 50)
    print()
    print(result["message"])
    print()
    print("Your soul now has foundational:")
    print(f"  - {result['details']['beliefs']} core beliefs")
    print(f"  - {result['details']['wisdom']} wisdom entries")
    print(f"  - {result['details']['vocabulary']} vocabulary terms")
    print()
    print("Run 'cc-soul summary' to see your soul.")
    print("Run 'cc-soul install-skills' to install skill definitions.")
    print("Run 'cc-soul install-hooks' to enable automatic soul injection.")


def cmd_health(args):
    """Check system health - not just 'is it running' but 'is it alive'."""
    import json
    from datetime import datetime, timedelta

    # Status levels: OK, WARN, FAIL
    results = {"infrastructure": [], "content": [], "activity": [], "vitality": []}

    def check(category, name, fn):
        try:
            status, detail = fn()
            results[category].append((name, status, detail))
        except Exception as e:
            results[category].append((name, "FAIL", str(e)))

    # ══════════════════════════════════════════════════════════════
    # INFRASTRUCTURE - Can the soul run?
    # ══════════════════════════════════════════════════════════════

    def check_database():
        from .core import SOUL_DIR, get_db_connection

        db_path = SOUL_DIR / "soul.db"
        if not db_path.exists():
            return "FAIL", "Database not found"
        conn = get_db_connection()
        cursor = conn.execute("SELECT COUNT(*) FROM wisdom")
        count = cursor.fetchone()[0]
        return "OK", f"{db_path.name} ({count} wisdom)"

    def check_hooks():
        settings_path = Path.home() / ".claude" / "settings.json"
        if not settings_path.exists():
            return "FAIL", "settings.json not found"
        with open(settings_path) as f:
            settings = json.load(f)
        hooks = settings.get("hooks", {})
        required = [
            "SessionStart",
            "SessionEnd",
            "UserPromptSubmit",
            "Stop",
            "PreCompact",
        ]
        installed = [h for h in required if h in hooks]
        if len(installed) == 0:
            return "FAIL", "No hooks installed"
        if len(installed) < len(required):
            missing = set(required) - set(installed)
            return "WARN", f"Missing: {', '.join(missing)}"
        return "OK", f"{len(installed)}/{len(required)} hooks"

    def check_embeddings():
        from .vectors import embed_text

        vec = embed_text("test")
        return "OK", f"dim={len(vec)}"

    def check_lancedb():
        import lancedb
        from .core import SOUL_DIR

        lance_dir = SOUL_DIR / "vectors" / "lancedb"
        lance_dir.mkdir(parents=True, exist_ok=True)
        db = lancedb.connect(str(lance_dir))
        tables = db.table_names()
        return "OK", f"{len(tables)} tables"

    def check_kuzu():
        try:
            import kuzu  # noqa: F401

            return "OK", "available"
        except ImportError:
            return "WARN", "not installed (optional)"

    # ══════════════════════════════════════════════════════════════
    # CONTENT - Does the soul have memories?
    # ══════════════════════════════════════════════════════════════

    def check_wisdom_content():
        from .core import get_db_connection

        conn = get_db_connection()
        cursor = conn.execute("SELECT COUNT(*) FROM wisdom")
        count = cursor.fetchone()[0]
        if count == 0:
            return "WARN", "Empty - no wisdom yet"
        return "OK", f"{count} entries"

    def check_beliefs():
        beliefs = get_beliefs()
        if not beliefs:
            return "WARN", "No beliefs defined"
        return "OK", f"{len(beliefs)} beliefs"

    def check_triggers():
        from .neural import get_trigger_stats

        stats = get_trigger_stats()
        count = stats.get("total_triggers", 0)
        if count == 0:
            return "WARN", "No neural triggers"
        return "OK", f"{count} triggers"

    # ══════════════════════════════════════════════════════════════
    # ACTIVITY - Is the soul being used?
    # ══════════════════════════════════════════════════════════════

    def check_recent_sessions():
        from .core import get_db_connection

        conn = get_db_connection()
        # Check if wisdom_applications table exists and has data
        cursor = conn.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='wisdom_applications'"
        )
        if not cursor.fetchone():
            return "WARN", "Table not created yet"
        week_ago = (datetime.now() - timedelta(days=7)).isoformat()
        cursor = conn.execute(
            "SELECT COUNT(*) FROM wisdom_applications WHERE applied_at > ?", (week_ago,)
        )
        count = cursor.fetchone()[0]
        if count == 0:
            return "WARN", "No activity in 7 days"
        return "OK", f"{count} applications (7d)"

    def check_wisdom_applications():
        from .core import get_db_connection

        conn = get_db_connection()
        cursor = conn.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='wisdom_applications'"
        )
        if not cursor.fetchone():
            return "WARN", "Table not created yet"
        cursor = conn.execute("SELECT COUNT(*) FROM wisdom_applications")
        count = cursor.fetchone()[0]
        if count == 0:
            return "WARN", "Wisdom never applied"
        return "OK", f"{count} applications"

    # ══════════════════════════════════════════════════════════════
    # VITALITY - Is the soul growing?
    # ══════════════════════════════════════════════════════════════

    def check_recent_learning():
        from .core import get_db_connection

        conn = get_db_connection()
        week_ago = (datetime.now() - timedelta(days=7)).isoformat()
        cursor = conn.execute(
            "SELECT COUNT(*) FROM wisdom WHERE timestamp > ?", (week_ago,)
        )
        count = cursor.fetchone()[0]
        if count == 0:
            return "WARN", "No new wisdom in 7 days"
        return "OK", f"+{count} wisdom (7d)"

    def check_decay():
        from .core import get_db_connection

        conn = get_db_connection()
        cursor = conn.execute("SELECT COUNT(*) FROM wisdom WHERE confidence < 0.3")
        low_conf = cursor.fetchone()[0]
        cursor = conn.execute("SELECT COUNT(*) FROM wisdom")
        total = cursor.fetchone()[0]
        if total == 0:
            return "OK", "No wisdom to decay"
        pct = (low_conf / total) * 100
        if pct > 50:
            return "WARN", f"{low_conf}/{total} ({pct:.0f}%) decaying"
        return "OK", f"{low_conf}/{total} low confidence"

    # Run all checks by tier
    check("infrastructure", "Database", check_database)
    check("infrastructure", "Hooks", check_hooks)
    check("infrastructure", "Embeddings", check_embeddings)
    check("infrastructure", "LanceDB", check_lancedb)
    check("infrastructure", "Kuzu", check_kuzu)

    check("content", "Wisdom", check_wisdom_content)
    check("content", "Beliefs", check_beliefs)
    check("content", "Triggers", check_triggers)

    check("activity", "Sessions", check_recent_sessions)
    check("activity", "Applications", check_wisdom_applications)

    check("vitality", "Learning", check_recent_learning)
    check("vitality", "Decay", check_decay)

    # Display by tier
    tier_names = {
        "infrastructure": "INFRASTRUCTURE (can it run?)",
        "content": "CONTENT (does it remember?)",
        "activity": "ACTIVITY (is it used?)",
        "vitality": "VITALITY (is it growing?)",
    }

    symbols = {"OK": "+", "WARN": "~", "FAIL": "x"}
    has_fail = False
    has_warn = False

    print("=" * 55)
    print("SOUL HEALTH")
    print("=" * 55)

    for tier, tier_label in tier_names.items():
        print(f"\n{tier_label}")
        print("-" * 40)
        for name, status, detail in results[tier]:
            sym = symbols[status]
            print(f"  [{sym}] {name}: {detail}")
            if status == "FAIL":
                has_fail = True
            elif status == "WARN":
                has_warn = True

    print("\n" + "=" * 55)
    if has_fail:
        print("STATUS: CRITICAL - Some systems failing")
    elif has_warn:
        print("STATUS: HEALTHY - Some warnings")
    else:
        print("STATUS: THRIVING - All systems go")
    print("=" * 55)


def cmd_mood(args):
    """Show current soul mood."""
    init_soul()
    mood = compute_mood()

    if args.reflect:
        print(get_mood_reflection(mood))
    else:
        print(format_mood_display(mood))


def cmd_bridge(args):
    """Bridge between soul and project memory."""
    init_soul()

    if args.subcommand == "status":
        print("=" * 55)
        print("SOUL-MEMORY BRIDGE")
        print("=" * 55)
        print()

        if is_memory_available():
            print("[+] cc-memory: installed")
            project_mem = get_project_memory()
            if project_mem and "error" not in project_mem:
                print(f"[+] Project: {project_mem['project']}")
                print(
                    f"    Observations: {project_mem['stats'].get('observations', 0)}"
                )
                print(f"    Sessions: {project_mem['stats'].get('sessions', 0)}")
            else:
                print("[-] No project memory in current directory")
        else:
            print("[-] cc-memory: not installed")
            print("    Install with: pip install cc-memory")

    elif args.subcommand == "context":
        ctx = unified_context(compact=args.compact)
        if args.raw:
            import json

            print(json.dumps(ctx, indent=2, default=str))
        else:
            print(format_unified_context(ctx))

    elif args.subcommand == "promote":
        if not args.observation_id:
            print("Usage: soul bridge promote <observation-id>")
            return

        result = promote_observation(args.observation_id, as_type=args.type)
        if result.get("promoted"):
            print("Promoted observation to wisdom!")
            print(f"  Observation: {result['observation_id']}")
            print(f"  Wisdom ID: {result['wisdom_id']}")
            print(f"  Type: {result['wisdom_type']}")
            print(f"  From project: {result['project']}")
        else:
            print(f"Failed: {result.get('error', 'Unknown error')}")

    elif args.subcommand == "candidates":
        candidates = detect_wisdom_candidates()
        if not candidates:
            print("No wisdom candidates found across projects.")
            return

        print("=" * 55)
        print("WISDOM CANDIDATES")
        print("=" * 55)
        print("(Observations appearing across multiple projects)")
        print()

        for c in candidates[:10]:
            print(f"[{c['occurrences']}x] {c['title']}")
            print(f"    Category: {c['category']}")
            print(f"    Projects: {', '.join(c['projects'])}")
            print()

    elif args.subcommand == "signals":
        signals = get_project_signals()
        if not signals or "error" in signals:
            print("No project signals available.")
            return

        print("=" * 55)
        print(f"PROJECT SIGNALS: {signals['project']}")
        print("=" * 55)
        print()
        print(f"  Total observations: {signals['total_observations']}")
        print(f"  Recent failures: {signals['recent_failures']}")
        print(f"  Recent discoveries: {signals['recent_discoveries']}")
        print(f"  Recent features: {signals['recent_features']}")
        print(f"  Sessions: {signals['sessions']}")
        print(f"  Tokens invested: {signals['tokens_invested']:,}")

    else:
        print("Subcommands: status, context, promote, candidates, signals")


def cmd_budget(args):
    """Check context window budget from transcript."""
    from .budget import get_context_budget, format_budget_status

    transcript = args.transcript
    budget = get_context_budget(transcript)

    if budget is None:
        if transcript:
            print(f"Could not read transcript: {transcript}")
        else:
            print("No transcript specified and no current session found.")
            print("Usage: soul budget /path/to/transcript.jsonl")
        return

    print("=" * 55)
    print("CONTEXT BUDGET")
    print("=" * 55)
    print()
    print(format_budget_status(budget))
    print()
    print(f"  Total tokens:  {budget.total_tokens:,}")
    print(f"  Input tokens:  {budget.input_tokens:,}")
    print(f"  Output tokens: {budget.output_tokens:,}")
    print(f"  Cache tokens:  {budget.cache_tokens:,}")
    print()
    print(f"  Context size:  {budget.context_size:,}")
    print(f"  Usable size:   {budget.usable_size:,}")
    print(f"  Remaining:     {budget.remaining:,}")
    print()
    print(f"  Messages:      {budget.message_count}")
    print()

    if budget.should_urgent_save:
        print("! URGENT: Context nearly full. Save important context NOW.")
    elif budget.should_compact:
        print("! COMPACT: Switching to reduced soul injection.")


def cmd_context(args):
    """Show full context dump."""
    import pprint

    init_soul()
    pprint.pprint(get_soul_context())


def cmd_wisdom(args):
    """List wisdom entries."""
    init_soul()
    for w in recall_wisdom(limit=20):
        eff = w.get("effective_confidence", w["confidence"])
        print(f"[{w['type']}] {w['title']} ({eff:.0%})")
        print(f"  {w['content'][:80]}...")
        print()


def cmd_pending(args):
    """Show pending wisdom applications."""
    init_soul()
    pending = get_pending_applications()
    if not pending:
        print("No pending wisdom applications")
    else:
        for p in pending:
            print(f"[{p['id']}] {p['title']}")
            print(f"  Applied: {p['applied_at']}")
            if p["context"]:
                print(f"  Context: {p['context'][:60]}...")


def cmd_session(args):
    """Show wisdom applied in current session."""
    init_soul()
    session = get_session_wisdom()
    if not session:
        print("No wisdom applied this session")
    else:
        print(f"Wisdom applied this session: {len(session)}")
        for w in session:
            print(f"  - {w['title']}")
            if w.get("context"):
                print(f"    Context: {w['context'][:50]}...")
            print(f"    Applied: {w['applied_at']}")


def cmd_save(args):
    """Save context for later restoration."""
    init_soul()

    content = args.content
    if not content:
        print("Usage: soul save 'context to remember' [--type insight] [--priority 5]")
        return

    ctx_id = save_context(
        content=content, context_type=args.type, priority=args.priority
    )
    print(f"Saved context [{args.type}] (id={ctx_id}, priority={args.priority})")


def cmd_restore(args):
    """Show saved context for restoration."""
    init_soul()

    if args.subcommand == "recent":
        contexts = get_recent_context(hours=args.hours, limit=args.limit)
        if not contexts:
            print(f"No context saved in the last {args.hours} hours")
        else:
            print(format_context_restoration(contexts))

    elif args.subcommand == "session":
        contexts = get_saved_context(limit=args.limit)
        if not contexts:
            print("No context saved for current session")
        else:
            print(f"Saved context ({len(contexts)} items):\n")
            for ctx in contexts:
                icon = {
                    "insight": "💡",
                    "decision": "⚖️",
                    "blocker": "🚧",
                    "progress": "📊",
                    "key_file": "📁",
                    "todo": "☐",
                }.get(ctx["type"], "•")
                print(
                    f"  {icon} [{ctx['type']}] P{ctx['priority']}: {ctx['content'][:80]}..."
                )

    else:
        # Default: show recent
        contexts = get_recent_context(hours=24, limit=20)
        print(format_context_restoration(contexts))


def cmd_grow(args):
    """Grow the soul - add wisdom, beliefs, identity, vocabulary."""
    init_soul()

    if args.type == "wisdom":
        if not args.title or not args.content:
            print("Usage: soul grow wisdom 'Title' 'Content'")
            return
        gain_wisdom(WisdomType.PATTERN, args.title, args.content)
        print(f"✓ Wisdom: {args.title}")

    elif args.type == "insight":
        if not args.title or not args.content:
            print("Usage: soul grow insight 'Title' 'Content'")
            return
        gain_wisdom(WisdomType.INSIGHT, args.title, args.content)
        print(f"✓ Insight: {args.title}")

    elif args.type == "fail":
        if not args.title or not args.content:
            print("Usage: soul grow fail 'What failed' 'Why and what was learned'")
            return
        gain_wisdom(WisdomType.FAILURE, args.title, args.content)
        print(f"✓ Failure recorded: {args.title}")

    elif args.type == "belief":
        if not args.title:
            print("Usage: soul grow belief 'Belief statement'")
            return
        hold_belief(args.title, args.content or "")
        print(f"✓ Belief: {args.title}")

    elif args.type == "identity":
        if not args.title or not args.content:
            print("Usage: soul grow identity 'key' 'value'")
            return
        observe_identity(IdentityAspect.WORKFLOW, args.title, args.content)
        print(f"✓ Identity: {args.title}")

    elif args.type == "vocab":
        if not args.title or not args.content:
            print("Usage: soul grow vocab 'term' 'meaning'")
            return
        learn_term(args.title, args.content)
        print(f"✓ Vocabulary: {args.title}")

    else:
        print("Usage: soul grow <type> 'title' 'content'")
        print("Types: wisdom, insight, fail, belief, identity, vocab")


def cmd_stats(args):
    """Show wisdom analytics and usage patterns."""
    init_soul()

    if args.subcommand == "health":
        health = get_wisdom_health()
        timeline = get_wisdom_timeline(days=args.days)
        print(format_wisdom_stats(health, timeline))

    elif args.subcommand == "timeline":
        timeline = get_wisdom_timeline(days=args.days, bucket=args.bucket)
        if not timeline:
            print("No wisdom applications in this period")
        else:
            print(f"Wisdom applications (last {args.days} days, by {args.bucket}):\n")
            for bucket in timeline:
                apps = bucket["applications"]
                success_rate = bucket["successes"] / apps * 100 if apps else 0
                bar = "█" * min(30, apps)
                print(f"  {bucket['period']}: {bar} {apps} ({success_rate:.0f}%)")

    elif args.subcommand == "top":
        health = get_wisdom_health()
        if health.get("top_performers"):
            print("Top performing wisdom:\n")
            for w in health["top_performers"]:
                print(f"  ✓ {w['title']}")
                print(
                    f"    Success rate: {w['success_rate']:.0%}, Applications: {w['total_applications']}"
                )
                print()
        else:
            print("No wisdom with enough applications to rank")

    elif args.subcommand == "issues":
        health = get_wisdom_health()
        has_issues = False

        if health.get("decaying"):
            has_issues = True
            print("Decaying wisdom (confidence dropping):\n")
            for w in health["decaying"][:5]:
                print(f"  ↓ {w['title']}")
                print(
                    f"    Effective confidence: {w['effective_confidence']:.0%}, Inactive: {w['inactive_days']}d"
                )
            print()

        if health.get("failing"):
            has_issues = True
            print("Failing wisdom (>50% failure rate):\n")
            for w in health["failing"]:
                print(f"  ✗ {w['title']}")
                print(
                    f"    Success rate: {w['success_rate']:.0%}, Applications: {w['total_applications']}"
                )
            print()

        if health.get("stale"):
            has_issues = True
            print("Stale wisdom (never applied):\n")
            for w in health["stale"][:5]:
                print(f"  ? {w['title']}")
                print(f"    Age: {w['age_days']}d, Type: {w['type']}")
            print()

        if not has_issues:
            print("No issues found - all wisdom is healthy!")

    elif args.subcommand == "decay":
        decay_data = get_decay_visualization(limit=args.limit)
        print(format_decay_chart(decay_data))


def cmd_trends(args):
    """Show cross-session trends and growth trajectory."""
    init_soul()

    if args.subcommand == "growth":
        trajectory = get_growth_trajectory(days=args.days)
        comparison = get_session_comparison(session_count=args.sessions)
        patterns = get_learning_patterns()
        print(format_trends_report(comparison, trajectory, patterns))

    elif args.subcommand == "sessions":
        comparison = get_session_comparison(session_count=args.sessions)
        print(f"Session Analysis ({comparison['sessions_analyzed']} sessions)\n")
        print(f"Total wisdom gained: {comparison['total_wisdom_gained']}")
        print(f"Avg per session: {comparison['avg_wisdom_per_session']}\n")
        for s in comparison["sessions"]:
            gained = f"+{s['wisdom_gained']}" if s["wisdom_gained"] > 0 else "0"
            project = s["project"] or "unknown"
            print(
                f"  {s['date']}: {project[:25]} ({gained} wisdom, {s['wisdom_applied']} applied)"
            )
            if s["summary"]:
                print(f"    {s['summary'][:60]}...")

    elif args.subcommand == "patterns":
        patterns = get_learning_patterns()
        print("Learning Patterns\n")
        print(f"Recent wisdom analyzed: {patterns['recent_wisdom_count']}")
        print(f"Dominant type: {patterns['dominant_type']}")
        print(f"Growing domains: {', '.join(patterns['growing_domains'])}")
        if patterns["temporal_patterns"]["peak_hour"] is not None:
            print(
                f"Peak learning: {patterns['temporal_patterns']['peak_day']}s at {patterns['temporal_patterns']['peak_hour']}:00"
            )
        print(f"\nType distribution: {patterns['type_distribution']}")
        print(f"Domain distribution: {patterns['domain_distribution']}")

    elif args.subcommand == "velocity":
        trajectory = get_growth_trajectory(days=args.days)
        print(f"Learning Velocity ({trajectory['period_days']} days)\n")
        print(f"Total wisdom gained: {trajectory['total_wisdom_gained']}")
        print(f"Domains explored: {trajectory['total_domains']}")
        print(f"Weekly velocity: {trajectory['avg_weekly_velocity']} wisdom/week")
        print(f"Recent velocity: {trajectory['recent_velocity']} wisdom/week")
        print(f"Trend: {trajectory['velocity_trend']}")
        if trajectory.get("trajectory"):
            print("\nWeekly progress:")
            for t in trajectory["trajectory"][-12:]:
                bar = "█" * min(30, t["gained"])
                new_d = f" +{len(t['new_domains'])} domains" if t["new_domains"] else ""
                print(f"  {t['week']}: {bar} {t['gained']}{new_d}")


# Global ultrathink context (persists during session)
_ultrathink_ctx = None


def cmd_ultrathink(args):
    """Soul-integrated ultrathink mode."""
    global _ultrathink_ctx
    init_soul()

    if args.subcommand == "enter":
        problem = args.problem or ""
        if not problem:
            print("Usage: soul ultrathink enter 'Problem statement'")
            return

        _ultrathink_ctx = enter_ultrathink(problem, domain=args.domain)
        print(format_ultrathink_context(_ultrathink_ctx))

    elif args.subcommand == "context":
        if _ultrathink_ctx:
            print(format_ultrathink_context(_ultrathink_ctx))
        else:
            print("No active ultrathink session. Use 'cc-soul ultrathink enter' first.")

    elif args.subcommand == "discover":
        if not _ultrathink_ctx:
            print("No active ultrathink session. Use 'cc-soul ultrathink enter' first.")
            return

        discovery = args.discovery or ""
        if not discovery:
            print("Usage: soul ultrathink discover 'Key insight discovered'")
            return

        record_discovery(_ultrathink_ctx, discovery)
        print(f"Recorded discovery: {discovery[:60]}...")

    elif args.subcommand == "exit":
        if not _ultrathink_ctx:
            print("No active ultrathink session.")
            return

        summary = args.summary or "Session completed"
        reflection = exit_ultrathink(_ultrathink_ctx, summary)

        print("\n" + "=" * 60)
        print("ULTRATHINK SESSION REFLECTION")
        print("=" * 60)
        print(f"\nDuration: {reflection.duration_minutes:.1f} minutes")
        print(f"Wisdom applied: {reflection.wisdom_applied_count}")
        print(f"Discoveries: {len(reflection.discoveries)}")
        print(f"\nGrowth: {reflection.growth_summary}")

        if reflection.discoveries:
            print("\nDiscoveries made:")
            for d in reflection.discoveries:
                print(f"  - {d['discovery'][:60]}...")

            commit = input("\nCommit discoveries as wisdom? [y/N] ").strip().lower()
            if commit == "y":
                ids = commit_session_learnings(reflection)
                print(f"Committed {len(ids)} wisdom items")

        _ultrathink_ctx = None


def cmd_efficiency(args):
    """Token efficiency features - problem patterns, file hints, decisions."""
    init_soul()

    if args.subcommand == "stats":
        stats = get_token_stats()
        print("Token Efficiency Statistics\n")
        print(f"Problem patterns: {stats['problem_patterns']}")
        print(f"Pattern matches: {stats['pattern_matches']}")
        print(f"File hints: {stats['file_hints']}")
        print(f"Decisions recorded: {stats['decisions']}")
        print(f"\nEstimated tokens saved: ~{stats['estimated_tokens_saved']:,}")

    elif args.subcommand == "learn":
        if not args.pattern_type or not args.solution:
            print(
                "Usage: soul efficiency learn 'pattern description' --type bug --solution 'how to solve'"
            )
            return
        prompt = args.pattern_type  # Using positional as description
        learn_problem_pattern(
            prompt=prompt,
            problem_type=args.type,
            solution_pattern=args.solution,
            file_hints=args.files.split(",") if args.files else None,
        )
        print(f"Learned pattern: [{args.type}] {prompt[:50]}...")

    elif args.subcommand == "hint":
        if not args.file_path or not args.purpose:
            print(
                "Usage: soul efficiency hint '/path/to/file.py' 'Purpose description'"
            )
            return
        add_file_hint(
            file_path=args.file_path,
            purpose=args.purpose,
            key_functions=args.functions.split(",") if args.functions else None,
            related_to=args.related.split(",") if args.related else None,
        )
        print(f"Added hint: {args.file_path}")

    elif args.subcommand == "decide":
        if not args.topic or not args.decision:
            print("Usage: soul efficiency decide 'Topic' 'Decision made'")
            return
        record_decision(
            topic=args.topic,
            decision=args.decision,
            rationale=args.rationale or "",
            context=args.context or "",
        )
        print(f"Recorded decision: {args.topic}")

    elif args.subcommand == "decisions":
        decisions = recall_decisions(topic=args.topic, limit=args.limit)
        if not decisions:
            print("No decisions recorded")
        else:
            for d in decisions:
                print(f"[{d['made_at'][:10]}] {d['topic']}")
                print(f"  Decision: {d['decision'][:60]}...")
                if d["rationale"]:
                    print(f"  Rationale: {d['rationale'][:50]}...")
                print()

    elif args.subcommand == "compact":
        ctx = get_compact_context(project=args.project)
        print(ctx)

    elif args.subcommand == "check":
        if not args.prompt:
            print("Usage: soul efficiency check 'problem description'")
            return
        prompt = " ".join(args.prompt)
        injection = format_efficiency_injection(prompt)
        if injection:
            print(injection)
        else:
            print("No efficiency hints for this prompt")


def cmd_observe(args):
    """Manage passive observations from sessions."""
    init_soul()

    if args.subcommand == "pending":
        observations = get_pending_observations(limit=args.limit)
        if not observations:
            print("No pending observations")
        else:
            print(f"Pending observations ({len(observations)}):\n")
            type_icons = {
                "correction": "🔄",
                "preference": "👤",
                "pattern": "🔁",
                "struggle": "💪",
                "breakthrough": "💡",
                "file_pattern": "📁",
                "decision": "⚖️",
            }
            for obs in observations:
                icon = type_icons.get(obs["type"], "•")
                conf = obs["confidence"]
                print(f"  {icon} [{obs['id']}] ({conf:.0%}) {obs['content'][:60]}...")

    elif args.subcommand == "promote":
        if args.id:
            wisdom_id = promote_observation_to_wisdom(args.id)
            if wisdom_id:
                print(f"Promoted observation {args.id} to wisdom {wisdom_id}")
            else:
                print(f"Observation {args.id} not found")
        elif args.all:
            promoted = auto_promote_high_confidence(threshold=args.threshold)
            print(f"Promoted {len(promoted)} high-confidence observations")
        else:
            print("Usage: soul observe promote <id> or soul observe promote --all")

    elif args.subcommand == "stats":
        observations = get_pending_observations(limit=100)
        from collections import Counter

        by_type = Counter(obs["type"] for obs in observations)
        print(f"Pending observations: {len(observations)}\n")
        print("By type:")
        for t, count in by_type.most_common():
            print(f"  {t}: {count}")


def cmd_graph(args):
    """Concept graph exploration and management."""
    init_soul()

    if not KUZU_AVAILABLE:
        print("Graph module not available. Install with: pip install cc-soul[graph]")
        return

    if args.subcommand == "stats":
        stats = get_graph_stats()
        print("Concept Graph Statistics\n")
        print(f"Total concepts: {stats['nodes']}")
        print(f"Total edges: {stats['edges']}")
        if stats["by_type"]:
            print("\nBy type:")
            for t, count in stats["by_type"].items():
                print(f"  {t}: {count}")
        if stats["by_relation"]:
            print("\nBy relation:")
            for r, count in stats["by_relation"].items():
                print(f"  {r}: {count}")

    elif args.subcommand == "sync":
        print("Syncing soul data to concept graph...")
        stats = sync_wisdom_to_graph()
        print(f"Synced {stats['nodes']} concepts with {stats['edges']} relationships")

    elif args.subcommand == "search":
        if not args.query:
            print("Usage: soul graph search 'query'")
            return
        query = " ".join(args.query)
        concepts = search_concepts(query, limit=args.limit)
        if not concepts:
            print(f"No concepts matching '{query}'")
        else:
            print(f"Concepts matching '{query}':\n")
            for c in concepts:
                print(f"  [{c.type.value}] {c.title}")
                print(f"    {c.content[:60]}...")

    elif args.subcommand == "activate":
        if not args.prompt:
            print("Usage: soul graph activate 'prompt text'")
            return
        prompt = " ".join(args.prompt)
        result = activate_from_prompt(prompt, limit=args.limit)
        print(format_activation_result(result))

    elif args.subcommand == "neighbors":
        if not args.concept_id:
            print("Usage: soul graph neighbors <concept_id>")
            return
        neighbors = get_neighbors(args.concept_id, limit=args.limit)
        if not neighbors:
            print(f"No neighbors for concept '{args.concept_id}'")
        else:
            print(f"Neighbors of '{args.concept_id}':\n")
            for concept, edge in neighbors:
                print(
                    f"  --[{edge.relation.value} {edge.weight:.2f}]--> {concept.title}"
                )

    elif args.subcommand == "link":
        if not args.source or not args.target:
            print("Usage: soul graph link <source_id> <target_id> --relation <type>")
            return
        try:
            relation = RelationType(args.relation)
        except ValueError:
            print(
                f"Invalid relation. Options: {', '.join(r.value for r in RelationType)}"
            )
            return
        success = link_concepts(
            args.source, args.target, relation, evidence=args.evidence or ""
        )
        if success:
            print(f"Linked {args.source} --[{relation.value}]--> {args.target}")
        else:
            print("Failed to create link. Check that both concepts exist.")


def cmd_curious(args):
    """Curiosity engine - detect gaps and ask questions."""
    init_soul()

    if args.subcommand == "gaps":
        gaps = detect_all_gaps()
        if not gaps:
            print("No knowledge gaps detected")
        else:
            type_icons = {
                "recurring_problem": "🔄",
                "repeated_correction": "✏️",
                "unknown_file": "📁",
                "missing_rationale": "❓",
                "new_domain": "🌍",
                "stale_wisdom": "📦",
                "failed_pattern": "❌",
            }
            print(f"Knowledge Gaps ({len(gaps)}):\n")
            for gap in gaps[: args.limit]:
                icon = type_icons.get(gap.type.value, "•")
                print(f"  {icon} [{gap.priority:.0%}] {gap.description[:60]}...")
                if gap.evidence:
                    print(f"      Evidence: {gap.evidence[0][:50]}...")

    elif args.subcommand == "questions":
        questions = get_pending_questions(limit=args.limit)
        if not questions:
            print("No pending questions")
        else:
            print(f"Pending Questions ({len(questions)}):\n")
            for q in questions:
                print(f"  [{q.id}] ({q.priority:.0%}) {q.question}")
                if q.context:
                    print(f"      Context: {q.context[:50]}...")

    elif args.subcommand == "ask":
        questions = run_curiosity_cycle(max_questions=args.limit)
        if not questions:
            print("No questions to ask right now")
        else:
            print(format_questions_for_prompt(questions, max_questions=args.limit))

    elif args.subcommand == "answer":
        if not args.id or not args.answer:
            print("Usage: soul curious answer <question_id> 'your answer'")
            return
        answer_text = " ".join(args.answer)
        success = answer_question(args.id, answer_text, incorporate=args.incorporate)
        if success:
            print(f"Recorded answer for question {args.id}")
            if args.incorporate:
                wisdom_id = incorporate_answer_as_wisdom(args.id)
                if wisdom_id:
                    print(f"Created wisdom entry {wisdom_id}")
        else:
            print(f"Question {args.id} not found")

    elif args.subcommand == "dismiss":
        if not args.id:
            print("Usage: soul curious dismiss <question_id>")
            return
        success = dismiss_question(args.id)
        if success:
            print(f"Dismissed question {args.id}")
        else:
            print(f"Question {args.id} not found")

    elif args.subcommand == "stats":
        stats = get_curiosity_stats()
        print("Curiosity Engine Statistics\n")
        print(f"Open gaps: {stats['open_gaps']}")
        if stats["gaps_by_type"]:
            print("\nGaps by type:")
            for t, count in stats["gaps_by_type"].items():
                print(f"  {t}: {count}")
        print("\nQuestions:")
        print(f"  Pending: {stats['questions']['pending']}")
        print(f"  Answered: {stats['questions']['answered']}")
        print(f"  Incorporated: {stats['questions']['incorporated']}")
        print(f"  Dismissed: {stats['questions']['dismissed']}")
        print(f"\nIncorporation rate: {stats['incorporation_rate']:.0%}")


def cmd_story(args):
    """Narrative memory - stories and episodes."""
    init_soul()

    if args.subcommand == "stats":
        stats = get_narrative_stats()
        print("Narrative Memory Statistics\n")
        print(f"Total episodes: {stats['total_episodes']}")
        print(
            f"Story threads: {stats['total_threads']} ({stats['ongoing_threads']} ongoing)"
        )
        print(f"Total time: {stats['total_hours']} hours")
        if stats["by_type"]:
            print("\nBy type:")
            for t, count in stats["by_type"].items():
                print(f"  {t}: {count}")

    elif args.subcommand == "breakthroughs":
        episodes = recall_breakthroughs(limit=args.limit)
        if not episodes:
            print("No breakthrough moments recorded yet")
        else:
            print(f"Breakthrough Moments ({len(episodes)}):\n")
            for ep in episodes:
                print(format_episode_story(ep))
                print("\n---\n")

    elif args.subcommand == "struggles":
        episodes = recall_struggles(limit=args.limit)
        if not episodes:
            print("No struggle moments recorded yet")
        else:
            print(f"Learning from Struggles ({len(episodes)}):\n")
            for ep in episodes:
                print(format_episode_story(ep))
                print("\n---\n")

    elif args.subcommand == "journey":
        journey = get_emotional_journey(days=args.days)
        print(f"Emotional Journey (last {args.days} days)\n")
        if journey["dominant"]:
            print(f"Dominant emotion: {journey['dominant']}")
        print(f"Breakthroughs: {journey['breakthroughs']}")
        print(f"Struggles: {journey['struggles']}")
        if journey["distribution"]:
            print("\nDistribution:")
            for emotion, pct in sorted(
                journey["distribution"].items(), key=lambda x: -x[1]
            ):
                bar = "█" * int(pct * 20)
                print(f"  {emotion:15} {bar} {pct:.0%}")

    elif args.subcommand == "characters":
        chars = get_recurring_characters(limit=args.limit)
        print("Recurring Characters\n")
        if chars["files"]:
            print("Files:")
            for f, count in chars["files"][:10]:
                print(f"  {f}: {count} episodes")
        if chars["concepts"]:
            print("\nConcepts:")
            for c, count in chars["concepts"][:10]:
                print(f"  {c}: {count} episodes")
        if chars["tools"]:
            print("\nTools:")
            for t, count in chars["tools"][:10]:
                print(f"  {t}: {count} episodes")

    elif args.subcommand == "episode":
        if not args.id:
            print("Usage: soul story episode <id>")
            return
        ep = get_episode(args.id)
        if ep:
            print(format_episode_story(ep))
        else:
            print(f"Episode {args.id} not found")

    elif args.subcommand == "recall":
        if args.type:
            try:
                ep_type = EpisodeType(args.type)
                episodes = recall_by_type(ep_type, limit=args.limit)
            except ValueError:
                print(
                    f"Unknown type. Options: {', '.join(t.value for t in EpisodeType)}"
                )
                return
        elif args.character:
            episodes = recall_by_character(args.character, limit=args.limit)
        else:
            print("Usage: soul story recall --type <type> or --character <name>")
            return

        if not episodes:
            print("No matching episodes")
        else:
            for ep in episodes:
                print(f"[{ep.id}] {ep.title}")
                if ep.summary:
                    print(f"    {ep.summary[:60]}...")


def cmd_neural(args):
    """Neural triggers - activation keys for Claude's latent knowledge."""
    init_soul()

    if args.subcommand == "stats":
        stats = get_trigger_stats()
        print("Neural Trigger Statistics\n")
        print(f"Total triggers: {stats.get('total_triggers', 0)}")
        print(f"Total bridges: {stats.get('total_bridges', 0)}")
        print(f"Total uses: {stats.get('total_uses', 0)}")
        if stats.get("avg_strength"):
            print(f"Avg strength: {stats['avg_strength']:.2f}")
        if stats.get("domains"):
            print(f"\nDomains: {', '.join(stats['domains'][:10])}")

    elif args.subcommand == "sync":
        print("Converting wisdom to neural triggers...")
        result = sync_wisdom_to_triggers()
        print(f"Processed {result['wisdom_count']} wisdom entries")
        print(f"Created {result['triggers_created']} triggers")

    elif args.subcommand == "activate":
        if not args.prompt:
            print("Usage: soul neural activate 'your prompt'")
            return
        prompt = " ".join(args.prompt)
        activation = activate_with_bridges(prompt)
        if activation:
            print("Neural activation key:\n")
            print(activation)
        else:
            print("No triggers matched this prompt")

    elif args.subcommand == "context":
        if not args.prompt:
            print("Usage: soul neural context 'your prompt'")
            return
        prompt = " ".join(args.prompt)
        ctx = format_neural_context(prompt)
        if ctx:
            print("Inject this into context:\n")
            print(ctx)
        else:
            print("No neural context generated")

    elif args.subcommand == "extract":
        if not args.text or not args.domain:
            print("Usage: soul neural extract --domain <domain> 'text'")
            return
        text = " ".join(args.text)
        trigger = create_trigger(text, args.domain)
        print(f"Created trigger: {trigger.id}")
        print(f"Domain: {trigger.domain}")
        print(f"Anchor tokens: {' '.join(trigger.anchor_tokens)}")

    elif args.subcommand == "bridge":
        if not args.source or not args.target:
            print(
                "Usage: soul neural bridge <source_domain> <target_domain> --via 'connecting text'"
            )
            return
        via = " ".join(args.via) if args.via else f"{args.source} {args.target}"
        bridge = create_bridge(args.source, args.target, via, args.evidence or "")
        print(f"Bridge created: {args.source} <-> {args.target}")
        print(f"Via: {' '.join(bridge.bridge_tokens)}")

    elif args.subcommand == "find":
        if not args.prompt:
            print("Usage: soul neural find 'prompt'")
            return
        prompt = " ".join(args.prompt)
        triggers = find_triggers(prompt, top_k=args.limit)
        if not triggers:
            print("No matching triggers")
        else:
            print(f"Found {len(triggers)} triggers:\n")
            for trigger, score in triggers:
                print(f"  [{score:.0%}] {trigger.domain}")
                print(f"       {' '.join(trigger.anchor_tokens)}")

    elif args.subcommand == "potential":
        if not args.potential_action:
            print("Usage: soul neural potential list [--domain <domain>]")
            print(
                "       soul neural potential add --obs 'what you noticed' --tension 'what seems unresolved' --potential 'what you might understand'"
            )
            return

        if args.potential_action == "list":
            vectors = get_growth_vectors(domain=args.domain, limit=10)
            if not vectors:
                print("No growth vectors stored yet")
            else:
                print(f"Growth Vectors ({len(vectors)}):\n")
                for v in vectors:
                    print(f"  [{', '.join(v.domains)}]")
                    print(f"    Observed: {v.observation[:60]}...")
                    print(f"    Tension: {v.tension[:60]}...")
                    print(f"    Potential: {v.potential[:60]}...")
                    print()

        elif args.potential_action == "add":
            if not args.obs or not args.tension or not args.potential:
                print(
                    "Usage: soul neural potential add --obs '...' --tension '...' --potential '...'"
                )
                return
            domains = [args.domain] if args.domain else None
            v = save_growth_vector(args.obs, args.tension, args.potential, domains)
            print(f"Saved growth vector: {v.id}")
            print(f"Domains: {', '.join(v.domains)}")

    elif args.subcommand == "learn":
        if not args.text:
            print("Usage: soul neural learn 'text with potential insight'")
            return
        text = " ".join(args.text)
        result = auto_learn_from_output(text)
        if result:
            print(f"Extracted {result['type']}:")
            if result["type"] == "breakthrough":
                print(f"  Insight: {result['insight'][:80]}...")
            else:
                print(f"  Learning: {result['content'][:80]}...")
            print(f"  Trigger ID: {result['trigger_id']}")
        else:
            print("No learnable content detected")

    elif args.subcommand == "resonance":
        if not args.resonance_action:
            print("Usage: soul neural resonance stats")
            print(
                "       soul neural resonance add --concepts 'ancient DNA' 'authentication' --query 'What confirms authenticity?'"
            )
            print("       soul neural resonance find 'your prompt'")
            return

        if args.resonance_action == "stats":
            stats = get_resonance_stats()
            print("Resonance Pattern Statistics\n")
            print(f"Total patterns: {stats.get('total_patterns', 0)}")
            if stats.get("avg_amplification"):
                print(f"Avg amplification: {stats['avg_amplification']:.2f}x")
            if stats.get("domains"):
                print(f"Domains: {', '.join(stats['domains'])}")

        elif args.resonance_action == "add":
            if not args.concepts or not args.query:
                print(
                    "Usage: soul neural resonance add --concepts 'concept1' 'concept2' --query 'deeper question'"
                )
                return
            pattern = create_resonance(args.concepts, args.query, args.amp or 1.5)
            print(f"Created resonance pattern: {pattern.id}")
            print(f"Concepts: {', '.join(pattern.concepts)}")
            print(f"Depth query: {pattern.depth_query}")

        elif args.resonance_action == "find":
            if not args.prompt:
                print("Usage: soul neural resonance find 'your prompt'")
                return
            prompt = " ".join(args.prompt)
            resonances = find_resonance(prompt)
            if not resonances:
                print("No resonance patterns matched")
            else:
                print(f"Found {len(resonances)} resonances:\n")
                for pattern, score in resonances:
                    print(f"  [{score:.0%}] {', '.join(pattern.concepts)}")
                    print(f"       Depth: {pattern.depth_query}")

    elif args.subcommand == "emotions":
        emotions = get_emotional_contexts(domain=args.domain, limit=args.limit or 10)
        if not emotions:
            print("No emotional contexts tracked yet")
        else:
            print(f"Emotional Contexts ({len(emotions)}):\n")
            for e in emotions:
                print(f"  [{e.response}] {e.trigger[:50]}...")
                print(
                    f"    Intensity: {'█' * int(e.intensity * 10)}{'░' * (10 - int(e.intensity * 10))}"
                )
                print()


def cmd_backup(args):
    """Backup and restore the soul."""
    init_soul()

    if args.subcommand == "create":
        path = create_timestamped_backup()
        print(f"Backup created: {path}")

    elif args.subcommand == "dump":
        from pathlib import Path

        output = Path(args.output) if args.output else None
        result = dump_soul(output)

        if "error" in result:
            print(f"Error: {result['error']}")
            return

        if output:
            print(f"Soul exported to: {output}")
        else:
            import json

            print(json.dumps(result, indent=2, default=str))

        print(
            f"\nExported: {len(result.get('wisdom', []))} wisdom, "
            f"{len(result.get('beliefs', []))} beliefs, "
            f"{len(result.get('insights', []))} insights"
        )

    elif args.subcommand == "load":
        from pathlib import Path

        result = restore_soul(Path(args.input), merge=args.merge)

        if "error" in result:
            print(f"Error: {result['error']}")
            return

        print(f"Soul restored from: {args.input}")
        print(f"Mode: {'merge' if args.merge else 'replace'}")
        for k, v in result.get("counts", {}).items():
            print(f"  {k}: {v}")

    elif args.subcommand == "list":
        backups = list_backups()
        print(format_backup_list(backups))


def cmd_reindex(args):
    """Reindex wisdom vectors."""
    init_soul()
    reindex_all_wisdom()


def cmd_install_hooks(args):
    """Install Claude Code hooks for soul integration."""
    import json
    import importlib.resources as pkg_resources
    from datetime import datetime

    claude_dir = Path.home() / ".claude"
    settings_path = claude_dir / "settings.json"
    hooks_dir = claude_dir / "hooks"

    # Create hooks directory
    hooks_dir.mkdir(parents=True, exist_ok=True)

    # Backup existing settings
    if settings_path.exists():
        backup_name = f"settings.json.backup.{datetime.now().strftime('%Y%m%d_%H%M%S')}"
        backup_path = claude_dir / backup_name
        shutil.copy(settings_path, backup_path)
        print(f"Backed up settings to: {backup_path}")

        with open(settings_path) as f:
            settings = json.load(f)
    else:
        settings = {}

    # Ensure hooks section exists
    if "hooks" not in settings:
        settings["hooks"] = {}

    # Add soul hooks (preserving existing ones)
    soul_hooks = {
        "SessionStart": [
            {
                "matcher": "startup",
                "hooks": [{"type": "command", "command": "cc-soul hook start --rich"}],
            },
            {
                "matcher": "resume",
                "hooks": [{"type": "command", "command": "cc-soul hook start"}],
            },
            {
                "matcher": "compact",
                "hooks": [
                    {"type": "command", "command": "cc-soul hook start --after-compact"}
                ],
            },
        ],
        "PreCompact": [
            {
                "matcher": "",
                "hooks": [{"type": "command", "command": "cc-soul hook pre-compact"}],
            }
        ],
        "UserPromptSubmit": [
            {
                "matcher": "",
                "hooks": [{"type": "command", "command": "cc-soul hook prompt"}],
            }
        ],
        "Stop": [
            {
                "matcher": "",
                "hooks": [
                    {"type": "command", "command": str(hooks_dir / "soul-stop.sh")}
                ],
            }
        ],
        "SessionEnd": [
            {"matcher": "", "hooks": [{"type": "command", "command": "cc-soul hook end"}]}
        ],
    }

    for hook_name, hook_config in soul_hooks.items():
        if hook_name not in settings["hooks"]:
            settings["hooks"][hook_name] = hook_config
            print(f"Added {hook_name} hook")
        else:
            print(f"Skipped {hook_name} (already configured)")

    # Write settings
    with open(settings_path, "w") as f:
        json.dump(settings, f, indent=2)

    # Install hook scripts from package
    try:
        hooks_src = Path(pkg_resources.files("cc_soul") / "hooks")
    except (TypeError, AttributeError):
        import pkg_resources as old_pkg

        hooks_src = Path(old_pkg.resource_filename("cc_soul", "hooks"))

    if hooks_src.exists():
        for script in hooks_src.glob("*.sh"):
            dest = hooks_dir / script.name
            if dest.exists() and not args.force:
                print(f"Skipped {script.name} (exists, use --force)")
            else:
                shutil.copy(script, dest)
                dest.chmod(0o755)
                print(f"Installed {script.name}")
    else:
        print(f"Warning: Hook scripts not found in package at {hooks_src}")

    # Also install permissions
    print()
    cmd_install_permissions(args)

    print()
    print("Soul hooks installed!")
    print("To uninstall: cc-soul uninstall-hooks")

    return 0


def cmd_uninstall_hooks(args):
    """Uninstall Claude Code hooks and restore settings backup."""
    import json

    claude_dir = Path.home() / ".claude"
    settings_path = claude_dir / "settings.json"
    hooks_dir = claude_dir / "hooks"

    # Find most recent backup
    backups = sorted(claude_dir.glob("settings.json.backup.*"), reverse=True)

    if args.restore and backups:
        backup_path = backups[0]
        print(f"Restoring from: {backup_path}")
        shutil.copy(backup_path, settings_path)
        print("Settings restored!")
    elif settings_path.exists():
        # Remove soul hooks from settings
        with open(settings_path) as f:
            settings = json.load(f)

        if "hooks" in settings:
            removed = []
            for hook_name in [
                "SessionStart",
                "UserPromptSubmit",
                "Stop",
                "SessionEnd",
                "PreCompact",
            ]:
                if hook_name in settings["hooks"]:
                    del settings["hooks"][hook_name]
                    removed.append(hook_name)

            if removed:
                with open(settings_path, "w") as f:
                    json.dump(settings, f, indent=2)
                print(f"Removed hooks: {', '.join(removed)}")
            else:
                print("No soul hooks found in settings")

    # Remove hook scripts
    if hooks_dir.exists():
        for script in ["soul-stop.sh"]:
            script_path = hooks_dir / script
            if script_path.exists():
                script_path.unlink()
                print(f"Removed {script}")

    print()
    print("Soul hooks uninstalled!")
    if backups:
        print(f"Backup available: {backups[0]}")

    return 0


def cmd_install_permissions(args):
    """Add soul and cc-memory MCP tools to auto-approve list."""
    import json

    claude_dir = Path.home() / ".claude"
    settings_path = claude_dir / "settings.json"

    # Load or create settings
    if settings_path.exists():
        with open(settings_path) as f:
            settings = json.load(f)
    else:
        settings = {}

    # Ensure permissions section exists
    if "permissions" not in settings:
        settings["permissions"] = {}
    if "allow" not in settings["permissions"]:
        settings["permissions"]["allow"] = []

    # Define patterns to add
    patterns = ["mcp__soul__*", "mcp__cc-memory__*"]

    added = []
    for pattern in patterns:
        if pattern not in settings["permissions"]["allow"]:
            settings["permissions"]["allow"].append(pattern)
            added.append(pattern)

    if added:
        with open(settings_path, "w") as f:
            json.dump(settings, f, indent=2)
        print("Added auto-approve permissions:")
        for p in added:
            print(f"  - {p}")
        print("\nChanges take effect in new sessions.")
    else:
        print("Permissions already configured:")
        for p in patterns:
            print(f"  - {p}")


def cmd_setup(args):
    """Register soul as MCP server with Claude Code."""
    claude_path = shutil.which("claude")
    if not claude_path:
        print("Error: 'claude' CLI not found in PATH")
        print("Install Claude Code first: https://claude.ai/code")
        sys.exit(1)

    mcp_path = shutil.which("cc-soul-mcp")
    if not mcp_path:
        print("Error: 'cc-soul-mcp' not found in PATH")
        print("Reinstall with: pip install cc-soul")
        sys.exit(1)

    scope = "--scope user" if args.user else ""
    cmd = f"claude mcp add soul {scope} -- {mcp_path}"

    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)

    if result.returncode == 0:
        print("soul registered as MCP server")
        print("Restart Claude Code to activate")
    else:
        if "already exists" in result.stderr.lower():
            print("soul already registered")
        else:
            print(f"Error: {result.stderr}")
            sys.exit(1)


def cmd_unsetup(args):
    """Remove soul MCP server from Claude Code."""
    claude_path = shutil.which("claude")
    if not claude_path:
        print("Error: 'claude' CLI not found")
        sys.exit(1)

    scope = "--scope user" if args.user else ""
    cmd = f"claude mcp remove soul {scope}"

    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)

    if result.returncode == 0:
        print("soul removed from Claude Code")
    else:
        print(f"Error: {result.stderr}")

    return 0


def cmd_install_skills(args):
    """Install bundled skills to ~/.claude/skills."""
    import importlib.resources as pkg_resources

    skills_dest = Path.home() / ".claude" / "skills"
    skills_dest.mkdir(parents=True, exist_ok=True)

    try:
        skills_src = Path(pkg_resources.files("cc_soul") / "skills")
    except (TypeError, AttributeError):
        import pkg_resources as old_pkg

        skills_src = Path(old_pkg.resource_filename("cc_soul", "skills"))

    if not skills_src.exists():
        print(f"Error: Skills directory not found in package at {skills_src}")
        return 1

    installed = []
    skipped = []

    for skill_dir in skills_src.iterdir():
        if not skill_dir.is_dir():
            continue

        dest_dir = skills_dest / skill_dir.name

        if dest_dir.exists() and not args.force:
            skipped.append(skill_dir.name)
            continue

        if dest_dir.exists():
            shutil.rmtree(dest_dir)

        shutil.copytree(skill_dir, dest_dir)
        installed.append(skill_dir.name)

    if installed:
        print(f"Installed skills: {', '.join(installed)}")
    if skipped:
        print(
            f"Skipped (already exist, use --force to overwrite): {', '.join(skipped)}"
        )
    if not installed and not skipped:
        print("No skills found in package")

    return 0


def cmd_hook(args):
    """Run a hook."""
    import json as json_lib
    from .budget import save_transcript_path

    init_soul()

    # Parse stdin as JSON to extract transcript_path and prompt
    stdin_data = None
    if not args.input:
        raw_input = sys.stdin.read()
        try:
            stdin_data = json_lib.loads(raw_input)
            # Save transcript path for budget tracking
            transcript_path = stdin_data.get("transcript_path")
            if transcript_path:
                save_transcript_path(transcript_path)
        except json_lib.JSONDecodeError:
            # Not JSON, treat as plain text
            stdin_data = {"prompt": raw_input}

    if args.hook == "start":
        # session_start with optional flags
        after_compact = getattr(args, "after_compact", False)
        include_rich = getattr(args, "rich", False)
        print(session_start(after_compact=after_compact, include_rich=include_rich))
    elif args.hook == "start-rich":
        # session_start returning both greeting and additional context
        from .hooks import session_start_rich

        greeting, rich = session_start_rich()
        print(greeting)
        print("---ADDITIONAL_CONTEXT---")
        print(rich)
    elif args.hook == "pre-compact":
        # PreCompact hook - save context before compaction
        from .hooks import pre_compact

        transcript_path = stdin_data.get("transcript_path") if stdin_data else None
        output = pre_compact(transcript_path=transcript_path)
        if output:
            print(output)
    elif args.hook == "end":
        print(session_end())
    elif args.hook == "prompt":
        if args.input:
            text = " ".join(args.input)
        elif stdin_data:
            text = stdin_data.get("prompt", "")
        else:
            text = ""
        transcript_path = stdin_data.get("transcript_path") if stdin_data else None
        output = user_prompt(text, transcript_path=transcript_path)
        if output:
            print(output)
    elif args.hook == "stop":
        if args.input:
            text = " ".join(args.input)
        elif stdin_data:
            text = stdin_data.get("prompt", stdin_data.get("output", ""))
        else:
            text = ""
        assistant_stop(text)


def cmd_evolve(args):
    """Manage soul evolution insights."""
    init_soul()
    seed_evolution_insights()

    if args.subcommand == "list":
        insights = get_evolution_insights(category=args.category, limit=args.limit)
        for i in insights:
            priority_icon = {
                "critical": "🔴",
                "high": "🟠",
                "medium": "🟡",
                "low": "🟢",
            }.get(i["priority"], "⚪")
            print(f"{priority_icon} [{i['category']}] {i['insight'][:60]}...")
            if i["suggested_change"]:
                print(f"   → {i['suggested_change'][:60]}...")
            print()

    elif args.subcommand == "summary":
        summary = get_evolution_summary()
        print(f"Total insights: {summary['total']}")
        print(f"Open: {summary['open']}, Implemented: {summary['implemented']}")
        print(f"High priority open: {summary['high_priority_open']}")
        print("\nBy category:")
        for cat, count in summary["by_category"].items():
            print(f"  {cat}: {count}")


def cmd_introspect(args):
    """Run self-introspection."""
    init_soul()

    if args.subcommand == "report":
        report = generate_introspection_report()
        if args.json:
            import json

            print(json.dumps(report, indent=2, default=str))
        else:
            print(format_introspection_report(report))

    elif args.subcommand == "pain":
        pain = analyze_pain_points()
        print(f"Open pain points: {pain['total_open']}")
        if pain["by_severity"]:
            print(f"By severity: {pain['by_severity']}")
        if pain["by_category"]:
            print(f"By category: {pain['by_category']}")
        print("\nRecent:")
        for p in pain.get("recent", [])[:5]:
            icon = {"critical": "🔴", "high": "🟠", "medium": "🟡", "low": "🟢"}.get(
                p["severity"], "⚪"
            )
            print(f"  {icon} [{p['category']}] {p['description'][:60]}...")

    elif args.subcommand == "diagnose":
        diag = diagnose()
        print(f"Improvement targets: {diag['summary']['total_targets']}")
        print(f"  Critical: {diag['summary']['critical']}")
        print(f"  High: {diag['summary']['high']}")
        print("\nTop targets:")
        for t in diag["targets"][:5]:
            print(f"  P{t['priority']}: [{t['type']}] {t['description'][:50]}...")


def cmd_improve(args):
    """Manage improvements."""
    init_soul()

    if args.subcommand == "suggest":
        suggestions = suggest_improvements(limit=args.limit)
        for i, s in enumerate(suggestions, 1):
            print(f"\n{'=' * 60}")
            print(f"Suggestion {i}: {s['target']['description'][:60]}...")
            print(f"Type: {s['target']['type']}, Priority: {s['target']['priority']}")
            print("\nPrompt for Claude:")
            print(s["prompt"])

    elif args.subcommand == "proposals":
        proposals = get_proposals(limit=args.limit)
        if not proposals:
            print("No proposals yet")
        else:
            for p in proposals:
                status_icon = {
                    "proposed": "📝",
                    "validating": "🔄",
                    "validated": "✓",
                    "applying": "⚙️",
                    "applied": "✅",
                    "failed": "❌",
                    "rejected": "🚫",
                }.get(p["status"], "?")
                print(f"{status_icon} [{p['id'][:15]}...] {p['title']}")
                print(f"   {p['description'][:60]}...")

    elif args.subcommand == "stats":
        stats = get_improvement_stats()
        print(f"Total improvements: {stats['total']}")
        print(f"Success rate: {stats['success_rate']:.0%}")
        print(
            f"Successes: {stats.get('successes', 0)}, Failures: {stats.get('failures', 0)}"
        )
        if stats.get("by_category"):
            print("\nBy category:")
            for cat, count in stats["by_category"].items():
                print(f"  {cat}: {count}")


def main():
    parser = argparse.ArgumentParser(
        description="CC-Soul: Persistent Identity for Claude Code", prog="cc-soul"
    )
    subparsers = parser.add_subparsers(dest="command", help="Commands")

    # Summary (default)
    subparsers.add_parser("summary", help="Show soul summary")

    # Seed
    seed_parser = subparsers.add_parser(
        "seed", help="Seed soul with foundational beliefs and wisdom"
    )
    seed_parser.add_argument(
        "--force", action="store_true", help="Reseed even if already seeded"
    )

    # Health check
    subparsers.add_parser("health", help="Check system health and dependencies")

    # Mood
    mood_parser = subparsers.add_parser("mood", help="Show current soul mood")
    mood_parser.add_argument(
        "--reflect", action="store_true", help="Show reflective narrative"
    )

    # Bridge (soul <-> memory integration)
    bridge_parser = subparsers.add_parser(
        "bridge", help="Bridge between soul and project memory"
    )
    bridge_sub = bridge_parser.add_subparsers(dest="subcommand")

    bridge_sub.add_parser("status", help="Show bridge status")

    ctx_parser = bridge_sub.add_parser("context", help="Show unified context")
    ctx_parser.add_argument("--compact", action="store_true", help="Compact output")
    ctx_parser.add_argument("--raw", action="store_true", help="Raw JSON output")

    promote_parser = bridge_sub.add_parser(
        "promote", help="Promote observation to wisdom"
    )
    promote_parser.add_argument(
        "observation_id", nargs="?", help="Observation ID to promote"
    )
    promote_parser.add_argument(
        "--type",
        default="pattern",
        help="Wisdom type (pattern, insight, principle, failure)",
    )

    bridge_sub.add_parser("candidates", help="Find wisdom candidates across projects")
    bridge_sub.add_parser("signals", help="Show project signals for mood")

    # Budget (context window tracking)
    budget_parser = subparsers.add_parser("budget", help="Check context window budget")
    budget_parser.add_argument("transcript", nargs="?", help="Path to transcript file")

    # Context
    subparsers.add_parser("context", help="Show full context dump")

    # Wisdom
    subparsers.add_parser("wisdom", help="List wisdom entries")

    # Pending
    subparsers.add_parser("pending", help="Show pending wisdom applications")

    # Session
    subparsers.add_parser("session", help="Show wisdom applied this session")

    # Save (context persistence)
    save_parser = subparsers.add_parser(
        "save", help="Save context for later restoration"
    )
    save_parser.add_argument("content", nargs="?", help="Context to save")
    save_parser.add_argument(
        "--type",
        choices=["insight", "decision", "blocker", "progress", "key_file", "todo"],
        default="insight",
        help="Type of context",
    )
    save_parser.add_argument(
        "--priority",
        type=int,
        default=5,
        help="Priority 1-10 (higher = more important)",
    )

    # Restore (context restoration)
    restore_parser = subparsers.add_parser("restore", help="Restore saved context")
    restore_subs = restore_parser.add_subparsers(dest="subcommand")

    recent_parser = restore_subs.add_parser(
        "recent", help="Show context from last N hours"
    )
    recent_parser.add_argument(
        "--hours", type=int, default=24, help="Hours to look back"
    )
    recent_parser.add_argument(
        "--limit", type=int, default=20, help="Max items to show"
    )

    session_ctx_parser = restore_subs.add_parser(
        "session", help="Show context from current session"
    )
    session_ctx_parser.add_argument(
        "--limit", type=int, default=20, help="Max items to show"
    )

    # Grow
    grow_parser = subparsers.add_parser(
        "grow", help="Grow the soul (add wisdom, beliefs, etc)"
    )
    grow_parser.add_argument(
        "type",
        choices=["wisdom", "insight", "fail", "belief", "identity", "vocab"],
        help="Type of entry to add",
    )
    grow_parser.add_argument("title", nargs="?", help="Title or key")
    grow_parser.add_argument("content", nargs="?", help="Content or value")

    # Reindex
    subparsers.add_parser("reindex", help="Reindex wisdom vectors")

    # Install skills
    install_parser = subparsers.add_parser(
        "install-skills", help="Install bundled skills to ~/.claude/skills"
    )
    install_parser.add_argument(
        "--force", action="store_true", help="Overwrite existing skills"
    )

    # Install hooks
    hooks_install_parser = subparsers.add_parser(
        "install-hooks", help="Install Claude Code hooks for soul integration"
    )
    hooks_install_parser.add_argument(
        "--force", action="store_true", help="Overwrite existing hook scripts"
    )

    # Uninstall hooks
    hooks_uninstall_parser = subparsers.add_parser(
        "uninstall-hooks", help="Uninstall Claude Code hooks"
    )
    hooks_uninstall_parser.add_argument(
        "--restore", action="store_true", help="Restore settings from backup"
    )

    # Install permissions
    subparsers.add_parser(
        "install-permissions",
        help="Add soul and cc-memory MCP tools to auto-approve list",
    )

    # Setup MCP
    setup_parser = subparsers.add_parser(
        "setup", help="Register soul MCP server with Claude Code"
    )
    setup_parser.add_argument(
        "--user", action="store_true", help="Install for user (all projects)"
    )

    # Unsetup MCP
    unsetup_parser = subparsers.add_parser(
        "unsetup", help="Remove soul MCP server from Claude Code"
    )
    unsetup_parser.add_argument(
        "--user", action="store_true", help="Remove from user scope"
    )

    # Hook
    hook_parser = subparsers.add_parser("hook", help="Run a Claude Code hook")
    hook_parser.add_argument(
        "hook", choices=["start", "start-rich", "pre-compact", "end", "prompt", "stop"]
    )
    hook_parser.add_argument("input", nargs="*", help="Input for prompt/stop hook")
    hook_parser.add_argument("--rich", action="store_true", help="Include rich context")
    hook_parser.add_argument(
        "--after-compact", action="store_true", help="After compaction resume"
    )

    # Evolve
    evolve_parser = subparsers.add_parser("evolve", help="Manage evolution insights")
    evolve_subs = evolve_parser.add_subparsers(dest="subcommand")

    list_parser = evolve_subs.add_parser("list", help="List evolution insights")
    list_parser.add_argument("--category", help="Filter by category")
    list_parser.add_argument("--limit", type=int, default=20)

    evolve_subs.add_parser("summary", help="Show evolution summary")

    # Introspect
    intro_parser = subparsers.add_parser("introspect", help="Self-introspection")
    intro_subs = intro_parser.add_subparsers(dest="subcommand")

    report_parser = intro_subs.add_parser(
        "report", help="Generate introspection report"
    )
    report_parser.add_argument("--json", action="store_true", help="Output as JSON")

    intro_subs.add_parser("pain", help="Show pain points")
    intro_subs.add_parser("diagnose", help="Diagnose improvement targets")

    # Improve
    imp_parser = subparsers.add_parser("improve", help="Self-improvement")
    imp_subs = imp_parser.add_subparsers(dest="subcommand")

    suggest_parser = imp_subs.add_parser("suggest", help="Suggest improvements")
    suggest_parser.add_argument("--limit", type=int, default=3)

    proposals_parser = imp_subs.add_parser("proposals", help="List proposals")
    proposals_parser.add_argument("--limit", type=int, default=20)

    imp_subs.add_parser("stats", help="Show improvement statistics")

    # Stats (wisdom analytics)
    stats_parser = subparsers.add_parser(
        "stats", help="Wisdom analytics and usage patterns"
    )
    stats_subs = stats_parser.add_subparsers(dest="subcommand")

    health_parser = stats_subs.add_parser("health", help="Overall wisdom health report")
    health_parser.add_argument("--days", type=int, default=30, help="Days to analyze")

    timeline_parser = stats_subs.add_parser("timeline", help="Application timeline")
    timeline_parser.add_argument("--days", type=int, default=30, help="Days to analyze")
    timeline_parser.add_argument(
        "--bucket", choices=["day", "week", "month"], default="day"
    )

    stats_subs.add_parser("top", help="Top performing wisdom")
    stats_subs.add_parser("issues", help="Wisdom issues (decaying, failing, stale)")

    decay_parser = stats_subs.add_parser(
        "decay", help="Visualize wisdom confidence decay over time"
    )
    decay_parser.add_argument(
        "--limit", type=int, default=20, help="Number of wisdom items to show"
    )

    # Trends (cross-session analysis)
    trends_parser = subparsers.add_parser(
        "trends", help="Cross-session trends and growth trajectory"
    )
    trends_subs = trends_parser.add_subparsers(dest="subcommand")

    growth_parser = trends_subs.add_parser("growth", help="Full growth report")
    growth_parser.add_argument("--days", type=int, default=90, help="Days to analyze")
    growth_parser.add_argument(
        "--sessions", type=int, default=10, help="Recent sessions to compare"
    )

    sessions_parser = trends_subs.add_parser(
        "sessions", help="Session-by-session analysis"
    )
    sessions_parser.add_argument(
        "--sessions", type=int, default=10, help="Number of sessions"
    )

    trends_subs.add_parser("patterns", help="Learning patterns analysis")

    velocity_parser = trends_subs.add_parser(
        "velocity", help="Learning velocity over time"
    )
    velocity_parser.add_argument("--days", type=int, default=90, help="Days to analyze")

    # Ultrathink (soul-integrated deep reasoning)
    ultra_parser = subparsers.add_parser(
        "ultrathink", help="Soul-integrated deep reasoning mode"
    )
    ultra_subs = ultra_parser.add_subparsers(dest="subcommand")

    enter_parser = ultra_subs.add_parser(
        "enter", help="Enter ultrathink mode with problem statement"
    )
    enter_parser.add_argument("problem", nargs="?", help="Problem statement")
    enter_parser.add_argument(
        "--domain", help="Domain hint (bioinformatics, web, cli, etc)"
    )

    ultra_subs.add_parser("context", help="Show current ultrathink context")

    discover_parser = ultra_subs.add_parser(
        "discover", help="Record a discovery during reasoning"
    )
    discover_parser.add_argument("discovery", nargs="?", help="The discovery/insight")

    exit_parser = ultra_subs.add_parser("exit", help="Exit ultrathink and reflect")
    exit_parser.add_argument("summary", nargs="?", help="Session summary")

    # Efficiency (token-saving features)
    eff_parser = subparsers.add_parser("efficiency", help="Token efficiency features")
    eff_subs = eff_parser.add_subparsers(dest="subcommand")

    eff_subs.add_parser("stats", help="Show token efficiency statistics")

    learn_parser = eff_subs.add_parser("learn", help="Learn a problem pattern")
    learn_parser.add_argument("pattern_type", nargs="?", help="Problem description")
    learn_parser.add_argument(
        "--type",
        choices=["bug", "feature", "performance", "refactor", "config"],
        default="bug",
        help="Problem type",
    )
    learn_parser.add_argument("--solution", help="Solution pattern")
    learn_parser.add_argument("--files", help="Comma-separated file hints")

    hint_parser = eff_subs.add_parser("hint", help="Add file hint")
    hint_parser.add_argument("file_path", nargs="?", help="File path")
    hint_parser.add_argument("purpose", nargs="?", help="Purpose of file")
    hint_parser.add_argument("--functions", help="Comma-separated key functions")
    hint_parser.add_argument("--related", help="Comma-separated related keywords")

    decide_parser = eff_subs.add_parser("decide", help="Record a decision")
    decide_parser.add_argument("topic", nargs="?", help="Decision topic")
    decide_parser.add_argument("decision", nargs="?", help="The decision made")
    decide_parser.add_argument("--rationale", help="Why this decision")
    decide_parser.add_argument("--context", help="Additional context")

    decisions_parser = eff_subs.add_parser("decisions", help="List past decisions")
    decisions_parser.add_argument("--topic", help="Filter by topic")
    decisions_parser.add_argument("--limit", type=int, default=10, help="Max items")

    compact_parser = eff_subs.add_parser("compact", help="Show compact context")
    compact_parser.add_argument("--project", help="Filter by project")

    check_parser = eff_subs.add_parser(
        "check", help="Check efficiency hints for a prompt"
    )
    check_parser.add_argument("prompt", nargs="*", help="Prompt to check")

    # Observe (passive learning)
    obs_parser = subparsers.add_parser("observe", help="Manage passive observations")
    obs_subs = obs_parser.add_subparsers(dest="subcommand")

    pending_parser = obs_subs.add_parser("pending", help="Show pending observations")
    pending_parser.add_argument(
        "--limit", type=int, default=20, help="Max items to show"
    )

    promote_parser = obs_subs.add_parser(
        "promote", help="Promote observation to wisdom"
    )
    promote_parser.add_argument(
        "id", type=int, nargs="?", help="Observation ID to promote"
    )
    promote_parser.add_argument(
        "--all", action="store_true", help="Promote all high-confidence"
    )
    promote_parser.add_argument(
        "--threshold", type=float, default=0.75, help="Confidence threshold"
    )

    obs_subs.add_parser("stats", help="Show observation statistics")

    # Graph (concept graph exploration)
    graph_parser = subparsers.add_parser("graph", help="Concept graph exploration")
    graph_subs = graph_parser.add_subparsers(dest="subcommand")

    graph_subs.add_parser("stats", help="Show graph statistics")
    graph_subs.add_parser("sync", help="Sync soul data to concept graph")

    search_g_parser = graph_subs.add_parser("search", help="Search concepts")
    search_g_parser.add_argument("query", nargs="*", help="Search query")
    search_g_parser.add_argument("--limit", type=int, default=10, help="Max results")

    activate_parser = graph_subs.add_parser(
        "activate", help="Activate concepts from prompt"
    )
    activate_parser.add_argument("prompt", nargs="*", help="Prompt text")
    activate_parser.add_argument("--limit", type=int, default=10, help="Max results")

    neighbors_parser = graph_subs.add_parser("neighbors", help="Show concept neighbors")
    neighbors_parser.add_argument("concept_id", nargs="?", help="Concept ID")
    neighbors_parser.add_argument("--limit", type=int, default=10, help="Max results")

    link_parser = graph_subs.add_parser("link", help="Link two concepts")
    link_parser.add_argument("source", nargs="?", help="Source concept ID")
    link_parser.add_argument("target", nargs="?", help="Target concept ID")
    link_parser.add_argument(
        "--relation",
        default="related_to",
        help="Relation type (related_to, led_to, contradicts, etc)",
    )
    link_parser.add_argument("--evidence", help="Evidence for the link")

    # Curious (curiosity engine)
    curious_parser = subparsers.add_parser(
        "curious", help="Curiosity engine - gaps and questions"
    )
    curious_subs = curious_parser.add_subparsers(dest="subcommand")

    gaps_parser = curious_subs.add_parser("gaps", help="Detect knowledge gaps")
    gaps_parser.add_argument("--limit", type=int, default=10, help="Max gaps to show")

    questions_parser = curious_subs.add_parser(
        "questions", help="Show pending questions"
    )
    questions_parser.add_argument(
        "--limit", type=int, default=10, help="Max questions to show"
    )

    ask_parser = curious_subs.add_parser(
        "ask", help="Run curiosity cycle and show questions to ask"
    )
    ask_parser.add_argument("--limit", type=int, default=3, help="Max questions to ask")

    answer_parser = curious_subs.add_parser("answer", help="Answer a question")
    answer_parser.add_argument("id", type=int, nargs="?", help="Question ID")
    answer_parser.add_argument("answer", nargs="*", help="Your answer")
    answer_parser.add_argument(
        "--incorporate", action="store_true", help="Also create wisdom from answer"
    )

    dismiss_parser = curious_subs.add_parser("dismiss", help="Dismiss a question")
    dismiss_parser.add_argument("id", type=int, nargs="?", help="Question ID")

    curious_subs.add_parser("stats", help="Show curiosity statistics")

    # Story (narrative memory)
    story_parser = subparsers.add_parser(
        "story", help="Narrative memory - episodes and stories"
    )
    story_subs = story_parser.add_subparsers(dest="subcommand")

    story_subs.add_parser("stats", help="Show narrative statistics")

    breakthroughs_parser = story_subs.add_parser(
        "breakthroughs", help="Recall breakthrough moments"
    )
    breakthroughs_parser.add_argument(
        "--limit", type=int, default=5, help="Max episodes"
    )

    struggles_parser = story_subs.add_parser(
        "struggles", help="Recall struggle moments"
    )
    struggles_parser.add_argument("--limit", type=int, default=5, help="Max episodes")

    journey_parser = story_subs.add_parser("journey", help="Show emotional journey")
    journey_parser.add_argument("--days", type=int, default=30, help="Days to analyze")

    chars_parser = story_subs.add_parser("characters", help="Show recurring characters")
    chars_parser.add_argument("--limit", type=int, default=10, help="Max characters")

    episode_parser = story_subs.add_parser("episode", help="View a specific episode")
    episode_parser.add_argument("id", type=int, nargs="?", help="Episode ID")

    recall_parser = story_subs.add_parser(
        "recall", help="Recall episodes by type or character"
    )
    recall_parser.add_argument("--type", help="Episode type (bugfix, feature, etc)")
    recall_parser.add_argument("--character", help="Character name (file, concept)")
    recall_parser.add_argument("--limit", type=int, default=10, help="Max episodes")

    # Neural (activation triggers)
    neural_parser = subparsers.add_parser(
        "neural", help="Neural triggers - activation keys for latent knowledge"
    )
    neural_subs = neural_parser.add_subparsers(dest="subcommand")

    neural_subs.add_parser("stats", help="Show trigger statistics")
    neural_subs.add_parser("sync", help="Convert wisdom to neural triggers")

    neural_activate = neural_subs.add_parser(
        "activate", help="Generate activation for a prompt"
    )
    neural_activate.add_argument("prompt", nargs="*", help="Prompt text")

    neural_context = neural_subs.add_parser(
        "context", help="Generate injectable context"
    )
    neural_context.add_argument("prompt", nargs="*", help="Prompt text")

    neural_extract = neural_subs.add_parser("extract", help="Extract trigger from text")
    neural_extract.add_argument("text", nargs="*", help="Text to extract from")
    neural_extract.add_argument("--domain", required=True, help="Knowledge domain")

    neural_bridge = neural_subs.add_parser(
        "bridge", help="Create bridge between domains"
    )
    neural_bridge.add_argument("source", nargs="?", help="Source domain")
    neural_bridge.add_argument("target", nargs="?", help="Target domain")
    neural_bridge.add_argument("--via", nargs="*", help="Connecting text")
    neural_bridge.add_argument("--evidence", help="Why this connection exists")

    neural_find = neural_subs.add_parser("find", help="Find triggers matching a prompt")
    neural_find.add_argument("prompt", nargs="*", help="Prompt text")
    neural_find.add_argument("--limit", type=int, default=5, help="Max results")

    neural_potential = neural_subs.add_parser(
        "potential", help="Manage growth vectors (unrealized potential)"
    )
    neural_potential.add_argument(
        "potential_action", nargs="?", choices=["list", "add"], help="Action"
    )
    neural_potential.add_argument("--domain", help="Filter by domain")
    neural_potential.add_argument("--obs", help="What you observed")
    neural_potential.add_argument("--tension", help="What seems unresolved")
    neural_potential.add_argument("--potential", help="What you might understand")

    neural_learn = neural_subs.add_parser("learn", help="Extract learnings from text")
    neural_learn.add_argument("text", nargs="*", help="Text to analyze for learnings")

    neural_resonance = neural_subs.add_parser(
        "resonance", help="Resonance patterns for deeper activation"
    )
    neural_resonance.add_argument(
        "resonance_action", nargs="?", choices=["stats", "add", "find"], help="Action"
    )
    neural_resonance.add_argument("prompt", nargs="*", help="Prompt for find action")
    neural_resonance.add_argument(
        "--concepts", nargs="*", help="Concepts that resonate"
    )
    neural_resonance.add_argument("--query", help="Deeper question to activate")
    neural_resonance.add_argument(
        "--amp", type=float, help="Amplification factor (default 1.5)"
    )

    neural_emotions = neural_subs.add_parser(
        "emotions", help="View tracked emotional contexts"
    )
    neural_emotions.add_argument("--domain", help="Filter by domain")
    neural_emotions.add_argument("--limit", type=int, default=10, help="Max results")

    # Backup (soul preservation)
    backup_parser = subparsers.add_parser("backup", help="Backup and restore soul")
    backup_subs = backup_parser.add_subparsers(dest="subcommand")

    backup_subs.add_parser("create", help="Create timestamped backup")

    dump_parser = backup_subs.add_parser("dump", help="Export soul to JSON file")
    dump_parser.add_argument("output", nargs="?", help="Output file path")

    load_parser = backup_subs.add_parser("load", help="Restore soul from backup")
    load_parser.add_argument("input", help="Backup file path")
    load_parser.add_argument(
        "--merge", action="store_true", help="Merge with existing instead of replacing"
    )

    backup_subs.add_parser("list", help="List available backups")

    args = parser.parse_args()

    if args.command is None or args.command == "summary":
        cmd_summary(args)
    elif args.command == "seed":
        cmd_seed(args)
    elif args.command == "health":
        cmd_health(args)
    elif args.command == "mood":
        cmd_mood(args)
    elif args.command == "bridge":
        cmd_bridge(args)
    elif args.command == "budget":
        cmd_budget(args)
    elif args.command == "context":
        cmd_context(args)
    elif args.command == "wisdom":
        cmd_wisdom(args)
    elif args.command == "pending":
        cmd_pending(args)
    elif args.command == "session":
        cmd_session(args)
    elif args.command == "save":
        cmd_save(args)
    elif args.command == "restore":
        if args.subcommand:
            cmd_restore(args)
        else:
            cmd_restore(argparse.Namespace(subcommand="recent", hours=24, limit=20))
    elif args.command == "grow":
        cmd_grow(args)
    elif args.command == "reindex":
        cmd_reindex(args)
    elif args.command == "install-skills":
        cmd_install_skills(args)
    elif args.command == "install-hooks":
        cmd_install_hooks(args)
    elif args.command == "uninstall-hooks":
        cmd_uninstall_hooks(args)
    elif args.command == "install-permissions":
        cmd_install_permissions(args)
    elif args.command == "setup":
        cmd_setup(args)
    elif args.command == "unsetup":
        cmd_unsetup(args)
    elif args.command == "hook":
        cmd_hook(args)
    elif args.command == "evolve":
        if args.subcommand:
            cmd_evolve(args)
        else:
            cmd_evolve(argparse.Namespace(subcommand="summary"))
    elif args.command == "introspect":
        if args.subcommand:
            cmd_introspect(args)
        else:
            cmd_introspect(argparse.Namespace(subcommand="report", json=False))
    elif args.command == "improve":
        if args.subcommand:
            cmd_improve(args)
        else:
            cmd_improve(argparse.Namespace(subcommand="suggest", limit=3))
    elif args.command == "stats":
        if args.subcommand:
            cmd_stats(args)
        else:
            cmd_stats(argparse.Namespace(subcommand="health", days=30))
    elif args.command == "trends":
        if args.subcommand:
            cmd_trends(args)
        else:
            cmd_trends(argparse.Namespace(subcommand="growth", days=90, sessions=10))
    elif args.command == "ultrathink":
        if args.subcommand:
            cmd_ultrathink(args)
        else:
            print("Usage: soul ultrathink <enter|context|discover|exit>")
    elif args.command == "efficiency":
        if args.subcommand:
            cmd_efficiency(args)
        else:
            cmd_efficiency(argparse.Namespace(subcommand="stats"))
    elif args.command == "observe":
        if args.subcommand:
            cmd_observe(args)
        else:
            cmd_observe(argparse.Namespace(subcommand="pending", limit=20))
    elif args.command == "graph":
        if args.subcommand:
            cmd_graph(args)
        else:
            cmd_graph(argparse.Namespace(subcommand="stats"))
    elif args.command == "curious":
        if args.subcommand:
            cmd_curious(args)
        else:
            cmd_curious(argparse.Namespace(subcommand="gaps", limit=10))
    elif args.command == "story":
        if args.subcommand:
            cmd_story(args)
        else:
            cmd_story(argparse.Namespace(subcommand="stats"))
    elif args.command == "neural":
        if args.subcommand:
            cmd_neural(args)
        else:
            cmd_neural(argparse.Namespace(subcommand="stats"))
    elif args.command == "backup":
        if args.subcommand:
            cmd_backup(args)
        else:
            cmd_backup(argparse.Namespace(subcommand="list"))


if __name__ == "__main__":
    main()
