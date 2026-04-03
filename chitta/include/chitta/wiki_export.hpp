#pragma once
// WikiExporter: compile memories into Obsidian-compatible .md files
// grouped by realm and kind, with triplet-derived [[backlinks]].

#include "field_store.hpp"
#include <string>
#include <vector>
#include <functional>

namespace chitta {

struct WikiExportConfig {
    std::string output_dir;  // default: ~/.claude/wiki/
    std::string realm = "";  // empty = all realms
    size_t max_memories = 5000;
};

struct WikiExportResult {
    int pages_written = 0;
    int memories_exported = 0;
    int backlinks_created = 0;
    bool success = false;
    std::string error;
};

class WikiExporter {
public:
    WikiExporter(FieldStore& field, const WikiExportConfig& config);

    WikiExportResult export_all();

    using LogCallback = std::function<void(const std::string&)>;
    void set_log_callback(LogCallback cb) { log_callback_ = cb; }

private:
    FieldStore* field_store_;
    WikiExportConfig config_;
    LogCallback log_callback_;

    void write_file(const std::string& path, const std::string& content);
    std::string sanitize_filename(const std::string& name);
    std::string memory_to_markdown(const nlohmann::json& mem,
                                    const std::string& backlinks);
    std::string get_backlinks(const std::string& memory_id);
    void log(const std::string& msg);
};

} // namespace chitta
