#pragma once
// Two-Stage Wisdom Foundry: Quality gate for synthesis
//
// New wisdom enters a staging queue before full integration.
// Promotion requires evidence (episodes, user approval, time).
// Quarantine period prevents premature crystallization.
//
// Flow: observe() -> staging -> evidence -> promotion -> wisdom

#include "types.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <algorithm>

namespace chitta {

// Staging status
enum class StagingStatus : uint8_t {
    Pending = 0,       // Awaiting evidence
    UnderReview = 1,   // Has some evidence, being evaluated
    Approved = 2,      // Approved for promotion
    Rejected = 3,      // Rejected, will decay
    Promoted = 4,      // Successfully promoted to wisdom
};

// Evidence for promotion
struct Evidence {
    enum class Type : uint8_t {
        EpisodeSupport,    // Supporting episode
        UserApproval,      // Explicit user thumbs-up
        ConsistentRecall,  // Recalled multiple times without contradiction
        ExternalValidation,// Validated by external source
        TimeMatured,       // Survived quarantine period
    };

    Type type;
    NodeId source;       // Episode ID or validation source
    std::string details;
    Timestamp added_at;
    float weight;        // Evidence strength (0-1)
};

// Staged wisdom entry
struct StagedWisdom {
    NodeId id;                    // ID of the staged node
    std::string content;          // Content being staged
    StagingStatus status = StagingStatus::Pending;
    Timestamp staged_at = 0;      // When it entered staging
    Timestamp status_changed_at = 0;
    std::vector<Evidence> evidence;
    float evidence_score = 0.0f;  // Cumulative evidence strength
    uint32_t recall_count = 0;    // Times recalled while staged
    uint32_t contradiction_count = 0;  // Contradictions found

    // Calculate total evidence score
    float total_evidence() const {
        float total = 0.0f;
        for (const auto& e : evidence) {
            total += e.weight;
        }
        return total;
    }

    // Check if has specific evidence type
    bool has_evidence_type(Evidence::Type type) const {
        for (const auto& e : evidence) {
            if (e.type == type) return true;
        }
        return false;
    }
};

// Promotion criteria
struct PromotionCriteria {
    float min_evidence_score = 2.0f;     // Minimum total evidence
    uint32_t min_recall_count = 3;       // Minimum times recalled
    uint64_t min_quarantine_ms = 86400000;  // 1 day minimum
    uint32_t max_contradictions = 0;     // Maximum allowed contradictions
    bool require_user_approval = false;  // Must have UserApproval evidence
    bool require_episode_support = true; // Must have EpisodeSupport evidence
};

// Synthesis queue manager
class SynthesisQueue {
public:
    explicit SynthesisQueue(PromotionCriteria criteria = {})
        : criteria_(criteria) {}

    // Stage new wisdom for evaluation
    void stage(const NodeId& id, const std::string& content, Timestamp now) {
        StagedWisdom sw;
        sw.id = id;
        sw.content = content;
        sw.status = StagingStatus::Pending;
        sw.staged_at = now;
        sw.status_changed_at = now;
        staged_[id] = sw;
    }

    // Add evidence for staged wisdom
    void add_evidence(const NodeId& id, const Evidence& evidence) {
        auto it = staged_.find(id);
        if (it == staged_.end()) return;

        it->second.evidence.push_back(evidence);
        it->second.evidence_score = it->second.total_evidence();

        // Update status if enough evidence
        if (it->second.status == StagingStatus::Pending &&
            it->second.evidence_score >= criteria_.min_evidence_score * 0.5f) {
            it->second.status = StagingStatus::UnderReview;
        }
    }

    // Record a recall of staged wisdom
    void record_recall(const NodeId& id) {
        auto it = staged_.find(id);
        if (it != staged_.end()) {
            it->second.recall_count++;
        }
    }

    // Record a contradiction
    void record_contradiction(const NodeId& id) {
        auto it = staged_.find(id);
        if (it != staged_.end()) {
            it->second.contradiction_count++;
        }
    }

    // User approval
    void approve(const NodeId& id, Timestamp now) {
        auto it = staged_.find(id);
        if (it == staged_.end()) return;

        Evidence e;
        e.type = Evidence::Type::UserApproval;
        e.added_at = now;
        e.weight = 1.0f;
        e.details = "User approved";
        add_evidence(id, e);
    }

    // User rejection
    void reject(const NodeId& id, Timestamp now) {
        auto it = staged_.find(id);
        if (it == staged_.end()) return;

        it->second.status = StagingStatus::Rejected;
        it->second.status_changed_at = now;
    }

