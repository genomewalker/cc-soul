#!/usr/bin/env python3
"""
Validate brain-like minimal startup context token count.

Expected: ~400 tokens (97% reduction from ~16k)
"""
import sys
sys.path.insert(0, '/maps/projects/fernandezguerra/apps/repos/cc-soul/src')

from cc_soul.hooks import (
    format_minimal_startup_context,
    format_rich_context,
    session_start,
    get_project_name,
)
from cc_soul.bridge import unified_context


def estimate_tokens(text: str) -> int:
    """Rough token estimate: ~4 chars per token."""
    return len(text) // 4


def main():
    project = get_project_name()
    ctx = unified_context()

    print("=" * 60)
    print("Validating Brain-like Minimal Startup Context")
    print("=" * 60)

    # Get minimal context
    minimal = format_minimal_startup_context(project, ctx)
    minimal_tokens = estimate_tokens(minimal)

    print(f"\n## Minimal Startup Context ({minimal_tokens} tokens)")
    print("-" * 40)
    print(minimal)
    print("-" * 40)

    # Get rich context for comparison
    rich = format_rich_context(project, ctx)
    rich_tokens = estimate_tokens(rich)

    print(f"\n## Rich Context for comparison ({rich_tokens} tokens)")
    print("-" * 40)
    print(rich[:500] + "..." if len(rich) > 500 else rich)
    print("-" * 40)

    # Get full session_start output (include_rich=True uses minimal now)
    greeting = session_start(include_rich=True)
    greeting_tokens = estimate_tokens(greeting)

    print(f"\n## Full session_start(include_rich=True) ({greeting_tokens} tokens)")
    print("-" * 40)
    print(greeting)
    print("-" * 40)

    # Summary
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"Minimal context:     ~{minimal_tokens} tokens")
    print(f"Rich context:        ~{rich_tokens} tokens")
    print(f"Full greeting:       ~{greeting_tokens} tokens")
    print(f"Target:              ~400 tokens")

    if minimal_tokens <= 500:
        print("\n✅ SUCCESS: Minimal context is within target (~400 tokens)")
        reduction = (1 - (minimal_tokens / max(rich_tokens, 1))) * 100
        print(f"   Reduction: {reduction:.0f}% vs rich context")
    else:
        print(f"\n❌ FAIL: Minimal context ({minimal_tokens}) exceeds target (400)")

    return 0 if minimal_tokens <= 500 else 1


if __name__ == "__main__":
    sys.exit(main())
