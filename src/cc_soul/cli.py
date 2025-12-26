"""
Command-line interface for cc-soul.
"""

import sys
import argparse

from .core import init_soul, summarize_soul, get_soul_context
from .wisdom import recall_wisdom, get_pending_applications, WisdomType
from .beliefs import get_beliefs
from .vocabulary import get_vocabulary
from .hooks import session_start, session_end, user_prompt
from .vectors import reindex_all_wisdom
from .evolve import get_evolution_insights, get_evolution_summary, seed_evolution_insights
from .introspect import (
    generate_introspection_report,
    format_introspection_report,
    get_pain_points,
    analyze_pain_points,
)
from .improve import (
    diagnose,
    suggest_improvements,
    format_improvement_prompt,
    get_proposals,
    get_improvement_stats,
    ImprovementStatus,
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

    args = parser.parse_args()

    if args.command is None or args.command == 'summary':
        cmd_summary(args)
    elif args.command == 'context':
        cmd_context(args)
    elif args.command == 'wisdom':
        cmd_wisdom(args)
    elif args.command == 'pending':
        cmd_pending(args)
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


if __name__ == '__main__':
    main()
