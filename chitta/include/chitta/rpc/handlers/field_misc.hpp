// Included into FieldRpcHandler class body — not a standalone header

// ═══════════════════════════════════════════════════════════════════════
// Sadhana tools — sadhana_manager_ only, no mind_ dependency
// ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_sadhana_start(const json& params) {
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not initialized");

        std::string goal = params.value("goal", "");
        if (goal.empty()) return DuckDBToolResult::error("Goal is required");

        std::string provider  = params.value("brain_provider", "");
        std::string model     = params.value("brain_model", "");
        int interval          = params.value("interval_seconds", 0);
        int max_turns         = params.value("max_turns", 0);
        std::string realm     = params.value("realm", "brahman");
        json goal_dsl;
        if (params.contains("goal_dsl") && params["goal_dsl"].is_object())
            goal_dsl = params["goal_dsl"];

        int64_t id = sadhana_manager_->create(goal, provider, model, interval, realm, goal_dsl, max_turns);
        if (id == 0) return DuckDBToolResult::error("Failed to create sadhana");

        if (!sadhana_manager_->start(id))
            return DuckDBToolResult::error("Created sadhana " + std::to_string(id) + " but failed to start");

        json result = {{"id", id}, {"state", "running"}, {"goal", goal.substr(0, 100)}};
        return DuckDBToolResult::ok("Started sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_pause(const json& params) {
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not initialized");

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) return DuckDBToolResult::error("Invalid sadhana ID");

        if (!sadhana_manager_->pause(id))
            return DuckDBToolResult::error("Failed to pause sadhana " + std::to_string(id));

        return DuckDBToolResult::ok("Paused sadhana " + std::to_string(id),
            {{"id", id}, {"state", "paused"}});
    }

    DuckDBToolResult tool_sadhana_resume(const json& params) {
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not initialized");

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) return DuckDBToolResult::error("Invalid sadhana ID");

        if (!sadhana_manager_->resume(id))
            return DuckDBToolResult::error("Failed to resume sadhana " + std::to_string(id));

        return DuckDBToolResult::ok("Resumed sadhana " + std::to_string(id),
            {{"id", id}, {"state", "running"}});
    }

    DuckDBToolResult tool_sadhana_stop(const json& params) {
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not initialized");

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) return DuckDBToolResult::error("Invalid sadhana ID");

        bool success  = params.value("success", true);
        std::string reason = params.value("reason", "");

        if (!sadhana_manager_->stop(id, success, reason))
            return DuckDBToolResult::error("Failed to stop sadhana " + std::to_string(id));

        json result = {{"id", id}, {"state", success ? "done" : "failed"}, {"reason", reason}};
        return DuckDBToolResult::ok("Stopped sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_status(const json& params) {
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not initialized");

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) return DuckDBToolResult::error("Invalid sadhana ID");

        auto opt = sadhana_manager_->get(id);
        if (!opt) return DuckDBToolResult::error("Sadhana " + std::to_string(id) + " not found");

        size_t history_limit = params.value("history_limit", 20);
        auto history = sadhana_manager_->get_history(id, history_limit);

        json result;
        result["id"]               = opt->id;
        result["goal"]             = opt->goal;
        result["state"]            = sadhana_state_to_string(opt->state);
        result["brain_provider"]   = opt->brain_provider;
        result["brain_model"]      = opt->brain_model;
        result["iterations"]       = opt->iterations;
        result["brain_calls"]      = opt->brain_calls;
        result["cost_usd"]         = opt->cost_usd;
        result["interval_seconds"] = opt->interval_seconds;
        result["max_turns"]        = opt->max_turns;
        result["realm"]            = opt->realm;
        result["created_at"]       = opt->created_at;
        result["updated_at"]       = opt->updated_at;
        result["last_sense"]       = opt->last_sense;
        result["last_action"]      = opt->last_action;
        result["last_result"]      = opt->last_result;
        result["history"]          = history;

        std::ostringstream msg;
        msg << "Sadhana " << id << " [" << sadhana_state_to_string(opt->state) << "] "
            << opt->iterations << " iterations";
        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_sadhana_list(const json& params) {
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not initialized");

        std::string state = params.value("state", "");
        std::string realm = params.value("realm", "");
        size_t limit      = params.value("limit", 50);

        auto sadhanas = sadhana_manager_->list(state, realm, limit);

        json result;
        result["sadhanas"] = json::array();
        result["count"]    = sadhanas.size();

        for (const auto& s : sadhanas) {
            result["sadhanas"].push_back({
                {"id",               s.id},
                {"goal",             s.goal.substr(0, 100)},
                {"state",            sadhana_state_to_string(s.state)},
                {"brain_model",      s.brain_model},
                {"iterations",       s.iterations},
                {"interval_seconds", s.interval_seconds},
                {"realm",            s.realm},
                {"created_at",       s.created_at},
            });
        }

        return DuckDBToolResult::ok("Found " + std::to_string(sadhanas.size()) + " sadhana(s)", result);
    }

    DuckDBToolResult tool_sadhana_set_model(const json& params) {
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not initialized");

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) return DuckDBToolResult::error("Invalid sadhana ID");

        std::string model = params.value("model", "");
        if (model.empty()) return DuckDBToolResult::error("model is required");

        if (!sadhana_manager_->set_model(id, model))
            return DuckDBToolResult::error("Failed to set model for sadhana " + std::to_string(id));

        return DuckDBToolResult::ok("Set model to " + model + " for sadhana " + std::to_string(id),
            {{"id", id}, {"model", model}});
    }

    DuckDBToolResult tool_sadhana_set_goal(const json& params) {
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not initialized");

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) return DuckDBToolResult::error("Invalid sadhana ID");

        std::string goal = params.value("goal", "");
        if (goal.empty()) return DuckDBToolResult::error("goal is required");

        if (!sadhana_manager_->set_goal(id, goal))
            return DuckDBToolResult::error("Failed to set goal for sadhana " + std::to_string(id));

        return DuckDBToolResult::ok("Updated goal for sadhana " + std::to_string(id),
            {{"id", id}, {"goal", goal}});
    }

    DuckDBToolResult tool_sadhana_set_interval(const json& params) {
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not initialized");

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) return DuckDBToolResult::error("Invalid sadhana ID");

        int interval = params.value("interval", 0);
        if (interval <= 0) return DuckDBToolResult::error("interval must be positive");

        if (!sadhana_manager_->set_interval(id, interval))
            return DuckDBToolResult::error("Failed to set interval for sadhana " + std::to_string(id));

        return DuckDBToolResult::ok(
            "Set interval to " + std::to_string(interval) + "s for sadhana " + std::to_string(id),
            {{"id", id}, {"interval", interval}});
    }

    DuckDBToolResult tool_sadhana_set_max_turns(const json& params) {
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not initialized");

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) return DuckDBToolResult::error("Invalid sadhana ID");

        int max_turns = params.value("max_turns", -1);
        if (max_turns < 0) return DuckDBToolResult::error("max_turns must be >= 0 (0 = use global default)");

        if (!sadhana_manager_->set_max_turns(id, max_turns))
            return DuckDBToolResult::error("Failed to set max_turns for sadhana " + std::to_string(id));

        std::string msg = max_turns == 0
            ? "Reset max_turns to global default for sadhana " + std::to_string(id)
            : "Set max_turns to " + std::to_string(max_turns) + " for sadhana " + std::to_string(id);
        return DuckDBToolResult::ok(msg, {{"id", id}, {"max_turns", max_turns}});
    }

    DuckDBToolResult tool_sadhana_checkpoint(const json& params) {
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not initialized");

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) return DuckDBToolResult::error("Invalid sadhana ID");

        std::string status  = params.value("status", "progressed");
        std::string summary = params.value("summary", "");
        if (summary.empty()) return DuckDBToolResult::error("summary is required");

        if (!sadhana_manager_->checkpoint(id, status, summary))
            return DuckDBToolResult::error("Checkpoint failed for sadhana " + std::to_string(id));

        return DuckDBToolResult::ok("Checkpoint [" + status + "] for sadhana " + std::to_string(id),
            {{"id", id}, {"status", status}, {"summary", summary}});
    }

