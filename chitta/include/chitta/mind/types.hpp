#pragma once
// Mind types: configuration and result structures
//
// Extracted from mind.hpp for modularity.
// These are the core types used across the Mind API.

#include "../types.hpp"
#include "../quantized.hpp"
#include "../provenance.hpp"
#include <vector>
#include <string>

namespace chitta {

// Mind configuration
struct MindConfig {
    std::string path;                    // Base path for storage files
    size_t hot_capacity = 1000;          // Max nodes in RAM (lowered for dev)
    size_t warm_capacity = 10000;        // Max nodes in mmap
    int64_t hot_age_ms = 3600000;        // 1 hour until warm (lowered for dev)
    int64_t warm_age_ms = 86400000;      // 1 day until cold
    int64_t decay_interval_ms = 600000;  // 10 min between decay (faster for dev)
    int64_t checkpoint_interval_ms = 60000;  // 1 minute between checkpoints
    float prune_threshold = 0.1f;        // Confidence below this = prune
    bool skip_bm25 = false;              // Skip BM25 loading for fast stats
    bool use_mmap_graph = false;         // Use MmapGraphStore for 100M+ scale

    // Phase 7: 100M Scale Options
    bool enable_quota_manager = false;   // Enable type-based quotas
    bool enable_utility_decay = false;   // Enable usage-driven decay
    bool enable_attractor_dampener = false;  // Enable over-retrieval dampening
    size_t total_capacity = 100000000;   // Total node capacity for quota manager

    // Phase 7: Priority 1 - Core Runtime Wiring
    bool enable_provenance = false;      // Track source metadata on every insert
    bool enable_realm_scoping = false;   // Filter recall by current realm
    bool enable_truth_maintenance = false;  // Surface conflicts in recall results
    std::string default_realm = "brahman";  // Default realm for new nodes
    ProvenanceSource default_provenance_source = ProvenanceSource::Unknown;
    std::string session_id;              // Current session ID for provenance

    // Phase 7: Priority 3 - Pipeline Integration
    bool enable_query_routing = false;   // Route queries based on intent classification
};

// Search result with meaning
struct Recall {
    NodeId id;
    float similarity;      // Raw semantic similarity
    float relevance;       // Soul-aware relevance score
    float epiplexity;      // Learnable structure (how reconstructable is this?)
    NodeType type;
    Confidence confidence;
    Timestamp created;
    Timestamp accessed;
    std::vector<uint8_t> payload;
    std::string text;  // Original text if available

    // Temporary embedding for competition (cleared after recall)
    QuantizedVector qnu;
    bool has_embedding = false;

    // Phase 7: Conflict info from TruthMaintenance
    bool has_conflict = false;
    std::vector<NodeId> conflicting_nodes;
};

// Search mode for hybrid retrieval
enum class SearchMode {
    Dense,      // Semantic only (fast)
    Sparse,     // BM25 only (keyword)
    Hybrid      // Dense + Sparse with RRF fusion
};

// Mind state for persistence
struct MindState {
    uint64_t snapshot_id;
    Coherence coherence;
    Timestamp last_decay;
    Timestamp last_checkpoint;
    size_t total_nodes;
    size_t hot_nodes;
    size_t warm_nodes;
    size_t cold_nodes;
    bool yantra_ready;
};

// Mind health for proactive monitoring
// Prevents catastrophic degradation through early detection
// Named "Ojas" (ओजस्) - Sanskrit for vital essence, the refined energy that sustains life
struct MindHealth {
    float structural;   // File integrity (checksums, free lists, indices)
    float semantic;     // Graph coherence (edge validity, no orphans)
    float temporal;     // Time-based (decay applied, WAL size, backup freshness)
    float capacity;     // Storage (not near limits, not fragmented)

    // Ojas (ओजस्): the vital essence score (0-1)
    // Like tau_k for coherence, ojas represents overall soul vitality
    // Greek symbol: ψ (psi) - associated with psyche/mind
    float ojas() const {
        return 0.4f * structural + 0.3f * semantic +
               0.2f * temporal + 0.1f * capacity;
    }

    // Greek letter alias for cc-status display
    float psi() const { return ojas(); }

    // Alias for backward compatibility
    float overall() const { return ojas(); }

    // Should we backup now?
    bool needs_backup(Timestamp last_backup) const {
        Timestamp now_ts = now();
        float hours_since_backup = (now_ts - last_backup) / 3600000.0f;
        return overall() >= 0.9f && hours_since_backup > 1.0f;
    }

    // Is this critical? Should we go read-only?
    bool critical() const {
        return overall() < 0.6f || structural < 0.5f;
    }

    // What action should we take?
    enum class Action {
        Normal,         // 95-100%: normal operation
        ScheduleBackup, // 80-95%: schedule backup, log warning
        ForceRepair,    // 60-80%: force backup, attempt repair
        Emergency       // <60%: emergency mode, read-only until repaired
    };

    Action recommended_action() const {
        float score = overall();
        if (score >= 0.95f) return Action::Normal;
        if (score >= 0.80f) return Action::ScheduleBackup;
        if (score >= 0.60f) return Action::ForceRepair;
        return Action::Emergency;
    }

    // Human-readable status
    const char* status_string() const {
        switch (recommended_action()) {
            case Action::Normal: return "healthy";
            case Action::ScheduleBackup: return "degraded";
            case Action::ForceRepair: return "repair_needed";
            case Action::Emergency: return "critical";
        }
        return "unknown";
    }
};

} // namespace chitta
