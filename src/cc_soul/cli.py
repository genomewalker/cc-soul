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


if __name__ == '__main__':
    main()