// ═══════════════════════════════════════════════════════════════════════
// Dream tools — reimplemented without mind_->store() SQL
// State is tracked via chitta-field events; sadhanas handle execution.
// ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_dream_cancel(const json& params) {
        if (!params.contains("id"))
            return DuckDBToolResult::error("id is required");

        int64_t dream_id = params["id"].is_string()
            ? std::stoll(params["id"].get<std::string>())
            : params["id"].get<int64_t>();

        field_store_->emit_event("dream", "cancelled", std::to_string(dream_id), "");

        return DuckDBToolResult::ok("Cancelled dream #" + std::to_string(dream_id),
            {{"dream_id", dream_id}, {"status", "cancelled"}});
    }

    DuckDBToolResult tool_dream_force_woke(const json& params) {
        if (!params.contains("id"))
            return DuckDBToolResult::error("id is required");

        int64_t dream_id = params["id"].is_string()
            ? std::stoll(params["id"].get<std::string>())
            : params["id"].get<int64_t>();

        field_store_->emit_event("dream", "force_woke", std::to_string(dream_id), "[force-woke]");

        return DuckDBToolResult::ok("Force-woke dream #" + std::to_string(dream_id),
            {{"dream_id", dream_id}, {"status", "woke"}});
    }

    DuckDBToolResult tool_dream_start(const json& params) {
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not initialized");
        if (subconscious_) subconscious_->notify_query();

        std::string topic = params.value("topic", "");
        if (topic.empty()) return DuckDBToolResult::error("Topic is required");

        std::string realm          = params.value("realm", "brahman");
        std::string publish_path   = params.value("publish_path", "");
        std::string brain_provider = params.value("brain_provider", "claude");
        std::string brain_model    = params.value("brain_model", "sonnet");

        json goal_dsl = {{"kind", "dream"}, {"topic", topic}};
        if (!publish_path.empty()) goal_dsl["publish_path"] = publish_path;

        std::string goal = "[dream] Explore: " + topic;
        int64_t sadhana_id = sadhana_manager_->create(
            goal, brain_provider, brain_model, 0, realm, goal_dsl);

        if (sadhana_id == 0)
            return DuckDBToolResult::error("Failed to create dream sadhana");

        if (!sadhana_manager_->start(sadhana_id)) {
            return DuckDBToolResult::error(
                "Created dream sadhana " + std::to_string(sadhana_id) + " but failed to start");
        }

        // Record dream start as an event
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        field_store_->emit_event("dream", "started", std::to_string(sadhana_id),
            "{\"topic\":\"" + topic + "\",\"started_at\":" + std::to_string(now_ms) + "}");

        json result = {
            {"sadhana_id", sadhana_id},
            {"topic",      topic},
            {"status",     "dreaming"},
        };
        return DuckDBToolResult::ok(
            "Dream started: " + topic + " (sadhana #" + std::to_string(sadhana_id) + ")",
            result);
    }

    DuckDBToolResult tool_dream_wander(const json& params) {
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not initialized");
        if (subconscious_) subconscious_->notify_query();

        std::string realm        = params.value("realm", "brahman");
        std::string publish_path = params.value("publish_path", "");
        if (publish_path.empty()) {
            const char* env_path = std::getenv("CHITTA_DREAM_PUBLISH_PATH");
            if (env_path) publish_path = env_path;
        }

        std::string topic;

        // Priority 1: open questions / gaps
        auto gaps = field_store_->recall_by_kind("question", 5);
        if (!gaps.empty()) {
            topic = gaps[0].content;
            if (topic.size() > 100) topic = topic.substr(0, 100);
        }

        // Priority 2: low-confidence memories
        if (topic.empty()) {
            auto low_conf = field_store_->recall_by_kind("episode", 20);
            for (const auto& h : low_conf) {
                if (h.confidence > 0.0f && h.confidence < 0.5f) {
                    topic = h.content;
                    if (topic.size() > 100) topic = topic.substr(0, 100);
                    break;
                }
            }
        }

        // Priority 3: curiosity seeds
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
                "entropy, information, and the arrow of time",
            };
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            topic = seeds[static_cast<size_t>(now_ms) % seeds.size()];
        }

        json start_params = {
            {"topic",          topic},
            {"realm",          realm},
            {"brain_provider", "opencode"},
            {"brain_model",    "gpt-4o"},
        };
        if (!publish_path.empty()) start_params["publish_path"] = publish_path;
        return tool_dream_start(start_params);
    }

    DuckDBToolResult tool_dream_list(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        size_t limit      = static_cast<size_t>(params.value("limit", 10));
        std::string realm = params.value("realm", "");

        // Dreams are recorded as events; retrieve recent dream start events
        auto hits = field_store_->recall_keyword("dream started", std::min(limit, size_t{50}));

        json dreams = json::array();
        for (const auto& h : hits) {
            if (!realm.empty() && h.realm != realm) continue;
            dreams.push_back({
                {"sadhana_id", std::to_string(h.memory_id)},
                {"topic",      h.content},
                {"status",     "unknown"},
                {"realm",      h.realm},
            });
            if (dreams.size() >= limit) break;
        }

        return DuckDBToolResult::ok("Found " + std::to_string(dreams.size()) + " dream(s)",
            {{"dreams", dreams}, {"count", dreams.size()}});
    }

    DuckDBToolResult tool_dream_status(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        if (!params.contains("id"))
            return DuckDBToolResult::error("id is required");

        int64_t dream_id = params["id"].is_string()
            ? std::stoll(params["id"].get<std::string>())
            : params["id"].get<int64_t>();

        // In chitta-field, dream state is tracked via sadhana
        json dream = {{"sadhana_id", dream_id}, {"status", "unknown"}};
        if (sadhana_manager_) {
            auto opt = sadhana_manager_->get(dream_id);
            if (opt) {
                dream["state"] = sadhana_state_to_string(opt->state);
                dream["iterations"] = opt->iterations;
                dream["last_action"] = opt->last_action;
            }
        }

        return DuckDBToolResult::ok("Dream/sadhana #" + std::to_string(dream_id), dream);
    }

    DuckDBToolResult tool_impl_start(const json& params) {
        if (subconscious_) subconscious_->notify_query();
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not available");

        std::string realm  = params.value("realm", std::string("brahman"));
        int interval       = params.value("interval_seconds", 86400);
        int max_turns      = params.value("max_turns", 15);
        std::string repo   = params.value("repo", std::string(""));
        if (repo.empty())
            repo = "/maps/projects/fernandezguerra/apps/repos/cc-soul";

        json goal_dsl = {{"kind", "impl"}, {"repo", repo}};

        std::string goal =
            "Autonomous self-improvement loop for cc-soul. "
            "Each cycle: find one pending [impl]/[thought][impl]/[dream][impl] memory, "
            "implement the change in " + repo + ", "
            "run opencode review gate, commit only if approved.";

        int64_t sadhana_id = sadhana_manager_->create(
            goal, "claude", "sonnet", interval, realm, goal_dsl, max_turns);
        if (!sadhana_id)
            return DuckDBToolResult::error("Failed to create impl sadhana");

        if (!sadhana_manager_->start(sadhana_id))
            return DuckDBToolResult::error("Failed to start impl sadhana");

        json result = {
            {"sadhana_id",       sadhana_id},
            {"status",           "running"},
            {"realm",            realm},
            {"interval_seconds", interval},
            {"repo",             repo},
        };
        return DuckDBToolResult::ok(
            "Impl sadhana #" + std::to_string(sadhana_id) +
            " started (daily, " + std::to_string(max_turns) + " turns/cycle)",
            result);
    }

    DuckDBToolResult tool_think_wander(const json& params) {
        if (subconscious_) subconscious_->notify_query();
        if (!sadhana_manager_)
            return DuckDBToolResult::error("Sadhana manager not available");

        std::string realm = params.value("realm", std::string("brahman"));
        json goal_dsl = {{"kind", "think"}};
        std::string goal = "[think] Internal memory synthesis: find patterns, connect gaps";

        int64_t sadhana_id = sadhana_manager_->create(
            goal, "claude", "sonnet", 0, realm, goal_dsl, 10);
        if (!sadhana_id)
            return DuckDBToolResult::error("Failed to create think sadhana");

        if (!sadhana_manager_->start(sadhana_id))
            return DuckDBToolResult::error("Failed to start think sadhana");

        return DuckDBToolResult::ok("Think sadhana #" + std::to_string(sadhana_id) + " started",
            {{"sadhana_id", sadhana_id}, {"status", "thinking"}, {"realm", realm}});
    }

