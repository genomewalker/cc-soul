#pragma once
// Anticipator: Proactive assistance through prediction
//
// Generates anticipation candidates based on:
// - Rule-based patterns: work mode → likely next action
// - Sequence patterns: recent events → predicted action
// - File-based gotchas: surface warnings for problematic files
//
// Uses the annoyance gate to avoid over-surfacing predictions.
// Tracks outcomes to learn what helps.

#include "duckdb_store.hpp"
#include "narrative.hpp"
#ifdef CHITTA_FIELD_AVAILABLE
#include "field_store.hpp"
#else
namespace chitta { class FieldStore; }
#endif
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_set>
#include <sstream>
#include <chrono>

namespace chitta {

using json = nlohmann::json;

class Anticipator {
public:
    // New constructor: FieldStore-first, DuckDB optional (for migration)
    Anticipator(FieldStore* field_store, NarrativeEngine* narrative,
                DuckDBStore* legacy_store = nullptr)
        : field_store_(field_store), narrative_(narrative), store_(legacy_store) {}

    // Legacy constructor: DuckDB-first (deprecated, for backward compat)
    Anticipator(DuckDBStore* store, NarrativeEngine* narrative)
        : store_(store), narrative_(narrative) {}

    void set_field_store(FieldStore* fs) { field_store_ = fs; }
    void set_legacy_store(DuckDBStore* s) { store_ = s; }

    // Generate anticipation candidates for a session
    // Calls rule-based and sequence-based generators
    // Returns number of candidates generated
    size_t generate(const std::string& session_id) {
        if (!store_) return 0;  // DuckDB needed for event log + candidates
        size_t count = 0;

        // Get current session state from narrative engine
        WorkMode mode = narrative_->current_mode(session_id);
        float confidence = narrative_->current_confidence(session_id);

        // Generate rule-based candidates
        count += generate_rule_based(session_id, mode, confidence);

        // Generate sequence-based candidates
        count += generate_sequence_based(session_id);

        return count;
    }

    // Adjust annoyance gate parameters based on current work mode
    // Call this before filter() to adapt gate to context
    void adjust_gate_for_mode(const std::string& session_id) {
        if (!store_) return;
        WorkMode mode = narrative_->current_mode(session_id);

        float confidence_floor;
        int64_t cooldown_ms;

        switch (mode) {
            case WorkMode::Blocked:
                // When blocked, be more helpful: lower threshold, shorter cooldown
                confidence_floor = 0.5f;
                cooldown_ms = 60000;  // 60 seconds
                break;

            case WorkMode::Flow:
                // In flow state, minimize interruptions: higher threshold, longer cooldown
                confidence_floor = 0.85f;
                cooldown_ms = 300000;  // 5 minutes
                break;

            case WorkMode::Debugging:
                // Debugging benefits from gotchas: moderate threshold
                confidence_floor = 0.6f;
                cooldown_ms = 90000;  // 90 seconds
                break;

            case WorkMode::Implementing:
                // Implementing: moderate settings, surface gotchas if relevant
                confidence_floor = 0.65f;
                cooldown_ms = 120000;  // 2 minutes
                break;

            case WorkMode::Validating:
                // Validating: slightly relaxed, tests might reveal issues
                confidence_floor = 0.6f;
                cooldown_ms = 120000;  // 2 minutes
                break;

            case WorkMode::Exploring:
            case WorkMode::Orienting:
            default:
                // Default settings for exploration/orientation
                confidence_floor = 0.7f;
                cooldown_ms = 120000;  // 2 minutes
                break;
        }

        store_->gate_update(session_id, confidence_floor, cooldown_ms);
    }

    // Filter candidates through annoyance gate
    // Returns candidates that pass the gate (up to max)
    // Note: Automatically calls adjust_gate_for_mode() for state-adaptive behavior
    std::vector<AnticipationCandidate> filter(const std::string& session_id, size_t max = 2) {
        if (!store_) return {};
        // Adjust gate for current mode before filtering
        adjust_gate_for_mode(session_id);

        std::vector<AnticipationCandidate> result;

        // Get pending candidates
        auto candidates = store_->candidate_pending(session_id, 10);

        for (const auto& candidate : candidates) {
            if (result.size() >= max) break;

            // Check if gate allows this candidate
            if (store_->gate_allows(session_id, candidate.confidence)) {
                // Mark as surfaced
                store_->candidate_surface(candidate.id);
                result.push_back(candidate);
            }
        }

        return result;
    }

