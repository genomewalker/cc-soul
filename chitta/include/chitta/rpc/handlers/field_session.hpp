// Included into FieldRpcHandler class body — not a standalone header

    DuckDBToolResult tool_transcript_register(const json& params) {
        std::string session_id      = params.value("session_id", "");
        std::string transcript_path = params.value("transcript_path", "");
        std::string realm           = params.value("realm", "default");

        if (session_id.empty())      return DuckDBToolResult::error("session_id is required");
        if (transcript_path.empty()) return DuckDBToolResult::error("transcript_path is required");

        json payload = {
            {"path",      transcript_path},
            {"realm",     realm},
            {"last_line", 0}
        };
        uint64_t event_id = field_store_->emit_event(
            "transcript", "register", session_id, payload.dump());

        if (event_id == 0) return DuckDBToolResult::error("Failed to register transcript");

        return DuckDBToolResult::ok("Registered transcript", {
            {"session_id",      session_id},
            {"transcript_path", transcript_path},
            {"realm",           realm},
            {"event_id",        event_id}
        });
    }

    DuckDBToolResult tool_transcript_get(const json& params) {
        std::string session_id = params.value("session_id", "");
        if (session_id.empty()) return DuckDBToolResult::error("session_id is required");

        auto hits = field_store_->recall_keyword(session_id, 5);
        for (const auto& h : hits) {
            if (h.kind == "transcript" || h.content.find(session_id) != std::string::npos) {
                return DuckDBToolResult::ok("Found transcript event", {
                    {"found",      true},
                    {"session_id", session_id},
                    {"memory_id",  h.memory_id},
                    {"content",    h.content},
                    {"note",       "session state tracked via events"}
                });
            }
        }

        return DuckDBToolResult::ok("Transcript not found", {
            {"found",      false},
            {"session_id", session_id}
        });
    }

    DuckDBToolResult tool_transcript_list(const json& params) {
        size_t limit = static_cast<size_t>(params.value("limit", 50));

        // Use native cf_transcript_list if available
        std::string raw = field_store_->transcript_list(limit);
        json list_json;
        try {
            list_json = json::parse(raw, nullptr, false);
        } catch (...) {
            list_json = json::array();
        }
        if (list_json.is_discarded() || !list_json.is_array()) {
            list_json = json::array();
        }

        // Fall back to keyword recall when native list is empty
        if (list_json.empty()) {
            auto hits = field_store_->recall_keyword("transcript register", limit);
            for (const auto& h : hits) {
                list_json.push_back({
                    {"memory_id", h.memory_id},
                    {"content",   h.content},
                    {"kind",      h.kind},
                    {"realm",     h.realm}
                });
            }
        }

        std::ostringstream ss;
        ss << "Registered transcripts: " << list_json.size() << "\n";
        ss << "Note: session state tracked via events\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"transcripts", list_json},
            {"count",       list_json.size()}
        });
    }

    DuckDBToolResult tool_transcript_update(const json& params) {
        std::string session_id = params.value("session_id", "");
        int64_t last_line      = params.value("last_line", int64_t(0));

        if (session_id.empty()) return DuckDBToolResult::error("session_id is required");

        json payload = {{"last_line", last_line}};
        uint64_t event_id = field_store_->emit_event(
            "transcript", "progress", session_id, payload.dump());

        if (event_id == 0) return DuckDBToolResult::error("Failed to update transcript progress");

        return DuckDBToolResult::ok("Updated transcript progress", {
            {"session_id", session_id},
            {"last_line",  last_line},
            {"event_id",   event_id}
        });
    }

    DuckDBToolResult tool_transcript_remove(const json& params) {
        std::string session_id = params.value("session_id", "");
        if (session_id.empty()) return DuckDBToolResult::error("session_id is required");

        uint64_t event_id = field_store_->emit_event(
            "transcript", "remove", session_id, "");

        if (event_id == 0) return DuckDBToolResult::error("Failed to remove transcript");

        return DuckDBToolResult::ok("Removed transcript", {
            {"session_id", session_id},
            {"event_id",   event_id}
        });
    }

    DuckDBToolResult tool_transcript_parse(const json& params) {
        std::string session_id = params.value("session_id", "");
        size_t min_turns       = static_cast<size_t>(params.value("min_turns", 4));

        if (session_id.empty()) return DuckDBToolResult::error("session_id is required");

        // Try to locate the transcript path via glob
        std::string path;
        {
            const char* home_cstr = std::getenv("HOME");
            if (home_cstr) {
                std::string pattern = std::string(home_cstr)
                    + "/.claude/projects/*/" + session_id + ".jsonl";
                glob_t g{};
                if (glob(pattern.c_str(), GLOB_TILDE, nullptr, &g) == 0 && g.gl_pathc > 0) {
                    path = g.gl_pathv[0];
                }
                globfree(&g);
            }
        }

        if (path.empty()) {
            return DuckDBToolResult::error("Could not find transcript for session: " + session_id);
        }

        std::ifstream file(path);
        if (!file) {
            return DuckDBToolResult::error("Cannot open transcript: " + path);
        }

        std::string line;
        size_t turn_count = 0;
        int64_t line_number = 0;

        while (std::getline(file, line)) {
            ++line_number;
            if (line.empty()) continue;
            auto obj = json::parse(line, nullptr, false);
            if (obj.is_discarded()) continue;
            std::string type = obj.value("type", "");
            if (type == "user" || type == "assistant") ++turn_count;
        }

        bool ready = turn_count >= min_turns;

        std::ostringstream ss;
        ss << "Parsed " << turn_count << " turns from transcript\n";
        ss << "  Session: " << session_id << "\n";
        ss << "  Path: " << path << "\n";
        ss << "  Lines: " << line_number << "\n";
        ss << "  Ready: " << (ready ? "yes" : "no") << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id",  session_id},
            {"path",        path},
            {"turns_found", turn_count},
            {"min_turns",   min_turns},
            {"last_line",   line_number},
            {"ready",       ready}
        });
    }

    DuckDBToolResult tool_transcript_search(const json& params) {
        std::string query      = params.value("query", "");
        std::string session_id = params.value("session_id", "");
        size_t limit           = static_cast<size_t>(params.value("limit", 10));

        if (query.empty()) return DuckDBToolResult::error("query is required");

        // Semantic search via embedding
        auto embedding = embed_query(query);
        json results_json = json::array();

        if (!embedding.empty()) {
            auto hits = field_store_->recall(embedding, limit, "");
            for (const auto& h : hits) {
                if (!session_id.empty() && h.content.find(session_id) == std::string::npos) continue;
                results_json.push_back({
                    {"memory_id",  h.memory_id},
                    {"score",      h.score},
                    {"content",    h.content.size() > 500 ? h.content.substr(0, 500) + "..." : h.content},
                    {"kind",       h.kind},
                    {"realm",      h.realm}
                });
            }
        }

        // Keyword fallback / supplement
        if (results_json.size() < limit) {
            std::string kw_query = query;
            if (!session_id.empty()) kw_query = session_id + " " + query;
            auto kw_hits = field_store_->recall_keyword(kw_query, limit);
            std::unordered_set<uint64_t> seen;
            for (const auto& r : results_json) {
                seen.insert(r.value("memory_id", uint64_t(0)));
            }
            for (const auto& h : kw_hits) {
                if (seen.count(h.memory_id)) continue;
                results_json.push_back({
                    {"memory_id",  h.memory_id},
                    {"score",      h.score},
                    {"content",    h.content.size() > 500 ? h.content.substr(0, 500) + "..." : h.content},
                    {"kind",       h.kind},
                    {"realm",      h.realm}
                });
                if (results_json.size() >= limit) break;
            }
        }

        std::ostringstream ss;
        ss << "Found " << results_json.size() << " matching passages for: " << query << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"query",      query},
            {"results",    results_json},
            {"count",      results_json.size()}
        });
    }

    DuckDBToolResult tool_read_transcript(const json& params) {
        std::string path       = params.value("path", "");
        std::string session_id = params.value("session_id", "");
        int start_turn         = params.value("start_turn", 0);
        size_t limit           = static_cast<size_t>(params.value("limit", 20));
        size_t max_chars       = static_cast<size_t>(params.value("max_chars_per_turn", 500));
        std::string role_filter = params.value("role_filter", "");
        std::string keyword    = params.value("keyword", "");
        bool metadata_only     = params.value("metadata_only", false);

        // Resolve path from session_id if not provided
        if (path.empty() && !session_id.empty()) {
            const char* home_cstr = std::getenv("HOME");
            if (home_cstr) {
                std::string pattern = std::string(home_cstr)
                    + "/.claude/projects/*/" + session_id + ".jsonl";
                glob_t g{};
                if (glob(pattern.c_str(), GLOB_TILDE, nullptr, &g) == 0 && g.gl_pathc > 0) {
                    path = g.gl_pathv[0];
                }
                globfree(&g);
            }
        }

        if (path.empty()) {
            return DuckDBToolResult::error("No transcript path provided and could not find session");
        }

        std::ifstream file(path);
        if (!file) {
            return DuckDBToolResult::error("Cannot open transcript: " + path);
        }

        // First pass: collect all turns
        struct Turn {
            std::string role;
            std::string content;
            int64_t line_number;
            int turn_index;
        };

        std::vector<Turn> all_turns;
        std::string line;
        int64_t line_number = 0;
        int turn_idx = 0;

        while (std::getline(file, line)) {
            ++line_number;
            if (line.empty()) continue;
            auto obj = json::parse(line, nullptr, false);
            if (obj.is_discarded()) continue;

            std::string type = obj.value("type", "");
            if (type != "user" && type != "assistant") continue;

            std::string content;
            if (obj.contains("message")) {
                const auto& msg = obj["message"];
                if (msg.contains("content")) {
                    const auto& mc = msg["content"];
                    if (mc.is_string()) {
                        content = mc.get<std::string>();
                    } else if (mc.is_array()) {
                        for (const auto& block : mc) {
                            if (block.contains("text") && block["text"].is_string()) {
                                if (!content.empty()) content += "\n";
                                content += block["text"].get<std::string>();
                            }
                        }
                    }
                }
            }

            if (content.empty()) continue;
            all_turns.push_back({type, content, line_number, turn_idx++});
        }

        size_t total_chars = 0;
        for (const auto& t : all_turns) total_chars += t.content.size();

        json result;
        result["path"]        = path;
        result["total_turns"] = all_turns.size();
        result["total_chars"] = total_chars;
        result["last_line"]   = line_number;

        if (metadata_only) {
            std::ostringstream ss;
            ss << "Transcript: " << path << "\n"
               << "Total turns: " << all_turns.size() << "\n"
               << "Total chars: " << total_chars << "\n"
               << "Lines: " << line_number;
            return DuckDBToolResult::ok(ss.str(), result);
        }

        // Apply filters
        std::vector<const Turn*> filtered;
        for (const auto& t : all_turns) {
            if (!role_filter.empty() && t.role != role_filter) continue;
            if (!keyword.empty() && t.content.find(keyword) == std::string::npos) continue;
            filtered.push_back(&t);
        }

        result["filtered_turns"] = filtered.size();

        // Paginate
        json turns_arr = json::array();
        size_t output_chars = 0;
        const size_t max_output_chars = 30000;

        for (size_t i = static_cast<size_t>(start_turn);
             i < filtered.size() && turns_arr.size() < limit; ++i) {
            const Turn& t = *filtered[i];

            std::string content = t.content;
            if (max_chars > 0 && content.size() > max_chars) {
                content = content.substr(0, max_chars) + "...";
            }

            if (output_chars + content.size() > max_output_chars) {
                turns_arr.push_back({
                    {"role",       "system"},
                    {"content",    "[output truncated — use start_turn=" + std::to_string(i) + " to continue]"},
                    {"turn_index", -1}
                });
                break;
            }

            output_chars += content.size();
            turns_arr.push_back({
                {"role",        t.role},
                {"content",     content},
                {"turn_index",  t.turn_index},
                {"line_number", t.line_number}
            });
        }

        result["turns"]      = turns_arr;
        result["returned"]   = turns_arr.size();
        result["start_turn"] = start_turn;

        std::ostringstream ss;
        ss << "Transcript: " << path << "\n"
           << "Total: " << all_turns.size() << " turns";
        if (!role_filter.empty() || !keyword.empty()) {
            ss << " (filtered: " << filtered.size() << ")";
        }
        ss << "\nShowing turns " << start_turn
           << "-" << (start_turn + static_cast<int>(turns_arr.size()) - 1) << ":\n\n";

        for (const auto& t : turns_arr) {
            ss << "[" << t["role"].get<std::string>() << "] ";
            std::string c = t["content"].get<std::string>();
            ss << (c.size() > 100 ? c.substr(0, 100) + "..." : c) << "\n";
        }

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_get_turns(const json& params) {
        std::string session_id = params.value("session_id", "");
        int start_index        = params.value("start_index", 0);
        size_t limit           = static_cast<size_t>(params.value("limit", 50));

        if (session_id.empty()) return DuckDBToolResult::error("session_id is required");

        // Locate transcript path
        std::string path;
        {
            const char* home_cstr = std::getenv("HOME");
            if (home_cstr) {
                std::string pattern = std::string(home_cstr)
                    + "/.claude/projects/*/" + session_id + ".jsonl";
                glob_t g{};
                if (glob(pattern.c_str(), GLOB_TILDE, nullptr, &g) == 0 && g.gl_pathc > 0) {
                    path = g.gl_pathv[0];
                }
                globfree(&g);
            }
        }

        if (path.empty()) {
            return DuckDBToolResult::ok("Transcript not found for session", {
                {"session_id", session_id},
                {"turns",      json::array()},
                {"count",      0}
            });
        }

        // Delegate to tool_read_transcript with adjusted params
        json rp = {
            {"path",        path},
            {"session_id",  session_id},
            {"start_turn",  start_index},
            {"limit",       static_cast<int>(limit)}
        };
        return tool_read_transcript(rp);
    }

    DuckDBToolResult tool_create_episode(const json& params) {
        std::string session_id   = params.value("session_id", "");
        std::string title        = params.value("title", "");
        int start_turn           = params.value("start_turn", 0);
        int end_turn             = params.value("end_turn", 0);
        std::string episode_type = params.value("episode_type", "distillation");
        std::string realm        = params.value("realm", "brahman");

        if (session_id.empty() || title.empty()) {
            return DuckDBToolResult::error("session_id and title are required");
        }

        std::string content = title + ": turns " + std::to_string(start_turn)
            + "-" + std::to_string(end_turn);

        auto embedding = embed_text(content);

        uint64_t episode_id = field_store_->remember(
            "episode", realm, content, embedding, 0.8f, 0.0f);

        // Link episode to session via triplet
        if (episode_id > 0) {
            field_store_->add_triplet(
                std::to_string(episode_id), "derived_from", session_id);
        }

        std::ostringstream msg;
        msg << "Created episode " << episode_id << ": " << title
            << " (turns " << start_turn << "-" << end_turn << ")";

        return DuckDBToolResult::ok(msg.str(), {
            {"episode_id",   episode_id},
            {"session_id",   session_id},
            {"title",        title},
            {"start_turn",   start_turn},
            {"end_turn",     end_turn},
            {"episode_type", episode_type},
            {"realm",        realm}
        });
    }

    DuckDBToolResult tool_msg_send(const json& params) {
        std::string target  = params.value("target", "");
        std::string content = params.value("content", "");

        if (target.empty())  return DuckDBToolResult::error("target is required");
        if (content.empty()) return DuckDBToolResult::error("content is required");

        std::string sender_session_id = params.value("sender_session_id", get_session_id());
        int priority                  = params.value("priority", 1);
        std::string content_type      = params.value("content_type", "text");

        json payload = {
            {"content",           content},
            {"sender_session_id", sender_session_id},
            {"priority",          priority},
            {"content_type",      content_type}
        };

        uint64_t event_id = field_store_->emit_event(
            "msg", "send", target, payload.dump());

        if (event_id == 0) return DuckDBToolResult::error("Failed to send message");

        std::ostringstream ss;
        ss << "Message sent to " << target << " (event #" << event_id << ")";

        return DuckDBToolResult::ok(ss.str(), {
            {"message_id",        event_id},
            {"target",            target},
            {"priority",          priority},
            {"sender_session_id", sender_session_id}
        });
    }

    DuckDBToolResult tool_msg_inbox(const json& params) {
        std::string session_id = params.value("session_id", get_session_id());
        size_t limit           = static_cast<size_t>(params.value("limit", 20));

        auto events_json_str = field_store_->get_events_by_target("msg", "send", session_id, limit);

        json events = json::parse(events_json_str, nullptr, false);
        if (events.is_discarded() || !events.is_array()) {
            return DuckDBToolResult::ok("No messages found", {
                {"session_id", session_id},
                {"count",      0},
                {"messages",   json::array()}
            });
        }

        json msg_list = json::array();
        for (const auto& ev : events) {
            json payload = ev.value("payload", json::object());
            msg_list.push_back({
                {"memory_id",         ev.value("event_id", 0)},
                {"content",           payload.value("content", "")},
                {"sender_session_id", payload.value("sender_session_id", "")},
                {"content_type",      payload.value("content_type", "text")},
                {"score",             payload.value("priority", 1)},
                {"ts_ms",             ev.value("ts_ms", 0)}
            });
        }

        if (msg_list.empty()) {
            return DuckDBToolResult::ok("No messages found", {
                {"session_id", session_id},
                {"count",      0},
                {"messages",   json::array()}
            });
        }

        std::ostringstream ss;
        ss << msg_list.size() << " message(s) found for " << session_id << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"count",      msg_list.size()},
            {"messages",   msg_list}
        });
    }

    DuckDBToolResult tool_msg_ack(const json& params) {
        std::string message_id = params.value("message_id", "");
        if (message_id.empty()) {
            // Try numeric
            auto [id, id_str] = parse_id(params, "message_id");
            if (id <= 0) return DuckDBToolResult::error("message_id is required");
            message_id = id_str;
        }

        uint64_t event_id = field_store_->emit_event(
            "msg", "ack", message_id, "");

        return DuckDBToolResult::ok("Message acknowledged", {
            {"message_id", message_id},
            {"event_id",   event_id}
        });
    }

    DuckDBToolResult tool_msg_ack_all(const json& params) {
        std::string session_id = params.value("session_id", get_session_id());

        uint64_t event_id = field_store_->emit_event(
            "msg", "ack_all", session_id, "");

        return DuckDBToolResult::ok("All messages acknowledged", {
            {"session_id", session_id},
            {"event_id",   event_id}
        });
    }

    DuckDBToolResult tool_msg_history(const json& params) {
        std::string session_id = params.value("session_id", get_session_id());
        size_t limit           = static_cast<size_t>(params.value("limit", 30));

        auto events_json_str = field_store_->get_events_by_target("msg", "send", session_id, limit);

        json events = json::parse(events_json_str, nullptr, false);
        if (events.is_discarded() || !events.is_array()) events = json::array();

        json msg_list = json::array();
        for (const auto& ev : events) {
            json payload = ev.value("payload", json::object());
            msg_list.push_back({
                {"memory_id",         ev.value("event_id", 0)},
                {"content",           payload.value("content", "")},
                {"sender_session_id", payload.value("sender_session_id", "")},
                {"content_type",      payload.value("content_type", "text")},
                {"score",             payload.value("priority", 1)},
                {"ts_ms",             ev.value("ts_ms", 0)},
                {"kind",              "msg_send"}
            });
        }

        if (msg_list.empty()) {
            return DuckDBToolResult::ok("No message history", {
                {"session_id", session_id},
                {"count",      0},
                {"messages",   json::array()}
            });
        }

        std::ostringstream ss;
        ss << "Message history (" << msg_list.size() << " messages):\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"count",      msg_list.size()},
            {"messages",   msg_list}
        });
    }

    DuckDBToolResult tool_session_register(const json& params) {
        std::string session_id      = params.value("session_id", get_session_id());
        std::string realm           = params.value("realm", "brahman");
        std::string transcript_path = params.value("transcript_path", "");
        std::string project_dir     = params.value("project_dir", "");
        std::string metadata        = params.value("metadata", "{}");
        int32_t pid = params.contains("pid")
            ? params["pid"].get<int32_t>()
            : static_cast<int32_t>(getpid());

        json payload = {
            {"realm",           realm},
            {"pid",             pid},
            {"transcript_path", transcript_path},
            {"project_dir",     project_dir},
            {"metadata",        metadata}
        };

        uint64_t event_id = field_store_->emit_event(
            "session", "register", session_id, payload.dump());

        if (event_id == 0) return DuckDBToolResult::error("Failed to register session");

        return DuckDBToolResult::ok("Session registered", {
            {"session_id",      session_id},
            {"realm",           realm},
            {"pid",             pid},
            {"transcript_path", transcript_path},
            {"event_id",        event_id}
        });
    }

    DuckDBToolResult tool_session_heartbeat(const json& params) {
        std::string session_id = params.value("session_id", get_session_id());
        std::string metadata   = params.value("metadata", "");

        uint64_t event_id = field_store_->emit_event(
            "session", "heartbeat", session_id, metadata);

        if (event_id == 0) return DuckDBToolResult::error("Failed to send heartbeat");

        return DuckDBToolResult::ok("Heartbeat sent", {
            {"session_id", session_id},
            {"event_id",   event_id}
        });
    }

    DuckDBToolResult tool_session_list(const json& params) {
        bool active_only = params.value("active_only", false);

        std::string raw = field_store_->session_list(active_only);
        json sessions_json;
        try {
            sessions_json = json::parse(raw, nullptr, false);
        } catch (...) {
            sessions_json = json::array();
        }
        if (sessions_json.is_discarded() || !sessions_json.is_array()) {
            sessions_json = json::array();
        }

        // Fall back to keyword recall when native list is empty
        if (sessions_json.empty()) {
            auto hits = field_store_->recall_keyword("session register", 20);
            for (const auto& h : hits) {
                sessions_json.push_back({
                    {"memory_id", h.memory_id},
                    {"content",   h.content},
                    {"score",     h.score}
                });
            }
        }

        if (sessions_json.empty()) {
            return DuckDBToolResult::ok("No sessions found", {
                {"count",    0},
                {"sessions", json::array()},
                {"note",     "session state tracked via events"}
            });
        }

        std::ostringstream ss;
        ss << sessions_json.size() << " session(s) found\n";
        ss << "Note: session state tracked via events\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"count",    sessions_json.size()},
            {"sessions", sessions_json}
        });
    }

    DuckDBToolResult tool_session_deregister(const json& params) {
        std::string session_id = params.value("session_id", get_session_id());
        if (session_id.empty()) return DuckDBToolResult::error("session_id is required");

        uint64_t event_id = field_store_->emit_event(
            "session", "deregister", session_id, "");

        if (event_id == 0) return DuckDBToolResult::error("Failed to deregister session");

        return DuckDBToolResult::ok("Session deregistered", {
            {"session_id", session_id},
            {"event_id",   event_id}
        });
    }

    DuckDBToolResult tool_session_sync(const json& params) {
        std::string projects_dir = params.value("projects_dir", "");

        if (projects_dir.empty()) {
            const char* env = std::getenv("CLAUDE_PROJECTS_DIR");
            if (env) {
                projects_dir = env;
            } else {
                const char* home_cstr = std::getenv("HOME");
                if (home_cstr) projects_dir = std::string(home_cstr) + "/.claude/projects";
            }
        }

        size_t discovered = 0;
        if (!projects_dir.empty() && std::filesystem::exists(projects_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(projects_dir)) {
                if (!entry.is_directory()) continue;
                for (const auto& file : std::filesystem::directory_iterator(entry.path())) {
                    if (file.path().extension() == ".jsonl") ++discovered;
                }
            }
        }

        json payload = {
            {"projects_dir", projects_dir},
            {"discovered",   discovered}
        };
        field_store_->emit_event("session", "sync", "all", payload.dump());

        std::ostringstream ss;
        ss << "Session sync: discovered " << discovered << " transcript(s) in " << projects_dir;

        return DuckDBToolResult::ok(ss.str(), {
            {"projects_dir", projects_dir},
            {"discovered",   discovered}
        });
    }