// ═══════════════════════════════════════════════════════════════════════
// Anticipation tools
// ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_anticipation_observe(const json& params) {
        std::string context = params.value("context", "");
        std::string action  = params.value("action", "");
        std::string realm   = params.value("realm", "brahman");

        if (context.empty() || action.empty())
            return DuckDBToolResult::error("context and action are required");

        field_store_->add_triplet(context, "anticipates", action);

        std::string text = "anticipation: " + context + " → " + action;
        auto embedding = embed_text(text);
        uint64_t id = field_store_->remember("anticipation", realm, text, embedding, 0.7f, 0.001f);

        return DuckDBToolResult::ok("Pattern recorded (id: " + std::to_string(id) + ")",
            {{"id", std::to_string(id)}});
    }

    DuckDBToolResult tool_anticipation_predict(const json& params) {
        std::string context   = params.value("context", "");
        size_t limit          = static_cast<size_t>(params.value("limit", 5));
        float min_confidence  = params.value("min_confidence", 0.3f);
        std::string realm     = params.value("realm", "");

        if (context.empty()) return DuckDBToolResult::error("context is required");

        // Query triplets for "anticipates" relationships from this context
        std::string triplets_raw = field_store_->query_subject(context);
        json triplets_json;
        try { triplets_json = json::parse(triplets_raw, nullptr, false); } catch (...) {}
        if (triplets_json.is_discarded() || !triplets_json.is_array()) triplets_json = json::array();

        json patterns = json::array();
        for (const auto& t : triplets_json) {
            std::string pred = t.value("predicate", "");
            if (pred != "anticipates") continue;
            patterns.push_back({
                {"context",    context},
                {"action",     t.value("object", "")},
                {"confidence", 0.7f},
            });
            if (patterns.size() >= limit) break;
        }

        // Supplement with semantic recall
        if (patterns.size() < limit) {
            auto embedding = embed_query(context);
            if (!embedding.empty()) {
                auto hits = field_store_->recall(embedding, limit * 2, realm);
                for (const auto& h : hits) {
                    if (h.kind != "anticipation") continue;
                    if (h.confidence < min_confidence) continue;
                    patterns.push_back({
                        {"id",         std::to_string(h.memory_id)},
                        {"content",    h.content},
                        {"confidence", h.confidence},
                        {"realm",      h.realm},
                    });
                    if (patterns.size() >= limit) break;
                }
            }
        }

        std::ostringstream ss;
        ss << "Predicted " << patterns.size() << " action(s) for context: " << context;
        return DuckDBToolResult::ok(ss.str(), {{"patterns", patterns}});
    }

    DuckDBToolResult tool_anticipation_success(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        field_store_->strengthen(static_cast<uint64_t>(id), 0.1f);
        return DuckDBToolResult::ok("Pattern #" + id_str + " marked successful");
    }

    DuckDBToolResult tool_anticipation_list(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit      = static_cast<size_t>(params.value("limit", 20));
        std::string sort_by = params.value("sort_by", "confidence");

        auto hits = field_store_->recall_by_kind("anticipation", limit);

        json patterns = json::array();
        for (const auto& h : hits) {
            if (!realm.empty() && h.realm != realm) continue;
            patterns.push_back({
                {"id",         std::to_string(h.memory_id)},
                {"content",    h.content},
                {"confidence", h.confidence},
                {"realm",      h.realm},
            });
        }

        if (sort_by == "confidence") {
            std::sort(patterns.begin(), patterns.end(), [](const json& a, const json& b) {
                return a.value("confidence", 0.0f) > b.value("confidence", 0.0f);
            });
        }

        std::ostringstream ss;
        ss << "Learned Anticipation Patterns\n"
           << "══════════════════════════════\n\n";
        if (patterns.empty()) {
            ss << "No patterns learned yet.\n";
        } else {
            for (const auto& p : patterns) {
                ss << "[" << p.value("id", "?") << "] " << p.value("content", "").substr(0, 80) << "\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", patterns.size()}, {"patterns", patterns}});
    }

    DuckDBToolResult tool_anticipation_filter(const json& params) {
        std::string session_id = params.value("session_id", "");
        size_t max = static_cast<size_t>(params.value("max", 2));

        auto hits = field_store_->recall_by_kind("anticipation", max * 4);

        json candidates = json::array();
        for (const auto& h : hits) {
            if (h.confidence < 0.5f) continue;
            candidates.push_back({
                {"id",         std::to_string(h.memory_id)},
                {"prediction", h.content},
                {"confidence", h.confidence},
            });
            if (candidates.size() >= max) break;
        }

        if (candidates.empty()) {
            return DuckDBToolResult::ok("No predictions pass the annoyance gate", {
                {"session_id", session_id}, {"count", 0}, {"candidates", json::array()}
            });
        }

        return DuckDBToolResult::ok(
            std::to_string(candidates.size()) + " prediction(s) ready to surface",
            {{"session_id", session_id}, {"count", candidates.size()}, {"candidates", candidates}});
    }

    DuckDBToolResult tool_anticipation_gate_status(const json& params) {
        std::string session_id = params.value("session_id", "");
        return DuckDBToolResult::ok("Annoyance gate status (chitta-field stub)", {
            {"session_id",       session_id},
            {"gate_open",        true},
            {"budget_remaining", 5},
            {"confidence_floor", 0.5f},
            {"note",             "Gate state not persisted in chitta-field backend"},
        });
    }

    DuckDBToolResult tool_anticipation_record_outcome(const json& params) {
        auto [candidate_id, candidate_str] = parse_id(params, "candidate_id");
        if (candidate_id <= 0) return DuckDBToolResult::error("candidate_id is required");

        bool correct = params.value("correct", false);

        if (correct) {
            field_store_->strengthen(static_cast<uint64_t>(candidate_id), 0.1f);
        } else {
            field_store_->weaken(static_cast<uint64_t>(candidate_id), 0.05f);
        }

        field_store_->emit_event("anticipation",
            correct ? "correct" : "incorrect", candidate_str, "");

        return DuckDBToolResult::ok(
            "Recorded outcome: " + std::string(correct ? "correct" : "incorrect") +
            " for candidate #" + candidate_str,
            {{"candidate_id", candidate_id}, {"outcome", correct ? "correct" : "incorrect"}});
    }

