// Subconscious: Background processor for autonomous learning
//
// Detects patterns in user/assistant messages and automatically stores
// learnings without requiring explicit calls.

#include <chitta/field_store.hpp>
#include <chitta/mind/subconscious.hpp>
#include <chitta/ssl_gloss.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <unordered_map>

namespace chitta {

Subconscious::Subconscious(FieldStore* field_store, VakYantra* embedder, SubconsciousConfig config)
    : field_store_(field_store)
    , embedder_(embedder)
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

    if (config_.enable_background_embedding && embedder_ && field_store_) {
        embed_thread_ = std::thread([this]() {
            embed_loop();
        });
    }

    std::cerr << "[subconscious] Started background processor\n";
}

void Subconscious::stop() {
    if (!running_.load()) return;

    running_ = false;
    queue_cv_.notify_all();

    if (process_thread_.joinable()) {
        process_thread_.join();
    }
    if (embed_thread_.joinable()) {
        embed_thread_.join();
    }

    std::cerr << "[subconscious] Stopped (processed=" << stats_.events_processed
              << ", corrections=" << stats_.corrections_detected
              << ", preferences=" << stats_.preferences_detected
              << ", hygiene_runs=" << stats_.hygiene_runs << ")\n";
}

std::unique_lock<std::shared_mutex> Subconscious::write_lock() {
    if (rpc_mutex_) return std::unique_lock<std::shared_mutex>(*rpc_mutex_);
    return {};
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

        // Theme maintenance via FieldStore
        if (config_.enable_theme_maintenance && time_for_theme_maintenance()) {
            run_theme_maintenance();
        }

        // Sleep consolidation: encode new memories into cortical index + snapshot
        if (config_.enable_sleep_consolidation && field_store_ && time_for_sleep_consolidation()) {
            run_sleep_consolidation();
        }

        // Demotion pass: tier demotion + hard-deletion of weak memories
        if (config_.enable_sleep_consolidation && field_store_ && time_for_demotion()) {
            run_demotion_pass();
        }

        // Belief maintenance: stale demotion + contradiction resolution + duplicate consolidation
        if (config_.enable_belief_maintenance && maintenance_cb_ && time_for_belief_maintenance()) {
            last_belief_maintenance_ = std::chrono::steady_clock::now();
            stats_.belief_maintenance_runs++;
            stats_.last_belief_maintenance_at = now_ms();
            try {
                maintenance_cb_();
            } catch (const std::exception& e) {
                std::cerr << "[subconscious] Belief maintenance failed: " << e.what() << "\n";
            }
        }

        // Autonomous learning cycle: auto-resolve debts, cluster wisdom, calibrate scorer
        if (config_.enable_learning_cycle && field_store_ && time_for_learning_cycle()) {
            run_learning_cycle();
        }

        // Code intel staleness: restore confidence of code intel memories
        if (config_.enable_code_intel_staleness && field_store_ && time_for_code_intel_staleness()) {
            run_code_intel_staleness();
        }

        // Correction promotion: elevate cross-project corrections to brahman
        if (config_.enable_correction_promotion && field_store_ && time_for_correction_promotion()) {
            run_correction_promotion();
        }

        // Episode pruning is manual-only (RPC tool `prune_episodes`) — see
        // register_system_tools.cpp. Do NOT add an automatic periodic call here:
        // it silently truncates episode count to 80% of max_count regardless of
        // how much real history exists, and has already caused two data-loss
        // incidents (removed in 9faaf4d9, reintroduced in a1b1abae). Enforced by
        // scripts/check-no-auto-prune.sh in CI.

        // Background embedding runs in embed_thread_ (dedicated thread, no rpc_mutex).

        // Auto-dream: trigger curiosity-driven exploration when idle > 10 min
        if (dream_callback_ && time_for_dream()) {
            last_dream_triggered_at_ = now_ms();
            try {
                dream_callback_();
            } catch (const std::exception& e) {
                std::cerr << "[subconscious] Dream callback failed: " << e.what() << "\n";
            }
        }

        // Auto-think: trigger internal memory synthesis when idle > 5 min, hourly
        if (think_callback_ && time_for_think()) {
            last_think_triggered_at_ = now_ms();
            try {
                think_callback_();
            } catch (const std::exception& e) {
                std::cerr << "[subconscious] Think callback failed: " << e.what() << "\n";
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
        verify_prediction(event.content, event.realm);
    }
}

void Subconscious::process_assistant_message(const SubconsciousEvent& event) {
    if (config_.enable_pattern_detection) {
        detect_uncertainty(event.content, event.realm);
    }

    if (config_.enable_suggestion_tracking) {
        std::smatch match;
        if (std::regex_search(event.content, match, suggestion_pattern_)) {
            size_t pos = match.position();
            size_t start = (pos > 200) ? (pos - 200) : 0;
            std::string context = event.content.substr(start, pos - start);

            size_t end = std::min(pos + match.length() + 100, event.content.size());
            std::string suggestion = event.content.substr(pos, end - pos);

            track_suggestion(suggestion, context, event.realm);
        }
    }

    if (config_.enable_anticipation) {
        std::lock_guard<std::mutex> lock(anticipation_mutex_);
        if (!last_context_.empty()) {
            observe_pattern(last_context_, event.content.substr(0, 200), event.realm);
        }
        last_context_ = event.content.substr(0, 200);
    }
}

void Subconscious::process_tool_result(const SubconsciousEvent& event) {
    if (config_.enable_anticipation) {
        verify_prediction(event.content, event.realm);
    }

    if (config_.enable_habit_formation) {
        std::string tool_name;
        size_t colon_pos = event.content.find(':');
        if (colon_pos != std::string::npos && colon_pos < 50) {
            tool_name = event.content.substr(0, colon_pos);
        } else {
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
        size_t pos = match.position() + match.length();
        std::string correction;
        if (pos < content.size()) {
            size_t end = std::min(pos + 200, content.size());
            correction = content.substr(pos, end - pos);
        }

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
// Uses FieldStore::remember() with embeddings from VakYantra

std::vector<float> Subconscious::embed(const std::string& text) {
    if (!embedder_ || !embedder_->ready()) return {};
    Artha artha = embedder_->transform(text);
    if (artha.nu.is_zero()) return {};
    return artha.nu.data;
}

// Score correction quality [0.5, 0.95] based on specificity and context richness.
// Higher score -> stored with higher confidence -> more weight in recall.
float Subconscious::score_correction_quality(const std::string& correction,
                                              const std::string& context) {
    float score = 0.60f;

    if (correction.size() > 50)  score += 0.08f;
    if (correction.size() > 120) score += 0.05f;

    if (!context.empty()) score += 0.08f;

    if (correction.find(".cpp") != std::string::npos ||
        correction.find(".hpp") != std::string::npos ||
        correction.find("::") != std::string::npos ||
        correction.find("()") != std::string::npos) {
        score += 0.07f;
    }

    static const std::vector<std::string> action_words = {
        "always", "never", "use", "prefer", "avoid", "instead", "should", "must"
    };
    for (const auto& w : action_words) {
        if (correction.find(w) != std::string::npos) { score += 0.05f; break; }
    }

    return std::max(0.50f, std::min(0.95f, score));
}

void Subconscious::store_correction(const std::string& context, const std::string& correction,
                                     const std::string& realm) {
    std::ostringstream content;
    content << "[correction] User corrected me\n";
    if (!context.empty()) {
        content << "Context: " << context << "\n";
    }
    content << "Correction: " << correction;

    float confidence = score_correction_quality(correction, context);
    auto embedding = embed(content.str());
    if (embedding.empty()) return;

    { auto lk = write_lock(); field_store_->remember("correction",
                           realm.empty() ? "brahman" : realm,
                           content.str(), embedding, confidence, 0.005f); }
}

void Subconscious::store_preference(const std::string& preference, const std::string& realm) {
    std::ostringstream content;
    content << "[preference] " << preference;

    auto embedding = embed(content.str());
    if (embedding.empty()) return;

    { auto lk = write_lock(); field_store_->remember("preference", "brahman",
                           content.str(), embedding, 0.8f, 0.0f); }
}

void Subconscious::store_frustration(const std::string& context, const std::string& realm) {
    std::ostringstream content;
    content << "[approach] User was frustrated/stuck\n";
    content << "Context: " << context;

    auto embedding = embed(content.str());
    if (embedding.empty()) return;

    { auto lk = write_lock(); field_store_->remember("episode",
                           realm.empty() ? "default" : realm,
                           content.str(), embedding, 0.8f, 0.03f); }
}

void Subconscious::store_milestone(const std::string& achievement, const std::string& realm) {
    std::ostringstream content;
    content << "[milestone] " << achievement;

    auto embedding = embed(content.str());
    if (embedding.empty()) return;

    { auto lk = write_lock(); field_store_->remember("wisdom",
                           realm.empty() ? "default" : realm,
                           content.str(), embedding, 0.8f, 0.005f); }
}

void Subconscious::store_uncertainty(const std::string& context, const std::string& realm) {
    std::ostringstream content;
    content << "[gap] " << context;

    auto embedding = embed(content.str());
    if (embedding.empty()) return;

    { auto lk = write_lock(); field_store_->remember("episode",
                           realm.empty() ? "brahman" : realm,
                           content.str(), embedding, 0.8f, 0.03f); }
}

// Suggestion Tracking
// Suggestions are stored as memories in FieldStore (no separate suggestions table)

void Subconscious::track_suggestion(const std::string& content, const std::string& context,
                                     const std::string& realm) {
    std::ostringstream text;
    text << "[suggestion] " << content;
    if (!context.empty()) {
        text << "\nContext: " << context;
    }

    auto embedding = embed(text.str());
    if (embedding.empty()) return;

    uint64_t id;
    { auto lk = write_lock(); id = field_store_->remember("episode", realm.empty() ? "brahman" : realm,
                                          text.str(), embedding, 0.6f, 0.03f); }

    if (id > 0) {
        std::lock_guard<std::mutex> lock(suggestions_mutex_);
        pending_suggestions_.push_back({content, context, realm, now_ms(), static_cast<int64_t>(id)});

        if (pending_suggestions_.size() > MAX_PENDING_SUGGESTIONS) {
            pending_suggestions_.erase(pending_suggestions_.begin());
        }

        stats_.suggestions_tracked++;
    }
}

void Subconscious::check_outcomes(const std::string& user_message, const std::string& realm) {
    std::lock_guard<std::mutex> lock(suggestions_mutex_);

    bool indicates_success = std::regex_search(user_message, milestone_pattern_);
    bool indicates_failure = std::regex_search(user_message, frustration_pattern_);

    if (!indicates_success && !indicates_failure) return;

    for (auto it = pending_suggestions_.begin(); it != pending_suggestions_.end(); ) {
        if (it->realm == realm || realm.empty()) {
            // Strengthen or weaken the suggestion memory based on outcome
            if (indicates_success) {
                { auto lk = write_lock(); field_store_->strengthen(static_cast<uint64_t>(it->db_id), 0.2f); }
            } else {
                { auto lk = write_lock(); field_store_->weaken(static_cast<uint64_t>(it->db_id), 0.2f); }
            }
            stats_.outcomes_verified++;
            it = pending_suggestions_.erase(it);
        } else {
            ++it;
        }
    }
}

// Anticipation
// Simplified: patterns are stored as memories tagged with [anticipation]

void Subconscious::observe_pattern(const std::string& context, const std::string& action,
                                    const std::string& realm) {
    std::ostringstream text;
    text << "[anticipation] context: " << context.substr(0, 200)
         << " -> action: " << action.substr(0, 200);

    auto embedding = embed(text.str());
    if (embedding.empty()) return;

    { auto lk = write_lock(); field_store_->remember("episode", realm.empty() ? "brahman" : realm,
                           text.str(), embedding, 0.5f, 0.03f); }
}

void Subconscious::verify_prediction(const std::string& actual_action, const std::string& realm) {
    std::lock_guard<std::mutex> lock(anticipation_mutex_);
    if (last_predicted_action_.empty()) return;

    // Simple word-overlap heuristic
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

    // No DB-backed anticipation success tracking; just clear the prediction
    last_predicted_action_.clear();
}

// Habit Formation from Tool Sequences
// Habits are stored as memories tagged with [habit]

void Subconscious::observe_tool_for_habit(const std::string& tool_name, const std::string& context,
                                           const std::string& realm) {
    std::lock_guard<std::mutex> lock(tool_sequence_mutex_);

    recent_tool_sequence_.push_back({tool_name, context.substr(0, 100)});
    if (recent_tool_sequence_.size() > MAX_TOOL_SEQUENCE) {
        recent_tool_sequence_.pop_front();
    }

    if (recent_tool_sequence_.size() < 3) return;

    auto categorize_tool = [](const std::string& name) -> std::string {
        if (name == "Read" || name == "cat" || name == "head" || name == "tail") return "read";
        if (name == "Edit" || name == "Write" || name == "sed" || name == "awk") return "edit";
        if (name == "Grep" || name == "Glob" || name == "find" || name == "rg") return "search";
        if (name.find("git") == 0 || name == "gh") return "git";
        if (name == "make" || name == "cmake" || name == "npm" || name == "cargo") return "build";
        if (name == "pytest" || name == "jest" || name == "test" || name.find("test") != std::string::npos) return "test";
        return name;
    };

    size_t seq_size = recent_tool_sequence_.size();
    if (seq_size < 3) return;

    std::string cat1 = categorize_tool(recent_tool_sequence_[seq_size - 3].first);
    std::string cat2 = categorize_tool(recent_tool_sequence_[seq_size - 2].first);
    std::string cat3 = categorize_tool(recent_tool_sequence_[seq_size - 1].first);

    // Pattern: Same category repeated 3 times suggests a workflow
    if (cat1 == cat2 && cat2 == cat3) {
        int pattern_count = 0;
        for (size_t i = 2; i < seq_size; ++i) {
            std::string c1 = categorize_tool(recent_tool_sequence_[i - 2].first);
            std::string c2 = categorize_tool(recent_tool_sequence_[i - 1].first);
            std::string c3 = categorize_tool(recent_tool_sequence_[i].first);
            if (c1 == cat1 && c2 == cat1 && c3 == cat1) {
                pattern_count++;
            }
        }

        if (pattern_count >= 2) {
            std::string trigger = cat1 + "," + cat1 + "," + cat1;
            std::string response = "likely_" + cat1 + "_workflow";

            std::ostringstream text;
            text << "[habit] trigger: " << trigger << " -> response: " << response;

            auto embedding = embed(text.str());
            if (!embedding.empty()) {
                { auto lk = write_lock(); field_store_->remember("wisdom", realm.empty() ? "brahman" : realm,
                                       text.str(), embedding, 0.6f, 0.005f); }
                stats_.habits_formed++;
            }
        }
    }

    // Transition patterns: A,A -> B
    if (seq_size >= 3 && cat1 == cat2 && cat2 != cat3) {
        int transition_count = 0;
        for (size_t i = 2; i < seq_size; ++i) {
            std::string c1 = categorize_tool(recent_tool_sequence_[i - 2].first);
            std::string c2 = categorize_tool(recent_tool_sequence_[i - 1].first);
            std::string c3 = categorize_tool(recent_tool_sequence_[i].first);
            if (c1 == cat1 && c2 == cat1 && c3 == cat3) {
                transition_count++;
            }
        }

        if (transition_count >= 2) {
            std::string trigger = cat1 + "," + cat1;

            std::ostringstream text;
            text << "[habit] trigger: " << trigger << " -> response: " << cat3;

            auto embedding = embed(text.str());
            if (!embedding.empty()) {
                { auto lk = write_lock(); field_store_->remember("wisdom", realm.empty() ? "brahman" : realm,
                                       text.str(), embedding, 0.6f, 0.005f); }
                stats_.habits_formed++;
            }
        }
    }
}

// Periodic Tasks

void Subconscious::run_theme_maintenance() {
    if (!is_idle() || !field_store_) return;

    try {
        std::string result_json;
        { auto lk = write_lock(); result_json = field_store_->theme_maintain(); }
        stats_.theme_maintenance_runs++;
        stats_.last_theme_maintenance_at = now_ms();

        std::cerr << "[subconscious] Theme maintenance via FieldStore: " << result_json << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[subconscious] Theme maintenance failed: " << e.what() << "\n";
    }
}

bool Subconscious::time_for_theme_maintenance() const {
    auto last = stats_.last_theme_maintenance_at.load();
    if (last == 0) return true;

    auto now = now_ms();
    auto interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        config_.theme_maintenance_interval
    ).count();

    return (now - last) >= interval_ms;
}

void Subconscious::run_sleep_consolidation() {
    stats_.last_sleep_consolidation_at = now_ms();
    try {
        // Consolidation-as-merge (THEORY.md §6): refresh stale competitive
        // weights in the background so recalls (budget 16) rarely pay it.
        size_t refreshed = field_store_->cw_refresh_sweep(256);
        if (refreshed > 0) {
            std::cerr << "[subconscious] CW refresh sweep: " << refreshed << " memories\n";
        }
        size_t encoded = field_store_->encode_all();
        bool snapped      = field_store_->save_snapshot();
        bool full_snapped = field_store_->save_full_snapshot();
        if (full_snapped && time_for_wal_compact()) {
            try {
                size_t deleted = field_store_->compact_wal();
                stats_.last_compact_wal_at = now_ms();
                std::cerr << "[subconscious] WAL compact: deleted " << deleted << " segments\n";
            } catch (const std::exception& e) {
                std::cerr << "[subconscious] WAL compact failed: " << e.what() << "\n";
            }
        }

        stats_.sleep_consolidation_runs++;

        std::cerr << "[subconscious] Sleep consolidation: encoded=" << encoded
                  << ", cortical_snapshot=" << (snapped ? "ok" : "failed")
                  << ", full_snapshot=" << (full_snapped ? "ok" : "failed") << "\n";

        // Train lite encoder if enough memories with sparse codes and not yet trained
        if (!field_store_->lite_encoder_ready()) {
            size_t mem_count = field_store_->memory_count();
            if (mem_count >= 500) {
                std::cerr << "[subconscious] Training lite encoder from " << mem_count << " memories...\n";
                if (field_store_->train_lite_encoder()) {
                    field_store_->save_lite_encoder();
                    std::cerr << "[subconscious] Lite encoder trained and saved.\n";
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[subconscious] Sleep consolidation failed: " << e.what() << "\n";
    }
}

bool Subconscious::time_for_wal_compact() const {
    auto last = stats_.last_compact_wal_at.load();
    if (last == 0) return true;
    return (now_ms() - last) >= 24LL * 3600 * 1000;
}

bool Subconscious::time_for_sleep_consolidation() const {
    auto last = stats_.last_sleep_consolidation_at.load();
    if (last == 0) return true;

    auto interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        config_.sleep_consolidation_interval
    ).count();

    return (now_ms() - last) >= interval_ms;
}

void Subconscious::run_demotion_pass() {
    stats_.last_demotion_at = now_ms();
    try {
        auto [demoted, deleted] = field_store_->run_demotion(now_ms());

        stats_.demotion_runs++;
        stats_.field_demoted += demoted;
        stats_.field_deleted += deleted;

        std::cerr << "[subconscious] Demotion pass: demoted=" << demoted
                  << ", deleted=" << deleted << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[subconscious] Demotion pass failed: " << e.what() << "\n";
    }
}

bool Subconscious::time_for_demotion() const {
    auto last = stats_.last_demotion_at.load();
    if (last == 0) return true;

    auto interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        config_.demotion_interval
    ).count();

    return (now_ms() - last) >= interval_ms;
}

bool Subconscious::time_for_belief_maintenance() const {
    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
        std::chrono::steady_clock::now() - last_belief_maintenance_).count();
    return elapsed >= config_.belief_maintenance_interval.count();
}

bool Subconscious::time_for_learning_cycle() const {
    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
        std::chrono::steady_clock::now() - last_learning_cycle_).count();
    return elapsed >= config_.learning_cycle_interval.count();
}

void Subconscious::run_learning_cycle() {
    last_learning_cycle_ = std::chrono::steady_clock::now();
    stats_.learning_cycle_runs++;
    stats_.last_learning_cycle_at = now_ms();
    try {
        // Move 3: Auto-resolve epistemic debts with sufficient evidence
        {
            char* raw;
            raw = cf_auto_resolve_debts(field_store_->handle(), 0.70f);
            if (raw) {
                auto result = nlohmann::json::parse(raw, nullptr, false);
                cf_free_string(raw);
                if (!result.is_discarded()) {
                    stats_.debts_auto_resolved += result.value("resolved_count", 0);
                }
            }
        }

        // Move 5: Query recent surprise events — read-only stats, no lock needed
        {
            auto* raw = cf_surprise_learning_stats(field_store_->handle());
            if (raw) {
                auto sl_stats = nlohmann::json::parse(raw, nullptr, false);
                cf_free_string(raw);
                (void)sl_stats;
            }
        }

        // Move 6: Batch scorer calibration — read-only stats, no lock needed
        {
            auto* raw = cf_learned_scorer_stats(field_store_->handle());
            if (raw) {
                auto scorer = nlohmann::json::parse(raw, nullptr, false);
                cf_free_string(raw);
                if (!scorer.is_discarded()) {
                    uint64_t outcomes = scorer.value("outcome_count", uint64_t(0));
                    uint64_t version = scorer.value("model_version", uint64_t(0));
                    if (outcomes > 0 && version > 0) {
                        stats_.scorer_updates.store(version);
                    }
                }
            }
        }

        // Layer 7: Auto-close stale open interventions (> 30 min)
        {
            int closed;
            closed = cf_close_stale_interventions(field_store_->handle(), 1800000LL);
            if (closed > 0) {
                stats_.interventions_auto_closed += static_cast<size_t>(closed);
            }
        }

        // Move: Auto-complete tasks whose all criteria are met
        {
            char* raw;
            raw = cf_auto_complete_tasks(field_store_->handle());
            if (raw) {
                auto result = nlohmann::json::parse(raw, nullptr, false);
                cf_free_string(raw);
                if (!result.is_discarded()) {
                    auto count = result.value("completed_count", 0ULL);
                    if (count > 0) stats_.tasks_auto_completed += count;
                }
            }
        }

        // Layer 9: Wisdom Homeostasis — staleness tick + TTL expiry demotions
        {
            char* tick_raw;
            tick_raw = cf_tick_lineage_staleness(field_store_->handle());
            if (tick_raw) {
                auto result = nlohmann::json::parse(tick_raw, nullptr, false);
                cf_free_string(tick_raw);
                if (!result.is_discarded()) {
                    stats_.lineage_staleness_ticks++;
                    auto transitioned = result.value("count", 0);
                    if (transitioned > 0)
                        stats_.lineages_inflamed += static_cast<size_t>(transitioned);
                }
            }

            char* expiry_raw;
            expiry_raw = cf_lineage_expiry_check(field_store_->handle());
            if (expiry_raw) {
                auto result = nlohmann::json::parse(expiry_raw, nullptr, false);
                cf_free_string(expiry_raw);
                if (!result.is_discarded()) {
                    if (result.contains("expired_ids") && result["expired_ids"].is_array()) {
                        for (auto& id_val : result["expired_ids"]) {
                            auto lid = id_val.get<uint64_t>();
                            cf_transition_wisdom_lineage(
                                field_store_->handle(), lid, 3 /*Demoted*/,
                                "rederive_ttl_expired", 0);
                            stats_.lineages_demoted_ttl++;
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[subconscious] Learning cycle failed: " << e.what() << "\n";
    }
}

bool Subconscious::time_for_code_intel_staleness() const {
    auto elapsed = std::chrono::duration_cast<std::chrono::hours>(
        std::chrono::steady_clock::now() - last_code_intel_staleness_).count();
    return elapsed >= config_.code_intel_staleness_interval_hours;
}

void Subconscious::run_code_intel_staleness() {
    last_code_intel_staleness_ = std::chrono::steady_clock::now();
    stats_.code_intel_staleness_runs++;
    try {
        const std::vector<std::string> kinds = {
            "symbol", "projectessence", "modulestate", "patternstate"
        };
        size_t restored = 0;
        for (const auto& kind : kinds) {
            auto hits = field_store_->recall_by_kind(kind, 2000);
            for (const auto& h : hits) {
                if (h.confidence < config_.code_intel_min_confidence) {
                    float delta = config_.code_intel_target_confidence - h.confidence;
                    if (delta > 0.0f) {
                        field_store_->strengthen(h.memory_id, delta);
                        restored++;
                    }
                }
            }
        }
        stats_.code_intel_memories_restored += restored;
        if (restored > 0) {
            std::cerr << "[subconscious] Code intel staleness: restored "
                      << restored << " memories\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[subconscious] Code intel staleness failed: " << e.what() << "\n";
    }
}

bool Subconscious::time_for_correction_promotion() const {
    auto elapsed = std::chrono::steady_clock::now() - last_correction_promotion_;
    auto interval = std::chrono::hours(config_.correction_promotion_interval_hours);
    return elapsed >= interval;
}

void Subconscious::run_correction_promotion() {
    last_correction_promotion_ = std::chrono::steady_clock::now();
    stats_.correction_promotion_runs++;
    try {
        auto json_str = field_store_->recall_filtered("wisdom", "", 0.0f, 0.0f, 5000);
        auto mems = nlohmann::json::parse(json_str, nullptr, false);
        if (!mems.is_array()) return;

        // Group correction memories by content key (first 80 chars after "[correction]")
        // value: map from realm -> memory_id (first seen wins per realm)
        std::unordered_map<std::string, std::unordered_map<std::string, uint64_t>> by_key;

        for (const auto& m : mems) {
            if (!m.contains("content") || !m.contains("realm") || !m.contains("id")) continue;
            std::string content = m.value("content", std::string{});
            std::string realm   = m.value("realm", std::string{});
            uint64_t    id      = m.value("id", uint64_t{0});

            auto pos = content.find("[correction]");
            if (pos == std::string::npos) continue;

            std::string key = content.substr(pos, std::min(content.size() - pos, size_t(80)));
            auto& realm_map = by_key[key];
            if (realm_map.find(realm) == realm_map.end()) {
                realm_map[realm] = id;
            }
        }

        size_t promoted = 0;
        for (auto& [key, realm_map] : by_key) {
            // Already in brahman — nothing to do
            if (realm_map.count("brahman")) continue;

            // Only promote if correction appears in enough distinct project realms
            if ((int)realm_map.size() < config_.correction_promotion_min_realms) continue;

            // Promote the first entry: move it to brahman
            auto it = realm_map.begin();
            bool promoted_now;
            promoted_now = field_store_->set_realm(it->second, "brahman");
            if (promoted_now) {
                promoted++;
                std::cerr << "[subconscious] Correction promoted to brahman (id="
                          << it->second << ", realms=" << realm_map.size() << ")\n";
            }
        }

        stats_.correction_promotions += promoted;
        if (promoted > 0) {
            std::cerr << "[subconscious] Correction promotion: promoted "
                      << promoted << " corrections to brahman\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[subconscious] Correction promotion failed: " << e.what() << "\n";
    }
}

bool Subconscious::time_for_background_embedding() const {
    auto last = stats_.last_embedding_at.load();
    if (last == 0) return true;
    auto interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        config_.embedding_interval).count();
    return (now_ms() - last) >= interval_ms;
}

// Dedicated embedding thread — runs independently of process_loop, holds no RPC mutex.
void Subconscious::embed_loop() {
    const std::string prefix = "search_document: ";
    // Initial delay: let WAL replay and HNSW load finish.
    for (int i = 0; i < 150 && running_.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // One-time cleanup: clear states whose payloads are missing (orphan states from
    // prior crashes or partial writes). These never become embeddable.
    if (field_store_) {
        size_t purged = field_store_->purge_orphan_embed_pending();
        if (purged > 0) {
            std::cerr << "[embed_loop] Purged " << purged << " orphan embed_pending states\n";
            field_store_->flush();
        }
    }

    while (running_.load()) {
        auto pending = field_store_->pending_embeddings(config_.embedding_batch_size);
        if (!pending.empty()) {
            stats_.last_embedding_at = now_ms();
            size_t embedded = 0;

            // Collect texts for the batch (content fetch + gloss prep).
            std::vector<uint64_t>    batch_ids;
            std::vector<std::string> batch_texts;
            batch_ids.reserve(pending.size());
            batch_texts.reserve(pending.size());
            std::vector<uint64_t> unembed_ids;
            for (uint64_t id : pending) {
                if (!running_.load()) break;
                auto content = field_store_->get_content(id);
                if (content.size() < 20) { unembed_ids.push_back(id); continue; }
                batch_ids.push_back(id);
                batch_texts.push_back(prefix + chitta::ssl::retrieval_text(content));
            }
            if (!unembed_ids.empty()) {
                field_store_->force_clear_embed_pending(unembed_ids);
                field_store_->flush();  // persist clears so they survive daemon restart
            }

            // One ONNX session_->Run() for the whole batch.
            if (!batch_ids.empty() && embedder_ && embedder_->ready()) {
                auto arthas = embedder_->transform_batch(batch_texts);
                for (size_t i = 0; i < batch_ids.size() && i < arthas.size(); ++i) {
                    if (arthas[i].nu.is_zero()) {
                        stats_.embedding_skips++;
                        unembed_ids.push_back(batch_ids[i]);
                        continue;
                    }
                    field_store_->backfill_embedding(batch_ids[i], arthas[i].nu.data);
                    embedded++;
                }
                if (!unembed_ids.empty())
                    field_store_->force_clear_embed_pending(unembed_ids);
            }

            if (embedded > 0) {
                stats_.symbols_embedded += embedded;
                std::cerr << "[embed_loop] Embedded " << embedded << "/" << pending.size() << " pending\n";
            }
        }
        // Periodic maintenance every 10 embed cycles (~5 min at 30s/cycle).
        if (++embed_cycle_count_ % 10 == 0)
            field_store_->maybe_compact_wal(50);
        if (embed_cycle_count_ % 50 == 0)
            field_store_->promote_staged_memories();
        // Sleep config_.embedding_interval in small increments so shutdown is responsive.
        auto sleep_ticks = std::max(static_cast<long>(1),
            static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                config_.embedding_interval).count() / 100));
        for (long i = 0; i < sleep_ticks && running_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Subconscious::run_background_embedding() {
    // Kept for API compat; work moved to embed_loop() dedicated thread.
}

size_t Subconscious::flush_embedding_queue() {
    // Queue kept for API compatibility but returns 0.
    std::lock_guard<std::mutex> lock(embedding_queue_mutex_);
    embedding_queue_.clear();
    return 0;
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
    if (last_query == 0) return true;

    auto now = now_ms();
    auto threshold_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        config_.idle_threshold
    ).count();

    return (now - last_query) >= threshold_ms;
}

bool Subconscious::time_for_dream() const {
    auto last_query = stats_.last_query_at.load();
    auto now = now_ms();
    if (last_query != 0 && (now - last_query) < 600000LL) return false;

    auto last_dream = last_dream_triggered_at_.load();
    if (last_dream != 0 && (now - last_dream) < 14400000LL) return false;

    return true;
}

bool Subconscious::time_for_think() const {
    auto last_query = stats_.last_query_at.load();
    auto now = now_ms();
    if (last_query != 0 && (now - last_query) < 300000LL) return false;

    auto last_think = last_think_triggered_at_.load();
    if (last_think != 0 && (now - last_think) < 3600000LL) return false;

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
