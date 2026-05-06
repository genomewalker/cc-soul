// long_task RPC handlers — bodies for declarations in
// chitta/include/chitta/rpc/handlers/long_task.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

namespace {
constexpr size_t kMinSnapshotTokens = 64;
constexpr size_t kMaxSnapshotTokens = 2000;
constexpr size_t kFieldPreviewChars = 240;
}

ToolResult FieldRpcHandler::tool_long_task_start(const json& params) {
    std::string task_id = params.value("task_id", "");
    std::string goal = params.value("goal", "");

    if (task_id.empty() || goal.empty()) {
        return ToolResult::error("task_id and goal are required");
    }

    std::string realm = params.value("realm", "brahman");

    json payload = {
        {"goal", goal},
        {"realm", realm},
        {"status", "active"},
        {"iterations", 0},
        {"completed_summary", ""}
    };

    if (params.contains("hard_checks") && params["hard_checks"].is_array())
        payload["hard_checks"] = params["hard_checks"];
    if (params.contains("soft_checks") && params["soft_checks"].is_array())
        payload["soft_checks"] = params["soft_checks"];
    if (params.contains("work_items") && params["work_items"].is_array())
        payload["work_items"] = params["work_items"];

    payload["blockers"] = json::array();

    int64_t ts = field_now_ms();
    int rc = field_store_->task_create(task_id, "long_task", payload.dump(), ts);
    if (rc != 0) {
        return ToolResult::error("Failed to create task: " + task_id);
    }

    // Start the task
    if (!field_store_->task_transition(task_id, "start", ts)) {
        return ToolResult::error("Failed to start task: " + task_id);
    }
    field_store_->emit_event("long_task", "start", task_id, payload.dump());

    std::ostringstream ss;
    ss << "Started long task:\n"
       << "  ID: " << task_id << "\n"
       << "  Goal: " << goal.substr(0, 100) << (goal.size() > 100 ? "..." : "") << "\n"
       << "  Realm: " << realm;

    return ToolResult::ok(ss.str(), {
        {"task_id", task_id},
        {"realm", realm},
        {"status", "active"}
    });
}

ToolResult FieldRpcHandler::tool_long_task_get(const json& params) {
    std::string task_id = params.value("task_id", "");
    if (task_id.empty()) {
        return ToolResult::error("task_id is required");
    }

    std::string raw = field_store_->task_get(task_id);
    if (raw.empty()) {
        return ToolResult::error("Task not found: " + task_id);
    }

    json task = parse_json_obj(raw);
    json payload = parse_json_obj(task.value("payload", ""));

    json result = {
        {"task_id", task_id},
        {"goal", payload.value("goal", "")},
        {"realm", payload.value("realm", "")},
        {"status", payload.value("status", task.value("status", ""))},
        {"iterations", payload.value("iterations", 0)},
        {"hard_checks", payload.value("hard_checks", json::array())},
        {"soft_checks", payload.value("soft_checks", json::array())},
        {"work_items", payload.value("work_items", json::array())},
        {"blockers", payload.value("blockers", json::array())}
    };

    std::string summary = payload.value("completed_summary", "");
    if (!summary.empty()) result["completed_summary"] = summary;
    std::string outcome = payload.value("outcome", "");
    if (!outcome.empty()) result["outcome"] = outcome;

    std::string goal = payload.value("goal", "");
    std::ostringstream ss;
    ss << "Task: " << task_id << " [" << result.value("status", "?") << "]\n"
       << "Goal: " << goal.substr(0, 200) << "\n"
       << "Iterations: " << payload.value("iterations", 0);

    return ToolResult::ok(ss.str(), result);
}

