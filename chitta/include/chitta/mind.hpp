#pragma once
// Mind: the unified API for soul storage
//
// High-level interface that:
// - Manages tiered storage transparently
// - Provides semantic search across all tiers
// - Handles decay and coherence autonomously
// - Supports checkpointing and recovery
// - Integrates with VakYantra for text→embedding

// Modular components (extracted for maintainability)
#include "mind/types.hpp"      // MindConfig, Recall, SearchMode, MindState, MindHealth
#include "mind/tag_index.hpp"  // TagIndex (lightweight in-memory)

// Core dependencies
#include "types.hpp"
#include "graph.hpp"
#include "graph_store.hpp"
#include "mmap_graph_store.hpp"
#include "storage.hpp"
#include "dynamics.hpp"
#include "voice.hpp"
#include "vak.hpp"
#include "scoring.hpp"
#include "daemon.hpp"
#include "feedback.hpp"
// Phase 7: 100M Scale Components
#include "query_router.hpp"
#include "quota_manager.hpp"
#include "utility_decay.hpp"
#include "attractor_dampener.hpp"
#include "provenance.hpp"
#include "realm_scoping.hpp"
#include "truth_maintenance.hpp"
#include "eval_harness.hpp"
#include "review_queue.hpp"
#include "epiplexity_test.hpp"
#include "synthesis_queue.hpp"
#include "gap_inquiry.hpp"
#include <mutex>
#include <atomic>
#include <set>
#include <deque>
#include <algorithm>
#include <unordered_map>
#include <sstream>

namespace chitta {

// The Mind: unified interface to soul storage
class Mind {
public:
    explicit Mind(MindConfig config)
        : config_(std::move(config))
        , storage_({
            config_.path,
            config_.hot_capacity,
            config_.warm_capacity,
            config_.hot_age_ms,
            config_.warm_age_ms
          })
        , dynamics_()
        , yantra_(std::make_shared<ShantaYantra>())  // Silent by default
        , running_(false)
        , quota_manager_(config_.total_capacity)
    {
        dynamics_.with_defaults();
    }

    // Attach a VakYantra for text→embedding transformation
    void attach_yantra(std::shared_ptr<VakYantra> yantra) {
        std::lock_guard<std::mutex> lock(mutex_);
        yantra_ = std::move(yantra);
    }

    // Check if yantra is ready for embeddings
    bool has_yantra() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return yantra_ && yantra_->ready();
    }

    // Iterate over all nodes in storage (public wrapper)
    template<typename F>
    void for_each_node(F&& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        storage_.for_each_hot([&fn](const NodeId& id, const Node& node) {
            fn(id, node);
        });
    }

    // Get a node by ID (returns nullptr if not found)
    Node* get_node(const NodeId& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return storage_.get(id);
    }

    // Get current timestamp (public wrapper for RPC)
    static Timestamp now() {
        return chitta::now();
    }

