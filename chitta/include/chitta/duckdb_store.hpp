#pragma once
// DuckDBStore: Durable storage with vector search and graph queries
//
// Uses DuckDB embedded database with:
// - VSS extension for HNSW vector search
// - DuckPGQ for graph queries (SQL/PGQ)
// - WAL for crash recovery

#include "types.hpp"
#include <duckdb.hpp>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <queue>
#include <condition_variable>

namespace chitta {

// Connection pool for concurrent reads
// DuckDB Database is thread-safe, but Connection is not
// Multiple connections can execute concurrent reads via MVCC
class ConnectionPool {
public:
    ConnectionPool(duckdb::DuckDB& db, size_t max_size = 8)
        : db_(db), max_size_(max_size), created_(0), emergency_count_(0) {}

    // RAII wrapper for borrowed connection
    class ScopedConnection {
    public:
        ScopedConnection(ConnectionPool& pool, std::unique_ptr<duckdb::Connection> conn)
            : pool_(pool), conn_(std::move(conn)) {}

        ~ScopedConnection() {
            if (conn_) pool_.release(std::move(conn_));
        }

        // Non-copyable, movable
        ScopedConnection(const ScopedConnection&) = delete;
        ScopedConnection& operator=(const ScopedConnection&) = delete;
        ScopedConnection(ScopedConnection&& other) noexcept
            : pool_(other.pool_), conn_(std::move(other.conn_)) {}
        ScopedConnection& operator=(ScopedConnection&& other) noexcept {
            if (this != &other) {
                if (conn_) pool_.release(std::move(conn_));
                conn_ = std::move(other.conn_);
            }
            return *this;
        }

        duckdb::Connection& operator*() { return *conn_; }
        duckdb::Connection* operator->() { return conn_.get(); }

    private:
        ConnectionPool& pool_;
        std::unique_ptr<duckdb::Connection> conn_;
    };

    // Acquire a connection (creates new if pool empty and under limit)
    // Returns connection or throws if timeout (prevents infinite blocking)
    ScopedConnection acquire(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::unique_lock lock(mutex_);

        if (!pool_.empty()) {
            auto conn = std::move(pool_.front());
            pool_.pop();
            return ScopedConnection(*this, std::move(conn));
        }

        if (created_ < max_size_) {
            created_++;
            lock.unlock();
            return ScopedConnection(*this, std::make_unique<duckdb::Connection>(db_));
        }

        // Wait for a connection with timeout (prevents indefinite blocking)
        if (!cv_.wait_for(lock, timeout, [this] { return !pool_.empty(); })) {
            // Timeout - create emergency connection to prevent deadlock
            // This temporarily exceeds max_size but prevents complete stall.
            // Emergency connections are destroyed on release rather than returned
            // to the pool, so the pool shrinks back to max_size_ over time.
            created_++;
            emergency_count_++;
            lock.unlock();
            return ScopedConnection(*this, std::make_unique<duckdb::Connection>(db_));
        }
        auto conn = std::move(pool_.front());
        pool_.pop();
        return ScopedConnection(*this, std::move(conn));
    }

private:
    void release(std::unique_ptr<duckdb::Connection> conn) {
        std::lock_guard lock(mutex_);
        if (created_ > max_size_ && emergency_count_ > 0) {
            // Over capacity due to emergency connections — destroy instead of returning
            emergency_count_--;
            created_--;
            // conn is destroyed when it goes out of scope
        } else {
            pool_.push(std::move(conn));
            cv_.notify_one();
        }
    }

    duckdb::DuckDB& db_;
    size_t max_size_;
    size_t created_;
    size_t emergency_count_;  // Track emergency connections created beyond max_size_
    std::queue<std::unique_ptr<duckdb::Connection>> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// Realm visibility levels
enum class RealmVisibility : uint8_t {
    Private = 0,   // Only visible in primary realm
    Shared = 1,    // Visible in primary + shared realms
    Global = 2,    // Visible everywhere (brahman)
};

// Priority tiers for budget-aware recall (ClawVault-inspired)
// Maps to: 0=🟢 background, 1=🟡 notable, 2=🔴 critical
enum class PriorityTier : uint8_t {
    Background = 0,  // Low-signal, loaded last
    Notable = 1,     // Medium importance, loaded if budget allows
    Critical = 2,    // Always loaded first
};

// Memory result from vector search
struct MemoryResult {
    int64_t id;
    std::string kind;
    std::string content;
    float confidence;
    float similarity;
    Timestamp created_at;
    Timestamp accessed_at;
    std::string realm;                      // Primary realm
    RealmVisibility visibility = RealmVisibility::Private;
    std::vector<std::string> shared_realms; // For Shared visibility
    PriorityTier priority_tier = PriorityTier::Background;

    // Provenance (optional, populated when enable_provenance=true)
    std::optional<std::string> source_session;
    std::optional<std::string> source_tool;
    std::optional<float> trust_score;
    std::optional<int64_t> derived_from;

    // Conflict info (optional, populated by truth maintenance)
    bool has_conflict = false;
    std::vector<int64_t> conflicting_ids;
};

// String-based triplet for DuckDB (different from NodeId-based Triplet in types.hpp)
struct StringTriplet {
    std::string subject;
    std::string predicate;
    std::string object;
    float weight;
};

// Code symbol for code intelligence
struct Symbol {
    int64_t id = 0;
    std::string kind;       // function, class, file, module
    std::string name;
    std::string signature;
    std::string file_path;
    int32_t line_start = 0;
    int32_t line_end = 0;
    int64_t repo_id = 0;
};

// Health metrics for the store
struct StoreHealth {
    size_t total_memories = 0;
    size_t total_symbols = 0;
    size_t total_triplets = 0;
    float avg_confidence = 0.0f;
    bool is_open = false;
    int64_t cached_at = 0;  // Timestamp when cached (0 = never)
};

// Ledger entry for session continuity
struct LedgerEntry {
    int64_t id = 0;
    std::string session_id;
    std::string project;
    int64_t created_at = 0;
    std::string transcript_path;  // Path to JSONL transcript for full context recovery

    // Soul state
    std::string mood;
    float coherence = 0.0f;
    float confidence = 0.0f;

    // Work state (JSON strings)
    std::string todos;           // JSON array of {content, status}
    std::string active_files;    // JSON array of file paths
    std::string decisions;       // JSON array of key decisions

    // Continuation (JSON strings)
    std::string next_steps;      // JSON array of next steps
    std::string blockers;        // JSON array of blockers
    std::string discoveries;     // JSON array of discoveries

    // Full snapshot for reconstruction
    std::string snapshot;
};

// Code file metadata for incremental indexing
struct CodeFile {
    std::string path;           // Full file path (primary key)
    std::string project;        // Project identifier
    int64_t mtime = 0;          // Last modification time
    int64_t indexed_at = 0;     // When we last indexed
    int32_t symbols_count = 0;  // Number of symbols extracted
    int32_t callsites_count = 0;// Number of callsites extracted
    std::string file_hash;      // Optional content hash
};

// File edit tracking for File Time Machine
// Indexes file-history-snapshot entries from Claude Code transcripts
struct FileEdit {
    int64_t id = 0;
    std::string session_id;
    std::string file_path;      // Relative path from cwd
    int32_t version = 0;        // Version number (1, 2, 3, ...)
    std::string backup_filename; // e.g., "9bd8e2ddbec15547@v2"
    int64_t backup_time = 0;    // Unix timestamp ms
    std::string realm;
};

// Transcript state for distillation (reads JSONL directly)
struct TranscriptState {
    std::string session_id;         // Claude session ID (primary key)
    std::string transcript_path;    // Path to .jsonl file
    std::string realm;              // Project/realm isolation
    int64_t last_processed_line = 0;// Last line number processed
    int64_t last_distilled_at = 0;  // When we last distilled
    int64_t created_at = 0;
};

// Long-running task for mind-powered persistence (elegant Ralph Wiggum)
struct LongTask {
    int64_t id = 0;
    std::string task_id;            // User-provided identifier
    std::string goal;               // What we're trying to achieve
    std::string realm;              // Project scope
    std::string status;             // active, paused, completed, blocked, abandoned

    // Definition of Done (JSON)
    std::string hard_checks;        // Deterministic: ["tests pass", "build succeeds"]
    std::string soft_checks;        // Semantic: ["docs accurate", "edge cases handled"]

    // Work tracking (JSON arrays)
    std::string work_items;         // Subtasks with status
    std::string completed_summary;  // What's been achieved (synthesized)
    std::string blockers;           // Current blockers

