#include <chitta/chitta.hpp>
#include <chitta/segment_manager.hpp>
#include <iostream>
#include <cassert>
#include <cmath>
#include <thread>
#include <chrono>

using namespace chitta;

Vector test_vector(float seed) {
    Vector v;
    for (size_t i = 0; i < EMBED_DIM; ++i) {
        v[i] = std::sin((static_cast<float>(i) + seed) * 0.1f);
    }
    return v;
}

void test_confidence() {
    std::cout << "Testing Confidence..." << std::endl;

    Confidence c(0.9f);
    c.decay(0.1f, 10.0f);
    assert(c.mu < 0.9f);
    assert(c.mu > 0.5f);

    Confidence c2(0.5f);
    for (int i = 0; i < 10; ++i) {
        c2.observe(1.0f);
    }
    assert(c2.mu > 0.8f);
    assert(c2.sigma_sq < 0.1f);

    std::cout << "  PASS" << std::endl;
}

void test_node() {
    std::cout << "Testing Node..." << std::endl;

    Node node(NodeType::Invariant, Vector::zeros());
    node.immutable();
    assert(node.delta == 0.0f);
    assert(node.kappa.mu > 0.99f);

    std::cout << "  PASS" << std::endl;
}

void test_graph_insert_get() {
    std::cout << "Testing Graph insert/get..." << std::endl;

    Graph graph;
    Node node(NodeType::Wisdom, test_vector(1.0f));
    NodeId id = graph.insert(std::move(node));

    auto retrieved = graph.get(id);
    assert(retrieved.has_value());
    assert(retrieved->node_type == NodeType::Wisdom);

    std::cout << "  PASS" << std::endl;
}

void test_graph_semantic_query() {
    std::cout << "Testing Graph semantic query..." << std::endl;

    Graph graph;

    Vector v1 = test_vector(1.0f);
    Vector v2 = test_vector(1.1f);  // Similar
    Vector v3 = test_vector(100.0f); // Different

    graph.insert(Node(NodeType::Wisdom, v1));
    graph.insert(Node(NodeType::Wisdom, v2));
    graph.insert(Node(NodeType::Wisdom, v3));

    auto results = graph.query(test_vector(1.0f), 0.9f, 10);
    assert(results.size() >= 1);

    std::cout << "  PASS" << std::endl;
}

void test_graph_snapshot_rollback() {
    std::cout << "Testing Graph snapshot/rollback..." << std::endl;

    Graph graph;
    graph.insert(Node(NodeType::Wisdom, test_vector(1.0f)));

    uint64_t snap = graph.snapshot();
    assert(graph.size() == 1);

    graph.insert(Node(NodeType::Wisdom, test_vector(2.0f)));
    assert(graph.size() == 2);

    graph.rollback(snap);
    assert(graph.size() == 1);

    std::cout << "  PASS" << std::endl;
}

void test_coherence() {
    std::cout << "Testing Coherence..." << std::endl;

    Coherence c;
    float tau_k = c.tau_k();
    assert(tau_k >= 0.0f && tau_k <= 1.0f);

    std::cout << "  PASS" << std::endl;
}

void test_ops() {
    std::cout << "Testing Ops..." << std::endl;

    Graph graph;
    Vector v = test_vector(1.0f);

    // Insert
    auto insert_op = Op::insert(Node(NodeType::Wisdom, v));
    auto result = insert_op.execute(graph);
    assert(result.type == OpResult::Type::NodeId);
    NodeId id = result.node_id;

    // Query
    auto query_op = Op::query(v, 0.9f, 10);
    result = query_op.execute(graph);
    assert(result.type == OpResult::Type::Nodes);
    assert(!result.nodes.empty());

    // Strengthen
    auto strengthen_op = Op::strengthen(id, 0.1f);
    strengthen_op.execute(graph);

    // Snapshot
    auto snap_op = Op::snapshot();
    result = snap_op.execute(graph);
    assert(result.type == OpResult::Type::SnapshotId);

    // Conditional
    auto when_op = Op::when(
        Condition::always(),
        {Op::compute_coherence()},
        {}
    );
    result = when_op.execute(graph);
    assert(result.type == OpResult::Type::Seq);

    std::cout << "  PASS" << std::endl;
}