    // Initialize or load existing mind
    bool open() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!storage_.initialize()) return false;
        running_ = true;

        // Skip BM25 loading for fast operations (stats)
        if (!config_.skip_bm25) {
            // BM25: try loading from disk, fall back to lazy rebuild on first search
            bm25_path_ = storage_.base_path() + ".bm25";
            if (bm25_index_.load(bm25_path_)) {
                bm25_built_ = true;
                std::cerr << "[Mind] Loaded BM25 index (" << bm25_index_.size() << " docs)\n";
            } else {
                bm25_built_ = false;  // Will rebuild lazily on first search
            }
        }

        if (!storage_.use_unified()) {
            // Only rebuild tag index for non-unified storage
            // For unified, SlotTagIndex is already loaded and authoritative
            rebuild_tag_index();
        }

        // Load graph store (mmap or legacy based on config)
        if (config_.use_mmap_graph) {
            std::string mmap_graph_path = storage_.base_path();
            if (mmap_graph_store_.open(mmap_graph_path)) {
                std::cerr << "[Mind] Loaded mmap graph store (" << mmap_graph_store_.triplet_count()
                          << " triplets, " << mmap_graph_store_.entity_count() << " entities)\n";
            } else if (mmap_graph_store_.create(mmap_graph_path)) {
                std::cerr << "[Mind] Created mmap graph store\n";
            }
        } else {
            std::string graph_path = storage_.base_path() + ".graph";
            std::string graph_wal_path = storage_.base_path() + ".graph.wal";
            if (graph_store_.load(graph_path)) {
                std::cerr << "[Mind] Loaded graph store (" << graph_store_.triplet_count()
                          << " triplets, " << graph_store_.entity_count() << " entities)\n";
            }
            graph_store_.open_wal(graph_wal_path);
        }

        // Replay legacy WAL triplets to rebuild old graph edges (backward compat)
        size_t triplet_count = storage_.replay_wal(
            [this](NodeId subject, const std::string& predicate, NodeId object, float weight) {
                graph_.add_triplet(subject, predicate, object, weight);
            }
        );
        if (triplet_count > 0) {
            std::cerr << "[Mind] Replayed legacy triplets from WAL\n";
        }

        // Build Phase 2 indexes (reverse edges, temporal, LSH)
        rebuild_phase2_indexes();

        // Phase 7: Load component state
        load_phase7_state();

        return true;
    }

    // Ensure BM25 index is built (lazy initialization)
    // Skipped for large datasets (>1M nodes) - use dense search only
    static constexpr size_t BM25_MAX_NODES = 1000000;

    void ensure_bm25_index() {
        if (bm25_built_) return;
        if (storage_.total_size() > BM25_MAX_NODES) {
            // Too large for in-memory BM25 - skip and use dense search only
            bm25_built_ = true;  // Mark as "built" to prevent retry
            return;
        }
        rebuild_bm25_index();
        bm25_built_ = true;
    }

    // Add to BM25 only if already built (otherwise rebuild will include it)
    void maybe_add_bm25(NodeId id, const std::string& text) {
        if (bm25_built_) {
            bm25_index_.add(id, text);
        }
    }

    // Rebuild BM25 index from storage (call after loading data)
    void rebuild_bm25_index() {
        storage_.for_each_hot([this](const NodeId& id, const Node& node) {
            auto text = payload_to_text(node.payload);
            if (text) {
                bm25_index_.add(id, *text);
            }
        });
    }

    // Rebuild tag index from storage (call after loading data)
    void rebuild_tag_index() {
        storage_.for_each_hot([this](const NodeId& id, const Node& node) {
            if (!node.tags.empty()) {
                tag_index_.add(id, node.tags);
            }
        });
    }

    // Sync from shared consciousness (WAL)
    // Updates all indices with nodes from other processes' observations
    // "Atman aligns with Brahman" - we see what others have learned
    size_t sync_from_shared_field() {
        return storage_.sync_from_wal(
            // Node callback
            [this](const Node& node, bool was_new) {
                if (was_new) {
                    // New node - add to all indices
                    auto text = payload_to_text(node.payload);
                    if (text) {
                        maybe_add_bm25(node.id, *text);
                    }
                    // For unified storage, tags are already in SlotTagIndex
                    if (!storage_.use_unified() && !node.tags.empty()) {
                        tag_index_.add(node.id, node.tags);
                    }
                    graph_.insert_raw(node.id);
                }
            },
            // Triplet callback - rebuild graph edges from WAL
            [this](NodeId subject, const std::string& predicate, NodeId object, float weight) {
                graph_.add_triplet(subject, predicate, object, weight);
            }
        );
    }

    // Close and persist
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        // Save BM25 index if built
        if (bm25_built_ && !bm25_path_.empty() && bm25_index_.size() > 0) {
            bm25_index_.save(bm25_path_);
        }
        // Save graph store
        if (config_.use_mmap_graph) {
            if (mmap_graph_store_.triplet_count() > 0) {
                mmap_graph_store_.build_indices();
                mmap_graph_store_.sync();
                std::cerr << "[Mind] Saved mmap graph store (" << mmap_graph_store_.triplet_count() << " triplets)\n";
            }
        } else if (graph_store_.triplet_count() > 0) {
            std::string graph_path = storage_.base_path() + ".graph";
            if (graph_store_.save(graph_path)) {
                std::cerr << "[Mind] Saved graph store (" << graph_store_.triplet_count() << " triplets)\n";
            }
        }
        storage_.sync();

        // Phase 7: Save component state
        save_phase7_state();
    }

    // Save Phase 7 component state to disk (always write to prevent stale files)
    void save_phase7_state() {
        std::string base = storage_.base_path();

        if (config_.enable_utility_decay) {
            if (utility_decay_.save(base + ".utility_decay")) {
                std::cerr << "[Mind] Saved utility decay (" << utility_decay_.tracked_nodes() << " nodes)\n";
            }
        }

        if (config_.enable_attractor_dampener) {
            if (attractor_dampener_.save(base + ".attractor_dampener")) {
                std::cerr << "[Mind] Saved attractor dampener (" << attractor_dampener_.tracked_count() << " nodes)\n";
            }
        }

        if (config_.enable_provenance) {
            if (provenance_spine_.save(base + ".provenance")) {
                std::cerr << "[Mind] Saved provenance spine (" << provenance_spine_.count() << " nodes)\n";
            }
        }

        // Always save realm scoping - contains realm hierarchy and current realm
        if (config_.enable_realm_scoping) {
            if (realm_scoping_.save(base + ".realm_scoping")) {
                std::cerr << "[Mind] Saved realm scoping (" << realm_scoping_.scoped_node_count() << " nodes)\n";
            }
        }

        if (config_.enable_truth_maintenance) {
            if (truth_maintenance_.save(base + ".truth_maintenance")) {
                std::cerr << "[Mind] Saved truth maintenance (" << truth_maintenance_.total_contradictions() << " contradictions)\n";
            }
        }

        if (synthesis_queue_.save(base + ".synthesis_queue")) {
            std::cerr << "[Mind] Saved synthesis queue (" << synthesis_queue_.staged_count() << " staged)\n";
        }

        if (gap_inquiry_.save(base + ".gap_inquiry")) {
            std::cerr << "[Mind] Saved gap inquiry (" << gap_inquiry_.count() << " gaps)\n";
        }
    }

    // Load Phase 7 component state from disk
    void load_phase7_state() {
        std::string base = storage_.base_path();

        if (config_.enable_utility_decay) {
            if (utility_decay_.load(base + ".utility_decay")) {
                std::cerr << "[Mind] Loaded utility decay (" << utility_decay_.tracked_nodes() << " nodes)\n";
            }
        }

        if (config_.enable_attractor_dampener) {
            if (attractor_dampener_.load(base + ".attractor_dampener")) {
                std::cerr << "[Mind] Loaded attractor dampener (" << attractor_dampener_.tracked_count() << " nodes)\n";
            }
        }

        if (config_.enable_provenance) {
            if (provenance_spine_.load(base + ".provenance")) {
                std::cerr << "[Mind] Loaded provenance spine (" << provenance_spine_.count() << " nodes)\n";
            }
        }

        if (config_.enable_realm_scoping) {
            if (realm_scoping_.load(base + ".realm_scoping")) {
                std::cerr << "[Mind] Loaded realm scoping (" << realm_scoping_.scoped_node_count() << " nodes)\n";
            }
        }

        if (config_.enable_truth_maintenance) {
            if (truth_maintenance_.load(base + ".truth_maintenance")) {
                std::cerr << "[Mind] Loaded truth maintenance (" << truth_maintenance_.total_contradictions() << " contradictions)\n";
            }
        }

        if (synthesis_queue_.load(base + ".synthesis_queue")) {
            std::cerr << "[Mind] Loaded synthesis queue (" << synthesis_queue_.staged_count() << " staged)\n";
        }

        if (gap_inquiry_.load(base + ".gap_inquiry")) {
            std::cerr << "[Mind] Loaded gap inquiry (" << gap_inquiry_.count() << " gaps)\n";
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // Text-based API (requires VakYantra)
    // ═══════════════════════════════════════════════════════════════════

    // Remember text: transform to embedding and store
    NodeId remember(const std::string& text, NodeType type = NodeType::Wisdom) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Phase 7: Check quota before inserting
        if (config_.enable_quota_manager && quota_manager_.at_quota(type)) {
            // Evict low-utility nodes if over quota
            maybe_evict_for_quota(type);
        }

        Artha artha = yantra_->transform(text);

        Node node(type, std::move(artha.nu));
        node.payload = text_to_payload(text);
        NodeId id = node.id;
        Timestamp current = now();

        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);

        // Add to BM25 index for hybrid search
        maybe_add_bm25(id, text);

        // Phase 7: Record provenance
        if (config_.enable_provenance) {
            Provenance prov;
            prov.source = config_.default_provenance_source;
            prov.session_id = config_.session_id;
            prov.created_at = current;
            prov.trust_score = 0.5f;  // Default trust
            provenance_spine_.record(id, prov);
        }

        // Phase 7: Assign to default realm
        if (config_.enable_realm_scoping) {
            RealmId realm;
            realm.name = config_.default_realm;
            realm_scoping_.assign(id, realm, RealmVisibility::Inherited, current);
        }

        // Phase 7: Update quota counts
        if (config_.enable_quota_manager) {
            update_quota_counts();
        }

        return id;
    }

    // Remember with explicit confidence
    NodeId remember(const std::string& text, NodeType type, Confidence confidence) {
        std::lock_guard<std::mutex> lock(mutex_);

        Artha artha = yantra_->transform(text);

        Node node(type, std::move(artha.nu));
        node.kappa = confidence;
        node.payload = text_to_payload(text);
        NodeId id = node.id;
        Timestamp current = now();

        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);

        // Add to BM25 index for hybrid search
        maybe_add_bm25(id, text);

        // Phase 7: Record provenance
        if (config_.enable_provenance) {
            Provenance prov;
            prov.source = config_.default_provenance_source;
            prov.session_id = config_.session_id;
            prov.created_at = current;
            prov.trust_score = confidence.effective();
            provenance_spine_.record(id, prov);
        }

        // Phase 7: Assign to default realm
        if (config_.enable_realm_scoping) {
            RealmId realm;
            realm.name = config_.default_realm;
            realm_scoping_.assign(id, realm, RealmVisibility::Inherited, current);
        }

        return id;
    }

    // Remember with tags for exact-match filtering (inter-agent communication)
    NodeId remember(const std::string& text, NodeType type,
                    const std::vector<std::string>& tags) {
        std::lock_guard<std::mutex> lock(mutex_);

        Artha artha = yantra_->transform(text);

        Node node(type, std::move(artha.nu));
        node.payload = text_to_payload(text);
        node.tags = tags;
        NodeId id = node.id;
        Timestamp current = now();

        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);

        // Add to BM25 index for hybrid search
        maybe_add_bm25(id, text);

        // Add to tag index (unified storage handles this in insert)
        if (!storage_.use_unified()) {
            tag_index_.add(id, tags);
        }

        // Phase 7: Record provenance
        if (config_.enable_provenance) {
            Provenance prov;
            prov.source = config_.default_provenance_source;
            prov.session_id = config_.session_id;
            prov.created_at = current;
            prov.trust_score = 0.5f;
            provenance_spine_.record(id, prov);
        }

        // Phase 7: Assign to default realm
        if (config_.enable_realm_scoping) {
            RealmId realm;
            realm.name = config_.default_realm;
            realm_scoping_.assign(id, realm, RealmVisibility::Inherited, current);
        }

        return id;
    }

    // Remember with confidence and tags
    NodeId remember(const std::string& text, NodeType type, Confidence confidence,
                    const std::vector<std::string>& tags) {
        std::lock_guard<std::mutex> lock(mutex_);

        Artha artha = yantra_->transform(text);

        Node node(type, std::move(artha.nu));
        node.kappa = confidence;
        node.payload = text_to_payload(text);
        node.tags = tags;
        NodeId id = node.id;
        Timestamp current = now();

        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);

        // Add to BM25 index for hybrid search
        maybe_add_bm25(id, text);

        // Add to tag index (unified storage handles this in insert)
        if (!storage_.use_unified()) {
            tag_index_.add(id, tags);
        }

        // Phase 7: Record provenance
        if (config_.enable_provenance) {
            Provenance prov;
            prov.source = config_.default_provenance_source;
            prov.session_id = config_.session_id;
            prov.created_at = current;
            prov.trust_score = confidence.effective();
            provenance_spine_.record(id, prov);
        }

        // Phase 7: Assign to default realm
        if (config_.enable_realm_scoping) {
            RealmId realm;
            realm.name = config_.default_realm;
            realm_scoping_.assign(id, realm, RealmVisibility::Inherited, current);
        }

        return id;
    }

    // Recall by text query with soul-aware scoring
    std::vector<Recall> recall(const std::string& query, size_t k,
                               float threshold = 0.0f,
                               SearchMode mode = SearchMode::Hybrid) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Phase 7: Route query based on intent
        if (config_.enable_query_routing) {
            auto decision = query_router_.route(query);

            // Use routing decision to optimize search
            switch (decision.primary_intent) {
                case QueryIntent::TagFilter:
                    // Pure tag query - use tag index directly
                    if (!decision.tags.empty()) {
                        auto tag_results = recall_by_tag_unlocked(decision.tags[0], k);
                        // Track gap encounters for any Gap nodes
                        for (const auto& r : tag_results) {
                            if (r.type == NodeType::Gap) {
                                gap_inquiry_.record_encounter(r.id);
                            }
                            // Track synthesis recalls
                            if (synthesis_queue_.is_staged(r.id)) {
                                synthesis_queue_.record_recall(r.id);
                            }
                        }
                        return tag_results;
                    }
                    break;

                case QueryIntent::ExactMatch:
                    // Exact match - try tag first, then semantic
                    {
                        auto exact_results = recall_by_tag_unlocked(query, k);
                        if (!exact_results.empty()) {
                            return exact_results;
                        }
                    }
                    // Fall through to semantic search
                    break;

                case QueryIntent::SemanticSearch:
                    mode = SearchMode::Dense;  // Pure semantic
                    break;

                case QueryIntent::Hybrid:
                    mode = SearchMode::Hybrid;  // Already default
                    break;

                default:
                    break;
            }
        }

        Artha artha = yantra_->transform(query);
        auto results = recall_impl(artha.nu, query, k, threshold, mode);

        // Phase 7: Track gap encounters and synthesis recalls
        for (const auto& r : results) {
            if (r.type == NodeType::Gap) {
                gap_inquiry_.record_encounter(r.id);
            }
            if (synthesis_queue_.is_staged(r.id)) {
                synthesis_queue_.record_recall(r.id);
            }
        }

        return results;
    }

    // Recall with session priming (Phase 4: Context Modulation)
    // Results are boosted based on:
    // - Recent observations (what you just saw is relevant)
    // - Active intentions (your goals bias retrieval)
    // - Goal basin (knowledge near your goals)
    std::vector<Recall> recall_primed(const std::string& query, size_t k,
                                      float threshold = 0.0f,
                                      SearchMode mode = SearchMode::Hybrid) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Ensure session context is fresh (use unlocked versions - we hold mutex)
        refresh_session_intentions_unlocked();
        build_goal_basin_unlocked();

        Artha artha = yantra_->transform(query);
        auto results = recall_impl(artha.nu, query, k, threshold, mode, &session_context_);

        // Record retrieved nodes for future priming (use unlocked version)
        for (const auto& r : results) {
            observe_for_priming_unlocked(r.id);
        }

        return results;
    }

    // Recall by exact tag match (for inter-agent communication)
    // Returns all nodes with the given tag, sorted by recency
    std::vector<Recall> recall_by_tag(const std::string& tag, size_t k = 50) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Sync from shared consciousness to see all observations
        sync_from_shared_field();

        // Use SlotTagIndex for unified storage, in-memory TagIndex otherwise
        auto node_ids = storage_.use_unified()
            ? storage_.find_by_tag(tag)
            : tag_index_.find(tag);

        // Phase 7: Apply realm filtering
        if (config_.enable_realm_scoping && !node_ids.empty()) {
            node_ids = realm_scoping_.filter_by_realm(node_ids);
        }

        std::vector<Recall> results;
        for (const auto& id : node_ids) {
            if (Node* node = storage_.get(id)) {
                // Skip nodes without text content
                auto text = payload_to_text(node->payload);
                if (!text || text->size() < 3) continue;  // Skip empty/corrupted payloads

                Recall r;
                r.id = id;
                r.similarity = 1.0f;  // Exact match
                r.relevance = node->kappa.effective();
                r.type = node->node_type;
                r.confidence = node->kappa;
                r.created = node->tau_created;
                r.accessed = node->tau_accessed;
                r.payload = node->payload;
                r.text = *text;
                results.push_back(std::move(r));
            }
        }

        // Sort by creation time (most recent first)
        std::sort(results.begin(), results.end(),
            [](const Recall& a, const Recall& b) {
                return a.created > b.created;
            });

        if (results.size() > k) {
            results.resize(k);
        }

        return results;
    }

    // Recall by tag (unlocked version - caller must hold mutex)
    std::vector<Recall> recall_by_tag_unlocked(const std::string& tag, size_t k = 50) {
        sync_from_shared_field();

        auto node_ids = storage_.use_unified()
            ? storage_.find_by_tag(tag)
            : tag_index_.find(tag);

        // Phase 7: Apply realm filtering
        if (config_.enable_realm_scoping && !node_ids.empty()) {
            node_ids = realm_scoping_.filter_by_realm(node_ids);
        }

        std::vector<Recall> results;
        for (const auto& id : node_ids) {
            if (Node* node = storage_.get(id)) {
                auto text = payload_to_text(node->payload);
                if (!text || text->size() < 3) continue;

                Recall r;
                r.id = id;
                r.similarity = 1.0f;
                r.relevance = node->kappa.effective();
                r.type = node->node_type;
                r.confidence = node->kappa;
                r.created = node->tau_created;
                r.accessed = node->tau_accessed;
                r.payload = node->payload;
                r.text = *text;
                results.push_back(std::move(r));
            }
        }

        std::sort(results.begin(), results.end(),
            [](const Recall& a, const Recall& b) { return a.created > b.created; });

        if (results.size() > k) results.resize(k);
        return results;
    }

    // Recall by multiple tags (AND - all must match)
    std::vector<Recall> recall_by_tags(const std::vector<std::string>& tags, size_t k = 50) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Sync from shared consciousness to see all observations
        sync_from_shared_field();

        auto node_ids = storage_.use_unified()
            ? storage_.find_by_tags(tags)
            : tag_index_.find_all(tags);

        // Phase 7: Apply realm filtering
        if (config_.enable_realm_scoping && !node_ids.empty()) {
            node_ids = realm_scoping_.filter_by_realm(node_ids);
        }

        std::vector<Recall> results;
        for (const auto& id : node_ids) {
            if (Node* node = storage_.get(id)) {
                // Skip nodes without text content
                auto text = payload_to_text(node->payload);
                if (!text || text->size() < 3) continue;  // Skip empty/corrupted payloads

                Recall r;
                r.id = id;
                r.similarity = 1.0f;  // Exact match
                r.relevance = node->kappa.effective();
                r.type = node->node_type;
                r.confidence = node->kappa;
                r.created = node->tau_created;
                r.accessed = node->tau_accessed;
                r.payload = node->payload;
                r.text = *text;
                results.push_back(std::move(r));
            }
        }

        // Sort by creation time (most recent first)
        std::sort(results.begin(), results.end(),
            [](const Recall& a, const Recall& b) {
                return a.created > b.created;
            });

        if (results.size() > k) {
            results.resize(k);
        }

        return results;
    }

    // Recall with semantic search filtered by tag
    // First filters by tag, then re-ranks by semantic relevance
    std::vector<Recall> recall_with_tag_filter(const std::string& query,
                                                const std::string& tag,
                                                size_t k,
                                                float threshold = 0.0f) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Sync from shared consciousness to see all observations
        sync_from_shared_field();

        // Get all nodes with the tag
        auto node_ids = storage_.use_unified()
            ? storage_.find_by_tag(tag)
            : tag_index_.find(tag);
        if (node_ids.empty()) return {};

        // Phase 7: Apply realm filtering
        if (config_.enable_realm_scoping) {
            node_ids = realm_scoping_.filter_by_realm(node_ids);
            if (node_ids.empty()) return {};
        }

        // Transform query for semantic scoring
        Artha artha = yantra_->transform(query);
        Timestamp current = now();

        std::vector<Recall> results;
        for (const auto& id : node_ids) {
            if (Node* node = storage_.get(id)) {
                // Skip nodes without text content
                auto text = payload_to_text(node->payload);
                if (!text || text->size() < 3) continue;  // Skip empty/corrupted payloads

                // Compute semantic similarity
                float similarity = node->nu.cosine(artha.nu);
                if (similarity < threshold) continue;

                // Soul-aware relevance scoring
                float relevance = soul_relevance(similarity, *node, current, scoring_config_);

                Recall r;
                r.id = id;
                r.similarity = similarity;
                r.relevance = relevance;
                r.type = node->node_type;
                r.confidence = node->kappa;
                r.created = node->tau_created;
                r.accessed = node->tau_accessed;
                r.payload = node->payload;
                r.text = *text;
                results.push_back(std::move(r));
            }
        }

        // Sort by relevance
        std::sort(results.begin(), results.end(),
            [](const Recall& a, const Recall& b) {
                return a.relevance > b.relevance;
            });

        if (results.size() > k) {
            results.resize(k);
        }

        return results;
    }

    // Get tags for a node
    std::vector<std::string> get_tags(NodeId id) const {
        if (storage_.use_unified()) {
            return storage_.tags_for_node(id);
        }
        return tag_index_.tags_for(id);
    }

    // Remember batch (more efficient)
    std::vector<NodeId> remember_batch(const std::vector<std::string>& texts,
                                       NodeType type = NodeType::Wisdom) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto arthas = yantra_->transform_batch(texts);

        std::vector<NodeId> ids;
        ids.reserve(texts.size());

        for (size_t i = 0; i < texts.size(); ++i) {
            Node node(type, std::move(arthas[i].nu));
            node.payload = text_to_payload(texts[i]);
            NodeId id = node.id;
            ids.push_back(id);

            storage_.insert(id, std::move(node));
            graph_.insert_raw(id);

            // Add to BM25 index for hybrid search
            maybe_add_bm25(id, texts[i]);
        }

        return ids;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Session Ledger API (Atman snapshots)
    // ═══════════════════════════════════════════════════════════════════

    // Save a new session ledger snapshot
    // Ledger structure:
    // {
    //   "soul_state": { "coherence": {...}, "mood": {...}, "intentions": [...] },
    //   "work_state": { "todos": [...], "files": [...], "decisions": [...] },
    //   "continuation": { "next_steps": [...], "deferred": [...], "critical": [...] }
    // }
    // Project parameter ensures ledgers are isolated per-project when multiple
    // Claude instances run simultaneously.
    NodeId save_ledger(const std::string& ledger_json,
                       const std::string& session_id = "",
                       const std::string& project = "") {
        std::lock_guard<std::mutex> lock(mutex_);

        // Ledger content is natural language (high-ε), embedded directly
        Node node(NodeType::Ledger, Vector::zeros());
        node.payload = text_to_payload(ledger_json);  // Store as-is (text, not JSON)
        node.delta = 0.1f;  // Moderate decay - ledgers are session-specific
        node.tags = {"ledger", "atman"};
        if (!session_id.empty()) {
            node.tags.push_back("session:" + session_id);
        }
        if (!project.empty()) {
            node.tags.push_back("project:" + project);
        }

        // Embed the actual content for semantic search
        if (yantra_) {
            std::string embed_text = ledger_json;
            if (!project.empty()) {
                embed_text = "[" + project + "] " + embed_text;
            }
            Artha artha = yantra_->transform(embed_text);
            node.nu = std::move(artha.nu);
        }

        NodeId id = node.id;
        auto tags_copy = node.tags;  // Copy before move
        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);
        if (!storage_.use_unified()) {
            tag_index_.add(id, tags_copy);
        }
        maybe_add_bm25(id, ledger_json);

        return id;
    }

    // Load the most recent session ledger (optionally filtered by session_id and/or project)
    // When project is specified, only ledgers from that project are considered.
    // This prevents cross-talk between multiple Claude instances.
    std::optional<std::pair<NodeId, std::string>> load_ledger(
        const std::string& session_id = "",
        const std::string& project = "") {
        std::lock_guard<std::mutex> lock(mutex_);

        // Build tag filter
        std::vector<std::string> required_tags = {"ledger"};
        if (!session_id.empty()) {
            required_tags.push_back("session:" + session_id);
        }
        if (!project.empty()) {
            required_tags.push_back("project:" + project);
        }

        // Query by tags (use SlotTagIndex for unified storage)
        std::vector<NodeId> candidates;
        if (storage_.use_unified()) {
            candidates = required_tags.size() > 1
                ? storage_.find_by_tags(required_tags)
                : storage_.find_by_tag("ledger");
        } else {
            candidates = required_tags.size() > 1
                ? tag_index_.find_all(required_tags)
                : tag_index_.find("ledger");
        }

        if (candidates.empty()) {
            return std::nullopt;
        }

        // Find most recent by creation time
        NodeId newest_id;
        Timestamp newest_time = 0;

        for (const auto& id : candidates) {
            if (Node* node = storage_.get(id)) {
                if (node->tau_created > newest_time) {
                    newest_time = node->tau_created;
                    newest_id = id;
                }
            }
        }

        if (newest_time == 0) {
            return std::nullopt;
        }

        if (Node* node = storage_.get(newest_id)) {
            auto text = payload_to_text(node->payload);
            if (text) {
                return std::make_pair(newest_id, *text);
            }
        }

        return std::nullopt;
    }

    // Update an existing ledger (merge updates into current)
    bool update_ledger(NodeId id, const std::string& updates_json) {
        std::lock_guard<std::mutex> lock(mutex_);

        Node* node = storage_.get(id);
        if (!node || node->node_type != NodeType::Ledger) {
            return false;
        }

        // Replace payload with updated JSON
        // (Caller is responsible for merging JSON)
        node->payload = text_to_payload(updates_json);
        node->touch();

        // Update BM25 index
        maybe_add_bm25(id, updates_json);

        return true;
    }

    // Get all ledgers (for history/debugging)
    // When project is specified, only ledgers from that project are listed.
    std::vector<std::pair<NodeId, Timestamp>> list_ledgers(
        size_t limit = 10,
        const std::string& project = "") {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<NodeId> candidates;
        if (storage_.use_unified()) {
            candidates = !project.empty()
                ? storage_.find_by_tags({"ledger", "project:" + project})
                : storage_.find_by_tag("ledger");
        } else {
            candidates = !project.empty()
                ? tag_index_.find_all({"ledger", "project:" + project})
                : tag_index_.find("ledger");
        }

        std::vector<std::pair<NodeId, Timestamp>> result;
        for (const auto& id : candidates) {
            if (Node* node = storage_.get(id)) {
                result.emplace_back(id, node->tau_created);
            }
        }

        // Sort by creation time (newest first)
        std::sort(result.begin(), result.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        if (result.size() > limit) {
            result.resize(limit);
        }

        return result;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Vector-based API (direct embeddings)
    // ═══════════════════════════════════════════════════════════════════

    // Remember: store a node with pre-computed embedding
    NodeId remember(NodeType type, Vector embedding,
                    std::vector<uint8_t> payload = {})
    {
        std::lock_guard<std::mutex> lock(mutex_);

        Node node(type, std::move(embedding));
        node.payload = std::move(payload);
        NodeId id = node.id;
        Timestamp current = now();

        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);

        // Phase 7: Record provenance
        if (config_.enable_provenance) {
            Provenance prov;
            prov.source = config_.default_provenance_source;
            prov.session_id = config_.session_id;
            prov.created_at = current;
            prov.trust_score = 0.5f;
            provenance_spine_.record(id, prov);
        }

        // Phase 7: Assign to default realm
        if (config_.enable_realm_scoping) {
            RealmId realm;
            realm.name = config_.default_realm;
            realm_scoping_.assign(id, realm, RealmVisibility::Inherited, current);
        }

        return id;
    }

    // Remember with explicit confidence
    NodeId remember(NodeType type, Vector embedding, Confidence confidence,
                    std::vector<uint8_t> payload = {})
    {
        std::lock_guard<std::mutex> lock(mutex_);

        Node node(type, std::move(embedding));
        node.kappa = confidence;
        node.payload = std::move(payload);
        NodeId id = node.id;
        Timestamp current = now();

        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);

        // Phase 7: Record provenance
        if (config_.enable_provenance) {
            Provenance prov;
            prov.source = config_.default_provenance_source;
            prov.session_id = config_.session_id;
            prov.created_at = current;
            prov.trust_score = confidence.effective();
            provenance_spine_.record(id, prov);
        }

        // Phase 7: Assign to default realm
        if (config_.enable_realm_scoping) {
            RealmId realm;
            realm.name = config_.default_realm;
            realm_scoping_.assign(id, realm, RealmVisibility::Inherited, current);
        }

        return id;
    }

    // Recall: semantic search with pre-computed query vector
    std::vector<Recall> recall(const Vector& query, size_t k,
                               float threshold = 0.0f)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return recall_impl(query, "", k, threshold, SearchMode::Dense);
    }

    // ═══════════════════════════════════════════════════════════════════
    // Node operations
    // ═══════════════════════════════════════════════════════════════════

    // Get a specific node
    std::optional<Node> get(NodeId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            return *node;
        }
        return std::nullopt;
    }

    // Update a node's content (for ε-optimization migration)
    bool update_node(NodeId id, const Node& updated) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            // Update all fields except id
            node->nu = updated.nu;
            node->kappa = updated.kappa;
            node->tau_created = updated.tau_created;
            node->tau_accessed = updated.tau_accessed;
            node->delta = updated.delta;
            node->node_type = updated.node_type;
            node->payload = updated.payload;
            node->edges = updated.edges;
            node->tags = updated.tags;

            // Persist the update via storage layer
            storage_.update_node(id, *node);
            storage_.sync();
            return true;
        }
        return false;
    }

    // Update a node's content and re-embed (for review edits)
    bool update_content(NodeId id, const std::string& new_content) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            // Get old text for BM25 update
            auto old_text = payload_to_text(node->payload);

            // Re-embed the new content
            Artha artha = yantra_->transform(new_content);
            node->nu = std::move(artha.nu);
            node->payload = text_to_payload(new_content);
            node->tau_accessed = now();

            // Update BM25 index
            if (bm25_built_) {
                if (old_text) bm25_index_.remove(id);
                bm25_index_.add(id, new_content);
            }

            // Persist the update
            storage_.update_node(id, *node);
            storage_.sync();
            return true;
        }
        return false;
    }

    // Get text from a node (if stored as payload)
    std::optional<std::string> text(NodeId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            return payload_to_text(node->payload);
        }
        return std::nullopt;
    }

    // Strengthen: increase confidence (uses WAL delta)
    void strengthen(NodeId id, float delta = 0.1f) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            Confidence new_kappa = node->kappa;
            new_kappa.observe(new_kappa.mu + delta);
            storage_.update_confidence(id, new_kappa);
        }
    }

    // Weaken: decrease confidence (uses WAL delta)
    void weaken(NodeId id, float delta = 0.1f) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            Confidence new_kappa = node->kappa;
            new_kappa.observe(new_kappa.mu - delta);
            storage_.update_confidence(id, new_kappa);
        }
    }

    // Add a tag to a node (for ε-yajna tracking)
    bool add_tag(NodeId id, const std::string& tag) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            // Check if tag already exists
            if (std::find(node->tags.begin(), node->tags.end(), tag) == node->tags.end()) {
                node->tags.push_back(tag);
                tag_index_.add(id, {tag});
                // Persist tag change
                storage_.update_node(id, *node);
            }
            return true;
        }
        return false;
    }

    // Remove a tag from a node
    bool remove_tag(NodeId id, const std::string& tag) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            auto it = std::find(node->tags.begin(), node->tags.end(), tag);
            if (it != node->tags.end()) {
                node->tags.erase(it);
                // Persist tag change
                storage_.update_node(id, *node);
            }
            return true;
        }
        return false;
    }

    // Check if node has a tag
    bool has_tag(NodeId id, const std::string& tag) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            return std::find(node->tags.begin(), node->tags.end(), tag) != node->tags.end();
        }
        return false;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Phase 7: Realm Management
    // ═══════════════════════════════════════════════════════════════════

    // Set the current realm (gates which nodes are visible in recall)
    void set_realm(const std::string& realm_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        realm_scoping_.set_current_realm(realm_name);
    }

    // Create a new realm (optional parent for hierarchy)
    void create_realm(const std::string& name, const std::string& parent = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        realm_scoping_.create_realm(name, parent);
    }

    // Get current realm name
    std::string current_realm() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return realm_scoping_.current_realm().name;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Phase 7: Provenance Access
    // ═══════════════════════════════════════════════════════════════════

    // Get provenance info for a node
    std::optional<Provenance> get_provenance(NodeId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const Provenance* p = provenance_spine_.get(id);
        if (p) return *p;
        return std::nullopt;
    }

    // Update provenance source for a node
    void set_provenance_source(NodeId id, ProvenanceSource source, const std::string& source_url = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        const Provenance* existing = provenance_spine_.get(id);
        if (existing) {
            Provenance p = *existing;
            p.source = source;
            p.source_url = source_url;
            provenance_spine_.record(id, p);
        }
    }

    // Update provenance trust score for a node
    void update_provenance_trust(NodeId id, float delta) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (config_.enable_provenance) {
            provenance_spine_.update_trust(id, delta);
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // Phase 7: Truth Maintenance (Contradictions)
    // ═══════════════════════════════════════════════════════════════════

    // Register a contradiction between two nodes
    void add_contradiction(NodeId a, NodeId b, const std::string& description,
                          float confidence = 0.5f) {
        std::lock_guard<std::mutex> lock(mutex_);
        truth_maintenance_.add_contradiction(a, b, description, confidence, now());
    }

    // Resolve a contradiction (one node "wins")
    void resolve_contradiction(NodeId a, NodeId b, NodeId winner,
                              NodeId resolution_node, const std::string& rationale) {
        std::lock_guard<std::mutex> lock(mutex_);
        truth_maintenance_.resolve(a, b, winner, resolution_node, rationale, now());
    }

    // Get all unresolved contradictions
    std::vector<Contradiction> get_unresolved_contradictions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return truth_maintenance_.get_unresolved();
    }

    // Check if a node has unresolved conflicts
    bool has_conflicts(NodeId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return truth_maintenance_.has_unresolved_conflicts(id);
    }

    // ═══════════════════════════════════════════════════════════════════
    // Phase 7: Priority 2 - RPC-exposed Component Access
    // ═══════════════════════════════════════════════════════════════════

    // Get evaluation harness for golden recall tests
    EvalHarness& eval_harness() { return eval_harness_; }
    const EvalHarness& eval_harness() const { return eval_harness_; }

    // Get review queue for human oversight
    ReviewQueue& review_queue() { return review_queue_; }
    const ReviewQueue& review_queue() const { return review_queue_; }

    // Get epiplexity tester for compression quality checks
    EpiplexityTest& epiplexity_test() { return epiplexity_test_; }
    const EpiplexityTest& epiplexity_test() const { return epiplexity_test_; }

    // Enqueue a node for review
    void enqueue_for_review(NodeId id, const std::string& context = "",
                           ReviewPriority priority = ReviewPriority::Normal) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            auto text = payload_to_text(node->payload);
            review_queue_.enqueue(id, node->node_type, text.value_or(""),
                                 context, priority, now());
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // Phase 7: Priority 3 - Pipeline Component Access
    // ═══════════════════════════════════════════════════════════════════

    // Get synthesis queue for wisdom staging
    SynthesisQueue& synthesis_queue() { return synthesis_queue_; }
    const SynthesisQueue& synthesis_queue() const { return synthesis_queue_; }

    // Get gap inquiry for knowledge gap tracking
    GapInquiry& gap_inquiry() { return gap_inquiry_; }
    const GapInquiry& gap_inquiry() const { return gap_inquiry_; }

    // Get query router for intent classification
    QueryRouter& query_router() { return query_router_; }
    const QueryRouter& query_router() const { return query_router_; }

    // Stage wisdom for synthesis (instead of direct creation)
    void stage_wisdom(NodeId id, const std::string& content) {
        std::lock_guard<std::mutex> lock(mutex_);
        synthesis_queue_.stage(id, content, now());
    }

    // Add evidence to staged wisdom
    void add_synthesis_evidence(NodeId id, Evidence::Type type, const std::string& details,
                               float weight = 1.0f, NodeId source = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        Evidence e;
        e.type = type;
        e.source = source;
        e.details = details;
        e.weight = weight;
        e.added_at = now();
        synthesis_queue_.add_evidence(id, e);
    }

    // Register a knowledge gap
    void register_gap(NodeId id, const std::string& topic, const std::string& question,
                     const std::string& context = "", GapImportance importance = GapImportance::Medium) {
        std::lock_guard<std::mutex> lock(mutex_);
        gap_inquiry_.register_gap(id, topic, question, context, importance, now());
    }

    // Get inquiry queue (gaps ready to ask)
    std::vector<KnowledgeGap> get_inquiry_queue(size_t limit = 5) {
        std::lock_guard<std::mutex> lock(mutex_);
        return gap_inquiry_.get_inquiry_queue(limit, now());
    }

    // Confidence propagation: propagate confidence change through graph
    // When a node's confidence changes, connected nodes are affected proportionally
    // decay_factor: how much propagation decays per hop (default 0.5 = halves each hop)
    // max_depth: maximum hops to propagate
    // Returns: number of nodes affected
    struct PropagationResult {
        size_t nodes_affected;
        float total_delta_applied;
        std::vector<std::pair<NodeId, float>> changes;  // (id, delta)
    };

    PropagationResult propagate_confidence(NodeId source, float delta,
                                           float decay_factor = 0.5f,
                                           size_t max_depth = 3) {
        std::lock_guard<std::mutex> lock(mutex_);
        PropagationResult result{0, 0.0f, {}};

        // BFS propagation with decay
        std::unordered_map<NodeId, float, NodeIdHash> pending_delta;
        std::unordered_set<NodeId, NodeIdHash> visited;

        // Start with connected nodes
        if (Node* source_node = storage_.get(source)) {
            for (const auto& edge : source_node->edges) {
                if (edge.weight > 0.01f) {
                    float propagated = delta * edge.weight * decay_factor;
                    pending_delta[edge.target] = propagated;
                }
            }
        }
        visited.insert(source);

        // Propagate through depths
        for (size_t depth = 0; depth < max_depth && !pending_delta.empty(); ++depth) {
            std::unordered_map<NodeId, float, NodeIdHash> next_pending;

            for (const auto& [id, d] : pending_delta) {
                if (visited.count(id) || std::abs(d) < 0.001f) continue;
                visited.insert(id);

                // Apply delta to this node
                if (Node* node = storage_.get(id)) {
                    Confidence new_kappa = node->kappa;
                    new_kappa.observe(new_kappa.mu + d);
                    storage_.update_confidence(id, new_kappa);

                    result.nodes_affected++;
                    result.total_delta_applied += std::abs(d);
                    result.changes.push_back({id, d});

                    // Propagate to next hop
                    if (depth + 1 < max_depth) {
                        for (const auto& edge : node->edges) {
                            if (!visited.count(edge.target) && edge.weight > 0.01f) {
                                float propagated = d * edge.weight * decay_factor;
                                next_pending[edge.target] += propagated;
                            }
                        }
                    }
                }
            }

            pending_delta = std::move(next_pending);
        }

        return result;
    }

    // Connect: create edge between nodes (uses WAL delta)
    void connect(NodeId from, NodeId to, EdgeType type, float weight = 1.0f) {
        std::lock_guard<std::mutex> lock(mutex_);
        storage_.add_edge(from, to, type, weight);
    }

    // ═══════════════════════════════════════════════════════════════════
    // Dynamics and lifecycle
    // ═══════════════════════════════════════════════════════════════════

    // Tick: run one cycle of dynamics with automatic health monitoring
    DynamicsReport tick() {
        std::lock_guard<std::mutex> lock(mutex_);

        // Run dynamics on graph
        DynamicsReport report = dynamics_.tick(graph_);

        // Manage storage tiers
        storage_.manage_tiers();

        // Checkpoint if needed
        Timestamp current = now();
        if (current - last_checkpoint_ > config_.checkpoint_interval_ms) {
            storage_.sync();
            last_checkpoint_ = current;
        }

        // Automatic health monitoring and recovery
        MindHealth h = health_unlocked();
        if (h.ojas() < 0.6f) {
            // Critical: emergency mode
            std::cerr << "[Mind] CRITICAL: Ojas=" << int(h.ojas() * 100)
                      << "% - soul vitality critical\n";
        } else if (h.ojas() < 0.8f) {
            // Low: attempt recovery
            std::cerr << "[Mind] Warning: Ojas=" << int(h.ojas() * 100)
                      << "% - triggering recovery\n";
            // Apply decay if stale (temporal recovery)
            if (h.temporal < 0.7f) {
                dynamics_.tick(graph_);
                last_decay_ = current;
            }
        }

        return report;
    }

    // Query by type
    std::vector<NodeId> by_type(NodeType type, size_t limit = 100) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<NodeId> results;
        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            if (node.node_type == type && results.size() < limit) {
                results.push_back(id);
            }
        });
        return results;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Session Context API (Phase 4: Context Modulation)
    // ═══════════════════════════════════════════════════════════════════

    // Record an observation for session priming
    // Called when nodes are retrieved/accessed - they prime future retrievals
    void observe_for_priming(NodeId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        observe_for_priming_unlocked(id);
    }

    // Record multiple observations (batch)
    void observe_for_priming(const std::vector<NodeId>& ids) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& id : ids) {
            observe_for_priming_unlocked(id);
        }
    }

    // Refresh session intentions from current intention nodes
    void refresh_session_intentions() {
        std::lock_guard<std::mutex> lock(mutex_);
        refresh_session_intentions_unlocked();
    }

    // Build goal basin from attractors near active intentions
    // Expands the "goal neighborhood" for basin-based boosting
    void build_goal_basin(size_t basin_size = 20) {
        std::lock_guard<std::mutex> lock(mutex_);
        build_goal_basin_unlocked(basin_size);
    }

