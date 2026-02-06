#pragma once
// Subconscious: Background processor for autonomous learning
//
// Runs as a daemon thread, detecting learning opportunities from
// user/assistant messages and closing feedback loops automatically.
//
// Architecture:
//   DuckDBMind ← Subconscious (background thread) ← Event Queue ← Hooks/RPC
//
// Capabilities:
//   - Pattern detection (correction, preference, frustration, milestone)
//   - Suggestion tracking with outcome verification
//   - Anticipation pattern learning
//   - Periodic hygiene (memory consolidation)

#include "duckdb_mind.hpp"
#include <string>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <regex>
#include <chrono>

namespace chitta {

// Event types the subconscious processes
enum class SubconsciousEventType {
    UserMessage,       // For pattern detection (corrections, preferences, etc.)
    AssistantMessage,  // For suggestion tracking
    ToolResult,        // For anticipation verification
};

// Event pushed from RPC handlers
struct SubconsciousEvent {
    SubconsciousEventType type;
    std::string content;
    std::string realm;
    int64_t timestamp;

    SubconsciousEvent() = default;
    SubconsciousEvent(SubconsciousEventType t, std::string c, std::string r, int64_t ts)
        : type(t), content(std::move(c)), realm(std::move(r)), timestamp(ts) {}
};

// Configuration for subconscious behavior
struct SubconsciousConfig {
    std::chrono::seconds process_interval{1};
    std::chrono::minutes hygiene_interval{30};
    std::chrono::minutes theme_maintenance_interval{60};  // Theme maintenance every hour
    std::chrono::minutes distillation_interval{120};      // Auto-distillation every 2 hours
    std::chrono::seconds embedding_interval{30};  // Background embedding interval (30s to reduce CPU)
    std::chrono::seconds idle_threshold{30};      // Only embed when no queries for this long
    size_t max_queue_size{1000};
    size_t embedding_batch_size{20};              // Small batches to avoid blocking
    float correction_confidence{0.8f};
    bool enable_hygiene{true};
    bool enable_theme_maintenance{true};          // xMemory theme maintenance
    bool enable_distillation{true};               // Auto-distill episodes into wisdom
    bool enable_anticipation{true};
    bool enable_pattern_detection{true};
    bool enable_suggestion_tracking{true};
    bool enable_background_embedding{false};      // Disabled by default - use embed_symbols tool
    bool enable_habit_formation{true};            // Auto-detect tool patterns and form habits
};

// Queued embedding result (computed in background, flushed by main thread)
struct QueuedEmbedding {
    int64_t symbol_id;
    std::vector<float> embedding;
};

// Runtime statistics
struct SubconsciousStats {
    std::atomic<size_t> events_processed{0};
    std::atomic<size_t> corrections_detected{0};
    std::atomic<size_t> preferences_detected{0};
    std::atomic<size_t> frustrations_detected{0};
    std::atomic<size_t> milestones_detected{0};
    std::atomic<size_t> uncertainties_detected{0};
    std::atomic<size_t> suggestions_tracked{0};
    std::atomic<size_t> outcomes_verified{0};
    std::atomic<size_t> hygiene_runs{0};
    std::atomic<size_t> theme_maintenance_runs{0};    // xMemory theme maintenance
    std::atomic<size_t> themes_split{0};
    std::atomic<size_t> themes_merged{0};
    std::atomic<size_t> memories_reassigned{0};
    std::atomic<size_t> distillation_runs{0};         // Auto-distillation runs
    std::atomic<size_t> wisdom_created{0};            // Wisdom nodes created from distillation
    std::atomic<size_t> symbols_embedded{0};
    std::atomic<size_t> embeddings_queued{0};
    std::atomic<size_t> embedding_skips{0};       // Skipped due to busy state
    std::atomic<size_t> habits_formed{0};         // Habits auto-created from tool patterns
    std::atomic<size_t> habits_matched{0};        // Existing habits matched by patterns
    std::atomic<int64_t> last_hygiene_at{0};
    std::atomic<int64_t> last_theme_maintenance_at{0};
    std::atomic<int64_t> last_distillation_at{0};
    std::atomic<int64_t> last_embedding_at{0};
    std::atomic<int64_t> last_query_at{0};        // Last RPC query timestamp
    std::atomic<int64_t> started_at{0};
};

// Tracked suggestion for outcome verification
struct TrackedSuggestion {
    std::string content;
    std::string context;
    std::string realm;
    int64_t suggested_at;
    int64_t db_id;  // ID in suggestions table
};

class Subconscious {
public:
    explicit Subconscious(DuckDBMind* mind, SubconsciousConfig config = {});
    ~Subconscious();