    // Multi-agent support
    std::string agent_id;           // Current agent (for leases)
    int64_t lease_until = 0;        // Lease expiry timestamp

    // Metrics
    int32_t iterations = 0;         // How many times resumed
    int64_t started_at = 0;
    int64_t updated_at = 0;
    int64_t completed_at = 0;

    // Outcome
    std::string outcome;            // Final result description
};

// Append-only event log for task execution
struct TaskEvent {
    int64_t id = 0;
    std::string task_id;            // Links to LongTask
    std::string kind;               // tool_result, decision, observation, error, checkpoint
    std::string payload;            // JSON: command, output, etc.
    std::string tags;               // JSON array for filtering
    std::string related_entities;   // JSON array: files, functions mentioned
    int64_t created_at = 0;
};

// Suggestion tracking for loop closure (did it help?)
struct Suggestion {
    int64_t id = 0;
    std::string content;            // What was suggested
    std::string context;            // Why/when it was suggested
    std::string realm;              // Project scope
    std::string status;             // pending, resolved
    bool helped = false;            // Did it work?
    std::string outcome_details;    // What happened
    int64_t memory_id = 0;          // Link to outcome memory (if resolved)
    int64_t suggested_at = 0;
    int64_t resolved_at = 0;
};

// Parsed conversation turn from JSONL
struct TranscriptTurn {
    std::string role;       // "user" or "assistant"
    std::string content;    // Message content
    int64_t line_number;    // Line in JSONL file
};

// Anticipation pattern: context→action predictions
struct AnticipationPattern {
    int64_t id = 0;
    std::string context;           // Context trigger (what situation)
    std::string action;            // Action taken (what was done)
    int32_t frequency = 1;         // How many times observed
    int32_t success_count = 0;     // How often it worked
    int64_t last_triggered = 0;
    std::string realm;
    int64_t created_at = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Context Repository (Letta-inspired): Version Control & Coordination
// ═══════════════════════════════════════════════════════════════════════════

// Memory version history entry (git-like tracking)
struct MemoryHistoryEntry {
    int64_t id = 0;
    int64_t memory_id = 0;
    int32_t version = 0;
    std::string operation;          // create, update, pin, unpin, delete
    std::string content_before;
    std::string content_after;
    float confidence_before = 0.0f;
    float confidence_after = 0.0f;
    std::string commit_message;
    std::string session_id;
    std::string tool_name;
    std::string realm;
    int64_t created_at = 0;
};

// Memory lock for concurrent sadhana coordination
struct MemoryLock {
    int64_t memory_id = 0;
    std::string holder_id;          // session/sadhana ID holding the lock
    std::string holder_type;        // "session", "sadhana", "skill"
    std::string lock_type;          // "exclusive", "shared"
    int64_t acquired_at = 0;
    int64_t expires_at = 0;
};

// Memory merge queue entry for conflict resolution
struct MemoryMergeEntry {
    int64_t id = 0;
    int64_t memory_id = 0;
    std::string proposed_content;
    std::string proposed_by;
    int32_t base_version = 0;
    std::string status;             // "pending", "applied", "rejected", "conflict"
    int64_t conflict_with = 0;
    std::string resolution;
    int64_t created_at = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Narrative Event Stream: Session Event Logging
// ═══════════════════════════════════════════════════════════════════════════

// Session event kinds (append-only log)
enum class SessionEventKind : uint8_t {
    UserMessage = 0,
    AssistantMessage = 1,
    ToolUse = 2,
    ToolResult = 3,
    Error = 4,
    FileEdit = 5,
    Search = 6,
    Build = 7,
    Test = 8,
    Commit = 9,
    ModeChange = 10,
};

// Convert SessionEventKind to string
inline std::string session_event_kind_to_string(SessionEventKind kind) {
    switch (kind) {
        case SessionEventKind::UserMessage: return "user_message";
        case SessionEventKind::AssistantMessage: return "assistant_message";
        case SessionEventKind::ToolUse: return "tool_use";
        case SessionEventKind::ToolResult: return "tool_result";
        case SessionEventKind::Error: return "error";
        case SessionEventKind::FileEdit: return "file_edit";
        case SessionEventKind::Search: return "search";
        case SessionEventKind::Build: return "build";
        case SessionEventKind::Test: return "test";
        case SessionEventKind::Commit: return "commit";
        case SessionEventKind::ModeChange: return "mode_change";
        default: return "unknown";
    }
}

// Convert string to SessionEventKind
inline SessionEventKind string_to_session_event_kind(const std::string& s) {
    if (s == "user_message") return SessionEventKind::UserMessage;
    if (s == "assistant_message") return SessionEventKind::AssistantMessage;
    if (s == "tool_use") return SessionEventKind::ToolUse;
    if (s == "tool_result") return SessionEventKind::ToolResult;
    if (s == "error") return SessionEventKind::Error;
    if (s == "file_edit") return SessionEventKind::FileEdit;
    if (s == "search") return SessionEventKind::Search;
    if (s == "build") return SessionEventKind::Build;
    if (s == "test") return SessionEventKind::Test;
    if (s == "commit") return SessionEventKind::Commit;
    if (s == "mode_change") return SessionEventKind::ModeChange;
    return SessionEventKind::UserMessage;  // Default
}

// Session event: individual action in the event stream
struct SessionEvent {
    int64_t id = 0;
    std::string session_id;
    SessionEventKind kind = SessionEventKind::UserMessage;
    std::string summary;           // Brief description of the event
    std::string payload;           // JSON: tool args, message content, etc.
    std::string tool_name;         // For ToolUse/ToolResult events
    bool success = true;           // For ToolResult events
    std::string files_mentioned;   // JSON array of file paths
    std::string realm;
    int64_t created_at = 0;
};

// Work mode for state segments
enum class WorkMode : uint8_t {
    Orienting = 0,      // Understanding the task/codebase
    Exploring = 1,      // Searching, reading, investigating
    Implementing = 2,   // Writing code, editing files
    Debugging = 3,      // Fixing issues, analyzing errors
    Validating = 4,     // Running tests, checking builds
    Blocked = 5,        // Waiting on user input or stuck
    Flow = 6,           // Deep productive work
};

// Convert WorkMode to string
inline std::string work_mode_to_string(WorkMode mode) {
    switch (mode) {
        case WorkMode::Orienting: return "orienting";
        case WorkMode::Exploring: return "exploring";
        case WorkMode::Implementing: return "implementing";
        case WorkMode::Debugging: return "debugging";
        case WorkMode::Validating: return "validating";
        case WorkMode::Blocked: return "blocked";
        case WorkMode::Flow: return "flow";
        default: return "unknown";
    }
}

// Convert string to WorkMode
inline WorkMode string_to_work_mode(const std::string& s) {
    if (s == "orienting") return WorkMode::Orienting;
    if (s == "exploring") return WorkMode::Exploring;
    if (s == "implementing") return WorkMode::Implementing;
    if (s == "debugging") return WorkMode::Debugging;
    if (s == "validating") return WorkMode::Validating;
    if (s == "blocked") return WorkMode::Blocked;
    if (s == "flow") return WorkMode::Flow;
    return WorkMode::Orienting;  // Default
}

// State segment: contiguous period of a work mode
struct StateSegment {
    int64_t id = 0;
    std::string session_id;
    WorkMode mode = WorkMode::Orienting;
    float confidence = 0.5f;       // How confident is the mode inference
    std::string evidence;          // JSON: what led to this inference
    int64_t started_at = 0;
    int64_t ended_at = 0;          // 0 = still open
    int32_t event_count = 0;       // Events in this segment
    std::string tools_used;        // JSON array of tool names
    std::string files_active;      // JSON array of file paths
    std::string realm;
    std::string status;            // "open" or "closed"
};

// ═══════════════════════════════════════════════════════════════════════════
// Anticipation Candidates and Annoyance Gate
// ═══════════════════════════════════════════════════════════════════════════

// Source of anticipation prediction
enum class AnticipationSource : uint8_t {
    Rule = 0,           // Pattern-matching rule (e.g., "after file edit, often build")
    Sequence = 1,       // Sequential pattern from event history
    EpisodeMatch = 2,   // Similar past episode recalled
};

// Convert AnticipationSource to string
inline std::string anticipation_source_to_string(AnticipationSource source) {
    switch (source) {
        case AnticipationSource::Rule: return "rule";
        case AnticipationSource::Sequence: return "sequence";
        case AnticipationSource::EpisodeMatch: return "episode_match";
        default: return "unknown";
    }
}

// Convert string to AnticipationSource
inline AnticipationSource string_to_anticipation_source(const std::string& s) {
    if (s == "rule") return AnticipationSource::Rule;
    if (s == "sequence") return AnticipationSource::Sequence;
    if (s == "episode_match") return AnticipationSource::EpisodeMatch;
    return AnticipationSource::Rule;  // Default
}

// Anticipation candidate: a prediction waiting to be surfaced or resolved
struct AnticipationCandidate {
    int64_t id = 0;
    std::string session_id;
    std::string prediction;         // What we predict will happen/be needed
    AnticipationSource source = AnticipationSource::Rule;
    float confidence = 0.5f;        // 0-1 confidence in the prediction
    std::string evidence;           // JSON: what led to this prediction
    std::string current_mode;       // Work mode when prediction was made
    bool surfaced = false;          // Has this been shown to the user?
    std::string outcome;            // "correct", "incorrect", "expired", "" (pending)
    int64_t created_at = 0;
    int64_t surfaced_at = 0;        // When it was shown (0 if not surfaced)
    int64_t resolved_at = 0;        // When outcome was determined (0 if pending)
};

// Annoyance gate state: controls how often predictions are surfaced
struct AnnoyanceGateState {
    std::string session_id;         // Session this gate applies to
    int32_t predictions_surfaced = 0;   // Total predictions shown
    int32_t predictions_correct = 0;    // How many were right
    int32_t predictions_incorrect = 0;  // How many were wrong
    int64_t last_surfaced_at = 0;       // When we last showed a prediction
    int32_t budget_remaining = 5;       // How many more we can show this session
    int64_t cooldown_ms = 120000;       // Min time between predictions (default 2 min)
    float confidence_floor = 0.7f;      // Min confidence to surface (precision-first)
    int64_t created_at = 0;
};

// Habit: repeated pattern that strengthens with use
struct Habit {
    int64_t id = 0;
    std::string trigger_pattern;   // What triggers the habit
    std::string response;          // What to do when triggered
    float strength = 0.1f;         // 0-1, increases with use
    int32_t frequency = 1;         // Times activated
    int64_t last_activated = 0;
    std::string realm;
    int64_t created_at = 0;
};

// Background task: daemon-level processing
struct BackgroundTask {
    int64_t id = 0;
    std::string task_type;         // consolidation, decay, pruning, pattern_extraction
    std::string status;            // pending, running, completed, failed
    int64_t scheduled_at = 0;
    int64_t started_at = 0;
    int64_t completed_at = 0;
    std::string result;            // JSON result
    std::string error;             // Error if failed
    std::string realm;
};

// User profile: structured understanding of the human partner
struct UserProfile {
    std::string user_id = "default";
    std::string expertise_json;      // JSON: [{"domain":"python","level":0.9}, ...]
    std::string style_json;          // JSON: {"tone":"direct","verbosity":"concise","formality":"casual"}
    std::string patterns_json;       // JSON: {"active_hours":"9-17","avg_session_mins":45}
    std::string preferences_json;    // JSON: {"no_emojis":true,"prefer_examples":true}
    int64_t updated_at = 0;
};

// Long-term goal: objectives spanning weeks/months
struct Goal {
    int64_t id = 0;
    std::string title;               // Short goal name
    std::string description;         // Detailed description
    std::string milestones_json;     // JSON: [{"name":"v1","done":false}, ...]
    std::string status;              // active, paused, completed, abandoned
    float progress = 0.0f;           // 0-1 completion
    int64_t deadline = 0;            // Optional deadline (unix timestamp)
    std::string outcome;             // Final outcome when completed
    std::string realm;
    int64_t created_at = 0;
    int64_t updated_at = 0;
};

// Calibration: tracking prediction accuracy by domain
struct CalibrationScore {
    std::string domain;              // Domain (e.g., "code", "architecture", "debugging")
    int32_t predictions = 0;         // Total predictions
    int32_t successes = 0;           // Correct predictions
    int32_t failures = 0;            // Incorrect predictions
    float accuracy = 0.0f;           // successes / predictions
    float confidence_adjustment = 0.0f;  // Suggested adjustment (-0.2 to +0.2)
    int64_t updated_at = 0;
};

// Usage outcome: tracks whether surfaced memories actually helped
struct UsageOutcome {
    int64_t id = 0;
    int64_t memory_id = 0;           // Memory that was surfaced
    std::string session_id;          // Session where used
    std::string outcome;             // positive, negative, neutral
    std::string context;             // What task triggered recall
    int64_t created_at = 0;
};

// Distillation candidate: cluster of similar episodes for wisdom extraction
struct DistillCandidate {
    std::vector<int64_t> episode_ids;  // Source episodes
    std::string pattern_content;        // Common pattern
    float avg_confidence = 0.0f;
    float avg_similarity = 0.0f;        // Average pairwise similarity
};

// Memory hygiene: stats about memory health
struct HygieneStats {
    size_t total_memories = 0;
    size_t low_confidence = 0;       // Below 0.3
    size_t medium_confidence = 0;    // 0.3-0.7
    size_t high_confidence = 0;      // Above 0.7
    size_t old_unaccessed = 0;       // Not accessed in 30+ days
    size_t consolidation_candidates = 0;  // Similar memories
    float avg_confidence = 0.0f;
    float growth_rate_per_day = 0.0f;  // Recent memory growth
};

// ═══════════════════════════════════════════════════════════════════════════
// xMemory-Inspired Theme System
// ═══════════════════════════════════════════════════════════════════════════

// Theme: semantic grouping of memories (xMemory layer)
struct Theme {
    int64_t id = 0;
    std::string name;
    std::vector<float> centroid;  // 384-dim average embedding
    float coherence = 1.0f;       // How tightly grouped (lower = consider split)
    float sparsity = 0.5f;        // Current sparsity score
    int32_t memory_count = 0;
    int64_t representative_id = 0; // Most central memory
    std::string realm;
    int64_t created_at = 0;
    int64_t updated_at = 0;
};

// Theme membership: many-to-many between memories and themes
struct ThemeMembership {
    int64_t memory_id = 0;
    int64_t theme_id = 0;
    float strength = 1.0f;        // Membership strength (0-1)
    bool is_representative = false;
    int64_t assigned_at = 0;
};

// Theme configuration (xMemory tuning)
struct ThemeConfig {
    float semantic_weight = 0.6f;     // Weight for embedding similarity
    float sparsity_weight = 0.4f;     // Weight for balanced distribution
    float assignment_threshold = 0.5f; // Minimum score to assign to theme
    size_t min_theme_size = 3;        // Minimum memories for a theme
    size_t max_theme_size = 100;      // Maximum before split considered
    size_t ideal_theme_size = 20;     // Target size for sparsity scoring
    float coherence_split_threshold = 0.6f;  // Below this, consider split
    float coherence_merge_threshold = 0.9f;  // Above this, consider merge
    float expansion_threshold = 0.7f;        // Min relevance for expansion
    size_t max_representatives = 3;          // Representatives per theme
    float reassignment_improvement = 0.1f;   // Score improvement to trigger move
};

// Theme retrieval result (for two-stage recall)
struct ThemeResult {
    int64_t theme_id = 0;
    std::string theme_name;
    float theme_relevance = 0.0f;  // How relevant is this theme to query
    std::vector<MemoryResult> representatives;
    std::vector<MemoryResult> expanded;  // Additional members from expansion
};

// Theme statistics
struct ThemeStats {
    size_t total_themes = 0;
    size_t total_memberships = 0;
    size_t orphan_memories = 0;      // Memories not in any theme
    float avg_theme_size = 0.0f;
    float avg_coherence = 0.0f;
    float size_variance = 0.0f;       // How balanced are theme sizes
    size_t undersized_themes = 0;     // Below min_theme_size
    size_t oversized_themes = 0;      // Above max_theme_size
};

// ═══════════════════════════════════════════════════════════════════════════
// Cross-Session Messaging
// ═══════════════════════════════════════════════════════════════════════════

// Cross-session message
struct SessionMessage {
    int64_t id = 0;
    std::string sender_session;
    std::string sender_realm;
    std::string target_type;       // "direct", "realm", "global"
    std::string target_id;         // session_id, realm name, or "*"
    int32_t priority = 1;          // 0=info, 1=normal, 2=important, 3=urgent
    std::string content;
    std::string content_type;      // "text", "json", "ssl"
    int64_t expires_at = 0;
    int64_t created_at = 0;
};

// Session registry entry
struct SessionRegistryEntry {
    std::string session_id;
    std::string realm;
    int32_t pid = 0;
    std::string transcript_path;   // Path to transcript .jsonl file (stable ID)
    std::string project_dir;       // Working directory
    int64_t started_at = 0;
    int64_t last_heartbeat = 0;
    std::string status;            // "active", "idle", "dead"
    std::string metadata;          // JSON
};

// Inbox item: message + delivery info
struct InboxItem {
    SessionMessage message;
    int64_t delivered_at = 0;
    bool is_read = false;
};

// DuckDBStore: unified storage using DuckDB embedded database
class DuckDBStore {
public:
    DuckDBStore() = default;
    ~DuckDBStore();