ToolResult FieldRpcHandler::tool_long_task_active(const json& params) {
    std::string filter_realm = params.value("realm", "");

    // Scan event log for active tasks.
    // Track which task_ids have been started vs completed/failed.
    std::unordered_map<std::string, json> active_tasks;
    std::unordered_set<std::string> finished;

    field_store_->iterate_log(0, [&](const std::string& op_json, uint64_t) {
        json op = parse_json_obj(op_json);
        if (op.value("domain", "") != "long_task") return;
        std::string entity_id = op.value("entity_id", "");
        std::string kind = op.value("kind", "");

        if (kind == "start") {
            json payload = parse_json_obj(op.value("payload", ""));
            if (!filter_realm.empty() && payload.value("realm", "") != filter_realm) return;
            active_tasks[entity_id] = payload;
            finished.erase(entity_id);
        } else if (kind == "complete" || kind == "fail") {
            active_tasks.erase(entity_id);
            finished.insert(entity_id);
        }
    });

    if (active_tasks.empty()) {
        return ToolResult::ok(
            "No active task" + (filter_realm.empty() ? "" : " in realm " + filter_realm),
            {{"found", false}});
    }

    // Return the first active task (unordered_map has no stable order)
    auto& [tid, payload] = *active_tasks.begin();

    // Fetch current state from task store for up-to-date iterations
    std::string raw = field_store_->task_get(tid);
    json current_payload = parse_json_obj(parse_json_obj(raw).value("payload", ""));

    json result = {
        {"found", true},
        {"task_id", tid},
        {"goal", current_payload.value("goal", payload.value("goal", ""))},
        {"realm", current_payload.value("realm", payload.value("realm", ""))},
        {"iterations", current_payload.value("iterations", 0)}
    };

    std::string goal = result.value("goal", "");
    std::ostringstream ss;
    ss << "Active task: " << tid << "\n"
       << "Goal: " << goal.substr(0, 200) << "\n"
       << "Iterations: " << result.value("iterations", 0);

    return ToolResult::ok(ss.str(), result);
}

ToolResult FieldRpcHandler::tool_long_task_update(const json& params) {
    std::string task_id = params.value("task_id", "");
    if (task_id.empty()) {
        return ToolResult::error("task_id is required");
    }

    // Get current payload
    std::string raw = field_store_->task_get(task_id);
    if (raw.empty()) {
        return ToolResult::error("Task not found: " + task_id);
    }

    json task = parse_json_obj(raw);
    json payload = parse_json_obj(task.value("payload", ""));

    // Apply updates
    if (params.contains("completed_summary")) {
        payload["completed_summary"] = params["completed_summary"];
    }
    if (params.contains("work_items") && params["work_items"].is_array()) {
        payload["work_items"] = params["work_items"];
    }
    if (params.contains("blockers") && params["blockers"].is_array()) {
        payload["blockers"] = params["blockers"];
    }

    payload["iterations"] = payload.value("iterations", 0) + 1;

    int64_t ts = field_now_ms();
    if (!field_store_->task_update_payload(task_id, payload.dump(), ts)) {
        return ToolResult::error("Failed to update task payload: " + task_id);
    }
    field_store_->emit_event("long_task", "update", task_id, payload.dump());

    return ToolResult::ok("Updated task: " + task_id, {{"task_id", task_id}, {"updated", true}});
}

ToolResult FieldRpcHandler::tool_long_task_complete(const json& params) {
    std::string task_id = params.value("task_id", "");
    std::string outcome = params.value("outcome", "");

    if (task_id.empty() || outcome.empty()) {
        return ToolResult::error("task_id and outcome are required");
    }

    std::string raw = field_store_->task_get(task_id);
    if (raw.empty()) {
        return ToolResult::error("Task not found: " + task_id);
    }

    json task = parse_json_obj(raw);
    json payload = parse_json_obj(task.value("payload", ""));
    payload["status"] = "completed";
    payload["outcome"] = outcome;
    int64_t ts = field_now_ms();
    if (!field_store_->task_update_payload(task_id, payload.dump(), ts)) {
        return ToolResult::error("Failed to update task payload: " + task_id);
    }

    if (!field_store_->task_transition(task_id, "complete", ts)) {
        return ToolResult::error("Failed to transition task to complete: " + task_id);
    }
    field_store_->emit_event("long_task", "complete", task_id, json({{"outcome", outcome}}).dump());

    return ToolResult::ok("Completed task: " + task_id, {
        {"task_id", task_id},
        {"status", "completed"},
        {"outcome", outcome}
    });
}

ToolResult FieldRpcHandler::tool_long_task_event(const json& params) {
    std::string task_id = params.value("task_id", "");
    std::string kind = params.value("kind", "");

    if (task_id.empty() || kind.empty()) {
        return ToolResult::error("task_id and kind are required");
    }

    json event_payload = {{"payload", params.value("payload", "")}};

    if (params.contains("tags") && params["tags"].is_array()) {
        event_payload["tags"] = params["tags"];
    }
    if (params.contains("related_entities") && params["related_entities"].is_array()) {
        event_payload["related_entities"] = params["related_entities"];
    }

    uint64_t event_id = field_store_->emit_event("long_task", kind, task_id, event_payload.dump());

    return ToolResult::ok("Event logged", {{"event_id", event_id}, {"task_id", task_id}, {"kind", kind}});
}

