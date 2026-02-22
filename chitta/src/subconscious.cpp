// Subconscious: Background processor for autonomous learning
//
// Detects patterns in user/assistant messages and automatically stores
// learnings without requiring explicit calls.

#include <chitta/mind/subconscious.hpp>
#include <iostream>
#include <sstream>

namespace chitta {

Subconscious::Subconscious(DuckDBMind* mind, SubconsciousConfig config)
    : mind_(mind)
    , config_(std::move(config))
{
    // Compile pattern matchers
    // NOTE: std::regex compilation is expensive (~ms each) but only happens once
    // at construction. Runtime matching is also slow compared to RE2 or string
    // matching, but acceptable on the background thread at low event rates.
    //
    // Correction patterns: "no", "wrong", "actually", "that's not", "not X, Y"
    correction_pattern_ = std::regex(
        R"(\b(no[,.]?\s|wrong|actually|that's not|that is not|not\s+\w+[,]\s+)\b)",
        std::regex_constants::icase
    );

    // Preference patterns: "I prefer", "always", "never", "don't"
    preference_pattern_ = std::regex(
        R"(\b(i prefer|always\s+\w+|never\s+\w+|don't\s+\w+|do not\s+\w+|please\s+always|please\s+don't)\b)",
        std::regex_constants::icase
    );

    // Frustration patterns: "stuck", "frustrated", "not working", "broken"
    frustration_pattern_ = std::regex(
        R"(\b(stuck|frustrated|frustrating|not working|doesn't work|broken|confused|lost)\b)",
        std::regex_constants::icase
    );

    // Milestone patterns: "done", "shipped", "working", "success", "finished"
    milestone_pattern_ = std::regex(
        R"(\b(done|shipped|working now|success|finished|completed|fixed|solved|resolved)\b)",
        std::regex_constants::icase
    );

    // Suggestion patterns in assistant messages: "try", "consider", "you could"
    suggestion_pattern_ = std::regex(
        R"(\b(try\s+\w+|consider\s+\w+|you could|you might|perhaps|maybe try|suggest)\b)",
        std::regex_constants::icase
    );

    // Uncertainty patterns in assistant messages: "I don't know", "I'm not sure", etc.
    uncertainty_pattern_ = std::regex(
        R"(\b(i don'?t know|i'?m not sure|unclear|couldn'?t (find|determine)|need to check|i'?ll have to look)\b)",
        std::regex_constants::icase
    );
}

Subconscious::~Subconscious() {
    stop();
}

void Subconscious::start() {
    if (running_.load()) return;

    running_ = true;
    stats_.started_at = now_ms();

    process_thread_ = std::thread([this]() {
        process_loop();
    });

    std::cerr << "[subconscious] Started background processor\n";
}

void Subconscious::stop() {
    if (!running_.load()) return;

    running_ = false;
    queue_cv_.notify_all();

    if (process_thread_.joinable()) {
        process_thread_.join();
    }

    std::cerr << "[subconscious] Stopped (processed=" << stats_.events_processed
              << ", corrections=" << stats_.corrections_detected
              << ", preferences=" << stats_.preferences_detected
              << ", hygiene_runs=" << stats_.hygiene_runs << ")\n";
}

void Subconscious::push_event(SubconsciousEvent event) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (event_queue_.size() >= config_.max_queue_size) {
            event_queue_.pop_front();
        }
        event_queue_.push_back(std::move(event));
    }
    queue_cv_.notify_one();
}