    // Lifecycle
    bool open(const std::string& path);
    void close();
    bool is_open() const { return db_ != nullptr; }

    // Memory operations
    int64_t remember(
        const std::string& content,
        const std::string& kind,
        const std::vector<float>& embedding,
        float confidence = 0.8f,
        float decay_rate = 0.05f,
        const std::string& realm = "brahman",
        RealmVisibility visibility = RealmVisibility::Private,
        const std::vector<std::string>& shared_realms = {}
    );

    std::vector<MemoryResult> recall(
        const std::vector<float>& query_embedding,
        size_t k = 10,
        const std::string& realm = "",      // Empty = all realms
        bool include_global = true,         // Include brahman/global memories
        const std::vector<std::string>& exclude_kinds = {}  // Filter out these kinds
    );

    // Tag-filtered recall: search only within memories that have the specified tag
    std::vector<MemoryResult> recall_with_tag(
        const std::vector<float>& query_embedding,
        const std::string& tag,
        size_t k = 10
    );

    // Time-bounded recall: filter memories by creation time window
    // Optionally combines with semantic search if query_embedding provided
    std::vector<MemoryResult> recall_temporal(
        const std::vector<float>& query_embedding,  // Empty = no semantic filtering
        std::optional<int64_t> start_time,          // Unix ms, optional start bound
        std::optional<int64_t> end_time,            // Unix ms, optional end bound
        size_t limit = 20,
        const std::string& realm = "",
        bool include_global = true
    );