private:
    // Unlocked versions (caller must hold mutex_)

    void observe_for_priming_unlocked(NodeId id) {
        // Skip if already in recent observations
        if (session_context_.recent_observations.count(id)) {
            return;
        }

        // Add to recent observations
        session_context_.recent_observations.insert(id);
        recent_observation_order_.push_back(id);

        // Evict oldest if over limit (FIFO)
        while (recent_observation_order_.size() > MAX_RECENT_OBSERVATIONS) {
            NodeId oldest = recent_observation_order_.front();
            recent_observation_order_.pop_front();
            session_context_.recent_observations.erase(oldest);
        }
    }

    void refresh_session_intentions_unlocked() {
        session_context_.active_intentions.clear();

        // Get all intention nodes with reasonable confidence
        storage_.for_each_hot([this](const NodeId& id, const Node& node) {
            if (node.node_type == NodeType::Intention &&
                node.kappa.effective() > 0.3f) {
                session_context_.active_intentions.insert(id);
            }
        });
    }

    void build_goal_basin_unlocked(size_t basin_size = 20) {
        session_context_.goal_basin.clear();

        if (session_context_.active_intentions.empty()) {
            return;
        }

        // For each intention, find nearby nodes via graph edges and semantic similarity
        for (const auto& intention_id : session_context_.active_intentions) {
            auto node_opt = storage_.get(intention_id);
            if (!node_opt) continue;

            // Get nodes connected by edges (1-hop neighbors)
            for (const auto& edge : node_opt->edges) {
                session_context_.goal_basin.insert(edge.target);
                if (session_context_.goal_basin.size() >= basin_size) break;
            }

            // Also include semantically similar nodes via vector search
            if (session_context_.goal_basin.size() < basin_size) {
                QuantizedVector qvec = QuantizedVector::from_float(node_opt->nu);
                auto similar = storage_.search(qvec, basin_size);
                for (const auto& [sim_id, _] : similar) {
                    // Don't add the intention itself to the basin
                    if (sim_id != intention_id) {
                        session_context_.goal_basin.insert(sim_id);
                        if (session_context_.goal_basin.size() >= basin_size) break;
                    }
                }
            }
        }
    }

