"""
Command-line interface for cc-soul.
"""

import sys
import argparse

from .core import init_soul, summarize_soul, get_soul_context
from .wisdom import recall_wisdom, get_pending_applications, get_session_wisdom, gain_wisdom, WisdomType
from .beliefs import get_beliefs, hold_belief
from .vocabulary import get_vocabulary, learn_term
from .identity import observe_identity, IdentityAspect
from .hooks import session_start, session_end, user_prompt
from .vectors import reindex_all_wisdom
from .evolve import get_evolution_insights, get_evolution_summary, seed_evolution_insights
from .introspect import (
    generate_introspection_report,
    format_introspection_report,
    get_pain_points,
    analyze_pain_points,
    get_wisdom_timeline,
    get_wisdom_health,
    format_wisdom_stats,
)
from .improve import (
    diagnose,
    suggest_improvements,
    format_improvement_prompt,
    get_proposals,
    get_improvement_stats,
    ImprovementStatus,
)
from .ultrathink import (
    enter_ultrathink,
    exit_ultrathink,
    format_ultrathink_context,
    record_discovery,
    commit_session_learnings,
)


def cmd_summary(args):
    """Show soul summary."""
    init_soul()
    print(summarize_soul())


def cmd_context(args):
    """Show full context dump."""
    import pprint
    init_soul()
    pprint.pprint(get_soul_context())


def cmd_wisdom(args):
    """List wisdom entries."""
    init_soul()
    for w in recall_wisdom(limit=20):
        eff = w.get('effective_confidence', w['confidence'])
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
            if p['context']:
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
            if w.get('context'):
                print(f"    Context: {w['context'][:50]}...")
            print(f"    Applied: {w['applied_at']}")


def cmd_grow(args):
    """Grow the soul - add wisdom, beliefs, identity, vocabulary."""
    init_soul()

    if args.type == 'wisdom':
        if not args.title or not args.content:
            print("Usage: soul grow wisdom 'Title' 'Content'")
            return
        gain_wisdom(WisdomType.PATTERN, args.title, args.content)
        print(f"✓ Wisdom: {args.title}")

    elif args.type == 'insight':
        if not args.title or not args.content:
            print("Usage: soul grow insight 'Title' 'Content'")
            return
        gain_wisdom(WisdomType.INSIGHT, args.title, args.content)
        print(f"✓ Insight: {args.title}")

    elif args.type == 'fail':
        if not args.title or not args.content:
            print("Usage: soul grow fail 'What failed' 'Why and what was learned'")
            return
        gain_wisdom(WisdomType.FAILURE, args.title, args.content)
        print(f"✓ Failure recorded: {args.title}")

    elif args.type == 'belief':
        if not args.title:
            print("Usage: soul grow belief 'Belief statement'")
            return
        hold_belief(args.title, args.content or "")
        print(f"✓ Belief: {args.title}")

    elif args.type == 'identity':
        if not args.title or not args.content:
            print("Usage: soul grow identity 'key' 'value'")
            return
        observe_identity(IdentityAspect.WORKFLOW, args.title, args.content)
        print(f"✓ Identity: {args.title}")

    elif args.type == 'vocab':
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

    if args.subcommand == 'health':
        health = get_wisdom_health()
        timeline = get_wisdom_timeline(days=args.days)
        print(format_wisdom_stats(health, timeline))

    elif args.subcommand == 'timeline':
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

    elif args.subcommand == 'top':
        health = get_wisdom_health()
        if health.get("top_performers"):
            print("Top performing wisdom:\n")
            for w in health["top_performers"]:
                print(f"  ✓ {w['title']}")
                print(f"    Success rate: {w['success_rate']:.0%}, Applications: {w['total_applications']}")
                print()
        else:
            print("No wisdom with enough applications to rank")

    elif args.subcommand == 'issues':
        health = get_wisdom_health()
        has_issues = False

        if health.get("decaying"):
            has_issues = True
            print("Decaying wisdom (confidence dropping):\n")
            for w in health["decaying"][:5]:
                print(f"  ↓ {w['title']}")
                print(f"    Effective confidence: {w['effective_confidence']:.0%}, Inactive: {w['inactive_days']}d")
            print()

        if health.get("failing"):
            has_issues = True
            print("Failing wisdom (>50% failure rate):\n")
            for w in health["failing"]:
                print(f"  ✗ {w['title']}")
                print(f"    Success rate: {w['success_rate']:.0%}, Applications: {w['total_applications']}")
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


