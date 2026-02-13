// SadhanaManager: Unified Autonomous Agent System implementation
//
// Manages persistent autonomous agents with sense-think-act loops.

#include <chitta/sadhana/sadhana_manager.hpp>
#include <iostream>
#include <sstream>

namespace chitta {

SadhanaManager::SadhanaManager(DuckDBStore& store, SadhanaConfig config)
    : store_(store)
    , config_(std::move(config))
{
    // Load any running sadhanas from database
    auto active = list_active();
    for (auto& s : active) {
        if (s.state == SadhanaState::Running) {
            RunningState rs;
            rs.next_run_at = now_ms();  // Run immediately
            rs.brain = create_brain(s.brain_provider, s.brain_model);
            running_[s.id] = std::move(rs);
        }
    }
    stats_.active_count = running_.size();
    std::cerr << "[sadhana] Loaded " << running_.size() << " active sadhana(s)\n";
}

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

    // Insert into database
    std::ostringstream sql;
    sql << "INSERT INTO sadhana (id, goal, goal_dsl, state, brain_provider, brain_model, "
        << "created_at, updated_at, iterations, brain_calls, interval_seconds, realm) "
        << "VALUES (nextval('sadhana_seq'), "
        << "'" << goal << "', "
        << "'" << (goal_dsl.is_null() ? "{}" : goal_dsl.dump()) << "', "
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
    s.id = id;

    log_event(id, SadhanaEventType::Created, {{"goal", goal}, {"brain", s.brain_provider}});
    stats_.total_created++;

    std::cerr << "[sadhana] Created sadhana " << id << ": " << goal.substr(0, 50) << "...\n";
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

    // Check concurrent limit
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (static_cast<int>(running_.size()) >= config_.max_concurrent) {
            std::cerr << "[sadhana] Start failed: max concurrent limit reached\n";
            return false;
        }
    }

    // Update state in database
    std::ostringstream sql;
    sql << "UPDATE sadhana SET state = 'running', updated_at = " << now_ms()
        << " WHERE id = " << id;
    if (!store_.execute_raw(sql.str())) {
        std::cerr << "[sadhana] Start failed: database update failed\n";
        return false;
    }

    // Add to running set
    {
        std::lock_guard<std::mutex> lock(mutex_);
        RunningState rs;
        rs.next_run_at = now_ms();  // Run immediately
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
    if (!store_.execute_raw(sql.str())) {
        return false;
    }

    log_event(id, SadhanaEventType::Paused);
    std::cerr << "[sadhana] Paused sadhana " << id << "\n";
    return true;
}

bool SadhanaManager::resume(int64_t id) {
    return start(id);  // Same logic as start
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
    if (!store_.execute_raw(sql.str())) {
        return false;
    }

    auto event_type = success ? SadhanaEventType::Done : SadhanaEventType::Failed;
    log_event(id, event_type, {{"reason", reason}});

    if (success) {
        stats_.total_completed++;
    } else {
        stats_.total_failed++;
    }

    std::cerr << "[sadhana] Stopped sadhana " << id << " (" << new_state << ")\n";
    return true;
}

