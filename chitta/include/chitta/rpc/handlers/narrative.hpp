// Included into DuckDBHandler class body — not a standalone header

    DuckDBToolResult tool_anticipation_observe(const json& params) {
        std::string context = params.value("context", "");
        std::string action = params.value("action", "");
        std::string realm = params.value("realm", "brahman");

        if (context.empty() || action.empty()) {
            return DuckDBToolResult::error("context and action are required");
        }

        int64_t id = mind_->store().anticipation_observe(context, action, realm);
        if (id <= 0) {
            return DuckDBToolResult::error("Failed to record pattern: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Pattern recorded (id: " + std::to_string(id) + ")", {{"id", id}});
    }

    DuckDBToolResult tool_anticipation_predict(const json& params) {
        std::string context = params.value("context", "");
        size_t limit = params.value("limit", 5);
        float min_confidence = params.value("min_confidence", 0.3f);
        std::string realm = params.value("realm", "");

        if (context.empty()) {
            return DuckDBToolResult::error("context is required");
        }

        auto patterns = mind_->store().anticipation_predict(context, limit * 2, realm);  // Fetch more for filtering

        std::ostringstream ss;
        ss << "Predicted Actions for Context\n";
        ss << "══════════════════════════════\n\n";

        json patterns_json = json::array();
        size_t count = 0;
        for (const auto& p : patterns) {
            float success_rate = p.frequency > 0 ? (float)p.success_count / p.frequency : 0;
            // Filter by min_confidence (success rate)
            if (success_rate < min_confidence) continue;

            ss << "• " << p.action << "\n";
            ss << "  Context: " << p.context.substr(0, 80) << (p.context.length() > 80 ? "..." : "") << "\n";
            ss << "  Frequency: " << p.frequency << " | Success: " << std::fixed << std::setprecision(0) << (success_rate * 100) << "%\n\n";

            patterns_json.push_back({
                {"id", p.id},
                {"context", p.context},
                {"action", p.action},
                {"frequency", p.frequency},
                {"success_count", p.success_count},
                {"confidence", success_rate},
                {"realm", p.realm}
            });

            if (++count >= limit) break;
        }

        if (patterns_json.empty()) {
            ss << "No matching patterns found.\n";
        }

        return DuckDBToolResult::ok(ss.str(), {{"patterns", patterns_json}});
    }

    DuckDBToolResult tool_anticipation_success(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        if (!mind_->store().anticipation_success(id)) {
            return DuckDBToolResult::error("Failed to mark success");
        }

        return DuckDBToolResult::ok("Pattern #" + std::to_string(id) + " marked successful");
    }

    DuckDBToolResult tool_anticipation_list(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit = params.value("limit", 20);
        std::string sort_by = params.value("sort_by", "frequency");

        auto patterns = mind_->store().anticipation_list(realm, limit);

        // Apply sorting
        if (sort_by == "confidence") {
            std::sort(patterns.begin(), patterns.end(), [](const auto& a, const auto& b) {
                float rate_a = a.frequency > 0 ? (float)a.success_count / a.frequency : 0;
                float rate_b = b.frequency > 0 ? (float)b.success_count / b.frequency : 0;
                return rate_a > rate_b;
            });
        } else if (sort_by == "created_at") {
            std::sort(patterns.begin(), patterns.end(), [](const auto& a, const auto& b) {
                return a.created_at > b.created_at;
            });
        }
        // Default: frequency (already sorted by store)

        std::ostringstream ss;
        ss << "Learned Anticipation Patterns\n";
        ss << "══════════════════════════════\n\n";

        if (patterns.empty()) {
            ss << "No patterns learned yet.\n";
        } else {
            for (const auto& p : patterns) {
                float success_rate = p.frequency > 0 ? (float)p.success_count / p.frequency * 100 : 0;
                ss << "#" << p.id << " [" << p.realm << "]\n";
                ss << "  Context: " << p.context.substr(0, 60) << (p.context.length() > 60 ? "..." : "") << "\n";
                ss << "  Action: " << p.action.substr(0, 60) << (p.action.length() > 60 ? "..." : "") << "\n";
                ss << "  Freq: " << p.frequency << " | Success: " << std::fixed << std::setprecision(0) << success_rate << "%\n\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", patterns.size()}});
    }

    DuckDBToolResult tool_habit_observe(const json& params) {
        std::string trigger = params.value("trigger", "");
        std::string response = params.value("response", "");
        std::string realm = params.value("realm", "brahman");

        if (trigger.empty() || response.empty()) {
            return DuckDBToolResult::error("trigger and response are required");
        }

        int64_t id = mind_->store().habit_observe(trigger, response, realm);
        if (id <= 0) {
            return DuckDBToolResult::error("Failed to record habit: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Habit recorded/strengthened (id: " + std::to_string(id) + ")", {{"id", id}});
    }

    DuckDBToolResult tool_habit_match(const json& params) {
        std::string context = params.value("context", "");
        float min_strength = params.value("min_strength", 0.3f);
        std::string realm = params.value("realm", "");

        if (context.empty()) {
            return DuckDBToolResult::error("context is required");
        }

        auto habits = mind_->store().habit_match(context, min_strength, realm);

        std::ostringstream ss;
        ss << "Matching Habits\n";
        ss << "═══════════════\n\n";

        if (habits.empty()) {
            ss << "No matching habits found.\n";
        } else {
            for (const auto& h : habits) {
                ss << "• " << h.trigger_pattern << " → " << h.response << "\n";
                ss << "  Strength: " << std::fixed << std::setprecision(2) << h.strength;
                ss << " | Frequency: " << h.frequency << "\n\n";
            }
        }

        json habits_json = json::array();
        for (const auto& h : habits) {
            habits_json.push_back({
                {"id", h.id},
                {"trigger", h.trigger_pattern},
                {"response", h.response},
                {"strength", h.strength},
                {"frequency", h.frequency},
                {"realm", h.realm}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"habits", habits_json}});
    }

    DuckDBToolResult tool_habit_strengthen(const json& params) {
        auto [id, id_str] = parse_id(params);
        float amount = params.value("amount", 0.1f);

        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        if (!mind_->store().habit_strengthen(id, amount)) {
            return DuckDBToolResult::error("Failed to strengthen habit");
        }

        return DuckDBToolResult::ok("Habit #" + std::to_string(id) + " strengthened by " + std::to_string(amount));
    }

    DuckDBToolResult tool_habit_weaken(const json& params) {
        auto [id, id_str] = parse_id(params);
        float amount = params.value("amount", 0.05f);

        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        if (!mind_->store().habit_weaken(id, amount)) {
            return DuckDBToolResult::error("Failed to weaken habit");
        }

        return DuckDBToolResult::ok("Habit #" + std::to_string(id) + " weakened by " + std::to_string(amount));
    }

    DuckDBToolResult tool_habit_list(const json& params) {
        std::string realm = params.value("realm", "");
        float min_strength = params.value("min_strength", 0.0f);
        size_t limit = params.value("limit", 20);
        std::string sort_by = params.value("sort_by", "strength");

        auto habits = mind_->store().habit_list(realm, min_strength, limit);

        // Apply sorting
        if (sort_by == "frequency") {
            std::sort(habits.begin(), habits.end(), [](const auto& a, const auto& b) {
                return a.frequency > b.frequency;
            });
        } else if (sort_by == "created_at") {
            std::sort(habits.begin(), habits.end(), [](const auto& a, const auto& b) {
                return a.created_at > b.created_at;
            });
        }
        // Default: strength (already sorted by store)

        std::ostringstream ss;
        ss << "Formed Habits\n";
        ss << "══════════════\n\n";

        if (habits.empty()) {
            ss << "No habits formed yet.\n";
        } else {
            for (const auto& h : habits) {
                int strength_bars = static_cast<int>(h.strength * 10);
                std::string bar(strength_bars, '#');
                bar += std::string(10 - strength_bars, '-');

                ss << "#" << h.id << " [" << bar << "] " << std::fixed << std::setprecision(2) << h.strength << "\n";
                ss << "  " << h.trigger_pattern << " → " << h.response << "\n";
                ss << "  Realm: " << h.realm << " | Freq: " << h.frequency << "\n\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", habits.size()}});
    }

    DuckDBToolResult tool_narrative_status(const json& params) {
        std::string session_id = get_session_id(params);

        auto& store = mind_->store();

        // Get current segment
        auto segment = store.segment_current(session_id);
        if (!segment) {
            return DuckDBToolResult::ok("No active session segment", {
                {"session_id", session_id},
                {"mode", "unknown"},
                {"confidence", 0.0f},
                {"segment_id", 0}
            });
        }

        std::ostringstream ss;
        ss << "Mode: " << work_mode_to_string(segment->mode)
           << " (" << std::fixed << std::setprecision(2) << segment->confidence << ")\n";
        ss << "Segment: " << segment->event_count << " events";

        // Parse files_active JSON array
        if (!segment->files_active.empty() && segment->files_active != "[]") {
            size_t count = std::count(segment->files_active.begin(), segment->files_active.end(), '"') / 2;
            ss << ", " << count << " files active";
        }

        // Parse tools_used JSON array
        if (!segment->tools_used.empty() && segment->tools_used != "[]") {
            size_t count = std::count(segment->tools_used.begin(), segment->tools_used.end(), '"') / 2;
            ss << ", " << count << " tools used";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"mode", work_mode_to_string(segment->mode)},
            {"confidence", segment->confidence},
            {"segment_id", segment->id},
            {"event_count", segment->event_count},
            {"started_at", segment->started_at},
            {"tools_used", segment->tools_used},
            {"files_active", segment->files_active}
        });
    }

    DuckDBToolResult tool_narrative_log(const json& params) {
        std::string session_id = get_session_id(params);
        std::string kind_str = params.value("kind", "");
        std::string summary = params.value("summary", "");

        if (kind_str.empty() || summary.empty()) {
            return DuckDBToolResult::error("kind and summary are required");
        }

        SessionEvent event;
        event.session_id = session_id;
        event.kind = string_to_session_event_kind(kind_str);
        event.summary = summary;
        event.tool_name = params.value("tool_name", "");
        event.success = params.value("success", true);
        event.payload = params.value("payload", "");
        event.files_mentioned = params.value("files_mentioned", "[]");
        event.realm = params.value("realm", "brahman");

        // Use NarrativeEngine to record event (handles mode inference and segments)
        auto* narrative = mind_->narrative();
        if (!narrative) {
            return DuckDBToolResult::error("Narrative engine not initialized");
        }

        // Append event to log first
        auto& store = mind_->store();
        int64_t event_id = store.event_log_append(event);
        if (event_id <= 0) {
            return DuckDBToolResult::error("Failed to append event: " + store.last_error());
        }

        // Then evaluate mode (updates segments)
        WorkMode mode = narrative->evaluate(session_id, event);

        return DuckDBToolResult::ok("Event logged, mode: " + work_mode_to_string(mode), {
            {"session_id", session_id},
            {"kind", kind_str},
            {"mode", work_mode_to_string(mode)}
        });
    }

    DuckDBToolResult tool_narrative_history(const json& params) {
        std::string session_id = get_session_id(params);

        size_t limit = params.value("limit", 20);
        auto& store = mind_->store();
        auto segments = store.segment_history(session_id, limit);

        std::ostringstream ss;
        ss << segments.size() << " segments for session " << session_id << ":\n\n";

        json segment_list = json::array();
        for (const auto& seg : segments) {
            ss << "- " << work_mode_to_string(seg.mode) << " ("
               << std::fixed << std::setprecision(2) << seg.confidence << "): "
               << seg.event_count << " events";
            if (seg.status == "open") ss << " [active]";
            ss << "\n";

            segment_list.push_back({
                {"id", seg.id},
                {"mode", work_mode_to_string(seg.mode)},
                {"confidence", seg.confidence},
                {"event_count", seg.event_count},
                {"started_at", seg.started_at},
                {"ended_at", seg.ended_at},
                {"status", seg.status}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"count", segments.size()},
            {"segments", segment_list}
        });
    }

    DuckDBToolResult tool_anticipation_filter(const json& params) {
        std::string session_id = params.value("session_id", "");
        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        size_t max = params.value("max", 2);
        auto& store = mind_->store();

        // First, generate new candidates using the Anticipator
        auto* anticipator = mind_->anticipator();
        if (anticipator) {
            anticipator->generate(session_id);
        }

        // Get pending candidates
        auto candidates = store.candidate_pending(session_id, 10);

        std::vector<AnticipationCandidate> surfaceable;
        for (const auto& c : candidates) {
            if (surfaceable.size() >= max) break;
            if (store.gate_allows(session_id, c.confidence)) {
                // Mark as surfaced before returning
                store.candidate_surface(c.id);
                surfaceable.push_back(c);
            }
        }

        if (surfaceable.empty()) {
            return DuckDBToolResult::ok("No predictions pass the annoyance gate", {
                {"session_id", session_id},
                {"count", 0},
                {"candidates", json::array()}
            });
        }

        std::ostringstream ss;
        ss << surfaceable.size() << " prediction(s) ready to surface:\n\n";

        json cand_list = json::array();
        for (const auto& c : surfaceable) {
            ss << "- [" << anticipation_source_to_string(c.source) << "] "
               << c.prediction << " (conf: "
               << std::fixed << std::setprecision(2) << c.confidence << ")\n";

            cand_list.push_back({
                {"id", c.id},
                {"prediction", c.prediction},
                {"source", anticipation_source_to_string(c.source)},
                {"confidence", c.confidence},
                {"current_mode", c.current_mode},
                {"evidence", c.evidence}
            });
        }

        ss << "\nUse anticipation_record_outcome to record feedback.";

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"count", surfaceable.size()},
            {"candidates", cand_list}
        });
    }

    DuckDBToolResult tool_anticipation_gate_status(const json& params) {
        std::string session_id = params.value("session_id", "");
        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        auto& store = mind_->store();
        auto gate = store.gate_get(session_id);

        if (!gate) {
            // Initialize gate if not exists
            store.gate_init(session_id);
            gate = store.gate_get(session_id);
        }

        if (!gate) {
            return DuckDBToolResult::error("Failed to get or create gate state");
        }

        std::ostringstream ss;
        ss << "Annoyance Gate for session " << session_id << ":\n\n";
        ss << "Budget remaining: " << gate->budget_remaining << "/5\n";
        ss << "Confidence floor: " << std::fixed << std::setprecision(2) << gate->confidence_floor << "\n";
        ss << "Cooldown: " << gate->cooldown_ms / 1000 << "s\n";
        ss << "Predictions surfaced: " << gate->predictions_surfaced << "\n";
        ss << "Correct: " << gate->predictions_correct << " / Incorrect: " << gate->predictions_incorrect << "\n";

        float accuracy = gate->predictions_surfaced > 0 ?
            static_cast<float>(gate->predictions_correct) / gate->predictions_surfaced : 0.0f;
        ss << "Accuracy: " << std::fixed << std::setprecision(1) << (accuracy * 100) << "%";

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"budget_remaining", gate->budget_remaining},
            {"confidence_floor", gate->confidence_floor},
            {"cooldown_ms", gate->cooldown_ms},
            {"predictions_surfaced", gate->predictions_surfaced},
            {"predictions_correct", gate->predictions_correct},
            {"predictions_incorrect", gate->predictions_incorrect},
            {"last_surfaced_at", gate->last_surfaced_at}
        });
    }

    DuckDBToolResult tool_anticipation_record_outcome(const json& params) {
        auto [candidate_id, _] = parse_id(params, "candidate_id");
        if (candidate_id <= 0) {
            return DuckDBToolResult::error("candidate_id is required");
        }

        bool correct = params.value("correct", false);
        auto& store = mind_->store();

        // Get candidate to find session
        auto candidate = store.candidate_get(candidate_id);
        if (!candidate) {
            return DuckDBToolResult::error("Candidate not found: " + std::to_string(candidate_id));
        }

        std::string outcome = correct ? "correct" : "incorrect";
        if (!store.candidate_resolve(candidate_id, outcome)) {
            return DuckDBToolResult::error("Failed to resolve candidate");
        }

        if (!store.gate_record_outcome(candidate->session_id, correct)) {
            return DuckDBToolResult::error("Failed to update gate state");
        }

        // Feed calibration system
        std::string domain = "anticipation";
        if (candidate) {
            domain = "anticipation:" + candidate->current_mode;
        }
        mind_->store().calibration_record(domain, correct);

        std::ostringstream ss;
        ss << "Recorded outcome: " << outcome << " for prediction \"" << candidate->prediction << "\"";

        return DuckDBToolResult::ok(ss.str(), {
            {"candidate_id", candidate_id},
            {"outcome", outcome},
            {"prediction", candidate->prediction},
            {"session_id", candidate->session_id}
        });
    }