    // Check if wisdom is ready for promotion
    bool ready_for_promotion(const NodeId& id, Timestamp now) const {
        auto it = staged_.find(id);
        if (it == staged_.end()) return false;

        const auto& sw = it->second;

        // Already promoted or rejected
        if (sw.status == StagingStatus::Promoted ||
            sw.status == StagingStatus::Rejected) {
            return false;
        }

        // Check quarantine period
        if (now - sw.staged_at < criteria_.min_quarantine_ms) {
            return false;
        }

        // Check evidence score
        if (sw.evidence_score < criteria_.min_evidence_score) {
            return false;
        }

        // Check recall count
        if (sw.recall_count < criteria_.min_recall_count) {
            return false;
        }

        // Check contradictions
        if (sw.contradiction_count > criteria_.max_contradictions) {
            return false;
        }

        // Check required evidence types
        if (criteria_.require_user_approval &&
            !sw.has_evidence_type(Evidence::Type::UserApproval)) {
            return false;
        }

        if (criteria_.require_episode_support &&
            !sw.has_evidence_type(Evidence::Type::EpisodeSupport)) {
            return false;
        }

        return true;
    }

    // Promote wisdom (mark as ready for integration)
    bool promote(const NodeId& id, Timestamp now) {
        auto it = staged_.find(id);
        if (it == staged_.end()) return false;

        if (!ready_for_promotion(id, now)) return false;

        it->second.status = StagingStatus::Promoted;
        it->second.status_changed_at = now;

        // Add time-matured evidence
        Evidence e;
        e.type = Evidence::Type::TimeMatured;
        e.added_at = now;
        e.weight = 0.5f;
        e.details = "Survived quarantine period";
        it->second.evidence.push_back(e);

        return true;
    }

    // Get all wisdom ready for promotion
    std::vector<NodeId> get_promotable(Timestamp now) const {
        std::vector<NodeId> result;
        for (const auto& [id, sw] : staged_) {
            if (ready_for_promotion(id, now)) {
                result.push_back(id);
            }
        }
        return result;
    }

    // Get all pending/under review wisdom
    std::vector<StagedWisdom> get_pending() const {
        std::vector<StagedWisdom> result;
        for (const auto& [_, sw] : staged_) {
            if (sw.status == StagingStatus::Pending ||
                sw.status == StagingStatus::UnderReview) {
                result.push_back(sw);
            }
        }
        return result;
    }

    // Get staging info for a node
    const StagedWisdom* get(const NodeId& id) const {
        auto it = staged_.find(id);
        return (it != staged_.end()) ? &it->second : nullptr;
    }

    // Check if node is in staging
    bool is_staged(const NodeId& id) const {
        auto it = staged_.find(id);
        if (it == staged_.end()) return false;
        return it->second.status != StagingStatus::Promoted &&
               it->second.status != StagingStatus::Rejected;
    }

    // Remove from staging (after promotion or deletion)
    void remove(const NodeId& id) {
        staged_.erase(id);
    }

    // Cleanup old rejected entries
    size_t cleanup_rejected(Timestamp cutoff) {
        size_t removed = 0;
        for (auto it = staged_.begin(); it != staged_.end(); ) {
            if (it->second.status == StagingStatus::Rejected &&
                it->second.status_changed_at < cutoff) {
                it = staged_.erase(it);
                removed++;
            } else {
                ++it;
            }
        }
        return removed;
    }

    // Statistics
    size_t staged_count() const { return staged_.size(); }

    size_t pending_count() const {
        size_t count = 0;
        for (const auto& [_, sw] : staged_) {
            if (sw.status == StagingStatus::Pending ||
                sw.status == StagingStatus::UnderReview) {
                count++;
            }
        }
        return count;
    }

    // Get/set criteria
    const PromotionCriteria& criteria() const { return criteria_; }
    void set_criteria(const PromotionCriteria& c) { criteria_ = c; }