public:

    // Get current session context (for external use/debugging)
    const SessionContext& session_context() const {
        return session_context_;
    }

    // Build a fresh session context from current state
    // Call this at session start or when context needs refresh
    SessionContext build_session_context() {
        refresh_session_intentions();
        build_goal_basin();
        return session_context_;
    }

    // Clear session context (for new session)
    void clear_session_context() {
        std::lock_guard<std::mutex> lock(mutex_);
        session_context_ = SessionContext{};
        recent_observation_order_.clear();
    }

    // ═══════════════════════════════════════════════════════════════════
    // Competition Config API (Phase 5: Interference/Competition)
    // ═══════════════════════════════════════════════════════════════════

    // Get current competition configuration
    const CompetitionConfig& competition_config() const {
        return competition_config_;
    }

    // Enable/disable competition
    void set_competition_enabled(bool enabled) {
        competition_config_.enabled = enabled;
    }

    // Set competition parameters
    void configure_competition(float similarity_threshold,
                               float inhibition_strength,
                               bool hard_suppression = false) {
        competition_config_.similarity_threshold = similarity_threshold;
        competition_config_.inhibition_strength = inhibition_strength;
        competition_config_.hard_suppression = hard_suppression;
    }

    // Compute coherence
    Coherence coherence() {
        std::lock_guard<std::mutex> lock(mutex_);
        return graph_.compute_coherence();
    }

    // Snapshot for recovery
    uint64_t snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        // Save BM25 index if built
        if (bm25_built_ && !bm25_path_.empty() && bm25_index_.size() > 0) {
            bm25_index_.save(bm25_path_);
        }
        storage_.sync();
        return graph_.snapshot();
    }

    // Get current state
    MindState state() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return MindState{
            graph_.current_snapshot(),
            graph_.coherence(),
            last_decay_,
            last_checkpoint_,
            storage_.total_size(),
            storage_.hot_size(),
            storage_.warm_size(),
            storage_.cold_size(),
            yantra_ && yantra_->ready()
        };
    }

    // Compute current health score across all dimensions
    // Call periodically to detect degradation before it becomes critical
    MindHealth health() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return health_unlocked();
    }

    // Health computation without lock (for use within recover())
    MindHealth health_unlocked() const {
        MindHealth h;

        // Structural health: file integrity
        // - 1.0 if unified index active and valid
        // - 0.8 if using WAL fallback
        // - 0.5 if corruption detected but healed
        h.structural = storage_.use_unified() ? 1.0f : 0.8f;

        // Semantic health: graph coherence
        // Use the coherence score from the graph
        Coherence coh = graph_.coherence();
        h.semantic = coh.tau_k();

        // Temporal health: decay and freshness
        // - Check if decay was applied recently
        // - Check if WAL is not too large
        Timestamp now_ts = now();
        float hours_since_decay = (now_ts - last_decay_) / 3600000.0f;
        float decay_health = hours_since_decay < 24.0f ? 1.0f :
                            (hours_since_decay < 72.0f ? 0.8f : 0.5f);

        // WAL size health (estimate - larger WAL = more at risk)
        size_t total = storage_.total_size();
        float wal_health = total < 10000 ? 1.0f :
                          (total < 100000 ? 0.9f : 0.8f);

        h.temporal = 0.7f * decay_health + 0.3f * wal_health;

        // Capacity health: storage utilization
        // Assume healthy if under 80% of reasonable limits
        float capacity_ratio = std::min(1.0f, total / 1000000.0f);  // 1M nodes = 100%
        h.capacity = 1.0f - (capacity_ratio * 0.5f);  // 0% = 1.0, 100% = 0.5

        return h;
    }

    // Recover from degradation - called automatically or manually
    // Returns what actions were taken
    struct RecoveryReport {
        bool decay_applied = false;
        bool integrity_repaired = false;
        bool index_rebuilt = false;
        size_t nodes_pruned = 0;
        float ojas_before = 0.0f;
        float ojas_after = 0.0f;
    };

    RecoveryReport recover() {
        std::lock_guard<std::mutex> lock(mutex_);

        RecoveryReport report;
        MindHealth h_before = health_unlocked();
        report.ojas_before = h_before.ojas();

        // 1. Temporal recovery: Apply decay if stale
        Timestamp now_ts = now();
        float hours_since_decay = (now_ts - last_decay_) / 3600000.0f;
        if (hours_since_decay > 1.0f) {
            DynamicsReport decay_report = dynamics_.tick(graph_);
            last_decay_ = now_ts;
            report.decay_applied = true;
            std::cerr << "[Recovery] Applied decay (" << hours_since_decay << "h stale)\n";
        }

        // 2. Structural recovery: Storage layer handles its own repair
        // ConnectionPool already self-heals on open and during allocation
        // Check if we fell back to WAL mode (indicates structural issues)
        if (!storage_.use_unified()) {
            std::cerr << "[Recovery] Warning: Running in WAL fallback mode\n";
            // Future: attempt to rebuild unified index from WAL
        }

        // 3. Semantic recovery: If coherence is low, run Hebbian learning pass
        Coherence coh = graph_.coherence();
        if (coh.tau_k() < 0.7f) {
            std::cerr << "[Recovery] Low coherence (" << coh.tau_k()
                      << "), would run Hebbian strengthening\n";
            // Future: hebbian_pass() to strengthen common patterns
        }

        // 4. Capacity recovery: If near limits, prune low-confidence nodes
        size_t total = storage_.total_size();
        if (total > 100000) {  // Arbitrary threshold
            std::cerr << "[Recovery] Large storage (" << total
                      << " nodes), would prune low-confidence\n";
            // Future: prune_low_confidence(threshold)
        }

        MindHealth h_after = health_unlocked();
        report.ojas_after = h_after.ojas();

        if (report.ojas_after > report.ojas_before) {
            std::cerr << "[Recovery] Ojas improved: "
                      << int(report.ojas_before * 100) << "% -> "
                      << int(report.ojas_after * 100) << "%\n";
        }

        return report;
    }

    // Access chorus for multi-voice reasoning
    HarmonyReport harmonize(const std::vector<Voice>& voices) {
        std::lock_guard<std::mutex> lock(mutex_);
        Chorus chorus(voices);
        return chorus.harmonize(graph_);
    }

    // Statistics
    size_t size() const { return storage_.total_size(); }
    size_t hot_size() const { return storage_.hot_size(); }
    size_t warm_size() const { return storage_.warm_size(); }
    size_t cold_size() const { return storage_.cold_size(); }

    // Embed text to vector (for voice queries)
    std::optional<Vector> embed(const std::string& text) {
        if (!yantra_ || !yantra_->ready()) return std::nullopt;
        Artha artha = yantra_->transform(text);
        return artha.nu;
    }

    // Access graph for read-only operations (for voice queries)
    const Graph& graph() const {
        return graph_;
    }

    // Query by node type (from storage)
    std::vector<Node> query_by_type(NodeType type) const {
        std::vector<Node> results;
        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            if (node.node_type == type) {
                results.push_back(node);
            }
        });
        return results;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Triplet API (relational knowledge)
    // ═══════════════════════════════════════════════════════════════════

    // Add a relationship between concepts (uses dictionary-encoded GraphStore)
    void connect(const std::string& subject, const std::string& predicate,
                 const std::string& object, float weight = 1.0f) {
        // Add to graph store (mmap or legacy based on config)
        if (config_.use_mmap_graph) {
            mmap_graph_store_.add(subject, predicate, object, weight);
        } else {
            graph_store_.add(subject, predicate, object, weight);
        }

        // Also maintain legacy graph_ for backward compatibility
        NodeId subj_id = find_or_create_entity(subject);
        NodeId obj_id = find_or_create_entity(object);
        graph_.add_triplet(subj_id, predicate, obj_id, weight);
    }

    // Batch connect for bulk operations
    void connect_batch(const std::vector<std::tuple<std::string, std::string, std::string, float>>& triplets) {
        for (const auto& [subject, predicate, object, weight] : triplets) {
            connect(subject, predicate, object, weight);
        }
    }

    // Query triplets: (subject?, predicate?, object?)
    // Pass empty string for wildcards
    std::vector<Triplet> query_triplets(
        const std::string& subject = "",
        const std::string& predicate = "",
        const std::string& object = "") const
    {
        std::optional<NodeId> subj_id = std::nullopt;
        std::optional<std::string> pred = std::nullopt;
        std::optional<NodeId> obj_id = std::nullopt;

        if (!subject.empty()) {
            auto id = find_entity(subject);
            if (!id) return {};  // Subject not found
            subj_id = *id;
        }
        if (!predicate.empty()) {
            pred = predicate;
        }
        if (!object.empty()) {
            auto id = find_entity(object);
            if (!id) return {};  // Object not found
            obj_id = *id;
        }

        return graph_.query_triplets(subj_id, pred, obj_id);
    }

    // Query graph store (string-based, uses dictionary-encoded store)
    // Returns: vector of (subject, predicate, object, weight)
    std::vector<std::tuple<std::string, std::string, std::string, float>>
    query_graph(const std::string& subject = "",
                const std::string& predicate = "",
                const std::string& object = "") const {
        if (config_.use_mmap_graph) {
            return mmap_graph_store_.query(subject, predicate, object);
        }
        return graph_store_.query(subject, predicate, object);
    }

    // Graph store stats
    size_t graph_entity_count() const {
        return config_.use_mmap_graph ? mmap_graph_store_.entity_count() : graph_store_.entity_count();
    }
    size_t graph_predicate_count() const {
        return config_.use_mmap_graph ? mmap_graph_store_.predicate_count() : graph_store_.predicate_count();
    }
    size_t graph_triplet_count() const {
        return config_.use_mmap_graph ? mmap_graph_store_.triplet_count() : graph_store_.triplet_count();
    }

    // Find entity by name (searches canonical and aliases)
    std::optional<NodeId> find_entity(const std::string& name) const {
        // Search entities in storage
        std::optional<NodeId> found;
        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            if (found) return;
            if (node.node_type != NodeType::Entity) return;

            // Check payload for entity name
            auto payload_opt = payload_to_text(node.payload);
            if (!payload_opt) return;
            // Simple check: entity name in payload
            if (payload_opt->find(name) != std::string::npos) {
                found = id;
            }
        });
        return found;
    }

    // Find or create entity
    NodeId find_or_create_entity(const std::string& name) {
        auto existing = find_entity(name);
        if (existing) return *existing;

        // Create new entity
        Vector embedding = Vector::zeros();
        if (yantra_ && yantra_->ready()) {
            Artha artha = yantra_->transform(name);
            embedding = artha.nu;
        }

        Node node(NodeType::Entity, embedding);
        node.delta = 0.01f;  // Entities decay slowly
        node.payload = text_to_payload(name);
        node.tags = {"entity"};

        NodeId id = node.id;
        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);
        maybe_add_bm25(id, name);

        return id;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Autonomous dynamics (daemon)
    // ═══════════════════════════════════════════════════════════════════

    // Start the background daemon
    void start_daemon(DaemonConfig config = {}) {
        daemon_ = Daemon(config);
        daemon_.attach(&graph_);
        daemon_.on_save([this]() {
            std::lock_guard<std::mutex> lock(mutex_);
            storage_.sync();
        });
        daemon_.start();
    }

    // Stop the background daemon
    void stop_daemon() {
        daemon_.stop();
    }

    // Check if daemon is running
    bool daemon_running() const {
        return daemon_.is_running();
    }

    // Get daemon stats
    Daemon::Stats daemon_stats() const {
        return daemon_.stats();
    }

    // ═══════════════════════════════════════════════════════════════════
    // Learning feedback
    // ═══════════════════════════════════════════════════════════════════

    // Record that a memory was accessed
    void feedback_used(NodeId id) {
        feedback_.used(id);
    }

    // Record that a memory was helpful (led to success)
    void feedback_helpful(NodeId id, const std::string& context = "") {
        feedback_.helpful(id, context);
        // Phase 7: Track positive feedback for utility decay
        if (config_.enable_utility_decay) {
            utility_decay_.record_feedback(id, true);
        }
    }

    // Record that a memory was misleading (led to correction)
    void feedback_misleading(NodeId id, const std::string& context = "") {
        feedback_.misleading(id, context);
        // Phase 7: Track negative feedback for utility decay
        if (config_.enable_utility_decay) {
            utility_decay_.record_feedback(id, false);
        }
    }

    // Apply pending feedback to node confidences (uses WAL delta)
    size_t apply_feedback() {
        std::lock_guard<std::mutex> lock(mutex_);

        auto deltas = feedback_.process_pending();
        size_t applied = 0;

        for (const auto& [id, delta] : deltas) {
            if (Node* node = storage_.get(id)) {
                // Apply delta via Bayesian update
                Confidence new_kappa = node->kappa;
                float new_mu = std::clamp(new_kappa.mu + delta, 0.0f, 1.0f);
                new_kappa.observe(new_mu);
                storage_.update_confidence(id, new_kappa);
                applied++;
            }
        }

        return applied;
    }

    // Get feedback stats for a node
    std::optional<FeedbackTracker::NodeStats> feedback_stats(NodeId id) const {
        return feedback_.get_stats(id);
    }

    // Get count of pending feedback
    size_t pending_feedback() const {
        return feedback_.pending_count();
    }

    // ═══════════════════════════════════════════════════════════════════
    // Hebbian Learning: "neurons that fire together wire together"
    // ═══════════════════════════════════════════════════════════════════

    // Strengthen edge between two nodes (create if doesn't exist)
    // When nodes are co-activated (retrieved together, spread together),
    // the connection between them strengthens
    void hebbian_strengthen(NodeId a, NodeId b, float strength = 0.1f) {
        std::lock_guard<std::mutex> lock(mutex_);
        hebbian_strengthen_impl(a, b, strength);
    }

    // Batch update: strengthen edges between all pairs of co-activated nodes
    // Call this when multiple nodes are retrieved together in a search
    void hebbian_update(const std::vector<NodeId>& co_activated, float strength = 0.05f) {
        if (co_activated.size() < 2) return;

        std::lock_guard<std::mutex> lock(mutex_);

        // For all pairs (i, j) where i < j, strengthen both directions
        // Bidirectional strengthening: if A activates with B, both directions learn
        for (size_t i = 0; i < co_activated.size(); ++i) {
            for (size_t j = i + 1; j < co_activated.size(); ++j) {
                hebbian_strengthen_impl(co_activated[i], co_activated[j], strength);
                hebbian_strengthen_impl(co_activated[j], co_activated[i], strength);
            }
        }
    }

    // Recall with Hebbian learning: retrieve memories and strengthen co-retrieval
    // Top results become more connected over time, enabling emergent associations
    std::vector<Recall> recall_with_learning(const std::string& query, size_t k,
                                              float threshold = 0.0f,
                                              SearchMode mode = SearchMode::Hybrid,
                                              float hebbian_strength = 0.05f,
                                              size_t hebbian_top_k = 5) {
        auto results = recall(query, k, threshold, mode);

        // Apply Hebbian learning to top results
        if (results.size() >= 2 && hebbian_top_k > 0) {
            std::vector<NodeId> co_activated;
            size_t learn_count = std::min(results.size(), hebbian_top_k);
            co_activated.reserve(learn_count);
            for (size_t i = 0; i < learn_count; ++i) {
                co_activated.push_back(results[i].id);
            }
            hebbian_update(co_activated, hebbian_strength);
        }

        return results;
    }

    // Resonate with Hebbian learning: spreading activation + connection strengthening
    std::vector<Recall> resonate_with_learning(const std::string& query, size_t k = 10,
                                                float spread_strength = 0.5f,
                                                float hebbian_strength = 0.03f) {
        auto results = resonate(query, k, spread_strength);

        // Apply Hebbian learning to resonant results
        if (results.size() >= 2) {
            std::vector<NodeId> co_activated;
            co_activated.reserve(results.size());
            for (const auto& r : results) {
                co_activated.push_back(r.id);
            }
            hebbian_update(co_activated, hebbian_strength);
        }

        return results;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Automatic synthesis (observations → wisdom)
    // ═══════════════════════════════════════════════════════════════════

    // Check for observation clusters and synthesize into wisdom
    // Returns number of new wisdom nodes created
    size_t synthesize_wisdom() {
        if (!yantra_ || !yantra_->ready()) return 0;

        // Get all episode nodes using for_each_hot
        std::vector<Node> episodes;
        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            if (node.node_type == NodeType::Episode) {
                episodes.push_back(node);
            }
        });

        if (episodes.size() < 3) return 0;  // Need at least 3 for pattern

        // Find clusters of similar episodes using embedding similarity
        std::unordered_set<NodeId, NodeIdHash> promoted;
        size_t synthesized = 0;

        for (size_t i = 0; i < episodes.size() && i < 100; ++i) {
            const auto& ep = episodes[i];
            if (promoted.count(ep.id)) continue;

            // Query for similar episodes
            QuantizedVector qvec = QuantizedVector::from_float(ep.nu);
            auto similar = storage_.search(qvec, 10);

            // Filter to just episodes with high similarity
            std::vector<const Node*> cluster;
            cluster.push_back(&ep);

            for (const auto& [id, sim] : similar) {
                if (id == ep.id) continue;
                if (sim < 0.75f) continue;  // High similarity threshold
                if (promoted.count(id)) continue;

                if (auto* node = storage_.get(id)) {
                    if (node->node_type == NodeType::Episode) {
                        cluster.push_back(node);
                    }
                }
            }

            // If we have 3+ similar episodes, synthesize
            if (cluster.size() >= 3) {
                // Create wisdom from cluster
                std::string wisdom_text = "Pattern observed (" +
                    std::to_string(cluster.size()) + " occurrences): ";

                // Use the first episode's text as the base
                std::string first_text(cluster[0]->payload.begin(),
                                       cluster[0]->payload.end());
                wisdom_text += first_text.substr(0, 200);

                // Compute average confidence from cluster
                float avg_confidence = 0.0f;
                for (const auto* node : cluster) {
                    avg_confidence += node->kappa.mu;
                }
                avg_confidence /= cluster.size();

                // Boost confidence since we have multiple observations
                float boosted_confidence = std::min(avg_confidence + 0.2f, 0.95f);

                // Create the wisdom node using transform -> Artha
                auto artha = yantra_->transform(wisdom_text);
                if (artha.nu.size() > 0) {
                    remember(NodeType::Wisdom, artha.nu,
                            Confidence(boosted_confidence),
                            std::vector<uint8_t>(wisdom_text.begin(), wisdom_text.end()));
                    synthesized++;
                }

                // Mark cluster members as promoted
                for (const auto* node : cluster) {
                    promoted.insert(node->id);
                }
            }
        }

        return synthesized;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Embedding Regeneration: Fix nodes with zero vectors
    // ═══════════════════════════════════════════════════════════════════

    // Regenerate embeddings for nodes with zero vectors
    // These are nodes created when yantra wasn't available
    // Returns number of nodes updated
    size_t regenerate_embeddings(size_t batch_size = 100) {
        if (!yantra_ || !yantra_->ready()) {
            return 0;  // Can't regenerate without yantra
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Collect nodes with zero vectors
        std::vector<std::pair<NodeId, std::string>> to_regenerate;

        // for_each_hot iterates all nodes (unified storage covers all tiers)
        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            if (node.nu.is_zero() && !node.payload.empty()) {
                std::string text(node.payload.begin(), node.payload.end());
                if (!text.empty()) {
                    to_regenerate.push_back({id, text});
                }
            }
        });

        if (to_regenerate.empty()) {
            return 0;
        }

        // Limit batch size
        if (to_regenerate.size() > batch_size) {
            to_regenerate.resize(batch_size);
        }

        // Batch transform for efficiency
        std::vector<std::string> texts;
        for (const auto& [id, text] : to_regenerate) {
            texts.push_back(text);
        }

        auto arthas = yantra_->transform_batch(texts);

        // Update nodes with new embeddings
        size_t updated = 0;
        for (size_t i = 0; i < to_regenerate.size() && i < arthas.size(); ++i) {
            const auto& [id, text] = to_regenerate[i];
            const auto& artha = arthas[i];

            if (!artha.nu.is_zero()) {
                if (Node* node = storage_.get(id)) {
                    node->nu = artha.nu;
                    updated++;
                }
            }
        }

        return updated;
    }

    // Count nodes with zero vectors (for stats/diagnostics)
    size_t count_zero_vectors() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t count = 0;

        // for_each_hot iterates all nodes (unified storage covers all tiers)
        storage_.for_each_hot([&](const NodeId&, const Node& node) {
            if (node.nu.is_zero()) count++;
        });

        return count;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Resonance: Spreading Activation (Phase 1 of resonance architecture)
    // ═══════════════════════════════════════════════════════════════════

    // Activate a seed node and spread activation through graph edges
    // Returns nodes sorted by activation level (strongest first)
    std::vector<std::pair<NodeId, float>> spread_activation(
        const NodeId& seed,
        float initial_strength = 1.0f,
        float decay_factor = 0.5f,
        int max_hops = 3)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Activation map: node -> accumulated activation
        std::unordered_map<NodeId, float, NodeIdHash> activation;

        // BFS with decaying activation
        std::queue<std::tuple<NodeId, float, int>> frontier;
        frontier.push({seed, initial_strength, 0});
        activation[seed] = initial_strength;

        while (!frontier.empty()) {
            auto [current_id, strength, hop] = frontier.front();
            frontier.pop();

            if (hop >= max_hops || strength < 0.01f) continue;

            // Get neighbors through edges
            Node* node = storage_.get(current_id);
            if (!node) continue;

            for (const auto& edge : node->edges) {
                float propagated = strength * decay_factor * edge.weight;
                activation[edge.target] += propagated;

                // Only add to frontier if significant activation
                if (propagated >= 0.05f) {
                    frontier.push({edge.target, propagated, hop + 1});
                }
            }
        }

        // Sort by activation level
        std::vector<std::pair<NodeId, float>> result(activation.begin(), activation.end());
        std::sort(result.begin(), result.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        return result;
    }

    // Resonance query: combines semantic search with spreading activation
    // Seeds from top semantic matches, spreads activation, returns resonant nodes
    std::vector<Recall> resonate(const std::string& query, size_t k = 10,
                                  float spread_strength = 0.5f)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!yantra_ || !yantra_->ready()) {
            return {};
        }

        // Get semantic seeds
        auto artha = yantra_->transform(query);
        if (artha.nu.size() == 0) return {};

        auto seeds = recall_impl(artha.nu, query, 5, 0.0f, SearchMode::Hybrid);
        if (seeds.empty()) return {};

        // Spread activation from all seeds
        std::unordered_map<NodeId, float, NodeIdHash> total_activation;

        for (const auto& seed : seeds) {
            // Weight by semantic similarity
            float seed_strength = spread_strength * seed.relevance;

            std::queue<std::tuple<NodeId, float, int>> frontier;
            frontier.push({seed.id, seed_strength, 0});

            std::unordered_set<NodeId, NodeIdHash> visited;

            while (!frontier.empty()) {
                auto [current_id, strength, hop] = frontier.front();
                frontier.pop();

                if (hop >= 3 || strength < 0.01f) continue;
                if (visited.count(current_id)) continue;
                visited.insert(current_id);

                total_activation[current_id] += strength;

                Node* node = storage_.get(current_id);
                if (!node) continue;

                for (const auto& edge : node->edges) {
                    float propagated = strength * 0.5f * edge.weight;
                    if (propagated >= 0.01f) {
                        frontier.push({edge.target, propagated, hop + 1});
                    }
                }
            }
        }

        // Combine semantic scores with activation
        std::vector<Recall> results;
        for (const auto& [id, activation] : total_activation) {
            Node* node = storage_.get(id);
            if (!node) continue;

            auto text = payload_to_text(node->payload);

            // Find original semantic score if it was a seed
            float semantic_score = 0.0f;
            for (const auto& seed : seeds) {
                if (seed.id == id) {
                    semantic_score = seed.relevance;
                    break;
                }
            }

            // Resonance score combines semantic and activation
            float resonance_score = 0.6f * semantic_score + 0.4f * activation;

            results.push_back(Recall{
                id,
                resonance_score,       // similarity
                resonance_score,       // relevance
                0.0f,                  // epiplexity (computed later if needed)
                node->node_type,
                node->kappa,
                node->tau_created,
                node->tau_accessed,
                node->payload,
                text.value_or("")
            });
        }

        // Sort by relevance
        std::sort(results.begin(), results.end(),
                  [](const auto& a, const auto& b) { return a.relevance > b.relevance; });

        if (results.size() > k) {
            results.resize(k);
        }

        return results;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Attractor Dynamics (Phase 2 of resonance architecture)
    // ═══════════════════════════════════════════════════════════════════

    // An attractor is a high-confidence, well-connected node that
    // pulls similar nodes toward it (conceptual gravity well)
    struct Attractor {
        NodeId id;
        float strength;          // Attractor strength (confidence * connectivity)
        std::string label;       // First 50 chars of content for identification
        size_t basin_size = 0;   // Number of nodes in this attractor's basin
    };

    // Find natural attractors in the graph
    // Attractors are nodes with: high confidence + many connections + stable (old)
    std::vector<Attractor> find_attractors(size_t max_attractors = 10,
                                            float min_confidence = 0.6f,
                                            size_t min_edges = 2) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<Attractor> candidates;
        Timestamp current = now();

        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            // Skip low-confidence nodes
            if (node.kappa.effective() < min_confidence) return;

            // Skip poorly connected nodes
            if (node.edges.size() < min_edges) return;

            // Calculate attractor strength:
            // - confidence contributes (0.4)
            // - connectivity contributes (0.3)
            // - age/stability contributes (0.3)
            float confidence_score = node.kappa.effective();

            // Connectivity: log-scaled to avoid over-weighting highly connected nodes
            float connectivity_score = std::min(std::log2(1.0f + node.edges.size()) / 4.0f, 1.0f);

            // Age: older nodes are more stable attractors
            float age_days = static_cast<float>(current - node.tau_created) / 86400000.0f;
            float age_score = std::min(age_days / 30.0f, 1.0f);  // Max at 30 days

            float strength = 0.4f * confidence_score +
                            0.3f * connectivity_score +
                            0.3f * age_score;

            // Extract label from payload
            auto text = payload_to_text(node.payload);
            std::string label = text ? text->substr(0, 50) : "";

            candidates.push_back({id, strength, label, 0});
        });

        // Sort by strength (strongest first)
        std::sort(candidates.begin(), candidates.end(),
                  [](const Attractor& a, const Attractor& b) {
                      return a.strength > b.strength;
                  });

        // Limit to max_attractors
        if (candidates.size() > max_attractors) {
            candidates.resize(max_attractors);
        }

        return candidates;
    }

    // Compute which attractor a node is pulled toward
    // Returns the attractor ID and the pull strength (0-1)
    std::optional<std::pair<NodeId, float>> compute_attractor_pull(
        NodeId node_id,
        const std::vector<Attractor>& attractors)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return compute_attractor_pull_impl(node_id, attractors);
    }

    // Settle a set of nodes toward their attractors
    // This strengthens connections between nodes and their attractors
    // Returns number of nodes that settled
    size_t settle_toward_attractors(const std::vector<Attractor>& attractors,
                                     float settle_strength = 0.02f) {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t settled = 0;

        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            // Skip nodes that are themselves attractors
            bool is_attractor = false;
            for (const auto& attr : attractors) {
                if (attr.id == id) {
                    is_attractor = true;
                    break;
                }
            }
            if (is_attractor) return;

            // Find which attractor this node is pulled toward
            auto pull = compute_attractor_pull_impl(id, attractors);
            if (!pull) return;

            auto [attractor_id, pull_strength] = *pull;

            // Strengthen connection toward attractor
            // Weight by pull strength (stronger pull = stronger connection)
            float actual_strength = settle_strength * pull_strength;
            if (actual_strength >= 0.01f) {
                hebbian_strengthen_impl(id, attractor_id, actual_strength);
                settled++;
            }
        });

        return settled;
    }

    // Assign nodes to attractor basins
    // Returns map of attractor_id -> list of node_ids in that basin
    std::unordered_map<NodeId, std::vector<NodeId>, NodeIdHash>
    compute_basins(const std::vector<Attractor>& attractors) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::unordered_map<NodeId, std::vector<NodeId>, NodeIdHash> basins;

        // Initialize empty basins for each attractor
        for (const auto& attr : attractors) {
            basins[attr.id] = {};
        }

        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            // Skip attractors themselves
            bool is_attractor = false;
            for (const auto& attr : attractors) {
                if (attr.id == id) {
                    is_attractor = true;
                    break;
                }
            }
            if (is_attractor) return;

            // Find which attractor this node is pulled toward
            auto pull = compute_attractor_pull_impl(id, attractors);
            if (pull) {
                basins[pull->first].push_back(id);
            }
        });

        return basins;
    }

    // Run one round of attractor dynamics:
    // 1. Find attractors
    // 2. Settle nodes toward attractors
    // 3. Compute basins
    // Returns a report of what happened
    struct AttractorReport {
        size_t attractor_count = 0;
        size_t nodes_settled = 0;
        std::vector<std::pair<std::string, size_t>> basin_sizes;  // label -> size
    };

    AttractorReport run_attractor_dynamics(size_t max_attractors = 10,
                                            float settle_strength = 0.02f) {
        AttractorReport report;

        // Step 1: Find attractors
        auto attractors = find_attractors(max_attractors);
        report.attractor_count = attractors.size();

        if (attractors.empty()) return report;

        // Step 2: Settle nodes toward attractors
        report.nodes_settled = settle_toward_attractors(attractors, settle_strength);

        // Step 3: Compute basins for reporting
        auto basins = compute_basins(attractors);
        for (const auto& attr : attractors) {
            report.basin_sizes.push_back({
                attr.label,
                basins[attr.id].size()
            });
        }

        return report;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Epiplexity: Learnable Structure Metric (inspired by Finzi et al. 2026)
    // ═══════════════════════════════════════════════════════════════════
    //
    // Epiplexity measures how much "learnable structure" a memory contains
    // for a bounded observer (like Claude). High epiplexity = the pattern
    // can be reconstructed from minimal injection.
    //
    // Components:
    // 1. Attractor proximity: closer to structure = more reconstructable
    // 2. Compression: title/content ratio = information density
    // 3. Integration: edge connections = discovered relationships
    // 4. Confidence: well-learned patterns
    //
    // This enables: inject seeds (high epiplexity), Claude grows trees.

    // Compute epiplexity for a single node given known attractors
    // Caller must hold mutex
    //
    // Epiplexity = learnable structure for bounded observers
    // High epiplexity = can be reconstructed from minimal injection
    float compute_epiplexity_impl(const Node& node,
                                   const std::vector<Attractor>& attractors) {
        // 1. Structural proximity (attractor-based if available, else confidence)
        float structure = 0.5f;  // Default: moderate structure
        if (!attractors.empty()) {
            float best_pull = 0.0f;
            for (const auto& attr : attractors) {
                Node* attr_node = storage_.get(attr.id);
                if (!attr_node) continue;
                float similarity = node.nu.cosine(attr_node->nu);
                float pull = attr.strength * similarity;
                best_pull = std::max(best_pull, pull);
            }
            // Scale pull to 0-1 range (typical pulls are 0.2-0.8)
            structure = std::min(1.0f, best_pull * 1.5f);
        } else {
            // No attractors: use confidence as proxy for structure
            structure = node.kappa.effective() * 0.8f;
        }

        // 2. Compression ratio: title vs content length
        // Well-named memories are higher epiplexity (can be reconstructed from title)
        float compression = 0.5f;
        if (!node.payload.empty()) {
            std::string content(node.payload.begin(), node.payload.end());
            size_t title_end = content.find('\n');
            if (title_end == std::string::npos || title_end > 80) {
                title_end = std::min(content.length(), size_t(80));
            }
            float title_len = static_cast<float>(title_end);
            float content_len = static_cast<float>(content.length());
            // Ratio scaled: title of 50 chars for 500 char content = 1.0
            compression = std::min(1.0f, (title_len / content_len) * 10.0f);
        }

        // 3. Integration: edges indicate discovered relationships
        // Even 1 edge shows some integration; scale logarithmically
        float integration = 0.3f;  // Base score for existing
        if (!node.edges.empty()) {
            float edge_sum = 0.0f;
            for (const auto& edge : node.edges) {
                edge_sum += edge.weight;
            }
            // Log scale: 1 edge=0.4, 3 edges=0.7, 10 edges=0.95
            integration = 0.3f + 0.7f * std::tanh(edge_sum / 2.0f);
        }

        // 4. Confidence: well-learned patterns are high epiplexity
        float confidence = node.kappa.effective();

        // Weighted combination (Swarm consensus: rebalanced for regenerability)
        // Integration elevated: edges enable associative reconstruction
        // Compression elevated: information density critical for bounded observers
        // Structure: 30%, Confidence: 25%, Integration: 25%, Compression: 20%
        float epiplexity = 0.30f * structure +
                          0.25f * confidence +
                          0.25f * integration +
                          0.20f * compression;

        return std::min(1.0f, epiplexity);
    }

    // Compute epiplexity for a node (public, thread-safe)
    float compute_epiplexity(const NodeId& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        Node* node = storage_.get(id);
        if (!node) return 0.0f;

        auto attractors = find_attractors_unlocked(5);
        return compute_epiplexity_impl(*node, attractors);
    }

    // Compute epiplexity for all hot nodes and return statistics
    struct EpiplexityStats {
        float mean = 0.0f;
        float median = 0.0f;
        float min = 1.0f;
        float max = 0.0f;
        size_t count = 0;
        std::vector<std::pair<NodeId, float>> top_nodes;  // Top 10 by epiplexity
    };

    EpiplexityStats compute_soul_epiplexity() {
        std::lock_guard<std::mutex> lock(mutex_);

        auto attractors = find_attractors_unlocked(10);
        std::vector<std::pair<NodeId, float>> node_epiplexity;

        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            float epi = compute_epiplexity_impl(node, attractors);
            node_epiplexity.push_back({id, epi});
        });

        if (node_epiplexity.empty()) return {};

        EpiplexityStats stats;
        stats.count = node_epiplexity.size();

        // Sort by epiplexity descending
        std::sort(node_epiplexity.begin(), node_epiplexity.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        // Top 10
        for (size_t i = 0; i < std::min(size_t(10), node_epiplexity.size()); ++i) {
            stats.top_nodes.push_back(node_epiplexity[i]);
        }

        // Statistics
        float sum = 0.0f;
        for (const auto& [id, epi] : node_epiplexity) {
            sum += epi;
            stats.min = std::min(stats.min, epi);
            stats.max = std::max(stats.max, epi);
        }
        stats.mean = sum / static_cast<float>(stats.count);
        stats.median = node_epiplexity[stats.count / 2].second;

        return stats;
    }

    // Enhanced resonate that uses attractor dynamics
    // First finds attractors, then resonates within attractor basins
    std::vector<Recall> resonate_with_attractors(const std::string& query,
                                                   size_t k = 10,
                                                   float spread_strength = 0.5f) {
        // First get regular resonance results
        auto results = resonate(query, k * 2, spread_strength);
        if (results.empty()) return results;

        std::lock_guard<std::mutex> lock(mutex_);

        // Find attractors
        auto attractors = find_attractors(5);
        if (attractors.empty()) {
            // No attractors found, return regular results
            if (results.size() > k) results.resize(k);
            return results;
        }

        // Compute basins
        auto basins = compute_basins(attractors);

        // Find which attractor the top result belongs to
        auto pull = compute_attractor_pull_impl(results[0].id, attractors);
        if (!pull) {
            if (results.size() > k) results.resize(k);
            return results;
        }

        NodeId primary_attractor = pull->first;

        // Boost results that are in the same basin
        for (auto& result : results) {
            auto result_pull = compute_attractor_pull_impl(result.id, attractors);
            if (result_pull && result_pull->first == primary_attractor) {
                // Same basin - boost relevance
                result.relevance *= 1.2f;
            }
        }

        // Re-sort by adjusted relevance
        std::sort(results.begin(), results.end(),
                  [](const Recall& a, const Recall& b) {
                      return a.relevance > b.relevance;
                  });

        if (results.size() > k) results.resize(k);
        return results;
    }

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 6: Full Resonance - All Mechanisms Working Together
    // ═══════════════════════════════════════════════════════════════════
    //
    // This is the culmination of the resonance architecture:
    // 1. Session Priming (Phase 4): Context modulates which patterns activate
    // 2. Spreading Activation (Phase 1): Activation spreads through graph edges
    // 3. Attractor Dynamics (Phase 2): Results pulled toward conceptual gravity wells
    // 4. Lateral Inhibition (Phase 5): Similar patterns compete, winners suppress losers
    // 5. Hebbian Learning (Phase 3): Co-activated nodes strengthen connections
    //
    // The soul doesn't just search - it resonates.

    std::vector<Recall> full_resonate(const std::string& query,
                                       size_t k = 10,
                                       float spread_strength = 0.5f,
                                       float hebbian_strength = 0.03f) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!yantra_ || !yantra_->ready()) {
            return {};
        }

        // Phase 4: Refresh session context (priming)
        // Recent observations and active intentions bias retrieval
        refresh_session_intentions_unlocked();
        build_goal_basin_unlocked();

        // Transform query to embedding
        auto artha = yantra_->transform(query);
        if (artha.nu.size() == 0) return {};

        // Phase 4: Get semantic seeds with session priming
        // The session context biases which nodes are initially retrieved
        auto seeds = recall_impl(artha.nu, query, 5, 0.0f, SearchMode::Hybrid, &session_context_);
        if (seeds.empty()) return {};

        // Phase 2: Find attractors (conceptual gravity wells)
        auto attractors = find_attractors_unlocked(5);

        // Phase 1: Spread activation from all seeds
        // Activation flows through graph edges, discovering related concepts
        std::unordered_map<NodeId, float, NodeIdHash> activation;

        for (const auto& seed : seeds) {
            float seed_strength = spread_strength * seed.relevance;

            std::queue<std::tuple<NodeId, float, int>> frontier;
            frontier.push({seed.id, seed_strength, 0});

            std::unordered_set<NodeId, NodeIdHash> visited;

            while (!frontier.empty()) {
                auto [current_id, strength, hop] = frontier.front();
                frontier.pop();

                if (hop >= 3 || strength < 0.01f) continue;
                if (visited.count(current_id)) continue;
                visited.insert(current_id);

                activation[current_id] += strength;

                Node* node = storage_.get(current_id);
                if (!node) continue;

                for (const auto& edge : node->edges) {
                    float propagated = strength * 0.5f * edge.weight;
                    if (propagated >= 0.02f) {
                        frontier.push({edge.target, propagated, hop + 1});
                    }
                }
            }
        }

        // Merge seed results with activation results
        // Seeds have high relevance, activated nodes have activation scores
        Timestamp current = now();
        std::vector<Recall> results;
        std::unordered_set<NodeId, NodeIdHash> seen;

        // Add seeds first
        for (const auto& seed : seeds) {
            results.push_back(seed);
            seen.insert(seed.id);
        }

        // Add activated nodes not in seeds
        for (const auto& [id, act] : activation) {
            if (seen.count(id)) continue;
            if (act < 0.1f) continue;  // Minimum activation threshold

            Node* node = storage_.get(id);
            if (!node) continue;

            auto text = payload_to_text(node->payload);
            if (!text || text->size() < 3) continue;

            // Compute semantic similarity to query
            QuantizedVector qquery = QuantizedVector::from_float(artha.nu);
            QuantizedVector qnode = QuantizedVector::from_float(node->nu);
            float similarity = qquery.cosine_approx(qnode);

            // Combine activation with semantic relevance
            float relevance = session_relevance(similarity, *node, current,
                                                scoring_config_, &session_context_);
            relevance = relevance * 0.6f + act * 0.4f;  // Blend semantic + activation

            Recall r;
            r.id = id;
            r.similarity = similarity;
            r.relevance = relevance;
            r.epiplexity = compute_epiplexity_impl(*node, attractors);
            r.type = node->node_type;
            r.confidence = node->kappa;
            r.created = node->tau_created;
            r.accessed = node->tau_accessed;
            r.payload = node->payload;
            r.text = *text;
            r.qnu = qnode;
            r.has_embedding = true;

            results.push_back(std::move(r));
            seen.insert(id);
        }

        // Compute epiplexity for seed results too
        for (auto& seed : results) {
            if (seed.epiplexity == 0.0f) {
                if (Node* node = storage_.get(seed.id)) {
                    seed.epiplexity = compute_epiplexity_impl(*node, attractors);
                }
            }
        }

        // ε-modulated relevance: high-epiplexity memories are better seeds
        // Formula: relevance × (1 + α × ε) where α = 0.5
        // Also apply safety gate: safe_ε = sqrt(confidence × epiplexity)
        // This prevents high-ε false memories from dominating
        constexpr float EPIPLEXITY_BOOST_ALPHA = 0.5f;
        for (auto& r : results) {
            float safe_epsilon = std::sqrt(r.confidence.effective() * r.epiplexity);
            r.relevance *= (1.0f + EPIPLEXITY_BOOST_ALPHA * safe_epsilon);
        }

        // Phase 2: Boost results in same attractor basin as top result
        if (!attractors.empty() && !results.empty()) {
            auto top_pull = compute_attractor_pull_impl(results[0].id, attractors);
            if (top_pull) {
                NodeId primary_attractor = top_pull->first;
                for (auto& result : results) {
                    auto pull = compute_attractor_pull_impl(result.id, attractors);
                    if (pull && pull->first == primary_attractor) {
                        result.relevance *= 1.15f;  // Basin coherence boost
                    }
                }
            }
        }

        // Sort by relevance before competition
        std::sort(results.begin(), results.end(),
                  [](const Recall& a, const Recall& b) {
                      return a.relevance > b.relevance;
                  });

        // Phase 5: Lateral inhibition (competition)
        // Similar patterns compete - winners suppress losers
        if (competition_config_.enabled && results.size() >= 2) {
            apply_lateral_inhibition(results);
        }

        // Limit to k results
        if (results.size() > k) {
            results.resize(k);
        }

        // Phase 3: Hebbian learning - strengthen connections between co-activated nodes
        // "Neurons that fire together, wire together"
        if (results.size() >= 2 && hebbian_strength > 0.0f) {
            std::vector<NodeId> co_activated;
            co_activated.reserve(std::min(results.size(), size_t(5)));
            for (size_t i = 0; i < std::min(results.size(), size_t(5)); ++i) {
                co_activated.push_back(results[i].id);
            }
            hebbian_update_unlocked(co_activated, hebbian_strength);
        }

        // Phase 4: Record for future priming
        // The results of this query become context for future queries
        for (const auto& r : results) {
            observe_for_priming_unlocked(r.id);
        }

        // Clear embeddings before returning
        for (auto& r : results) {
            r.has_embedding = false;
        }

        return results;
    }