ToolResult FieldRpcHandler::tool_unified_checkpoint(const json& params) {
    std::string realm = params.value("realm", "brahman");
    std::string mood = params.value("mood", "flowing");
    std::string summary = params.value("summary", "");

    // Check for active long task in this realm by scanning events
    std::string active_task_id;
    field_store_->iterate_log(0, [&](const std::string& op_json, uint64_t) {
        json op = parse_json_obj(op_json);
        if (op.value("domain", "") != "long_task") return;
        std::string kind = op.value("kind", "");
        std::string entity_id = op.value("entity_id", "");

        if (kind == "start") {
            json payload = parse_json_obj(op.value("payload", ""));
            if (payload.value("realm", "") == realm || realm.empty()) {
                active_task_id = entity_id;
            }
        } else if (kind == "complete" || kind == "fail") {
            if (entity_id == active_task_id) active_task_id.clear();
        }
    });

    json ckpt_payload = {
        {"mood", mood},
        {"summary", summary}
    };

    if (params.contains("next_steps")) ckpt_payload["next_steps"] = params["next_steps"];
    if (params.contains("active_files")) ckpt_payload["active_files"] = params["active_files"];
    if (params.contains("discoveries")) ckpt_payload["discoveries"] = params["discoveries"];

    if (!active_task_id.empty()) {
        // Checkpoint as a long_task event
        uint64_t event_id = field_store_->emit_event("long_task", "checkpoint", active_task_id, ckpt_payload.dump());

        // Also update task summary if provided
        if (!summary.empty()) {
            std::string raw = field_store_->task_get(active_task_id);
            if (!raw.empty()) {
                json task = parse_json_obj(raw);
                json payload = parse_json_obj(task.value("payload", ""));
                payload["completed_summary"] = summary;
                if (!field_store_->task_update_payload(active_task_id, payload.dump(), field_now_ms())) {
                    return ToolResult::error("Failed to update active task checkpoint payload: " + active_task_id);
                }
            }
        }

        std::ostringstream ss;
        ss << "Checkpoint saved to long task: " << active_task_id << "\n"
           << "  Event #" << event_id << " (kind: checkpoint)\n"
           << "  Mood: " << mood;

        return ToolResult::ok(ss.str(), {
            {"mode", "long_task"},
            {"task_id", active_task_id},
            {"event_id", event_id}
        });
    } else {
        // Fallback to standalone ledger checkpoint
        std::string key = "checkpoint-" + std::to_string(std::time(nullptr));
        json ledger_payload = ckpt_payload;
        ledger_payload["session_id"] = key;
        ledger_payload["project"] = realm;
        ledger_payload["coherence"] = 0.85f;
        ledger_payload["confidence"] = 0.85f;
        ledger_payload["snapshot"] = summary;

        uint64_t event_id = field_store_->emit_event("ledger", "save", key, ledger_payload.dump());

        return ToolResult::ok(
            "Checkpoint saved to ledger event #" + std::to_string(event_id) + " (no active long task)",
            {{"mode", "ledger"}, {"event_id", event_id}}
        );
    }
}