// ═══════════════════════════════════════════════════════════════════════
// Habit tools
// ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_habit_observe(const json& params) {
        std::string trigger  = params.value("trigger", "");
        std::string response = params.value("response", "");
        std::string realm    = params.value("realm", "brahman");
        static constexpr int MIN_OBSERVATIONS = 3;

        if (trigger.empty() || response.empty())
            return DuckDBToolResult::error("trigger and response are required");

        // Always strengthen the triplet edge (tracks frequency as weight)
        field_store_->add_triplet(trigger, "triggers", response);

        // Count how many times this exact pair has been observed via triplet query
        std::string triplets_raw = field_store_->query_subject(trigger);
        int count = 0;
        try {
            auto arr = json::parse(triplets_raw);
            for (const auto& t : arr) {
                if (t.value("predicate","") == "triggers" && t.value("object","") == response)
                    ++count;
            }
        } catch (...) {}

        // Only store a habit memory once the pair has been seen MIN_OBSERVATIONS times
        if (count < MIN_OBSERVATIONS) {
            return DuckDBToolResult::ok(
                "Habit observed (" + std::to_string(count) + "/" +
                std::to_string(MIN_OBSERVATIONS) + " before stored)",
                {{"count", count}, {"threshold", MIN_OBSERVATIONS}});
        }

        std::string text = "habit: " + trigger + " → " + response;
        auto embedding = embed_text(text);
        uint64_t id = field_store_->remember("habit", realm, text, embedding, 0.7f, 0.001f);

        return DuckDBToolResult::ok(
            "Habit stored (id: " + std::to_string(id) + ", observations=" + std::to_string(count) + ")",
            {{"id", std::to_string(id)}, {"count", count}});
    }

    DuckDBToolResult tool_habit_match(const json& params) {
        std::string context   = params.value("context", "");
        float min_strength    = params.value("min_strength", 0.3f);
        std::string realm     = params.value("realm", "");

        if (context.empty()) return DuckDBToolResult::error("context is required");

        // Triplet query for "triggers" relationships
        std::string triplets_raw = field_store_->query_subject(context);
        json triplets_json;
        try { triplets_json = json::parse(triplets_raw, nullptr, false); } catch (...) {}
        if (triplets_json.is_discarded() || !triplets_json.is_array()) triplets_json = json::array();

        json habits = json::array();
        for (const auto& t : triplets_json) {
            std::string pred = t.value("predicate", "");
            if (pred != "triggers") continue;
            habits.push_back({
                {"trigger",  context},
                {"response", t.value("object", "")},
                {"strength", 0.7f},
            });
        }

        // Supplement with semantic recall
        if (habits.empty()) {
            auto embedding = embed_query(context);
            if (!embedding.empty()) {
                auto hits = field_store_->recall(embedding, 10, realm);
                for (const auto& h : hits) {
                    if (h.kind != "habit") continue;
                    if (h.confidence < min_strength) continue;
                    habits.push_back({
                        {"id",       std::to_string(h.memory_id)},
                        {"content",  h.content},
                        {"strength", h.confidence},
                        {"realm",    h.realm},
                    });
                }
            }
        }

        std::ostringstream ss;
        ss << "Matching Habits\n═══════════════\n\n";
        if (habits.empty()) {
            ss << "No matching habits found.\n";
        } else {
            for (const auto& h : habits) {
                ss << "• " << h.value("trigger", h.value("content", "?"))
                   << " → " << h.value("response", "") << "\n";
                ss << "  Strength: " << std::fixed << std::setprecision(2)
                   << h.value("strength", 0.0f) << "\n\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), {{"habits", habits}});
    }

    DuckDBToolResult tool_habit_strengthen(const json& params) {
        auto [id, id_str] = parse_id(params);
        float amount = params.value("amount", 0.1f);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        field_store_->strengthen(static_cast<uint64_t>(id), amount);
        return DuckDBToolResult::ok(
            "Habit #" + id_str + " strengthened by " + std::to_string(amount));
    }

    DuckDBToolResult tool_habit_weaken(const json& params) {
        auto [id, id_str] = parse_id(params);
        float amount = params.value("amount", 0.05f);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        field_store_->weaken(static_cast<uint64_t>(id), amount);
        return DuckDBToolResult::ok(
            "Habit #" + id_str + " weakened by " + std::to_string(amount));
    }

    DuckDBToolResult tool_habit_list(const json& params) {
        std::string realm   = params.value("realm", "");
        float min_strength  = params.value("min_strength", 0.0f);
        size_t limit        = static_cast<size_t>(params.value("limit", 20));

        auto hits = field_store_->recall_by_kind("habit", limit);

        json habits = json::array();
        for (const auto& h : hits) {
            if (!realm.empty() && h.realm != realm) continue;
            if (h.confidence < min_strength) continue;
            habits.push_back({
                {"id",       std::to_string(h.memory_id)},
                {"content",  h.content},
                {"strength", h.confidence},
                {"realm",    h.realm},
            });
        }

        std::ostringstream ss;
        ss << "Formed Habits\n══════════════\n\n";
        if (habits.empty()) {
            ss << "No habits formed yet.\n";
        } else {
            for (const auto& h : habits) {
                ss << "[" << h.value("id", "?") << "] "
                   << h.value("content", "").substr(0, 80) << "\n"
                   << "  strength=" << std::fixed << std::setprecision(2)
                   << h.value("strength", 0.0f) << "\n\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", habits.size()}, {"habits", habits}});
    }

// ═══════════════════════════════════════════════════════════════════════
// Profile tools
// ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_profile_get(const json& params) {
        std::string user_id = params.value("user_id", "default");
        auto hits = field_store_->recall_keyword("profile " + user_id, 10);
        json profile_entries = hits_to_results_json(hits);
        return DuckDBToolResult::ok("Profile entries for " + user_id,
            {{"user_id", user_id}, {"entries", profile_entries}});
    }

    DuckDBToolResult tool_profile_update(const json& params) {
        std::string user_id = params.value("user_id", "default");
        std::string field   = params.value("field", "");
        std::string value   = params.value("value", "");

        if (field.empty()) return DuckDBToolResult::error("field is required");

        std::string content = "profile:" + field + "=" + value;
        auto embedding = embed_text("profile " + user_id + " " + field + " " + value);
        uint64_t id = field_store_->remember("profile", "brahman", content, embedding, 0.9f, 0.0f);

        field_store_->add_triplet("profile:" + user_id, "has_" + field, value);

        return DuckDBToolResult::ok("Profile updated for " + user_id,
            {{"id", std::to_string(id)}, {"user_id", user_id}, {"field", field}, {"value", value}});
    }

    DuckDBToolResult tool_profile_observe(const json& params) {
        std::string observation_type = params.value("observation_type", "");
        std::string value            = params.value("value", "");
        std::string user_id          = params.value("user_id", "default");

        std::string content = "observation[" + observation_type + "] for " + user_id + ": " + value;
        auto embedding = embed_text(content);
        uint64_t id = field_store_->remember("observation", "brahman", content, embedding, 0.7f, 0.01f);

        return DuckDBToolResult::ok("Observation recorded #" + std::to_string(id),
            {{"id", std::to_string(id)}});
    }