    // Record the outcome of a surfaced prediction
    void record_outcome(int64_t candidate_id, bool correct) {
        if (!store_) return;
        std::string outcome = correct ? "correct" : "incorrect";
        store_->candidate_resolve(candidate_id, outcome);

        // Get candidate to find session
        auto candidate = store_->candidate_get(candidate_id);
        if (candidate) {
            store_->gate_record_outcome(candidate->session_id, correct);
        }
    }

private:
    DuckDBStore* store_ = nullptr;
    NarrativeEngine* narrative_;
    FieldStore* field_store_ = nullptr;

    // Rule-based generator: switch on current WorkMode
    size_t generate_rule_based(const std::string& session_id, WorkMode mode, float mode_confidence) {
        size_t count = 0;

        // Get recent events for context
        auto events = store_->event_log_recent(session_id, 20);

        // Count recent event types
        int recent_reads = 0;
        int recent_edits = 0;
        int recent_errors = 0;
        int recent_tests = 0;

        for (const auto& event : events) {
            if (event.kind == SessionEventKind::ToolUse || event.kind == SessionEventKind::ToolResult) {
                ToolCategory cat = classify_tool(event.tool_name, event.payload);
                switch (cat) {
                    case ToolCategory::Read: recent_reads++; break;
                    case ToolCategory::Edit: recent_edits++; break;
                    case ToolCategory::Test: recent_tests++; break;
                    default: break;
                }
            }
            if (!event.success || event.kind == SessionEventKind::Error) {
                recent_errors++;
            }
        }

        switch (mode) {
            case WorkMode::Exploring:
                // After reading several files, likely to start editing
                if (recent_reads >= 3) {
                    create_candidate(session_id, "About to start editing",
                                     AnticipationSource::Rule, 0.6f, mode,
                                     build_evidence("exploring", recent_reads, "reads"));
                    count++;
                }
                break;

            case WorkMode::Debugging:
                // Multiple errors suggest adding a test case
                if (recent_errors >= 2) {
                    create_candidate(session_id, "Consider adding a test case",
                                     AnticipationSource::Rule, 0.5f, mode,
                                     build_evidence("debugging", recent_errors, "errors"));
                    count++;
                }
                break;

            case WorkMode::Implementing:
                // Many edits suggests running tests
                if (recent_edits >= 5) {
                    create_candidate(session_id, "Consider running tests",
                                     AnticipationSource::Rule, 0.65f, mode,
                                     build_evidence("implementing", recent_edits, "edits"));
                    count++;
                }
                // Surface file-specific gotchas when implementing
                count += surface_file_gotchas(session_id);
                break;

            case WorkMode::Blocked:
                // Surface relevant gotchas from memory
                count += surface_gotchas(session_id);
                break;

            case WorkMode::Validating:
                // If tests pass, suggest commit
                if (recent_tests > 0 && recent_errors == 0) {
                    create_candidate(session_id, "Consider committing",
                                     AnticipationSource::Rule, 0.55f, mode,
                                     build_evidence("validating", recent_tests, "tests"));
                    count++;
                }
                break;

            case WorkMode::Orienting:
                // Surface unresolved curiosity gaps when orienting
                count += surface_curiosity_gaps(session_id);
                break;

            default:
                // No rule-based predictions for Flow
                break;
        }

        return count;
    }