    // Lifecycle
    void start();
    void stop();
    bool is_running() const { return running_.load(); }

    // Event interface (non-blocking)
    void push_event(SubconsciousEvent event);

    // Statistics
    const SubconsciousStats& stats() const { return stats_; }

    // Access to config (for RPC stats)
    const SubconsciousConfig& config() const { return config_; }

    // Embedding queue (call from main thread to flush queued embeddings to DB)
    size_t flush_embedding_queue();
    size_t embedding_queue_size() const;

    // Query notification (call from RPC handlers to signal daemon is busy)
    void notify_query();
    bool is_idle() const;

private:
    DuckDBMind* mind_;
    SubconsciousConfig config_;
    SubconsciousStats stats_;

    // Threading
    std::thread process_thread_;
    std::atomic<bool> running_{false};
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<SubconsciousEvent> event_queue_;

    // Embedding queue (filled by background thread, flushed by main thread)
    mutable std::mutex embedding_queue_mutex_;
    std::vector<QueuedEmbedding> embedding_queue_;

    // Pattern matchers (compiled regexes)
    // Known limitation: std::regex is slow (~10-100x slower than RE2 or manual
    // string matching). Acceptable here because pattern detection runs on a
    // background thread with low frequency. If this becomes a hot path,
    // consider replacing with RE2 or simple string::find-based matching.
    std::regex correction_pattern_;
    std::regex preference_pattern_;
    std::regex frustration_pattern_;
    std::regex milestone_pattern_;
    std::regex suggestion_pattern_;
    std::regex uncertainty_pattern_;

    // Tracked suggestions for outcome verification
    std::mutex suggestions_mutex_;
    std::vector<TrackedSuggestion> pending_suggestions_;
    static constexpr size_t MAX_PENDING_SUGGESTIONS = 50;

    // Anticipation patterns (context -> predicted action)
    std::mutex anticipation_mutex_;
    std::string last_context_;
    std::string last_predicted_action_;

    // Habit formation from tool sequences
    std::deque<std::pair<std::string, std::string>> recent_tool_sequence_;
    static constexpr size_t MAX_TOOL_SEQUENCE = 10;
    std::mutex tool_sequence_mutex_;

    // Main processing loop
    void process_loop();

    // Event handlers
    void process_user_message(const SubconsciousEvent& event);
    void process_assistant_message(const SubconsciousEvent& event);
    void process_tool_result(const SubconsciousEvent& event);

    // Pattern detection
    void detect_correction(const std::string& content, const std::string& realm);
    void detect_preference(const std::string& content, const std::string& realm);
    void detect_frustration(const std::string& content, const std::string& realm);
    void detect_milestone(const std::string& content, const std::string& realm);
    void detect_uncertainty(const std::string& content, const std::string& realm);

    // Auto-learning (stores to DuckDBStore)
    void store_correction(const std::string& context, const std::string& correction,
                          const std::string& realm);
    void store_preference(const std::string& preference, const std::string& realm);
    void store_frustration(const std::string& context, const std::string& realm);
    void store_milestone(const std::string& achievement, const std::string& realm);
    void store_uncertainty(const std::string& context, const std::string& realm);

    // Suggestion tracking
    void track_suggestion(const std::string& content, const std::string& context,
                          const std::string& realm);
    void check_outcomes(const std::string& user_message, const std::string& realm);

    // Anticipation
    void observe_pattern(const std::string& context, const std::string& action,
                         const std::string& realm);
    void verify_prediction(const std::string& actual_action, const std::string& realm);

    // Habit formation from tool sequences
    void observe_tool_for_habit(const std::string& tool_name, const std::string& context,
                                 const std::string& realm);

    // Periodic tasks
    void run_hygiene();
    bool time_for_hygiene() const;
    void run_theme_maintenance();
    bool time_for_theme_maintenance() const;
    void run_auto_distillation();
    bool time_for_distillation() const;
    void run_background_embedding();
    bool time_for_embedding() const;

    // Helpers
    static int64_t now_ms();
    std::optional<SubconsciousEvent> pop_event_with_timeout(std::chrono::seconds timeout);
};

}  // namespace chitta