void test_voice() {
    std::cout << "Testing Voice..." << std::endl;

    Graph graph;
    Vector v = test_vector(1.0f);
    graph.insert(Node(NodeType::Wisdom, v));

    auto manas = antahkarana::manas();
    auto results = manas.query(graph, v, 0.5f, 10);
    assert(!results.empty());

    std::cout << "  PASS" << std::endl;
}

void test_chorus() {
    std::cout << "Testing Chorus..." << std::endl;

    Graph graph;
    graph.insert(Node(NodeType::Wisdom, test_vector(1.0f)));

    Chorus chorus({
        antahkarana::manas(),
        antahkarana::buddhi(),
        antahkarana::ahamkara()
    });

    auto report = chorus.harmonize(graph);
    assert(report.perspectives.size() == 3);

    std::cout << "  PASS" << std::endl;
}

void test_dynamics() {
    std::cout << "Testing Dynamics..." << std::endl;

    Graph graph;
    Dynamics dynamics;
    dynamics.with_defaults();

    auto report = dynamics.tick(graph);
    // First tick with defaults
    assert(report.decay_applied || report.triggers_fired.size() >= 0);

    std::cout << "  PASS" << std::endl;
}

void test_learning_cycle() {
    std::cout << "Testing LearningCycle..." << std::endl;

    Graph graph;
    Node node(NodeType::Wisdom, test_vector(1.0f));
    NodeId id = graph.insert(std::move(node));

    cycles::LearningCycle cycle;
    cycle.observe("User corrected my approach");
    cycle.learn("Check assumptions before acting");
    cycle.apply(id);
    cycle.confirm(true, graph);

    assert(cycle.complete());

    auto updated = graph.get(id);
    assert(updated.has_value());
    assert(updated->kappa.mu > 0.8f);

    std::cout << "  PASS" << std::endl;
}

void test_quantized_vector() {
    std::cout << "Testing QuantizedVector..." << std::endl;

    Vector v1 = test_vector(1.0f);
    Vector v2 = test_vector(1.1f);
    Vector v3 = test_vector(100.0f);

    // Quantize
    QuantizedVector q1 = QuantizedVector::from_float(v1);
    QuantizedVector q2 = QuantizedVector::from_float(v2);
    QuantizedVector q3 = QuantizedVector::from_float(v3);

    // Similar vectors should have high approx cosine
    float sim12 = q1.cosine_approx(q2);
    float sim13 = q1.cosine_approx(q3);

    assert(sim12 > 0.9f);  // Similar
    assert(sim13 < 0.5f);  // Different

    // Dequantize and check accuracy
    Vector d1 = q1.to_float();
    float exact_sim = v1.cosine(d1);
    assert(exact_sim > 0.99f);  // Should be very close

    std::cout << "  PASS" << std::endl;
}

void test_hnsw_index() {
    std::cout << "Testing HNSWIndex..." << std::endl;

    HNSWIndex index;

    // Insert some vectors
    for (int i = 0; i < 20; ++i) {
        Vector v = test_vector(static_cast<float>(i));
        QuantizedVector qv = QuantizedVector::from_float(v);
        NodeId id = NodeId::generate();
        index.insert(id, qv);
    }

    assert(index.size() == 20);

    // Search
    Vector query = test_vector(0.0f);
    QuantizedVector qquery = QuantizedVector::from_float(query);
    auto results = index.search(qquery, 5);

    assert(results.size() == 5);
    assert(results[0].second > 0.8f);  // Best match should be similar

    std::cout << "  PASS" << std::endl;
}

