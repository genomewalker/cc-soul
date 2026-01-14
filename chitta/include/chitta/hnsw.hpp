#pragma once
// HNSW (Hierarchical Navigable Small World) index for fast semantic search
// Simplified implementation optimized for mind-scale graphs

#include "types.hpp"
#include "quantized.hpp"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <queue>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace chitta {

// NodeIdHash is now defined in types.hpp

// HNSW configuration
struct HNSWConfig {
    size_t M = 16;           // Max connections per node per layer
    size_t ef_construction = 200;  // Search width during construction
    size_t ef_search = 50;   // Search width during query
    size_t max_layers = 6;   // Maximum number of layers
};

// HNSW node with connections
struct HNSWNode {
    NodeId id;
    QuantizedVector vector;
    std::vector<std::vector<NodeId>> connections;  // connections[layer] = neighbors

    HNSWNode(NodeId i, QuantizedVector v, size_t layers)
        : id(i), vector(std::move(v)), connections(layers) {}
};

// Distance pair for priority queues
struct DistPair {
    float distance;
    NodeId id;

    DistPair() : distance(0.0f), id() {}
    DistPair(float d, NodeId i) : distance(d), id(i) {}

    bool operator<(const DistPair& o) const { return distance < o.distance; }
    bool operator>(const DistPair& o) const { return distance > o.distance; }
};

// HNSW index for approximate nearest neighbor search
class HNSWIndex {
public:
    explicit HNSWIndex(HNSWConfig config = {})
        : config_(config), rng_(std::random_device{}()) {}

    // Insert a node
    void insert(NodeId id, const QuantizedVector& vector) {
        size_t level = random_level();

        auto node = std::make_shared<HNSWNode>(id, vector, level + 1);
        nodes_[id] = node;

        if (nodes_.size() == 1) {
            entry_point_ = id;
            max_level_ = level;
            return;
        }

        // Find entry point and descend
        NodeId curr = entry_point_;
        for (int l = max_level_; l > static_cast<int>(level); --l) {
            curr = search_layer_greedy(vector, curr, l);
        }

        // Insert at each level
        for (int l = std::min(level, max_level_); l >= 0; --l) {
            auto neighbors = search_layer(vector, curr, config_.ef_construction, l);
            select_neighbors(node, neighbors, l);
            curr = neighbors.empty() ? curr : neighbors[0].id;
        }

        // Update entry point if needed
        if (level > max_level_) {
            entry_point_ = id;
            max_level_ = level;
        }
    }

    // Search for k nearest neighbors
    std::vector<std::pair<NodeId, float>> search(
        const QuantizedVector& query, size_t k) const
    {
        if (nodes_.empty()) return {};

        NodeId curr = entry_point_;

        // Greedy search from top to layer 0
        for (int l = max_level_; l > 0; --l) {
            curr = search_layer_greedy(query, curr, l);
        }

        // Search layer 0 with ef_search
        auto candidates = search_layer(query, curr, config_.ef_search, 0);

        // Return top k
        std::vector<std::pair<NodeId, float>> results;
        for (size_t i = 0; i < std::min(k, candidates.size()); ++i) {
            results.emplace_back(candidates[i].id, 1.0f - candidates[i].distance);
        }
        return results;
    }

    // Remove a node (optimized O(m) by tracking bidirectional connections)
    void remove(NodeId id) {
        auto it = nodes_.find(id);
        if (it == nodes_.end()) return;

        // Remove from neighbors' connection lists
        auto& node = it->second;
        for (size_t l = 0; l < node->connections.size(); ++l) {
            for (const auto& neighbor_id : node->connections[l]) {
                auto nit = nodes_.find(neighbor_id);
                if (nit != nodes_.end()) {
                    auto& conns = nit->second->connections[l];
                    // Use unordered_map for O(1) lookup instead of linear scan
                    auto conn_it = std::find(conns.begin(), conns.end(), id);
                    if (conn_it != conns.end()) {
                        conns.erase(conn_it);
                    }
                }
            }
        }

        nodes_.erase(it);

        // Update entry point if needed
        if (id == entry_point_ && !nodes_.empty()) {
            entry_point_ = nodes_.begin()->first;
            max_level_ = nodes_.begin()->second->connections.size() - 1;
        }
    }

    size_t size() const { return nodes_.size(); }
    bool empty() const { return nodes_.empty(); }