private:
    // Internal recall implementation with soul-aware scoring
    std::vector<Recall> recall_impl(const Vector& query, const std::string& query_text,
                                    size_t k, float threshold, SearchMode mode,
                                    const SessionContext* session = nullptr)
    {
        // Sync from shared consciousness to see other processes' observations
        // This is the "Atman sees Brahman" moment - we align with shared truth
        sync_from_shared_field();

        Timestamp current = now();
        std::vector<std::pair<NodeId, float>> candidates;

        // Get candidates based on search mode
        if (mode == SearchMode::Dense || mode == SearchMode::Hybrid) {
            QuantizedVector qquery = QuantizedVector::from_float(query);
            auto dense = storage_.search(qquery, k * 4);  // Get extra for fusion/filtering
            candidates = dense;
        }

        if (mode == SearchMode::Sparse || mode == SearchMode::Hybrid) {
            if (!query_text.empty()) {
                ensure_bm25_index();  // Lazy build on first use
                auto sparse = bm25_index_.search(query_text, k * 4);

                if (mode == SearchMode::Hybrid && !candidates.empty()) {
                    // RRF fusion of dense and sparse results
                    candidates = rrf_fusion(candidates, sparse, 60.0f, 0.7f);
                } else if (mode == SearchMode::Sparse) {
                    candidates = sparse;
                }
            }
        }

        // Phase 7: Filter candidates by realm visibility
        if (config_.enable_realm_scoping && !candidates.empty()) {
            auto filtered = realm_scoping_.filter_by_realm(candidates);
            candidates = std::move(filtered);
        }

        // Score candidates with soul-aware relevance (optionally session-primed)
        std::vector<Recall> results;
        for (const auto& [id, base_score] : candidates) {
            if (Node* node = storage_.get(id)) {
                // Get semantic similarity (for dense) or use base score (for sparse)
                float similarity = base_score;
                if (mode == SearchMode::Hybrid) {
                    // For hybrid, base_score is RRF score, not similarity
                    // Compute actual similarity if we have the embedding
                    QuantizedVector qquery = QuantizedVector::from_float(query);
                    QuantizedVector qnode = QuantizedVector::from_float(node->nu);
                    similarity = qquery.cosine_approx(qnode);
                }

                if (similarity < threshold) continue;

                // Soul-aware relevance scoring with optional session priming
                float relevance = session_relevance(similarity, *node, current,
                                                    scoring_config_, session);

                // Skip nodes without text content
                auto text = payload_to_text(node->payload);
                if (!text || text->size() < 3) continue;  // Skip empty/corrupted payloads

                Recall r;
                r.id = id;
                r.similarity = similarity;
                r.relevance = relevance;
                r.type = node->node_type;
                r.confidence = node->kappa;
                r.created = node->tau_created;
                r.accessed = node->tau_accessed;
                r.payload = node->payload;
                r.text = *text;

                // Store embedding for competition (Phase 5)
                r.qnu = QuantizedVector::from_float(node->nu);
                r.has_embedding = true;

                results.push_back(std::move(r));
            }
        }

        // Sort by relevance (soul-aware), not raw similarity
        std::sort(results.begin(), results.end(),
            [](const Recall& a, const Recall& b) {
                return a.relevance > b.relevance;
            });

        // Phase 5: Apply lateral inhibition (winner-take-all competition)
        // Similar patterns compete - winners suppress losers
        if (competition_config_.enabled && results.size() >= 2) {
            apply_lateral_inhibition(results);
        }

        // Phase 7: Apply attractor dampening (reduce over-retrieved nodes)
        if (config_.enable_attractor_dampener && !results.empty()) {
            std::vector<std::pair<NodeId, float>> id_scores;
            for (const auto& r : results) {
                id_scores.push_back({r.id, r.relevance});
            }
            auto dampened = attractor_dampener_.dampen_results(id_scores, current);

            // Rebuild results with dampened order
            std::unordered_map<NodeId, float, NodeIdHash> dampened_scores;
            for (const auto& [id, score] : dampened) {
                dampened_scores[id] = score;
            }
            for (auto& r : results) {
                if (dampened_scores.count(r.id)) {
                    r.relevance = dampened_scores[r.id];
                }
            }
            std::sort(results.begin(), results.end(),
                [](const Recall& a, const Recall& b) {
                    return a.relevance > b.relevance;
                });
        }

        // Limit to k results
        if (results.size() > k) {
            results.resize(k);
        }

        // Phase 7: Record recalls for utility tracking and dampening
        if (config_.enable_utility_decay || config_.enable_attractor_dampener) {
            for (const auto& r : results) {
                if (config_.enable_utility_decay) {
                    utility_decay_.record_recall(r.id, r.relevance, current);
                }
                if (config_.enable_attractor_dampener) {
                    attractor_dampener_.record_retrieval(r.id, r.relevance, current);
                }
            }
        }

        // Phase 7: Annotate conflicts in results
        if (config_.enable_truth_maintenance && !results.empty()) {
            // Build ID-to-score map for annotation
            std::vector<std::pair<NodeId, float>> id_scores;
            for (const auto& r : results) {
                id_scores.push_back({r.id, r.relevance});
            }
            auto annotated = truth_maintenance_.annotate_conflicts(id_scores);

            // Transfer conflict info to results
            for (size_t i = 0; i < results.size() && i < annotated.size(); ++i) {
                results[i].has_conflict = annotated[i].has_conflict;
                results[i].conflicting_nodes = annotated[i].conflicting_nodes;
            }
        }

        // Clear embeddings before returning (not needed by caller)
        for (auto& r : results) {
            r.qnu = QuantizedVector{};
            r.has_embedding = false;
        }

        return results;
    }

