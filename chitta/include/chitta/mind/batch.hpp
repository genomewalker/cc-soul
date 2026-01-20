#pragma once
// Batch: Efficient batch operations for bulk indexing
//
// Use for code indexing and other bulk operations where
// per-node overhead would be prohibitive.

#include "../types.hpp"
#include "storage_facade.hpp"
#include <vector>
#include <string>

namespace chitta {

// Specification for a raw node (pre-embedded)
struct RawNodeSpec {
    NodeType type = NodeType::Symbol;
    Vector embedding;                      // Use Vector::zeros() to skip embedding
    Confidence confidence = Confidence(0.9f);
    std::vector<uint8_t> payload;
    std::vector<std::string> tags;

    // Optional provenance for code indexing
    std::string source_path;
    std::string source_hash;
    Timestamp last_verified_at = 0;
    StaleState stale_state = StaleState::Fresh;
};

// Options for batch write
struct BatchWriteOptions {
    bool sync_on_flush = true;   // Call sync() once at end
};

// Batch insert nodes
inline std::vector<NodeId> remember_batch_raw(
    StorageFacade& storage,
    const std::vector<RawNodeSpec>& specs,
    const BatchWriteOptions& opts = {})
{
    std::vector<NodeId> ids;
    ids.reserve(specs.size());
    std::vector<Node> nodes;
    nodes.reserve(specs.size());
    Timestamp current = now();

    // Build all nodes
    for (const auto& spec : specs) {
        Node node(spec.type, spec.embedding);
        node.kappa = spec.confidence;
        node.payload = spec.payload;
        node.tags = spec.tags;

        if (!spec.source_path.empty()) {
            node.source_path = spec.source_path;
            node.source_hash = spec.source_hash;
            node.last_verified_at = spec.last_verified_at > 0 ? spec.last_verified_at : current;
            node.stale_state = spec.stale_state;
        }

        ids.push_back(node.id);
        nodes.push_back(std::move(node));
    }

    // Batch insert
    storage.insert_batch(std::move(nodes));

    // Sync if requested
    if (opts.sync_on_flush) {
        storage.sync();
    }

    return ids;
}

}  // namespace chitta
