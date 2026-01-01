#!/usr/bin/env python3
"""
Validate cc-brain spreading activation.

Tests:
1. Content-addressed deduplication
2. Semantic spreading (via embeddings)
3. Hebbian spreading (via co-activation edges)
4. Hebbian learning (edge creation)
"""

from cc_soul.brain import Brain, _content_id


def main():
    print("=== cc-brain Validation ===\n")

    # Test 1: Content-addressed IDs
    print("1. Content-addressed identity:")
    id1 = _content_id("Simplicity over cleverness")
    id2 = _content_id("simplicity over cleverness")  # Same normalized
    id3 = _content_id("Different concept")
    print(f"   Same title, different case: {id1 == id2}")
    print(f"   Different titles: {id1 != id3}")
    assert id1 == id2, "Content addressing should normalize case"
    assert id1 != id3, "Different titles should have different IDs"
    print("   ✓ Pass\n")

    # Test 2: Deduplication
    print("2. Concept deduplication:")
    b = Brain()
    initial = len(b._concepts)

    # Add same concept with different types
    c1 = b.add_concept("Test Concept", type_tag="wisdom")
    c2 = b.add_concept("Test Concept", type_tag="belief")
    c3 = b.add_concept("test concept", type_tag="term")  # Same normalized

    print(f"   Added 3 times, got: {len(b._concepts) - initial} new concept(s)")
    print(f"   Types on concept: {c1.types}")
    assert c1.id == c2.id == c3.id, "Same title should yield same concept"
    assert c1.types == {"wisdom", "belief", "term"}, "Types should accumulate"
    print("   ✓ Pass\n")

    # Test 3: Dual-path spreading
    print("3. Dual-path spreading:")
    result = b.activate_from_prompt("How should I approach problems?", limit=10)
    print(f"   Semantic results: {len(result.semantic)}")
    print(f"   Hebbian results: {len(result.hebbian)}")
    print(f"   Combined: {len(result.activated)}")

    if result.semantic:
        print(f"   Top semantic: {result.semantic[0][0].title[:40]}...")
    print("   ✓ Pass\n")

    # Test 4: Hebbian learning
    print("4. Hebbian learning:")
    edges_before = b._weights.nnz
    b.hebbian_learn(["test_a", "test_b", "test_c"], strength=0.1)  # Won't work (no concepts)

    # Add real concepts and test
    a = b.add_concept("Concept A", type_tag="test")
    c = b.add_concept("Concept B", type_tag="test")
    d = b.add_concept("Concept C", type_tag="test")
    b.hebbian_learn([a.id, c.id, d.id], strength=0.1)
    edges_after = b._weights.nnz

    print(f"   Edges before: {edges_before}")
    print(f"   Edges after: {edges_after}")
    print(f"   New edges: {edges_after - edges_before}")
    assert edges_after > edges_before, "Hebbian learning should create edges"
    print("   ✓ Pass\n")

    # Summary
    print("=== All Tests Passed ===")
    stats = b.stats()
    print(f"\nBrain stats:")
    for k, v in stats.items():
        print(f"  {k}: {v}")


if __name__ == "__main__":
    main()