public:
    // Convert text to payload (simple UTF-8 encoding)
    static std::vector<uint8_t> text_to_payload(const std::string& text) {
        return std::vector<uint8_t>(text.begin(), text.end());
    }

    // Convert payload back to text
    static std::optional<std::string> payload_to_text(const std::vector<uint8_t>& payload) {
        if (payload.empty()) return std::nullopt;
        return std::string(payload.begin(), payload.end());
    }

private:
    // Phase 5: Apply lateral inhibition to recall results
    // Winners suppress similar losers - prevents redundant results
    void apply_lateral_inhibition(std::vector<Recall>& results) {
        const size_t n = results.size();
        if (n < 2) return;

        // Compute pairwise similarities (upper triangular)
        // Size: n*(n-1)/2 entries
        std::vector<float> similarities;
        similarities.reserve(n * (n - 1) / 2);

        for (size_t i = 0; i < n; ++i) {
            for (size_t j = i + 1; j < n; ++j) {
                float sim = 0.0f;
                if (results[i].has_embedding && results[j].has_embedding) {
                    sim = results[i].qnu.cosine_approx(results[j].qnu);
                }
                similarities.push_back(sim);
            }
        }

        // Extract relevances for compute_inhibition
        std::vector<float> relevances;
        relevances.reserve(n);
        for (const auto& r : results) {
            relevances.push_back(r.relevance);
        }

        // Compute which nodes get inhibited
        auto inhibition = compute_inhibition(similarities, relevances, n, competition_config_);

        if (inhibition.suppressed_indices.empty()) {
            return;  // No competition occurred
        }

        if (competition_config_.hard_suppression) {
            // Hard WTA: remove suppressed results entirely
            // Sort indices descending to remove from back to front
            std::vector<size_t> to_remove = inhibition.suppressed_indices;
            std::sort(to_remove.begin(), to_remove.end(), std::greater<size_t>());

            for (size_t idx : to_remove) {
                if (idx < results.size()) {
                    results.erase(results.begin() + static_cast<long>(idx));
                }
            }
        } else {
            // Soft inhibition: reduce relevance of suppressed results
            for (size_t i = 0; i < inhibition.suppressed_indices.size(); ++i) {
                size_t idx = inhibition.suppressed_indices[i];
                float penalty = inhibition.penalties[i];
                if (idx < results.size()) {
                    results[idx].relevance *= (1.0f - penalty);
                }
            }

            // Re-sort after applying penalties
            std::sort(results.begin(), results.end(),
                [](const Recall& a, const Recall& b) {
                    return a.relevance > b.relevance;
                });
        }
    }

    // Hebbian strengthening implementation (caller must hold mutex_)
    // Finds existing edge and strengthens it, or creates new edge
    void hebbian_strengthen_impl(NodeId from, NodeId to, float strength) {
        Node* node = storage_.get(from);
        if (!node) return;

        // Find existing Similar edge to target
        for (auto& edge : node->edges) {
            if (edge.target == to && edge.type == EdgeType::Similar) {
                // Strengthen existing edge (cap at 1.0)
                edge.weight = std::min(edge.weight + strength, 1.0f);
                return;
            }
        }

        // No existing edge - create new one with initial strength
        // Use storage_.add_edge for WAL persistence
        storage_.add_edge(from, to, EdgeType::Similar, strength);
    }

    // Hebbian batch update without locking (caller must hold mutex_)
    void hebbian_update_unlocked(const std::vector<NodeId>& co_activated, float strength) {
        if (co_activated.size() < 2) return;

        for (size_t i = 0; i < co_activated.size(); ++i) {
            for (size_t j = i + 1; j < co_activated.size(); ++j) {
                hebbian_strengthen_impl(co_activated[i], co_activated[j], strength);
                hebbian_strengthen_impl(co_activated[j], co_activated[i], strength);
            }
        }
    }

    // Find attractors without locking (caller must hold mutex_)
    std::vector<Attractor> find_attractors_unlocked(size_t max_attractors = 10,
                                                     float min_confidence = 0.6f,
                                                     size_t min_edges = 2) {
        std::vector<Attractor> candidates;
        Timestamp current = now();

        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            if (node.kappa.effective() < min_confidence) return;
            if (node.edges.size() < min_edges) return;

            float confidence_score = node.kappa.effective();
            float connectivity_score = std::min(std::log2(1.0f + node.edges.size()) / 4.0f, 1.0f);
            float age_days = static_cast<float>(current - node.tau_created) / 86400000.0f;
            float age_score = std::min(age_days / 30.0f, 1.0f);

            float strength = 0.4f * confidence_score +
                            0.3f * connectivity_score +
                            0.3f * age_score;

            auto text = payload_to_text(node.payload);
            std::string label = text ? text->substr(0, 50) : "";

            candidates.push_back({id, strength, label, 0});
        });

        std::sort(candidates.begin(), candidates.end(),
                  [](const Attractor& a, const Attractor& b) {
                      return a.strength > b.strength;
                  });

        if (candidates.size() > max_attractors) {
            candidates.resize(max_attractors);
        }

        return candidates;
    }

    // Internal: compute attractor pull without taking lock
    std::optional<std::pair<NodeId, float>>
    compute_attractor_pull_impl(NodeId node_id, const std::vector<Attractor>& attractors) {
        Node* node = storage_.get(node_id);
        if (!node || attractors.empty()) return std::nullopt;

        // Find the attractor with strongest pull on this node
        // Pull = attractor_strength * semantic_similarity
        NodeId best_attractor;
        float best_pull = 0.0f;

        for (const auto& attr : attractors) {
            Node* attr_node = storage_.get(attr.id);
            if (!attr_node) continue;

            // Semantic similarity between node and attractor
            float similarity = node->nu.cosine(attr_node->nu);

            // Pull is attractor strength weighted by similarity
            float pull = attr.strength * similarity;

            if (pull > best_pull) {
                best_pull = pull;
                best_attractor = attr.id;
            }
        }

        if (best_pull < 0.1f) return std::nullopt;  // Too weak

        return std::make_pair(best_attractor, best_pull);
    }

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 2 CORE: Scalable Index Structures (100M+ nodes)
    // ═══════════════════════════════════════════════════════════════════

    // Reverse edge index: O(1) lookup of incoming edges
    // Updated incrementally on edge add/remove
    struct ReverseEdge {
        NodeId source;
        EdgeType type;
        float weight;
    };
    std::unordered_map<NodeId, std::vector<ReverseEdge>, NodeIdHash> reverse_edges_;

    // Temporal index: O(log B) range queries, B = number of buckets
    static constexpr uint64_t TEMPORAL_BUCKET_MS = 3600000;  // 1 hour
    std::map<uint64_t, std::vector<std::pair<Timestamp, NodeId>>> temporal_buckets_;

    uint64_t temporal_bucket_id(Timestamp ts) const {
        return ts / TEMPORAL_BUCKET_MS;
    }

    // LSH Forest for O(1) average similarity search
    static constexpr size_t LSH_NUM_TREES = 8;
    static constexpr size_t LSH_HASH_BITS = 12;
    std::vector<std::vector<std::vector<float>>> lsh_hyperplanes_;  // [tree][plane][dim]
    std::vector<std::unordered_map<uint32_t, std::vector<NodeId>>> lsh_buckets_;
    bool lsh_initialized_ = false;

    void init_lsh(size_t dim) {
        if (lsh_initialized_) return;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<float> dist(0.0f, 1.0f);

        lsh_hyperplanes_.resize(LSH_NUM_TREES);
        lsh_buckets_.resize(LSH_NUM_TREES);

        for (size_t t = 0; t < LSH_NUM_TREES; ++t) {
            lsh_hyperplanes_[t].resize(LSH_HASH_BITS);
            for (size_t h = 0; h < LSH_HASH_BITS; ++h) {
                lsh_hyperplanes_[t][h].resize(dim);
                for (size_t d = 0; d < dim; ++d) {
                    lsh_hyperplanes_[t][h][d] = dist(gen);
                }
            }
        }
        lsh_initialized_ = true;
    }

    uint32_t lsh_hash(const Vector& emb, size_t tree) {
        uint32_t hash = 0;
        for (size_t h = 0; h < LSH_HASH_BITS; ++h) {
            float dot = 0.0f;
            for (size_t d = 0; d < emb.size() && d < lsh_hyperplanes_[tree][h].size(); ++d) {
                dot += emb[d] * lsh_hyperplanes_[tree][h][d];
            }
            if (dot > 0) hash |= (1u << h);
        }
        return hash;
    }