void Subconscious::process_loop() {
    auto last_hygiene = std::chrono::steady_clock::now();

    while (running_.load()) {
        auto event_opt = pop_event_with_timeout(config_.process_interval);

        if (event_opt) {
            stats_.events_processed++;

            switch (event_opt->type) {
                case SubconsciousEventType::UserMessage:
                    process_user_message(*event_opt);
                    break;
                case SubconsciousEventType::AssistantMessage:
                    process_assistant_message(*event_opt);
                    break;
                case SubconsciousEventType::ToolResult:
                    process_tool_result(*event_opt);
                    break;
            }
        }

        // Check for periodic hygiene
        if (config_.enable_hygiene && time_for_hygiene()) {
            run_hygiene();
            last_hygiene = std::chrono::steady_clock::now();
        }

        // Check for theme maintenance (xMemory)
        if (config_.enable_theme_maintenance && time_for_theme_maintenance()) {
            run_theme_maintenance();
        }

        // Check for auto-distillation (episodes -> wisdom)
        if (config_.enable_distillation && time_for_distillation()) {
            run_auto_distillation();
        }

        // Check for background embedding
        if (config_.enable_background_embedding && time_for_embedding()) {
            run_background_embedding();
        }

        // Auto-dream: trigger curiosity-driven exploration when idle > 10 min
        if (dream_callback_ && time_for_dream()) {
            last_dream_triggered_at_ = now_ms();
            try {
                dream_callback_();
            } catch (const std::exception& e) {
                std::cerr << "[subconscious] Dream callback failed: " << e.what() << "\n";
            }
        }
    }
}

void Subconscious::process_user_message(const SubconsciousEvent& event) {
    if (config_.enable_pattern_detection) {
        detect_correction(event.content, event.realm);
        detect_preference(event.content, event.realm);
        detect_frustration(event.content, event.realm);
        detect_milestone(event.content, event.realm);
    }

    if (config_.enable_suggestion_tracking) {
        check_outcomes(event.content, event.realm);
    }

    if (config_.enable_anticipation) {
        // User message can verify a prediction about what they would ask/do
        verify_prediction(event.content, event.realm);
    }
}

void Subconscious::process_assistant_message(const SubconsciousEvent& event) {
    // Detect uncertainty/knowledge gaps in assistant responses
    if (config_.enable_pattern_detection) {
        detect_uncertainty(event.content, event.realm);
    }

    if (config_.enable_suggestion_tracking) {
        // Look for suggestions in assistant output
        std::smatch match;
        if (std::regex_search(event.content, match, suggestion_pattern_)) {
            // Extract context (first 200 chars before the suggestion)
            size_t pos = match.position();
            size_t start = (pos > 200) ? (pos - 200) : 0;
            std::string context = event.content.substr(start, pos - start);

            // Extract suggestion (the matching part plus 100 chars after)
            size_t end = std::min(pos + match.length() + 100, event.content.size());
            std::string suggestion = event.content.substr(pos, end - pos);

            track_suggestion(suggestion, context, event.realm);
        }
    }

    if (config_.enable_anticipation) {
        // Assistant messages represent actions taken - observe patterns
        // Context = what was being discussed, Action = what assistant did
        // This is simplified - in practice, would track more context
        std::lock_guard<std::mutex> lock(anticipation_mutex_);
        if (!last_context_.empty()) {
            observe_pattern(last_context_, event.content.substr(0, 200), event.realm);
        }
        // Update context for next observation
        last_context_ = event.content.substr(0, 200);
    }
}

void Subconscious::process_tool_result(const SubconsciousEvent& event) {
    if (config_.enable_anticipation) {
        verify_prediction(event.content, event.realm);
    }

    // Extract tool name from content (format: "tool_name: result" or just tool_name)
    if (config_.enable_habit_formation) {
        std::string tool_name;
        size_t colon_pos = event.content.find(':');
        if (colon_pos != std::string::npos && colon_pos < 50) {
            tool_name = event.content.substr(0, colon_pos);
        } else {
            // Try to extract first word as tool name
            size_t space_pos = event.content.find(' ');
            if (space_pos != std::string::npos && space_pos < 50) {
                tool_name = event.content.substr(0, space_pos);
            }
        }

        if (!tool_name.empty()) {
            observe_tool_for_habit(tool_name, event.content, event.realm);
        }
    }
}

// Pattern Detection