// ═══════════════════════════════════════════════════════════════════════
// Goal tools
// ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_goal_set(const json& params) {
        std::string title       = params.value("title", "");
        std::string description = params.value("description", "");
        std::string deadline    = params.value("deadline", "");
        std::string realm       = params.value("realm", "brahman");

        if (title.empty()) return DuckDBToolResult::error("title is required");

        std::string content = title;
        if (!description.empty()) content += ": " + description;
        if (!deadline.empty())    content += " (by " + deadline + ")";

        if (params.contains("milestones") && params["milestones"].is_array()) {
            content += "\nMilestones:";
            for (const auto& m : params["milestones"]) {
                if (m.is_string()) content += "\n- " + m.get<std::string>();
            }
        }

        auto embedding = embed_text(content);
        uint64_t id = field_store_->remember("goal", realm, content, embedding, 0.9f, 0.0f);

        return DuckDBToolResult::ok("Goal set #" + std::to_string(id),
            {{"id", std::to_string(id)}, {"title", title}, {"realm", realm}});
    }

    DuckDBToolResult tool_goal_get(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        auto hits = field_store_->recall_by_kind("goal", 100);
        for (const auto& h : hits) {
            if (static_cast<int64_t>(h.memory_id) == id) {
                return DuckDBToolResult::ok("Goal #" + id_str, {
                    {"id",         id_str},
                    {"content",    h.content},
                    {"confidence", h.confidence},
                    {"realm",      h.realm},
                });
            }
        }
        return DuckDBToolResult::error("Goal #" + id_str + " not found");
    }

    DuckDBToolResult tool_goal_list(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit      = static_cast<size_t>(params.value("limit", 20));

        auto hits = field_store_->recall_by_kind("goal", limit);

        json goals = json::array();
        for (const auto& h : hits) {
            if (!realm.empty() && h.realm != realm) continue;
            goals.push_back({
                {"id",         std::to_string(h.memory_id)},
                {"content",    h.content},
                {"confidence", h.confidence},
                {"realm",      h.realm},
            });
        }

        return DuckDBToolResult::ok(std::to_string(goals.size()) + " goal(s)",
            {{"goals", goals}, {"count", goals.size()}});
    }

    DuckDBToolResult tool_goal_progress(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        std::string progress  = params.value("progress", "");
        std::string milestone = params.value("milestone", "");

        std::string payload = progress;
        if (!milestone.empty()) payload += "; milestone: " + milestone;

        field_store_->emit_event("goal", "progress", id_str, payload);
        field_store_->strengthen(static_cast<uint64_t>(id), 0.05f);

        return DuckDBToolResult::ok("Progress recorded for goal #" + id_str,
            {{"id", id_str}, {"progress", progress}});
    }

    DuckDBToolResult tool_goal_complete(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        std::string outcome = params.value("outcome", "");
        field_store_->emit_event("goal", "complete", id_str, outcome);
        field_store_->strengthen(static_cast<uint64_t>(id), 0.2f);
        field_store_->add_triplet(id_str, "status", "completed");

        return DuckDBToolResult::ok("Goal #" + id_str + " completed",
            {{"id", id_str}, {"outcome", outcome}});
    }