    // Realm management
    bool set_realm(int64_t id, const std::string& realm);
    bool set_visibility(int64_t id, RealmVisibility visibility);
    bool add_to_realm(int64_t id, const std::string& realm);      // Multi-realm membership
    bool remove_from_realm(int64_t id, const std::string& realm);
    std::vector<std::string> get_realms(int64_t id);              // Get all realms for a memory
    std::vector<std::string> list_realms();                        // List all known realms

    bool strengthen(int64_t id, float amount = 0.1f);
    bool weaken(int64_t id, float amount = 0.1f);
    bool forget(int64_t id);
    bool touch(int64_t id);  // Update accessed_at

    // Get memory by ID
    std::optional<MemoryResult> get_memory(int64_t id);

    // Update memory content
    bool update_content(int64_t id, const std::string& new_content);

    // Update memory kind/type (for taxonomy enforcement)
    bool update_kind(int64_t id, const std::string& new_kind);

    // Update memory visibility (for cross-project promotion)
    bool update_visibility(int64_t id, RealmVisibility visibility);

    // ═══════════════════════════════════════════════════════════════════════
    // Context Repository: Version Control (Letta-inspired)
    // ═══════════════════════════════════════════════════════════════════════

    // Record a history entry for memory changes (git-like audit trail)
    bool record_history(int64_t memory_id, const std::string& operation,
                       const std::string& content_before, const std::string& content_after,
                       float conf_before, float conf_after,
                       const std::string& commit_msg = "",
                       const std::string& session_id = "",
                       const std::string& tool_name = "");

    // Get version history for a memory
    std::vector<MemoryHistoryEntry> get_history(int64_t memory_id, int32_t limit = 20);

    // Get a specific version of a memory
    std::optional<MemoryHistoryEntry> get_version(int64_t memory_id, int32_t version);

    // Revert memory to a previous version (creates new history entry)
    bool revert_to_version(int64_t memory_id, int32_t version, const std::string& reason = "");

    // Get current version number for a memory
    int32_t get_current_version(int64_t memory_id);

    // ═══════════════════════════════════════════════════════════════════════
    // Context Repository: Agent-Managed Pinning
    // ═══════════════════════════════════════════════════════════════════════

    // Pin a memory to keep it "hot" (agent decides importance)
    bool pin_memory(int64_t memory_id, const std::string& reason = "");

    // Unpin a memory
    bool unpin_memory(int64_t memory_id);

    // List all pinned memories
    std::vector<MemoryResult> list_pinned(const std::string& realm = "", size_t limit = 50);

    // Check if a memory is pinned
    bool is_pinned(int64_t memory_id);

    // ═══════════════════════════════════════════════════════════════════════
    // Context Repository: Concurrent Coordination (Locking)
    // ═══════════════════════════════════════════════════════════════════════

    // Acquire a lock on a memory (for concurrent sadhana coordination)
    bool acquire_lock(int64_t memory_id, const std::string& holder_id,
                     const std::string& holder_type = "session",
                     const std::string& lock_type = "exclusive",
                     int64_t duration_seconds = 300);  // 5 min default

    // Release a lock
    bool release_lock(int64_t memory_id, const std::string& holder_id);

    // Check if a memory is locked
    std::optional<MemoryLock> get_lock(int64_t memory_id);

    // Clean up expired locks
    size_t cleanup_expired_locks();

    // ═══════════════════════════════════════════════════════════════════════
    // Context Repository: Merge Queue (Conflict Resolution)
    // ═══════════════════════════════════════════════════════════════════════

    // Propose a change to a locked memory (queues for later merge)
    int64_t propose_change(int64_t memory_id, const std::string& proposed_content,
                          const std::string& proposed_by);

    // List pending merge proposals
    std::vector<MemoryMergeEntry> list_merge_queue(const std::string& status = "pending",
                                                   size_t limit = 50);

    // Resolve a merge proposal (apply, reject, or mark as conflict)
    bool resolve_merge(int64_t merge_id, const std::string& resolution,
                      const std::string& new_status = "applied");

    // Update memory embedding (for re-embedding with better vectors)
    bool set_memory_embedding(int64_t id, const std::vector<float>& embedding);

    // List global memories (cross-project insights)
    std::vector<MemoryResult> list_global_memories(size_t limit = 20, const std::string& kind = "");

    // List memories by semantic aspect (filters by node kind)
    // Aspects: preferences, corrections, insights, failures, decisions, approaches,
    //          milestones, goals, habits, beliefs, wisdom, code, gaps
    std::vector<MemoryResult> list_by_aspect(
        const std::string& aspect,
        size_t limit = 50,
        float min_confidence = 0.1f
    );

    // ═══════════════════════════════════════════════════════════════════════
    // Memory Index: Fast pre-retrieval scanning (ClawVault-inspired)
    // ═══════════════════════════════════════════════════════════════════════

    // Brief index entry for fast scanning (vault index pattern)
    struct MemoryIndexEntry {
        int64_t id;
        std::string kind;
        PriorityTier priority_tier;
        int64_t created_at;
        std::string one_liner;  // First 80 chars of content
    };

    // List memories as brief index entries (fast path before expensive retrieval)
    // Use to scan what exists, then fetch full content for relevant entries
    std::vector<MemoryIndexEntry> list_memories_brief(
        size_t limit = 500,
        const std::string& realm = "",
        const std::string& kind = "",              // Filter by kind
        std::optional<PriorityTier> tier = {}      // Filter by priority tier
    );

    // ═══════════════════════════════════════════════════════════════════════
    // Priority Tiers: Budget-aware recall (ClawVault-inspired)
    // ═══════════════════════════════════════════════════════════════════════

    // Set priority tier for a memory
    bool set_priority_tier(int64_t memory_id, PriorityTier tier);

    // Budget-aware recall: fills critical first, then notable, then background
    // Estimates ~4 chars per token for budget calculation
    std::vector<MemoryResult> recall_by_priority(
        const std::vector<float>& query_embedding,
        size_t budget_tokens = 4000,           // Token budget
        const std::string& realm = "",
        bool include_global = true
    );

    // Tag management
    bool add_tag(int64_t id, const std::string& tag);
    bool remove_tag(int64_t id, const std::string& tag);
    std::vector<std::string> get_tags(int64_t id);

    // Apply decay to all memories (returns count updated)
    size_t apply_decay();

    // Prune weak memories (returns count removed)
    size_t prune(float threshold = 0.1f, float min_age_days = 7.0f);