void Subconscious::detect_correction(const std::string& content, const std::string& realm) {
    std::smatch match;
    if (std::regex_search(content, match, correction_pattern_)) {
        // Extract the correction context (what comes after the correction word)
        size_t pos = match.position() + match.length();
        std::string correction;
        if (pos < content.size()) {
            size_t end = std::min(pos + 200, content.size());
            correction = content.substr(pos, end - pos);
        }

        // Extract what was being corrected (what comes before)
        std::string context;
        if (match.position() > 0) {
            size_t start = (match.position() > 100) ? (match.position() - 100) : 0;
            context = content.substr(start, match.position() - start);
        }

        store_correction(context, correction, realm);
        stats_.corrections_detected++;
    }
}

void Subconscious::detect_preference(const std::string& content, const std::string& realm) {
    std::smatch match;
    if (std::regex_search(content, match, preference_pattern_)) {
        // Extract the preference statement (surrounding text)
        size_t pos = static_cast<size_t>(match.position());
        size_t len = static_cast<size_t>(match.length());
        size_t start = (pos > 50) ? (pos - 50) : 0;
        size_t end = std::min(pos + len + 100, content.size());
        std::string preference = content.substr(start, end - start);

        store_preference(preference, realm);
        stats_.preferences_detected++;
    }
}

void Subconscious::detect_frustration(const std::string& content, const std::string& realm) {
    std::smatch match;
    if (std::regex_search(content, match, frustration_pattern_)) {
        // Extract frustration context
        size_t pos = static_cast<size_t>(match.position());
        size_t len = static_cast<size_t>(match.length());
        size_t start = (pos > 100) ? (pos - 100) : 0;
        size_t end = std::min(pos + len + 100, content.size());
        std::string context = content.substr(start, end - start);

        store_frustration(context, realm);
        stats_.frustrations_detected++;
    }
}

void Subconscious::detect_milestone(const std::string& content, const std::string& realm) {
    std::smatch match;
    if (std::regex_search(content, match, milestone_pattern_)) {
        // Extract milestone achievement
        size_t pos = static_cast<size_t>(match.position());
        size_t len = static_cast<size_t>(match.length());
        size_t start = (pos > 50) ? (pos - 50) : 0;
        size_t end = std::min(pos + len + 100, content.size());
        std::string achievement = content.substr(start, end - start);

        store_milestone(achievement, realm);
        stats_.milestones_detected++;
    }
}

void Subconscious::detect_uncertainty(const std::string& content, const std::string& realm) {
    std::smatch match;
    if (std::regex_search(content, match, uncertainty_pattern_)) {
        // Extract uncertainty context
        size_t pos = static_cast<size_t>(match.position());
        size_t len = static_cast<size_t>(match.length());
        size_t start = (pos > 100) ? (pos - 100) : 0;
        size_t end = std::min(pos + len + 100, content.size());
        std::string context = content.substr(start, end - start);

        store_uncertainty(context, realm);
        stats_.uncertainties_detected++;
    }
}

// Auto-learning Storage
// Uses mind_->remember() to generate proper embeddings via yantra

void Subconscious::store_correction(const std::string& context, const std::string& correction,
                                     const std::string& realm) {
    std::ostringstream content;
    content << "[correction] User corrected me\n";
    if (!context.empty()) {
        content << "Context: " << context << "\n";
    }
    content << "Correction: " << correction;

    // Use mind->remember which generates proper embeddings
    auto id = mind_->remember(content.str(), NodeType::Wisdom,
                              realm.empty() ? "brahman" : realm,
                              RealmVisibility::Global);  // Corrections are global
    if (id != NodeId{}) {
        // Set visibility to global
        mind_->store().set_visibility(static_cast<int64_t>(id.low), RealmVisibility::Global);
    }
}

void Subconscious::store_preference(const std::string& preference, const std::string& realm) {
    std::ostringstream content;
    content << "[preference] " << preference;

    // Preferences are global beliefs
    auto id = mind_->remember(content.str(), NodeType::Belief,
                              "brahman",  // Global realm
                              RealmVisibility::Global);
    if (id != NodeId{}) {
        mind_->store().set_visibility(static_cast<int64_t>(id.low), RealmVisibility::Global);
    }
}

