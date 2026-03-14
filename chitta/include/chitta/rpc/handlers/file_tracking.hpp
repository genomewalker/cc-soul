// Included into DuckDBHandler class body — not a standalone header

    DuckDBToolResult tool_file_index_session(const json& params) {
        std::string session_id = params.value("session_id", "");
        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        bool force = params.value("force", false);

        // Check if already indexed (unless force)
        if (!force && mind_->store().session_file_edits_indexed(session_id)) {
            return DuckDBToolResult::ok(
                "Session " + session_id + " already indexed",
                {{"session_id", session_id}, {"already_indexed", true}}
            );
        }

        // Find transcript path
        std::string projects_dir = get_claude_projects_dir();
        std::string transcript_path;
        std::string realm = "brahman";

        // Search through project directories for this session's transcript
        for (const auto& project_entry : std::filesystem::directory_iterator(projects_dir)) {
            if (!project_entry.is_directory()) continue;

            std::string candidate = project_entry.path().string() + "/" + session_id + ".jsonl";
            if (std::filesystem::exists(candidate)) {
                transcript_path = candidate;
                // Extract realm from project directory name
                realm = "project:" + project_entry.path().filename().string();
                break;
            }
        }

        if (transcript_path.empty()) {
            return DuckDBToolResult::error("Could not find transcript for session " + session_id);
        }

        size_t indexed = index_file_history_from_transcript(session_id, transcript_path, realm);

        return DuckDBToolResult::ok(
            "Indexed " + std::to_string(indexed) + " file edits from session " + session_id,
            {{"session_id", session_id}, {"indexed_count", indexed}, {"transcript_path", transcript_path}}
        );
    }

    DuckDBToolResult tool_file_index_all(const json& params) {
        bool force = params.value("force", false);
        size_t max_sessions = params.value("limit", 100);  // Default limit to avoid long runs

        std::string file_history_dir = get_file_history_dir();
        if (!std::filesystem::exists(file_history_dir)) {
            return DuckDBToolResult::error("File history directory not found: " + file_history_dir);
        }

        std::string projects_dir = get_claude_projects_dir();
        size_t total_indexed = 0;
        size_t sessions_processed = 0;
        size_t sessions_skipped = 0;
        std::vector<std::string> indexed_sessions;

        // Iterate through file-history directories (each is a session)
        for (const auto& session_entry : std::filesystem::directory_iterator(file_history_dir)) {
            if (!session_entry.is_directory()) continue;
            if (max_sessions > 0 && sessions_processed >= max_sessions) break;

            std::string session_id = session_entry.path().filename().string();

            // Skip if already indexed (unless force)
            if (!force && mind_->store().session_file_edits_indexed(session_id)) {
                sessions_skipped++;
                continue;
            }

            // Find transcript for this session
            std::string transcript_path;
            std::string realm = "brahman";

            for (const auto& project_entry : std::filesystem::directory_iterator(projects_dir)) {
                if (!project_entry.is_directory()) continue;

                std::string candidate = project_entry.path().string() + "/" + session_id + ".jsonl";
                if (std::filesystem::exists(candidate)) {
                    transcript_path = candidate;
                    realm = "project:" + project_entry.path().filename().string();
                    break;
                }
            }

            if (transcript_path.empty()) {
                // No transcript found - index directly from file-history metadata
                // Just mark as indexed with 0 entries for now
                mind_->store().mark_session_file_edits_indexed(session_id);
                sessions_processed++;
                continue;
            }

            size_t indexed = index_file_history_from_transcript(session_id, transcript_path, realm);
            total_indexed += indexed;
            sessions_processed++;

            if (indexed > 0) {
                indexed_sessions.push_back(session_id);
            }
        }

        std::ostringstream text;
        text << "Indexed " << total_indexed << " file edits from " << sessions_processed << " sessions\n";
        text << "Skipped " << sessions_skipped << " already-indexed sessions\n";

        return DuckDBToolResult::ok(
            text.str(),
            {
                {"total_indexed", total_indexed},
                {"sessions_processed", sessions_processed},
                {"sessions_skipped", sessions_skipped},
                {"indexed_sessions", indexed_sessions}
            }
        );
    }

    DuckDBToolResult tool_file_timeline(const json& params) {
        std::string query = params.value("query", "");
        std::string session_id = params.value("session_id", "");
        // Accept "path" as alias for "file_pattern" (matches file_at_time parameter name)
        std::string file_pattern = params.value("file_pattern", params.value("path", ""));
        size_t limit = params.value("limit", 20);
        bool path_given = !file_pattern.empty();
        // Default cross_session to true when a specific path is given (search all history)
        bool cross_session = params.value("cross_session", path_given);

        std::vector<FileEdit> edits;

        // If cross_session, auto-index recent sessions first
        if (cross_session) {
            // Index up to 50 recent sessions to enable cross-session queries
            tool_file_index_all({{"limit", 50}});
        }

        if (!session_id.empty()) {
            // Index this session if not already done
            if (!mind_->store().session_file_edits_indexed(session_id)) {
                tool_file_index_session({{"session_id", session_id}});
            }
            edits = mind_->store().get_session_file_edits(session_id, limit);
        } else if (!query.empty()) {
            // Parse time query
            int64_t target_time = parse_time_string(query);
            if (target_time == 0) {
                return DuckDBToolResult::error("Could not parse time query: " + query);
            }

            // Search ±30 minutes around target time (or wider for cross-session)
            int64_t window_ms = cross_session ? (24 * 60 * 60 * 1000LL) : (30 * 60 * 1000LL);
            edits = mind_->store().get_file_edits_in_range(
                target_time - window_ms,
                target_time + window_ms,
                file_pattern,
                limit
            );
        } else if (path_given) {
            // Path given but no time query: search all indexed history (no time constraint)
            edits = mind_->store().get_file_edits_in_range(
                0,
                std::numeric_limits<int64_t>::max(),
                file_pattern,
                limit
            );
        } else {
            // Default: last 24h (or last 7 days for cross-session)
            auto now = std::chrono::system_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            int64_t window_ms = cross_session ? (7 * 24 * 60 * 60 * 1000LL) : (24 * 60 * 60 * 1000LL);
            edits = mind_->store().get_file_edits_in_range(
                now_ms - window_ms,
                now_ms,
                file_pattern,
                limit
            );
        }

        // Format output
        json results = json::array();
        std::ostringstream text;
        text << "File Timeline:\n";

        for (const auto& edit : edits) {
            text << format_time(edit.backup_time) << "  "
                 << edit.file_path << "  v" << edit.version << "\n";

            results.push_back({
                {"id", edit.id},
                {"file_path", edit.file_path},
                {"version", edit.version},
                {"backup_filename", edit.backup_filename},
                {"backup_time", edit.backup_time},
                {"time_formatted", format_time(edit.backup_time)},
                {"session_id", edit.session_id}
            });
        }

        if (edits.empty()) {
            text << "(no file edits found)\n";
        }

        return DuckDBToolResult::ok(text.str(), {{"edits", results}, {"count", edits.size()}});
    }

    DuckDBToolResult tool_file_at_time(const json& params) {
        std::string file_path = params.value("file_path", "");
        if (file_path.empty()) {
            return DuckDBToolResult::error("file_path is required");
        }

        std::string time_str = params.value("time", "");
        std::string session_id = params.value("session_id", "");
        bool show_diff = params.value("show_diff", false);

        // Determine target time
        int64_t target_time;
        if (!time_str.empty()) {
            target_time = parse_time_string(time_str);
            if (target_time == 0) {
                return DuckDBToolResult::error("Could not parse time: " + time_str);
            }
        } else {
            // Default: now (get most recent version)
            auto now = std::chrono::system_clock::now();
            target_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
        }

        // Find the file edit closest to the target time
        auto edit_opt = mind_->store().get_file_at_time(file_path, target_time);
        if (!edit_opt) {
            return DuckDBToolResult::error("No version found for " + file_path + " at or before " + format_time(target_time));
        }

        auto& edit = *edit_opt;

        // Read the actual content from file-history
        std::string file_history_dir = get_file_history_dir();
        std::string backup_path = file_history_dir + "/" + edit.session_id + "/" + edit.backup_filename;

        if (!std::filesystem::exists(backup_path)) {
            return DuckDBToolResult::error("Backup file not found: " + backup_path);
        }

        std::ifstream file(backup_path);
        std::stringstream content_stream;
        content_stream << file.rdbuf();
        std::string content = content_stream.str();

        json result = {
            {"file_path", edit.file_path},
            {"version", edit.version},
            {"backup_filename", edit.backup_filename},
            {"backup_time", edit.backup_time},
            {"time_formatted", format_time(edit.backup_time)},
            {"session_id", edit.session_id},
            {"content", content},
            {"content_lines", std::count(content.begin(), content.end(), '\n') + 1}
        };

        std::ostringstream text;
        text << "File: " << edit.file_path << " (v" << edit.version << ")\n";
        text << "Time: " << format_time(edit.backup_time) << "\n";
        text << "Session: " << edit.session_id << "\n";
        text << "Lines: " << result["content_lines"] << "\n\n";

        if (show_diff) {
            // Read current file if it exists
            if (std::filesystem::exists(file_path)) {
                std::ifstream current_file(file_path);
                std::stringstream current_stream;
                current_stream << current_file.rdbuf();
                std::string current_content = current_stream.str();

                if (current_content != content) {
                    text << "[Content differs from current version - use file_restore to restore]\n\n";
                    result["differs_from_current"] = true;
                } else {
                    text << "[Content matches current version]\n\n";
                    result["differs_from_current"] = false;
                }
            }
        }

        // Show first 100 lines of content
        std::istringstream lines(content);
        std::string line;
        int line_count = 0;
        text << "--- Content ---\n";
        while (std::getline(lines, line) && line_count < 100) {
            text << line << "\n";
            line_count++;
        }
        if (line_count >= 100) {
            text << "\n... (truncated, " << result["content_lines"] << " total lines)\n";
        }

        return DuckDBToolResult::ok(text.str(), result);
    }

    DuckDBToolResult tool_file_restore(const json& params) {
        std::string file_path = params.value("file_path", "");
        if (file_path.empty()) {
            return DuckDBToolResult::error("file_path is required");
        }

        int64_t version_id = params.value("version_id", int64_t(0));
        bool preview = params.value("preview", true);

        // If no version_id, get the most recent version
        std::optional<FileEdit> edit_opt;
        if (version_id > 0) {
            // Look up by ID - not implemented yet, use file_at_time for now
            // For now, we'll use the file path lookup
            auto edits = mind_->store().get_file_edits(file_path, 1);
            if (edits.empty()) {
                return DuckDBToolResult::error("No version found for " + file_path);
            }
            edit_opt = edits[0];
        } else {
            auto edits = mind_->store().get_file_edits(file_path, 1);
            if (edits.empty()) {
                return DuckDBToolResult::error("No version found for " + file_path);
            }
            edit_opt = edits[0];
        }

        auto& edit = *edit_opt;

        // Read the backup content
        std::string file_history_dir = get_file_history_dir();
        std::string backup_path = file_history_dir + "/" + edit.session_id + "/" + edit.backup_filename;

        if (!std::filesystem::exists(backup_path)) {
            return DuckDBToolResult::error("Backup file not found: " + backup_path);
        }

        std::ifstream backup_file(backup_path);
        std::stringstream content_stream;
        content_stream << backup_file.rdbuf();
        std::string content = content_stream.str();

        json result = {
            {"file_path", edit.file_path},
            {"version", edit.version},
            {"backup_time", edit.backup_time},
            {"time_formatted", format_time(edit.backup_time)},
            {"preview", preview},
            {"content_lines", std::count(content.begin(), content.end(), '\n') + 1}
        };

        if (preview) {
            std::ostringstream text;
            text << "Preview: Would restore " << file_path << " to version from " << format_time(edit.backup_time) << "\n";
            text << "Lines: " << result["content_lines"] << "\n\n";
            text << "To actually restore, call file_restore with preview=false\n\n";

            // Show first 50 lines
            std::istringstream lines(content);
            std::string line;
            int line_count = 0;
            text << "--- Preview Content ---\n";
            while (std::getline(lines, line) && line_count < 50) {
                text << line << "\n";
                line_count++;
            }
            if (line_count >= 50) {
                text << "\n... (truncated)\n";
            }

            result["content"] = content;
            return DuckDBToolResult::ok(text.str(), result);
        }

        // Actually restore the file
        std::ofstream out_file(file_path);
        if (!out_file) {
            return DuckDBToolResult::error("Failed to write to " + file_path);
        }
        out_file << content;
        out_file.close();

        result["restored"] = true;
        return DuckDBToolResult::ok(
            "Restored " + file_path + " to version from " + format_time(edit.backup_time),
            result
        );
    }
