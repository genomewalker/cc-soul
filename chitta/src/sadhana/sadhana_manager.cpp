// SadhanaManager: Fully Agentic Autonomous Agent System implementation

#include <chitta/sadhana/sadhana_manager.hpp>
#include <iostream>
#include <sstream>
#include <regex>
#include <sys/wait.h>

namespace chitta {

// ============================================================================
// Text helpers
// ============================================================================

std::string SadhanaManager::strip_ansi(const std::string& text) {
    static const std::regex ansi_regex(R"(\x1b\[[0-9;]*[a-zA-Z])");
    return std::regex_replace(text, ansi_regex, "");
}

std::string SadhanaManager::truncate(const std::string& text, size_t max_chars) {
    if (text.length() <= max_chars) return text;
    return text.substr(0, max_chars) + "... [truncated]";
}

std::string SadhanaManager::escape_sql(const std::string& text) {
    std::string result = text;
    size_t pos = 0;
    while ((pos = result.find("'", pos)) != std::string::npos) {
        result.replace(pos, 1, "''");
        pos += 2;
    }
    return result;
}

// ============================================================================
// Constructor
// ============================================================================

SadhanaManager::SadhanaManager(DuckDBStore& store, SadhanaConfig config)
    : store_(store)
    , config_(std::move(config))
{
    auto active = list_active();
    for (auto& s : active) {
        if (s.state == SadhanaState::Running) {
            RunningState rs;
            rs.next_run_at = now_ms();
            rs.brain = create_brain(s.brain_provider, s.brain_model);
            running_[s.id] = std::move(rs);
        }
    }
    stats_.active_count = running_.size();
    std::cerr << "[sadhana] Loaded " << running_.size() << " active sadhana(s)\n";
}

// ============================================================================
// CRUD operations
// ============================================================================

int64_t SadhanaManager::create(const std::string& goal,
                                const std::string& brain_provider,
                                const std::string& brain_model,
                                int interval_seconds,
                                const std::string& realm,
                                const json& goal_dsl)
{
    Sadhana s;
    s.goal = goal;
    s.goal_dsl = goal_dsl;
    s.state = SadhanaState::Pending;
    s.brain_provider = brain_provider.empty() ? config_.default_brain_provider : brain_provider;
    s.brain_model = brain_model.empty() ? config_.default_brain_model : brain_model;
    s.created_at = now_ms();
    s.updated_at = s.created_at;
    s.interval_seconds = interval_seconds > 0 ? interval_seconds : config_.default_interval_seconds;
    s.realm = realm;

    std::ostringstream sql;
    sql << "INSERT INTO sadhana (id, goal, goal_dsl, state, brain_provider, brain_model, "
        << "created_at, updated_at, iterations, brain_calls, interval_seconds, realm) "
        << "VALUES (nextval('sadhana_seq'), "
        << "'" << escape_sql(goal) << "', "
        << "'" << (goal_dsl.is_null() ? "{}" : escape_sql(goal_dsl.dump())) << "', "
        << "'pending', "
        << "'" << s.brain_provider << "', "
        << "'" << s.brain_model << "', "
        << s.created_at << ", "
        << s.updated_at << ", "
        << "0, 0, "
        << s.interval_seconds << ", "
        << "'" << realm << "') "
        << "RETURNING id";

    auto result = store_.raw_query(sql.str());
    if (!result || result->HasError()) {
        std::cerr << "[sadhana] Create failed: " << (result ? result->GetError() : "null result") << "\n";
        return 0;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) {
        std::cerr << "[sadhana] Create failed: no ID returned\n";
        return 0;
    }

    int64_t id = chunk->GetValue(0, 0).GetValue<int64_t>();
    log_event(id, SadhanaEventType::Created, {{"goal", goal}, {"brain", s.brain_provider}});
    stats_.total_created++;

    std::cerr << "[sadhana] Created sadhana " << id << ": " << goal.substr(0, 60) << "\n";
    return id;
}

bool SadhanaManager::start(int64_t id) {
    auto opt = get(id);
    if (!opt) {
        std::cerr << "[sadhana] Start failed: not found " << id << "\n";
        return false;
    }

    auto& s = *opt;
    if (s.state != SadhanaState::Pending && s.state != SadhanaState::Paused) {
        std::cerr << "[sadhana] Start failed: invalid state " << sadhana_state_to_string(s.state) << "\n";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (static_cast<int>(running_.size()) >= config_.max_concurrent) {
            std::cerr << "[sadhana] Start failed: max concurrent limit reached\n";
            return false;
        }
    }

    std::ostringstream sql;
    sql << "UPDATE sadhana SET state = 'running', updated_at = " << now_ms()
        << " WHERE id = " << id;
    if (!store_.execute_raw(sql.str())) {
        std::cerr << "[sadhana] Start failed: database update failed\n";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        RunningState rs;
        rs.next_run_at = now_ms();
        rs.brain = create_brain(s.brain_provider, s.brain_model);
        running_[id] = std::move(rs);
        stats_.active_count = running_.size();
    }

    log_event(id, SadhanaEventType::Started);
    std::cerr << "[sadhana] Started sadhana " << id << "\n";
    return true;
}

bool SadhanaManager::pause(int64_t id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = running_.find(id);
        if (it == running_.end()) {
            std::cerr << "[sadhana] Pause failed: not running " << id << "\n";
            return false;
        }
        running_.erase(it);
        stats_.active_count = running_.size();
    }