    // Persistence (atomic: write temp → fsync → rename)
    bool save(const std::string& path) const {
        return safe_save(path, [this](FILE* f) {
            uint32_t magic = 0x53594E51;  // "SYNQ"
            uint32_t version = 1;
            uint64_t count = staged_.size();

            if (fwrite(&magic, sizeof(magic), 1, f) != 1) return false;
            if (fwrite(&version, sizeof(version), 1, f) != 1) return false;
            if (fwrite(&count, sizeof(count), 1, f) != 1) return false;

            for (const auto& [id, sw] : staged_) {
                if (fwrite(&id.high, sizeof(id.high), 1, f) != 1) return false;
                if (fwrite(&id.low, sizeof(id.low), 1, f) != 1) return false;
                if (fwrite(&sw.status, sizeof(sw.status), 1, f) != 1) return false;
                if (fwrite(&sw.staged_at, sizeof(sw.staged_at), 1, f) != 1) return false;
                if (fwrite(&sw.status_changed_at, sizeof(sw.status_changed_at), 1, f) != 1) return false;
                if (fwrite(&sw.evidence_score, sizeof(sw.evidence_score), 1, f) != 1) return false;
                if (fwrite(&sw.recall_count, sizeof(sw.recall_count), 1, f) != 1) return false;
                if (fwrite(&sw.contradiction_count, sizeof(sw.contradiction_count), 1, f) != 1) return false;

                uint16_t content_len = static_cast<uint16_t>(std::min(sw.content.size(), size_t(65535)));
                if (fwrite(&content_len, sizeof(content_len), 1, f) != 1) return false;
                if (fwrite(sw.content.data(), 1, content_len, f) != content_len) return false;

                uint16_t ev_count = static_cast<uint16_t>(sw.evidence.size());
                if (fwrite(&ev_count, sizeof(ev_count), 1, f) != 1) return false;
                for (const auto& e : sw.evidence) {
                    if (fwrite(&e.type, sizeof(e.type), 1, f) != 1) return false;
                    if (fwrite(&e.source.high, sizeof(e.source.high), 1, f) != 1) return false;
                    if (fwrite(&e.source.low, sizeof(e.source.low), 1, f) != 1) return false;
                    if (fwrite(&e.added_at, sizeof(e.added_at), 1, f) != 1) return false;
                    if (fwrite(&e.weight, sizeof(e.weight), 1, f) != 1) return false;

                    uint16_t det_len = static_cast<uint16_t>(std::min(e.details.size(), size_t(1000)));
                    if (fwrite(&det_len, sizeof(det_len), 1, f) != 1) return false;
                    if (fwrite(e.details.data(), 1, det_len, f) != det_len) return false;
                }
            }
            return true;
        });
    }

    bool load(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;

        uint32_t magic, version;
        uint64_t count;

        if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x53594E51 ||
            fread(&version, sizeof(version), 1, f) != 1 || version != 1 ||
            fread(&count, sizeof(count), 1, f) != 1 || count > 10000000) {
            fclose(f);
            return false;
        }

        staged_.clear();
        for (uint64_t i = 0; i < count; ++i) {
            StagedWisdom sw;
            NodeId id;

            if (fread(&id.high, sizeof(id.high), 1, f) != 1 ||
                fread(&id.low, sizeof(id.low), 1, f) != 1 ||
                fread(&sw.status, sizeof(sw.status), 1, f) != 1 ||
                fread(&sw.staged_at, sizeof(sw.staged_at), 1, f) != 1 ||
                fread(&sw.status_changed_at, sizeof(sw.status_changed_at), 1, f) != 1 ||
                fread(&sw.evidence_score, sizeof(sw.evidence_score), 1, f) != 1 ||
                fread(&sw.recall_count, sizeof(sw.recall_count), 1, f) != 1 ||
                fread(&sw.contradiction_count, sizeof(sw.contradiction_count), 1, f) != 1) {
                fclose(f);
                return false;
            }

            sw.id = id;

            uint16_t content_len;
            if (fread(&content_len, sizeof(content_len), 1, f) != 1) {
                fclose(f);
                return false;
            }
            sw.content.resize(content_len);
            if (fread(&sw.content[0], 1, content_len, f) != content_len) {
                fclose(f);
                return false;
            }

            uint16_t ev_count;
            if (fread(&ev_count, sizeof(ev_count), 1, f) != 1 || ev_count > 1000) {
                fclose(f);
                return false;
            }

            for (uint16_t j = 0; j < ev_count; ++j) {
                Evidence e;
                if (fread(&e.type, sizeof(e.type), 1, f) != 1 ||
                    fread(&e.source.high, sizeof(e.source.high), 1, f) != 1 ||
                    fread(&e.source.low, sizeof(e.source.low), 1, f) != 1 ||
                    fread(&e.added_at, sizeof(e.added_at), 1, f) != 1 ||
                    fread(&e.weight, sizeof(e.weight), 1, f) != 1) {
                    fclose(f);
                    return false;
                }

                uint16_t det_len;
                if (fread(&det_len, sizeof(det_len), 1, f) != 1 || det_len > 10000) {
                    fclose(f);
                    return false;
                }
                e.details.resize(det_len);
                if (fread(&e.details[0], 1, det_len, f) != det_len) {
                    fclose(f);
                    return false;
                }
                sw.evidence.push_back(e);
            }

            staged_[id] = sw;
        }

        fclose(f);
        return true;
    }

private:
    PromotionCriteria criteria_;
    std::unordered_map<NodeId, StagedWisdom, NodeIdHash> staged_;
};

} // namespace chitta