    // Graph operations (triplets with string entities)
    bool connect(
        const std::string& subject,
        const std::string& predicate,
        const std::string& object,
        float weight = 1.0f
    );

    // Connect with source file tracking (for fast file-based deletion)
    bool connect_with_source(
        const std::string& subject,
        const std::string& predicate,
        const std::string& object,
        const std::string& source_file,
        float weight = 1.0f
    );

    // Batch connect: (subject, predicate, object, source_file) tuples
    // Uses DuckDB Appender for maximum speed (bypasses SQL parser)
    size_t connect_batch(
        const std::vector<std::tuple<std::string, std::string, std::string, std::string>>& triplets,
        float weight = 1.0f
    );

    // Fallback SQL-based batch insert (if Appender fails)
    size_t connect_batch_sql(
        const std::vector<std::tuple<std::string, std::string, std::string, std::string>>& triplets,
        float weight = 1.0f
    );

    std::vector<StringTriplet> query_subject(const std::string& subject);
    std::vector<StringTriplet> query_object(const std::string& object);
    std::vector<StringTriplet> query_predicate(const std::string& predicate);

    // ═══════════════════════════════════════════════════════════════════════
    // Temporal Fact Versioning (Zep-inspired)
    // ═══════════════════════════════════════════════════════════════════════

    // Temporal triplet with validity period
    struct TemporalTriplet {
        int64_t id = 0;
        std::string subject;
        std::string predicate;
        std::string object;
        float weight = 1.0f;
        int64_t valid_from_ms = 0;      // When fact became true
        int64_t valid_to_ms = 0;        // When fact stopped being true (0 = still valid)
        int64_t superseded_by = 0;      // ID of replacing triplet
        int64_t context_date_ms = 0;    // Session date used for relative date resolution
        int64_t created_at = 0;
    };

    // Connect with temporal validity
    // valid_from_ms: when the fact became true (0 = use context_date_ms or now)
    // valid_to_ms: when the fact stopped being true (0 = still valid)
    // context_date_ms: session date for relative date resolution
    bool connect_temporal(
        const std::string& subject,
        const std::string& predicate,
        const std::string& object,
        float weight,
        int64_t valid_from_ms,
        int64_t valid_to_ms = 0,
        int64_t context_date_ms = 0
    );

    // Supersede an old triplet with a new one
    // Sets old.valid_to_ms = now and old.superseded_by = new_id
    bool supersede_triplet(int64_t old_triplet_id, int64_t new_triplet_id);

    // Query triplets at a specific point in time
    // Returns triplets where: valid_from_ms <= at_time AND (valid_to_ms = 0 OR valid_to_ms > at_time)
    std::vector<TemporalTriplet> query_triplets_temporal(
        const std::string& subject = "",
        const std::string& predicate = "",
        const std::string& object = "",
        int64_t at_time_ms = 0,  // 0 = current time
        size_t limit = 50
    );

    // Get the history of a subject-predicate relationship
    // Returns all versions ordered by valid_from_ms DESC
    std::vector<TemporalTriplet> query_triplet_history(
        const std::string& subject,
        const std::string& predicate,
        size_t limit = 20
    );

    // Code intelligence
    int64_t add_symbol(const Symbol& sym, const std::vector<float>& embedding = {});
    std::vector<Symbol> find_symbol(const std::string& name, const std::string& kind = "");
    std::optional<Symbol> find_symbol_at_line(const std::string& file_path, int line);
    std::optional<Symbol> get_symbol_by_id(int64_t symbol_id);
    std::vector<int64_t> callers(int64_t symbol_id);
    std::vector<int64_t> callees(int64_t symbol_id);
    bool add_call(int64_t caller_id, int64_t callee_id);

    // Health and stats
    StoreHealth health();           // Full health check (runs queries, may block)
    StoreHealth cached_health();    // Non-blocking: returns cached stats
    void update_health_cache();     // Update the cache (call periodically)

    // Vector index management (deferred rebuild for stability)
    bool needs_reindex() const { return needs_reindex_.load(); }
    void mark_needs_reindex() { needs_reindex_.store(true); }
    bool rebuild_vector_index();    // DROP + CREATE index (call during maintenance)
    bool rebuild_fts_index();       // Rebuild FTS index on memory content
    bool has_vector_index() const { return index_exists_.load(); }
    size_t migrate_embeddings_to_vss();  // Copy embeddings from main DB to VSS DB

    size_t memory_count();
    size_t triplet_count();
    size_t symbol_count();

    // Efficient attractor finding (excludes code intel triplets)
    std::vector<std::pair<std::string, size_t>> get_top_connected_entities(size_t limit = 20);

    // BM25 full-text search on symbols (no embeddings needed)
    std::vector<Symbol> bm25_search_symbols(const std::string& query, size_t limit = 10,
                                             const std::string& project = "");
    bool has_fts() const;

    // BM25 full-text search on memory content for hybrid recall
    // Returns (memory_id, bm25_score) pairs sorted by score descending
    std::vector<std::pair<int64_t, float>> bm25_search_memory(
        const std::string& query,
        size_t limit = 10,
        const std::string& realm = "",
        bool include_global = true,
        const std::vector<std::string>& exclude_kinds = {}) const;

    // Get memory IDs that have tags matching any of the given terms
    std::unordered_set<int64_t> tag_hits(const std::vector<std::string>& terms) const;

    // Ledger operations (session continuity)
    int64_t save_ledger(const LedgerEntry& entry);
    std::optional<LedgerEntry> load_ledger(const std::string& session_id = "", const std::string& project = "");
    std::vector<LedgerEntry> list_ledgers(const std::string& project = "", size_t limit = 10);
    std::optional<LedgerEntry> get_ledger(int64_t id);
    bool delete_ledger(int64_t id);

    // Code file tracking (incremental indexing)
    bool set_file_metadata(const CodeFile& file);
    std::optional<CodeFile> get_file_metadata(const std::string& path);
    std::vector<CodeFile> list_project_files(const std::string& project);
    bool delete_file_metadata(const std::string& path);
    bool delete_project_files(const std::string& project);  // Clear all files for project

    // Delete symbols/triplets for a file (before re-indexing)
    size_t delete_file_symbols(const std::string& file_path);
    size_t delete_file_triplets(const std::string& file_path);

    // ═══════════════════════════════════════════════════════════════════════
    // File Time Machine: Track file versions from Claude Code file-history
    // ═══════════════════════════════════════════════════════════════════════

    // Store a file edit record (from file-history-snapshot in transcript)
    int64_t store_file_edit(const FileEdit& edit);

    // Batch store file edits (efficient for indexing entire transcripts)
    size_t store_file_edits_batch(const std::vector<FileEdit>& edits);

    // Get file edits for a specific file path, ordered by time desc
    std::vector<FileEdit> get_file_edits(const std::string& file_path, size_t limit = 50);

    // Get file edits in a time range (for timeline queries)
    std::vector<FileEdit> get_file_edits_in_range(
        int64_t start_time,      // Unix timestamp ms
        int64_t end_time,        // Unix timestamp ms
        const std::string& file_pattern = "",  // Optional glob pattern
        size_t limit = 100
    );

    // Get file edits for a session
    std::vector<FileEdit> get_session_file_edits(const std::string& session_id, size_t limit = 100);

    // Get the file edit closest to a specific time
    std::optional<FileEdit> get_file_at_time(const std::string& file_path, int64_t timestamp);

    // Check if a session has been indexed for file edits
    bool session_file_edits_indexed(const std::string& session_id);

    // Mark a session as indexed (to avoid re-processing)
    bool mark_session_file_edits_indexed(const std::string& session_id);

    // Clear entire project codebase (symbols, triplets, file metadata)
    struct ClearProjectResult {
        size_t files_deleted = 0;
        size_t symbols_deleted = 0;
        size_t triplets_deleted = 0;
    };
    ClearProjectResult clear_project_codebase(const std::string& project);

    // Delete triplets by subject pattern (SQL LIKE)
    size_t count_triplets_by_pattern(const std::string& pattern);
    size_t delete_triplets_by_pattern(const std::string& pattern);

    // Semantic enrichment for code symbols
    struct UndescribedSymbol {
        int64_t id;
        std::string kind;
        std::string name;
        std::string signature;
        std::string file_path;
        int line_start;
        int line_end;
        int priority;  // Lower = more important (class=0, function=1, method=2)
    };
    std::vector<UndescribedSymbol> get_undescribed_symbols(size_t limit = 10);
    bool set_symbol_memory(int64_t symbol_id, int64_t memory_id);
    bool set_symbol_description(int64_t symbol_id, const std::string& description);
    size_t count_undescribed_symbols();
    size_t count_total_symbols();