    std::ostringstream sql;
    sql << "UPDATE sadhana SET state = 'paused', updated_at = " << now_ms()
        << " WHERE id = " << id;
    if (!store_.execute_raw(sql.str())) return false;

    log_event(id, SadhanaEventType::Paused);
    std::cerr << "[sadhana] Paused sadhana " << id << "\n";
    return true;
}

bool SadhanaManager::resume(int64_t id) {
    return start(id);
}

bool SadhanaManager::stop(int64_t id, bool success, const std::string& reason) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_.erase(id);
        stats_.active_count = running_.size();
    }

    std::string new_state = success ? "done" : "failed";
    std::ostringstream sql;
    sql << "UPDATE sadhana SET state = '" << new_state << "', updated_at = " << now_ms()
        << " WHERE id = " << id;
    if (!store_.execute_raw(sql.str())) return false;

    auto event_type = success ? SadhanaEventType::Done : SadhanaEventType::Failed;
    log_event(id, event_type, {{"reason", reason}});

    if (success) stats_.total_completed++;
    else stats_.total_failed++;

    std::cerr << "[sadhana] Stopped sadhana " << id << " (" << new_state << ")\n";
    return true;
}

bool SadhanaManager::checkpoint(int64_t id, const std::string& status, const std::string& summary) {
    if (status != "progressed" && status != "achieved" && status != "blocked") {
        std::cerr << "[sadhana] Invalid checkpoint status: " << status << "\n";
        return false;
    }

    json content;
    content["status"] = status;
    content["summary"] = summary;
    log_event(id, SadhanaEventType::Checkpoint, content);

    // Update last_action immediately for live monitoring visibility
    if (!summary.empty()) {
        std::ostringstream sql;
        sql << "UPDATE sadhana SET last_action = '" << escape_sql(summary)
            << "', updated_at = " << now_ms()
            << " WHERE id = " << id;
        store_.execute_raw(sql.str());
    }

    std::cerr << "[sadhana] Checkpoint " << id << ": [" << status << "] "
              << summary.substr(0, 80) << "\n";
    return true;
}

// ============================================================================
// Query operations
// ============================================================================

