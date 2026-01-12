#pragma once
// Wisdom Review Queue: Human oversight for knowledge quality
//
// Allows users to accept/reject/edit synthesized wisdom.
// Stores feedback signals for learning.
// Supports batch review mode.
//
// Human in the loop for critical knowledge validation.

#include "types.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <functional>

namespace chitta {

// Review status
enum class ReviewStatus : uint8_t {
    Pending = 0,        // Awaiting review
    Approved = 1,       // Accepted as-is
    Edited = 2,         // Accepted with edits
    Rejected = 3,       // Rejected entirely
    Deferred = 4,       // Review postponed
};

// Priority levels for review
enum class ReviewPriority : uint8_t {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3,
};

// Item in review queue
struct ReviewItem {
    NodeId id;
    NodeType type;
    std::string content;
    std::string context;          // Why this needs review
    ReviewStatus status = ReviewStatus::Pending;
    ReviewPriority priority = ReviewPriority::Normal;
    Timestamp queued_at = 0;
    Timestamp reviewed_at = 0;

    // Feedback
    std::string reviewer_comment;
    std::string edited_content;   // If edited
    float quality_rating = 0.0f;  // 1-5 scale, 0 = not rated

    // Source tracking
    std::string source_session;
    std::string source_tool;
};

// Review statistics
struct ReviewStats {
    size_t pending;
    size_t approved;
    size_t edited;
    size_t rejected;
    size_t deferred;
    float avg_quality_rating;
    float approval_rate;
};

// Review queue manager
class ReviewQueue {
public:
    ReviewQueue() = default;

    // Add item to review queue
    void enqueue(const ReviewItem& item) {
        items_[item.id] = item;
        if (item.status == ReviewStatus::Pending) {
            priority_queue_.push({item.id, item.priority, item.queued_at});
        }
    }

    // Enqueue with defaults
    void enqueue(const NodeId& id, NodeType type, const std::string& content,
                const std::string& context = "", ReviewPriority priority = ReviewPriority::Normal,
                Timestamp now = 0) {
        ReviewItem item;
        item.id = id;
        item.type = type;
        item.content = content;
        item.context = context;
        item.priority = priority;
        item.queued_at = now;
        enqueue(item);
    }

    // Get next item to review (highest priority, oldest first)
    const ReviewItem* next() const {
        while (!priority_queue_.empty()) {
            const auto& [id, _, __] = priority_queue_.top();
            auto it = items_.find(id);
            if (it != items_.end() && it->second.status == ReviewStatus::Pending) {
                return &it->second;
            }
            // Item no longer pending, skip it
            priority_queue_.pop();
        }
        return nullptr;
    }

    // Get item by ID
    const ReviewItem* get(const NodeId& id) const {
        auto it = items_.find(id);
        return (it != items_.end()) ? &it->second : nullptr;
    }

    // Approve item
    void approve(const NodeId& id, const std::string& comment = "",
                float quality_rating = 0.0f, Timestamp now = 0) {
        auto it = items_.find(id);
        if (it == items_.end()) return;

        it->second.status = ReviewStatus::Approved;
        it->second.reviewer_comment = comment;
        it->second.quality_rating = quality_rating;
        it->second.reviewed_at = now;
    }

    // Approve with edits
    void approve_with_edits(const NodeId& id, const std::string& edited_content,
                           const std::string& comment = "", float quality_rating = 0.0f,
                           Timestamp now = 0) {
        auto it = items_.find(id);
        if (it == items_.end()) return;

        it->second.status = ReviewStatus::Edited;
        it->second.edited_content = edited_content;
        it->second.reviewer_comment = comment;
        it->second.quality_rating = quality_rating;
        it->second.reviewed_at = now;
    }

    // Reject item
    void reject(const NodeId& id, const std::string& reason = "", Timestamp now = 0) {
        auto it = items_.find(id);
        if (it == items_.end()) return;

        it->second.status = ReviewStatus::Rejected;
        it->second.reviewer_comment = reason;
        it->second.reviewed_at = now;
    }