# Global ultrathink context (persists during session)
_ultrathink_ctx = None


def cmd_ultrathink(args):
    """Soul-integrated ultrathink mode."""
    global _ultrathink_ctx
    init_soul()

    if args.subcommand == 'enter':
        problem = args.problem or ""
        if not problem:
            print("Usage: soul ultrathink enter 'Problem statement'")
            return

        _ultrathink_ctx = enter_ultrathink(problem, domain=args.domain)
        print(format_ultrathink_context(_ultrathink_ctx))

    elif args.subcommand == 'context':
        if _ultrathink_ctx:
            print(format_ultrathink_context(_ultrathink_ctx))
        else:
            print("No active ultrathink session. Use 'soul ultrathink enter' first.")

    elif args.subcommand == 'discover':
        if not _ultrathink_ctx:
            print("No active ultrathink session. Use 'soul ultrathink enter' first.")
            return

        discovery = args.discovery or ""
        if not discovery:
            print("Usage: soul ultrathink discover 'Key insight discovered'")
            return

        record_discovery(_ultrathink_ctx, discovery)
        print(f"Recorded discovery: {discovery[:60]}...")

    elif args.subcommand == 'exit':
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
            if commit == 'y':
                ids = commit_session_learnings(reflection)
                print(f"Committed {len(ids)} wisdom items")

        _ultrathink_ctx = None


def cmd_reindex(args):
    """Reindex wisdom vectors."""
    init_soul()
    reindex_all_wisdom()


def cmd_hook(args):
    """Run a hook."""
    init_soul()

    if args.hook == 'start':
        print(session_start())
    elif args.hook == 'end':
        print(session_end())
    elif args.hook == 'prompt':
        if args.input:
            text = " ".join(args.input)
        else:
            text = sys.stdin.read()
        output = user_prompt(text)
        if output:
            print(output)


def cmd_evolve(args):
    """Manage soul evolution insights."""
    init_soul()
    seed_evolution_insights()

    if args.subcommand == 'list':
        insights = get_evolution_insights(category=args.category, limit=args.limit)
        for i in insights:
            priority_icon = {'critical': '🔴', 'high': '🟠', 'medium': '🟡', 'low': '🟢'}.get(i['priority'], '⚪')
            print(f"{priority_icon} [{i['category']}] {i['insight'][:60]}...")
            if i['suggested_change']:
                print(f"   → {i['suggested_change'][:60]}...")
            print()

    elif args.subcommand == 'summary':
        summary = get_evolution_summary()
        print(f"Total insights: {summary['total']}")
        print(f"Open: {summary['open']}, Implemented: {summary['implemented']}")
        print(f"High priority open: {summary['high_priority_open']}")
        print("\nBy category:")
        for cat, count in summary['by_category'].items():
            print(f"  {cat}: {count}")


def cmd_introspect(args):
    """Run self-introspection."""
    init_soul()

    if args.subcommand == 'report':
        report = generate_introspection_report()
        if args.json:
            import json
            print(json.dumps(report, indent=2, default=str))
        else:
            print(format_introspection_report(report))

    elif args.subcommand == 'pain':
        pain = analyze_pain_points()
        print(f"Open pain points: {pain['total_open']}")
        if pain['by_severity']:
            print(f"By severity: {pain['by_severity']}")
        if pain['by_category']:
            print(f"By category: {pain['by_category']}")
        print("\nRecent:")
        for p in pain.get('recent', [])[:5]:
            icon = {'critical': '🔴', 'high': '🟠', 'medium': '🟡', 'low': '🟢'}.get(p['severity'], '⚪')
            print(f"  {icon} [{p['category']}] {p['description'][:60]}...")

    elif args.subcommand == 'diagnose':
        diag = diagnose()
        print(f"Improvement targets: {diag['summary']['total_targets']}")
        print(f"  Critical: {diag['summary']['critical']}")
        print(f"  High: {diag['summary']['high']}")
        print("\nTop targets:")
        for t in diag['targets'][:5]:
            print(f"  P{t['priority']}: [{t['type']}] {t['description'][:50]}...")