    // Serialize to bytes for persistence
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;

        // Helper to write primitives
        auto write = [&data](const void* ptr, size_t size) {
            const uint8_t* bytes = static_cast<const uint8_t*>(ptr);
            data.insert(data.end(), bytes, bytes + size);
        };

        // Header: magic, version, config, metadata
        uint32_t magic = 0x484E5357;  // "HNSW"
        uint32_t version = 1;
        write(&magic, sizeof(magic));
        write(&version, sizeof(version));
        write(&config_.M, sizeof(config_.M));
        write(&config_.ef_construction, sizeof(config_.ef_construction));
        write(&config_.ef_search, sizeof(config_.ef_search));
        write(&config_.max_layers, sizeof(config_.max_layers));

        // Index state
        size_t node_count = nodes_.size();
        write(&node_count, sizeof(node_count));
        write(&max_level_, sizeof(max_level_));
        write(&entry_point_.high, sizeof(entry_point_.high));
        write(&entry_point_.low, sizeof(entry_point_.low));

        // Nodes
        for (const auto& [id, node] : nodes_) {
            // Node ID
            write(&id.high, sizeof(id.high));
            write(&id.low, sizeof(id.low));

            // Quantized vector
            write(node->vector.data, sizeof(node->vector.data));
            write(&node->vector.scale, sizeof(node->vector.scale));
            write(&node->vector.offset, sizeof(node->vector.offset));

            // Connections
            size_t num_layers = node->connections.size();
            write(&num_layers, sizeof(num_layers));
            for (const auto& layer : node->connections) {
                size_t num_conns = layer.size();
                write(&num_conns, sizeof(num_conns));
                for (const auto& conn : layer) {
                    write(&conn.high, sizeof(conn.high));
                    write(&conn.low, sizeof(conn.low));
                }
            }
        }

        return data;
    }

    // Deserialize from bytes
    static HNSWIndex deserialize(const std::vector<uint8_t>& data) {
        size_t pos = 0;

        // Helper to read primitives with detailed error reporting
        auto read = [&data, &pos](void* ptr, size_t size) {
            if (pos + size > data.size()) {
                char error_buf[256];
                snprintf(error_buf, sizeof(error_buf),
                    "HNSW deserialize: unexpected end at offset %zu, need %zu bytes, have %zu bytes",
                    pos, size, data.size() - pos);
                throw std::runtime_error(error_buf);
            }
            std::memcpy(ptr, data.data() + pos, size);
            pos += size;
        };

        // Header
        uint32_t magic, version;
        read(&magic, sizeof(magic));
        read(&version, sizeof(version));

        if (magic != 0x484E5357) throw std::runtime_error("HNSW deserialize: invalid magic");
        if (version != 1) throw std::runtime_error("HNSW deserialize: unsupported version");

        HNSWConfig config;
        read(&config.M, sizeof(config.M));
        read(&config.ef_construction, sizeof(config.ef_construction));
        read(&config.ef_search, sizeof(config.ef_search));
        read(&config.max_layers, sizeof(config.max_layers));

        HNSWIndex index(config);

        // Index state
        size_t node_count;
        read(&node_count, sizeof(node_count));
        read(&index.max_level_, sizeof(index.max_level_));
        read(&index.entry_point_.high, sizeof(index.entry_point_.high));
        read(&index.entry_point_.low, sizeof(index.entry_point_.low));

        // Nodes
        for (size_t i = 0; i < node_count; ++i) {
            NodeId id;
            read(&id.high, sizeof(id.high));
            read(&id.low, sizeof(id.low));

            QuantizedVector vec;
            read(vec.data, sizeof(vec.data));
            read(&vec.scale, sizeof(vec.scale));
            read(&vec.offset, sizeof(vec.offset));

            size_t num_layers;
            read(&num_layers, sizeof(num_layers));

            auto node = std::make_shared<HNSWNode>(id, vec, num_layers);

            for (size_t l = 0; l < num_layers; ++l) {
                size_t num_conns;
                read(&num_conns, sizeof(num_conns));
                node->connections[l].reserve(num_conns);
                for (size_t c = 0; c < num_conns; ++c) {
                    NodeId conn;
                    read(&conn.high, sizeof(conn.high));
                    read(&conn.low, sizeof(conn.low));
                    node->connections[l].push_back(conn);
                }
            }

            index.nodes_[id] = node;
        }

        return index;
    }