std::optional<Sadhana> SadhanaManager::get(int64_t id) {
    std::ostringstream sql;
    sql << "SELECT id, goal, goal_dsl, state, brain_provider, brain_model, "
        << "created_at, updated_at, iterations, last_sense, last_action, last_result, "
        << "brain_calls, learned_patterns, interval_seconds, realm "
        << "FROM sadhana WHERE id = " << id;

    auto result = store_.raw_query(sql.str());
    if (!result || result->HasError()) return std::nullopt;

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return std::nullopt;

    Sadhana s;
    s.id = chunk->GetValue(0, 0).GetValue<int64_t>();
    s.goal = chunk->GetValue(1, 0).ToString();
    try { s.goal_dsl = json::parse(chunk->GetValue(2, 0).ToString()); } catch (...) {}
    s.state = string_to_sadhana_state(chunk->GetValue(3, 0).ToString());
    s.brain_provider = chunk->GetValue(4, 0).ToString();
    s.brain_model = chunk->GetValue(5, 0).ToString();
    s.created_at = chunk->GetValue(6, 0).GetValue<int64_t>();
    s.updated_at = chunk->GetValue(7, 0).GetValue<int64_t>();
    s.iterations = chunk->GetValue(8, 0).GetValue<int32_t>();
    try { s.last_sense = json::parse(chunk->GetValue(9, 0).ToString()); } catch (...) {}
    s.last_action = chunk->GetValue(10, 0).ToString();
    try { s.last_result = json::parse(chunk->GetValue(11, 0).ToString()); } catch (...) {}
    s.brain_calls = chunk->GetValue(12, 0).GetValue<int32_t>();
    try { s.learned_patterns = json::parse(chunk->GetValue(13, 0).ToString()); } catch (...) {}
    s.interval_seconds = chunk->GetValue(14, 0).GetValue<int32_t>();
    s.realm = chunk->GetValue(15, 0).ToString();

    return s;
}

std::vector<Sadhana> SadhanaManager::list(const std::string& state_filter,
                                           const std::string& realm,
                                           size_t limit)
{
    std::ostringstream sql;
    sql << "SELECT id, goal, state, brain_provider, brain_model, "
        << "created_at, updated_at, iterations, brain_calls, interval_seconds, realm "
        << "FROM sadhana WHERE 1=1";

    if (!state_filter.empty()) sql << " AND state = '" << state_filter << "'";
    if (!realm.empty()) sql << " AND realm = '" << realm << "'";
    sql << " ORDER BY created_at DESC LIMIT " << limit;

    auto result = store_.raw_query(sql.str());
    std::vector<Sadhana> sadhanas;
    if (!result || result->HasError()) return sadhanas;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); ++i) {
            Sadhana s;
            s.id = chunk->GetValue(0, i).GetValue<int64_t>();
            s.goal = chunk->GetValue(1, i).ToString();
            s.state = string_to_sadhana_state(chunk->GetValue(2, i).ToString());
            s.brain_provider = chunk->GetValue(3, i).ToString();
            s.brain_model = chunk->GetValue(4, i).ToString();
            s.created_at = chunk->GetValue(5, i).GetValue<int64_t>();
            s.updated_at = chunk->GetValue(6, i).GetValue<int64_t>();
            s.iterations = chunk->GetValue(7, i).GetValue<int32_t>();
            s.brain_calls = chunk->GetValue(8, i).GetValue<int32_t>();
            s.interval_seconds = chunk->GetValue(9, i).GetValue<int32_t>();
            s.realm = chunk->GetValue(10, i).ToString();
            sadhanas.push_back(s);
        }
    }

    return sadhanas;
}

std::vector<Sadhana> SadhanaManager::list_active() {
    return list("running");
}

bool SadhanaManager::set_model(int64_t id, const std::string& model) {
    std::ostringstream sql;
    sql << "UPDATE sadhana SET brain_model = '" << model << "', updated_at = " << now_ms()
        << " WHERE id = " << id;
    if (!store_.execute_raw(sql.str())) return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = running_.find(id);
        if (it != running_.end()) it->second.brain->set_model(model);
    }

    log_event(id, SadhanaEventType::ModelChanged, {{"model", model}});
    return true;
}

bool SadhanaManager::set_interval(int64_t id, int interval_seconds) {
    std::ostringstream sql;
    sql << "UPDATE sadhana SET interval_seconds = " << interval_seconds
        << ", updated_at = " << now_ms() << " WHERE id = " << id;
    return store_.execute_raw(sql.str());
}