void Subconscious::store_frustration(const std::string& context, const std::string& realm) {
    std::ostringstream content;
    content << "[approach] User was frustrated/stuck\n";
    content << "Context: " << context;

    mind_->remember(content.str(), NodeType::Episode,
                    realm.empty() ? "default" : realm,
                    RealmVisibility::Private);
}

void Subconscious::store_milestone(const std::string& achievement, const std::string& realm) {
    std::ostringstream content;
    content << "[milestone] " << achievement;

    auto id = mind_->remember(content.str(), NodeType::Wisdom,
                              realm.empty() ? "default" : realm,
                              RealmVisibility::Shared);
    if (id != NodeId{}) {
        mind_->store().set_visibility(static_cast<int64_t>(id.low), RealmVisibility::Shared);
    }
}

void Subconscious::store_uncertainty(const std::string& context, const std::string& realm) {
    // Store uncertainty as a curiosity gap (integrates with curiosity system)
    std::ostringstream content;
    content << "[gap] " << context;

    // Use mind->remember which generates proper embeddings
    auto id = mind_->remember(content.str(), NodeType::Episode,  // Use Episode, will be tagged as gap
                              realm.empty() ? "brahman" : realm,
                              RealmVisibility::Private);

    if (id != NodeId{}) {
        // Tag as gap and unresolved for the curiosity system
        mind_->store().add_tag(static_cast<int64_t>(id.low), "gap");
        mind_->store().add_tag(static_cast<int64_t>(id.low), "unresolved");
    }
}

// Suggestion Tracking

void Subconscious::track_suggestion(const std::string& content, const std::string& context,
                                     const std::string& realm) {
    Suggestion suggestion;
    suggestion.content = content;
    suggestion.context = context;
    suggestion.realm = realm;
    suggestion.status = "pending";
    suggestion.suggested_at = now_ms();

    int64_t id = mind_->store().suggestion_track(suggestion);

    if (id > 0) {
        std::lock_guard<std::mutex> lock(suggestions_mutex_);
        pending_suggestions_.push_back({content, context, realm, suggestion.suggested_at, id});

        // Limit pending suggestions
        if (pending_suggestions_.size() > MAX_PENDING_SUGGESTIONS) {
            pending_suggestions_.erase(pending_suggestions_.begin());
        }

        stats_.suggestions_tracked++;
    }
}

void Subconscious::check_outcomes(const std::string& user_message, const std::string& realm) {
    std::lock_guard<std::mutex> lock(suggestions_mutex_);

    // Check if user message indicates success or failure of a suggestion
    bool indicates_success = std::regex_search(user_message, milestone_pattern_);
    bool indicates_failure = std::regex_search(user_message, frustration_pattern_);

    if (!indicates_success && !indicates_failure) return;

    // Look for pending suggestions in the same realm
    for (auto it = pending_suggestions_.begin(); it != pending_suggestions_.end(); ) {
        if (it->realm == realm || realm.empty()) {
            // Resolve the suggestion
            mind_->store().suggestion_resolve(it->db_id, indicates_success, user_message, 0);
            stats_.outcomes_verified++;

            it = pending_suggestions_.erase(it);
        } else {
            ++it;
        }
    }
}

// Anticipation

void Subconscious::observe_pattern(const std::string& context, const std::string& action,
                                    const std::string& realm) {
    // Simplified pattern: store what action was taken in what context
    mind_->store().anticipation_observe(context, action, realm);
}

void Subconscious::verify_prediction(const std::string& actual_action, const std::string& realm) {
    std::lock_guard<std::mutex> lock(anticipation_mutex_);

    if (last_predicted_action_.empty()) return;

    // Simple similarity check: does actual action contain key words from prediction?
    // This is a heuristic - production would use embedding similarity
    bool match = false;
    std::istringstream iss(last_predicted_action_);
    std::string word;
    int matches = 0;
    int total = 0;
    while (iss >> word) {
        if (word.length() >= 4) {
            total++;
            if (actual_action.find(word) != std::string::npos) {
                matches++;
            }
        }
    }

    if (total > 0 && (static_cast<float>(matches) / total) > 0.3f) {
        // Prediction was roughly correct
        auto patterns = mind_->store().anticipation_predict(last_context_, 1, realm);
        for (const auto& p : patterns) {
            mind_->store().anticipation_success(p.id);
        }
    }

    last_predicted_action_.clear();
}

