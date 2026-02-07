#pragma once
// NarrativeEngine: Work mode inference from event streams
//
// Tracks session state and infers work modes:
// - Orienting: understanding the task/codebase (first few events)
// - Exploring: searching, reading, investigating
// - Implementing: writing code, editing files
// - Debugging: fixing issues, analyzing errors
// - Validating: running tests, checking builds
// - Blocked: waiting on user input or stuck
// - Flow: deep productive work (extended edits without errors)
//
// Uses sliding window counters and heuristic rules for V1.
// Future: ML-based inference, sequence pattern learning.

#include "duckdb_store.hpp"
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <algorithm>
#include <chrono>

namespace chitta {

// Tool classification for event analysis
enum class ToolCategory : uint8_t {
    Read = 0,       // Read, recall, get_memory
    Search = 1,     // Grep, Glob, find_symbol, search_symbols
    Edit = 2,       // Edit, Write, NotebookEdit
    Test = 3,       // Detected from bash: test, pytest, npm test, etc.
    Build = 4,      // Detected from bash: make, cmake, cargo, npm run build
    Other = 5,
};

// Classify tool name to category
inline ToolCategory classify_tool(const std::string& tool_name, const std::string& payload = "") {
    // Direct tool mappings
    if (tool_name == "Read" || tool_name == "recall" || tool_name == "get_memory" ||
        tool_name == "read_symbol" || tool_name == "read_function" ||
        tool_name == "mcp__chitta__recall" || tool_name == "mcp__chitta__get") {
        return ToolCategory::Read;
    }

    if (tool_name == "Grep" || tool_name == "Glob" || tool_name == "find_symbol" ||
        tool_name == "search_symbols" || tool_name == "symbol_callers" ||
        tool_name == "symbol_callees" || tool_name == "query" ||
        tool_name == "mcp__chitta__find_symbol" || tool_name == "mcp__chitta__search_symbols" ||
        tool_name == "mcp__chitta__query" || tool_name == "mcp__chitta__query_graph") {
        return ToolCategory::Search;
    }

    if (tool_name == "Edit" || tool_name == "Write" || tool_name == "NotebookEdit") {
        return ToolCategory::Edit;
    }

    // Bash command analysis
    if (tool_name == "Bash") {
        // Test patterns
        if (payload.find("test") != std::string::npos ||
            payload.find("pytest") != std::string::npos ||
            payload.find("jest") != std::string::npos ||
            payload.find("mocha") != std::string::npos ||
            payload.find("cargo test") != std::string::npos ||
            payload.find("go test") != std::string::npos) {
            return ToolCategory::Test;
        }
        // Build patterns
        if (payload.find("make") != std::string::npos ||
            payload.find("cmake") != std::string::npos ||
            payload.find("cargo build") != std::string::npos ||
            payload.find("npm run build") != std::string::npos ||
            payload.find("yarn build") != std::string::npos ||
            payload.find("go build") != std::string::npos) {
            return ToolCategory::Build;
        }
    }

    return ToolCategory::Other;
}

// Per-session state for mode inference
struct SessionState {
    std::string session_id;
    WorkMode current_mode = WorkMode::Orienting;
    float confidence = 0.5f;
    int64_t segment_id = 0;
    int64_t mode_started_at = 0;

    // Sliding window counters (last ~20 events)
    static constexpr size_t WINDOW_SIZE = 20;
    int recent_reads = 0;
    int recent_edits = 0;
    int recent_errors = 0;
    int recent_tests = 0;
    int recent_searches = 0;
    int recent_builds = 0;

    // Consecutive counters (reset on different action)
    int consecutive_edits = 0;
    int consecutive_errors = 0;
    int consecutive_reads = 0;

    // Total event count in session
    int event_count = 0;

    // Window of last event categories (for sliding window)
    std::vector<ToolCategory> event_window;

    // Tools used in current segment
    std::unordered_set<std::string> tools_used;

    // Files active in current segment
    std::unordered_set<std::string> files_active;