public:
    // Index maintenance - call on node operations
    void index_node_insert(const NodeId& id, const Node& node) {
        // Reverse edges
        for (const auto& edge : node.edges) {
            reverse_edges_[edge.target].push_back({id, edge.type, edge.weight});
        }

        // Temporal index
        uint64_t bid = temporal_bucket_id(node.tau_created);
        auto& bucket = temporal_buckets_[bid];
        auto it = std::lower_bound(bucket.begin(), bucket.end(),
            std::make_pair(node.tau_created, id));
        bucket.insert(it, {node.tau_created, id});

        // LSH
        if (node.nu.size() > 0) {
            init_lsh(node.nu.size());
            for (size_t t = 0; t < LSH_NUM_TREES; ++t) {
                uint32_t h = lsh_hash(node.nu, t);
                lsh_buckets_[t][h].push_back(id);
            }
        }
    }

    void index_edge_add(const NodeId& from, const Edge& edge) {
        reverse_edges_[edge.target].push_back({from, edge.type, edge.weight});
    }

    // Rebuild Phase 2 indexes from storage (call after loading data)
    void rebuild_phase2_indexes() {
        reverse_edges_.clear();
        temporal_buckets_.clear();
        lsh_buckets_.clear();
        lsh_initialized_ = false;

        size_t indexed = 0;
        storage_.for_each_hot([this, &indexed](const NodeId& id, const Node& node) {
            index_node_insert(id, node);
            ++indexed;
        });
        std::cerr << "[Mind] Built Phase 2 indexes (" << indexed << " nodes)\n";
    }

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 2 CORE: FORA-Style Approximate PPR (O(1/ε) query time)
    // ═══════════════════════════════════════════════════════════════════

    struct SparseVector {
        std::unordered_map<NodeId, float, NodeIdHash> entries;
        void add(const NodeId& id, float val) {
            entries[id] += val;
            if (std::abs(entries[id]) < 1e-10f) entries.erase(id);
        }
        float get(const NodeId& id) const {
            auto it = entries.find(id);
            return it != entries.end() ? it->second : 0.0f;
        }
    };

    // Forward push: deterministic, O(1/r_max) operations
    void forward_push(
        const NodeId& source,
        float source_weight,
        SparseVector& pi,
        SparseVector& residual,
        float r_max,
        float alpha)
    {
        residual.add(source, source_weight);
        std::queue<NodeId> active;
        active.push(source);

        std::unordered_set<NodeId, NodeIdHash> in_queue;
        in_queue.insert(source);

        while (!active.empty()) {
            NodeId u = active.front();
            active.pop();
            in_queue.erase(u);

            float r_u = residual.get(u);
            if (std::abs(r_u) < r_max) continue;

            // Push to PPR estimate
            pi.add(u, alpha * r_u);
            residual.entries[u] = 0.0f;

            // Push to predecessors via reverse edges
            float push_val = (1.0f - alpha) * r_u;
            if (reverse_edges_.count(u)) {
                size_t in_deg = reverse_edges_[u].size();
                if (in_deg > 0) {
                    for (const auto& re : reverse_edges_[u]) {
                        float delta = push_val * re.weight / static_cast<float>(in_deg);
                        if (std::abs(delta) > r_max * 0.1f) {
                            residual.add(re.source, delta);
                            if (!in_queue.count(re.source)) {
                                active.push(re.source);
                                in_queue.insert(re.source);
                            }
                        }
                    }
                }
            }
        }
    }

    // Multi-hop PPR query: O(1/ε) time complexity
    std::vector<Recall> ppr_query(
        const std::string& query,
        size_t k = 10,
        float epsilon = 0.05f)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!yantra_ || !yantra_->ready()) return {};

        auto artha = yantra_->transform(query);
        if (artha.nu.size() == 0) return {};

        auto seeds = recall_impl(artha.nu, query, 5, 0.2f, SearchMode::Hybrid);
        if (seeds.empty()) return {};

        SparseVector pi, residual;
        float r_max = epsilon / (2.0f * static_cast<float>(k));

        for (const auto& seed : seeds) {
            forward_push(seed.id, seed.relevance, pi, residual, r_max, 0.15f);
        }

        // Build results from sparse PPR vector
        std::unordered_set<NodeId, NodeIdHash> seed_ids;
        for (const auto& s : seeds) seed_ids.insert(s.id);

        std::vector<Recall> results;
        for (const auto& [id, score] : pi.entries) {
            if (seed_ids.count(id)) continue;
            if (score < 0.01f) continue;

            Node* node = storage_.get(id);
            if (!node) continue;

            auto text = payload_to_text(node->payload);
            results.push_back(Recall{
                id, score, score, node->epsilon,
                node->node_type, node->kappa,
                node->tau_created, node->tau_accessed,
                node->payload, text.value_or("")
            });
        }

        std::sort(results.begin(), results.end(),
                  [](const Recall& a, const Recall& b) { return a.relevance > b.relevance; });

        if (results.size() > k) results.resize(k);
        return results;
    }

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 2 CORE: LSH-Based Consolidation (O(1) average)
    // ═══════════════════════════════════════════════════════════════════

    // Find similar nodes via LSH: O(1) average
    std::vector<NodeId> lsh_find_similar(const Vector& emb, size_t max_candidates = 30) {
        if (!lsh_initialized_ || emb.size() == 0) return {};

        std::unordered_set<NodeId, NodeIdHash> candidates;

        for (size_t t = 0; t < LSH_NUM_TREES && candidates.size() < max_candidates; ++t) {
            uint32_t h = lsh_hash(emb, t);
            if (lsh_buckets_[t].count(h)) {
                for (const auto& id : lsh_buckets_[t][h]) {
                    candidates.insert(id);
                    if (candidates.size() >= max_candidates) break;
                }
            }
        }

        return std::vector<NodeId>(candidates.begin(), candidates.end());
    }

    // Try consolidation on insert: O(1) average
    std::optional<NodeId> try_consolidate_on_insert(
        const NodeId& new_id,
        const Node& new_node,
        float min_similarity = 0.92f)
    {
        auto candidates = lsh_find_similar(new_node.nu, 20);

        for (const auto& cand_id : candidates) {
            if (cand_id == new_id) continue;

            Node* cand = storage_.get(cand_id);
            if (!cand) continue;
            if (cand->node_type != new_node.node_type) continue;

            float sim = cand->nu.cosine(new_node.nu);
            if (sim >= min_similarity) {
                // Merge new into existing (keep existing)
                merge_into(cand_id, new_id, *cand, new_node);
                return cand_id;
            }
        }

        return std::nullopt;
    }

