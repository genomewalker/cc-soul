#include "../include/chitta/wiki_export.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <chrono>
#include <iomanip>

namespace chitta {

using json = nlohmann::json;

WikiExporter::WikiExporter(FieldStore& field, const WikiExportConfig& config)
    : field_store_(&field), config_(config) {
    if (config_.output_dir.empty()) {
        const char* home = std::getenv("HOME");
        config_.output_dir = std::string(home ? home : "/tmp") + "/.claude/wiki";
    }
}

void WikiExporter::log(const std::string& msg) {
    if (log_callback_) log_callback_(msg);
    else std::cerr << msg << "\n";
}

void WikiExporter::write_file(const std::string& path, const std::string& content) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream ofs(path);
    if (ofs) ofs << content;
}

std::string WikiExporter::sanitize_filename(const std::string& name) {
    std::string safe;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == '<' || c == '>' || c == ':' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            safe += '_';
        else
            safe += c;
    }
    return safe;
}

std::string WikiExporter::get_backlinks(const std::string& memory_id) {
    std::string raw = field_store_->list_triplets_for_entity(memory_id, 50);
    if (raw.empty()) return "";

    json triplets;
    try { triplets = json::parse(raw); } catch (...) { return ""; }

    std::string links;
    for (const auto& t : triplets) {
        std::string subj = t.value("subject", "");
        std::string pred = t.value("predicate", "");
        std::string obj  = t.value("object", "");
        std::string other = (subj == memory_id) ? obj : subj;
        std::string relation = pred;

        if (!other.empty()) {
            links += "- " + relation + " → [[" + other + "]]\n";
        }
    }
    return links;
}

std::string WikiExporter::memory_to_markdown(const json& mem, const std::string& backlinks) {
    std::string id   = mem.value("id", "");
    std::string text = mem.value("text", "");
    std::string kind = mem.value("kind", "unknown");
    std::string realm = mem.value("realm", "");
    float confidence = mem.value("confidence", 0.0f);
    float strength   = mem.value("strength", 0.0f);

    std::ostringstream md;
    md << "### " << text.substr(0, 80) << "\n\n";
    md << text << "\n\n";
    md << "**ID**: " << id
       << " | **Kind**: " << kind
       << " | **Confidence**: " << std::fixed << std::setprecision(2) << confidence
       << " | **Strength**: " << std::fixed << std::setprecision(2) << strength
       << "\n";

    if (!backlinks.empty()) {
        md << "\n**Links**:\n" << backlinks << "\n";
    }

    md << "---\n\n";
    return md.str();
}

WikiExportResult WikiExporter::export_all() {
    WikiExportResult result;

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

    log("[wiki] Exporting " + std::to_string(realms.size()) + " realm(s) to " + config_.output_dir);

    // Master index
    std::ostringstream master_index;
    master_index << "# Soul Wiki\n\n";
    master_index << "Generated: " << [](){
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M");
        return ss.str();
    }() << "\n\n";
    master_index << "## Realms\n\n";

    for (const auto& realm : realms) {
        std::string raw = field_store_->list_memories("", realm, "recency",
                                                       config_.max_memories, 0);
        json memories;
        try { memories = json::parse(raw); } catch (...) { continue; }

        if (memories.empty()) continue;

        // Group by kind
        std::map<std::string, std::vector<json>> by_kind;
        for (const auto& m : memories) {
            std::string kind = m.value("kind", "unknown");
            by_kind[kind].push_back(m);
        }

        std::string realm_safe = sanitize_filename(realm);
        std::string realm_dir = config_.output_dir + "/" + realm_safe;

        master_index << "- [[" << realm_safe << "/index|" << realm << "]] ("
                     << memories.size() << " memories)\n";

        // Realm index
        std::ostringstream realm_index;
        realm_index << "# " << realm << "\n\n";
        realm_index << memories.size() << " memories\n\n";

        for (const auto& [kind, mems] : by_kind) {
            realm_index << "- [[" << kind << "]] (" << mems.size() << ")\n";

            // Kind page
            std::ostringstream kind_page;
            kind_page << "# " << realm << " / " << kind << "\n\n";
            kind_page << mems.size() << " entries\n\n";

            for (const auto& mem : mems) {
                std::string id = mem.value("id", "");
                std::string backlinks = get_backlinks(id);
                if (!backlinks.empty()) result.backlinks_created++;

                kind_page << memory_to_markdown(mem, backlinks);
                result.memories_exported++;
            }

            write_file(realm_dir + "/" + sanitize_filename(kind) + ".md", kind_page.str());
            result.pages_written++;
        }

        write_file(realm_dir + "/index.md", realm_index.str());
        result.pages_written++;
    }

    write_file(config_.output_dir + "/index.md", master_index.str());
    result.pages_written++;

    result.success = true;
    log("[wiki] Done: " + std::to_string(result.pages_written) + " pages, " +
        std::to_string(result.memories_exported) + " memories, " +
        std::to_string(result.backlinks_created) + " backlinks");

    return result;
}

} // namespace chitta