    // Update sliding window
    void push_event(ToolCategory cat, bool is_error, const std::string& tool_name,
                    const std::string& files_mentioned) {
        event_count++;

        // Add to window
        event_window.push_back(cat);
        if (event_window.size() > WINDOW_SIZE) {
            // Remove oldest and decrement its counter
            auto old_cat = event_window.front();
            event_window.erase(event_window.begin());
            switch (old_cat) {
                case ToolCategory::Read: recent_reads--; break;
                case ToolCategory::Search: recent_searches--; break;
                case ToolCategory::Edit: recent_edits--; break;
                case ToolCategory::Test: recent_tests--; break;
                case ToolCategory::Build: recent_builds--; break;
                default: break;
            }
        }

        // Increment counters for new event
        switch (cat) {
            case ToolCategory::Read:
                recent_reads++;
                consecutive_reads++;
                consecutive_edits = 0;
                break;
            case ToolCategory::Search:
                recent_searches++;
                consecutive_reads = 0;
                consecutive_edits = 0;
                break;
            case ToolCategory::Edit:
                recent_edits++;
                consecutive_edits++;
                consecutive_reads = 0;
                break;
            case ToolCategory::Test:
                recent_tests++;
                consecutive_edits = 0;
                consecutive_reads = 0;
                break;
            case ToolCategory::Build:
                recent_builds++;
                consecutive_edits = 0;
                consecutive_reads = 0;
                break;
            default:
                break;
        }

        // Track errors
        if (is_error) {
            recent_errors++;
            consecutive_errors++;
            consecutive_edits = 0;  // Errors break flow
        } else if (cat == ToolCategory::Edit || cat == ToolCategory::Test || cat == ToolCategory::Build) {
            // Successful edit/test/build resets error streak
            if (!is_error) consecutive_errors = 0;
        }

        // Track tool and files
        if (!tool_name.empty()) {
            tools_used.insert(tool_name);
        }

        // Parse files from JSON array
        if (!files_mentioned.empty() && files_mentioned != "[]") {
            // Simple parse: find quoted strings
            size_t pos = 0;
            while ((pos = files_mentioned.find('"', pos)) != std::string::npos) {
                size_t end = files_mentioned.find('"', pos + 1);
                if (end != std::string::npos) {
                    files_active.insert(files_mentioned.substr(pos + 1, end - pos - 1));
                    pos = end + 1;
                } else {
                    break;
                }
            }
        }
    }

    // Reset counters for new segment
    void reset_segment_state() {
        tools_used.clear();
        files_active.clear();
    }
};

// NarrativeEngine: manages session state and infers work modes
class NarrativeEngine {
public:
    NarrativeEngine(DuckDBStore& store) : store_(store) {}

    // Evaluate mode after an event, return current mode
    WorkMode evaluate(const std::string& session_id, const SessionEvent& event) {
        auto& state = get_or_create_state(session_id);

        // Classify the event
        ToolCategory cat = classify_tool(event.tool_name, event.payload);
        bool is_error = !event.success ||
                        event.kind == SessionEventKind::Error ||
                        (event.payload.find("error") != std::string::npos);

        // Update state counters
        state.push_event(cat, is_error, event.tool_name, event.files_mentioned);

        // Run mode inference
        WorkMode new_mode = infer_mode(state);
        float new_confidence = compute_confidence(state, new_mode);

        // Check for mode change
        if (new_mode != state.current_mode) {
            // Close old segment if exists
            if (state.segment_id > 0) {
                close_segment(state);
            }

            // Update state
            state.current_mode = new_mode;
            state.confidence = new_confidence;
            state.reset_segment_state();

            // Open new segment
            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            state.mode_started_at = now;

            std::ostringstream evidence;
            evidence << "{\"reads\":" << state.recent_reads
                     << ",\"edits\":" << state.recent_edits
                     << ",\"errors\":" << state.recent_errors
                     << ",\"tests\":" << state.recent_tests
                     << ",\"searches\":" << state.recent_searches
                     << ",\"consecutive_edits\":" << state.consecutive_edits
                     << ",\"consecutive_errors\":" << state.consecutive_errors
                     << ",\"event_count\":" << state.event_count << "}";

            state.segment_id = store_.segment_open(
                session_id, new_mode, new_confidence, evidence.str()
            );
        } else {
            // Update confidence if changed significantly
            if (std::abs(new_confidence - state.confidence) > 0.1f) {
                state.confidence = new_confidence;
                if (state.segment_id > 0) {
                    std::ostringstream evidence;
                    evidence << "{\"reads\":" << state.recent_reads
                             << ",\"edits\":" << state.recent_edits
                             << ",\"errors\":" << state.recent_errors
                             << ",\"tests\":" << state.recent_tests
                             << ",\"searches\":" << state.recent_searches << "}";
                    store_.segment_update(state.segment_id, state.current_mode,
                                         state.confidence, evidence.str());
                }
            }
        }

        return state.current_mode;
    }