    // Defer review
    void defer(const NodeId& id, const std::string& reason = "") {
        auto it = items_.find(id);
        if (it == items_.end()) return;

        it->second.status = ReviewStatus::Deferred;
        it->second.reviewer_comment = reason;
    }

    // Get all pending items
    std::vector<ReviewItem> get_pending() const {
        std::vector<ReviewItem> result;
        for (const auto& [_, item] : items_) {
            if (item.status == ReviewStatus::Pending) {
                result.push_back(item);
            }
        }
        // Sort by priority (highest first) then by time (oldest first)
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            if (a.priority != b.priority) {
                return static_cast<uint8_t>(a.priority) > static_cast<uint8_t>(b.priority);
            }
            return a.queued_at < b.queued_at;
        });
        return result;
    }

    // Get items by status
    std::vector<ReviewItem> get_by_status(ReviewStatus status) const {
        std::vector<ReviewItem> result;
        for (const auto& [_, item] : items_) {
            if (item.status == status) {
                result.push_back(item);
            }
        }
        return result;
    }

    // Get batch for review (up to n items)
    std::vector<ReviewItem> get_batch(size_t n) const {
        auto pending = get_pending();
        if (pending.size() > n) {
            pending.resize(n);
        }
        return pending;
    }

    // Process batch decisions
    struct BatchDecision {
        NodeId id;
        ReviewStatus decision;
        std::string edited_content;  // For Edited status
        std::string comment;
        float quality_rating;
    };

    void process_batch(const std::vector<BatchDecision>& decisions, Timestamp now = 0) {
        for (const auto& d : decisions) {
            switch (d.decision) {
                case ReviewStatus::Approved:
                    approve(d.id, d.comment, d.quality_rating, now);
                    break;
                case ReviewStatus::Edited:
                    approve_with_edits(d.id, d.edited_content, d.comment, d.quality_rating, now);
                    break;
                case ReviewStatus::Rejected:
                    reject(d.id, d.comment, now);
                    break;
                case ReviewStatus::Deferred:
                    defer(d.id, d.comment);
                    break;
                default:
                    break;
            }
        }
    }

    // Get review statistics
    ReviewStats get_stats() const {
        ReviewStats stats{};
        float total_rating = 0.0f;
        size_t rated_count = 0;
        size_t reviewed_count = 0;

        for (const auto& [_, item] : items_) {
            switch (item.status) {
                case ReviewStatus::Pending: stats.pending++; break;
                case ReviewStatus::Approved:
                    stats.approved++;
                    reviewed_count++;
                    break;
                case ReviewStatus::Edited:
                    stats.edited++;
                    reviewed_count++;
                    break;
                case ReviewStatus::Rejected:
                    stats.rejected++;
                    reviewed_count++;
                    break;
                case ReviewStatus::Deferred: stats.deferred++; break;
            }

            if (item.quality_rating > 0) {
                total_rating += item.quality_rating;
                rated_count++;
            }
        }

        stats.avg_quality_rating = rated_count > 0 ? total_rating / rated_count : 0.0f;
        stats.approval_rate = reviewed_count > 0 ?
            static_cast<float>(stats.approved + stats.edited) / reviewed_count : 0.0f;

        return stats;
    }

    // Remove item
    void remove(const NodeId& id) {
        items_.erase(id);
    }

    // Cleanup old reviewed items
    size_t cleanup(Timestamp cutoff) {
        size_t removed = 0;
        for (auto it = items_.begin(); it != items_.end(); ) {
            if (it->second.status != ReviewStatus::Pending &&
                it->second.reviewed_at > 0 &&
                it->second.reviewed_at < cutoff) {
                it = items_.erase(it);
                removed++;
            } else {
                ++it;
            }
        }
        return removed;
    }

    size_t total_count() const { return items_.size(); }
    size_t pending_count() const {
        size_t count = 0;
        for (const auto& [_, item] : items_) {
            if (item.status == ReviewStatus::Pending) count++;
        }
        return count;
    }

    // Persistence
    bool save(const std::string& path) const {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) return false;

        uint32_t magic = 0x52455651;  // "REVQ"
        uint32_t version = 1;
        uint64_t count = items_.size();

        fwrite(&magic, sizeof(magic), 1, f);
        fwrite(&version, sizeof(version), 1, f);
        fwrite(&count, sizeof(count), 1, f);

        for (const auto& [id, item] : items_) {
            fwrite(&id.high, sizeof(id.high), 1, f);
            fwrite(&id.low, sizeof(id.low), 1, f);
            fwrite(&item.type, sizeof(item.type), 1, f);
            fwrite(&item.status, sizeof(item.status), 1, f);
            fwrite(&item.priority, sizeof(item.priority), 1, f);
            fwrite(&item.queued_at, sizeof(item.queued_at), 1, f);
            fwrite(&item.reviewed_at, sizeof(item.reviewed_at), 1, f);
            fwrite(&item.quality_rating, sizeof(item.quality_rating), 1, f);

            auto write_string = [f](const std::string& s) {
                uint16_t len = static_cast<uint16_t>(std::min(s.size(), size_t(65535)));
                fwrite(&len, sizeof(len), 1, f);
                fwrite(s.data(), 1, len, f);
            };

            write_string(item.content);
            write_string(item.context);
            write_string(item.reviewer_comment);
            write_string(item.edited_content);
            write_string(item.source_session);
            write_string(item.source_tool);
        }

        fclose(f);
        return true;
    }

    bool load(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;

        uint32_t magic, version;
        uint64_t count;

        if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x52455651 ||
            fread(&version, sizeof(version), 1, f) != 1 || version != 1 ||
            fread(&count, sizeof(count), 1, f) != 1 || count > 10000000) {
            fclose(f);
            return false;
        }

        items_.clear();
        while (!priority_queue_.empty()) priority_queue_.pop();

        auto read_string = [f]() -> std::string {
            uint16_t len;
            if (fread(&len, sizeof(len), 1, f) != 1 || len > 65535) return "";
            std::string s(len, '\0');
            if (fread(&s[0], 1, len, f) != len) return "";
            return s;
        };

        for (uint64_t i = 0; i < count; ++i) {
            ReviewItem item;
            NodeId id;

            if (fread(&id.high, sizeof(id.high), 1, f) != 1 ||
                fread(&id.low, sizeof(id.low), 1, f) != 1 ||
                fread(&item.type, sizeof(item.type), 1, f) != 1 ||
                fread(&item.status, sizeof(item.status), 1, f) != 1 ||
                fread(&item.priority, sizeof(item.priority), 1, f) != 1 ||
                fread(&item.queued_at, sizeof(item.queued_at), 1, f) != 1 ||
                fread(&item.reviewed_at, sizeof(item.reviewed_at), 1, f) != 1 ||
                fread(&item.quality_rating, sizeof(item.quality_rating), 1, f) != 1) {
                fclose(f);
                return false;
            }

            item.id = id;
            item.content = read_string();
            item.context = read_string();
            item.reviewer_comment = read_string();
            item.edited_content = read_string();
            item.source_session = read_string();
            item.source_tool = read_string();

            items_[id] = item;
            if (item.status == ReviewStatus::Pending) {
                priority_queue_.push({id, item.priority, item.queued_at});
            }
        }

        fclose(f);
        return true;
    }

private:
    struct QueueEntry {
        NodeId id;
        ReviewPriority priority;
        Timestamp queued_at;

        bool operator<(const QueueEntry& other) const {
            // Higher priority first, then older first
            if (priority != other.priority) {
                return static_cast<uint8_t>(priority) < static_cast<uint8_t>(other.priority);
            }
            return queued_at > other.queued_at;  // Older = higher priority
        }
    };

    std::unordered_map<NodeId, ReviewItem, NodeIdHash> items_;
    mutable std::priority_queue<QueueEntry> priority_queue_;
};

} // namespace chitta