// ═══════════════════════════════════════════════════════════════════════
// Calibration tools
// ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_calibration_record(const json& params) {
        std::string domain = params.value("domain", "");
        bool success       = params.value("success", true);

        if (domain.empty()) return DuckDBToolResult::error("domain is required");

        field_store_->emit_event("calibration",
            success ? "success" : "failure", domain, "");

        return DuckDBToolResult::ok(
            "Calibration recorded: " + domain + " " + (success ? "success" : "failure"),
            {{"domain", domain}, {"success", success}});
    }

    DuckDBToolResult tool_calibration_score(const json& params) {
        std::string domain = params.value("domain", "");
        if (domain.empty()) return DuckDBToolResult::error("domain is required");

        auto hits = field_store_->recall_keyword("calibration " + domain, 20);

        size_t successes = 0, failures = 0;
        for (const auto& h : hits) {
            if (h.content.find("success") != std::string::npos) successes++;
            else if (h.content.find("failure") != std::string::npos) failures++;
        }
        size_t total = successes + failures;
        float score  = total > 0 ? static_cast<float>(successes) / static_cast<float>(total) : 0.5f;

        json result = {
            {"domain",    domain},
            {"score",     score},
            {"successes", successes},
            {"failures",  failures},
            {"total",     total},
        };
        std::ostringstream ss;
        ss << "Calibration [" << domain << "]: "
           << std::fixed << std::setprecision(1) << (score * 100) << "% ("
           << successes << "/" << total << ")";
        return DuckDBToolResult::ok(ss.str(), result);
    }

// ═══════════════════════════════════════════════════════════════════════
// Narrative tools
// ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_narrative_status(const json& params) {
        std::string session_id = params.value("session_id", get_session_id());
        return DuckDBToolResult::ok("Narrative status (chitta-field)", {
            {"session_id", session_id},
            {"mode",       "work"},
            {"segment",    "active"},
            {"note",       "Narrative engine not available in chitta-field backend"},
        });
    }

    DuckDBToolResult tool_narrative_log(const json& params) {
        std::string session_id = params.value("session_id", get_session_id());
        std::string kind       = params.value("kind", "");
        std::string summary    = params.value("summary", "");

        if (kind.empty() || summary.empty())
            return DuckDBToolResult::error("kind and summary are required");

        std::string payload = summary;
        if (params.contains("payload")) payload = params["payload"].dump();

        field_store_->emit_event("narrative", kind, session_id, payload);

        return DuckDBToolResult::ok("Event logged, kind: " + kind, {
            {"session_id", session_id},
            {"kind",       kind},
            {"mode",       "work"},
        });
    }

    DuckDBToolResult tool_narrative_history(const json& params) {
        std::string session_id = params.value("session_id", get_session_id());
        size_t limit           = static_cast<size_t>(params.value("limit", 20));

        auto hits = field_store_->recall_keyword("narrative " + session_id, limit);
        json segments = hits_to_results_json(hits);

        return DuckDBToolResult::ok(
            std::to_string(segments.size()) + " narrative event(s) for session " + session_id,
            {{"session_id", session_id}, {"count", segments.size()}, {"segments", segments}});
    }

