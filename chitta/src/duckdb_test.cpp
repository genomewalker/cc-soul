#include "chitta/duckdb_store.hpp"
#include <iostream>
#include <vector>
#include <cassert>
#include <cstdlib>
#include <filesystem>

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
    std::string test_path = "/tmp/duckdb_test_" + std::to_string(std::time(nullptr));

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

    // Cleanup
    std::filesystem::remove_all(test_path);

    std::cout << "\n=== All tests passed! ===\n";
    return 0;
}
