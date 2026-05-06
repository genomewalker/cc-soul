// ledger RPC handlers — bodies for declarations in
// chitta/include/chitta/rpc/handlers/ledger.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

namespace {
std::string truncate_snapshot(const std::string& s, size_t limit = 2000) {
    return s.size() > limit ? s.substr(0, limit) + "..." : s;
}
}

ToolResult FieldRpcHandler::tool_ledger_save(const json& params) {
    std::string session_id = get_session_id(params);
    std::string project = params.value("project", "default");
    std::string key = ledger_key(session_id, project);

    json payload = {
        {"session_id", session_id},
        {"project", project},
        {"transcript_path", params.value("transcript_path", "")},
        {"mood", params.value("mood", "")},
        {"coherence", params.value("coherence", 0.0f)},
        {"confidence", params.value("confidence", 0.0f)},
        {"snapshot", params.value("snapshot", "")}
    };

    auto store_array = [&](const char* field) {
        if (params.contains(field) && params[field].is_array()) {
            payload[field] = params[field];
        } else {
            payload[field] = json::array();
        }
    };

    store_array("todos");
    store_array("active_files");
    store_array("decisions");
    store_array("next_steps");
    store_array("blockers");
    store_array("discoveries");

    uint64_t event_id = field_store_->emit_event("ledger", "save", key, payload.dump());

    std::ostringstream ss;
    ss << "Checkpoint saved:\n";
    ss << "  Event: " << event_id << "\n";
    ss << "  Session: " << session_id << "\n";
    ss << "  Project: " << project << "\n";
    if (!payload["mood"].get<std::string>().empty()) {
        ss << "  Mood: " << payload["mood"].get<std::string>() << "\n";
    }

    return ToolResult::ok(ss.str(), {
        {"event_id", event_id},
        {"session_id", session_id},
        {"project", project}
    });
}

ToolResult FieldRpcHandler::tool_ledger_load(const json& params) {
    std::string session_id = params.value("session_id", "");
    std::string project = params.value("project", "");
    std::string key = ledger_key(session_id, project);

    auto payload_str = field_store_->get_latest_event("ledger", "save", key);
    if (!payload_str) {
        return ToolResult::ok("No checkpoint found", {{"found", false}});
    }

    json entry = parse_json_safe(*payload_str);
    const bool include_snapshot = params.value("include_snapshot", false);
    const std::string snapshot = entry.value("snapshot", "");
    const bool snapshot_truncated = snapshot.size() > 2000;
    const std::string snapshot_preview = truncate_snapshot(snapshot);

    std::ostringstream ss;
    ss << "Checkpoint loaded:\n";
    ss << "  Session: " << entry.value("session_id", "") << "\n";
    ss << "  Project: " << entry.value("project", "") << "\n";
    if (!entry.value("transcript_path", "").empty())
        ss << "  Transcript: " << entry.value("transcript_path", "") << "\n";
    if (!entry.value("mood", "").empty())
        ss << "  Mood: " << entry.value("mood", "") << "\n";
    float coherence = entry.value("coherence", 0.0f);
    float confidence = entry.value("confidence", 0.0f);
    if (coherence > 0) ss << "  Coherence: " << coherence << "\n";
    if (confidence > 0) ss << "  Confidence: " << confidence << "\n";

    json result = {
        {"found", true},
        {"session_id", entry.value("session_id", "")},
        {"project", entry.value("project", "")},
        {"transcript_path", entry.value("transcript_path", "")},
        {"mood", entry.value("mood", "")},
        {"coherence", coherence},
        {"confidence", confidence},
        {"snapshot", include_snapshot ? snapshot_preview : ""},
        {"snapshot_truncated", snapshot_truncated},
        {"todos", entry.value("todos", json::array())},
        {"active_files", entry.value("active_files", json::array())},
        {"decisions", entry.value("decisions", json::array())},
        {"next_steps", entry.value("next_steps", json::array())},
        {"blockers", entry.value("blockers", json::array())},
        {"discoveries", entry.value("discoveries", json::array())}
    };

    return ToolResult::ok(ss.str(), result);
}

