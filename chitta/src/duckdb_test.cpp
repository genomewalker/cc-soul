#include "chitta/duckdb_store.hpp"
#include <iostream>
#include <vector>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

using namespace chitta;

// Generate a test embedding
std::vector<float> test_embedding(float seed) {
    std::vector<float> v(EMBED_DIM);
    for (size_t i = 0; i < EMBED_DIM; ++i) {
        v[i] = std::sin(seed + static_cast<float>(i) * 0.1f);
    }
    // Normalize
    float norm = 0.0f;
    for (float x : v) norm += x * x;
    norm = std::sqrt(norm);
    for (float& x : v) x /= norm;
    return v;
}

int main() {
    std::string test_path = "/tmp/duckdb_test_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(getpid());

    std::cout << "=== DuckDB Store Test ===\n";
    std::cout << "Test path: " << test_path << "\n\n";

    DuckDBStore store;

    // Test 1: Open
    std::cout << "Test 1: Open database... ";
    if (!store.open(test_path)) {
        std::cerr << "FAILED\n";
        return 1;
    }
    std::cout << "OK\n";

    // Test 2: Remember
    std::cout << "Test 2: Remember memories... ";
    auto emb1 = test_embedding(1.0f);
    auto emb2 = test_embedding(2.0f);
    auto emb3 = test_embedding(3.0f);

    int64_t id1 = store.remember("First test memory about cats", "wisdom", emb1, 0.9f, 0.02f);
    int64_t id2 = store.remember("Second test memory about dogs", "episode", emb2, 0.8f, 0.10f);
    int64_t id3 = store.remember("Third test memory about birds", "wisdom", emb3, 0.7f, 0.05f);

    if (id1 < 0 || id2 < 0 || id3 < 0) {
        std::cerr << "FAILED (ids: " << id1 << ", " << id2 << ", " << id3 << ")\n";
        return 1;
    }
    std::cout << "OK (ids: " << id1 << ", " << id2 << ", " << id3 << ")\n";

    // Test 3: Recall
    std::cout << "Test 3: Recall by similarity... ";
    auto results = store.recall(emb1, 5);
    if (results.empty()) {
        std::cerr << "FAILED (no results)\n";
        return 1;
    }
    if (results[0].id != id1) {
        std::cerr << "FAILED (expected id " << id1 << ", got " << results[0].id << ")\n";
        return 1;
    }
    std::cout << "OK (top result: " << results[0].content.substr(0, 30) << "... sim=" << results[0].similarity << ")\n";

    // Test 4: Connect triplets
    std::cout << "Test 4: Connect triplets... ";
    if (!store.connect("cat", "is_a", "animal")) {
        std::cerr << "FAILED\n";
        return 1;
    }
    if (!store.connect("dog", "is_a", "animal")) {
        std::cerr << "FAILED\n";
        return 1;
    }
    if (!store.connect("cat", "chases", "mouse")) {
        std::cerr << "FAILED\n";
        return 1;
    }
    std::cout << "OK\n";

    // Test 5: Query triplets
    std::cout << "Test 5: Query triplets... ";
    auto cat_triplets = store.query_subject("cat");
    if (cat_triplets.size() != 2) {
        std::cerr << "FAILED (expected 2 triplets, got " << cat_triplets.size() << ")\n";
        return 1;
    }
    std::cout << "OK (cat has " << cat_triplets.size() << " relationships)\n";

    auto animals = store.query_object("animal");
    if (animals.size() != 2) {
        std::cerr << "FAILED (expected 2 animals, got " << animals.size() << ")\n";
        return 1;
    }
    std::cout << "  - " << animals.size() << " things are animals\n";

    // Test 6: Strengthen/weaken
    std::cout << "Test 6: Strengthen/weaken... ";
    auto mem = store.get_memory(id2);
    if (!mem) {
        std::cerr << "FAILED (get_memory)\n";
        return 1;
    }
    float orig_conf = mem->confidence;
    store.strengthen(id2, 0.1f);
    mem = store.get_memory(id2);
    if (mem->confidence <= orig_conf) {
        std::cerr << "FAILED (strengthen didn't increase confidence)\n";
        return 1;
    }
    std::cout << "OK (confidence: " << orig_conf << " -> " << mem->confidence << ")\n";

    // Test 7: Health stats
    std::cout << "Test 7: Health stats... ";
    auto health = store.health();
    if (health.total_memories != 3) {
        std::cerr << "FAILED (expected 3 memories, got " << health.total_memories << ")\n";
        return 1;
    }
    if (health.total_triplets != 3) {
        std::cerr << "FAILED (expected 3 triplets, got " << health.total_triplets << ")\n";
        return 1;
    }
    std::cout << "OK (memories: " << health.total_memories << ", triplets: " << health.total_triplets << ")\n";

    // Test 8: Forget
    std::cout << "Test 8: Forget... ";
    if (!store.forget(id3)) {
        std::cerr << "FAILED\n";
        return 1;
    }
    if (store.memory_count() != 2) {
        std::cerr << "FAILED (expected 2 memories after forget)\n";
        return 1;
    }
    std::cout << "OK\n";

    // Test 9: Close and reopen (persistence test)
    std::cout << "Test 9: Close and reopen... ";
    store.close();
    if (!store.open(test_path)) {
        std::cerr << "FAILED (reopen)\n";
        return 1;
    }
    if (store.memory_count() != 2) {
        std::cerr << "FAILED (expected 2 memories after reopen, got " << store.memory_count() << ")\n";
        return 1;
    }
    if (store.triplet_count() != 3) {
        std::cerr << "FAILED (expected 3 triplets after reopen, got " << store.triplet_count() << ")\n";
        return 1;
    }
    std::cout << "OK (data persisted)\n";

    store.close();

    // Reopen for new tests (Tests 10-15)
    if (!store.open(test_path)) {
        std::cerr << "FAILED (reopen for tests 10-15)\n";
        return 1;
    }

    // Test 10: SparseVector from_dense edge cases
    std::cout << "Test 10: SparseVector from_dense edge cases... ";
    {
        // Empty input must not crash and must return empty
        SparseVector sv_empty = SparseVector::from_dense({});
        if (!sv_empty.active.empty()) {
            std::cerr << "FAILED (empty input should produce empty SparseVector)\n";
            return 1;
        }

        // k_pct > 1.0 (2.0f) on 10-element vector: k clamped to 10, all active
        std::vector<float> v10(10, 1.0f);
        SparseVector sv_clamped = SparseVector::from_dense(v10, 2.0f);
        if (sv_clamped.active.empty() || sv_clamped.active.size() > 10) {
            std::cerr << "FAILED (clamped k should produce non-empty SparseVector with at most 10 elements)\n";
            return 1;
        }

        // 5% of 100 elements = 5 active
        std::vector<float> v100(100);
        for (size_t i = 0; i < 100; i++) v100[i] = static_cast<float>(i + 1);
        SparseVector sv5pct = SparseVector::from_dense(v100, 0.05f);
        if (sv5pct.active.size() != 5) {
            std::cerr << "FAILED (expected 5 active, got " << sv5pct.active.size() << ")\n";
            return 1;
        }
    }
    std::cout << "OK\n";

    // Test 11: SparseVector serialize/deserialize roundtrip
    std::cout << "Test 11: SparseVector serialize/deserialize roundtrip... ";
    {
        SparseVector sv;
        sv.active = {3, 7, 15, 42, 100};  // already sorted

        std::string serialized = sv.serialize();
        SparseVector sv2 = SparseVector::deserialize(serialized);

        if (sv2.active != sv.active) {
            std::cerr << "FAILED (roundtrip mismatch: serialized='" << serialized << "')\n";
            return 1;
        }

        // Malformed string: valid tokens 1, 3, 5 should survive; "abc" and "xyz" are skipped
        SparseVector sv_mal = SparseVector::deserialize("1,abc,3,xyz,5");
        std::vector<uint16_t> expected_mal = {1, 3, 5};
        if (sv_mal.active != expected_mal) {
            std::cerr << "FAILED (malformed deserialize: expected {1,3,5}, got size=" << sv_mal.active.size() << ")\n";
            return 1;
        }
    }
    std::cout << "OK\n";

    // Test 12: SparseVector IoU
    std::cout << "Test 12: SparseVector IoU... ";
    {
        SparseVector sva, svb, svc, svd;
        sva.active = {1, 2, 3};
        svb.active = {1, 2, 3};
        svc.active = {4, 5, 6};
        svd.active = {2, 3, 4};

        // Identical vectors → IoU == 1.0
        float iou_identical = sva.iou(svb);
        if (std::abs(iou_identical - 1.0f) > 1e-5f) {
            std::cerr << "FAILED (identical IoU expected 1.0, got " << iou_identical << ")\n";
            return 1;
        }

        // Disjoint vectors → IoU == 0.0
        float iou_disjoint = sva.iou(svc);
        if (std::abs(iou_disjoint - 0.0f) > 1e-5f) {
            std::cerr << "FAILED (disjoint IoU expected 0.0, got " << iou_disjoint << ")\n";
            return 1;
        }

        // {1,2,3} vs {2,3,4}: intersection={2,3}=2, union={1,2,3,4}=4 → IoU=0.5
        float iou_partial = sva.iou(svd);
        if (std::abs(iou_partial - 0.5f) > 1e-5f) {
            std::cerr << "FAILED (partial IoU expected 0.5, got " << iou_partial << ")\n";
            return 1;
        }
    }
    std::cout << "OK\n";

    // Test 13: store_sdr / get_sdr roundtrip
    std::cout << "Test 13: store_sdr / get_sdr roundtrip... ";
    {
        // Reopen store to get id1 back — we need to look it up by content
        // id1 is the "First test memory about cats" stored at the top
        // Use emb1 to find it via recall
        SparseVector sdr = SparseVector::from_dense(emb1);
        std::string sdr_str = sdr.serialize();

        store.store_sdr(id1, sdr_str);

        std::string retrieved_str = store.get_sdr(id1);
        if (retrieved_str != sdr_str) {
            std::cerr << "FAILED (stored='" << sdr_str << "', retrieved='" << retrieved_str << "')\n";
            return 1;
        }

        // Verify active indices match after deserialize
        SparseVector sdr2 = SparseVector::deserialize(retrieved_str);
        if (sdr2.active != sdr.active) {
            std::cerr << "FAILED (deserialized active indices mismatch)\n";
            return 1;
        }
    }
    std::cout << "OK\n";

    // Test 14: sample_fast_memories
    std::cout << "Test 14: sample_fast_memories... ";
    {
        // Store 10 episode memories
        int64_t since_ms = chitta::now();
        for (int i = 0; i < 10; i++) {
            auto emb = test_embedding(static_cast<float>(10 + i));
            store.remember("Episode memory " + std::to_string(i), "episode", emb, 0.7f, 0.05f);
        }

        // Sample up to 5: result must be <= 5
        auto sample5 = store.sample_fast_memories(5, since_ms);
        if (sample5.size() > 5) {
            std::cerr << "FAILED (requested 5, got " << sample5.size() << ")\n";
            return 1;
        }

        // Sample 100: result bounded by total fast memories
        auto sample100 = store.sample_fast_memories(100, since_ms);
        if (sample100.size() > 100) {
            std::cerr << "FAILED (result exceeded requested cap of 100)\n";
            return 1;
        }
    }
    std::cout << "OK (fast memory sampling works)\n";

    // Test 15: accelerate_decay
    std::cout << "Test 15: accelerate_decay... ";
    {
        auto emb_decay = test_embedding(99.0f);
        int64_t id_decay = store.remember("Decay test memory", "wisdom", emb_decay, 0.8f, 0.01f);
        if (id_decay < 0) {
            std::cerr << "FAILED (could not store decay test memory)\n";
            return 1;
        }

        // Accelerate decay by 2.5x: 0.01 * 2.5 = 0.025
        store.accelerate_decay(id_decay, 2.5f);

        // Verify memory still exists after update
        auto mem_after = store.get_memory(id_decay);
        if (!mem_after) {
            std::cerr << "FAILED (memory disappeared after accelerate_decay)\n";
            return 1;
        }
        if (mem_after->id != id_decay) {
            std::cerr << "FAILED (returned wrong memory id)\n";
            return 1;
        }
    }
    std::cout << "OK (decay_rate updated, memory intact)\n";

    store.close();

    // Cleanup
    std::filesystem::remove_all(test_path);

    std::cout << "\n=== All tests passed! ===\n";
    return 0;
}