bool SadhanaManager::set_goal(int64_t id, const std::string& goal) {
    std::ostringstream sql;
    sql << "UPDATE sadhana SET goal = '" << escape_sql(goal) << "', updated_at = " << now_ms()
        << " WHERE id = " << id;
    if (!store_.execute_raw(sql.str())) return false;
    log_event(id, SadhanaEventType::GoalChanged, {{"goal", goal}});
    return true;
}

std::vector<json> SadhanaManager::get_history(int64_t id, size_t limit) {
    std::ostringstream sql;
    sql << "SELECT timestamp, event_type, content, duration_ms "
        << "FROM sadhana_history WHERE sadhana_id = " << id
        << " ORDER BY timestamp DESC LIMIT " << limit;

    auto result = store_.raw_query(sql.str());
    std::vector<json> history;
    if (!result || result->HasError()) return history;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); ++i) {
            json event;
            event["timestamp"] = chunk->GetValue(0, i).GetValue<int64_t>();
            event["event_type"] = chunk->GetValue(1, i).ToString();
            try {
                event["content"] = json::parse(chunk->GetValue(2, i).ToString());
            } catch (...) {
                event["content"] = chunk->GetValue(2, i).ToString();
            }
            event["duration_ms"] = chunk->GetValue(3, i).GetValue<int32_t>();
            history.push_back(event);
        }
    }

    return history;
}

// ============================================================================
// Scheduler tick
// ============================================================================

void SadhanaManager::tick() {
    stats_.last_tick_at = now_ms();

    std::vector<int64_t> to_run;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int64_t now = now_ms();
        for (auto& [id, rs] : running_) {
            if (now >= rs.next_run_at) to_run.push_back(id);
        }
    }

    for (int64_t id : to_run) {
        auto opt = get(id);
        if (!opt) {
            std::lock_guard<std::mutex> lock(mutex_);
            running_.erase(id);
            stats_.active_count = running_.size();
            continue;
        }

        run_cycle(*opt);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = running_.find(id);
            if (it != running_.end()) {
                it->second.next_run_at = now_ms() + (opt->interval_seconds * 1000LL);
            }
        }
    }
}

// ============================================================================
// Context builders
// ============================================================================

std::string SadhanaManager::build_memory_context(const Sadhana& sadhana) {
    try {
        auto hits = store_.bm25_search_memory(sadhana.goal, 10, sadhana.realm, true);
        if (hits.empty()) return "";

        std::ostringstream ctx;
        ctx << "Relevant memories from past experience:\n";
        int shown = 0;
        for (const auto& [mem_id, score] : hits) {
            if (score < 0.05f) continue;
            auto mem = store_.get_memory(mem_id);
            if (mem && !mem->content.empty()) {
                ctx << "- " << mem->content.substr(0, 200) << "\n";
                if (++shown >= 8) break;
            }
        }
        return shown > 0 ? ctx.str() : "";
    } catch (...) {
        return "";
    }
}

std::string SadhanaManager::build_history_context(const Sadhana& sadhana) {
    auto history = get_history(sadhana.id, 10);

    std::vector<std::string> summaries;
    for (const auto& event : history) {
        if (summaries.size() >= 3) break;
        std::string event_type = event.value("event_type", "");
        if (event_type != "cycle" && event_type != "checkpoint") continue;
        const auto& content = event["content"];
        if (!content.is_object()) continue;
        std::string summary = content.value("summary", "");
        std::string status = content.value("status", "");
        if (!summary.empty()) {
            summaries.push_back("[" + status + "] " + summary.substr(0, 150));
        }
    }

    if (summaries.empty()) return "";

    std::ostringstream ctx;
    ctx << "Recent cycle history (newest first):\n";
    for (const auto& s : summaries) ctx << "- " << s << "\n";
    return ctx.str();
}

