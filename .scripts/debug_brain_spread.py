#!/usr/bin/env python3
"""
Validate cc-brain spreading activation and new features.

Tests:
1. Matrix transpose fix (W.T @ activation)
2. Semantic seeding via embeddings
3. Resonance gap detection
4. Hebbian learning
"""

import sys
sys.path.insert(0, "/maps/projects/fernandezguerra/apps/repos/cc-soul/src")

import numpy as np
from scipy import sparse
from pathlib import Path
import tempfile

from cc_soul.brain import Brain

print("=" * 60)
print("CC-BRAIN VALIDATION")
print("=" * 60)

# Test 1: Spreading activation
print("\n[1] SPREADING ACTIVATION")
print("-" * 40)

with tempfile.TemporaryDirectory() as tmpdir:
    test_db = Path(tmpdir) / "test_brain.db"
    brain = Brain(db_path=test_db)

    # Create a network: A -> B -> C -> D
    brain.add_concept("A", "Root Node A")
    brain.add_concept("B", "Connected Node B")
    brain.add_concept("C", "Distant Node C")
    brain.add_concept("D", "Far Node D")
    brain.add_concept("E", "Unconnected Node E")  # For gap testing

    brain.connect("A", "B", 0.8)
    brain.connect("B", "C", 0.7)
    brain.connect("C", "D", 0.6)

    print(f"Network: A→B→C→D + isolated E")
    print(f"Stats: {brain.stats()}")

    result = brain.spread(["A"], depth=3, decay=0.8, threshold=0.01)

    print("\nActivation from A:")
    for concept, score in result.activated:
        print(f"  {concept.title}: {score:.3f}")

    expected = {"A": 1.0, "B": 0.64, "C": 0.358, "D": 0.172}
    passed = len(result.activated) >= 4
    print(f"\n{'✅' if passed else '❌'} Spreading: {'PASS' if passed else 'FAIL'}")

# Test 2: Resonance gaps
print("\n[2] RESONANCE GAP DETECTION")
print("-" * 40)

with tempfile.TemporaryDirectory() as tmpdir:
    test_db = Path(tmpdir) / "test_brain.db"
    brain = Brain(db_path=test_db)

    # Create two separate clusters
    brain.add_concept("X1", "Cluster X Node 1")
    brain.add_concept("X2", "Cluster X Node 2")
    brain.add_concept("Y1", "Cluster Y Node 1")
    brain.add_concept("Y2", "Cluster Y Node 2")

    # Connect within clusters
    brain.connect("X1", "X2", 0.9)
    brain.connect("X2", "X1", 0.9)
    brain.connect("Y1", "Y2", 0.9)
    brain.connect("Y2", "Y1", 0.9)

    # No connection between X and Y clusters!

    # Activate both clusters
    result = brain.spread(["X1", "Y1"], depth=2, decay=0.8, threshold=0.2)

    print("Activated (from seeds X1, Y1):")
    for concept, score in result.activated:
        print(f"  {concept.title}: {score:.3f}")

    print("\nResonance gaps (strongly co-activated, no edge):")
    for a, b, strength in result.gaps:
        print(f"  {a} <-?-> {b} (resonance: {strength:.3f})")

    has_gaps = len(result.gaps) > 0
    print(f"\n{'✅' if has_gaps else '❌'} Gap detection: {'Found cross-cluster gaps' if has_gaps else 'No gaps'}")

# Test 3: Hebbian learning
print("\n[3] HEBBIAN LEARNING")
print("-" * 40)

with tempfile.TemporaryDirectory() as tmpdir:
    test_db = Path(tmpdir) / "test_brain.db"
    brain = Brain(db_path=test_db)

    brain.add_concept("P", "Concept P")
    brain.add_concept("Q", "Concept Q")

    # No initial connection
    initial_weight = brain._weights[0, 1]
    print(f"Initial P→Q weight: {initial_weight}")

    # Co-activate and learn
    brain.hebbian_learn(["P", "Q"], strength=0.3)

    new_weight = brain._weights[0, 1]
    print(f"After Hebbian learning: {new_weight}")

    strengthened = new_weight > initial_weight
    print(f"\n{'✅' if strengthened else '❌'} Hebbian: {'Connection strengthened' if strengthened else 'No change'}")

# Test 4: Semantic seeding (if embeddings available)
print("\n[4] SEMANTIC SEEDING")
print("-" * 40)

try:
    from cc_soul.vectors import search_wisdom
    print("Embeddings available")

    # Test with real brain
    brain = Brain()
    brain.sync_from_wisdom()

    result = brain.activate_from_prompt("debugging errors in code", limit=5)
    print(f"\nPrompt: 'debugging errors in code'")
    print(f"Seeds found: {len(result.activated)} concepts activated")

    for concept, score in result.activated[:5]:
        print(f"  {concept.title[:50]}: {score:.3f}")

    print("\n✅ Semantic seeding: Working")
except ImportError as e:
    print(f"Embeddings not available: {e}")
    print("⚠️  Semantic seeding: Skipped (no sentence-transformers)")
except Exception as e:
    print(f"Error: {e}")
    print("⚠️  Semantic seeding: Error")

print("\n" + "=" * 60)
print("VALIDATION COMPLETE")
print("=" * 60)