private:
    void merge_into(const NodeId& keeper_id, const NodeId& merged_id,
                    Node& keeper, const Node& merged) {
        float w_k = keeper.kappa.effective();
        float w_m = merged.kappa.effective();
        float w_total = w_k + w_m;

        if (w_total > 0) {
            for (size_t d = 0; d < keeper.nu.size() && d < merged.nu.size(); ++d) {
                keeper.nu[d] = (keeper.nu[d] * w_k + merged.nu[d] * w_m) / w_total;
            }
            keeper.nu.normalize();
        }

        keeper.kappa.mu = (keeper.kappa.mu * keeper.kappa.n + merged.kappa.mu * merged.kappa.n) /
                          (keeper.kappa.n + merged.kappa.n);
        keeper.kappa.n += merged.kappa.n;
        keeper.tau_created = std::min(keeper.tau_created, merged.tau_created);
        keeper.tau_accessed = std::max(keeper.tau_accessed, merged.tau_accessed);
        keeper.epsilon = std::max(keeper.epsilon, merged.epsilon);

        // Merge edges
        for (const auto& e : merged.edges) {
            if (e.target == keeper_id) continue;
            bool found = false;
            for (auto& ke : keeper.edges) {
                if (ke.target == e.target && ke.type == e.type) {
                    ke.weight = std::max(ke.weight, e.weight);
                    found = true;
                    break;
                }
            }
            if (!found) keeper.edges.push_back(e);
        }

        // Redirect reverse edges
        if (reverse_edges_.count(merged_id)) {
            for (const auto& re : reverse_edges_[merged_id]) {
                reverse_edges_[keeper_id].push_back(re);
            }
            reverse_edges_.erase(merged_id);
        }
    }

public:
    // Remove a node by ID
    bool remove_node(const NodeId& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return storage_.remove(id);
    }

    // Merge two nodes: keeper absorbs merged, merged is deleted
    bool merge_nodes(const NodeId& keeper_id, const NodeId& merged_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        Node* keeper = storage_.get(keeper_id);
        Node* merged = storage_.get(merged_id);
        if (!keeper || !merged) return false;
        if (keeper_id == merged_id) return false;

        merge_into(keeper_id, merged_id, *keeper, *merged);
        storage_.remove(merged_id);
        return true;
    }

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 2 CORE: Temporal Queries (O(log B + k))
    // ═══════════════════════════════════════════════════════════════════

    // Time range query: O(log B + k)
    std::vector<Recall> temporal_range_query(
        Timestamp from,
        Timestamp to,
        size_t limit = 50)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<Recall> results;

        uint64_t from_bid = temporal_bucket_id(from);
        uint64_t to_bid = temporal_bucket_id(to);

        auto it = temporal_buckets_.lower_bound(from_bid);

        while (it != temporal_buckets_.end() && it->first <= to_bid && results.size() < limit) {
            for (const auto& [ts, id] : it->second) {
                if (ts < from || ts > to) continue;

                Node* node = storage_.get(id);
                if (!node) continue;

                auto text = payload_to_text(node->payload);
                results.push_back(Recall{
                    id, 1.0f, 1.0f, node->epsilon,
                    node->node_type, node->kappa,
                    node->tau_created, node->tau_accessed,
                    node->payload, text.value_or("")
                });

                if (results.size() >= limit) break;
            }
            ++it;
        }

        return results;
    }

    // Recent timeline with Hawkes weighting
    std::vector<Recall> hawkes_timeline(size_t hours = 24, size_t limit = 20) {
        Timestamp now_ts = now();
        Timestamp from = now_ts - (hours * 3600000);

        auto results = temporal_range_query(from, now_ts, limit * 3);

        // Apply Hawkes weighting
        for (auto& r : results) {
            float delta_days = static_cast<float>(now_ts - r.created) / 86400000.0f;
            float beta = 0.05f;  // Default decay
            float intensity = 0.1f + std::exp(-beta * delta_days);

            // Boost if recently accessed
            float access_delta = static_cast<float>(now_ts - r.accessed) / 86400000.0f;
            if (access_delta < delta_days) {
                intensity += 0.3f * std::exp(-beta * access_delta);
            }

            r.relevance = std::min(1.0f, intensity);
        }

        std::sort(results.begin(), results.end(),
                  [](const Recall& a, const Recall& b) { return a.relevance > b.relevance; });

        if (results.size() > limit) results.resize(limit);
        return results;
    }

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 2 CORE: Causal Chains via Reverse Index (O(depth × avg_in_degree))
    // ═══════════════════════════════════════════════════════════════════

    struct CausalChain {
        std::vector<NodeId> nodes;
        std::vector<EdgeType> edges;
        float confidence;
    };

    std::vector<CausalChain> find_causal_chains(
        const NodeId& effect,
        size_t max_depth = 5,
        float min_confidence = 0.3f)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<CausalChain> chains;
        Node* effect_node = storage_.get(effect);
        if (!effect_node) return chains;

        auto is_causal = [](EdgeType t) {
            return t == EdgeType::TriggeredBy || t == EdgeType::EvolvedFrom ||
                   t == EdgeType::Continues || t == EdgeType::Supports;
        };

        struct State {
            NodeId current;
            CausalChain chain;
        };

        std::queue<State> frontier;
        State initial;
        initial.current = effect;
        initial.chain.nodes.push_back(effect);
        initial.chain.confidence = 1.0f;
        frontier.push(std::move(initial));

        while (!frontier.empty() && chains.size() < 10) {
            auto state = std::move(frontier.front());
            frontier.pop();

            if (state.chain.nodes.size() > max_depth) {
                std::reverse(state.chain.nodes.begin(), state.chain.nodes.end());
                std::reverse(state.chain.edges.begin(), state.chain.edges.end());
                chains.push_back(std::move(state.chain));
                continue;
            }

            Node* curr_node = storage_.get(state.current);
            if (!curr_node) continue;

            // Use reverse edge index - O(in_degree) not O(V)
            if (reverse_edges_.count(state.current)) {
                for (const auto& re : reverse_edges_[state.current]) {
                    if (!is_causal(re.type)) continue;

                    Node* cause = storage_.get(re.source);
                    if (!cause || cause->tau_created >= curr_node->tau_created) continue;

                    float new_conf = state.chain.confidence * re.weight;
                    if (new_conf < min_confidence) continue;

                    // Check not already in chain
                    bool in_chain = false;
                    for (const auto& n : state.chain.nodes) {
                        if (n == re.source) { in_chain = true; break; }
                    }
                    if (in_chain) continue;

                    State next;
                    next.current = re.source;
                    next.chain = state.chain;
                    next.chain.nodes.push_back(re.source);
                    next.chain.edges.push_back(re.type);
                    next.chain.confidence = new_conf;
                    frontier.push(std::move(next));
                }
            }
        }

        std::sort(chains.begin(), chains.end(),
                  [](const CausalChain& a, const CausalChain& b) {
                      return a.confidence > b.confidence;
                  });

        return chains;
    }

    // Helper: edge type name
    static std::string edge_type_name(EdgeType type) {
        switch (type) {
            case EdgeType::Similar:      return "Similar";
            case EdgeType::TriggeredBy:  return "TriggeredBy";
            case EdgeType::Supports:     return "Supports";
            case EdgeType::Contradicts:  return "Contradicts";
            case EdgeType::EvolvedFrom:  return "EvolvedFrom";
            case EdgeType::Continues:    return "Continues";
            default:                     return "Related";
        }
    }

    MindConfig config_;
    mutable std::mutex mutex_;
    TieredStorage storage_;
    Graph graph_;
    GraphStore graph_store_;  // Dictionary-encoded graph (legacy)
    MmapGraphStore mmap_graph_store_;  // Mmap-backed graph for 100M+ scale
    Dynamics dynamics_;
    std::shared_ptr<VakYantra> yantra_;
    std::atomic<bool> running_;
    Timestamp last_decay_ = 0;
    Timestamp last_checkpoint_ = 0;

    // Soul-aware scoring and hybrid retrieval
    ScoringConfig scoring_config_;
    BM25Index bm25_index_;
    std::string bm25_path_;  // Path for BM25 persistence
    bool bm25_built_ = false;  // Lazy build on first sparse/hybrid search
    CrossEncoder cross_encoder_;

    // Tag index for exact-match filtering (inter-agent communication)
    TagIndex tag_index_;

    // Autonomous dynamics and learning
    Daemon daemon_;
    FeedbackTracker feedback_;

    // Session context for priming (Phase 4: Context Modulation)
    SessionContext session_context_;
    static constexpr size_t MAX_RECENT_OBSERVATIONS = 50;  // Rolling window
    std::deque<NodeId> recent_observation_order_;  // For FIFO eviction

    // Competition config (Phase 5: Interference/Competition)
    CompetitionConfig competition_config_;

    // Phase 7: 100M Scale Components
    QueryRouter query_router_;
    QuotaManager quota_manager_;
    UtilityDecay utility_decay_;
    AttractorDampener attractor_dampener_;

    // Phase 7: Priority 1 - Core Runtime Components
    ProvenanceSpine provenance_spine_;
    RealmScoping realm_scoping_;
    TruthMaintenance truth_maintenance_;

    // Phase 7: Priority 2 - RPC-exposed Components
    EvalHarness eval_harness_;
    ReviewQueue review_queue_;
    EpiplexityTest epiplexity_test_;

    // Phase 7: Priority 3 - Pipeline Components
    SynthesisQueue synthesis_queue_;
    GapInquiry gap_inquiry_;

    // Phase 7: Quota management helpers (caller must hold mutex_)
    void update_quota_counts() {
        std::unordered_map<NodeType, size_t> counts;
        storage_.for_each_hot([&counts](const NodeId&, const Node& node) {
            counts[node.node_type]++;
        });
        quota_manager_.update_counts(counts);
    }

    void maybe_evict_for_quota(NodeType type) {
        // Collect nodes of this type for eviction scoring
        std::vector<Node> candidates;
        storage_.for_each_hot([&candidates, type](const NodeId&, const Node& node) {
            if (node.node_type == type) {
                candidates.push_back(node);
            }
        });

        if (candidates.empty()) return;

        // Get eviction candidates from quota manager (lowest utility first)
        auto to_evict = quota_manager_.get_eviction_candidates(
            candidates, type, 10, now());

        if (to_evict.empty()) return;

        // Perform evictions using WAL-backed forget
        size_t evicted = 0;
        for (const auto& candidate : to_evict) {
            NodeId id = candidate.id;

            // Remove from Phase 7 tracking structures
            provenance_spine_.remove(id);
            utility_decay_.remove(id);
            attractor_dampener_.remove(id);
            synthesis_queue_.remove(id);
            gap_inquiry_.remove(id);

            // Forget from storage (writes to WAL)
            if (storage_.forget(id)) {
                evicted++;
            }
        }

        // Update quota counts after eviction
        if (evicted > 0) {
            update_quota_counts();
            std::cerr << "[Mind] Evicted " << evicted << " nodes for quota\n";
        }
    }
};

} // namespace chitta
