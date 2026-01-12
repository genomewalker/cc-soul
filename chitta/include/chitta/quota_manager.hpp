#pragma once
// Quota Manager: Type-based quotas and eviction for memory control
//
// Controls growth at scale by:
// - Setting quotas per NodeType (e.g., Episodes max 30%)
// - Evicting low-utility nodes when quotas exceeded
// - Tracking usage statistics for capacity planning
//
// Eviction priority (lowest first):
// 1. Low confidence + old access time
// 2. High decay rate (signals > wisdom)
// 3. Episodes before core beliefs

#include "types.hpp"
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cmath>

namespace chitta {

// Quota configuration per node type
struct TypeQuota {
    NodeType type;
    float max_fraction;     // Maximum fraction of total nodes (0.0-1.0)
    size_t min_reserved;    // Minimum guaranteed slots (even if over quota elsewhere)
    float eviction_priority; // Higher = evict first when over quota (1.0 default)
};

// Default quotas - prevents any single type from dominating
inline std::vector<TypeQuota> default_quotas() {
    return {
        {NodeType::Episode,    0.30f, 100, 1.5f},   // Episodes cap at 30%, evict first
        {NodeType::Failure,    0.10f, 50,  0.4f},   // Failures cap at 10%, protect (gold)
        {NodeType::Gap,        0.10f, 100, 1.2f},   // Gaps cap at 10%
        {NodeType::Question,   0.05f, 50,  1.0f},   // Questions cap at 5%
        {NodeType::Wisdom,     0.15f, 200, 0.5f},   // Wisdom cap at 15%, protect
        {NodeType::Belief,     0.10f, 100, 0.3f},   // Beliefs cap at 10%, most protected
        {NodeType::Invariant,  0.05f, 50,  0.2f},   // Invariants cap at 5%, never evict
        {NodeType::Intention,  0.05f, 50,  0.8f},   // Intentions cap at 5%
        {NodeType::Aspiration, 0.05f, 50,  0.6f},   // Aspirations cap at 5%
        {NodeType::Dream,      0.03f, 25,  0.7f},   // Dreams cap at 3%
        {NodeType::Triplet,    0.02f, 100, 1.0f},   // Triplets cap at 2%
    };
}

// Budget alert levels
enum class BudgetAlert {
    Normal,     // Under 70% of quota
    Warning,    // 70-90% of quota
    Critical,   // 90-100% of quota
    Exceeded    // Over quota, eviction needed
};

// Per-type statistics
struct TypeStats {
    NodeType type;
    size_t count;
    size_t quota_count;     // count at quota limit
    float current_fraction;
    BudgetAlert alert_level;
};

// Node eviction candidate
struct EvictionCandidate {
    NodeId id;
    NodeType type;
    float utility_score;    // Lower = evict first
    Timestamp last_access;
};

// Quota manager for type-based memory control
class QuotaManager {
public:
    explicit QuotaManager(size_t total_capacity = 100000000)
        : total_capacity_(total_capacity) {
        set_quotas(default_quotas());
    }

    // Configure quotas
    void set_quotas(const std::vector<TypeQuota>& quotas) {
        quotas_.clear();
        for (const auto& q : quotas) {
            quotas_[q.type] = q;
        }
    }

    void set_quota(NodeType type, float max_fraction, size_t min_reserved = 0,
                   float eviction_priority = 1.0f) {
        quotas_[type] = TypeQuota{type, max_fraction, min_reserved, eviction_priority};
    }

    void set_capacity(size_t capacity) {
        total_capacity_ = capacity;
    }

    // Update counts from storage
    void update_counts(const std::unordered_map<NodeType, size_t>& type_counts) {
        type_counts_ = type_counts;
        total_count_ = 0;
        for (const auto& [_, count] : type_counts) {
            total_count_ += count;
        }
    }

    // Check if type is at or over quota
    bool at_quota(NodeType type) const {
        auto it = quotas_.find(type);
        if (it == quotas_.end()) return false;

        size_t quota_limit = static_cast<size_t>(total_capacity_ * it->second.max_fraction);
        auto count_it = type_counts_.find(type);
        size_t count = (count_it != type_counts_.end()) ? count_it->second : 0;

        return count >= quota_limit;
    }

    // Get alert level for a type
    BudgetAlert alert_level(NodeType type) const {
        auto it = quotas_.find(type);
        if (it == quotas_.end()) return BudgetAlert::Normal;

        size_t quota_limit = static_cast<size_t>(total_capacity_ * it->second.max_fraction);
        auto count_it = type_counts_.find(type);
        size_t count = (count_it != type_counts_.end()) ? count_it->second : 0;

        if (quota_limit == 0) return BudgetAlert::Normal;

        float ratio = static_cast<float>(count) / quota_limit;
        if (ratio >= 1.0f) return BudgetAlert::Exceeded;
        if (ratio >= 0.9f) return BudgetAlert::Critical;
        if (ratio >= 0.7f) return BudgetAlert::Warning;
        return BudgetAlert::Normal;
    }