std::optional<Sadhana> SadhanaManager::get(int64_t id) {
    std::ostringstream sql;
    sql << "SELECT id, goal, goal_dsl, state, brain_provider, brain_model, "
        << "created_at, updated_at, iterations, last_sense, last_action, last_result, "
        << "brain_calls, learned_patterns, interval_seconds, realm "
        << "FROM sadhana WHERE id = " << id;

    auto result = store_.raw_query(sql.str());
    if (!result || result->HasError()) {
        return std::nullopt;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) {
        return std::nullopt;
    }

    Sadhana s;
    s.id = chunk->GetValue(0, 0).GetValue<int64_t>();
    s.goal = chunk->GetValue(1, 0).ToString();
    try {
        s.goal_dsl = json::parse(chunk->GetValue(2, 0).ToString());
    } catch (...) {
        s.goal_dsl = json();
    }
    s.state = string_to_sadhana_state(chunk->GetValue(3, 0).ToString());
    s.brain_provider = chunk->GetValue(4, 0).ToString();
    s.brain_model = chunk->GetValue(5, 0).ToString();
    s.created_at = chunk->GetValue(6, 0).GetValue<int64_t>();
    s.updated_at = chunk->GetValue(7, 0).GetValue<int64_t>();
    s.iterations = chunk->GetValue(8, 0).GetValue<int32_t>();
    try {
        s.last_sense = json::parse(chunk->GetValue(9, 0).ToString());
    } catch (...) {
        s.last_sense = json();
    }
    s.last_action = chunk->GetValue(10, 0).ToString();
    try {
        s.last_result = json::parse(chunk->GetValue(11, 0).ToString());
    } catch (...) {
        s.last_result = json();
    }
    s.brain_calls = chunk->GetValue(12, 0).GetValue<int32_t>();
    try {
        s.learned_patterns = json::parse(chunk->GetValue(13, 0).ToString());
    } catch (...) {
        s.learned_patterns = json();
    }
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

    if (!state_filter.empty()) {
        sql << " AND state = '" << state_filter << "'";
    }
    if (!realm.empty()) {
        sql << " AND realm = '" << realm << "'";
    }
    sql << " ORDER BY created_at DESC LIMIT " << limit;

    auto result = store_.raw_query(sql.str());
    std::vector<Sadhana> sadhanas;
    if (!result || result->HasError()) {
        return sadhanas;
    }

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
    if (!store_.execute_raw(sql.str())) {
        return false;
    }

    // Update brain if running
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = running_.find(id);
        if (it != running_.end()) {
            it->second.brain->set_model(model);
        }
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
    // Escape single quotes in goal
    std::string escaped_goal = goal;
    size_t pos = 0;
    while ((pos = escaped_goal.find("'", pos)) != std::string::npos) {
        escaped_goal.replace(pos, 1, "''");
        pos += 2;
    }

    std::ostringstream sql;
    sql << "UPDATE sadhana SET goal = '" << escaped_goal << "', updated_at = " << now_ms()
        << " WHERE id = " << id;
    if (!store_.execute_raw(sql.str())) {
        return false;
    }

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
    if (!result || result->HasError()) {
        return history;
    }

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

void SadhanaManager::tick() {
    stats_.last_tick_at = now_ms();

    std::vector<int64_t> to_run;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        int64_t now = now_ms();

        for (auto& [id, rs] : running_) {
            if (now >= rs.next_run_at) {
                to_run.push_back(id);
            }
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

        auto& sadhana = *opt;

        // Run the sense-think-act cycle
        run_cycle(sadhana);

        // Update next run time
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = running_.find(id);
            if (it != running_.end()) {
                it->second.next_run_at = now_ms() + (sadhana.interval_seconds * 1000);
            }
        }
    }
}

void SadhanaManager::run_cycle(Sadhana& sadhana) {
    std::cerr << "[sadhana] Running cycle for " << sadhana.id << "\n";

    BrainProvider* brain = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = running_.find(sadhana.id);
        if (it != running_.end()) {
            brain = it->second.brain.get();
        }
    }

    if (!brain) {
        std::cerr << "[sadhana] No brain for " << sadhana.id << "\n";
        return;
    }

    // SENSE
    auto observation = sense(sadhana, *brain);

    // THINK
    auto decision = think(sadhana, *brain, observation);

    // ACT (if action needed)
    json result;
    if (decision.contains("action") && !decision["action"].is_null()) {
        result = act(sadhana, decision);
    }

    // LEARN
    if (config_.enable_learning) {
        learn(sadhana, observation, decision, result);
    }

    // Update iteration count
    std::ostringstream sql;
    sql << "UPDATE sadhana SET iterations = iterations + 1, updated_at = " << now_ms()
        << " WHERE id = " << sadhana.id;
    store_.execute_raw(sql.str());

    // Check if goal achieved
    if (decision.contains("goal_achieved") && decision["goal_achieved"].get<bool>()) {
        stop(sadhana.id, true, "Goal achieved");
    }
}

