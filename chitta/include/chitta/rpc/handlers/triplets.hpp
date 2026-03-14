// Included into DuckDBHandler class body — not a standalone header

    DuckDBToolResult tool_connect(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string subject = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string object = params.value("object", "");

        if (subject.empty() || predicate.empty() || object.empty()) {
            return DuckDBToolResult::error("Subject, predicate, and object are required");
        }

        bool ok = mind_->connect(subject, predicate, object);
        if (!ok) {
            return DuckDBToolResult::error("Failed to create triplet");
        }

        return DuckDBToolResult::ok(
            "Connected: " + subject + " → " + predicate + " → " + object,
            {{"subject", subject}, {"predicate", predicate}, {"object", object}}
        );
    }

    DuckDBToolResult tool_query_graph(const json& params) {
        std::string subject = params.value("subject", "");
        std::string object = params.value("object", "");

        json results_json = json::array();
        std::ostringstream ss;

        if (!subject.empty()) {
            auto results = mind_->query_subject(subject);
            ss << "Triplets with subject '" << subject << "':\n";
            for (const auto& [pred, obj, weight] : results) {
                ss << "  → " << pred << " → " << obj << "\n";
                results_json.push_back({
                    {"subject", subject},
                    {"predicate", pred},
                    {"object", obj},
                    {"weight", weight}
                });
            }
        }

        if (!object.empty()) {
            auto results = mind_->query_object(object);
            ss << "Triplets with object '" << object << "':\n";
            for (const auto& [subj, pred, weight] : results) {
                ss << "  " << subj << " → " << pred << " →\n";
                results_json.push_back({
                    {"subject", subj},
                    {"predicate", pred},
                    {"object", object},
                    {"weight", weight}
                });
            }
        }

        if (subject.empty() && object.empty()) {
            return DuckDBToolResult::error("Either subject or object is required");
        }

        return DuckDBToolResult::ok(ss.str(), {{"triplets", results_json}});
    }

    DuckDBToolResult tool_query_triplets_temporal(const json& params) {
        std::string subject = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string object = params.value("object", "");
        std::string at_date = params.value("at_date", "");
        size_t limit = params.value("limit", 50);

        // Parse at_date to timestamp
        int64_t at_time_ms = 0;
        if (!at_date.empty()) {
            auto resolved = TemporalResolver::resolve(at_date, TemporalResolver::now_ms());
            if (resolved) {
                at_time_ms = resolved->timestamp_ms;
            } else {
                return DuckDBToolResult::error("Invalid date format: " + at_date);
            }
        }

        auto triplets = mind_->store().query_triplets_temporal(subject, predicate, object, at_time_ms, limit);

        // Track traversal for convergence metrics
        if (!triplets.empty()) {
            std::ostringstream upd;
            upd << "UPDATE triplet SET use_count = use_count + 1, last_used_at = "
                << TemporalResolver::now_ms() << " WHERE id IN (";
            for (size_t i = 0; i < triplets.size(); ++i) {
                if (i > 0) upd << ",";
                upd << triplets[i].id;
            }
            upd << ")";
            mind_->store().execute_raw(upd.str());
        }

        std::ostringstream ss;
        json results_json = json::array();

        if (at_time_ms > 0) {
            ss << "Triplets valid at " << TemporalResolver::format_iso_date(at_time_ms) << ":\n";
        } else {
            ss << "Current triplets:\n";
        }

        for (const auto& t : triplets) {
            ss << "  " << t.subject << " → " << t.predicate << " → " << t.object;
            if (t.valid_from_ms > 0) {
                ss << " (from " << TemporalResolver::format_iso_date(t.valid_from_ms);
                if (t.valid_to_ms > 0) {
                    ss << " to " << TemporalResolver::format_iso_date(t.valid_to_ms);
                }
                ss << ")";
            }
            ss << "\n";

            results_json.push_back({
                {"id", t.id},
                {"subject", t.subject},
                {"predicate", t.predicate},
                {"object", t.object},
                {"weight", t.weight},
                {"valid_from_ms", t.valid_from_ms},
                {"valid_to_ms", t.valid_to_ms},
                {"valid_from", t.valid_from_ms > 0 ? TemporalResolver::format_iso_date(t.valid_from_ms) : ""},
                {"valid_to", t.valid_to_ms > 0 ? TemporalResolver::format_iso_date(t.valid_to_ms) : ""}
            });
        }

        if (triplets.empty()) {
            ss << "  (no matching triplets found)\n";
        }

        return DuckDBToolResult::ok(ss.str(), {{"triplets", results_json}, {"count", triplets.size()}});
    }

    DuckDBToolResult tool_triplet_history(const json& params) {
        std::string subject = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        size_t limit = params.value("limit", 20);

        if (subject.empty() || predicate.empty()) {
            return DuckDBToolResult::error("Subject and predicate are required");
        }

        auto history = mind_->store().query_triplet_history(subject, predicate, limit);

        std::ostringstream ss;
        json results_json = json::array();

        ss << "History of " << subject << " → " << predicate << ":\n";

        for (const auto& t : history) {
            ss << "  " << t.object;
            if (t.valid_from_ms > 0) {
                ss << " (from " << TemporalResolver::format_iso_date(t.valid_from_ms);
                if (t.valid_to_ms > 0) {
                    ss << " to " << TemporalResolver::format_iso_date(t.valid_to_ms);
                } else {
                    ss << " to now";
                }
                ss << ")";
            }
            if (t.superseded_by > 0) {
                ss << " [superseded]";
            }
            ss << "\n";

            results_json.push_back({
                {"id", t.id},
                {"object", t.object},
                {"valid_from_ms", t.valid_from_ms},
                {"valid_to_ms", t.valid_to_ms},
                {"valid_from", t.valid_from_ms > 0 ? TemporalResolver::format_iso_date(t.valid_from_ms) : ""},
                {"valid_to", t.valid_to_ms > 0 ? TemporalResolver::format_iso_date(t.valid_to_ms) : ""},
                {"superseded_by", t.superseded_by}
            });
        }

        if (history.empty()) {
            ss << "  (no history found)\n";
        }

        return DuckDBToolResult::ok(ss.str(), {{"history", results_json}, {"count", history.size()}});
    }

    DuckDBToolResult tool_connect_temporal(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string subject = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string object = params.value("object", "");
        std::string valid_from = params.value("valid_from", "");
        std::string valid_to = params.value("valid_to", "");
        std::string context_date = params.value("context_date", "");

        if (subject.empty() || predicate.empty() || object.empty()) {
            return DuckDBToolResult::error("Subject, predicate, and object are required");
        }

        int64_t context_date_ms = TemporalResolver::now_ms();
        if (!context_date.empty()) {
            auto resolved = TemporalResolver::resolve(context_date, context_date_ms);
            if (resolved) {
                context_date_ms = resolved->timestamp_ms;
            }
        }

        int64_t valid_from_ms = 0;
        if (!valid_from.empty()) {
            auto resolved = TemporalResolver::resolve(valid_from, context_date_ms);
            if (resolved) {
                valid_from_ms = resolved->timestamp_ms;
            } else {
                return DuckDBToolResult::error("Invalid valid_from date: " + valid_from);
            }
        }

        int64_t valid_to_ms = 0;
        if (!valid_to.empty()) {
            auto resolved = TemporalResolver::resolve(valid_to, context_date_ms);
            if (resolved) {
                valid_to_ms = resolved->timestamp_ms;
            } else {
                return DuckDBToolResult::error("Invalid valid_to date: " + valid_to);
            }
        }

        bool ok = mind_->store().connect_temporal(
            subject, predicate, object, 1.0f,
            valid_from_ms, valid_to_ms, context_date_ms
        );

        if (!ok) {
            return DuckDBToolResult::error("Failed to create temporal triplet");
        }

        std::ostringstream ss;
        ss << "Connected: " << subject << " → " << predicate << " → " << object;
        if (valid_from_ms > 0) {
            ss << " (from " << TemporalResolver::format_iso_date(valid_from_ms);
            if (valid_to_ms > 0) {
                ss << " to " << TemporalResolver::format_iso_date(valid_to_ms);
            }
            ss << ")";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"subject", subject},
            {"predicate", predicate},
            {"object", object},
            {"valid_from_ms", valid_from_ms},
            {"valid_to_ms", valid_to_ms}
        });
    }

    DuckDBToolResult tool_get_entities(const json& params) {
        std::string entity_type = params.value("type", "");
        size_t limit = params.value("limit", 20);

        auto entities = mind_->store().get_top_entities(entity_type, limit);

        json result;
        result["entities"] = json::array();
        result["count"] = entities.size();

        for (const auto& e : entities) {
            json ent;
            ent["id"] = e.id;
            ent["name"] = e.name;
            ent["display_name"] = e.display_name;
            ent["type"] = e.entity_type;
            ent["mention_count"] = e.mention_count;
            ent["salience"] = e.salience_score;
            ent["last_mentioned"] = e.last_mentioned;
            result["entities"].push_back(ent);
        }

        std::ostringstream msg;
        msg << "Found " << entities.size() << " entity/entities";

        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_get_relationship_events(const json& params) {
        std::string event_type = params.value("event_type", "");
        std::string session_id = params.value("session_id", "");
        size_t limit = params.value("limit", 20);

        auto events = mind_->store().get_relationship_events(event_type, session_id, limit);

        json result;
        result["events"] = json::array();
        result["count"] = events.size();

        for (const auto& e : events) {
            json evt;
            evt["id"] = e.id;
            evt["session_id"] = e.session_id;
            evt["event_type"] = e.event_type;
            evt["content"] = e.content.substr(0, 300);
            evt["context"] = e.context;
            evt["resolved"] = e.resolved;
            evt["created_at"] = e.created_at;
            result["events"].push_back(evt);
        }

        std::ostringstream msg;
        msg << "Found " << events.size() << " relationship event(s)";

        return DuckDBToolResult::ok(msg.str(), result);
    }