// Habit Formation from Tool Sequences

void Subconscious::observe_tool_for_habit(const std::string& tool_name, const std::string& context,
                                           const std::string& realm) {
    std::lock_guard<std::mutex> lock(tool_sequence_mutex_);

    // Add to sequence
    recent_tool_sequence_.push_back({tool_name, context.substr(0, 100)});
    if (recent_tool_sequence_.size() > MAX_TOOL_SEQUENCE) {
        recent_tool_sequence_.pop_front();
    }

    // Need at least 3 tools to detect a pattern
    if (recent_tool_sequence_.size() < 3) return;

    // Categorize tools into groups for pattern detection
    auto categorize_tool = [](const std::string& name) -> std::string {
        // File reading tools
        if (name == "Read" || name == "cat" || name == "head" || name == "tail") {
            return "read";
        }
        // File editing tools
        if (name == "Edit" || name == "Write" || name == "sed" || name == "awk") {
            return "edit";
        }
        // Search tools
        if (name == "Grep" || name == "Glob" || name == "find" || name == "rg") {
            return "search";
        }
        // Git tools
        if (name.find("git") == 0 || name == "gh") {
            return "git";
        }
        // Build tools
        if (name == "make" || name == "cmake" || name == "npm" || name == "cargo") {
            return "build";
        }
        // Test tools
        if (name == "pytest" || name == "jest" || name == "test" || name.find("test") != std::string::npos) {
            return "test";
        }
        return name;  // Use tool name as category if unrecognized
    };

    // Check for pattern: 3 consecutive same-category tools -> next action
    size_t seq_size = recent_tool_sequence_.size();
    if (seq_size < 3) return;

    std::string cat1 = categorize_tool(recent_tool_sequence_[seq_size - 3].first);
    std::string cat2 = categorize_tool(recent_tool_sequence_[seq_size - 2].first);
    std::string cat3 = categorize_tool(recent_tool_sequence_[seq_size - 1].first);

    // Pattern: Same category repeated 3 times suggests a workflow
    if (cat1 == cat2 && cat2 == cat3) {
        // Build trigger pattern
        std::string trigger = cat1 + "," + cat1 + "," + cat1;
        std::string response = "likely_" + cat1 + "_workflow";

        // Check if we've seen this pattern before (look for repeat in history)
        // by scanning our sequence for previous occurrences
        int pattern_count = 0;
        for (size_t i = 2; i < seq_size; ++i) {
            std::string c1 = categorize_tool(recent_tool_sequence_[i - 2].first);
            std::string c2 = categorize_tool(recent_tool_sequence_[i - 1].first);
            std::string c3 = categorize_tool(recent_tool_sequence_[i].first);
            if (c1 == cat1 && c2 == cat1 && c3 == cat1) {
                pattern_count++;
            }
        }

        // If pattern has occurred 2+ times, form a habit
        if (pattern_count >= 2) {
            int64_t id = mind_->store().habit_observe(trigger, response, realm.empty() ? "brahman" : realm);
            if (id > 0) {
                stats_.habits_formed++;
            }
        }
    }

    // Also check for transition patterns: A,A -> B suggests "after reading, edit"
    if (seq_size >= 3) {
        std::string prev_cat = categorize_tool(recent_tool_sequence_[seq_size - 2].first);
        std::string curr_cat = cat3;

        // If previous two were same category and now different, it's a transition
        if (cat1 == cat2 && cat2 != curr_cat) {
            std::string trigger = cat1 + "," + cat1;
            std::string response = curr_cat;

            // Count how often this transition occurs
            int transition_count = 0;
            for (size_t i = 2; i < seq_size; ++i) {
                std::string c1 = categorize_tool(recent_tool_sequence_[i - 2].first);
                std::string c2 = categorize_tool(recent_tool_sequence_[i - 1].first);
                std::string c3 = categorize_tool(recent_tool_sequence_[i].first);
                if (c1 == cat1 && c2 == cat1 && c3 == curr_cat) {
                    transition_count++;
                }
            }

            if (transition_count >= 2) {
                int64_t id = mind_->store().habit_observe(trigger, response, realm.empty() ? "brahman" : realm);
                if (id > 0) {
                    stats_.habits_formed++;
                }
            }
        }
    }
}