json SadhanaManager::sense(Sadhana& sadhana, BrainProvider& brain) {
    auto start = std::chrono::steady_clock::now();

    // Build sense prompt
    std::ostringstream prompt;
    prompt << "You are an autonomous agent working toward this goal:\n"
           << sadhana.goal << "\n\n";

    if (!sadhana.last_sense.is_null()) {
        prompt << "Previous observation:\n" << sadhana.last_sense.dump(2) << "\n\n";
    }
    if (!sadhana.last_action.empty()) {
        prompt << "Last action taken: " << sadhana.last_action << "\n\n";
    }
    if (!sadhana.last_result.is_null()) {
        prompt << "Result of last action:\n" << sadhana.last_result.dump(2) << "\n\n";
    }

    prompt << "Generate a shell command to observe the current state relevant to your goal.\n"
           << "Output ONLY the command, nothing else. For example:\n"
           << "ls -la /path/to/watch\n"
           << "cat /path/to/file.txt\n"
           << "git status\n";

    // Call brain
    BrainConfig config;
    config.timeout_ms = config_.max_brain_timeout_ms;
    auto result = brain.think(prompt.str(), config);
    stats_.total_brain_calls++;

    // Update brain_calls in database
    store_.execute_raw("UPDATE sadhana SET brain_calls = brain_calls + 1 WHERE id = " +
                       std::to_string(sadhana.id));

    json observation;
    if (result.success && !result.output.empty()) {
        // Execute the observation command
        std::string cmd = result.output;
        // Trim whitespace
        while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == ' ')) {
            cmd.pop_back();
        }

        // Execute command
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            char buffer[4096];
            std::string output;
            while (fgets(buffer, sizeof(buffer), pipe)) {
                output += buffer;
            }
            int exit_code = pclose(pipe);

            observation["command"] = cmd;
            observation["output"] = output;
            observation["exit_code"] = exit_code;
            observation["success"] = (exit_code == 0);
        } else {
            observation["command"] = cmd;
            observation["error"] = "Failed to execute command";
            observation["success"] = false;
        }
    } else {
        observation["error"] = result.error.empty() ? "Brain failed to generate command" : result.error;
        observation["success"] = false;
    }

    // Save observation
    std::ostringstream sql;
    sql << "UPDATE sadhana SET last_sense = '" << observation.dump() << "', updated_at = " << now_ms()
        << " WHERE id = " << sadhana.id;
    store_.execute_raw(sql.str());

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    log_event(sadhana.id, SadhanaEventType::Sense, observation, static_cast<int>(elapsed));

    sadhana.last_sense = observation;
    return observation;
}