    // Sequence-based generator: pattern matching on recent events
    size_t generate_sequence_based(const std::string& session_id) {
        size_t count = 0;

        // Get last 5 events
        auto events = store_->event_log_recent(session_id, 5);
        if (events.size() < 3) return 0;  // Need at least 3 events for patterns

        // Build sequence string for pattern matching
        std::vector<ToolCategory> sequence;
        for (const auto& event : events) {
            if (event.kind == SessionEventKind::ToolUse) {
                sequence.push_back(classify_tool(event.tool_name, event.payload));
            }
        }

        // Match known patterns
        // Pattern: [explore, explore, explore] -> edit
        if (sequence.size() >= 3) {
            int explore_count = 0;
            for (size_t i = 0; i < 3 && i < sequence.size(); i++) {
                if (sequence[i] == ToolCategory::Read || sequence[i] == ToolCategory::Search) {
                    explore_count++;
                }
            }
            if (explore_count >= 3) {
                create_candidate(session_id, "Ready to start implementing",
                                 AnticipationSource::Sequence, 0.55f,
                                 narrative_->current_mode(session_id),
                                 "{\"pattern\":\"explore_explore_explore\"}");
                count++;
            }
        }

        // Pattern: [edit, edit, edit] -> test
        if (sequence.size() >= 3) {
            int edit_count = 0;
            for (size_t i = 0; i < 3 && i < sequence.size(); i++) {
                if (sequence[i] == ToolCategory::Edit) {
                    edit_count++;
                }
            }
            if (edit_count >= 3) {
                create_candidate(session_id, "Consider running tests after edits",
                                 AnticipationSource::Sequence, 0.6f,
                                 narrative_->current_mode(session_id),
                                 "{\"pattern\":\"edit_edit_edit\"}");
                count++;
            }
        }

        // Use learned patterns from anticipation_predict
        auto learned = store_->anticipation_predict(build_context_string(events), 3);
        for (const auto& pattern : learned) {
            // Only use high-frequency patterns with good success rate
            if (pattern.frequency >= 3 && pattern.success_count > pattern.frequency / 2) {
                float conf = static_cast<float>(pattern.success_count) / pattern.frequency;
                conf = std::min(0.8f, conf);  // Cap at 0.8

                create_candidate(session_id, pattern.action,
                                 AnticipationSource::Sequence, conf,
                                 narrative_->current_mode(session_id),
                                 "{\"learned_pattern_id\":" + std::to_string(pattern.id) + "}");
                count++;
            }
        }

        // Use habits for pattern prediction
        // Habits are stronger than anticipation patterns because they've been reinforced
        std::string context_str = build_context_string(events);
        auto habits = store_->habit_match(context_str, 0.5f);
        for (const auto& habit : habits) {
            // Confidence is proportional to habit strength, capped at 0.85
            float conf = std::min(0.85f, habit.strength);

            std::ostringstream evidence;
            evidence << "{\"habit_id\":" << habit.id << ","
                     << "\"habit_strength\":" << habit.strength << ","
                     << "\"trigger\":\"" << habit.trigger_pattern << "\"}";

            create_candidate(session_id, habit.response,
                             AnticipationSource::Sequence, conf,
                             narrative_->current_mode(session_id),
                             evidence.str());
            count++;

            // Limit to 2 habit predictions
            if (count >= 5) break;
        }

        return count;
    }

    // Helper: create and store a candidate
    void create_candidate(const std::string& session_id, const std::string& prediction,
                          AnticipationSource source, float confidence, WorkMode mode,
                          const std::string& evidence) {
        AnticipationCandidate candidate;
        candidate.session_id = session_id;
        candidate.prediction = prediction;
        candidate.source = source;
        candidate.confidence = confidence;
        candidate.current_mode = work_mode_to_string(mode);
        candidate.evidence = evidence;
        candidate.created_at = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        store_->candidate_create(candidate);
    }

    // Helper: build evidence JSON
    std::string build_evidence(const std::string& mode, int count, const std::string& type) {
        std::ostringstream json;
        json << "{\"mode\":\"" << mode << "\",\"" << type << "\":" << count << "}";
        return json.str();
    }