def cmd_improve(args):
    """Manage improvements."""
    init_soul()

    if args.subcommand == 'suggest':
        suggestions = suggest_improvements(limit=args.limit)
        for i, s in enumerate(suggestions, 1):
            print(f"\n{'='*60}")
            print(f"Suggestion {i}: {s['target']['description'][:60]}...")
            print(f"Type: {s['target']['type']}, Priority: {s['target']['priority']}")
            print(f"\nPrompt for Claude:")
            print(s['prompt'])

    elif args.subcommand == 'proposals':
        proposals = get_proposals(limit=args.limit)
        if not proposals:
            print("No proposals yet")
        else:
            for p in proposals:
                status_icon = {
                    'proposed': '📝',
                    'validating': '🔄',
                    'validated': '✓',
                    'applying': '⚙️',
                    'applied': '✅',
                    'failed': '❌',
                    'rejected': '🚫',
                }.get(p['status'], '?')
                print(f"{status_icon} [{p['id'][:15]}...] {p['title']}")
                print(f"   {p['description'][:60]}...")

    elif args.subcommand == 'stats':
        stats = get_improvement_stats()
        print(f"Total improvements: {stats['total']}")
        print(f"Success rate: {stats['success_rate']:.0%}")
        print(f"Successes: {stats.get('successes', 0)}, Failures: {stats.get('failures', 0)}")
        if stats.get('by_category'):
            print("\nBy category:")
            for cat, count in stats['by_category'].items():
                print(f"  {cat}: {count}")


