#pragma once
// Gap-Driven Inquiry: Active learning from knowledge gaps
//
// Generates questions from Gap nodes.
// Prioritizes important gaps for inquiry.
// Stores answers directly when resolved.
//
// Enables proactive knowledge acquisition.

#include "types.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <algorithm>

namespace chitta {

// Gap importance level
enum class GapImportance : uint8_t {
    Low = 0,         // Nice to know
    Medium = 1,      // Useful knowledge
    High = 2,        // Important for current work
    Critical = 3,    // Blocking progress
};

// Gap status
enum class GapStatus : uint8_t {
    Open = 0,        // Needs answer
    Pending = 1,     // Question asked, awaiting response
    Answered = 2,    // Has answer
    Dismissed = 3,   // Determined not important
};

// A knowledge gap
struct KnowledgeGap {
    NodeId id;                    // Gap node ID
    std::string topic;            // What the gap is about
    std::string question;         // Generated question
    std::string context;          // Why this gap matters
    GapImportance importance = GapImportance::Medium;
    GapStatus status = GapStatus::Open;
    Timestamp detected_at = 0;    // When gap was identified
    Timestamp asked_at = 0;       // When question was asked
    Timestamp answered_at = 0;    // When answer was received

    // Related nodes (what triggered this gap)
    std::vector<NodeId> related_nodes;

    // Answer when resolved
    NodeId answer_node;           // Node containing the answer
    std::string answer_preview;   // Short preview of answer

    // Metrics
    uint32_t ask_count = 0;       // Times this question was asked
    uint32_t recall_count = 0;    // Times gap was encountered during recall
};

// Gap inquiry configuration
struct GapInquiryConfig {
    size_t max_active_gaps = 100;           // Maximum gaps to track
    uint32_t recall_threshold = 3;           // Encounters before asking
    uint64_t cooldown_ms = 86400000;         // 1 day between asks
    bool auto_dismiss_low_importance = true; // Auto-dismiss low importance after time
    uint64_t auto_dismiss_ms = 604800000;    // 1 week
};

// Gap inquiry manager
class GapInquiry {
public:
    explicit GapInquiry(GapInquiryConfig config = {})
        : config_(config) {}

    // Register a new gap
    void register_gap(const KnowledgeGap& gap) {
        if (gaps_.size() >= config_.max_active_gaps) {
            // Evict lowest importance gap
            evict_lowest_importance();
        }
        gaps_[gap.id] = gap;
    }

    // Register gap with defaults
    void register_gap(const NodeId& id, const std::string& topic,
                     const std::string& question, const std::string& context = "",
                     GapImportance importance = GapImportance::Medium,
                     Timestamp now = 0) {
        KnowledgeGap gap;
        gap.id = id;
        gap.topic = topic;
        gap.question = question;
        gap.context = context;
        gap.importance = importance;
        gap.detected_at = now;
        register_gap(gap);
    }

    // Record that a gap was encountered during recall
    void record_encounter(const NodeId& id) {
        auto it = gaps_.find(id);
        if (it != gaps_.end()) {
            it->second.recall_count++;
        }
    }

    // Get gap by ID
    const KnowledgeGap* get(const NodeId& id) const {
        auto it = gaps_.find(id);
        return (it != gaps_.end()) ? &it->second : nullptr;
    }

    // Check if gap is ready to ask (enough encounters, not on cooldown)
    bool ready_to_ask(const NodeId& id, Timestamp now) const {
        auto it = gaps_.find(id);
        if (it == gaps_.end()) return false;

        const auto& gap = it->second;
        if (gap.status != GapStatus::Open) return false;
        if (gap.recall_count < config_.recall_threshold) return false;
        if (gap.ask_count > 0 && now - gap.asked_at < config_.cooldown_ms) return false;

        return true;
    }

    // Get next gap to ask about (highest priority, most encountered)
    const KnowledgeGap* next_to_ask(Timestamp now) const {
        const KnowledgeGap* best = nullptr;
        float best_score = -1.0f;

        for (const auto& [_, gap] : gaps_) {
            if (!ready_to_ask(gap.id, now)) continue;

            // Score: importance * recall_count
            float score = static_cast<float>(gap.importance) * gap.recall_count;
            if (score > best_score) {
                best_score = score;
                best = &gap;
            }
        }

        return best;
    }