    // Helper: surface gotchas from memory for blocked state
    size_t surface_gotchas(const std::string& session_id) {
        size_t count = 0;

        // Get recent events to extract file context
        auto events = store_->event_log_recent(session_id, 20);
        auto files = extract_active_files(events);

        // Try file-based gotcha surfacing first
        if (!files.empty()) {
            for (const auto& file : files) {
                std::string filename = extract_filename(file);

#ifdef CHITTA_FIELD_AVAILABLE
                if (field_store_) {
                    auto hits = field_store_->recall_keyword(filename, 5);
                    for (const auto& hit : hits) {
                        if (hit.score < 0.5f) continue;
                        if (hit.kind != "gotcha") continue;

                        // Higher confidence for blocked state (0.7-0.85)
                        float confidence = 0.7f + (hit.confidence * 0.15f);

                        std::ostringstream evidence;
                        evidence << "{\"file\":\"" << filename << "\","
                                 << "\"memory_id\":" << hit.memory_id << ","
                                 << "\"bm25_score\":" << hit.score << ","
                                 << "\"blocked\":true}";

                        std::string prediction = "Gotcha: " + hit.content.substr(0, 120);
                        if (hit.content.length() > 120) prediction += "...";
                        prediction += " (in " + filename + ")";

                        create_candidate(session_id, prediction,
                                         AnticipationSource::EpisodeMatch, confidence,
                                         WorkMode::Blocked, evidence.str());
                        count++;

                        if (count >= 2) return count;
                    }
                } else {
#endif
                // BM25 search for memories mentioning this file
                auto hits = store_->bm25_search_memory(filename, 5, "", true);

                for (const auto& [memory_id, score] : hits) {
                    if (score < 0.5f) continue;

                    auto mem = store_->get_memory(memory_id);
                    if (!mem || mem->kind != "gotcha") continue;

                    // Higher confidence for blocked state (0.7-0.85)
                    float confidence = 0.7f + (mem->confidence * 0.15f);

                    std::ostringstream evidence;
                    evidence << "{\"file\":\"" << filename << "\","
                             << "\"memory_id\":" << memory_id << ","
                             << "\"bm25_score\":" << score << ","
                             << "\"blocked\":true}";

                    std::string prediction = "Gotcha: " + mem->content.substr(0, 120);
                    if (mem->content.length() > 120) prediction += "...";
                    prediction += " (in " + filename + ")";

                    create_candidate(session_id, prediction,
                                     AnticipationSource::EpisodeMatch, confidence,
                                     WorkMode::Blocked, evidence.str());
                    count++;

                    if (count >= 2) return count;
                }
#ifdef CHITTA_FIELD_AVAILABLE
                }
#endif
            }
        }

        // Fallback: if no file-specific gotchas, extract context from payloads
        if (count == 0) {
            std::string context;
            for (const auto& event : events) {
                if (!event.payload.empty() && event.payload.length() < 200) {
                    context += event.payload + " ";
                    if (context.length() > 300) break;
                }
            }

            if (!context.empty()) {
                std::string ctx_query = context.substr(0, 100);
#ifdef CHITTA_FIELD_AVAILABLE
                if (field_store_) {
                    auto hits = field_store_->recall_keyword(ctx_query, 3);
                    for (const auto& hit : hits) {
                        if (hit.score < 0.3f) continue;
                        if (hit.kind != "gotcha") continue;

                        float confidence = 0.65f + (hit.confidence * 0.15f);

                        std::ostringstream evidence;
                        evidence << "{\"memory_id\":" << hit.memory_id << ","
                                 << "\"bm25_score\":" << hit.score << ","
                                 << "\"blocked\":true,\"context_match\":true}";

                        std::string prediction = "Check: " + hit.content.substr(0, 120);
                        if (hit.content.length() > 120) prediction += "...";

                        create_candidate(session_id, prediction,
                                         AnticipationSource::EpisodeMatch, confidence,
                                         WorkMode::Blocked, evidence.str());
                        count++;

                        if (count >= 1) return count;
                    }
                } else {
#endif
                // Search for gotchas matching the context
                auto hits = store_->bm25_search_memory(ctx_query, 3, "", true);

                for (const auto& [memory_id, score] : hits) {
                    if (score < 0.3f) continue;

                    auto mem = store_->get_memory(memory_id);
                    if (!mem || mem->kind != "gotcha") continue;

                    float confidence = 0.65f + (mem->confidence * 0.15f);

                    std::ostringstream evidence;
                    evidence << "{\"memory_id\":" << memory_id << ","
                             << "\"bm25_score\":" << score << ","
                             << "\"blocked\":true,\"context_match\":true}";

                    std::string prediction = "Check: " + mem->content.substr(0, 120);
                    if (mem->content.length() > 120) prediction += "...";

                    create_candidate(session_id, prediction,
                                     AnticipationSource::EpisodeMatch, confidence,
                                     WorkMode::Blocked, evidence.str());
                    count++;

                    if (count >= 1) return count;
                }
#ifdef CHITTA_FIELD_AVAILABLE
                }
#endif
            }
        }

        return count;
    }

    // Helper: build context string from events for pattern lookup
    std::string build_context_string(const std::vector<SessionEvent>& events) {
        std::ostringstream ctx;
        for (size_t i = 0; i < std::min(events.size(), size_t(3)); i++) {
            if (i > 0) ctx << ",";
            ctx << events[i].tool_name;
        }
        return ctx.str();
    }

    // Helper: extract active file paths from events
    std::unordered_set<std::string> extract_active_files(const std::vector<SessionEvent>& events) {
        std::unordered_set<std::string> files;
        for (const auto& e : events) {
            if (!e.files_mentioned.empty() && e.files_mentioned != "[]") {
                // Parse JSON array
                try {
                    auto arr = json::parse(e.files_mentioned);
                    for (const auto& f : arr) {
                        if (f.is_string()) files.insert(f.get<std::string>());
                    }
                } catch (...) {}
            }
        }
        return files;
    }

    // Helper: extract just the filename from a path
    std::string extract_filename(const std::string& path) {
        auto pos = path.find_last_of("/\\");
        if (pos != std::string::npos) {
            return path.substr(pos + 1);
        }
        return path;
    }