    // Get current mode for session
    WorkMode current_mode(const std::string& session_id) const {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            return it->second.current_mode;
        }
        // Check database for existing segment
        auto seg = store_.segment_current(session_id);
        if (seg) {
            return seg->mode;
        }
        return WorkMode::Orienting;
    }

    // Get current confidence for session
    float current_confidence(const std::string& session_id) const {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            return it->second.confidence;
        }
        auto seg = store_.segment_current(session_id);
        if (seg) {
            return seg->confidence;
        }
        return 0.5f;
    }

    // Generate SSL-formatted narrative summary for context injection
    std::string narrative_ssl(const std::string& session_id) const {
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            auto seg = store_.segment_current(session_id);
            if (!seg) {
                return "[narrative] no active session";
            }
            std::ostringstream ssl;
            ssl << "[narrative:" << work_mode_to_string(seg->mode) << "] "
                << "confidence=" << seg->confidence;
            return ssl.str();
        }

        const auto& state = it->second;
        std::ostringstream ssl;

        ssl << "[narrative:" << work_mode_to_string(state.current_mode) << "] ";

        // Add context about current state
        switch (state.current_mode) {
            case WorkMode::Orienting:
                ssl << "understanding task (" << state.event_count << " events)";
                break;
            case WorkMode::Exploring:
                ssl << state.recent_reads << " reads, " << state.recent_searches << " searches";
                break;
            case WorkMode::Implementing:
                ssl << state.recent_edits << " edits, " << state.consecutive_edits << " consecutive";
                break;
            case WorkMode::Debugging:
                ssl << state.recent_errors << " errors, investigating";
                break;
            case WorkMode::Validating:
                ssl << state.recent_tests << " tests, " << state.recent_builds << " builds";
                break;
            case WorkMode::Blocked:
                ssl << state.consecutive_errors << " consecutive errors";
                break;
            case WorkMode::Flow:
                ssl << state.consecutive_edits << " consecutive edits, deep work";
                break;
        }

        ssl << " @" << state.confidence;

        return ssl.str();
    }

    // Close current session (e.g., on session end)
    void close_session(const std::string& session_id) {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            if (it->second.segment_id > 0) {
                close_segment(it->second);
            }
            sessions_.erase(it);
        }
    }