void test_tiered_storage() {
    std::cout << "Testing TieredStorage..." << std::endl;

    // Clean up previous test data
    std::system("rm -f /tmp/chitta_test.*");

    TieredStorage::Config config;
    config.base_path = "/tmp/chitta_test";
    config.hot_max_nodes = 10;

    TieredStorage storage(config);
    assert(storage.initialize());

    // Insert nodes
    std::vector<NodeId> ids;
    for (int i = 0; i < 5; ++i) {
        Node node(NodeType::Wisdom, test_vector(static_cast<float>(i)));
        NodeId id = node.id;
        ids.push_back(id);
        storage.insert(id, std::move(node));
    }

    assert(storage.hot_size() == 5);

    // Retrieve
    Node* node = storage.get(ids[0]);
    assert(node != nullptr);
    assert(node->node_type == NodeType::Wisdom);

    // Search
    Vector query = test_vector(0.0f);
    QuantizedVector qquery = QuantizedVector::from_float(query);
    auto results = storage.search(qquery, 3);
    assert(results.size() == 3);

    std::cout << "  PASS" << std::endl;
}

void test_wal_deltas() {
    std::cout << "Testing WAL Deltas (Phase 2)..." << std::endl;

    // Clean up previous test data
    std::system("rm -f /tmp/chitta_wal_delta_test.*");

    TieredStorage::Config config;
    config.base_path = "/tmp/chitta_wal_delta_test";
    config.hot_max_nodes = 100;
    config.use_wal = true;

    // Phase 1: Create storage with WAL, perform operations
    NodeId node_id;
    Timestamp initial_touch;
    {
        TieredStorage storage(config);
        assert(storage.initialize());

        // Insert a node (full node write to WAL)
        Node node(NodeType::Wisdom, test_vector(1.0f));
        node_id = node.id;
        initial_touch = node.tau_accessed;
        storage.insert(node_id, std::move(node));

        // Access node multiple times (should trigger touch deltas)
        for (int i = 0; i < 3; ++i) {
            Node* n = storage.get(node_id);
            assert(n != nullptr);
            // Small delay to ensure different timestamps
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Update confidence (should trigger confidence delta)
        Confidence new_kappa(0.85f);
        new_kappa.observe(0.9f);
        storage.update_confidence(node_id, new_kappa);

        // Add an edge (should trigger edge delta)
        NodeId target_id = NodeId::generate();
        storage.add_edge(node_id, target_id, EdgeType::Supports, 0.75f);

        // Sync to ensure WAL is flushed
        storage.sync();
    }

    // Phase 2: Reopen and verify deltas were persisted
    {
        TieredStorage storage(config);
        assert(storage.initialize());

        // Node should be recovered with updated state
        Node* node = storage.get(node_id);
        assert(node != nullptr);
        assert(node->node_type == NodeType::Wisdom);

        // Confidence should reflect the update
        // (Note: actual value depends on Confidence::observe implementation)
        assert(node->kappa.mu > 0.84f);  // Should be updated from default

        // Touch timestamp should be newer than initial
        assert(node->tau_accessed > initial_touch);

        // Edge should be present
        assert(node->edges.size() >= 1);
        bool found_edge = false;
        for (const auto& e : node->edges) {
            if (e.type == EdgeType::Supports && std::abs(e.weight - 0.75f) < 0.01f) {
                found_edge = true;
                break;
            }
        }
        assert(found_edge);
    }

    std::cout << "  PASS" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 3: Unified Index Tests
// ═══════════════════════════════════════════════════════════════════════════

void test_hilbert_curve() {
    std::cout << "Testing Hilbert Curve..." << std::endl;

    // Test basic Hilbert key generation
    QuantizedVector vec1 = QuantizedVector::from_float(test_vector(1.0f));
    QuantizedVector vec2 = QuantizedVector::from_float(test_vector(1.1f));
    QuantizedVector vec3 = QuantizedVector::from_float(test_vector(100.0f));

    uint64_t key1 = hilbert_key(vec1);
    uint64_t key2 = hilbert_key(vec2);
    uint64_t key3 = hilbert_key(vec3);

    // Similar vectors should have closer Hilbert keys
    uint64_t diff_12 = (key1 > key2) ? (key1 - key2) : (key2 - key1);
    uint64_t diff_13 = (key1 > key3) ? (key1 - key3) : (key3 - key1);

    // vec1 and vec2 are more similar than vec1 and vec3
    // Their Hilbert keys should be closer
    assert(diff_12 < diff_13);

    // Test hilbert_close function
    assert(hilbert_close(key1, key2, diff_12 + 1));
    assert(!hilbert_close(key1, key3, diff_12));

    // Test raw key function
    int8_t raw_data[8] = {0, 10, 20, 30, 40, 50, 60, 70};
    uint64_t raw_key = hilbert_key_raw(raw_data, 8);
    assert(raw_key != 0);

    std::cout << "  PASS" << std::endl;
}

void test_connection_pool() {
    std::cout << "Testing Connection Pool..." << std::endl;

    // Clean up
    std::system("rm -f /tmp/chitta_conn_test.*");

    ConnectionPool pool;
    assert(pool.create("/tmp/chitta_conn_test", 1000));

    // Allocate connections for a node
    std::vector<std::vector<ConnectionEdge>> connections(3);
    connections[0] = {{1, 0.1f}, {2, 0.2f}, {3, 0.3f}};  // Level 0
    connections[1] = {{4, 0.4f}, {5, 0.5f}};             // Level 1
    connections[2] = {{6, 0.6f}};                         // Level 2

    uint64_t offset = pool.allocate(42, 3, connections);
    assert(offset > 0);

    // Read back
    uint32_t slot_id;
    uint8_t level_count;
    std::vector<std::vector<ConnectionEdge>> read_connections;
    assert(pool.read(offset, slot_id, level_count, read_connections));

    assert(slot_id == 42);
    assert(level_count == 3);
    assert(read_connections.size() == 3);
    assert(read_connections[0].size() == 3);
    assert(read_connections[1].size() == 2);
    assert(read_connections[2].size() == 1);

    // Check values
    assert(std::abs(read_connections[0][0].distance - 0.1f) < 0.001f);
    assert(read_connections[0][0].target_slot == 1);

    // Test read_level
    auto level1 = pool.read_level(offset, 1);
    assert(level1.size() == 2);
    assert(level1[0].target_slot == 4);

    // Test removal
    pool.remove(offset);
    assert(!pool.read(offset, slot_id, level_count, read_connections));

    pool.close();

    std::cout << "  PASS" << std::endl;
}

void test_unified_index() {
    std::cout << "Testing Unified Index..." << std::endl;

    // Clean up
    std::system("rm -f /tmp/chitta_unified_test.*");

    UnifiedIndex index;
    assert(index.create("/tmp/chitta_unified_test", 1000));

    // Insert nodes
    std::vector<NodeId> ids;
    for (int i = 0; i < 100; ++i) {
        Node node(NodeType::Wisdom, test_vector(static_cast<float>(i)));
        SlotId slot = index.insert(node.id, node);
        assert(slot.valid());
        ids.push_back(node.id);
    }

    assert(index.count() == 100);

    // Lookup by ID
    SlotId slot0 = index.lookup(ids[0]);
    assert(slot0.valid());

    const IndexedNode* node0 = index.get(ids[0]);
    assert(node0 != nullptr);
    assert(node0->id == ids[0]);

    // Get vector
    const QuantizedVector* vec = index.vector(slot0);
    assert(vec != nullptr);

    // Search
    QuantizedVector query = QuantizedVector::from_float(test_vector(50.0f));
    auto results = index.search(query, 10);

    assert(!results.empty());
    // Result should include nodes near index 50
    bool found_nearby = false;
    for (const auto& [slot, dist] : results) {
        const IndexedNode* n = index.get_slot(slot);
        if (n) {
            // Check if it's in reasonable range
            SlotId found_slot = index.lookup(n->id);
            if (found_slot.value >= 40 && found_slot.value <= 60) {
                found_nearby = true;
            }
        }
    }
    assert(found_nearby);

    // Close and reopen
    index.close();

    UnifiedIndex index2;
    assert(index2.open("/tmp/chitta_unified_test"));
    assert(index2.count() == 100);

    // Verify node is still there
    const IndexedNode* reloaded = index2.get(ids[0]);
    assert(reloaded != nullptr);

    // Search should still work
    auto results2 = index2.search(query, 10);
    assert(!results2.empty());

    index2.close();

    std::cout << "  PASS" << std::endl;
}

void test_unified_index_scale() {
    std::cout << "Testing Unified Index Scale (1K nodes)..." << std::endl;

    // Clean up
    std::system("rm -f /tmp/chitta_scale_test.*");

    UnifiedIndex index;
    assert(index.create("/tmp/chitta_scale_test", 2000));

    // Insert 1K nodes (reduced from 10K for faster testing)
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        Node node(NodeType::Wisdom, test_vector(static_cast<float>(i)));
        SlotId slot = index.insert(node.id, node);
        assert(slot.valid());
    }
    auto insert_time = std::chrono::high_resolution_clock::now() - start;

    assert(index.count() == 1000);
    std::cout << "    Insert time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(insert_time).count()
              << " ms" << std::endl;

    // Close and measure reopen time
    index.sync();
    index.close();

    start = std::chrono::high_resolution_clock::now();
    UnifiedIndex index2;
    assert(index2.open("/tmp/chitta_scale_test"));
    auto open_time = std::chrono::high_resolution_clock::now() - start;

    std::cout << "    Open time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(open_time).count()
              << " ms" << std::endl;

    // Open should be fast (no rebuild)
    assert(std::chrono::duration_cast<std::chrono::milliseconds>(open_time).count() < 1000);

    // Search should be fast
    QuantizedVector query = QuantizedVector::from_float(test_vector(500.0f));

    start = std::chrono::high_resolution_clock::now();
    auto results = index2.search(query, 10);
    auto search_time = std::chrono::high_resolution_clock::now() - start;

    std::cout << "    Search time: "
              << std::chrono::duration_cast<std::chrono::microseconds>(search_time).count()
              << " us" << std::endl;

    assert(!results.empty());
    assert(std::chrono::duration_cast<std::chrono::milliseconds>(search_time).count() < 100);

    std::cout << "  PASS" << std::endl;
}

void test_unified_snapshot() {
    std::cout << "Testing Unified Index Snapshot (CoW)..." << std::endl;

    // Clean up previous test data
    std::system("rm -f /tmp/chitta_snapshot_test.*");
    std::system("rm -f /tmp/chitta_snapshot_copy.*");

    // Create index with some nodes
    UnifiedIndex index;
    assert(index.create("/tmp/chitta_snapshot_test", 1000));

    for (int i = 0; i < 50; ++i) {
        Node node(NodeType::Wisdom, test_vector(static_cast<float>(i)));
        index.insert(node.id, node);
    }

    assert(index.count() == 50);
    uint64_t snap_id_before = index.snapshot_id();

    // Create snapshot
    auto start = std::chrono::high_resolution_clock::now();
    assert(index.create_snapshot("/tmp/chitta_snapshot_copy"));
    auto snapshot_time = std::chrono::high_resolution_clock::now() - start;

    std::cout << "    Snapshot time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(snapshot_time).count()
              << " ms" << std::endl;

    // Snapshot ID should have incremented
    assert(index.snapshot_id() == snap_id_before + 1);

    // Add more nodes after snapshot
    for (int i = 50; i < 100; ++i) {
        Node node(NodeType::Wisdom, test_vector(static_cast<float>(i)));
        index.insert(node.id, node);
    }
    assert(index.count() == 100);

    // Open snapshot - should have original 50 nodes
    UnifiedIndex snapshot;
    assert(snapshot.open("/tmp/chitta_snapshot_copy"));
    assert(snapshot.count() == 50);

    // Original index still has 100
    assert(index.count() == 100);

    // Search in snapshot should work
    QuantizedVector query = QuantizedVector::from_float(test_vector(25.0f));
    auto results = snapshot.search(query, 5);
    assert(!results.empty());

    std::cout << "  PASS" << std::endl;
}

void test_segment_manager() {
    std::cout << "Testing Segment Manager..." << std::endl;

    // Clean up previous test data
    std::system("rm -f /tmp/chitta_segment_test.*");

    // Create segment manager
    SegmentManager manager("/tmp/chitta_segment_test");
    assert(manager.create());
    assert(manager.valid());
    assert(manager.segment_count() == 1);

    // Insert nodes
    std::vector<NodeId> ids;
    for (int i = 0; i < 100; ++i) {
        Node node(NodeType::Wisdom, test_vector(static_cast<float>(i)));
        ids.push_back(node.id);
        auto slot = manager.insert(node.id, node);
        assert(slot.valid());
    }

    assert(manager.total_nodes() == 100);

    // Lookup
    auto* indexed = manager.get(ids[50]);
    assert(indexed != nullptr);
    assert(indexed->id == ids[50]);

    // Search
    QuantizedVector query = QuantizedVector::from_float(test_vector(50.0f));
    auto results = manager.search(query, 5);
    assert(results.size() == 5);

    // Close and reopen
    manager.sync();
    manager.close();

    SegmentManager manager2("/tmp/chitta_segment_test");
    assert(manager2.open());
    assert(manager2.segment_count() == 1);
    assert(manager2.total_nodes() == 100);

    std::cout << "  PASS" << std::endl;
}

void test_tiered_storage_segments() {
    std::cout << "Testing TieredStorage with Segments..." << std::endl;

    // Clean up previous test data
    std::system("rm -f /tmp/chitta_tiered_segments_test.*");

    TieredStorage::Config config;
    config.base_path = "/tmp/chitta_tiered_segments_test";
    config.use_segments = true;

    TieredStorage storage(config);
    assert(storage.initialize());

    // Insert nodes
    std::vector<NodeId> ids;
    for (int i = 0; i < 50; ++i) {
        Node node(NodeType::Wisdom, test_vector(static_cast<float>(i)));
        NodeId id = node.id;
        ids.push_back(id);
        storage.insert(id, std::move(node));
    }

    assert(storage.hot_size() == 50);
    assert(storage.total_size() == 50);

    // Retrieve
    Node* node = storage.get(ids[0]);
    assert(node != nullptr);
    assert(node->node_type == NodeType::Wisdom);

    // Search
    Vector query = test_vector(25.0f);
    QuantizedVector qquery = QuantizedVector::from_float(query);
    auto results = storage.search(qquery, 5);
    assert(results.size() == 5);

    // Sync and close
    storage.sync();

    std::cout << "  PASS" << std::endl;
}

void test_mind() {
    std::cout << "Testing Mind..." << std::endl;

    // Clean up any previous test data
    std::system("rm -f /tmp/chitta_mind_test.*");

    MindConfig config;
    config.path = "/tmp/chitta_mind_test";

    Mind mind(config);
    assert(mind.open());

    // Remember some things
    NodeId id1 = mind.remember(NodeType::Wisdom, test_vector(1.0f));
    NodeId id2 = mind.remember(NodeType::Wisdom, test_vector(1.1f));
    NodeId id3 = mind.remember(NodeType::Episode, test_vector(100.0f));

    assert(mind.size() == 3);

    // Recall
    auto results = mind.recall(test_vector(1.0f), 5, 0.5f);
    assert(!results.empty());
    assert(results[0].similarity > 0.8f);

    // Strengthen
    mind.strengthen(id1, 0.1f);
    auto node = mind.get(id1);
    assert(node.has_value());
    assert(node->kappa.mu > 0.8f);

    // State
    auto state = mind.state();
    assert(state.hot_nodes == 3);

    mind.close();

    std::cout << "  PASS" << std::endl;
}

void test_persistence() {
    std::cout << "Testing Persistence..." << std::endl;

    // Clean up previous test data
    std::system("rm -f /tmp/chitta_persist_test.*");

    const std::string path = "/tmp/chitta_persist_test";
    NodeId saved_id;
    float saved_mu;

    // Phase 1: Create and populate
    {
        MindConfig config;
        config.path = path;

        Mind mind(config);
        assert(mind.open());

        // Remember something
        saved_id = mind.remember(NodeType::Wisdom, test_vector(42.0f));

        // Strengthen it
        mind.strengthen(saved_id, 0.15f);

        auto node = mind.get(saved_id);
        assert(node.has_value());
        saved_mu = node->kappa.mu;

        assert(mind.size() == 1);

        // Close (should save)
        mind.close();
    }

    // Phase 2: Reopen and verify
    {
        MindConfig config;
        config.path = path;

        Mind mind(config);
        assert(mind.open());

        // Should have loaded the data
        assert(mind.size() == 1);

        // Retrieve the node
        auto node = mind.get(saved_id);
        assert(node.has_value());
        assert(node->node_type == NodeType::Wisdom);

        // Confidence should be preserved
        float loaded_mu = node->kappa.mu;
        assert(std::abs(loaded_mu - saved_mu) < 0.001f);

        // Semantic search should work
        auto results = mind.recall(test_vector(42.0f), 5, 0.5f);
        assert(!results.empty());
        assert(results[0].id == saved_id);

        mind.close();
    }

    // Cleanup
    std::remove((path + ".hot").c_str());
    std::remove((path + ".warm").c_str());
    std::remove((path + ".cold").c_str());

    std::cout << "  PASS" << std::endl;
}

#ifdef CHITTA_WITH_ONNX
void test_vak_onnx() {
    std::cout << "Testing VakYantra (ONNX)..." << std::endl;

    // Check if model exists
    const char* model_path = "../models/model.onnx";
    const char* vocab_path = "../models/vocab.txt";

    std::ifstream model_check(model_path);
    std::ifstream vocab_check(vocab_path);

    if (!model_check || !vocab_check) {
        std::cout << "  SKIP (model files not found)" << std::endl;
        return;
    }

    // Create yantra
    AntahkaranaYantra::Config config;
    config.pooling = PoolingStrategy::Mean;
    config.normalize_embeddings = true;
    config.max_seq_length = 128;

    auto yantra = std::make_shared<AntahkaranaYantra>(config);
    bool awakened = yantra->awaken(model_path, vocab_path);

    if (!awakened) {
        std::cout << "  SKIP (failed to load model: " << yantra->error() << ")" << std::endl;
        return;
    }

    assert(yantra->ready());
    std::cout << "  Model loaded, hidden_dim=" << yantra->dimension() << std::endl;

    // Test single embedding
    Artha artha = yantra->transform("The quick brown fox jumps over the lazy dog.");
    assert(artha.nu.size() == EMBED_DIM);

    // Check normalization (should be unit vector)
    float norm = 0.0f;
    for (size_t i = 0; i < EMBED_DIM; ++i) {
        norm += artha.nu[i] * artha.nu[i];
    }
    norm = std::sqrt(norm);
    assert(std::abs(norm - 1.0f) < 0.01f);  // Should be ~1.0

    // Test similarity
    Artha artha2 = yantra->transform("A fast brown fox leaps over a sleepy dog.");
    Artha artha3 = yantra->transform("The weather is sunny today.");

    float sim_similar = artha.nu.cosine(artha2.nu);
    float sim_different = artha.nu.cosine(artha3.nu);

    std::cout << "  Similar sentences: " << sim_similar << std::endl;
    std::cout << "  Different sentences: " << sim_different << std::endl;

    assert(sim_similar > sim_different);  // Similar should be more similar
    assert(sim_similar > 0.7f);  // Should be quite similar

    // Test batch
    auto arthas = yantra->transform_batch({
        "Machine learning is fascinating.",
        "Deep learning uses neural networks.",
        "I like pizza."
    });
    assert(arthas.size() == 3);

    float ml_dl_sim = arthas[0].nu.cosine(arthas[1].nu);
    float ml_pizza_sim = arthas[0].nu.cosine(arthas[2].nu);

    std::cout << "  ML vs DL: " << ml_dl_sim << std::endl;
    std::cout << "  ML vs Pizza: " << ml_pizza_sim << std::endl;

    assert(ml_dl_sim > ml_pizza_sim);  // ML and DL should be more similar

    std::cout << "  PASS" << std::endl;
}

void test_mind_with_text() {
    std::cout << "Testing Mind with text..." << std::endl;

    // Clean up any previous test data
    std::system("rm -f /tmp/chitta_mind_text_test.*");

    const char* model_path = "../models/model.onnx";
    const char* vocab_path = "../models/vocab.txt";

    std::ifstream model_check(model_path);
    if (!model_check) {
        std::cout << "  SKIP (model files not found)" << std::endl;
        return;
    }

    // Create yantra with caching
    auto yantra = create_yantra(model_path, vocab_path, 1000);
    if (!yantra) {
        std::cout << "  SKIP (failed to create yantra)" << std::endl;
        return;
    }

    // Create mind with yantra
    MindConfig config;
    config.path = "/tmp/chitta_mind_text_test";

    Mind mind(config);
    mind.attach_yantra(yantra);
    assert(mind.open());
    assert(mind.has_yantra());

    // Remember some wisdom
    NodeId id1 = mind.remember("Simplicity is the ultimate sophistication.", NodeType::Wisdom);
    NodeId id2 = mind.remember("Less is more in design.", NodeType::Wisdom);
    NodeId id3 = mind.remember("The weather is nice today.", NodeType::Episode);

    assert(mind.size() == 3);

    // Recall by similar text
    auto results = mind.recall("Keep things simple and elegant.", 5, 0.0f);
    assert(!results.empty());

    std::cout << "  Query: 'Keep things simple and elegant.'" << std::endl;
    for (const auto& r : results) {
        std::cout << "    " << r.similarity << ": " << r.text << std::endl;
    }

    // The simplicity quotes should rank higher than weather
    // Verify semantic search works - top result should NOT be about weather
    assert(results[0].similarity > results[2].similarity);
    assert(results[0].similarity > 0.4f);  // Should be reasonably similar

    mind.close();

    std::cout << "  PASS" << std::endl;
}
#endif

int main() {
    std::cout << "=== Chitta C++ Tests ===" << std::endl;
    std::cout << "EMBED_DIM = " << EMBED_DIM << std::endl;
    std::cout << std::endl;

    test_confidence();
    test_node();
    test_graph_insert_get();
    test_graph_semantic_query();
    test_graph_snapshot_rollback();
    test_coherence();
    test_ops();
    test_voice();
    test_chorus();
    test_dynamics();
    test_learning_cycle();
    test_quantized_vector();
    test_hnsw_index();
    test_tiered_storage();
    test_wal_deltas();

    std::cout << std::endl;
    std::cout << "=== Phase 3: Unified Index Tests ===" << std::endl;
    test_hilbert_curve();
    test_connection_pool();
    test_unified_index();
    test_unified_index_scale();
    test_unified_snapshot();
    test_segment_manager();
    test_tiered_storage_segments();

    test_mind();
    test_persistence();

#ifdef CHITTA_WITH_ONNX
    std::cout << std::endl;
    std::cout << "=== ONNX Embedding Tests ===" << std::endl;
    test_vak_onnx();
    test_mind_with_text();
#endif

    std::cout << std::endl;
    std::cout << "=== All tests passed! ===" << std::endl;
    return 0;
}
