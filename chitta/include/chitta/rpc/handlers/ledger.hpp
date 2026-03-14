// Included into DuckDBHandler class body — not a standalone header

    DuckDBToolResult tool_ledger_save(const json& params) {
        std::string session_id = get_session_id(params);

        LedgerEntry entry;
        entry.session_id = session_id;
        entry.project = params.value("project", "default");
        entry.transcript_path = params.value("transcript_path", "");
        entry.mood = params.value("mood", "");
        entry.coherence = params.value("coherence", 0.0f);
        entry.confidence = params.value("confidence", 0.0f);

        // Convert arrays to JSON strings
        if (params.contains("todos") && params["todos"].is_array()) {
            entry.todos = params["todos"].dump();
        }
        if (params.contains("active_files") && params["active_files"].is_array()) {
            entry.active_files = params["active_files"].dump();
        }
        if (params.contains("decisions") && params["decisions"].is_array()) {
            entry.decisions = params["decisions"].dump();
        }
        if (params.contains("next_steps") && params["next_steps"].is_array()) {
            entry.next_steps = params["next_steps"].dump();
        }
        if (params.contains("blockers") && params["blockers"].is_array()) {
            entry.blockers = params["blockers"].dump();
        }
        if (params.contains("discoveries") && params["discoveries"].is_array()) {
            entry.discoveries = params["discoveries"].dump();
        }

        entry.snapshot = params.value("snapshot", "");

        int64_t id = mind_->store().save_ledger(entry);
        if (id < 0) {
            return DuckDBToolResult::error("Failed to save checkpoint");
        }

        std::ostringstream ss;
        ss << "Checkpoint saved:\n";
        ss << "  ID: " << id << "\n";
        ss << "  Session: " << session_id << "\n";
        ss << "  Project: " << entry.project << "\n";
        if (!entry.mood.empty()) ss << "  Mood: " << entry.mood << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"id", id},
            {"session_id", session_id},
            {"project", entry.project}
        });
    }

    DuckDBToolResult tool_ledger_load(const json& params) {
        std::string session_id = params.value("session_id", "");
        std::string project = params.value("project", "");

        auto entry = mind_->store().load_ledger(session_id, project);
        if (!entry) {
            return DuckDBToolResult::ok("No checkpoint found", {{"found", false}});
        }

        std::ostringstream ss;
        ss << "Checkpoint loaded:\n";
        ss << "  ID: " << entry->id << "\n";
        ss << "  Session: " << entry->session_id << "\n";
        ss << "  Project: " << entry->project << "\n";
        if (!entry->transcript_path.empty()) ss << "  Transcript: " << entry->transcript_path << "\n";
        if (!entry->mood.empty()) ss << "  Mood: " << entry->mood << "\n";
        if (entry->coherence > 0) ss << "  Coherence: " << entry->coherence << "\n";
        if (entry->confidence > 0) ss << "  Confidence: " << entry->confidence << "\n";

        // Parse JSON fields back to arrays for structured output
        json result = {
            {"found", true},
            {"id", entry->id},
            {"session_id", entry->session_id},
            {"project", entry->project},
            {"transcript_path", entry->transcript_path},
            {"created_at", entry->created_at},
            {"mood", entry->mood},
            {"coherence", entry->coherence},
            {"confidence", entry->confidence},
            {"snapshot", entry->snapshot}
        };

        // Parse JSON strings back to JSON arrays
        auto parse_json_field = [](const std::string& s) -> json {
            if (s.empty()) return json::array();
            try {
                return json::parse(s);
            } catch (...) {
                return json::array();
            }
        };

        result["todos"] = parse_json_field(entry->todos);
        result["active_files"] = parse_json_field(entry->active_files);
        result["decisions"] = parse_json_field(entry->decisions);
        result["next_steps"] = parse_json_field(entry->next_steps);
        result["blockers"] = parse_json_field(entry->blockers);
        result["discoveries"] = parse_json_field(entry->discoveries);

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_ledger_list(const json& params) {
        std::string project = params.value("project", "");
        size_t limit = params.value("limit", 10);

        auto entries = mind_->store().list_ledgers(project, limit);

        std::ostringstream ss;
        ss << "Checkpoints";
        if (!project.empty()) ss << " for project '" << project << "'";
        ss << " (" << entries.size() << "):\n\n";

        json entries_json = json::array();
        for (const auto& entry : entries) {
            // Format timestamp
            auto ts = entry.created_at / 1000;
            std::time_t t = static_cast<std::time_t>(ts);
            char time_buf[32];
            std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", std::localtime(&t));

            ss << "  [" << entry.id << "] " << time_buf << " - " << entry.session_id;
            if (!entry.mood.empty()) ss << " (" << entry.mood << ")";
            ss << "\n";

            entries_json.push_back({
                {"id", entry.id},
                {"session_id", entry.session_id},
                {"project", entry.project},
                {"transcript_path", entry.transcript_path},
                {"created_at", entry.created_at},
                {"mood", entry.mood}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"entries", entries_json}, {"count", entries.size()}});
    }

    DuckDBToolResult tool_ledger_get(const json& params) {
        int64_t id = params.value("id", 0LL);
        if (id <= 0) {
            return DuckDBToolResult::error("Valid ID is required");
        }

        auto entry = mind_->store().get_ledger(id);
        if (!entry) {
            return DuckDBToolResult::error("Checkpoint not found: " + std::to_string(id));
        }

        std::ostringstream ss;
        ss << "Checkpoint " << id << ":\n";
        ss << "  Session: " << entry->session_id << "\n";
        ss << "  Project: " << entry->project << "\n";
        if (!entry->transcript_path.empty()) ss << "  Transcript: " << entry->transcript_path << "\n";
        if (!entry->mood.empty()) ss << "  Mood: " << entry->mood << "\n";
        if (entry->coherence > 0) ss << "  Coherence: " << entry->coherence << "\n";
        if (entry->confidence > 0) ss << "  Confidence: " << entry->confidence << "\n";
        if (!entry->snapshot.empty()) {
            ss << "\nSnapshot:\n" << entry->snapshot << "\n";
        }

        auto parse_json_field = [](const std::string& s) -> json {
            if (s.empty()) return json::array();
            try {
                return json::parse(s);
            } catch (...) {
                return json::array();
            }
        };

        json result = {
            {"id", entry->id},
            {"session_id", entry->session_id},
            {"project", entry->project},
            {"transcript_path", entry->transcript_path},
            {"created_at", entry->created_at},
            {"mood", entry->mood},
            {"coherence", entry->coherence},
            {"confidence", entry->confidence},
            {"snapshot", entry->snapshot},
            {"todos", parse_json_field(entry->todos)},
            {"active_files", parse_json_field(entry->active_files)},
            {"decisions", parse_json_field(entry->decisions)},
            {"next_steps", parse_json_field(entry->next_steps)},
            {"blockers", parse_json_field(entry->blockers)},
            {"discoveries", parse_json_field(entry->discoveries)}
        };

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_ledger_delete(const json& params) {
        int64_t id = params.value("id", 0LL);
        if (id <= 0) {
            return DuckDBToolResult::error("Valid ID is required");
        }

        bool ok = mind_->store().delete_ledger(id);
        if (!ok) {
            return DuckDBToolResult::error("Failed to delete checkpoint " + std::to_string(id));
        }

        return DuckDBToolResult::ok("Deleted checkpoint " + std::to_string(id), {{"id", id}, {"deleted", true}});
    }