std::string SadhanaManager::build_system_prompt(const Sadhana& sadhana) const {
    std::ostringstream sys;
    sys << "You are a background autonomous agent (sadhana #" << sadhana.id << ").\n"
        << "You work continuously toward a long-term goal, one cycle at a time.\n"
        << "You have full tool access: bash, file operations, and chitta memory tools.\n\n"
        << "GOAL:\n" << sadhana.goal << "\n\n"
        << "CHITTA MEMORY TOOLS (use these to learn and remember):\n"
        << "  chitta recall --query \"...\"          Search past experience\n"
        << "  chitta remember --content \"...\"       Store important findings\n"
        << "  chitta observe --content \"...\"        Log structured observations\n\n"
        << "COMPLETION PROTOCOL:\n"
        << "Your FINAL response in this cycle MUST be a JSON object on its own line:\n"
        << "  {\"status\": \"progressed\", \"summary\": \"What you did and what you found\"}\n"
        << "Valid statuses:\n"
        << "  progressed - Made progress, run again at next interval\n"
        << "  achieved   - Goal is fully complete, stop the sadhana\n"
        << "  blocked    - Need user input to continue, pause until resumed\n\n"
        << "CONSTRAINTS:\n"
        << "  - Memory realm: " << sadhana.realm << "\n"
        << "  - This is a background process. Do not ask the user questions.\n"
        << "  - Be efficient: don't repeat what previous cycles already accomplished.\n"
        << "  - Prefer incremental progress over attempting everything at once.\n";

    return sys.str();
}

std::string SadhanaManager::build_user_message(const Sadhana& sadhana,
                                                const std::string& memory_ctx,
                                                const std::string& history_ctx) const
{
    std::ostringstream msg;
    msg << "== Cycle #" << (sadhana.iterations + 1) << " ==\n\n";

    if (!memory_ctx.empty()) msg << memory_ctx << "\n";
    if (!history_ctx.empty()) msg << history_ctx << "\n";

    if (!sadhana.last_action.empty()) {
        msg << "Last cycle summary: " << sadhana.last_action.substr(0, 200) << "\n\n";
    }

    msg << "Work toward the goal. End with the JSON status object.\n";
    return msg.str();
}

// ============================================================================
// JSON extraction from agent output
// ============================================================================

json SadhanaManager::extract_last_json(const std::string& text) {
    size_t last_close = text.rfind('}');
    if (last_close == std::string::npos) return json();

    int depth = 0;
    size_t start = std::string::npos;
    for (int i = static_cast<int>(last_close); i >= 0; --i) {
        if (text[i] == '}') depth++;
        else if (text[i] == '{') {
            if (--depth == 0) {
                start = static_cast<size_t>(i);
                break;
            }
        }
    }

    if (start == std::string::npos) return json();

    try {
        auto j = json::parse(text.substr(start, last_close - start + 1));
        // Only return if it looks like a status object
        if (j.contains("status") || j.contains("summary")) return j;
    } catch (...) {}

    return json();
}

// ============================================================================
// Core agentic cycle
// ============================================================================