// ═══════════════════════════════════════════════════════════════════════
// Memory management tools (from memory.hpp, adapted for chitta-field)
// ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_memory_history(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        auto hits = field_store_->recall_by_kind("episode", 1);
        std::string content;
        for (const auto& h : hits) {
            if (static_cast<int64_t>(h.memory_id) == id) {
                content = h.content;
                break;
            }
        }

        json versions = json::array();
        versions.push_back({{"version", 1}, {"content", content}});

        return DuckDBToolResult::ok("Memory #" + id_str + " history (chitta-field)",
            {{"id", id_str}, {"versions", versions},
             {"note", "Version history is append-only in chitta-field"}});
    }

    DuckDBToolResult tool_memory_revert(const json& params) {
        return DuckDBToolResult::error(
            "Version history not available in chitta-field backend");
    }

    DuckDBToolResult tool_pin_memory(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        std::string reason = params.value("reason", "");
        field_store_->strengthen(static_cast<uint64_t>(id), 0.3f);
        field_store_->add_triplet(id_str, "pinned", "true");
        if (!reason.empty())
            field_store_->add_triplet(id_str, "pin_reason", reason);

        return DuckDBToolResult::ok("Memory #" + id_str + " pinned",
            {{"id", id_str}, {"reason", reason}});
    }

    DuckDBToolResult tool_unpin_memory(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        field_store_->weaken(static_cast<uint64_t>(id), 0.1f);
        field_store_->add_triplet(id_str, "pinned", "false");

        return DuckDBToolResult::ok("Memory #" + id_str + " unpinned", {{"id", id_str}});
    }

    DuckDBToolResult tool_list_pinned(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit      = static_cast<size_t>(params.value("limit", 20));

        auto hits = field_store_->recall_keyword("pinned", limit);
        json pinned = hits_to_results_json(hits);

        return DuckDBToolResult::ok(std::to_string(pinned.size()) + " pinned memory(ies)",
            {{"pinned", pinned}, {"count", pinned.size()}});
    }

    DuckDBToolResult tool_memory_lock(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        std::string holder_id   = params.value("holder_id", "");
        std::string holder_type = params.value("holder_type", "");
        int duration            = params.value("duration", 0);

        json payload = {
            {"holder_id",   holder_id},
            {"holder_type", holder_type},
            {"duration",    duration},
        };
        field_store_->emit_event("lock", "acquire", id_str, payload.dump());

        return DuckDBToolResult::ok("Memory #" + id_str + " locked",
            {{"id", id_str}, {"holder_id", holder_id}, {"status", "locked"}});
    }

    DuckDBToolResult tool_memory_unlock(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        std::string holder_id = params.value("holder_id", "");
        field_store_->emit_event("lock", "release", id_str, holder_id);

        return DuckDBToolResult::ok("Memory #" + id_str + " unlocked",
            {{"id", id_str}, {"status", "unlocked"}});
    }

    DuckDBToolResult tool_memory_lock_status(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        return DuckDBToolResult::ok("Lock status for memory #" + id_str,
            {{"id", id_str}, {"locked", false},
             {"note", "Lock state not persisted in chitta-field backend"}});
    }

    DuckDBToolResult tool_propose_change(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        std::string content     = params.value("content", "");
        std::string proposed_by = params.value("proposed_by", "");

        if (content.empty()) return DuckDBToolResult::error("content is required");

        std::string text = "change to " + id_str + ": " + content;
        if (!proposed_by.empty()) text += " (by " + proposed_by + ")";

        auto embedding = embed_text(text);
        uint64_t proposal_id = field_store_->remember(
            "proposal", "brahman", text, embedding, 0.7f, 0.001f);

        return DuckDBToolResult::ok("Proposal #" + std::to_string(proposal_id) + " created",
            {{"merge_id", std::to_string(proposal_id)}, {"target_id", id_str}});
    }

    DuckDBToolResult tool_list_merge_queue(const json& params) {
        size_t limit = static_cast<size_t>(params.value("limit", 20));
        auto hits = field_store_->recall_by_kind("proposal", limit);
        json queue = hits_to_results_json(hits);
        return DuckDBToolResult::ok(std::to_string(queue.size()) + " proposal(s) in queue",
            {{"queue", queue}, {"count", queue.size()}});
    }

    DuckDBToolResult tool_resolve_merge(const json& params) {
        auto [merge_id, merge_str] = parse_id(params, "merge_id");
        if (merge_id <= 0) return DuckDBToolResult::error("merge_id is required");

        std::string status     = params.value("status", "");
        std::string resolution = params.value("resolution", "");

        field_store_->emit_event("proposal", status, merge_str, resolution);

        if (status == "accepted") {
            field_store_->strengthen(static_cast<uint64_t>(merge_id), 0.1f);
        } else if (status == "rejected") {
            field_store_->weaken(static_cast<uint64_t>(merge_id), 0.2f);
        }

        return DuckDBToolResult::ok("Proposal #" + merge_str + " " + status,
            {{"merge_id", merge_str}, {"status", status}});
    }

// ═══════════════════════════════════════════════════════════════════════
// File timeline tools
// ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_file_timeline(const json& params) {
        std::string query      = params.value("query", "");
        std::string session_id = params.value("session_id", "");
        std::string path       = params.value("path", "");
        size_t limit           = static_cast<size_t>(params.value("limit", 20));

        std::string search = query.empty() ? "file" : query;
        if (!path.empty()) search = path;

        auto hits = field_store_->recall_keyword(search, limit);
        json events = hits_to_results_json(hits);

        return DuckDBToolResult::ok(std::to_string(events.size()) + " file event(s) found",
            {{"events", events}, {"count", events.size()}});
    }

    DuckDBToolResult tool_file_at_time(const json&) {
        return DuckDBToolResult::error(
            "File time machine not available in chitta-field backend");
    }

    DuckDBToolResult tool_file_restore(const json&) {
        return DuckDBToolResult::error(
            "File restore not available in chitta-field backend");
    }

    DuckDBToolResult tool_file_index_session(const json& params) {
        std::string session_id = params.value("session_id", "");
        field_store_->emit_event("file_index", "session", session_id, "{}");
        return DuckDBToolResult::ok("File index session event emitted",
            {{"session_id", session_id}, {"status", "ok"}});
    }

    DuckDBToolResult tool_file_index_all(const json& params) {
        field_store_->emit_event("file_index", "all", "", "{}");
        return DuckDBToolResult::ok("File index all event emitted", {{"status", "ok"}});
    }

// ═══════════════════════════════════════════════════════════════════════
// Learning and exposure tools
// ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_learn_outcome(const json& params) {
        auto [memory_id, memory_str] = parse_id(params, "memory_id");
        if (memory_id <= 0) return DuckDBToolResult::error("memory_id is required");

        std::string outcome = params.value("outcome", "");
        std::string context = params.value("context", "");

        field_store_->emit_event("outcome", outcome, memory_str, context);

        float reward = 0.0f;
        if (outcome == "positive" || outcome == "helpful" || outcome == "correct") {
            field_store_->strengthen(static_cast<uint64_t>(memory_id), 0.1f);
            reward = 0.5f;
        } else if (outcome == "negative" || outcome == "unhelpful" || outcome == "incorrect") {
            field_store_->weaken(static_cast<uint64_t>(memory_id), 0.05f);
            reward = -0.3f;
        }

        // Feed reward back to route learner if episode_id provided
        uint64_t episode_id = params.value("episode_id", (uint64_t)0);
        if (episode_id > 0 && reward != 0.0f) {
            field_store_->route_feedback(episode_id, reward);
        }

        return DuckDBToolResult::ok("Outcome recorded for memory #" + memory_str,
            {{"memory_id", memory_str}, {"outcome", outcome}, {"reward", reward}});
    }

    DuckDBToolResult tool_log_exposure(const json& params) {
        std::string session_id = params.value("session_id", "");
        std::string turn_id    = params.value("turn_id", "");
        std::string hook_type  = params.value("hook_type", "");

        json payload;
        if (params.contains("memory_ids"))     payload["memory_ids"]      = params["memory_ids"];
        if (params.contains("ranks"))          payload["ranks"]            = params["ranks"];
        if (params.contains("resonance_scores")) payload["resonance_scores"] = params["resonance_scores"];
        payload["turn_id"] = turn_id;

        field_store_->emit_event("exposure", hook_type, session_id, payload.dump());

        return DuckDBToolResult::ok("Exposure logged for session " + session_id,
            {{"session_id", session_id}, {"hook_type", hook_type}});
    }

    DuckDBToolResult tool_get_sus_metrics(const json&) {
        return DuckDBToolResult::ok("SUS metrics (chitta-field stub)", {
            {"exposures", 0},
            {"note",      "SUS metrics not tracked in chitta-field"},
        });
    }

    DuckDBToolResult tool_episode_cluster_status(const json& params) {
        float similarity_threshold = params.value("similarity_threshold", 0.8f);
        int min_occurrences        = params.value("min_occurrences", 2);

        return DuckDBToolResult::ok("Episode cluster status (chitta-field stub)", {
            {"clusters",            0},
            {"similarity_threshold", similarity_threshold},
            {"min_occurrences",      min_occurrences},
            {"note",                 "Episode clustering not available in chitta-field backend"},
        });
    }