ToolResult FieldRpcHandler::tool_long_task_snapshot(const json& params) {
    std::string task_id = params.value("task_id", "");
    std::string mode = params.value("mode", "inject");
    size_t max_tokens = params.value("max_tokens", 1200);
    max_tokens = std::clamp(max_tokens, kMinSnapshotTokens, kMaxSnapshotTokens);
    size_t max_chars = max_tokens * 4;

    if (task_id.empty()) {
        return ToolResult::error("task_id is required");
    }

    std::string raw = field_store_->task_get(task_id);
    if (raw.empty()) {
        return ToolResult::error("Task not found: " + task_id);
    }

    json task = parse_json_obj(raw);
    json payload = parse_json_obj(task.value("payload", ""));

    json work_items = payload.value("work_items", json::array());
    json blockers = payload.value("blockers", json::array());
    std::string goal = payload.value("goal", "");
    int iterations = payload.value("iterations", 0);
    std::string status = payload.value("status", task.value("status", ""));

    // Collect recent events for this task
    struct RecentEvent { std::string kind; std::string event_payload; };
    std::vector<RecentEvent> events;
    field_store_->iterate_log(0, [&](const std::string& op_json, uint64_t) {
        json op = parse_json_obj(op_json);
        if (op.value("domain", "") != "long_task") return;
        if (op.value("entity_id", "") != task_id) return;
        events.push_back({op.value("kind", ""), op.value("payload", "")});
    });
    // Keep only last 20
    if (events.size() > 20) {
        events.erase(events.begin(), events.end() - 20);
    }

    std::ostringstream ss;

    if (mode == "inject") {
        ss << "[LONG_TASK:" << task_id << "] " << goal.substr(0, kFieldPreviewChars) << "\n";
        ss << "Iteration: " << iterations << " | Status: " << status << "\n";

        std::string summary = payload.value("completed_summary", "");
        if (!summary.empty()) {
            ss << "Done: " << summary.substr(0, 200) << "\n";
        }

        if (!work_items.empty()) {
            ss << "Pending: ";
            for (size_t i = 0; i < std::min(work_items.size(), size_t(3)); i++) {
                if (i > 0) ss << "; ";
                std::string item = work_items[i].get<std::string>();
                ss << item.substr(0, 50);
            }
            if (work_items.size() > 3) ss << " (+" << (work_items.size() - 3) << " more)";
            ss << "\n";
        }

        if (!blockers.empty()) {
            ss << "BLOCKED: " << blockers[0].get<std::string>().substr(0, kFieldPreviewChars) << "\n";
        }

        int event_count = 0;
        for (const auto& e : events) {
            if (e.kind == "error" || e.kind == "decision") {
                ss << "[" << e.kind << "] " << e.event_payload.substr(0, 100) << "\n";
                if (++event_count >= 2) break;
            }
        }
    } else {
        ss << "=== Task Snapshot: " << task_id << " ===\n\n";
        ss << "Goal: " << goal.substr(0, kFieldPreviewChars) << "\n";
        ss << "Status: " << status << "\n";
        ss << "Realm: " << payload.value("realm", "") << "\n";
        ss << "Iterations: " << iterations << "\n\n";

        ss << "Completed: " << payload.value("completed_summary", "").substr(0, kFieldPreviewChars) << "\n\n";

        ss << "Work Items:\n";
        for (const auto& item : work_items) {
            ss << "  - " << item.get<std::string>().substr(0, kFieldPreviewChars) << "\n";
        }

        ss << "\nBlockers:\n";
        for (const auto& b : blockers) {
            ss << "  ! " << b.get<std::string>().substr(0, kFieldPreviewChars) << "\n";
        }

        ss << "\nRecent Events (" << events.size() << "):\n";
        for (const auto& e : events) {
            ss << "  [" << e.kind << "] " << e.event_payload.substr(0, 200) << "\n";
        }
    }

    std::string snapshot = ss.str();
    bool truncated = false;
    if (snapshot.size() > max_chars) {
        snapshot = snapshot.substr(0, max_chars) + "\n... (truncated)";
        truncated = true;
    }

    json result = {
        {"task_id", task_id},
        {"status", status},
        {"iterations", iterations},
        {"event_count", events.size()},
        {"truncated", truncated},
        {"snapshot", snapshot}
    };

    return ToolResult::ok(snapshot, result);
}

ToolResult FieldRpcHandler::tool_long_task_evaluate(const json& params) {
    std::string task_id = params.value("task_id", "");
    if (task_id.empty()) {
        return ToolResult::error("task_id is required");
    }

    std::string raw = field_store_->task_get(task_id);
    if (raw.empty()) {
        return ToolResult::error("Task not found: " + task_id);
    }

    json task = parse_json_obj(raw);
    json payload = parse_json_obj(task.value("payload", ""));

    json hard_checks = payload.value("hard_checks", json::array());
    json blockers = payload.value("blockers", json::array());

    std::string decision = "continue";
    std::vector<std::string> missing;
    float confidence = 0.5f;
    std::string next_prompt;

    if (!blockers.empty()) {
        decision = "blocked";
        confidence = 0.9f;
        for (const auto& b : blockers) {
            missing.push_back(b.get<std::string>());
        }
        next_prompt = "Task is blocked. Blockers: " + blockers.dump();
    } else if (hard_checks.empty()) {
        decision = "continue";
        confidence = 0.3f;
        next_prompt = "Continue working on: " + payload.value("goal", "");
    } else {
        decision = "continue";
        confidence = 0.5f;
        next_prompt = "Continue task. Check completion criteria when ready.";
        for (const auto& c : hard_checks) {
            missing.push_back(c.get<std::string>());
        }
    }

    std::ostringstream ss;
    ss << "Evaluation: " << decision << " (confidence: " << confidence << ")\n";
    if (!missing.empty()) {
        ss << "Missing/Blocked:\n";
        for (const auto& m : missing) {
            ss << "  - " << m << "\n";
        }
    }
    ss << "\nNext: " << next_prompt;

    json result = {
        {"decision", decision},
        {"confidence", confidence},
        {"missing", missing},
        {"next_prompt", next_prompt},
        {"iterations", payload.value("iterations", 0)}
    };

    return ToolResult::ok(ss.str(), result);
}

} // namespace chitta