private:
    DuckDBStore& store_;
    mutable std::unordered_map<std::string, SessionState> sessions_;

    // Get or create session state
    SessionState& get_or_create_state(const std::string& session_id) {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            return it->second;
        }

        // Create new state
        SessionState state;
        state.session_id = session_id;

        // Check for existing open segment in database
        auto seg = store_.segment_current(session_id);
        if (seg) {
            state.current_mode = seg->mode;
            state.confidence = seg->confidence;
            state.segment_id = seg->id;
            state.mode_started_at = seg->started_at;
            state.event_count = seg->event_count;
        } else {
            // Initialize with default mode and create initial segment
            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            state.mode_started_at = now;

            // Create initial segment in database
            state.segment_id = store_.segment_open(
                session_id, WorkMode::Orienting, 0.5f,
                "{\"initial\":true}"
            );
        }

        auto [inserted_it, _] = sessions_.emplace(session_id, std::move(state));
        return inserted_it->second;
    }

    // Infer work mode from state using V1 heuristics
    WorkMode infer_mode(const SessionState& state) const {
        // Rule 1: First 3 events = orienting
        if (state.event_count <= 3) {
            return WorkMode::Orienting;
        }

        // Rule 2: 3+ consecutive errors = blocked
        if (state.consecutive_errors >= 3) {
            return WorkMode::Blocked;
        }

        // Rule 3: 5+ consecutive edits without errors = flow
        if (state.consecutive_edits >= 5 && state.consecutive_errors == 0) {
            return WorkMode::Flow;
        }

        // Rule 4: 2+ recent errors and more errors than edits = debugging
        if (state.recent_errors >= 2 && state.recent_errors > state.recent_edits) {
            return WorkMode::Debugging;
        }

        // Rule 5: 2+ recent tests = validating
        if (state.recent_tests >= 2) {
            return WorkMode::Validating;
        }

        // Rule 6: 2+ recent edits and more edits than reads = implementing
        if (state.recent_edits >= 2 && state.recent_edits > state.recent_reads) {
            return WorkMode::Implementing;
        }

        // Rule 7: 3+ reads + searches = exploring
        if (state.recent_reads + state.recent_searches >= 3) {
            return WorkMode::Exploring;
        }

        // Default: stay in current mode or orienting
        if (state.current_mode != WorkMode::Orienting) {
            return state.current_mode;
        }

        return WorkMode::Orienting;
    }

    // Compute confidence based on how strongly evidence supports mode
    float compute_confidence(const SessionState& state, WorkMode mode) const {
        float conf = 0.5f;

        switch (mode) {
            case WorkMode::Orienting:
                // High confidence at start, decreases as more events happen
                conf = std::max(0.5f, 1.0f - (state.event_count / 10.0f));
                break;

            case WorkMode::Blocked:
                // Higher with more consecutive errors
                conf = std::min(0.95f, 0.6f + (state.consecutive_errors * 0.1f));
                break;

            case WorkMode::Flow:
                // Higher with more consecutive edits
                conf = std::min(0.95f, 0.6f + (state.consecutive_edits * 0.05f));
                break;

            case WorkMode::Debugging:
                // Based on error to edit ratio
                if (state.recent_edits > 0) {
                    float ratio = static_cast<float>(state.recent_errors) / state.recent_edits;
                    conf = std::min(0.9f, 0.5f + (ratio * 0.2f));
                } else {
                    conf = 0.7f;
                }
                break;

            case WorkMode::Validating:
                // Based on test count
                conf = std::min(0.9f, 0.5f + (state.recent_tests * 0.1f));
                break;

            case WorkMode::Implementing:
                // Based on edit dominance
                {
                    int total = state.recent_edits + state.recent_reads + state.recent_searches;
                    if (total > 0) {
                        conf = std::min(0.9f, 0.4f + (static_cast<float>(state.recent_edits) / total) * 0.5f);
                    }
                }
                break;

            case WorkMode::Exploring:
                // Based on read/search dominance
                {
                    int total = state.recent_edits + state.recent_reads + state.recent_searches;
                    if (total > 0) {
                        int explore = state.recent_reads + state.recent_searches;
                        conf = std::min(0.9f, 0.4f + (static_cast<float>(explore) / total) * 0.5f);
                    }
                }
                break;
        }

        return conf;
    }

    // Close segment and persist state
    void close_segment(SessionState& state) {
        if (state.segment_id <= 0) return;

        // Build JSON arrays
        std::ostringstream tools_json;
        tools_json << "[";
        bool first = true;
        for (const auto& tool : state.tools_used) {
            if (!first) tools_json << ",";
            tools_json << "\"" << tool << "\"";
            first = false;
        }
        tools_json << "]";

        std::ostringstream files_json;
        files_json << "[";
        first = true;
        for (const auto& file : state.files_active) {
            if (!first) files_json << ",";
            files_json << "\"" << file << "\"";
            first = false;
        }
        files_json << "]";

        // Calculate event count in segment (approximate)
        int segment_events = static_cast<int>(state.tools_used.size());

        store_.segment_close(state.segment_id, segment_events,
                            tools_json.str(), files_json.str());

        state.segment_id = 0;
    }
};

} // namespace chitta
