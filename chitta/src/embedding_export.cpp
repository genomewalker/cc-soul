#include "../include/chitta/embedding_export.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <random>
#include <set>

namespace chitta {

using json = nlohmann::json;

static std::string json_str(const json& j, const std::string& key) {
    if (!j.contains(key)) return "";
    const auto& v = j[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
    if (v.is_number_unsigned()) return std::to_string(v.get<uint64_t>());
    if (v.is_number_float()) return std::to_string(v.get<double>());
    return v.dump();
}

EmbeddingExporter::EmbeddingExporter(FieldStore& field, EmbedFn embedder,
                                     const EmbeddingExportConfig& config)
    : field_store_(&field), embedder_(std::move(embedder)), config_(config) {
    if (config_.output_path.empty()) {
        const char* home = std::getenv("HOME");
        config_.output_path = std::string(home ? home : "/tmp") +
                              "/.claude/training/pairs.jsonl";
    }
}

void EmbeddingExporter::log(const std::string& msg) {
    if (log_callback_) log_callback_(msg);
    else std::cerr << msg << "\n";
}

void EmbeddingExporter::write_jsonl(const std::string& path,
                                     const std::vector<std::string>& lines) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream ofs(path);
    for (const auto& line : lines) ofs << line << "\n";
}

std::string EmbeddingExporter::make_query(const std::string& content) {
    // Extract first meaningful line as a pseudo-query
    size_t end = content.find('\n');
    std::string first_line = (end != std::string::npos) ? content.substr(0, end) : content;

    // Strip SSL markers
    if (first_line.size() > 2 && first_line[0] == '[') {
        size_t close = first_line.find(']');
        if (close != std::string::npos && close < 30)
            first_line = first_line.substr(close + 1);
    }

    // Trim
    size_t start = first_line.find_first_not_of(" \t");
    if (start != std::string::npos) first_line = first_line.substr(start);
    if (first_line.size() > 200) first_line = first_line.substr(0, 200);

    return first_line;
}

EmbeddingExportResult EmbeddingExporter::export_pairs() {
    EmbeddingExportResult result;

    // Get all realms
    std::vector<std::string> realms;
    if (!config_.realm.empty()) {
        realms.push_back(config_.realm);
    } else {
        std::string raw = field_store_->realm_list();
        try {
            auto arr = json::parse(raw);
            for (const auto& r : arr) {
                if (r.is_string()) realms.push_back(r.get<std::string>());
            }
        } catch (...) {
            realms.push_back("brahman");
        }
    }

    log("[export] Exporting training pairs from " +
        std::to_string(realms.size()) + " realm(s)");

    std::vector<std::string> jsonl_lines;
    std::vector<json> all_memories;

    for (const auto& realm : realms) {
        std::string raw = field_store_->list_memories("", realm, "strength",
                                                       config_.max_pairs, 0);
        json memories;
        try { memories = json::parse(raw); } catch (...) { continue; }

        for (auto& m : memories) {
            float conf = m.value("confidence", 0.0f);
            float str  = m.value("strength", 0.0f);
            if (conf >= config_.min_confidence && str >= config_.min_strength) {
                all_memories.push_back(std::move(m));
            }
        }
    }

    log("[export] " + std::to_string(all_memories.size()) +
        " memories pass quality filter");

    // Generate positive pairs: query → passage
    for (const auto& mem : all_memories) {
        std::string content = mem.value("content", "");
        if (content.size() < 20) continue;

        std::string query = make_query(content);
        if (query.empty()) continue;

        json pair;
        pair["query"] = query;
        pair["pos"] = content;

        // Triplet-derived context as additional positive signal
        std::string id = json_str(mem, "id");
        if (!id.empty()) {
            std::string triplets_raw = field_store_->list_triplets_for_entity(id, 5);
            if (!triplets_raw.empty()) {
                try {
                    auto triplets = json::parse(triplets_raw);
                    std::string context;
                    for (const auto& t : triplets) {
                        std::string s = t.value("subject", "");
                        std::string p = t.value("predicate", "");
                        std::string o = t.value("object", "");
                        if (!context.empty()) context += "; ";
                        context += s + " " + p + " " + o;
                    }
                    if (!context.empty()) pair["context"] = context;
                } catch (...) {}
            }
        }

        jsonl_lines.push_back(pair.dump());
        result.pairs_exported++;

        if (static_cast<size_t>(result.pairs_exported) >= config_.max_pairs) break;
    }

    // Generate hard negatives: for each pair, find a semantically distant memory
    if (config_.include_negatives && embedder_ && result.pairs_exported > 0) {
        std::mt19937 rng(42);
        size_t n = all_memories.size();

        for (size_t i = 0; i < jsonl_lines.size() && i < n; ++i) {
            // Pick a random memory from a different index range as hard negative
            size_t neg_idx = (i + n / 2 + rng() % std::max<size_t>(1, n / 4)) % n;
            if (neg_idx == i) neg_idx = (i + 1) % n;

            std::string neg_content = all_memories[neg_idx].value("content", "");
            if (neg_content.empty()) continue;

            // Augment the existing line with neg field
            try {
                auto pair = json::parse(jsonl_lines[i]);
                pair["neg"] = neg_content;
                jsonl_lines[i] = pair.dump();
                result.negatives_generated++;
            } catch (...) {}
        }
    }

    write_jsonl(config_.output_path, jsonl_lines);
    result.output_path = config_.output_path;
    result.success = true;

    log("[export] Done: " + std::to_string(result.pairs_exported) + " pairs, " +
        std::to_string(result.negatives_generated) + " negatives → " +
        config_.output_path);

    return result;
}

} // namespace chitta
