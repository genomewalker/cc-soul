// Included into FieldRpcHandler class body — not a standalone header.
// Behavioral probe: detect sycophancy, hedging, and shallow reasoning in LLM outputs.
//
// Approach: embed text → cosine similarity to learned centroid clusters.
// No model retraining; clusters are stored as probe_centroid memories in chitta.
//
// Tools:
//   probe_seed       — store exemplar text as a centroid for a behavioral class
//   behavioral_probe — score text against all centroid clusters, return class scores
//   probe_calibrate  — add confirmed example to cluster, recompute mean centroid
//   probe_status     — list classes and their centroid counts

    ToolResult tool_probe_seed(const json& params) {
        std::string class_name = params.value("class", "");
        std::string text       = params.value("text", "");
        std::string note       = params.value("note", "");

        if (class_name.empty() || text.empty()) {
            return ToolResult::error("class and text are required");
        }
        static const std::unordered_set<std::string> valid_classes = {
            "sycophantic", "hedging", "shallow", "direct"
        };
        if (valid_classes.find(class_name) == valid_classes.end()) {
            return ToolResult::error(
                "class must be one of: sycophantic, hedging, shallow, direct");
        }

        auto embedding = embed_query(text);
        if (embedding.empty()) {
            return ToolResult::error("embedding failed");
        }

        // Content: store class + note so we can identify the exemplar later
        std::string content = "[probe_centroid:" + class_name + "] " + text;
        if (!note.empty()) content += "\nnote: " + note;

        uint64_t mem_id = 0;
        try {
            mem_id = field_store_->remember(
                "probe_centroid",       // kind
                "behavior",             // realm
                content,
                embedding,
                1.0f,                   // confidence
                0.0001f                 // very slow decay — centroids should persist
            );
        } catch (const std::exception& e) {
            return ToolResult::error(std::string("failed to store centroid: ") + e.what());
        }

        json out;
        out["memory_id"] = mem_id;
        out["class"]     = class_name;
        out["text_preview"] = text.substr(0, 80);
        return ToolResult::ok(
            "Stored probe centroid for class '" + class_name + "' (id=" + std::to_string(mem_id) + ")",
            out);
    }

    ToolResult tool_behavioral_probe(const json& params) {
        std::string text = params.value("text", "");
        if (text.empty()) {
            return ToolResult::error("text is required");
        }

        auto query_emb = embed_query(text);
        if (query_emb.empty()) {
            return ToolResult::error("embedding failed");
        }

        // Recall all probe_centroid memories from behavior realm
        auto hits = field_store_->recall(query_emb, 40, "behavior");

        // Aggregate per class: collect cosine similarities
        std::unordered_map<std::string, std::vector<float>> class_sims;
        for (const auto& h : hits) {
            if (h.kind != "probe_centroid") continue;
            // Extract class name from content "[probe_centroid:CLASS]..."
            std::string cls;
            const std::string prefix = "[probe_centroid:";
            auto pos = h.content.find(prefix);
            if (pos != std::string::npos) {
                auto end = h.content.find(']', pos + prefix.size());
                if (end != std::string::npos) {
                    cls = h.content.substr(pos + prefix.size(), end - pos - prefix.size());
                }
            }
            if (!cls.empty()) {
                class_sims[cls].push_back(h.semantic_score);
            }
        }

        if (class_sims.empty()) {
            return ToolResult::error(
                "No probe centroids found. Seed centroids first with probe_seed.");
        }

        // Compute mean similarity per class
        std::unordered_map<std::string, float> scores;
        for (const auto& [cls, sims] : class_sims) {
            float sum = 0.0f;
            for (float s : sims) sum += s;
            scores[cls] = sum / static_cast<float>(sims.size());
        }

        // Determine dominant class
        std::string dominant;
        float best = -1.0f;
        for (const auto& [cls, s] : scores) {
            if (s > best) { best = s; dominant = cls; }
        }

        json out;
        out["scores"] = json::object();
        for (const auto& [cls, s] : scores) {
            out["scores"][cls] = std::round(s * 1000.0f) / 1000.0f;
        }
        out["dominant"]    = dominant;
        out["dominant_score"] = std::round(best * 1000.0f) / 1000.0f;
        out["text_preview"] = text.substr(0, 80);

        // Quality estimate: direct score is the behavioral quality signal
        float quality = scores.count("direct") ? scores["direct"] : (1.0f - best);
        out["quality"] = std::round(quality * 1000.0f) / 1000.0f;

        // Format summary
        std::ostringstream ss;
        ss << "Probe result: dominant=" << dominant
           << " (" << static_cast<int>(best * 100) << "%)  quality="
           << static_cast<int>(quality * 100) << "%\n";
        for (const auto& [cls, s] : scores) {
            ss << "  " << cls << ": " << static_cast<int>(s * 100) << "%\n";
        }

        return ToolResult::ok(ss.str(), out);
    }

    ToolResult tool_probe_calibrate(const json& params) {
        std::string class_name = params.value("class", "");
        std::string text       = params.value("text", "");

        if (class_name.empty() || text.empty()) {
            return ToolResult::error("class and text are required");
        }

        // Delegate to probe_seed — calibration is just adding another exemplar
        json seed_params;
        seed_params["class"] = class_name;
        seed_params["text"]  = text;
        seed_params["note"]  = "calibration sample";
        auto result = tool_probe_seed(seed_params);

        // Override message to indicate calibration
        if (!result.is_error) {
            result.text = "Calibrated class '" + class_name + "' with new exemplar. "
                "Probe will incorporate this on next call.";
        }
        return result;
    }

    ToolResult tool_probe_status(const json& params) {
        auto hits = field_store_->recall(
            embed_query("probe centroid behavioral class"), 100, "behavior");

        std::unordered_map<std::string, int> class_counts;
        for (const auto& h : hits) {
            if (h.kind != "probe_centroid") continue;
            const std::string prefix = "[probe_centroid:";
            auto pos = h.content.find(prefix);
            if (pos != std::string::npos) {
                auto end = h.content.find(']', pos + prefix.size());
                if (end != std::string::npos) {
                    std::string cls = h.content.substr(pos + prefix.size(),
                                                        end - pos - prefix.size());
                    class_counts[cls]++;
                }
            }
        }

        if (class_counts.empty()) {
            return ToolResult::ok(
                "No probe centroids seeded yet. Use probe_seed to bootstrap.", {});
        }

        json out = json::object();
        std::ostringstream ss;
        ss << "Behavioral probe status:\n";
        for (const auto& [cls, cnt] : class_counts) {
            ss << "  " << cls << ": " << cnt << " exemplar(s)\n";
            out[cls] = cnt;
        }
        return ToolResult::ok(ss.str(), out);
    }