    // Get statistics for all types
    std::vector<TypeStats> get_stats() const {
        std::vector<TypeStats> stats;

        for (const auto& [type, quota] : quotas_) {
            TypeStats s;
            s.type = type;
            s.quota_count = static_cast<size_t>(total_capacity_ * quota.max_fraction);

            auto count_it = type_counts_.find(type);
            s.count = (count_it != type_counts_.end()) ? count_it->second : 0;

            s.current_fraction = (total_count_ > 0) ?
                static_cast<float>(s.count) / total_count_ : 0.0f;
            s.alert_level = alert_level(type);

            stats.push_back(s);
        }

        return stats;
    }

    // Calculate utility score for eviction decisions
    // Lower score = higher priority for eviction
    static float utility_score(const Node& node, Timestamp now) {
        // Components:
        // 1. Confidence (higher = more useful)
        // 2. Recency (more recent access = more useful)
        // 3. Decay rate (slower decay = more valuable)
        // 4. Type base value (beliefs > episodes)

        float confidence = node.kappa.effective();

        // Days since last access (capped at 365)
        float days_old = std::min(365.0f,
            static_cast<float>(now - node.tau_accessed) / 86400000.0f);
        float recency = 1.0f / (1.0f + days_old / 30.0f);  // Half-life of 30 days

        // Decay rate factor (slower = more valuable)
        float decay_factor = 1.0f - std::min(1.0f, node.delta / 0.2f);

        // Type base value
        float type_value = type_base_value(node.node_type);

        // Combined score
        return confidence * recency * decay_factor * type_value;
    }

    // Identify candidates for eviction
    // Returns nodes sorted by utility (lowest first = evict first)
    std::vector<EvictionCandidate> get_eviction_candidates(
        const std::vector<Node>& nodes,
        NodeType type,
        size_t count,
        Timestamp now) const
    {
        // Get eviction priority for this type
        float priority = 1.0f;
        auto it = quotas_.find(type);
        if (it != quotas_.end()) {
            priority = it->second.eviction_priority;
        }

        std::vector<EvictionCandidate> candidates;
        for (const auto& node : nodes) {
            if (node.node_type != type) continue;

            EvictionCandidate c;
            c.id = node.id;
            c.type = type;
            c.utility_score = utility_score(node, now) / priority;  // Lower priority = lower score = evict first
            c.last_access = node.tau_accessed;
            candidates.push_back(c);
        }

        // Sort by utility (lowest first)
        std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) {
                return a.utility_score < b.utility_score;
            });

        // Return requested count
        if (candidates.size() > count) {
            candidates.resize(count);
        }

        return candidates;
    }

    // Calculate how many nodes to evict to get back under quota
    size_t eviction_target(NodeType type) const {
        auto it = quotas_.find(type);
        if (it == quotas_.end()) return 0;

        size_t quota_limit = static_cast<size_t>(total_capacity_ * it->second.max_fraction);
        auto count_it = type_counts_.find(type);
        size_t count = (count_it != type_counts_.end()) ? count_it->second : 0;

        if (count <= quota_limit) return 0;

        // Target: get 10% below quota for headroom
        size_t target = static_cast<size_t>(quota_limit * 0.9f);
        return count - target;
    }

    // Alert level to string
    static std::string alert_name(BudgetAlert level) {
        switch (level) {
            case BudgetAlert::Normal: return "normal";
            case BudgetAlert::Warning: return "warning";
            case BudgetAlert::Critical: return "critical";
            case BudgetAlert::Exceeded: return "exceeded";
            default: return "unknown";
        }
    }

private:
    // Base value by type (for utility calculation)
    static float type_base_value(NodeType type) {
        switch (type) {
            case NodeType::Invariant: return 10.0f;  // Most valuable
            case NodeType::Belief:    return 5.0f;
            case NodeType::Failure:   return 4.0f;   // Failures are gold
            case NodeType::Wisdom:    return 3.0f;
            case NodeType::Aspiration:return 2.0f;
            case NodeType::Dream:     return 1.5f;
            case NodeType::Intention: return 1.2f;
            case NodeType::Gap:       return 1.0f;
            case NodeType::Question:  return 1.0f;
            case NodeType::Episode:   return 0.8f;
            case NodeType::Triplet:   return 1.0f;
            default: return 1.0f;
        }
    }

    size_t total_capacity_;
    size_t total_count_ = 0;
    std::unordered_map<NodeType, TypeQuota> quotas_;
    std::unordered_map<NodeType, size_t> type_counts_;
};

} // namespace chitta