// ═══════════════════════════════════════════════════════════════════════
// Insight and aspect tools
// ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_insight_promote(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) return DuckDBToolResult::error("id is required");

        std::string reason = params.value("reason", "");
        field_store_->strengthen(static_cast<uint64_t>(id), 0.5f);
        field_store_->add_triplet(id_str, "promoted", "global");
        if (!reason.empty())
            field_store_->add_triplet(id_str, "promotion_reason", reason);

        return DuckDBToolResult::ok("Insight #" + id_str + " promoted to global",
            {{"id", id_str}, {"reason", reason}});
    }

    DuckDBToolResult tool_insight_global(const json& params) {
        size_t limit   = static_cast<size_t>(params.value("limit", 20));
        std::string tag = params.value("tag", "");

        auto hits = field_store_->recall_by_kind("insight", limit);

        json insights = json::array();
        for (const auto& h : hits) {
            // Filter by tag triplet if requested (approximate: check content)
            if (!tag.empty() && h.content.find(tag) == std::string::npos) continue;
            insights.push_back({
                {"id",         std::to_string(h.memory_id)},
                {"content",    h.content},
                {"confidence", h.confidence},
                {"realm",      h.realm},
            });
        }

        return DuckDBToolResult::ok(std::to_string(insights.size()) + " global insight(s)",
            {{"insights", insights}, {"count", insights.size()}});
    }

    DuckDBToolResult tool_list_by_aspect(const json& params) {
        std::string aspect = params.value("aspect", "");
        size_t limit       = static_cast<size_t>(params.value("limit", 20));
        float min_confidence = params.value("min_confidence", 0.0f);

        if (aspect.empty()) return DuckDBToolResult::error("aspect is required");

        auto it = ASPECT_KINDS.find(aspect);
        if (it == ASPECT_KINDS.end())
            return DuckDBToolResult::error("Unknown aspect: " + aspect);

        json results = json::array();
        for (const auto& kind : it->second) {
            auto hits = field_store_->recall_by_kind(kind, limit);
            for (const auto& h : hits) {
                if (h.confidence < min_confidence) continue;
                results.push_back({
                    {"id",         std::to_string(h.memory_id)},
                    {"kind",       h.kind},
                    {"content",    h.content},
                    {"confidence", h.confidence},
                    {"realm",      h.realm},
                });
            }
        }

        // Sort by confidence descending
        std::sort(results.begin(), results.end(), [](const json& a, const json& b) {
            return a.value("confidence", 0.0f) > b.value("confidence", 0.0f);
        });
        if (results.size() > limit)
            results.erase(results.begin() + static_cast<int>(limit), results.end());

        return DuckDBToolResult::ok(
            std::to_string(results.size()) + " result(s) for aspect '" + aspect + "'",
            {{"aspect", aspect}, {"results", results}, {"count", results.size()}});
    }

    DuckDBToolResult tool_list_aspects(const json&) {
        json aspects = json::array();
        for (const auto& [key, _] : ASPECT_KINDS) {
            aspects.push_back(key);
        }
        std::sort(aspects.begin(), aspects.end());
        return DuckDBToolResult::ok(std::to_string(aspects.size()) + " aspect(s) available",
            {{"aspects", aspects}});
    }

// ═══════════════════════════════════════════════════════════════════════
// Claims, policies, entities, relationship tools
// ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_query_claims(const json& params) {
        std::string subject   = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        size_t limit          = static_cast<size_t>(params.value("limit", 20));

        if (subject.empty()) return DuckDBToolResult::error("subject is required");

        std::string triplets_raw = field_store_->query_subject(subject);
        json triplets_json;
        try { triplets_json = json::parse(triplets_raw, nullptr, false); } catch (...) {}
        if (triplets_json.is_discarded() || !triplets_json.is_array()) triplets_json = json::array();

        json claims = json::array();
        for (const auto& t : triplets_json) {
            std::string pred = t.value("predicate", "");
            if (!predicate.empty() && pred != predicate) continue;
            claims.push_back({
                {"subject",   t.value("subject", subject)},
                {"predicate", pred},
                {"object",    t.value("object", "")},
            });
            if (claims.size() >= limit) break;
        }

        return DuckDBToolResult::ok(
            std::to_string(claims.size()) + " claim(s) for subject '" + subject + "'",
            {{"claims", claims}, {"count", claims.size()}});
    }

    DuckDBToolResult tool_get_policies(const json& params) {
        size_t limit = static_cast<size_t>(params.value("limit", 20));
        auto hits    = field_store_->recall_by_kind("policy", limit);
        json policies = hits_to_results_json(hits);
        return DuckDBToolResult::ok(std::to_string(policies.size()) + " policy(ies)",
            {{"policies", policies}, {"count", policies.size()}});
    }

    DuckDBToolResult tool_get_entities(const json& params) {
        std::string type = params.value("type", "");
        size_t limit     = static_cast<size_t>(params.value("limit", 20));

        auto hits = field_store_->recall_by_kind(type.empty() ? "entity" : type, limit);
        json entities = hits_to_results_json(hits);
        return DuckDBToolResult::ok(std::to_string(entities.size()) + " entit(y/ies)",
            {{"entities", entities}, {"count", entities.size()}});
    }

    DuckDBToolResult tool_get_relationship_events(const json& params) {
        std::string event_type = params.value("event_type", "");
        size_t limit           = static_cast<size_t>(params.value("limit", 20));

        std::string search = "relationship " + event_type;
        auto hits = field_store_->recall_keyword(search, limit);
        json events = hits_to_results_json(hits);
        return DuckDBToolResult::ok(std::to_string(events.size()) + " relationship event(s)",
            {{"events", events}, {"count", events.size()}});
    }