    // Fast embedding: embed symbol metadata directly (no LLM needed)
    bool set_symbol_embedding(int64_t symbol_id, const std::vector<float>& embedding);
    std::vector<UndescribedSymbol> get_unembedded_symbols(size_t limit = 100);
    size_t count_unembedded_symbols();

    // Semantic symbol search: find symbols by embedding similarity
    struct SymbolMatch {
        Symbol symbol;
        float score;
    };
    std::vector<SymbolMatch> search_symbols_by_embedding(const std::vector<float>& query_embedding,
                                                          size_t limit = 10,
                                                          const std::string& kind_filter = "",
                                                          const std::string& project = "");

    // Purge zero-vector embeddings from separate embeddings DB, return purged IDs
    std::vector<int64_t> purge_zero_embeddings_ids();

    // Clean orphaned symbol embeddings (symbol_id no longer exists in symbol table)
    size_t clean_orphaned_symbol_embeddings();

    // Clear all symbol embeddings from separate embeddings DB (for full re-embed)
    size_t clear_symbol_embeddings();

    // Raw SQL execution (for maintenance operations)
    bool execute_raw(const std::string& sql);

    // Transcript state operations (for distillation)
    bool register_transcript(const std::string& session_id, const std::string& transcript_path,
                             const std::string& realm = "default");
    std::optional<TranscriptState> get_transcript(const std::string& session_id);
    std::vector<TranscriptState> get_pending_transcripts();  // Transcripts with new content
    bool update_transcript_progress(const std::string& session_id, int64_t last_line);
    bool mark_transcript_distilled(const std::string& session_id);
    bool remove_transcript(const std::string& session_id);
    size_t transcript_count();

    // Long-running tasks (mind-powered Ralph Wiggum)
    int64_t task_start(const LongTask& task);
    bool task_update(const std::string& task_id, const LongTask& updates);
    std::optional<LongTask> task_get(const std::string& task_id);
    std::optional<LongTask> task_get_active(const std::string& realm = "");
    std::vector<LongTask> task_list(const std::string& realm = "", const std::string& status = "");
    bool task_complete(const std::string& task_id, const std::string& outcome);
    bool task_abandon(const std::string& task_id, const std::string& reason);
    bool task_claim(const std::string& task_id, const std::string& agent_id, int64_t lease_seconds = 300);
    bool task_heartbeat(const std::string& task_id, const std::string& agent_id);

    // Task events (append-only log)
    int64_t event_append(const TaskEvent& event);
    std::vector<TaskEvent> event_list(const std::string& task_id, size_t limit = 100);
    std::vector<TaskEvent> event_get_recent(const std::string& task_id, const std::string& kind = "", size_t limit = 20);

    // Suggestion tracking (loop closure)
    int64_t suggestion_track(const Suggestion& suggestion);
    std::vector<Suggestion> suggestion_list_pending(const std::string& realm = "", size_t limit = 20);
    bool suggestion_resolve(int64_t id, bool helped, const std::string& details, int64_t memory_id = 0);
    std::optional<Suggestion> suggestion_get(int64_t id);
    size_t suggestion_count_pending(const std::string& realm = "");

    // Memory consolidation (merge similar memories)
    struct ConsolidationCandidate {
        int64_t primary_id;
        int64_t secondary_id;
        float similarity;
        std::string primary_content;
        std::string secondary_content;
    };

    std::vector<ConsolidationCandidate> consolidation_scan(
        float similarity_threshold = 0.85f,
        size_t limit = 50,
        const std::string& realm = ""
    );

    bool consolidation_merge(int64_t primary_id, int64_t secondary_id, const std::string& merged_content = "");

    size_t consolidation_auto(float similarity_threshold = 0.90f, size_t max_merges = 20);

    // Meta-cognition support
    std::unique_ptr<duckdb::QueryResult> raw_query(const std::string& sql) const {
        return read_query(sql);
    }

    // Anticipation: context→action pattern learning
    int64_t anticipation_observe(const std::string& context, const std::string& action,
                                  const std::string& realm = "brahman");
    std::vector<AnticipationPattern> anticipation_predict(const std::string& context,
                                                           size_t limit = 5,
                                                           const std::string& realm = "");
    bool anticipation_success(int64_t id);
    std::vector<AnticipationPattern> anticipation_list(const std::string& realm = "",
                                                        size_t limit = 50);

    // Session event logging: append-only event stream
    int64_t event_log_append(const SessionEvent& event);
    std::vector<SessionEvent> event_log_recent(const std::string& session_id,
                                                size_t limit = 50,
                                                const std::string& kind = "");
    size_t event_log_count(const std::string& session_id = "");

    // State segments: work mode inference
    int64_t segment_open(const std::string& session_id, WorkMode mode,
                         float confidence, const std::string& evidence,
                         const std::string& realm = "brahman");
    bool segment_close(int64_t segment_id, int32_t event_count,
                       const std::string& tools_used,
                       const std::string& files_active);
    bool segment_update(int64_t segment_id, WorkMode mode, float confidence,
                        const std::string& evidence);
    std::optional<StateSegment> segment_current(const std::string& session_id);
    std::vector<StateSegment> segment_history(const std::string& session_id,
                                               size_t limit = 20);

    // Anticipation candidates: predictions waiting to be surfaced
    int64_t candidate_create(const AnticipationCandidate& candidate);
    std::vector<AnticipationCandidate> candidate_pending(const std::string& session_id,
                                                          size_t limit = 10);
    bool candidate_surface(int64_t candidate_id);
    bool candidate_resolve(int64_t candidate_id, const std::string& outcome);
    std::optional<AnticipationCandidate> candidate_get(int64_t candidate_id);

    // Annoyance gate: controls prediction surfacing frequency
    bool gate_init(const std::string& session_id);
    std::optional<AnnoyanceGateState> gate_get(const std::string& session_id);
    bool gate_record_surface(const std::string& session_id);
    bool gate_record_outcome(const std::string& session_id, bool correct);
    bool gate_allows(const std::string& session_id, float confidence);
    bool gate_update(const std::string& session_id, float confidence_floor, int64_t cooldown_ms);

    // Habit formation: repeated patterns that strengthen
    int64_t habit_observe(const std::string& trigger, const std::string& response,
                          const std::string& realm = "brahman");
    std::vector<Habit> habit_match(const std::string& context, float min_strength = 0.3f,
                                    const std::string& realm = "");
    bool habit_strengthen(int64_t id, float amount = 0.1f);
    bool habit_weaken(int64_t id, float amount = 0.05f);
    std::vector<Habit> habit_list(const std::string& realm = "", float min_strength = 0.0f,
                                   size_t limit = 50);

    // Background processing: daemon-level tasks
    int64_t background_schedule(const std::string& task_type, const std::string& realm = "brahman");
    std::optional<BackgroundTask> background_claim(const std::string& task_type = "");
    bool background_complete(int64_t id, const std::string& result);
    bool background_fail(int64_t id, const std::string& error);
    struct BackgroundStatus {
        size_t pending = 0;
        size_t running = 0;
        size_t completed_today = 0;
        size_t failed_today = 0;
    };
    BackgroundStatus background_status();
    size_t background_run_cycle();  // Run one processing cycle, returns tasks processed

    // User profile: structured understanding of partner
    bool profile_update(const std::string& user_id, const std::string& field, const std::string& value);
    std::optional<UserProfile> profile_get(const std::string& user_id = "default");
    bool profile_observe(const std::string& observation_type, const std::string& value,
                         const std::string& user_id = "default");

    // Long-term goals: objectives spanning weeks/months
    int64_t goal_set(const std::string& title, const std::string& description,
                     const std::string& milestones_json = "[]", int64_t deadline = 0,
                     const std::string& realm = "brahman");
    std::optional<Goal> goal_get(int64_t id);
    std::vector<Goal> goal_list(const std::string& status = "active", const std::string& realm = "",
                                 size_t limit = 20);
    bool goal_progress(int64_t id, float progress, const std::string& milestone_completed = "");
    bool goal_complete(int64_t id, const std::string& outcome);
    bool goal_update_status(int64_t id, const std::string& status);

    // Confidence calibration: tracking prediction accuracy
    bool calibration_record(const std::string& domain, bool success);
    std::optional<CalibrationScore> calibration_get(const std::string& domain);
    std::vector<CalibrationScore> calibration_all();
    float calibration_adjustment(const std::string& domain);  // Get confidence adjustment for domain

