// Included into DuckDBHandler class body — not a standalone header

    DuckDBToolResult tool_transcript_register(const json& params) {
        std::string session_id = params.value("session_id", "");
        std::string transcript_path = params.value("transcript_path", "");
        std::string realm = params.value("realm", "default");

        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }
        if (transcript_path.empty()) {
            return DuckDBToolResult::error("transcript_path is required");
        }

        bool ok = mind_->store().register_transcript(session_id, transcript_path, realm);
        if (!ok) {
            return DuckDBToolResult::error("Failed to register transcript");
        }

        return DuckDBToolResult::ok("Registered transcript", {
            {"session_id", session_id},
            {"transcript_path", transcript_path},
            {"realm", realm}
        });
    }

    DuckDBToolResult tool_transcript_get(const json& params) {
        std::string session_id = params.value("session_id", "");

        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        auto state = mind_->store().get_transcript(session_id);
        if (!state) {
            return DuckDBToolResult::ok("Transcript not found", {{"found", false}});
        }

        std::ostringstream ss;
        ss << "Transcript: " << state->session_id << "\n";
        ss << "  Path: " << state->transcript_path << "\n";
        ss << "  Realm: " << state->realm << "\n";
        ss << "  Last processed line: " << state->last_processed_line << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"found", true},
            {"session_id", state->session_id},
            {"transcript_path", state->transcript_path},
            {"realm", state->realm},
            {"last_processed_line", state->last_processed_line},
            {"last_distilled_at", state->last_distilled_at},
            {"created_at", state->created_at}
        });
    }

    DuckDBToolResult tool_transcript_list(const json& params) {
        auto transcripts = mind_->store().get_pending_transcripts();

        std::ostringstream ss;
        ss << "Registered transcripts: " << transcripts.size() << "\n\n";

        json list_json = json::array();
        for (const auto& t : transcripts) {
            ss << "  [" << t.session_id << "] " << t.realm << " - line " << t.last_processed_line << "\n";
            ss << "    " << t.transcript_path << "\n";

            list_json.push_back({
                {"session_id", t.session_id},
                {"transcript_path", t.transcript_path},
                {"realm", t.realm},
                {"last_processed_line", t.last_processed_line},
                {"last_distilled_at", t.last_distilled_at}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"transcripts", list_json},
            {"count", transcripts.size()}
        });
    }

    DuckDBToolResult tool_transcript_update(const json& params) {
        std::string session_id = params.value("session_id", "");
        int64_t last_line = params.value("last_line", 0LL);

        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        bool ok = mind_->store().update_transcript_progress(session_id, last_line);
        if (!ok) {
            return DuckDBToolResult::error("Failed to update transcript progress");
        }

        return DuckDBToolResult::ok("Updated transcript progress", {
            {"session_id", session_id},
            {"last_line", last_line}
        });
    }

    DuckDBToolResult tool_transcript_remove(const json& params) {
        std::string session_id = params.value("session_id", "");

        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        bool ok = mind_->store().remove_transcript(session_id);
        if (!ok) {
            return DuckDBToolResult::error("Failed to remove transcript");
        }

        return DuckDBToolResult::ok("Removed transcript", {
            {"session_id", session_id}
        });
    }

    DuckDBToolResult tool_transcript_parse(const json& params) {
        std::string session_id = params.value("session_id", "");
        size_t min_turns = params.value("min_turns", 4);

        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        // Get transcript state
        auto state = mind_->store().get_transcript(session_id);
        if (!state) {
            return DuckDBToolResult::error("Transcript not found: " + session_id);
        }

        // Open and read the JSONL file
        std::ifstream file(state->transcript_path);
        if (!file) {
            return DuckDBToolResult::error("Cannot open transcript: " + state->transcript_path);
        }

        // Skip to last processed line
        std::string line;
        int64_t current_line = 0;
        while (current_line < state->last_processed_line && std::getline(file, line)) {
            current_line++;
        }

        // Parse new lines
        json turns_json = json::array();
        int64_t last_line = state->last_processed_line;

        while (std::getline(file, line)) {
            current_line++;
            if (line.empty()) continue;

            try {
                auto entry = json::parse(line);

                // Claude Code JSONL format: {"type": "user"|"assistant", "message": {...}}
                std::string type = entry.value("type", "");
                if (type != "user" && type != "assistant") continue;

                std::string content;
                if (entry.contains("message")) {
                    auto& msg = entry["message"];
                    // Extract text content from message
                    if (msg.contains("content")) {
                        auto& msg_content = msg["content"];
                        if (msg_content.is_string()) {
                            content = msg_content.get<std::string>();
                        } else if (msg_content.is_array()) {
                            // Array of content blocks
                            for (const auto& block : msg_content) {
                                if (block.contains("text") && block["text"].is_string()) {
                                    if (!content.empty()) content += "\n";
                                    content += block["text"].get<std::string>();
                                }
                            }
                        }
                    }
                }

                if (!content.empty()) {
                    turns_json.push_back({
                        {"role", type},
                        {"content", content},
                        {"line", current_line}
                    });
                    last_line = current_line;
                }
            } catch (...) {
                // Skip malformed lines
                continue;
            }
        }

        // Check if we have enough turns
        if (turns_json.size() < min_turns) {
            return DuckDBToolResult::ok("Not enough new turns", {
                {"session_id", session_id},
                {"turns_found", turns_json.size()},
                {"min_turns", min_turns},
                {"ready", false}
            });
        }

        std::ostringstream ss;
        ss << "Parsed " << turns_json.size() << " new turns from transcript\n";
        ss << "  Session: " << session_id << "\n";
        ss << "  Lines: " << state->last_processed_line << " -> " << last_line << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"realm", state->realm},
            {"turns", turns_json},
            {"turns_count", turns_json.size()},
            {"last_line", last_line},
            {"ready", true}
        });
    }

    DuckDBToolResult tool_transcript_search(const json& params) {
        std::string query = params.value("query", "");
        std::string session_id = params.value("session_id", "");
        size_t limit = params.value("limit", 10);
        float min_similarity = params.value("min_similarity", 0.3f);
        size_t max_candidates = params.value("max_candidates", 100);  // Pre-filter limit
        bool keyword_only = params.value("keyword_only", false);  // Skip embedding, keyword match only

        if (query.empty()) {
            return DuckDBToolResult::error("query is required");
        }

        // Check embedder is ready (unless keyword_only)
        if (!keyword_only && !mind_->embedder_ready()) {
            return DuckDBToolResult::error("Embedder not ready");
        }

        // Extract keywords from query (lowercase, split on spaces)
        std::vector<std::string> keywords;
        {
            std::string lower_query = query;
            std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);
            std::istringstream iss(lower_query);
            std::string word;
            while (iss >> word) {
                if (word.size() >= 3) {  // Skip very short words
                    keywords.push_back(word);
                }
            }
        }

        // Helper to check if content contains any keyword
        auto contains_keyword = [&keywords](const std::string& content) -> bool {
            if (keywords.empty()) return true;  // No keywords = match all
            std::string lower = content;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            for (const auto& kw : keywords) {
                if (lower.find(kw) != std::string::npos) return true;
            }
            return false;
        };

        // Helper to compute cosine similarity
        auto cosine_similarity = [](const std::vector<float>& a, const std::vector<float>& b) -> float {
            if (a.size() != b.size() || a.empty()) return 0.0f;
            float dot = 0, norm_a = 0, norm_b = 0;
            for (size_t i = 0; i < a.size(); ++i) {
                dot += a[i] * b[i];
                norm_a += a[i] * a[i];
                norm_b += b[i] * b[i];
            }
            float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
            return denom > 0 ? dot / denom : 0.0f;
        };

        // Get transcripts to search
        std::vector<TranscriptState> transcripts;
        if (!session_id.empty()) {
            auto state = mind_->store().get_transcript(session_id);
            if (state) transcripts.push_back(*state);
        } else {
            transcripts = mind_->store().get_pending_transcripts();
        }

        if (transcripts.empty()) {
            return DuckDBToolResult::error("No transcripts found");
        }

        // Structure to hold candidates (pre-filtered by keyword)
        struct Candidate {
            std::string session_id;
            std::string realm;
            std::string role;
            std::string content;
            int64_t line;
        };
        std::vector<Candidate> candidates;

        // Phase 1: Keyword pre-filter (fast, no embeddings)
        for (const auto& state : transcripts) {
            std::ifstream file(state.transcript_path);
            if (!file) continue;

            std::string line;
            int64_t current_line = 0;

            while (std::getline(file, line) && candidates.size() < max_candidates * 2) {
                current_line++;
                if (line.empty()) continue;

                try {
                    auto entry = json::parse(line);
                    std::string type = entry.value("type", "");
                    if (type != "user" && type != "assistant") continue;

                    std::string content;
                    if (entry.contains("message")) {
                        auto& msg = entry["message"];
                        if (msg.contains("content")) {
                            auto& msg_content = msg["content"];
                            if (msg_content.is_string()) {
                                content = msg_content.get<std::string>();
                            } else if (msg_content.is_array()) {
                                for (const auto& block : msg_content) {
                                    if (block.contains("text") && block["text"].is_string()) {
                                        if (!content.empty()) content += "\n";
                                        content += block["text"].get<std::string>();
                                    }
                                }
                            }
                        }
                    }

                    if (content.empty() || content.size() < 20) continue;

                    // Keyword pre-filter
                    if (!contains_keyword(content)) continue;

                    candidates.push_back({
                        state.session_id,
                        state.realm,
                        type,
                        content,
                        current_line
                    });
                } catch (...) {
                    continue;
                }
            }
        }

        // Limit candidates before embedding
        if (candidates.size() > max_candidates) {
            candidates.resize(max_candidates);
        }

        // Phase 2: Semantic ranking (only on filtered candidates)
        struct SearchResult {
            std::string session_id;
            std::string realm;
            std::string role;
            std::string content;
            int64_t line;
            float similarity;
        };
        std::vector<SearchResult> results;

        if (!candidates.empty()) {
            if (keyword_only) {
                // Keyword-only mode: rank by keyword density (fast, no embedding)
                auto count_keywords = [&keywords](const std::string& content) -> float {
                    if (keywords.empty()) return 1.0f;
                    std::string lower = content;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    int count = 0;
                    for (const auto& kw : keywords) {
                        size_t pos = 0;
                        while ((pos = lower.find(kw, pos)) != std::string::npos) {
                            count++;
                            pos += kw.size();
                        }
                    }
                    return static_cast<float>(count) / keywords.size();
                };

                for (const auto& c : candidates) {
                    float density = count_keywords(c.content);
                    results.push_back({
                        c.session_id,
                        c.realm,
                        c.role,
                        c.content.size() > 500 ? c.content.substr(0, 500) + "..." : c.content,
                        c.line,
                        density
                    });
                }
            } else {
                // Full semantic search with embeddings
                // Generate query embedding once (query mode for BGE)
                Artha query_artha = mind_->embedder().transform_query(query);
                const std::vector<float>& query_embedding = query_artha.nu.data;

                for (const auto& c : candidates) {
                    std::string embed_content = c.content;
                    if (embed_content.size() > 2000) {
                        embed_content = embed_content.substr(0, 2000);
                    }

                    Artha content_artha = mind_->embedder().transform(embed_content);
                    const std::vector<float>& content_embedding = content_artha.nu.data;

                    float sim = cosine_similarity(query_embedding, content_embedding);
                    if (sim >= min_similarity) {
                        results.push_back({
                            c.session_id,
                            c.realm,
                            c.role,
                            c.content.size() > 500 ? c.content.substr(0, 500) + "..." : c.content,
                            c.line,
                            sim
                        });
                    }
                }
            }
        }

        // Sort by similarity (descending)
        std::sort(results.begin(), results.end(),
            [](const SearchResult& a, const SearchResult& b) { return a.similarity > b.similarity; });

        // Limit results
        if (results.size() > limit) {
            results.resize(limit);
        }

        // Build response
        json results_json = json::array();
        for (const auto& r : results) {
            results_json.push_back({
                {"session_id", r.session_id},
                {"realm", r.realm},
                {"role", r.role},
                {"content", r.content},
                {"line", r.line},
                {"similarity", r.similarity}
            });
        }

        std::ostringstream ss;
        ss << "Found " << results.size() << " matching passages\n";
        if (!results.empty()) {
            ss << "Top match: " << results[0].content.substr(0, 100) << "...\n";
            ss << "  Similarity: " << std::fixed << std::setprecision(2) << results[0].similarity;
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"query", query},
            {"results", results_json},
            {"count", results.size()},
            {"transcripts_searched", transcripts.size()}
        });
    }

    DuckDBToolResult tool_msg_send(const json& params) {
        std::string target = params.value("target", "");
        std::string content = params.value("content", "");

        if (target.empty()) return DuckDBToolResult::error("target is required");
        if (content.empty()) return DuckDBToolResult::error("content is required");

        std::string target_type = params.value("target_type", "");
        int32_t priority = params.value("priority", 1);
        std::string content_type = params.value("content_type", "text");
        int32_t ttl = params.value("ttl", 3600);
        std::string session_id = get_session_id(params);

        // Auto-detect target_type
        if (target_type.empty()) {
            if (target == "*") {
                target_type = "global";
            } else if (target.find("project:") == 0 || target == "brahman") {
                target_type = "realm";
            } else {
                target_type = "direct";
            }
        }

        SessionMessage msg;
        msg.sender_session = session_id;
        msg.sender_realm = detect_current_realm();
        msg.target_type = target_type;
        msg.target_id = target;
        msg.priority = priority;
        msg.content = content;
        msg.content_type = content_type;
        msg.expires_at = ttl > 0 ? current_time_ms() + (static_cast<int64_t>(ttl) * 1000) : 0;
        msg.created_at = current_time_ms();

        int64_t msg_id = mind_->store().msg_send(msg);

        if (msg_id <= 0) {
            return DuckDBToolResult::error("Failed to send message: " + mind_->store().last_error());
        }

        std::ostringstream ss;
        ss << "Message sent (" << target_type << " to " << target << ")";

        return DuckDBToolResult::ok(ss.str(), {
            {"message_id", msg_id},
            {"target_type", target_type},
            {"target", target},
            {"priority", priority}
        });
    }

    DuckDBToolResult tool_msg_inbox(const json& params) {
        std::string session_id = get_session_id(params);
        size_t limit = params.value("limit", 20);
        int32_t min_priority = params.value("min_priority", 0);
        bool auto_ack = params.value("auto_ack", false);

        auto items = mind_->store().msg_inbox(session_id, limit, min_priority);

        if (items.empty()) {
            return DuckDBToolResult::ok("No unread messages", {
                {"session_id", session_id},
                {"count", 0},
                {"messages", json::array()}
            });
        }

        std::ostringstream ss;
        ss << items.size() << " unread message(s):\n\n";

        json msg_list = json::array();
        for (const auto& item : items) {
            const auto& msg = item.message;
            ss << "[" << priority_to_string(msg.priority) << "] "
               << "From: " << msg.sender_session << " (" << msg.sender_realm << ")\n"
               << msg.content << "\n\n";

            msg_list.push_back({
                {"id", msg.id},
                {"sender_session", msg.sender_session},
                {"sender_realm", msg.sender_realm},
                {"priority", msg.priority},
                {"content", msg.content},
                {"content_type", msg.content_type},
                {"created_at", msg.created_at},
                {"is_read", item.is_read}
            });

            if (auto_ack) {
                mind_->store().msg_ack(msg.id, session_id);
            }
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"count", items.size()},
            {"messages", msg_list}
        });
    }

    DuckDBToolResult tool_msg_ack(const json& params) {
        auto [message_id, _] = parse_id(params, "message_id");
        if (message_id <= 0) {
            return DuckDBToolResult::error("message_id is required");
        }

        std::string session_id = get_session_id(params);

        if (!mind_->store().msg_ack(message_id, session_id)) {
            return DuckDBToolResult::error("Failed to acknowledge message");
        }

        return DuckDBToolResult::ok("Message acknowledged", {
            {"message_id", message_id},
            {"session_id", session_id}
        });
    }

    DuckDBToolResult tool_msg_ack_all(const json& params) {
        std::string session_id = get_session_id(params);

        if (!mind_->store().msg_ack_all(session_id)) {
            return DuckDBToolResult::error("Failed to acknowledge messages");
        }

        return DuckDBToolResult::ok("All messages acknowledged", {
            {"session_id", session_id}
        });
    }

    DuckDBToolResult tool_msg_history(const json& params) {
        std::string session_id = get_session_id(params);
        size_t limit = params.value("limit", 30);

        auto messages = mind_->store().msg_history(session_id, limit);

        if (messages.empty()) {
            return DuckDBToolResult::ok("No message history", {
                {"session_id", session_id},
                {"count", 0},
                {"messages", json::array()}
            });
        }

        std::ostringstream ss;
        ss << "Message history (" << messages.size() << " messages):\n\n";

        json msg_list = json::array();
        for (const auto& msg : messages) {
            ss << "[" << format_timestamp(msg.created_at) << "] "
               << msg.sender_session << " -> " << msg.target_id << ": "
               << msg.content.substr(0, 80)
               << (msg.content.size() > 80 ? "..." : "") << "\n";

            msg_list.push_back({
                {"id", msg.id},
                {"sender_session", msg.sender_session},
                {"sender_realm", msg.sender_realm},
                {"target_type", msg.target_type},
                {"target_id", msg.target_id},
                {"priority", msg.priority},
                {"content", msg.content},
                {"content_type", msg.content_type},
                {"created_at", msg.created_at},
                {"expires_at", msg.expires_at}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"count", messages.size()},
            {"messages", msg_list}
        });
    }

    DuckDBToolResult tool_session_register(const json& params) {
        std::string session_id = get_session_id(params);
        std::string realm = params.value("realm", "");
        std::string transcript_path = params.value("transcript_path", "");
        std::string project_dir = params.value("project_dir", "");
        std::string metadata = params.value("metadata", "{}");

        if (realm.empty()) {
            realm = detect_current_realm();
        }

        // Use passed PID or fall back to caller's PID
        int32_t pid = params.contains("pid") ? params["pid"].get<int32_t>() : static_cast<int32_t>(getpid());

        if (!mind_->store().session_register(session_id, realm, pid, transcript_path, project_dir, metadata)) {
            return DuckDBToolResult::error("Failed to register session: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Session registered", {
            {"session_id", session_id},
            {"realm", realm},
            {"pid", pid},
            {"transcript_path", transcript_path}
        });
    }

    DuckDBToolResult tool_session_heartbeat(const json& params) {
        std::string session_id = get_session_id(params);
        std::string metadata = params.value("metadata", "");

        if (!mind_->store().session_heartbeat(session_id, metadata)) {
            return DuckDBToolResult::error("Failed to send heartbeat");
        }

        return DuckDBToolResult::ok("Heartbeat sent", {
            {"session_id", session_id}
        });
    }

    DuckDBToolResult tool_session_list(const json& params) {
        std::string realm = params.value("realm", "");
        std::string status = params.value("status", "active");

        auto sessions = mind_->store().session_list(realm, status);

        if (sessions.empty()) {
            return DuckDBToolResult::ok("No sessions found", {
                {"count", 0},
                {"realm", realm},
                {"status", status},
                {"sessions", json::array()}
            });
        }

        std::ostringstream ss;
        ss << sessions.size() << " session(s):\n\n";

        json session_list = json::array();
        for (const auto& s : sessions) {
            ss << "- " << s.session_id << " [" << s.status << "] "
               << s.realm << " (pid " << s.pid << ")\n";

            session_list.push_back({
                {"session_id", s.session_id},
                {"realm", s.realm},
                {"pid", s.pid},
                {"status", s.status},
                {"started_at", s.started_at},
                {"last_heartbeat", s.last_heartbeat},
                {"metadata", s.metadata}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", sessions.size()},
            {"realm", realm},
            {"status", status},
            {"sessions", session_list}
        });
    }

    DuckDBToolResult tool_session_deregister(const json& params) {
        std::string session_id = get_session_id(params);

        if (!mind_->store().session_deregister(session_id)) {
            return DuckDBToolResult::error("Failed to deregister session");
        }

        return DuckDBToolResult::ok("Session deregistered", {
            {"session_id", session_id}
        });
    }

    DuckDBToolResult tool_session_sync(const json& params) {
        std::string projects_dir = params.value("projects_dir", "");

        auto result = mind_->store().session_sync(projects_dir);

        std::ostringstream msg;
        msg << "Session sync complete: "
            << result.discovered << " discovered, "
            << result.updated << " updated, "
            << result.marked_dead << " marked dead";

        return DuckDBToolResult::ok(msg.str(), {
            {"discovered", result.discovered},
            {"updated", result.updated},
            {"marked_dead", result.marked_dead}
        });
    }

    DuckDBToolResult tool_read_transcript(const json& params) {
        std::string path = params.value("path", "");
        std::string session_id = params.value("session_id", "");
        int start_turn = params.value("start_turn", 0);
        size_t limit = params.value("limit", 20);
        size_t max_chars = params.value("max_chars_per_turn", 500);
        std::string role_filter = params.value("role_filter", "");
        std::string keyword = params.value("keyword", "");
        bool metadata_only = params.value("metadata_only", false);

        // Find transcript path from session_id if not provided
        if (path.empty() && !session_id.empty()) {
            // Try common locations using glob
            std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
            std::string pattern = home + "/.claude/projects/*/" + session_id + ".jsonl";

            glob_t glob_result;
            if (glob(pattern.c_str(), GLOB_TILDE, nullptr, &glob_result) == 0) {
                if (glob_result.gl_pathc > 0) {
                    path = glob_result.gl_pathv[0];
                }
                globfree(&glob_result);
            }
        }

        if (path.empty()) {
            return DuckDBToolResult::error("No transcript path provided and couldn't find session");
        }

        // Parse transcript
        TranscriptParser parser;
        TranscriptParseOptions opts;
        opts.filter_system_reminders = true;
        opts.include_thinking = false;  // Skip thinking blocks for brevity

        int64_t last_line = 0;
        auto all_turns = parser.parse(path, opts, &last_line);

        if (all_turns.empty()) {
            return DuckDBToolResult::error("Failed to parse transcript: " + parser.last_error());
        }

        // Calculate metadata
        size_t total_chars = 0;
        for (const auto& t : all_turns) total_chars += t.content.size();

        json result;
        result["path"] = path;
        result["total_turns"] = all_turns.size();
        result["total_chars"] = total_chars;
        result["last_line"] = last_line;

        if (metadata_only) {
            std::ostringstream ss;
            ss << "Transcript: " << path << "\n"
               << "Total turns: " << all_turns.size() << "\n"
               << "Total chars: " << total_chars << "\n"
               << "Lines: " << last_line;
            return DuckDBToolResult::ok(ss.str(), result);
        }

        // Apply filters and pagination
        std::vector<ConversationTurn> filtered;
        for (size_t i = 0; i < all_turns.size(); i++) {
            const auto& t = all_turns[i];

            // Role filter
            if (!role_filter.empty() && t.role != role_filter) continue;

            // Keyword filter
            if (!keyword.empty()) {
                if (t.content.find(keyword) == std::string::npos) continue;
            }

            filtered.push_back(t);
        }

        result["filtered_turns"] = filtered.size();

        // Paginate
        json turns_arr = json::array();
        size_t output_chars = 0;
        const size_t max_output_chars = 30000;  // Prevent huge responses

        for (size_t i = start_turn; i < filtered.size() && turns_arr.size() < limit; i++) {
            const auto& t = filtered[i];

            std::string content = t.content;
            if (max_chars > 0 && content.size() > max_chars) {
                content = content.substr(0, max_chars) + "...";
            }

            if (output_chars + content.size() > max_output_chars) {
                turns_arr.push_back({
                    {"role", "system"},
                    {"content", "[output truncated - use start_turn=" + std::to_string(i) + " to continue]"},
                    {"turn_index", -1}
                });
                break;
            }

            output_chars += content.size();
            turns_arr.push_back({
                {"role", t.role},
                {"content", content},
                {"turn_index", t.turn_index},
                {"line_number", t.line_number}
            });
        }

        result["turns"] = turns_arr;
        result["returned"] = turns_arr.size();
        result["start_turn"] = start_turn;

        std::ostringstream ss;
        ss << "Transcript: " << path << "\n"
           << "Total: " << all_turns.size() << " turns";
        if (!role_filter.empty() || !keyword.empty()) {
            ss << " (filtered: " << filtered.size() << ")";
        }
        ss << "\nShowing turns " << start_turn << "-" << (start_turn + turns_arr.size() - 1) << ":\n\n";

        for (const auto& t : turns_arr) {
            ss << "[" << t["role"].get<std::string>() << "] ";
            std::string content = t["content"].get<std::string>();
            if (content.size() > 100) {
                ss << content.substr(0, 100) << "...\n";
            } else {
                ss << content << "\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_get_turns(const json& params) {
        std::string session_id = params.value("session_id", "");
        int start_index = params.value("start_index", 0);
        size_t limit = params.value("limit", 50);

        auto turns = mind_->store().get_conversation_turns(session_id, start_index, limit);

        json result;
        result["turns"] = json::array();
        result["count"] = turns.size();

        for (const auto& turn : turns) {
            json t;
            t["id"] = turn.id;
            t["session_id"] = turn.session_id;
            t["role"] = turn.role;
            t["turn_index"] = turn.turn_index;
            t["content"] = turn.content.substr(0, 500);
            t["tools_used"] = turn.tools_used;
            t["files_touched"] = turn.files_touched;
            t["has_error"] = turn.has_error;
            t["created_at"] = turn.created_at;
            result["turns"].push_back(t);
        }

        std::ostringstream msg;
        msg << "Found " << turns.size() << " conversation turn(s)";

        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_create_episode(const json& params) {
        std::string session_id = params.value("session_id", "");
        std::string title = params.value("title", "");
        int start_turn = params.value("start_turn", 0);
        int end_turn = params.value("end_turn", 0);
        std::string episode_type = params.value("episode_type", "distillation");
        std::string realm = params.value("realm", "brahman");

        if (session_id.empty() || title.empty()) {
            return DuckDBToolResult::error("session_id and title are required");
        }

        int64_t episode_id = mind_->store().create_dialogue_episode(
            session_id, title, start_turn, episode_type, realm
        );

        if (episode_id < 0) {
            return DuckDBToolResult::error("Failed to create episode");
        }

        // If end_turn provided, set it directly (close_dialogue_episode would override)
        if (end_turn > 0) {
            std::ostringstream sql;
            sql << "UPDATE dialogue_episode SET end_turn = " << end_turn
                << ", turn_count = " << (end_turn - start_turn + 1)
                << ", outcome = 'completed' WHERE id = " << episode_id;
            mind_->store().execute_raw(sql.str());
        }

        std::ostringstream msg;
        msg << "Created episode " << episode_id << ": " << title
            << " (turns " << start_turn << "-" << (end_turn > 0 ? end_turn : start_turn) << ")";

        return DuckDBToolResult::ok(msg.str(), {
            {"episode_id", episode_id},
            {"session_id", session_id},
            {"title", title},
            {"start_turn", start_turn},
            {"end_turn", end_turn}
        });
    }

    DuckDBToolResult tool_query_claims(const json& params) {
        std::string subject = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string scope = params.value("scope", "");
        bool active_only = params.value("active_only", true);
        size_t limit = params.value("limit", 20);

        auto claims = mind_->store().query_claims(subject, predicate, scope, active_only, limit);

        json result;
        result["claims"] = json::array();
        result["count"] = claims.size();

        for (const auto& claim : claims) {
            json c;
            c["id"] = claim.id;
            c["subject"] = claim.subject;
            c["predicate"] = claim.predicate;
            c["object"] = claim.object_norm;
            c["scope"] = claim.scope_key;
            c["polarity"] = claim.polarity;
            c["confidence"] = claim.confidence;
            c["support_count"] = claim.support_count;
            c["source"] = claim.source_class;
            c["created_at"] = claim.created_at;
            result["claims"].push_back(c);
        }

        std::ostringstream msg;
        msg << "Found " << claims.size() << " claim(s)";

        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_get_policies(const json& params) {
        std::string scope = params.value("scope", "");
        std::string policy_type = params.value("type", "");
        size_t limit = params.value("limit", 30);

        auto policies = mind_->store().get_active_policies(scope, policy_type, limit);

        json result;
        result["policies"] = json::array();
        result["count"] = policies.size();

        for (const auto& policy : policies) {
            json p;
            p["id"] = policy.id;
            p["type"] = policy.policy_type;
            p["content"] = policy.content.substr(0, 300);
            p["scope"] = policy.scope_key;
            p["state"] = policy.state;
            p["confidence"] = policy.confidence;
            p["support_count"] = policy.support_count;
            p["session_count"] = policy.session_count;
            p["created_at"] = policy.created_at;
            result["policies"].push_back(p);
        }

        std::ostringstream msg;
        msg << "Found " << policies.size() << " active policy/policies";

        return DuckDBToolResult::ok(msg.str(), result);
    }