void SadhanaManager::run_cycle(Sadhana& sadhana) {
    std::cerr << "[sadhana] Cycle #" << (sadhana.iterations + 1)
              << " for sadhana " << sadhana.id << "\n";

    BrainProvider* brain = nullptr;
    int* consecutive_failures = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = running_.find(sadhana.id);
        if (it == running_.end()) return;
        brain = it->second.brain.get();
        consecutive_failures = &it->second.consecutive_failures;
    }
    if (!brain) return;

    auto start_time = std::chrono::steady_clock::now();

    // Build context
    std::string memory_ctx  = build_memory_context(sadhana);
    std::string history_ctx = build_history_context(sadhana);
    std::string system_prompt = build_system_prompt(sadhana);
    std::string user_message  = build_user_message(sadhana, memory_ctx, history_ctx);

    // Configure brain for full agentic operation
    BrainConfig brain_config;
    brain_config.timeout_ms   = config_.max_agent_timeout_ms;
    brain_config.system_prompt = system_prompt;
    brain_config.max_turns    = config_.max_agent_turns;

    // Run the agent
    auto result = brain->think(user_message, brain_config);
    stats_.total_brain_calls++;
    stats_.total_actions++;

    store_.execute_raw("UPDATE sadhana SET brain_calls = brain_calls + 1 WHERE id = " +
                       std::to_string(sadhana.id));

    // Determine status from exit code first, then from last JSON in output
    std::string status  = "progressed";
    std::string summary = "";

    if (result.exit_code == 10)      status = "achieved";
    else if (result.exit_code == 20) status = "blocked";

    // Override/augment from agent's final JSON status message
    std::string clean_output = config_.strip_ansi_codes ? strip_ansi(result.output) : result.output;
    auto last_json = extract_last_json(clean_output);
    if (!last_json.is_null()) {
        std::string json_status = last_json.value("status", "");
        if (!json_status.empty()) status = json_status;
        summary = last_json.value("summary", "");
    }

    // Treat timeout with empty output as a failure for circuit-breaker purposes
    bool cycle_failed = (result.exit_code == -1 && clean_output.empty()) ||
                        (status == "error");

    // Build cycle result for storage
    json cycle_result;
    cycle_result["status"]      = status;
    cycle_result["summary"]     = summary;
    cycle_result["exit_code"]   = result.exit_code;
    cycle_result["duration_ms"] = result.duration_ms;
    // Keep a snippet of the output for diagnostics (last 500 chars, most informative)
    if (!clean_output.empty()) {
        size_t snippet_start = clean_output.size() > 500 ? clean_output.size() - 500 : 0;
        cycle_result["output_tail"] = clean_output.substr(snippet_start);
    }
    if (!result.error.empty()) {
        std::string err_snippet = result.error.size() > 200 ? result.error.substr(0, 200) : result.error;
        cycle_result["error"] = err_snippet;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    // Persist to DB
    std::string escaped_summary = escape_sql(summary.empty() ? status : summary);
    std::string escaped_result  = escape_sql(cycle_result.dump());

    std::ostringstream sql;
    sql << "UPDATE sadhana SET "
        << "last_action = '" << escaped_summary << "', "
        << "last_result = '" << escaped_result  << "', "
        << "iterations = iterations + 1, "
        << "updated_at = " << now_ms()
        << " WHERE id = " << sadhana.id;
    store_.execute_raw(sql.str());

    log_event(sadhana.id, SadhanaEventType::Cycle, cycle_result, static_cast<int>(elapsed));

    // Circuit breaker
    if (cycle_failed && consecutive_failures) {
        (*consecutive_failures)++;
        std::cerr << "[sadhana] Cycle failed, consecutive failures: " << *consecutive_failures << "\n";
        if (*consecutive_failures >= config_.max_consecutive_failures) {
            std::cerr << "[sadhana] Auto-pausing sadhana " << sadhana.id
                      << " after " << *consecutive_failures << " consecutive failures\n";
            pause(sadhana.id);
            log_event(sadhana.id, SadhanaEventType::Paused,
                     {{"reason", "max_consecutive_failures"}, {"failures", *consecutive_failures}});
            return;
        }
    } else if (consecutive_failures) {
        *consecutive_failures = 0;
    }

    // Handle completion
    if (status == "achieved") {
        stop(sadhana.id, true, summary.empty() ? "Goal achieved" : summary);
    } else if (status == "blocked") {
        pause(sadhana.id);
        log_event(sadhana.id, SadhanaEventType::Paused,
                 {{"reason", "blocked_by_agent"}, {"summary", summary}});
    }
}

// ============================================================================
// Persistence helpers
// ============================================================================

bool SadhanaManager::log_event(int64_t sadhana_id, SadhanaEventType type,
                                const json& content, int duration_ms)
{
    std::string content_str = content.is_null() ? "{}" : escape_sql(content.dump());
    std::ostringstream sql;
    sql << "INSERT INTO sadhana_history (id, sadhana_id, timestamp, event_type, content, duration_ms) "
        << "VALUES (nextval('sadhana_history_seq'), "
        << sadhana_id << ", "
        << now_ms() << ", "
        << "'" << sadhana_event_type_to_string(type) << "', "
        << "'" << content_str << "', "
        << duration_ms << ")";

    return store_.execute_raw(sql.str());
}

int64_t SadhanaManager::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

}  // namespace chitta