private:
    size_t random_level() {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        float r = dist(rng_);
        size_t level = 0;
        float p = 1.0f / config_.M;
        while (r < p && level < config_.max_layers - 1) {
            level++;
            r = dist(rng_);
        }
        return level;
    }

    float distance(const QuantizedVector& a, const QuantizedVector& b) const {
        return 1.0f - a.cosine_approx(b);  // Convert similarity to distance
    }

    NodeId search_layer_greedy(const QuantizedVector& query, NodeId start, size_t layer) const {
        NodeId curr = start;
        auto curr_it = nodes_.find(curr);
        if (curr_it == nodes_.end()) return curr;  // Entry point missing, return as-is
        float curr_dist = distance(query, curr_it->second->vector);

        bool changed = true;
        while (changed) {
            changed = false;
            auto node_it = nodes_.find(curr);
            if (node_it == nodes_.end()) break;  // Current node deleted
            const auto& node = node_it->second;
            if (layer < node->connections.size()) {
                for (const auto& neighbor : node->connections[layer]) {
                    auto neighbor_it = nodes_.find(neighbor);
                    if (neighbor_it == nodes_.end()) continue;  // Skip deleted neighbors
                    float d = distance(query, neighbor_it->second->vector);
                    if (d < curr_dist) {
                        curr = neighbor;
                        curr_dist = d;
                        changed = true;
                    }
                }
            }
        }
        return curr;
    }

    std::vector<DistPair> search_layer(
        const QuantizedVector& query, NodeId start, size_t ef, size_t layer) const
    {
        std::unordered_set<NodeId, NodeIdHash> visited;
        std::priority_queue<DistPair, std::vector<DistPair>, std::greater<DistPair>> candidates;
        std::priority_queue<DistPair> results;

        auto start_it = nodes_.find(start);
        if (start_it == nodes_.end()) return {};  // Start node missing
        float start_dist = distance(query, start_it->second->vector);
        candidates.push(DistPair(start_dist, start));
        results.push(DistPair(start_dist, start));
        visited.insert(start);

        while (!candidates.empty()) {
            DistPair curr_pair = candidates.top();
            float c_dist = curr_pair.distance;
            NodeId c_id = curr_pair.id;
            candidates.pop();

            if (c_dist > results.top().distance && results.size() >= ef) break;

            auto node_it = nodes_.find(c_id);
            if (node_it == nodes_.end()) continue;  // Node deleted
            const auto& node = node_it->second;
            if (layer < node->connections.size()) {
                for (const auto& neighbor : node->connections[layer]) {
                    if (visited.count(neighbor)) continue;
                    visited.insert(neighbor);

                    auto neighbor_it = nodes_.find(neighbor);
                    if (neighbor_it == nodes_.end()) continue;  // Skip deleted neighbors
                    float n_dist = distance(query, neighbor_it->second->vector);
                    if (results.size() < ef || n_dist < results.top().distance) {
                        candidates.push(DistPair(n_dist, neighbor));
                        results.push(DistPair(n_dist, neighbor));
                        if (results.size() > ef) results.pop();
                    }
                }
            }
        }

        std::vector<DistPair> result_vec;
        while (!results.empty()) {
            result_vec.push_back(results.top());
            results.pop();
        }
        std::reverse(result_vec.begin(), result_vec.end());
        return result_vec;
    }

    void select_neighbors(std::shared_ptr<HNSWNode>& node,
                          const std::vector<DistPair>& candidates, size_t layer)
    {
        size_t M = (layer == 0) ? config_.M * 2 : config_.M;

        for (size_t i = 0; i < std::min(M, candidates.size()); ++i) {
            NodeId neighbor_id = candidates[i].id;
            node->connections[layer].push_back(neighbor_id);

            // Add reverse connection
            auto& neighbor = nodes_[neighbor_id];
            if (layer < neighbor->connections.size()) {
                if (neighbor->connections[layer].size() < M) {
                    neighbor->connections[layer].push_back(node->id);
                }
            }
        }
    }

    HNSWConfig config_;
    std::unordered_map<NodeId, std::shared_ptr<HNSWNode>, NodeIdHash> nodes_;
    NodeId entry_point_;
    size_t max_level_ = 0;
    mutable std::mt19937 rng_;
};

} // namespace chitta
