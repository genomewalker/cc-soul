#pragma once
// SadhanaManager: Fully Agentic Autonomous Agent System
//
// Each sadhana is a persistent goal pursued by a full Claude Code agent
// running with complete tool access (bash, chitta MCP, file I/O, sub-tasks).
//
// Architecture:
//   SadhanaManager (daemon singleton)
//     |
//     +-- tick() called every 100ms from daemon loop
//     |
//     +-- For each running sadhana with elapsed interval:
//         |
//         +-- Build context: memories + history + goal
//         +-- Spawn Claude Code agent with --dangerously-skip-permissions
//         +-- Agent runs N turns with full tool access
//         +-- Parse final JSON status message from agent output
//         +-- Store result, check for completion/blocking
//
// The agent calls chitta tools directly to search/store memories.
// It signals completion via exit code (10=achieved, 20=blocked) or
// a final JSON message: {"status": "progressed|achieved|blocked", "summary": "..."}

#include "brain_provider.hpp"
#include "../duckdb_store.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
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
    json last_sense;                // Legacy: last observation (unused in agentic mode)
    std::string last_action;        // Summary of what the last cycle did
    json last_result;               // Last cycle result: {status, summary, exit_code, duration_ms}
    int brain_calls = 0;            // Total agent invocations
    json learned_patterns;          // Patterns learned (legacy, agent manages its own memory)
    int interval_seconds = 300;     // Seconds between cycles (default 5 min)
    std::string realm = "brahman";
};

// Sadhana history event types
enum class SadhanaEventType {
    Created,
    Started,
    Paused,
    Resumed,
    Cycle,          // One agentic cycle completed
    Checkpoint,     // Mid-cycle agent checkpoint
    Done,
    Failed,
    ModelChanged,
    GoalChanged,
    // Legacy (kept for DB compatibility with old records)
    Sense,
    Think,
    Act,
    Learn
};

inline std::string sadhana_event_type_to_string(SadhanaEventType type) {
    switch (type) {
        case SadhanaEventType::Created:      return "created";
        case SadhanaEventType::Started:      return "started";
        case SadhanaEventType::Paused:       return "paused";
        case SadhanaEventType::Resumed:      return "resumed";
        case SadhanaEventType::Cycle:        return "cycle";
        case SadhanaEventType::Checkpoint:   return "checkpoint";
        case SadhanaEventType::Done:         return "done";
        case SadhanaEventType::Failed:       return "failed";
        case SadhanaEventType::ModelChanged: return "model_changed";
        case SadhanaEventType::GoalChanged:  return "goal_changed";
        case SadhanaEventType::Sense:        return "sense";
        case SadhanaEventType::Think:        return "think";
        case SadhanaEventType::Act:          return "act";
        case SadhanaEventType::Learn:        return "learn";
        default: return "unknown";
    }
}

// Configuration for the manager
struct SadhanaConfig {
    int max_concurrent = 3;
    int max_agent_timeout_ms = 600000;      // 10 minutes per agent cycle
    int max_agent_turns = 20;               // Max turns per cycle
    int default_interval_seconds = 300;     // 5 minutes between cycles
    bool enable_learning = true;            // Remind agent to use memory tools
    std::string default_brain_provider = "claude";
    std::string default_brain_model = "sonnet";

    // Hardening settings
    int max_consecutive_failures = 5;
    size_t max_output_chars = 4000;         // Truncate agent output stored in DB
    bool strip_ansi_codes = true;
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

    // Agent self-reporting: called by the agent subprocess via chitta CLI
    bool checkpoint(int64_t id, const std::string& status, const std::string& summary);

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

    // Event streaming: push events to subscribed clients in real-time
    using StreamFn = std::function<void(int fd, std::string line)>;
    void set_stream_fn(StreamFn fn) { stream_fn_ = std::move(fn); }
    void stream_subscribe(int fd, int64_t sadhana_id = 0);
    void stream_unsubscribe(int fd);

    // History
    std::vector<json> get_history(int64_t id, size_t limit = 50);

    // Called from daemon's background loop every 100ms
    void tick();

    // Statistics
    const SadhanaStats& stats() const { return stats_; }
    const SadhanaConfig& config() const { return config_; }

private:
    DuckDBStore& store_;
    SadhanaConfig config_;
    SadhanaStats stats_;
    mutable std::mutex mutex_;

    // Event streaming subscribers
    StreamFn stream_fn_;
    struct StreamSub { int fd; int64_t sadhana_id; };
    std::vector<StreamSub> stream_subs_;
    mutable std::mutex stream_subs_mutex_;
    void push_to_streams(int64_t sadhana_id, SadhanaEventType type,
                         const json& content, int duration_ms = 0);

    // Cache of running sadhanas with their next run time
    struct RunningState {
        int64_t next_run_at = 0;
        std::unique_ptr<BrainProvider> brain;
        int consecutive_failures = 0;
    };
    std::unordered_map<int64_t, RunningState> running_;

    // Text sanitization helpers
    static std::string strip_ansi(const std::string& text);
    static std::string truncate(const std::string& text, size_t max_chars);
    static std::string escape_sql(const std::string& text);

    // Context builders for agent prompt
    std::string build_system_prompt(const Sadhana& sadhana) const;
    std::string build_user_message(const Sadhana& sadhana,
                                    const std::string& memory_ctx,
                                    const std::string& history_ctx) const;
    std::string build_memory_context(const Sadhana& sadhana);
    std::string build_history_context(const Sadhana& sadhana);

    // Parse the last JSON status object from agent output
    static json extract_last_json(const std::string& text);

    // Core agentic cycle — returns status: "progressed", "achieved", "blocked", "failed", "stopped"
    std::string run_cycle(Sadhana& sadhana);

    // Persistence helpers
    bool log_event(int64_t sadhana_id, SadhanaEventType type,
                   const json& content = json(), int duration_ms = 0);

    // Time utilities
    static int64_t now_ms();
};

}  // namespace chitta