def main():
    parser = argparse.ArgumentParser(
        description='CC-Soul: Persistent Identity for Claude Code',
        prog='soul'
    )
    subparsers = parser.add_subparsers(dest='command', help='Commands')

    # Summary (default)
    subparsers.add_parser('summary', help='Show soul summary')

    # Context
    subparsers.add_parser('context', help='Show full context dump')

    # Wisdom
    subparsers.add_parser('wisdom', help='List wisdom entries')

    # Pending
    subparsers.add_parser('pending', help='Show pending wisdom applications')

    # Session
    subparsers.add_parser('session', help='Show wisdom applied this session')

    # Grow
    grow_parser = subparsers.add_parser('grow', help='Grow the soul (add wisdom, beliefs, etc)')
    grow_parser.add_argument('type', choices=['wisdom', 'insight', 'fail', 'belief', 'identity', 'vocab'],
                            help='Type of entry to add')
    grow_parser.add_argument('title', nargs='?', help='Title or key')
    grow_parser.add_argument('content', nargs='?', help='Content or value')

    # Reindex
    subparsers.add_parser('reindex', help='Reindex wisdom vectors')

    # Hook
    hook_parser = subparsers.add_parser('hook', help='Run a Claude Code hook')
    hook_parser.add_argument('hook', choices=['start', 'end', 'prompt'])
    hook_parser.add_argument('input', nargs='*', help='Input for prompt hook')

    # Evolve
    evolve_parser = subparsers.add_parser('evolve', help='Manage evolution insights')
    evolve_subs = evolve_parser.add_subparsers(dest='subcommand')

    list_parser = evolve_subs.add_parser('list', help='List evolution insights')
    list_parser.add_argument('--category', help='Filter by category')
    list_parser.add_argument('--limit', type=int, default=20)

    evolve_subs.add_parser('summary', help='Show evolution summary')

    # Introspect
    intro_parser = subparsers.add_parser('introspect', help='Self-introspection')
    intro_subs = intro_parser.add_subparsers(dest='subcommand')

    report_parser = intro_subs.add_parser('report', help='Generate introspection report')
    report_parser.add_argument('--json', action='store_true', help='Output as JSON')

    intro_subs.add_parser('pain', help='Show pain points')
    intro_subs.add_parser('diagnose', help='Diagnose improvement targets')

    # Improve
    imp_parser = subparsers.add_parser('improve', help='Self-improvement')
    imp_subs = imp_parser.add_subparsers(dest='subcommand')

    suggest_parser = imp_subs.add_parser('suggest', help='Suggest improvements')
    suggest_parser.add_argument('--limit', type=int, default=3)

    proposals_parser = imp_subs.add_parser('proposals', help='List proposals')
    proposals_parser.add_argument('--limit', type=int, default=20)

    imp_subs.add_parser('stats', help='Show improvement statistics')

    # Stats (wisdom analytics)
    stats_parser = subparsers.add_parser('stats', help='Wisdom analytics and usage patterns')
    stats_subs = stats_parser.add_subparsers(dest='subcommand')

    health_parser = stats_subs.add_parser('health', help='Overall wisdom health report')
    health_parser.add_argument('--days', type=int, default=30, help='Days to analyze')

    timeline_parser = stats_subs.add_parser('timeline', help='Application timeline')
    timeline_parser.add_argument('--days', type=int, default=30, help='Days to analyze')
    timeline_parser.add_argument('--bucket', choices=['day', 'week', 'month'], default='day')

    stats_subs.add_parser('top', help='Top performing wisdom')
    stats_subs.add_parser('issues', help='Wisdom issues (decaying, failing, stale)')

    # Ultrathink (soul-integrated deep reasoning)
    ultra_parser = subparsers.add_parser('ultrathink', help='Soul-integrated deep reasoning mode')
    ultra_subs = ultra_parser.add_subparsers(dest='subcommand')

    enter_parser = ultra_subs.add_parser('enter', help='Enter ultrathink mode with problem statement')
    enter_parser.add_argument('problem', nargs='?', help='Problem statement')
    enter_parser.add_argument('--domain', help='Domain hint (bioinformatics, web, cli, etc)')

    ultra_subs.add_parser('context', help='Show current ultrathink context')

    discover_parser = ultra_subs.add_parser('discover', help='Record a discovery during reasoning')
    discover_parser.add_argument('discovery', nargs='?', help='The discovery/insight')

    exit_parser = ultra_subs.add_parser('exit', help='Exit ultrathink and reflect')
    exit_parser.add_argument('summary', nargs='?', help='Session summary')

    args = parser.parse_args()

    if args.command is None or args.command == 'summary':
        cmd_summary(args)
    elif args.command == 'context':
        cmd_context(args)
    elif args.command == 'wisdom':
        cmd_wisdom(args)
    elif args.command == 'pending':
        cmd_pending(args)
    elif args.command == 'session':
        cmd_session(args)
    elif args.command == 'grow':
        cmd_grow(args)
    elif args.command == 'reindex':
        cmd_reindex(args)
    elif args.command == 'hook':
        cmd_hook(args)
    elif args.command == 'evolve':
        if args.subcommand:
            cmd_evolve(args)
        else:
            cmd_evolve(argparse.Namespace(subcommand='summary'))
    elif args.command == 'introspect':
        if args.subcommand:
            cmd_introspect(args)
        else:
            cmd_introspect(argparse.Namespace(subcommand='report', json=False))
    elif args.command == 'improve':
        if args.subcommand:
            cmd_improve(args)
        else:
            cmd_improve(argparse.Namespace(subcommand='suggest', limit=3))
    elif args.command == 'stats':
        if args.subcommand:
            cmd_stats(args)
        else:
            cmd_stats(argparse.Namespace(subcommand='health', days=30))
    elif args.command == 'ultrathink':
        if args.subcommand:
            cmd_ultrathink(args)
        else:
            print("Usage: soul ultrathink <enter|context|discover|exit>")


if __name__ == '__main__':
    main()