// Periodic Tasks

void Subconscious::run_hygiene() {
    auto result = mind_->store().hygiene_run(
        0.1f,   // prune_threshold
        7.0f,   // min_age_days
        0.85f,  // consolidation_threshold
        10      // max_consolidations
    );

    stats_.hygiene_runs++;
    stats_.last_hygiene_at = now_ms();

    std::cerr << "[subconscious] Hygiene run: decayed=" << result.decayed
              << ", pruned=" << result.pruned
              << ", consolidated=" << result.consolidated << "\n";

    // Heal session registry - check if PIDs are still alive
    size_t healed_sessions = mind_->store().session_heal();
    if (healed_sessions > 0) {
        std::cerr << "[subconscious] Healed " << healed_sessions << " stale sessions (dead PIDs)\n";
    }

    // Clean dead sessions (no heartbeat for 10+ minutes)
    size_t dead_sessions = mind_->store().session_cleanup_dead(600000);
    if (dead_sessions > 0) {
        std::cerr << "[subconscious] Cleaned " << dead_sessions << " dead sessions\n";
    }

    // Clean expired messages
    size_t expired_msgs = mind_->store().msg_cleanup_expired();
    if (expired_msgs > 0) {
        std::cerr << "[subconscious] Cleaned " << expired_msgs << " expired messages\n";
    }
}

bool Subconscious::time_for_hygiene() const {
    auto last = stats_.last_hygiene_at.load();
    if (last == 0) return true;  // Never run

    auto now = now_ms();
    auto interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        config_.hygiene_interval
    ).count();

    return (now - last) >= interval_ms;
}

void Subconscious::run_theme_maintenance() {
    // Skip if daemon is busy with queries
    if (!is_idle()) {
        return;
    }

    auto result = mind_->run_theme_maintenance();

    stats_.theme_maintenance_runs++;
    stats_.themes_split += result.themes_split;
    stats_.themes_merged += result.themes_merged;
    stats_.memories_reassigned += result.memories_reassigned;
    stats_.last_theme_maintenance_at = now_ms();

    std::cerr << "[subconscious] Theme maintenance: split=" << result.themes_split
              << ", merged=" << result.themes_merged
              << ", reassigned=" << result.memories_reassigned
              << ", reps_updated=" << result.representatives_updated
              << ", centroids=" << result.centroids_recomputed << "\n";
}

bool Subconscious::time_for_theme_maintenance() const {
    auto last = stats_.last_theme_maintenance_at.load();
    if (last == 0) return true;  // Never run

    auto now = now_ms();
    auto interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        config_.theme_maintenance_interval
    ).count();

    return (now - last) >= interval_ms;
}

void Subconscious::run_auto_distillation() {
    // Skip if daemon is busy with queries
    if (!is_idle()) {
        return;
    }

    // Run auto-distillation to convert repeated episode patterns into wisdom
    size_t created = mind_->auto_distill_episodes(
        5,      // max_distillations per run
        0.85f,  // similarity_threshold
        3       // min_occurrences
    );

    stats_.distillation_runs++;
    stats_.wisdom_created += created;
    stats_.last_distillation_at = now_ms();

    if (created > 0) {
        std::cerr << "[subconscious] Auto-distillation: created " << created
                  << " wisdom nodes from episode patterns\n";
    }
}

bool Subconscious::time_for_distillation() const {
    auto last = stats_.last_distillation_at.load();
    if (last == 0) return true;  // Never run

    auto now = now_ms();
    auto interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        config_.distillation_interval
    ).count();

    return (now - last) >= interval_ms;
}