ToolResult FieldRpcHandler::tool_ledger_list(const json& params) {
    std::string filter_project = params.value("project", "");
    size_t limit = params.value("limit", 10);

    // Scan the event log for ledger save events, collect unique keys
    // keeping only the latest event per key (skipping tombstoned ones).
    struct LedgerEntry {
        std::string key;
        uint64_t seqno;
        json payload;
    };
    std::unordered_map<std::string, LedgerEntry> latest;
    std::unordered_set<std::string> deleted;

    field_store_->iterate_log(0, [&](const std::string& op_json, uint64_t seqno) {
        json op = parse_json_safe(op_json);
        if (op.value("domain", "") != "ledger") return;

        std::string kind = op.value("kind", "");
        std::string entity_id = op.value("entity_id", "");

        if (kind == "delete") {
            deleted.insert(entity_id);
            latest.erase(entity_id);
        } else if (kind == "save") {
            if (deleted.count(entity_id)) return;
            json payload = parse_json_safe(op.value("payload", ""));
            if (!filter_project.empty() && payload.value("project", "") != filter_project) return;
            latest[entity_id] = {entity_id, seqno, payload};
        }
    });

    // Sort by seqno descending (most recent first)
    std::vector<LedgerEntry> entries;
    entries.reserve(latest.size());
    for (auto& [k, v] : latest) entries.push_back(std::move(v));
    std::sort(entries.begin(), entries.end(),
              [](const LedgerEntry& a, const LedgerEntry& b) { return a.seqno > b.seqno; });
    if (entries.size() > limit) entries.resize(limit);

    std::ostringstream ss;
    ss << "Checkpoints";
    if (!filter_project.empty()) ss << " for project '" << filter_project << "'";
    ss << " (" << entries.size() << "):\n\n";

    json entries_json = json::array();
    for (const auto& e : entries) {
        ss << "  [" << e.seqno << "] " << e.payload.value("session_id", "");
        std::string mood = e.payload.value("mood", "");
        if (!mood.empty()) ss << " (" << mood << ")";
        ss << "\n";

        entries_json.push_back({
            {"key", e.key},
            {"session_id", e.payload.value("session_id", "")},
            {"project", e.payload.value("project", "")},
            {"mood", mood}
        });
    }

    return ToolResult::ok(ss.str(), {{"entries", entries_json}, {"count", entries.size()}});
}

ToolResult FieldRpcHandler::tool_ledger_get(const json& params) {
    // Accept either "id" (legacy numeric) or "key" (session:project).
    // For field-based storage, key is the canonical identifier.
    std::string key = params.value("key", "");
    if (key.empty()) {
        // Legacy: build key from session_id + project
        std::string session_id = params.value("session_id", "");
        std::string project = params.value("project", "");
        if (session_id.empty()) {
            return ToolResult::error("key or session_id required");
        }
        key = ledger_key(session_id, project);
    }

    auto payload_str = field_store_->get_latest_event("ledger", "save", key);
    if (!payload_str) {
        return ToolResult::error("Checkpoint not found: " + key);
    }

    json entry = parse_json_safe(*payload_str);
    const bool include_snapshot = params.value("include_snapshot", false);
    const std::string snapshot = entry.value("snapshot", "");
    const bool snapshot_truncated = snapshot.size() > 2000;
    const std::string snapshot_preview = truncate_snapshot(snapshot);

    std::ostringstream ss;
    ss << "Checkpoint " << key << ":\n";
    ss << "  Session: " << entry.value("session_id", "") << "\n";
    ss << "  Project: " << entry.value("project", "") << "\n";
    if (!entry.value("transcript_path", "").empty())
        ss << "  Transcript: " << entry.value("transcript_path", "") << "\n";
    if (!entry.value("mood", "").empty())
        ss << "  Mood: " << entry.value("mood", "") << "\n";
    float coherence = entry.value("coherence", 0.0f);
    float confidence = entry.value("confidence", 0.0f);
    if (coherence > 0) ss << "  Coherence: " << coherence << "\n";
    if (confidence > 0) ss << "  Confidence: " << confidence << "\n";
    if (include_snapshot && !snapshot_preview.empty()) {
        ss << "\nSnapshot:\n" << snapshot_preview << "\n";
    }

    json result = {
        {"key", key},
        {"session_id", entry.value("session_id", "")},
        {"project", entry.value("project", "")},
        {"transcript_path", entry.value("transcript_path", "")},
        {"mood", entry.value("mood", "")},
        {"coherence", coherence},
        {"confidence", confidence},
        {"snapshot", include_snapshot ? snapshot_preview : ""},
        {"snapshot_truncated", snapshot_truncated},
        {"todos", entry.value("todos", json::array())},
        {"active_files", entry.value("active_files", json::array())},
        {"decisions", entry.value("decisions", json::array())},
        {"next_steps", entry.value("next_steps", json::array())},
        {"blockers", entry.value("blockers", json::array())},
        {"discoveries", entry.value("discoveries", json::array())}
    };

    return ToolResult::ok(ss.str(), result);
}

ToolResult FieldRpcHandler::tool_ledger_delete(const json& params) {
    std::string key = params.value("key", "");
    if (key.empty()) {
        std::string session_id = params.value("session_id", "");
        std::string project = params.value("project", "");
        if (session_id.empty()) {
            return ToolResult::error("key or session_id required");
        }
        key = ledger_key(session_id, project);
    }

    field_store_->emit_event("ledger", "delete", key, "");
    return ToolResult::ok("Deleted checkpoint " + key, {{"key", key}, {"deleted", true}});
}

} // namespace chitta
