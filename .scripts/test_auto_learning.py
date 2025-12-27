#!/usr/bin/env python3
"""
Test organic learning from session fragments.

Philosophy:
- Save significant text fragments (raw)
- Claude's understanding provides meaning at read time
- No Python pattern matching for structured extraction
"""

import sys
sys.path.insert(0, '/maps/projects/fernandezguerra/apps/repos/cc-soul/src')

from cc_soul.neural import (
    save_fragment,
    get_session_fragments,
    clear_session_fragments,
    summarize_session_work,
    auto_learn_from_output,
    detect_breakthrough,
    detect_tension,
)


def test_fragment_saving():
    """Test that fragments are saved and retrieved."""
    print("=== Testing Fragment Memory ===\n")

    clear_session_fragments()
    print("Cleared session fragments")

    # Simulate assistant outputs
    outputs = [
        "I refactored the greeting system to provide memory context instead of pre-written text",
        "The key insight is that synthesis should happen at read time, not write time",
        "There's still an open question about how to capture emotional continuity across sessions",
    ]

    print("\nSaving fragments from outputs...")
    for output in outputs:
        auto_learn_from_output(output)
        print(f"  Saved: {output[:50]}...")

    fragments = get_session_fragments()
    print(f"\nRetrieved {len(fragments)} fragments:")
    for f in fragments:
        print(f"  → {f[:60]}...")

    summary = summarize_session_work()
    print(f"\nSummary (for next session): {summary[:100]}...")
    print()


def test_breakthrough_detection():
    """Test breakthrough pattern detection (still pattern-based for triggers)."""
    print("=== Testing Breakthrough Detection ===\n")

    test_cases = [
        "I see now that the threshold was filtering too early",
        "The root cause was the threshold gating",
        "This is a normal sentence without breakthroughs",
    ]

    for text in test_cases:
        result = detect_breakthrough(text)
        if result:
            print(f"  BREAKTHROUGH: {result['insight'][:50]}... ({result['domain']})")
        else:
            print(f"  [no breakthrough]: {text[:40]}...")
    print()


def test_tension_detection():
    """Test tension detection for growth vectors."""
    print("=== Testing Tension Detection ===\n")

    test_cases = [
        "The question remains how to balance organic vs structured",
        "Worth investigating whether continuous activation scales",
        "This is a normal statement",
    ]

    for text in test_cases:
        result = detect_tension(text)
        if result:
            print(f"  TENSION: {result['tension'][:50]}... ({result['domain']})")
        else:
            print(f"  [no tension]: {text[:40]}...")
    print()


def test_philosophy():
    """Demonstrate the philosophy in action."""
    print("=== Philosophy Demonstration ===\n")

    print("OLD approach (pattern matching):")
    print("  Input: 'I implemented the threshold refactor'")
    print("  Python extracts: action='implemented', what='threshold refactor'")
    print("  Problem: Python does the interpretation, not Claude")
    print()

    print("NEW approach (fragment memory):")
    print("  Input: 'I implemented the threshold refactor'")
    print("  Soul saves: 'I implemented the threshold refactor' (raw)")
    print("  Next session: Claude reads fragment, interprets naturally")
    print("  Correct: Claude does the interpretation, not Python")
    print()


if __name__ == "__main__":
    test_philosophy()
    test_fragment_saving()
    test_breakthrough_detection()
    test_tension_detection()
    print("All tests completed!")
