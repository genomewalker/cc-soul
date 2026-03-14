// Included into DuckDBHandler class body — not a standalone header

    DuckDBToolResult tool_long_task_start(const json& params) {
        std::string task_id = params.value("task_id", "");
        std::string goal = params.value("goal", "");

        if (task_id.empty() || goal.empty()) {
            return DuckDBToolResult::error("task_id and goal are required");
        }

        LongTask task;
        task.task_id = task_id;
        task.goal = goal;
        task.realm = params.value("realm", "brahman");

        if (params.contains("hard_checks") && params["hard_checks"].is_array()) {
            task.hard_checks = params["hard_checks"].dump();
        }
        if (params.contains("soft_checks") && params["soft_checks"].is_array()) {
            task.soft_checks = params["soft_checks"].dump();
        }
        if (params.contains("work_items") && params["work_items"].is_array()) {
            task.work_items = params["work_items"].dump();
        }

        int64_t id = mind_->store().task_start(task);
        if (id < 0) {
            return DuckDBToolResult::error("Failed to start task: " + mind_->store().last_error());
        }

        std::ostringstream ss;
        ss << "Started long task:\n"
           << "  ID: " << task_id << "\n"
           << "  Goal: " << goal.substr(0, 100) << (goal.size() > 100 ? "..." : "") << "\n"
           << "  Realm: " << task.realm;

        return DuckDBToolResult::ok(ss.str(), {
            {"task_id", task_id},
            {"db_id", id},
            {"realm", task.realm},
            {"status", "active"}
        });
    }

    DuckDBToolResult tool_long_task_get(const json& params) {
        std::string task_id = params.value("task_id", "");
        if (task_id.empty()) {
            return DuckDBToolResult::error("task_id is required");
        }

        auto task = mind_->store().task_get(task_id);
        if (!task) {
            return DuckDBToolResult::error("Task not found: " + task_id);
        }

        json result = {
            {"task_id", task->task_id},
            {"goal", task->goal},
            {"realm", task->realm},
            {"status", task->status},
            {"iterations", task->iterations},
            {"started_at", task->started_at},
            {"updated_at", task->updated_at}
        };

        // Parse JSON fields
        auto parse_json = [](const std::string& s) -> json {
            if (s.empty()) return json::array();
            try { return json::parse(s); } catch (...) { return json::array(); }
        };

        result["hard_checks"] = parse_json(task->hard_checks);
        result["soft_checks"] = parse_json(task->soft_checks);
        result["work_items"] = parse_json(task->work_items);
        result["blockers"] = parse_json(task->blockers);

        if (!task->completed_summary.empty()) result["completed_summary"] = task->completed_summary;
        if (!task->outcome.empty()) result["outcome"] = task->outcome;
        if (task->completed_at > 0) result["completed_at"] = task->completed_at;

        std::ostringstream ss;
        ss << "Task: " << task->task_id << " [" << task->status << "]\n"
           << "Goal: " << task->goal.substr(0, 200) << "\n"
           << "Iterations: " << task->iterations;

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_long_task_active(const json& params) {
        std::string realm = params.value("realm", "");

        auto task = mind_->store().task_get_active(realm);
        if (!task) {
            return DuckDBToolResult::ok("No active task" + (realm.empty() ? "" : " in realm " + realm),
                                        {{"found", false}});
        }

        json result = {
            {"found", true},
            {"task_id", task->task_id},
            {"goal", task->goal},
            {"realm", task->realm},
            {"iterations", task->iterations}
        };

        std::ostringstream ss;
        ss << "Active task: " << task->task_id << "\n"
           << "Goal: " << task->goal.substr(0, 200) << "\n"
           << "Iterations: " << task->iterations;

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_long_task_update(const json& params) {
        std::string task_id = params.value("task_id", "");
        if (task_id.empty()) {
            return DuckDBToolResult::error("task_id is required");
        }

        LongTask updates;
        updates.completed_summary = params.value("completed_summary", "");

        if (params.contains("work_items") && params["work_items"].is_array()) {
            updates.work_items = params["work_items"].dump();
        }
        if (params.contains("blockers") && params["blockers"].is_array()) {
            updates.blockers = params["blockers"].dump();
        }

        updates.iterations = 1;  // Signal to increment

        bool ok = mind_->store().task_update(task_id, updates);
        if (!ok) {
            return DuckDBToolResult::error("Failed to update task: " + task_id);
        }

        return DuckDBToolResult::ok("Updated task: " + task_id, {{"task_id", task_id}, {"updated", true}});
    }

    DuckDBToolResult tool_long_task_complete(const json& params) {
        std::string task_id = params.value("task_id", "");
        std::string outcome = params.value("outcome", "");

        if (task_id.empty() || outcome.empty()) {
            return DuckDBToolResult::error("task_id and outcome are required");
        }

        bool ok = mind_->store().task_complete(task_id, outcome);
        if (!ok) {
            return DuckDBToolResult::error("Failed to complete task: " + task_id);
        }

        return DuckDBToolResult::ok("Completed task: " + task_id, {
            {"task_id", task_id},
            {"status", "completed"},
            {"outcome", outcome}
        });
    }

    DuckDBToolResult tool_long_task_event(const json& params) {
        std::string task_id = params.value("task_id", "");
        std::string kind = params.value("kind", "");

        if (task_id.empty() || kind.empty()) {
            return DuckDBToolResult::error("task_id and kind are required");
        }

        TaskEvent event;
        event.task_id = task_id;
        event.kind = kind;
        event.payload = params.value("payload", "");

        if (params.contains("tags") && params["tags"].is_array()) {
            event.tags = params["tags"].dump();
        }
        if (params.contains("related_entities") && params["related_entities"].is_array()) {
            event.related_entities = params["related_entities"].dump();
        }

        int64_t id = mind_->store().event_append(event);
        if (id < 0) {
            return DuckDBToolResult::error("Failed to append event: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Event logged", {{"event_id", id}, {"task_id", task_id}, {"kind", kind}});
    }

    DuckDBToolResult tool_unified_checkpoint(const json& params) {
        std::string realm = params.value("realm", "brahman");
        std::string mood = params.value("mood", "flowing");
        std::string summary = params.value("summary", "");

        // Check for active long task in this realm
        auto active_task = mind_->store().task_get_active(realm);

        json payload = {
            {"mood", mood},
            {"summary", summary}
        };

        if (params.contains("next_steps")) payload["next_steps"] = params["next_steps"];
        if (params.contains("active_files")) payload["active_files"] = params["active_files"];
        if (params.contains("discoveries")) payload["discoveries"] = params["discoveries"];

        if (active_task) {
            // Use long task event system
            TaskEvent event;
            event.task_id = active_task->task_id;
            event.kind = "checkpoint";
            event.payload = payload.dump();

            if (params.contains("active_files") && params["active_files"].is_array()) {
                event.related_entities = params["active_files"].dump();
            }

            int64_t id = mind_->store().event_append(event);

            // Also update task's completed_summary if summary provided
            if (!summary.empty()) {
                LongTask updates;
                updates.completed_summary = summary;
                mind_->store().task_update(active_task->task_id, updates);
            }

            std::ostringstream ss;
            ss << "Checkpoint saved to long task: " << active_task->task_id << "\n"
               << "  Event #" << id << " (kind: checkpoint)\n"
               << "  Mood: " << mood;

            return DuckDBToolResult::ok(ss.str(), {
                {"mode", "long_task"},
                {"task_id", active_task->task_id},
                {"event_id", id}
            });
        } else {
            // Fallback to standalone ledger
            LedgerEntry entry;
            entry.session_id = "checkpoint-" + std::to_string(std::time(nullptr));
            entry.project = realm;
            entry.mood = mood;
            entry.coherence = 0.85f;
            entry.confidence = 0.85f;

            if (params.contains("next_steps") && params["next_steps"].is_array()) {
                entry.next_steps = params["next_steps"].dump();
            }
            if (params.contains("active_files") && params["active_files"].is_array()) {
                entry.active_files = params["active_files"].dump();
            }
            if (params.contains("discoveries") && params["discoveries"].is_array()) {
                entry.discoveries = params["discoveries"].dump();
            }
            entry.snapshot = summary;

            int64_t id = mind_->store().save_ledger(entry);

            return DuckDBToolResult::ok(
                "Checkpoint saved to ledger #" + std::to_string(id) + " (no active long task)",
                {{"mode", "ledger"}, {"ledger_id", id}}
            );
        }
    }

    DuckDBToolResult tool_long_task_snapshot(const json& params) {
        std::string task_id = params.value("task_id", "");
        std::string mode = params.value("mode", "inject");
        size_t max_tokens = params.value("max_tokens", 2000);
        size_t max_chars = max_tokens * 4;  // Rough estimate: ~4 chars per token

        if (task_id.empty()) {
            return DuckDBToolResult::error("task_id is required");
        }

        auto task = mind_->store().task_get(task_id);
        if (!task) {
            return DuckDBToolResult::error("Task not found: " + task_id);
        }

        // Get recent events
        auto events = mind_->store().event_get_recent(task_id, "", 20);

        // Parse JSON fields
        auto parse_json = [](const std::string& s) -> json {
            if (s.empty()) return json::array();
            try { return json::parse(s); } catch (...) { return json::array(); }
        };

        json work_items = parse_json(task->work_items);
        json blockers = parse_json(task->blockers);

        // Build snapshot
        std::ostringstream ss;

        if (mode == "inject") {
            // Compact format for context injection
            ss << "[LONG_TASK:" << task_id << "] " << task->goal << "\n";
            ss << "Iteration: " << task->iterations << " | Status: " << task->status << "\n";

            if (!task->completed_summary.empty()) {
                ss << "Done: " << task->completed_summary.substr(0, 200) << "\n";
            }

            if (!work_items.empty()) {
                ss << "Pending: ";
                for (size_t i = 0; i < std::min(work_items.size(), size_t(3)); i++) {
                    if (i > 0) ss << "; ";
                    ss << work_items[i].get<std::string>().substr(0, 50);
                }
                if (work_items.size() > 3) ss << " (+" << (work_items.size() - 3) << " more)";
                ss << "\n";
            }

            if (!blockers.empty()) {
                ss << "BLOCKED: " << blockers[0].get<std::string>() << "\n";
            }

            // Recent significant events
            int event_count = 0;
            for (const auto& e : events) {
                if (e.kind == "error" || e.kind == "decision") {
                    ss << "[" << e.kind << "] " << e.payload.substr(0, 100) << "\n";
                    if (++event_count >= 2) break;
                }
            }
        } else {
            // Verbose debug format
            ss << "=== Task Snapshot: " << task_id << " ===\n\n";
            ss << "Goal: " << task->goal << "\n";
            ss << "Status: " << task->status << "\n";
            ss << "Realm: " << task->realm << "\n";
            ss << "Iterations: " << task->iterations << "\n\n";

            ss << "Completed: " << task->completed_summary << "\n\n";

            ss << "Work Items:\n";
            for (const auto& item : work_items) {
                ss << "  - " << item.get<std::string>() << "\n";
            }

            ss << "\nBlockers:\n";
            for (const auto& b : blockers) {
                ss << "  ! " << b.get<std::string>() << "\n";
            }

            ss << "\nRecent Events (" << events.size() << "):\n";
            for (const auto& e : events) {
                ss << "  [" << e.kind << "] " << e.payload.substr(0, 200) << "\n";
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
            {"status", task->status},
            {"iterations", task->iterations},
            {"event_count", events.size()},
            {"truncated", truncated},
            {"snapshot", snapshot}
        };

        return DuckDBToolResult::ok(snapshot, result);
    }

    DuckDBToolResult tool_long_task_evaluate(const json& params) {
        std::string task_id = params.value("task_id", "");
        if (task_id.empty()) {
            return DuckDBToolResult::error("task_id is required");
        }

        auto task = mind_->store().task_get(task_id);
        if (!task) {
            return DuckDBToolResult::error("Task not found: " + task_id);
        }

        // Parse hard_checks
        auto parse_json = [](const std::string& s) -> json {
            if (s.empty()) return json::array();
            try { return json::parse(s); } catch (...) { return json::array(); }
        };

        json hard_checks = parse_json(task->hard_checks);
        json blockers = parse_json(task->blockers);

        // For now, basic evaluation:
        // - If blockers exist -> blocked
        // - If hard_checks empty -> continue (no criteria defined)
        // - Otherwise -> continue (need semantic evaluation)

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
            // No completion criteria defined
            decision = "continue";
            confidence = 0.3f;
            next_prompt = "Continue working on: " + task->goal;
        } else {
            // Has criteria but we can't evaluate them automatically yet
            // This would need shell command execution or semantic evaluation
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
            {"iterations", task->iterations}
        };

        return DuckDBToolResult::ok(ss.str(), result);
    }
