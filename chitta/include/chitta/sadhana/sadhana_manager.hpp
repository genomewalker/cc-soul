#pragma once
// SadhanaManager: Unified Autonomous Agent System
//
// Manages persistent autonomous agents with sense-think-act loops.
// Each sadhana represents a goal that the agent works toward autonomously.
//
// Architecture:
//   SadhanaManager (daemon singleton)
//     |
//     +-- tick() called every second from daemon loop
//     |
//     +-- For each running sadhana with elapsed interval:
//         |
//         +-- SENSE: LLM generates observation command
//         +-- THINK: LLM decides if action needed
//         +-- ACT: Execute generated action
//         +-- LEARN: Store patterns in memory
//
// The brain (LLM) dynamically generates both sensors and actions,
// rather than using pre-defined plugins.

#include "brain_provider.hpp"
#include "../duckdb_store.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>

namespace chitta {

using json = nlohmann::json;

// Sadhana state enum
enum class SadhanaState {
    Pending,    // Created but not started
    Running,    // Actively being processed
    Paused,     // Temporarily suspended
    Done,       // Goal achieved
    Failed      // Goal unachievable
};

inline std::string sadhana_state_to_string(SadhanaState state) {
    switch (state) {
        case SadhanaState::Pending: return "pending";
        case SadhanaState::Running: return "running";
        case SadhanaState::Paused:  return "paused";
        case SadhanaState::Done:    return "done";
        case SadhanaState::Failed:  return "failed";
        default: return "unknown";
    }
}

inline SadhanaState string_to_sadhana_state(const std::string& s) {
    if (s == "pending") return SadhanaState::Pending;
    if (s == "running") return SadhanaState::Running;
    if (s == "paused")  return SadhanaState::Paused;
    if (s == "done")    return SadhanaState::Done;
    if (s == "failed")  return SadhanaState::Failed;
    return SadhanaState::Pending;
}

// Sadhana data structure
struct Sadhana {
    int64_t id = 0;
    std::string goal;
    json goal_dsl;                  // Structured goal (optional)
    SadhanaState state = SadhanaState::Pending;
    std::string brain_provider;     // "claude" or "opencode"
    std::string brain_model;        // "sonnet", "opus", "haiku", etc.
    int64_t created_at = 0;
    int64_t updated_at = 0;
    int iterations = 0;
    json last_sense;                // Last observation
    std::string last_action;        // Last action taken
    json last_result;               // Result of last action
    int brain_calls = 0;            // Total LLM invocations
    json learned_patterns;          // Patterns learned
    int interval_seconds = 60;      // Time between cycles
    std::string realm = "brahman";
};

// Sadhana history event types
enum class SadhanaEventType {
    Created,
    Started,
    Paused,
    Resumed,
    Sense,
    Think,
    Act,
    Learn,
    Done,
    Failed,
    ModelChanged,
    GoalChanged
};

inline std::string sadhana_event_type_to_string(SadhanaEventType type) {
    switch (type) {
        case SadhanaEventType::Created:      return "created";
        case SadhanaEventType::Started:      return "started";
        case SadhanaEventType::Paused:       return "paused";
        case SadhanaEventType::Resumed:      return "resumed";
        case SadhanaEventType::Sense:        return "sense";
        case SadhanaEventType::Think:        return "think";
        case SadhanaEventType::Act:          return "act";
        case SadhanaEventType::Learn:        return "learn";
        case SadhanaEventType::Done:         return "done";
        case SadhanaEventType::Failed:       return "failed";
        case SadhanaEventType::ModelChanged: return "model_changed";
        case SadhanaEventType::GoalChanged:  return "goal_changed";
        default: return "unknown";
    }
}

// Configuration for the manager
struct SadhanaConfig {
    int max_concurrent = 3;         // Max running sadhanas
    int max_brain_timeout_ms = 300000;  // 5 minute max per LLM call
    int default_interval_seconds = 60;
    bool enable_learning = true;    // Store learned patterns in memory
    std::string default_brain_provider = "claude";
    std::string default_brain_model = "sonnet";
};

// Statistics for monitoring
struct SadhanaStats {
    std::atomic<size_t> total_created{0};
    std::atomic<size_t> total_completed{0};
    std::atomic<size_t> total_failed{0};
    std::atomic<size_t> total_brain_calls{0};
    std::atomic<size_t> total_actions{0};
    std::atomic<int64_t> last_tick_at{0};
    std::atomic<size_t> active_count{0};
};

class SadhanaManager {
public:
    explicit SadhanaManager(DuckDBStore& store, SadhanaConfig config = {});
    ~SadhanaManager() = default;

    // CRUD operations
    int64_t create(const std::string& goal,
                   const std::string& brain_provider = "",
                   const std::string& brain_model = "",
                   int interval_seconds = 0,
                   const std::string& realm = "brahman",
                   const json& goal_dsl = json());

    bool start(int64_t id);
    bool pause(int64_t id);
    bool resume(int64_t id);
    bool stop(int64_t id, bool success = true, const std::string& reason = "");

    // Query operations
    std::optional<Sadhana> get(int64_t id);
    std::vector<Sadhana> list(const std::string& state_filter = "",
                               const std::string& realm = "",
                               size_t limit = 50);
    std::vector<Sadhana> list_active();

    // Configuration
    bool set_model(int64_t id, const std::string& model);
    bool set_interval(int64_t id, int interval_seconds);
    bool set_goal(int64_t id, const std::string& goal);

    // History
    std::vector<json> get_history(int64_t id, size_t limit = 50);

    // Called from daemon's background loop every second
    void tick();

    // Statistics
    const SadhanaStats& stats() const { return stats_; }
    const SadhanaConfig& config() const { return config_; }

private:
    DuckDBStore& store_;
    SadhanaConfig config_;
    SadhanaStats stats_;
    mutable std::mutex mutex_;

    // Cache of running sadhanas with their next run time
    struct RunningState {
        int64_t next_run_at = 0;
        std::unique_ptr<BrainProvider> brain;
    };
    std::unordered_map<int64_t, RunningState> running_;

    // Core sense-think-act cycle
    void run_cycle(Sadhana& sadhana);

    // Individual phases
    json sense(Sadhana& sadhana, BrainProvider& brain);
    json think(Sadhana& sadhana, BrainProvider& brain, const json& observation);
    json act(Sadhana& sadhana, const json& decision);
    void learn(Sadhana& sadhana, const json& observation, const json& decision, const json& result);

    // Persistence helpers
    bool save_sadhana(const Sadhana& s);
    bool log_event(int64_t sadhana_id, SadhanaEventType type,
                   const json& content = json(), int duration_ms = 0);

    // Time utilities
    static int64_t now_ms();
};

}  // namespace chitta
