// Included into DuckDBHandler class body — not a standalone header

    DuckDBToolResult tool_sadhana_start(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        std::string goal = params.value("goal", "");
        if (goal.empty()) {
            return DuckDBToolResult::error("Goal is required");
        }

        std::string provider = params.value("brain_provider", "");
        std::string model = params.value("brain_model", "");
        int interval = params.value("interval_seconds", 0);
        int max_turns = params.value("max_turns", 0);
        std::string realm = params.value("realm", "brahman");
        json goal_dsl;
        if (params.contains("goal_dsl") && params["goal_dsl"].is_object()) {
            goal_dsl = params["goal_dsl"];
        }

        int64_t id = sadhana_manager_->create(goal, provider, model, interval, realm, goal_dsl, max_turns);
        if (id == 0) {
            return DuckDBToolResult::error("Failed to create sadhana");
        }

        // Auto-start the sadhana
        if (!sadhana_manager_->start(id)) {
            return DuckDBToolResult::error("Created sadhana " + std::to_string(id) + " but failed to start");
        }

        json result;
        result["id"] = id;
        result["state"] = "running";
        result["goal"] = goal.substr(0, 100);

        return DuckDBToolResult::ok("Started sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_pause(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        if (!sadhana_manager_->pause(id)) {
            return DuckDBToolResult::error("Failed to pause sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["state"] = "paused";

        return DuckDBToolResult::ok("Paused sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_resume(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        if (!sadhana_manager_->resume(id)) {
            return DuckDBToolResult::error("Failed to resume sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["state"] = "running";

        return DuckDBToolResult::ok("Resumed sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_stop(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        bool success = params.value("success", true);
        std::string reason = params.value("reason", "");

        if (!sadhana_manager_->stop(id, success, reason)) {
            return DuckDBToolResult::error("Failed to stop sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["state"] = success ? "done" : "failed";
        result["reason"] = reason;

        return DuckDBToolResult::ok("Stopped sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_status(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        auto opt = sadhana_manager_->get(id);
        if (!opt) {
            return DuckDBToolResult::error("Sadhana " + std::to_string(id) + " not found");
        }

        size_t history_limit = params.value("history_limit", 20);
        auto history = sadhana_manager_->get_history(id, history_limit);

        json result;
        result["id"] = opt->id;
        result["goal"] = opt->goal;
        result["state"] = sadhana_state_to_string(opt->state);
        result["brain_provider"] = opt->brain_provider;
        result["brain_model"] = opt->brain_model;
        result["iterations"] = opt->iterations;
        result["brain_calls"] = opt->brain_calls;
        result["cost_usd"] = opt->cost_usd;
        result["interval_seconds"] = opt->interval_seconds;
        result["max_turns"] = opt->max_turns;
        result["realm"] = opt->realm;
        result["created_at"] = opt->created_at;
        result["updated_at"] = opt->updated_at;
        result["last_sense"] = opt->last_sense;
        result["last_action"] = opt->last_action;
        result["last_result"] = opt->last_result;
        result["history"] = history;

        std::ostringstream msg;
        msg << "Sadhana " << id << " [" << sadhana_state_to_string(opt->state) << "] "
            << opt->iterations << " iterations";

        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_sadhana_list(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        std::string state = params.value("state", "");
        std::string realm = params.value("realm", "");
        size_t limit = params.value("limit", 50);

        auto sadhanas = sadhana_manager_->list(state, realm, limit);

        json result;
        result["sadhanas"] = json::array();
        result["count"] = sadhanas.size();

        for (const auto& s : sadhanas) {
            json item;
            item["id"] = s.id;
            item["goal"] = s.goal.substr(0, 100);
            item["state"] = sadhana_state_to_string(s.state);
            item["brain_model"] = s.brain_model;
            item["iterations"] = s.iterations;
            item["interval_seconds"] = s.interval_seconds;
            item["realm"] = s.realm;
            item["created_at"] = s.created_at;
            result["sadhanas"].push_back(item);
        }

        std::ostringstream msg;
        msg << "Found " << sadhanas.size() << " sadhana(s)";

        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_sadhana_set_model(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        std::string model = params.value("model", "");
        if (model.empty()) {
            return DuckDBToolResult::error("Model is required");
        }

        if (!sadhana_manager_->set_model(id, model)) {
            return DuckDBToolResult::error("Failed to set model for sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["model"] = model;

        return DuckDBToolResult::ok("Set model to " + model + " for sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_set_goal(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        std::string goal = params.value("goal", "");
        if (goal.empty()) {
            return DuckDBToolResult::error("Goal is required");
        }

        if (!sadhana_manager_->set_goal(id, goal)) {
            return DuckDBToolResult::error("Failed to set goal for sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["goal"] = goal;

        return DuckDBToolResult::ok("Updated goal for sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_set_interval(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        int interval = params.value("interval", 0);
        if (interval <= 0) {
            return DuckDBToolResult::error("Interval must be positive");
        }

        if (!sadhana_manager_->set_interval(id, interval)) {
            return DuckDBToolResult::error("Failed to set interval for sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["interval"] = interval;

        return DuckDBToolResult::ok("Set interval to " + std::to_string(interval) + "s for sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_set_max_turns(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        int max_turns = params.value("max_turns", -1);
        if (max_turns < 0) {
            return DuckDBToolResult::error("max_turns must be >= 0 (0 = use global default)");
        }

        if (!sadhana_manager_->set_max_turns(id, max_turns)) {
            return DuckDBToolResult::error("Failed to set max_turns for sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["max_turns"] = max_turns;

        std::string msg = max_turns == 0
            ? "Reset max_turns to global default for sadhana " + std::to_string(id)
            : "Set max_turns to " + std::to_string(max_turns) + " for sadhana " + std::to_string(id);
        return DuckDBToolResult::ok(msg, result);
    }

    DuckDBToolResult tool_sadhana_checkpoint(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        std::string status = params.value("status", "progressed");
        std::string summary = params.value("summary", "");

        if (summary.empty()) {
            return DuckDBToolResult::error("Summary is required");
        }

        if (!sadhana_manager_->checkpoint(id, status, summary)) {
            return DuckDBToolResult::error("Checkpoint failed for sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["status"] = status;
        result["summary"] = summary;

        return DuckDBToolResult::ok("Checkpoint [" + status + "] for sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_dream_cancel(const json& params) {
        if (!params.contains("id")) {
            return DuckDBToolResult::error("id is required");
        }
        int64_t dream_id = params["id"].is_string()
            ? std::stoll(params["id"].get<std::string>())
            : params["id"].get<int64_t>();

        // Get sadhana_id for this dream
        auto res = mind_->store().execute_sql_query(
            "SELECT sadhana_id, status FROM dream WHERE id = " + std::to_string(dream_id));
        if (!res.success || res.rows.empty()) {
            return DuckDBToolResult::error("Dream " + std::to_string(dream_id) + " not found");
        }
        std::string status = res.rows[0][1];
        if (status == "cancelled" || status == "woke") {
            return DuckDBToolResult::ok("Dream " + std::to_string(dream_id) + " already " + status, {});
        }

        int64_t sadhana_id = 0;
        if (res.rows[0][0] != "NULL" && !res.rows[0][0].empty()) {
            try { sadhana_id = std::stoll(res.rows[0][0]); } catch (...) {}
        }

        // Stop the underlying sadhana
        if (sadhana_manager_ && sadhana_id > 0) {
            sadhana_manager_->stop(sadhana_id);
        }

        // Mark dream as cancelled
        auto now_val = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        mind_->store().execute_raw(
            "UPDATE dream SET status = 'cancelled', ended_at = " + std::to_string(now_val) +
            " WHERE id = " + std::to_string(dream_id));

        return DuckDBToolResult::ok(
            "Cancelled dream #" + std::to_string(dream_id) +
            (sadhana_id > 0 ? " (stopped sadhana #" + std::to_string(sadhana_id) + ")" : ""),
            {{"dream_id", dream_id}, {"sadhana_id", sadhana_id}, {"status", "cancelled"}});
    }

    DuckDBToolResult tool_dream_force_woke(const json& params) {
        if (!params.contains("id")) {
            return DuckDBToolResult::error("id is required");
        }
        int64_t dream_id = params["id"].is_string()
            ? std::stoll(params["id"].get<std::string>())
            : params["id"].get<int64_t>();

        auto res = mind_->store().execute_sql_query(
            "SELECT sadhana_id, status FROM dream WHERE id = " + std::to_string(dream_id));
        if (!res.success || res.rows.empty()) {
            return DuckDBToolResult::error("Dream " + std::to_string(dream_id) + " not found");
        }
        std::string status = res.rows[0][1];
        if (status == "woke") {
            return DuckDBToolResult::ok("Dream " + std::to_string(dream_id) + " already woke", {});
        }

        int64_t sadhana_id = 0;
        if (res.rows[0][0] != "NULL" && !res.rows[0][0].empty()) {
            try { sadhana_id = std::stoll(res.rows[0][0]); } catch (...) {}
        }
        if (sadhana_manager_ && sadhana_id > 0) {
            sadhana_manager_->stop(sadhana_id);
        }

        auto now_val = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        mind_->store().execute_raw(
            "UPDATE dream SET status = 'woke', findings = '[force-woke]', ended_at = " +
            std::to_string(now_val) + " WHERE id = " + std::to_string(dream_id));

        return DuckDBToolResult::ok(
            "Force-woke dream #" + std::to_string(dream_id) +
            (sadhana_id > 0 ? " (stopped sadhana #" + std::to_string(sadhana_id) + ")" : ""),
            {{"dream_id", dream_id}, {"sadhana_id", sadhana_id}, {"status", "woke"}});
    }

    DuckDBToolResult tool_dream_start(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }
        if (subconscious_) subconscious_->notify_query();

        std::string topic = params.value("topic", "");
        if (topic.empty()) {
            return DuckDBToolResult::error("Topic is required");
        }
        std::string realm = params.value("realm", "brahman");
        std::string publish_path = params.value("publish_path", "");
        std::string brain_provider = params.value("brain_provider", "claude");
        std::string brain_model   = params.value("brain_model", "sonnet");
        int max_concurrent = params.value("max_concurrent", 2);

        auto now_val = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Rate limit: check active dream count
        auto active_res = mind_->store().execute_sql_query(
            "SELECT COUNT(*) FROM dream WHERE status = 'dreaming'");
        if (active_res.success && !active_res.rows.empty() && !active_res.rows[0].empty()) {
            int active = 0;
            try { active = std::stoi(active_res.rows[0][0]); } catch (...) {}
            if (active >= max_concurrent) {
                return DuckDBToolResult::error(
                    "Dream rate limit: " + std::to_string(active) +
                    " dreams already active (max " + std::to_string(max_concurrent) + "). "
                    "Wait for a dream to finish or use dream_cancel to clear a stuck one.");
            }
        }

        // Insert dream record using write connection (execute_raw = write_execute)
        if (!mind_->store().execute_raw(
            "INSERT INTO dream (id, topic, status, sadhana_id, started_at, realm) "
            "VALUES (nextval('dream_seq'), '" + esc_sql(topic) + "', 'dreaming', 0, " +
            std::to_string(now_val) + ", '" + esc_sql(realm) + "')")) {
            return DuckDBToolResult::error("Failed to create dream record");
        }

        // Retrieve the newly inserted dream ID
        auto id_res = mind_->store().execute_sql_query(
            "SELECT id FROM dream WHERE topic = '" + esc_sql(topic) +
            "' AND started_at = " + std::to_string(now_val));
        if (!id_res.success || id_res.rows.empty()) {
            return DuckDBToolResult::error("Failed to retrieve dream ID after insert");
        }
        int64_t dream_id = std::stoll(id_res.rows[0][0]);

        // Create sadhana with dream goal_dsl (single-cycle: agent returns "achieved")
        json goal_dsl = {{"kind", "dream"}, {"topic", topic}, {"dream_id", dream_id}};
        if (!publish_path.empty()) {
            goal_dsl["publish_path"] = publish_path;
        }
        std::string goal = "[dream] Explore: " + topic;

        int64_t sadhana_id = sadhana_manager_->create(goal, brain_provider, brain_model, 0, realm, goal_dsl);
        if (sadhana_id == 0) {
            return DuckDBToolResult::error("Failed to create dream sadhana");
        }

        // Link sadhana to dream record before starting (so it's always set even if start fails)
        mind_->store().execute_raw(
            "UPDATE dream SET sadhana_id = " + std::to_string(sadhana_id) +
            " WHERE id = " + std::to_string(dream_id));

        if (!sadhana_manager_->start(sadhana_id)) {
            // Roll back dream status so it doesn't clog the rate limit
            int64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            mind_->store().execute_raw(
                "UPDATE dream SET status = 'cancelled', ended_at = " + std::to_string(ts) +
                " WHERE id = " + std::to_string(dream_id));
            return DuckDBToolResult::error(
                "Created dream sadhana " + std::to_string(sadhana_id) + " but failed to start");
        }

        json result;
        result["dream_id"]   = dream_id;
        result["sadhana_id"] = sadhana_id;
        result["topic"]      = topic;
        result["status"]     = "dreaming";

        std::ostringstream msg;
        msg << "Dream started: " << topic
            << " (dream #" << dream_id << ", sadhana #" << sadhana_id << ")";
        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_dream_wander(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }
        if (subconscious_) subconscious_->notify_query();

        std::string realm = params.value("realm", "brahman");
        std::string publish_path = params.value("publish_path", "");
        if (publish_path.empty()) {
            const char* env_path = std::getenv("CHITTA_DREAM_PUBLISH_PATH");
            if (env_path) publish_path = env_path;
        }
        std::string topic;

        // Exclude internal/code topics that can't be web-searched meaningfully
        const std::string exclude_code =
            " AND content NOT LIKE '[code]%' "
            " AND content NOT LIKE '[training]%' "
            " AND content NOT LIKE '[symbol]%' "
            " AND content NOT LIKE '[distilled]%' "
            " AND content NOT LIKE '[locomo%' "
            " AND tags NOT LIKE '%symbol%' ";

        // Priority 1: memories tagged as gaps/unresolved
        auto gap_res = mind_->store().execute_sql_query(
            "SELECT content FROM memory "
            "WHERE tags LIKE '%gap%' AND tags LIKE '%unresolved%' " +
            exclude_code +
            "ORDER BY RANDOM() LIMIT 1");
        if (gap_res.success && !gap_res.rows.empty()) {
            topic = gap_res.rows[0][0];
            if (topic.size() > 100) topic = topic.substr(0, 100);
        }

        // Priority 2: low-confidence memories (uncertain knowledge)
        if (topic.empty()) {
            auto low_res = mind_->store().execute_sql_query(
                "SELECT content FROM memory "
                "WHERE confidence < 0.5 AND confidence > 0.0 " +
                exclude_code +
                "ORDER BY RANDOM() LIMIT 1");
            if (low_res.success && !low_res.rows.empty()) {
                topic = low_res.rows[0][0];
                if (topic.size() > 100) topic = topic.substr(0, 100);
            }
        }

        // Priority 3: curiosity seeds (hardcoded topics of enduring interest)
        if (topic.empty()) {
            static const std::vector<std::string> seeds = {
                "consciousness and the hard problem of subjective experience",
                "emergent complexity in distributed systems",
                "the nature of memory and forgetting in biological brains",
                "Vedantic philosophy and modern neuroscience",
                "language models and the limits of statistical learning",
                "self-organization in nature: from cells to civilizations",
                "the history of symbolic AI versus connectionism",
                "epistemic humility in scientific discovery",
                "attention mechanisms and the binding problem",
                "entropy, information, and the arrow of time"
            };
            auto now_val = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            topic = seeds[static_cast<size_t>(now_val) % seeds.size()];
        }

        json start_params = {
            {"topic", topic},
            {"realm", realm},
            // Oracle pattern: use opencode (cheap) for autonomous dreaming;
            // Claude is reserved for interactive sessions.
            {"brain_provider", "opencode"},
            {"brain_model", "gpt-4o"},
            // Hard cap: never more than 2 concurrent wandering dreams
            {"max_concurrent", 2}
        };
        if (!publish_path.empty()) start_params["publish_path"] = publish_path;
        return tool_dream_start(start_params);
    }

    DuckDBToolResult tool_dream_list(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        int limit = params.value("limit", 10);
        if (limit <= 0 || limit > 100) limit = 10;
        std::string realm = params.value("realm", "");

        // Lazily finalize any completed dreams (sets status, ended_at, findings, memories_created)
        finalize_completed_dreams();

        std::string where = realm.empty() ? "" : "WHERE d.realm = '" + esc_sql(realm) + "' ";

        auto res = mind_->store().execute_sql_query(
            "SELECT d.id, d.topic, d.status, d.findings, d.memories_created, "
            "       d.started_at, d.ended_at, d.sadhana_id, d.realm, "
            "       s.iterations, s.last_action "
            "FROM dream d "
            "LEFT JOIN sadhana s ON d.sadhana_id = s.id " +
            where +
            "ORDER BY d.started_at DESC LIMIT " + std::to_string(limit));

        json dreams = json::array();
        if (res.success) {
            for (const auto& row : res.rows) {
                if (row.size() < 9) continue;
                json d;
                d["id"]               = std::stoll(row[0]);
                d["topic"]            = row[1];
                d["status"]           = row[2];
                d["findings"]         = (row[3] == "NULL") ? "" : row[3];
                d["memories_created"] = (row[4] == "NULL") ? 0 : std::stoi(row[4]);
                d["started_at"]       = std::stoll(row[5]);
                d["ended_at"]         = std::stoll(row[6]);
                d["sadhana_id"]       = std::stoll(row[7]);
                d["realm"]            = row[8];
                if (row.size() >= 10) d["iterations"]  = (row[9]  == "NULL") ? 0 : std::stoi(row[9]);
                if (row.size() >= 11) d["last_action"]  = (row[10] == "NULL") ? "" : row[10];
                dreams.push_back(d);
            }
        }

        json result;
        result["dreams"] = dreams;
        result["count"]  = dreams.size();

        std::ostringstream msg;
        msg << "Found " << dreams.size() << " dream(s)";
        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_dream_status(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        if (!params.contains("id")) {
            return DuckDBToolResult::error("id is required");
        }
        int64_t dream_id = params["id"].is_string()
            ? std::stoll(params["id"].get<std::string>())
            : params["id"].get<int64_t>();

        // Lazily finalize if sadhana completed (sets status, ended_at, findings, memories_created)
        finalize_completed_dreams(dream_id);

        auto res = mind_->store().execute_sql_query(
            "SELECT d.id, d.topic, d.status, d.findings, d.memories_created, "
            "       d.started_at, d.ended_at, d.sadhana_id, d.realm, "
            "       s.id, s.state, s.goal, s.iterations, s.brain_calls, s.last_action, "
            "       s.brain_provider, s.brain_model "
            "FROM dream d "
            "LEFT JOIN sadhana s ON d.sadhana_id = s.id "
            "WHERE d.id = " + std::to_string(dream_id));

        if (!res.success || res.rows.empty()) {
            return DuckDBToolResult::error("Dream #" + std::to_string(dream_id) + " not found");
        }

        const auto& row = res.rows[0];
        json dream;
        dream["id"]               = std::stoll(row[0]);
        dream["topic"]            = row[1];
        dream["status"]           = row[2];
        dream["findings"]         = (row[3] == "NULL") ? "" : row[3];
        dream["memories_created"] = (row[4] == "NULL") ? 0 : std::stoi(row[4]);
        dream["started_at"]       = std::stoll(row[5]);
        dream["ended_at"]         = std::stoll(row[6]);
        dream["sadhana_id"]       = std::stoll(row[7]);
        dream["realm"]            = row[8];

        if (row.size() >= 15 && row[9] != "NULL") {
            json sadhana;
            sadhana["id"]             = std::stoll(row[9]);
            sadhana["state"]          = row[10];
            sadhana["goal"]           = row[11];
            sadhana["iterations"]     = (row[12] == "NULL") ? 0 : std::stoi(row[12]);
            sadhana["brain_calls"]    = (row[13] == "NULL") ? 0 : std::stoi(row[13]);
            sadhana["last_action"]    = (row[14] == "NULL") ? "" : row[14];
            sadhana["brain_provider"] = (row.size() > 15 && row[15] != "NULL") ? row[15] : "";
            sadhana["brain_model"]    = (row.size() > 16 && row[16] != "NULL") ? row[16] : "";
            dream["sadhana"] = sadhana;

            if (sadhana_manager_) {
                int64_t sadhana_id = std::stoll(row[9]);
                dream["sadhana_history"] = sadhana_manager_->get_history(sadhana_id, 3);
            }
        }

        std::ostringstream msg;
        msg << "Dream #" << dream_id << " [" << row[2] << "]: " << row[1];
        return DuckDBToolResult::ok(msg.str(), dream);
    }

    DuckDBToolResult tool_impl_start(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not available");
        }

        std::string realm = params.value("realm", std::string("brahman"));
        int interval = params.value("interval_seconds", 86400);
        int max_turns = params.value("max_turns", 15);

        // Auto-detect repo from binary location if not provided
        std::string repo = params.value("repo", std::string(""));
        if (repo.empty()) {
            // Walk up from binary to find repo root (contains chitta/ and .git/)
            repo = "/maps/projects/fernandezguerra/apps/repos/cc-soul";
        }

        json goal_dsl;
        goal_dsl["kind"] = "impl";
        goal_dsl["repo"] = repo;

        std::string goal = "Autonomous self-improvement loop for cc-soul. "
                           "Each cycle: find one pending [impl]/[thought][impl]/[dream][impl] memory, "
                           "implement the change in " + repo + ", "
                           "run opencode review gate, commit only if approved.";

        int64_t sadhana_id = sadhana_manager_->create(
            goal, "claude", "sonnet",
            interval, realm, goal_dsl, max_turns
        );

        if (!sadhana_id) {
            return DuckDBToolResult::error("Failed to create impl sadhana");
        }

        if (!sadhana_manager_->start(sadhana_id)) {
            return DuckDBToolResult::error("Failed to start impl sadhana");
        }

        json result;
        result["sadhana_id"]       = sadhana_id;
        result["status"]           = "running";
        result["realm"]            = realm;
        result["interval_seconds"] = interval;
        result["repo"]             = repo;

        std::ostringstream msg;
        msg << "Impl sadhana #" << sadhana_id << " started (daily, " << max_turns << " turns/cycle)";
        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_think_wander(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not available");
        }

        std::string realm = params.value("realm", std::string("brahman"));

        // Build goal_dsl for a think sadhana
        json goal_dsl;
        goal_dsl["kind"] = "think";

        std::string goal = "[think] Internal memory synthesis: find patterns, connect gaps";

        int64_t sadhana_id = sadhana_manager_->create(
            goal, "claude", "sonnet",
            /*interval=*/0,
            realm,
            goal_dsl,
            /*max_turns=*/10
        );

        if (!sadhana_id) {
            return DuckDBToolResult::error("Failed to create think sadhana");
        }

        if (!sadhana_manager_->start(sadhana_id)) {
            return DuckDBToolResult::error("Failed to start think sadhana");
        }

        json result;
        result["sadhana_id"] = sadhana_id;
        result["status"]     = "thinking";
        result["realm"]      = realm;

        std::ostringstream msg;
        msg << "Think sadhana #" << sadhana_id << " started";
        return DuckDBToolResult::ok(msg.str(), result);
    }