    // Memory hygiene: keep memory bounded and healthy
    HygieneStats hygiene_stats();
    struct HygieneResult {
        size_t decayed = 0;
        size_t pruned = 0;
        size_t consolidated = 0;
    };
    HygieneResult hygiene_run(float prune_threshold = 0.1f, float min_age_days = 7.0f,
                               float consolidation_threshold = 0.85f, size_t max_consolidations = 10);

    // Usage outcome tracking: did surfaced memories actually help?
    int64_t record_usage_outcome(int64_t memory_id, const std::string& session_id,
                                  const std::string& outcome, const std::string& context = "");
    std::vector<UsageOutcome> get_usage_outcomes(int64_t memory_id, size_t limit = 10);
    struct UsageStats {
        int64_t positive = 0;
        int64_t negative = 0;
        int64_t neutral = 0;
        float positive_rate = 0.0f;
    };
    UsageStats get_usage_stats(int64_t memory_id);

    // Episode pattern detection for auto-distillation
    std::vector<DistillCandidate> find_distill_candidates(
        float similarity_threshold = 0.85f,
        size_t min_occurrences = 3,
        size_t limit = 20
    );

    // Provenance tracking: where did knowledge come from?
    bool set_provenance(int64_t memory_id, const std::string& session_id,
                        const std::string& tool_name = "", float trust_score = 0.8f,
                        int64_t derived_from = 0);
    bool get_provenance(int64_t memory_id, std::string& session_id, std::string& tool_name,
                        float& trust_score, int64_t& derived_from);

    // Recall with provenance (extends base recall)
    std::vector<MemoryResult> recall_with_provenance(
        const std::vector<float>& query_embedding,
        size_t k = 10,
        const std::string& realm = "",
        bool include_global = true,
        const std::vector<std::string>& exclude_kinds = {}
    );

    // ═══════════════════════════════════════════════════════════════════════
    // Hybrid Retrieval - Combines Vector + BM25 + Graph for state-of-the-art recall
    // ═══════════════════════════════════════════════════════════════════════

    struct HybridRecallConfig {
        float vector_weight;
        float bm25_weight;
        float graph_weight;
        float recency_weight;
        size_t graph_hops;
        float rrf_k;

        HybridRecallConfig()
            : vector_weight(0.4f)
            , bm25_weight(0.3f)
            , graph_weight(0.2f)
            , recency_weight(0.1f)
            , graph_hops(2)
            , rrf_k(60.0f) {}
    };

    // Hybrid recall: combines vector search, BM25, and graph spreading with RRF fusion
    std::vector<MemoryResult> hybrid_recall(
        const std::vector<float>& query_embedding,
        const std::string& query_text,
        size_t k = 10,
        const std::string& realm = "",
        bool include_global = true,
        HybridRecallConfig config = HybridRecallConfig()
    );

    // Graph spreading: find memories connected via triplets (RWR-lite)
    std::vector<std::pair<int64_t, float>> graph_spread(
        const std::vector<int64_t>& seed_ids,
        size_t max_results = 20,
        size_t max_hops = 2
    );

    // Code intel confidence restoration: fix memories that were incorrectly decayed
    struct CodeIntelRestoreResult {
        size_t total_updated = 0;
        std::vector<std::pair<std::string, size_t>> counts_by_kind;  // kind -> count
        std::vector<std::pair<std::string, float>> avg_confidence_before;  // kind -> avg
    };
    CodeIntelRestoreResult restore_code_intel_confidence(float target_confidence = 0.8f, bool dry_run = false);

    // SQL query (read-only) - for debugging and analysis
    struct SqlQueryResult {
        bool success = false;
        std::string error;
        std::vector<std::string> columns;
        std::vector<std::vector<std::string>> rows;
    };
    SqlQueryResult execute_sql_query(const std::string& sql) const;

    // ═══════════════════════════════════════════════════════════════════════
    // xMemory Theme System: Hierarchical Memory Organization
    // ═══════════════════════════════════════════════════════════════════════

    // Theme CRUD
    int64_t theme_create(const std::string& name, const std::vector<float>& centroid,
                         const std::string& realm = "brahman");
    std::optional<Theme> theme_get(int64_t theme_id);
    std::vector<Theme> theme_list(const std::string& realm = "", size_t limit = 100);
    bool theme_update(int64_t theme_id, const std::optional<std::string>& name = std::nullopt,
                      const std::optional<std::vector<float>>& centroid = std::nullopt,
                      const std::optional<float>& coherence = std::nullopt,
                      const std::optional<int64_t>& representative_id = std::nullopt);
    bool theme_delete(int64_t theme_id);
    size_t theme_count(const std::string& realm = "");

    // Theme membership
    bool theme_assign(int64_t memory_id, int64_t theme_id, float strength = 1.0f,
                      bool is_representative = false);
    bool theme_unassign(int64_t memory_id, int64_t theme_id);
    bool theme_set_representative(int64_t memory_id, int64_t theme_id, bool is_rep);
    std::vector<ThemeMembership> theme_memberships(int64_t theme_id, size_t limit = 100);
    std::vector<ThemeMembership> memory_themes(int64_t memory_id);
    std::optional<int64_t> memory_primary_theme(int64_t memory_id);
    bool set_primary_theme(int64_t memory_id, int64_t theme_id);

    // Theme queries
    std::vector<MemoryResult> theme_members(int64_t theme_id, size_t limit = 50);
    std::vector<MemoryResult> theme_representatives(int64_t theme_id, size_t limit = 3);
    std::vector<Theme> themes_by_relevance(const std::vector<float>& query_embedding,
                                            size_t limit = 5,
                                            const std::string& realm = "");

    // Theme statistics and maintenance
    ThemeStats theme_stats(const std::string& realm = "");
    std::vector<int64_t> get_orphan_memories(size_t limit = 100, const std::string& realm = "");

    // Theme maintenance results
    struct ThemeMaintenanceResult {
        size_t themes_split = 0;
        size_t themes_merged = 0;
        size_t memories_reassigned = 0;
        size_t representatives_updated = 0;
        size_t centroids_recomputed = 0;
    };

    // Orphan memory with embedding (for batch theme assignment)
    struct OrphanMemory {
        int64_t id;
        std::string content;
        std::string kind;
        std::string realm;
        std::vector<float> embedding;
    };
    std::vector<OrphanMemory> get_orphan_memories_with_embeddings(size_t limit = 100,
                                                                   const std::string& realm = "");

    // Count orphan memories (not in any theme)
    size_t count_orphan_memories(const std::string& realm = "") const;

    // ═══════════════════════════════════════════════════════════════════════
    // Cross-Session Messaging
    // ═══════════════════════════════════════════════════════════════════════

    // Session registry
    bool session_register(const std::string& session_id, const std::string& realm,
                          int32_t pid, const std::string& transcript_path = "",
                          const std::string& project_dir = "", const std::string& metadata = "{}");
    bool session_heartbeat(const std::string& session_id, const std::string& metadata = "");
    bool session_deregister(const std::string& session_id);
    std::vector<SessionRegistryEntry> session_list(const std::string& realm = "",
                                                    const std::string& status = "active");
    size_t session_cleanup_dead(int64_t timeout_ms = 600000);

    // Heal session registry by checking if PIDs are still alive
    // Returns number of sessions marked as dead
    size_t session_heal();

    // Sync registry with running processes and transcripts
    struct SessionSyncResult {
        size_t discovered = 0;      // New sessions found from transcripts
        size_t updated = 0;         // Existing sessions updated
        size_t marked_dead = 0;     // Sessions marked dead (process not running)
    };
    SessionSyncResult session_sync(const std::string& claude_projects_dir = "");

    // Messages
    int64_t msg_send(const SessionMessage& msg);
    std::vector<InboxItem> msg_inbox(const std::string& session_id,
                                      size_t limit = 20, int32_t min_priority = 0);
    bool msg_ack(int64_t message_id, const std::string& session_id);
    bool msg_ack_all(const std::string& session_id);
    std::vector<SessionMessage> msg_history(const std::string& session_id,
                                             size_t limit = 50);
    size_t msg_cleanup_expired();

    // ═══════════════════════════════════════════════════════════════════════
    // Conversational Memory System
    // ═══════════════════════════════════════════════════════════════════════