    // Get top N gaps to ask about
    std::vector<KnowledgeGap> get_inquiry_queue(size_t n, Timestamp now) const {
        std::vector<std::pair<float, NodeId>> scored;

        for (const auto& [id, gap] : gaps_) {
            if (!ready_to_ask(id, now)) continue;
            float score = static_cast<float>(gap.importance) * gap.recall_count;
            scored.push_back({score, id});
        }

        std::sort(scored.begin(), scored.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        std::vector<KnowledgeGap> result;
        for (size_t i = 0; i < std::min(n, scored.size()); ++i) {
            result.push_back(gaps_.at(scored[i].second));
        }
        return result;
    }

    // Mark gap as asked
    void mark_asked(const NodeId& id, Timestamp now) {
        auto it = gaps_.find(id);
        if (it == gaps_.end()) return;

        it->second.status = GapStatus::Pending;
        it->second.asked_at = now;
        it->second.ask_count++;
    }

    // Provide answer to a gap
    void answer(const NodeId& gap_id, const NodeId& answer_node,
               const std::string& answer_preview, Timestamp now) {
        auto it = gaps_.find(gap_id);
        if (it == gaps_.end()) return;

        it->second.status = GapStatus::Answered;
        it->second.answer_node = answer_node;
        it->second.answer_preview = answer_preview;
        it->second.answered_at = now;
    }

    // Dismiss a gap
    void dismiss(const NodeId& id, const std::string& reason = "") {
        auto it = gaps_.find(id);
        if (it == gaps_.end()) return;
        it->second.status = GapStatus::Dismissed;
        it->second.context += " [Dismissed: " + reason + "]";
    }

    // Update importance
    void set_importance(const NodeId& id, GapImportance importance) {
        auto it = gaps_.find(id);
        if (it != gaps_.end()) {
            it->second.importance = importance;
        }
    }

    // Get all gaps by status
    std::vector<KnowledgeGap> get_by_status(GapStatus status) const {
        std::vector<KnowledgeGap> result;
        for (const auto& [_, gap] : gaps_) {
            if (gap.status == status) {
                result.push_back(gap);
            }
        }
        return result;
    }

    // Get open gaps
    std::vector<KnowledgeGap> get_open_gaps() const {
        return get_by_status(GapStatus::Open);
    }

    // Run maintenance (auto-dismiss, cleanup)
    size_t maintain(Timestamp now) {
        size_t changes = 0;

        for (auto& [id, gap] : gaps_) {
            // Auto-dismiss old low-importance gaps
            if (config_.auto_dismiss_low_importance &&
                gap.importance == GapImportance::Low &&
                gap.status == GapStatus::Open &&
                now - gap.detected_at > config_.auto_dismiss_ms) {
                gap.status = GapStatus::Dismissed;
                gap.context += " [Auto-dismissed]";
                changes++;
            }

            // Reset pending to open after cooldown
            if (gap.status == GapStatus::Pending &&
                now - gap.asked_at > config_.cooldown_ms * 2) {
                gap.status = GapStatus::Open;
                changes++;
            }
        }

        return changes;
    }

    // Remove gap
    void remove(const NodeId& id) {
        gaps_.erase(id);
    }

    // Statistics
    struct GapStats {
        size_t total;
        size_t open;
        size_t pending;
        size_t answered;
        size_t dismissed;
        size_t critical;
        size_t high;
    };

    GapStats get_stats() const {
        GapStats stats{};
        stats.total = gaps_.size();

        for (const auto& [_, gap] : gaps_) {
            switch (gap.status) {
                case GapStatus::Open: stats.open++; break;
                case GapStatus::Pending: stats.pending++; break;
                case GapStatus::Answered: stats.answered++; break;
                case GapStatus::Dismissed: stats.dismissed++; break;
            }

            if (gap.importance == GapImportance::Critical) stats.critical++;
            else if (gap.importance == GapImportance::High) stats.high++;
        }

        return stats;
    }

    size_t count() const { return gaps_.size(); }

    // Configuration
    const GapInquiryConfig& config() const { return config_; }
    void set_config(const GapInquiryConfig& c) { config_ = c; }

    // Persistence
    bool save(const std::string& path) const {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) return false;

        uint32_t magic = 0x47415049;  // "GAPI"
        uint32_t version = 1;
        uint64_t count = gaps_.size();

        fwrite(&magic, sizeof(magic), 1, f);
        fwrite(&version, sizeof(version), 1, f);
        fwrite(&count, sizeof(count), 1, f);

        for (const auto& [id, gap] : gaps_) {
            fwrite(&id.high, sizeof(id.high), 1, f);
            fwrite(&id.low, sizeof(id.low), 1, f);
            fwrite(&gap.importance, sizeof(gap.importance), 1, f);
            fwrite(&gap.status, sizeof(gap.status), 1, f);
            fwrite(&gap.detected_at, sizeof(gap.detected_at), 1, f);
            fwrite(&gap.asked_at, sizeof(gap.asked_at), 1, f);
            fwrite(&gap.answered_at, sizeof(gap.answered_at), 1, f);
            fwrite(&gap.ask_count, sizeof(gap.ask_count), 1, f);
            fwrite(&gap.recall_count, sizeof(gap.recall_count), 1, f);
            fwrite(&gap.answer_node.high, sizeof(gap.answer_node.high), 1, f);
            fwrite(&gap.answer_node.low, sizeof(gap.answer_node.low), 1, f);

            auto write_string = [f](const std::string& s) {
                uint16_t len = static_cast<uint16_t>(std::min(s.size(), size_t(65535)));
                fwrite(&len, sizeof(len), 1, f);
                fwrite(s.data(), 1, len, f);
            };

            write_string(gap.topic);
            write_string(gap.question);
            write_string(gap.context);
            write_string(gap.answer_preview);

            uint16_t rel_count = static_cast<uint16_t>(gap.related_nodes.size());
            fwrite(&rel_count, sizeof(rel_count), 1, f);
            for (const auto& rel : gap.related_nodes) {
                fwrite(&rel.high, sizeof(rel.high), 1, f);
                fwrite(&rel.low, sizeof(rel.low), 1, f);
            }
        }

        fclose(f);
        return true;
    }