void Subconscious::run_background_embedding() {
    // Always update timestamp to prevent tight loop
    stats_.last_embedding_at = now_ms();

    // Skip if daemon is busy with queries (idle-based scheduling)
    if (!is_idle()) {
        stats_.embedding_skips++;
        return;
    }

    if (!mind_->has_yantra()) return;

    try {
        // Get small batch of unembedded symbols (quick DB read)
        auto symbols = mind_->store().get_unembedded_symbols(config_.embedding_batch_size);
        if (symbols.empty()) return;

        // Build embedding texts
        std::vector<std::string> texts;
        std::vector<int64_t> ids;
        for (const auto& sym : symbols) {
            std::string text = sym.kind + " " + sym.name;
            if (!sym.signature.empty()) text += " " + sym.signature;
            texts.push_back(text);
            ids.push_back(sym.id);
        }

        // Generate embeddings (CPU-bound, no DB access)
        auto embeddings = mind_->embedder().embed_batch(texts);

        // Queue results for main thread to flush (no DB write here)
        {
            std::lock_guard<std::mutex> lock(embedding_queue_mutex_);
            for (size_t i = 0; i < embeddings.size(); ++i) {
                if (!embeddings[i].is_zero()) {
                    embedding_queue_.push_back({ids[i], embeddings[i].data});
                    stats_.embeddings_queued++;
                }
            }
        }

        if (!embeddings.empty()) {
            std::cerr << "[subconscious] Queued " << embeddings.size() << " embeddings for flush\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[subconscious] Embedding error: " << e.what() << "\n";
    }
}

size_t Subconscious::flush_embedding_queue() {
    std::vector<QueuedEmbedding> to_flush;

    // Quickly grab queued embeddings
    {
        std::lock_guard<std::mutex> lock(embedding_queue_mutex_);
        if (embedding_queue_.empty()) return 0;
        to_flush.swap(embedding_queue_);
    }

    // Write to DB (on main thread, no contention)
    size_t flushed = 0;
    for (const auto& qe : to_flush) {
        if (mind_->store().set_symbol_embedding(qe.symbol_id, qe.embedding)) {
            flushed++;
        }
    }

    if (flushed > 0) {
        stats_.symbols_embedded += flushed;
        std::cerr << "[subconscious] Flushed " << flushed << " embeddings to DB\n";
    }

    return flushed;
}

size_t Subconscious::embedding_queue_size() const {
    std::lock_guard<std::mutex> lock(embedding_queue_mutex_);
    return embedding_queue_.size();
}

void Subconscious::notify_query() {
    stats_.last_query_at = now_ms();
}

bool Subconscious::is_idle() const {
    auto last_query = stats_.last_query_at.load();
    if (last_query == 0) return true;  // Never queried = idle

    auto now = now_ms();
    auto threshold_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        config_.idle_threshold
    ).count();

    return (now - last_query) >= threshold_ms;
}

bool Subconscious::time_for_embedding() const {
    auto last = stats_.last_embedding_at.load();
    if (last == 0) return true;  // Never run

    auto now = now_ms();
    auto interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        config_.embedding_interval
    ).count();

    return (now - last) >= interval_ms;
}

bool Subconscious::time_for_dream() const {
    // Idle > 10 min (600,000ms)
    auto last_query = stats_.last_query_at.load();
    auto now = now_ms();
    if (last_query != 0 && (now - last_query) < 600000LL) return false;

    // Last dream trigger > 4 hours ago (14,400,000ms)
    auto last_dream = last_dream_triggered_at_.load();
    if (last_dream != 0 && (now - last_dream) < 14400000LL) return false;

    return true;
}

// Helpers

int64_t Subconscious::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::optional<SubconsciousEvent> Subconscious::pop_event_with_timeout(std::chrono::seconds timeout) {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    if (queue_cv_.wait_for(lock, timeout, [this] {
        return !event_queue_.empty() || !running_.load();
    })) {
        if (!running_.load()) return std::nullopt;
        if (event_queue_.empty()) return std::nullopt;

        auto event = std::move(event_queue_.front());
        event_queue_.pop_front();
        return event;
    }

    return std::nullopt;
}

}  // namespace chitta