    // Conversation turns - lossless storage
    struct ConversationTurn {
        int64_t id = 0;
        std::string session_id;
        std::string realm;
        std::string role;           // user, assistant, system
        int turn_index = 0;
        std::string content;
        std::string content_hash;
        int token_count = 0;
        int64_t episode_id = 0;
        std::string intent_type;
        std::string tools_used;     // JSON array
        std::string files_touched;  // JSON array
        bool has_error = false;
        int64_t created_at = 0;
    };

    int64_t store_conversation_turn(const ConversationTurn& turn);
    std::vector<ConversationTurn> get_conversation_turns(
        const std::string& session_id, int32_t start_index = 0, size_t limit = 100);
    std::optional<ConversationTurn> get_turn_by_id(int64_t turn_id);

    // Dialogue episodes
    struct DialogueEpisode {
        int64_t id = 0;
        std::string session_id;
        std::string realm;
        std::string title;
        std::string summary;
        int32_t start_turn = 0;
        int32_t end_turn = 0;
        int32_t turn_count = 0;
        std::string episode_type;
        std::string outcome;
        std::string key_entities;   // JSON array
        int64_t created_at = 0;
        int64_t closed_at = 0;
    };

    int64_t create_dialogue_episode(
        const std::string& session_id,
        const std::string& title,
        int32_t start_turn,
        const std::string& episode_type = "",
        const std::string& realm = "brahman");
    bool close_dialogue_episode(
        int64_t episode_id,
        const std::string& outcome,
        const std::string& summary = "",
        const std::string& key_entities = "[]");
    std::optional<DialogueEpisode> get_dialogue_episode(int64_t episode_id);
    std::vector<DialogueEpisode> list_dialogue_episodes(
        const std::string& session_id, size_t limit = 20);

    // Hierarchical memory expansion - drill down from SSL to full context
    struct ExpandedMemory {
        // Level 1: The SSL memory itself
        int64_t memory_id = 0;
        std::string memory_content;
        std::string memory_type;
        float confidence = 0.0f;

        // Level 2: Linked episode (if any)
        int64_t episode_id = 0;
        std::string episode_title;
        std::string episode_summary;
        std::string session_id;
        int32_t start_turn = 0;
        int32_t end_turn = 0;

        // Level 3: Full conversation turns
        std::vector<ConversationTurn> turns;
    };

    // Expand a memory to its full hierarchical context
    // depth: 1 = memory only, 2 = + episode, 3 = + full turns
    std::optional<ExpandedMemory> expand_memory(int64_t memory_id, int depth = 3);

    // Claims - semantic memory with contradiction handling
    struct Claim {
        int64_t id = 0;
        std::string subject;
        std::string predicate;
        std::string object_norm;
        std::string value_json;
        std::string scope_key;      // user|repo|branch|session|task
        int polarity = 1;           // +1 true, -1 false
        float confidence = 0.5f;
        int support_count = 1;
        int contradiction_count = 0;
        int64_t valid_from_ms = 0;
        int64_t valid_to_ms = 0;    // 0 = still valid
        int64_t superseded_by = 0;
        std::string source_class;   // tool, user_explicit, rule, llm_infer
        int64_t source_turn_id = 0;
        std::string realm;
        int64_t created_at = 0;
    };

    int64_t store_claim(const Claim& claim);
    bool supersede_claim(int64_t old_claim_id, int64_t new_claim_id);
    std::vector<Claim> query_claims(const std::string& subject = "",
                                    const std::string& predicate = "",
                                    const std::string& scope_key = "",
                                    bool active_only = true,
                                    size_t limit = 20);
    std::vector<Claim> find_contradicting_claims(const Claim& new_claim);

    // Policy memory with promotion states
    struct PolicyMemory {
        int64_t id = 0;
        std::string policy_type;    // correction, preference, constraint
        std::string content;
        std::string scope_key;
        std::string state;          // ephemeral, candidate, stable_soft, stable_hard
        float confidence = 0.5f;
        int support_count = 1;
        int session_count = 1;
        int violation_count = 0;
        int64_t last_applied_at = 0;
        int64_t last_reinforced_at = 0;
        int64_t source_turn_id = 0;
        std::string realm;
        int64_t created_at = 0;
    };

    int64_t store_policy(const PolicyMemory& policy);
    bool promote_policy(int64_t policy_id, const std::string& new_state);
    bool reinforce_policy(int64_t policy_id);
    std::vector<PolicyMemory> get_active_policies(
        const std::string& scope_key = "",
        const std::string& policy_type = "",
        size_t limit = 50);

    // Entity registry
    struct Entity {
        int64_t id = 0;
        std::string name;
        std::string display_name;
        std::string entity_type;    // person, project, concept, tool, file, function
        std::string description;
        int mention_count = 1;
        float salience_score = 0.5f;
        std::string realm;
        int64_t last_mentioned = 0;
        int64_t created_at = 0;
    };

    int64_t get_or_create_entity(const std::string& name,
                                 const std::string& entity_type,
                                 const std::string& display_name = "");
    bool update_entity_salience(int64_t entity_id);
    std::optional<Entity> get_entity(const std::string& name);
    std::vector<Entity> get_top_entities(const std::string& entity_type = "",
                                         size_t limit = 20);

    // Relationship events
    struct RelationshipEvent {
        int64_t id = 0;
        std::string session_id;
        std::string event_type;     // correction, praise, frustration, discovery
        std::string context;
        std::string content;
        int64_t turn_id = 0;
        int64_t episode_id = 0;
        bool resolved = false;
        std::string resolution;
        std::string realm;
        int64_t created_at = 0;
    };

    int64_t store_relationship_event(const RelationshipEvent& event);
    std::vector<RelationshipEvent> get_relationship_events(
        const std::string& event_type = "",
        const std::string& session_id = "",
        size_t limit = 20);

    // Correction log
    struct CorrectionLogEntry {
        int64_t id = 0;
        std::string session_id;
        int64_t turn_id = 0;
        std::string wrong_belief;
        std::string correct_answer;
        std::string domain;
        int64_t memory_id = 0;
        bool applied = false;
        int recurrence = 0;
        std::string realm;
        int64_t created_at = 0;
    };

    int64_t log_correction(const CorrectionLogEntry& entry);
    std::vector<CorrectionLogEntry> get_corrections(
        const std::string& domain = "",
        size_t limit = 20);
    bool mark_correction_applied(int64_t correction_id, int64_t memory_id);

    // Error tracking
    std::string last_error() const { return last_error_; }

private:
    mutable std::string last_error_;  // Last error message for debugging
    std::unique_ptr<duckdb::DuckDB> db_;
    std::unique_ptr<duckdb::Connection> write_conn_;  // Dedicated write connection
    std::unique_ptr<ConnectionPool> read_pool_;       // Pool for concurrent reads
    std::string path_;
    mutable std::mutex write_mutex_;                  // Only for write operations
    bool vss_loaded_ = false;
    bool pgq_loaded_ = false;
    bool fts_loaded_ = false;
    std::atomic<bool> needs_reindex_{false};  // Flag for deferred index rebuild
    std::atomic<bool> index_exists_{false};   // Track if HNSW index is present

    // Cached health for non-blocking health_check
    mutable StoreHealth cached_health_;
    mutable std::mutex health_cache_mutex_;

    // Separate embeddings database (no write contention with main DB)
    std::unique_ptr<duckdb::DuckDB> emb_db_;
    std::unique_ptr<duckdb::Connection> emb_conn_;    // Dedicated embedding connection
    mutable std::mutex emb_mutex_;                    // Separate mutex for embeddings
    bool emb_attached_ = false;                       // Whether embeddings DB is attached to main
    size_t detected_embed_dim_ = EMBED_DIM;           // Detected embedding dimension from schema

    // Schema creation and migration
    void migrate_embedding_dimensions();
    bool create_schema();
    bool load_extensions();
    bool create_vector_index();
    void fix_sequences();

    // Embeddings database management
    bool open_embeddings_db(const std::string& path);
    bool create_embeddings_schema();
    bool attach_embeddings_db();

    // Helper to execute queries
    // write_execute/write_query: use write connection with mutex (for INSERT/UPDATE/DELETE)
    // read_query: use pool connection (for SELECT) - concurrent safe
    bool write_execute(const std::string& sql);
    std::unique_ptr<duckdb::QueryResult> write_query(const std::string& sql);
    std::unique_ptr<duckdb::QueryResult> read_query(const std::string& sql) const;

    // Convert between types
    static std::string kind_to_string(NodeType type);
    static NodeType string_to_kind(const std::string& kind);

    // Build array literal for embedding
    static std::string embedding_to_sql(const std::vector<float>& embedding);
};

}  // namespace chitta