json SadhanaManager::think(Sadhana& sadhana, BrainProvider& brain, const json& observation) {
    auto start = std::chrono::steady_clock::now();

    // Query memory for relevant context using BM25 text search
    std::string memory_context;
    try {
        // Build query from goal and observation
        std::string query = sadhana.goal;
        if (observation.contains("error")) {
            query += " " + observation["error"].get<std::string>();
        }
        if (observation.contains("output")) {
            std::string out = observation["output"].get<std::string>();
            if (out.length() > 200) out = out.substr(0, 200);
            query += " " + out;
        }

        // Use BM25 text search (no embedding needed)
        auto hits = store_.bm25_search_memory(query, 5, sadhana.realm, true);
        if (!hits.empty()) {
            std::ostringstream mem_str;
            mem_str << "Relevant memories from past experience:\n";
            for (const auto& [mem_id, score] : hits) {
                auto mem = store_.get_memory(mem_id);
                if (mem) {
                    mem_str << "- " << mem->content << "\n";
                }
            }
            memory_context = mem_str.str();
        }
    } catch (...) {
        // Ignore memory errors, continue without context
    }

    // Build think prompt
    std::ostringstream prompt;
    prompt << "You are an autonomous agent working toward this goal:\n"
           << sadhana.goal << "\n\n";

    if (!memory_context.empty()) {
        prompt << memory_context << "\n";
    }

    prompt << "Current observation:\n" << observation.dump(2) << "\n\n";

    if (sadhana.iterations > 0) {
        prompt << "This is iteration " << (sadhana.iterations + 1) << " of your work.\n\n";
    }

    prompt << "Decide what to do next. Output valid JSON with these fields:\n"
           << "{\n"
           << "  \"analysis\": \"Your analysis of the current state\",\n"
           << "  \"action\": \"Shell command to execute, or null if no action needed\",\n"
           << "  \"reasoning\": \"Why you chose this action\",\n"
           << "  \"goal_achieved\": false,\n"
           << "  \"progress\": 0.5\n"
           << "}\n"
           << "Set goal_achieved to true if the goal is complete.\n"
           << "Output ONLY the JSON, nothing else.";

    // Call brain
    BrainConfig config;
    config.timeout_ms = config_.max_brain_timeout_ms;
    auto result = brain.think(prompt.str(), config);
    stats_.total_brain_calls++;

    store_.execute_raw("UPDATE sadhana SET brain_calls = brain_calls + 1 WHERE id = " +
                       std::to_string(sadhana.id));

    json decision;
    if (result.success && !result.output.empty()) {
        try {
            // Find JSON in output (may have markdown wrapper)
            std::string output = result.output;
            size_t start_pos = output.find('{');
            size_t end_pos = output.rfind('}');
            if (start_pos != std::string::npos && end_pos != std::string::npos) {
                decision = json::parse(output.substr(start_pos, end_pos - start_pos + 1));
            } else {
                decision["error"] = "No JSON found in brain output";
                decision["raw_output"] = output;
            }
        } catch (const std::exception& e) {
            decision["error"] = std::string("JSON parse error: ") + e.what();
            decision["raw_output"] = result.output;
        }
    } else {
        decision["error"] = result.error.empty() ? "Brain failed" : result.error;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    log_event(sadhana.id, SadhanaEventType::Think, decision, static_cast<int>(elapsed));

    return decision;
}

json SadhanaManager::act(Sadhana& sadhana, const json& decision) {
    auto start = std::chrono::steady_clock::now();

    json result;
    std::string action = decision.value("action", "");
    if (action.empty()) {
        result["skipped"] = true;
        result["reason"] = "No action specified";
        return result;
    }

    // Execute the action
    FILE* pipe = popen(action.c_str(), "r");
    if (pipe) {
        char buffer[4096];
        std::string output;
        while (fgets(buffer, sizeof(buffer), pipe)) {
            output += buffer;
        }
        int exit_code = pclose(pipe);

        result["command"] = action;
        result["output"] = output;
        result["exit_code"] = exit_code;
        result["success"] = (exit_code == 0);
    } else {
        result["command"] = action;
        result["error"] = "Failed to execute command";
        result["success"] = false;
    }

    // Save result
    std::ostringstream sql;
    sql << "UPDATE sadhana SET last_action = '" << action << "', "
        << "last_result = '" << result.dump() << "', updated_at = " << now_ms()
        << " WHERE id = " << sadhana.id;
    store_.execute_raw(sql.str());

    sadhana.last_action = action;
    sadhana.last_result = result;
    stats_.total_actions++;

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    log_event(sadhana.id, SadhanaEventType::Act, result, static_cast<int>(elapsed));

    return result;
}

void SadhanaManager::learn(Sadhana& sadhana, const json& observation,
                            const json& decision, const json& result)
{
    // Extract learned patterns and store in memory
    bool success = result.contains("success") && result["success"].get<bool>();

    json pattern;
    pattern["action"] = decision.value("action", "");
    pattern["outcome"] = success ? "success" : "failure";
    pattern["context"] = sadhana.goal.substr(0, 100);

    if (success) {
        // Successful action - store as positive pattern
        pattern["observation"] = observation.value("command", "");
        std::string content = "[sadhana] Success: " + decision.value("action", "") +
                              " worked for goal: " + sadhana.goal.substr(0, 80);
        store_.remember(content, "episode", {}, 0.7f, 0.05f, sadhana.realm,
                        RealmVisibility::Private, {});
    } else {
        // Failed action - store as failure pattern so we don't repeat mistakes
        // Use Global visibility so all sadhanas can learn from failures
        std::string error_info;
        if (result.contains("output")) {
            error_info = result["output"].get<std::string>();
            if (error_info.length() > 200) error_info = error_info.substr(0, 200);
        } else if (result.contains("error")) {
            error_info = result["error"].get<std::string>();
        }
        pattern["error"] = error_info;

        std::string content = "[sadhana] Failure: " + decision.value("action", "") +
                              " failed with: " + error_info +
                              " | Goal: " + sadhana.goal.substr(0, 60);
        // Store in project realm but with Global visibility for cross-project learning
        store_.remember(content, "episode", {"failure", "sadhana"}, 0.8f, 0.03f, sadhana.realm,
                        RealmVisibility::Global, {});
    }

    log_event(sadhana.id, SadhanaEventType::Learn, pattern);
}

bool SadhanaManager::save_sadhana(const Sadhana& s) {
    std::ostringstream sql;
    sql << "UPDATE sadhana SET "
        << "goal = '" << s.goal << "', "
        << "state = '" << sadhana_state_to_string(s.state) << "', "
        << "brain_provider = '" << s.brain_provider << "', "
        << "brain_model = '" << s.brain_model << "', "
        << "updated_at = " << now_ms() << ", "
        << "iterations = " << s.iterations << ", "
        << "brain_calls = " << s.brain_calls << ", "
        << "interval_seconds = " << s.interval_seconds
        << " WHERE id = " << s.id;

    return store_.execute_raw(sql.str());
}

bool SadhanaManager::log_event(int64_t sadhana_id, SadhanaEventType type,
                                const json& content, int duration_ms)
{
    std::ostringstream sql;
    sql << "INSERT INTO sadhana_history (id, sadhana_id, timestamp, event_type, content, duration_ms) "
        << "VALUES (nextval('sadhana_history_seq'), "
        << sadhana_id << ", "
        << now_ms() << ", "
        << "'" << sadhana_event_type_to_string(type) << "', "
        << "'" << (content.is_null() ? "{}" : content.dump()) << "', "
        << duration_ms << ")";

    return store_.execute_raw(sql.str());
}

int64_t SadhanaManager::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

}  // namespace chitta
