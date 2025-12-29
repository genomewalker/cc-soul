#!/usr/bin/env python3
"""
Test the session ledger system.

Tests:
1. Capture soul state
2. Save ledger (to cc-memory or local fallback)
3. Load latest ledger
4. Restore from ledger
5. Format ledger for context injection
"""

import sys
sys.path.insert(0, '/maps/projects/fernandezguerra/apps/repos/cc-soul/src')

from cc_soul import (
    save_ledger,
    load_latest_ledger,
    restore_from_ledger,
    format_ledger_for_context,
    capture_soul_state,
    capture_work_state,
)
from cc_soul.core import init_soul


def test_capture_soul_state():
    """Test capturing the soul's current state."""
    print("\n1. Testing capture_soul_state()...")
    init_soul()

    state = capture_soul_state()
    print(f"   Coherence: {state.coherence:.2%}")
    print(f"   Mood: {state.mood}")
    print(f"   Active intentions: {len(state.active_intentions)}")
    print(f"   Pending questions: {len(state.pending_questions)}")
    print(f"   Recent wisdom: {len(state.recent_wisdom)}")

    return state


def test_capture_work_state():
    """Test capturing current work context."""
    print("\n2. Testing capture_work_state()...")

    work = capture_work_state(
        todos=[{"content": "Test task", "status": "in_progress"}],
        files_touched=["ledger.py", "hooks.py"],
    )
    print(f"   Todos: {len(work.todos)}")
    print(f"   Files touched: {work.files_touched}")
    print(f"   Key decisions: {len(work.key_decisions)}")

    return work


def test_save_ledger():
    """Test saving a ledger."""
    print("\n3. Testing save_ledger()...")

    ledger = save_ledger(
        context_pct=0.75,
        todos=[{"content": "Implement ledger", "status": "completed"}],
        files_touched=["ledger.py", "hooks.py", "__init__.py"],
        immediate_next="Test the complete system",
        deferred=["Add more test cases", "Optimize performance"],
        critical_context="Testing the ledger save/restore cycle",
    )

    print(f"   Ledger ID: {ledger.ledger_id}")
    print(f"   Project: {ledger.project}")
    print(f"   Created: {ledger.created_at}")
    print(f"   Context %: {ledger.context_pct:.0%}")
    print(f"   Continuation: {ledger.continuation.immediate_next}")

    return ledger


def test_load_ledger():
    """Test loading the latest ledger."""
    print("\n4. Testing load_latest_ledger()...")

    ledger = load_latest_ledger()
    if ledger:
        print(f"   Found ledger: {ledger.ledger_id}")
        print(f"   From: {ledger.created_at}")
        print(f"   Context %: {ledger.context_pct:.0%}")
        return ledger
    else:
        print("   No ledger found (this is OK on first run)")
        return None


def test_restore_ledger(ledger):
    """Test restoring from a ledger."""
    print("\n5. Testing restore_from_ledger()...")

    if not ledger:
        print("   Skipped (no ledger to restore)")
        return

    result = restore_from_ledger(ledger)
    print(f"   Restored intentions: {result.get('intentions', 0)}")
    print(f"   Coherence: {result.get('coherence', 0):.2%}")
    print(f"   Continuation: {result.get('continuation', 'N/A')}")


def test_format_ledger(ledger):
    """Test formatting ledger for context injection."""
    print("\n6. Testing format_ledger_for_context()...")

    if not ledger:
        print("   Skipped (no ledger)")
        return

    context = format_ledger_for_context(ledger)
    print("   Formatted context:")
    for line in context.split('\n')[:10]:
        print(f"   {line}")


def main():
    print("=" * 60)
    print("Session Ledger Test Suite")
    print("=" * 60)

    # Run tests
    soul_state = test_capture_soul_state()
    work_state = test_capture_work_state()
    saved_ledger = test_save_ledger()
    loaded_ledger = test_load_ledger()
    test_restore_ledger(loaded_ledger or saved_ledger)
    test_format_ledger(loaded_ledger or saved_ledger)

    print("\n" + "=" * 60)
    print("All tests completed!")
    print("=" * 60)


if __name__ == "__main__":
    main()
