// Minimal smoke test for FieldStore C++ wrapper.
// Compiled and linked only when CHITTA_FIELD_AVAILABLE is defined.
#ifdef CHITTA_FIELD_AVAILABLE

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include "chitta/field_store.hpp"

static void test_field_store_basic() {
    chitta::FieldStore store("/tmp/chitta_field_test_data", "/tmp/chitta_field_test_lock");

    std::vector<float> emb(768, 0.1f);
    auto id = store.remember("wisdom", "test", "hello from field store", emb, 0.9f, 0.001f, 0);
    assert(id > 0);

    auto hits = store.recall(emb, 5, "test");
    assert(!hits.empty());
    assert(hits[0].memory_id == id);
    assert(hits[0].content == "hello from field store");

    store.strengthen(id, 0.1f);
    store.touch(id);

    auto temporal = store.recall_temporal(0, INT64_MAX, 10, "test");
    assert(!temporal.empty());

    store.forget(id);
    auto hits2 = store.recall(emb, 5, "test");
    assert(hits2.empty());

    printf("  FieldStore basic: PASS\n");
}

void run_field_store_tests() {
    printf("FieldStore tests:\n");
    test_field_store_basic();
    printf("All FieldStore tests passed.\n");
}

#endif // CHITTA_FIELD_AVAILABLE