    // Surface file-specific gotchas for implementing mode
    size_t surface_file_gotchas(const std::string& session_id) {
        size_t count = 0;

        // Get recent events to find active files
        auto events = store_->event_log_recent(session_id, 20);
        auto files = extract_active_files(events);

        if (files.empty()) return 0;

        // For each file, search for gotcha memories that mention it
        for (const auto& file : files) {
            std::string filename = extract_filename(file);

#ifdef CHITTA_FIELD_AVAILABLE
            if (field_store_) {
                auto hits = field_store_->recall_keyword(filename, 5);
                for (const auto& hit : hits) {
                    if (hit.score < 0.5f) continue;
                    if (hit.kind != "gotcha") continue;

                    float confidence = 0.6f + (hit.confidence * 0.2f);

                    std::ostringstream evidence;
                    evidence << "{\"file\":\"" << filename << "\","
                             << "\"memory_id\":" << hit.memory_id << ","
                             << "\"bm25_score\":" << hit.score << ","
                             << "\"memory_confidence\":" << hit.confidence << "}";

                    std::string prediction = "Watch out: " + hit.content.substr(0, 100);
                    if (hit.content.length() > 100) prediction += "...";
                    prediction += " (when editing " + filename + ")";

                    create_candidate(session_id, prediction,
                                     AnticipationSource::EpisodeMatch, confidence,
                                     WorkMode::Implementing, evidence.str());
                    count++;

                    if (count >= 2) return count;
                }
            } else {
#endif
            // BM25 search for memories mentioning this file
            auto hits = store_->bm25_search_memory(filename, 5, "", true);

            for (const auto& [memory_id, score] : hits) {
                // Skip low-scoring hits
                if (score < 0.5f) continue;

                // Get memory details and check if it's a gotcha
                auto mem = store_->get_memory(memory_id);
                if (!mem || mem->kind != "gotcha") continue;

                // Map memory confidence (0-1) to anticipation confidence (0.6-0.8)
                float confidence = 0.6f + (mem->confidence * 0.2f);

                // Build evidence JSON
                std::ostringstream evidence;
                evidence << "{\"file\":\"" << filename << "\","
                         << "\"memory_id\":" << memory_id << ","
                         << "\"bm25_score\":" << score << ","
                         << "\"memory_confidence\":" << mem->confidence << "}";

                // Create candidate with specific warning
                std::string prediction = "Watch out: " + mem->content.substr(0, 100);
                if (mem->content.length() > 100) prediction += "...";
                prediction += " (when editing " + filename + ")";

                create_candidate(session_id, prediction,
                                 AnticipationSource::EpisodeMatch, confidence,
                                 WorkMode::Implementing, evidence.str());
                count++;

                // Limit to 2 gotchas per session to avoid noise
                if (count >= 2) return count;
            }
#ifdef CHITTA_FIELD_AVAILABLE
            }
#endif
        }

        return count;
    }

    // Surface unresolved curiosity gaps when orienting
    size_t surface_curiosity_gaps(const std::string& session_id) {
        size_t count = 0;

        // Query unresolved gaps using raw SQL (same as tool_curiosity_gaps)
        std::string sql = "SELECT m.id, m.content, m.confidence FROM memory m "
                          "JOIN memory_tags t ON m.id = t.memory_id "
                          "WHERE t.tag = 'unresolved' AND m.kind = 'gap' "
                          "ORDER BY m.created_at DESC LIMIT 3";

        auto result = store_->raw_query(sql);
        if (!result) return 0;

        auto chunk = result->Fetch();
        while (chunk && chunk->size() > 0 && count < 1) {
            for (size_t i = 0; i < chunk->size() && count < 1; i++) {
                int64_t id = chunk->GetValue(0, i).GetValue<int64_t>();
                std::string content = chunk->GetValue(1, i).ToString();
                float gap_confidence = chunk->GetValue(2, i).GetValue<float>();

                // Strip [gap] prefix if present
                if (content.substr(0, 6) == "[gap] ") {
                    content = content.substr(6);
                }
                if (content.length() > 120) {
                    content = content.substr(0, 120) + "...";
                }

                // Create candidate with low confidence (curiosity, not urgent)
                float confidence = 0.45f + (gap_confidence * 0.15f);

                std::ostringstream evidence;
                evidence << "{\"gap_id\":" << id << ","
                         << "\"gap_confidence\":" << gap_confidence << ","
                         << "\"orienting\":true}";

                std::string prediction = "Unresolved question: " + content;

                create_candidate(session_id, prediction,
                                 AnticipationSource::EpisodeMatch, confidence,
                                 WorkMode::Orienting, evidence.str());
                count++;
            }
            chunk = result->Fetch();
        }

        return count;
    }
};

}  // namespace chitta
