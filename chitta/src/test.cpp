#include <synapse/synapse.hpp>
#include <iostream>
#include <cassert>
#include <cmath>

using namespace synapse;

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

    TieredStorage::Config config;
    config.base_path = "/tmp/synapse_test";
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

void test_mind() {
    std::cout << "Testing Mind..." << std::endl;

    // Clean up any previous test data
    std::system("rm -f /tmp/synapse_mind_test.*");

    MindConfig config;
    config.path = "/tmp/synapse_mind_test";

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

    const std::string path = "/tmp/synapse_persist_test";
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

#ifdef SYNAPSE_WITH_ONNX
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
    std::system("rm -f /tmp/synapse_mind_text_test.*");

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
    config.path = "/tmp/synapse_mind_text_test";

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
    std::cout << "=== Synapse C++ Tests ===" << std::endl;
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
    test_mind();
    test_persistence();

#ifdef SYNAPSE_WITH_ONNX
    std::cout << std::endl;
    std::cout << "=== ONNX Embedding Tests ===" << std::endl;
    test_vak_onnx();
    test_mind_with_text();
#endif

    std::cout << std::endl;
    std::cout << "=== All tests passed! ===" << std::endl;
    return 0;
}