    bool load(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;

        uint32_t magic, version;
        uint64_t count;

        if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x47415049 ||
            fread(&version, sizeof(version), 1, f) != 1 || version != 1 ||
            fread(&count, sizeof(count), 1, f) != 1 || count > 1000000) {
            fclose(f);
            return false;
        }

        gaps_.clear();

        auto read_string = [f]() -> std::string {
            uint16_t len;
            if (fread(&len, sizeof(len), 1, f) != 1 || len > 65535) return "";
            std::string s(len, '\0');
            if (fread(&s[0], 1, len, f) != len) return "";
            return s;
        };

        for (uint64_t i = 0; i < count; ++i) {
            KnowledgeGap gap;
            NodeId id;

            if (fread(&id.high, sizeof(id.high), 1, f) != 1 ||
                fread(&id.low, sizeof(id.low), 1, f) != 1 ||
                fread(&gap.importance, sizeof(gap.importance), 1, f) != 1 ||
                fread(&gap.status, sizeof(gap.status), 1, f) != 1 ||
                fread(&gap.detected_at, sizeof(gap.detected_at), 1, f) != 1 ||
                fread(&gap.asked_at, sizeof(gap.asked_at), 1, f) != 1 ||
                fread(&gap.answered_at, sizeof(gap.answered_at), 1, f) != 1 ||
                fread(&gap.ask_count, sizeof(gap.ask_count), 1, f) != 1 ||
                fread(&gap.recall_count, sizeof(gap.recall_count), 1, f) != 1 ||
                fread(&gap.answer_node.high, sizeof(gap.answer_node.high), 1, f) != 1 ||
                fread(&gap.answer_node.low, sizeof(gap.answer_node.low), 1, f) != 1) {
                fclose(f);
                return false;
            }

            gap.id = id;
            gap.topic = read_string();
            gap.question = read_string();
            gap.context = read_string();
            gap.answer_preview = read_string();

            uint16_t rel_count;
            if (fread(&rel_count, sizeof(rel_count), 1, f) != 1 || rel_count > 1000) {
                fclose(f);
                return false;
            }

            for (uint16_t j = 0; j < rel_count; ++j) {
                NodeId rel;
                if (fread(&rel.high, sizeof(rel.high), 1, f) != 1 ||
                    fread(&rel.low, sizeof(rel.low), 1, f) != 1) {
                    fclose(f);
                    return false;
                }
                gap.related_nodes.push_back(rel);
            }

            gaps_[id] = gap;
        }

        fclose(f);
        return true;
    }

private:
    void evict_lowest_importance() {
        NodeId to_evict;
        GapImportance lowest = GapImportance::Critical;
        Timestamp oldest = UINT64_MAX;

        for (const auto& [id, gap] : gaps_) {
            // Don't evict answered or high importance
            if (gap.status == GapStatus::Answered) continue;
            if (gap.importance == GapImportance::Critical) continue;

            if (gap.importance < lowest ||
                (gap.importance == lowest && gap.detected_at < oldest)) {
                lowest = gap.importance;
                oldest = gap.detected_at;
                to_evict = id;
            }
        }

        if (to_evict.valid()) {
            gaps_.erase(to_evict);
        }
    }

    GapInquiryConfig config_;
    std::unordered_map<NodeId, KnowledgeGap, NodeIdHash> gaps_;
};

} // namespace chitta
