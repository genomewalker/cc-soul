#pragma once
// FileTracker: Track source files for staleness detection
//
// Maintains a file index to efficiently detect when code changes
// and mark derived nodes as stale.

#include "types.hpp"  // StaleState is defined there
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cstdio>

namespace chitta {

namespace fs = std::filesystem;

// File record for tracking source files
struct FileRecord {
    std::string path;               // Normalized relative path
    std::string git_oid;            // Git blob OID (empty if not in git)
    std::string content_hash;       // Content hash (fallback for non-git)
    uint64_t last_indexed_at;       // When symbols were extracted
    std::string extractor_version;  // e.g., "tree-sitter@v0.24.6"
    uint64_t file_size;             // For quick change detection
    uint64_t mtime;                 // Modification time

    bool operator==(const FileRecord& other) const {
        return path == other.path;
    }
};

// File tracker for staleness detection
class FileTracker {
public:
    FileTracker() = default;

    // Register a file after extraction
    void register_file(const std::string& path, const std::string& extractor_version) {
        FileRecord record;
        record.path = normalize_path(path);
        record.extractor_version = extractor_version;
        record.last_indexed_at = now();

        // Get file stats
        if (fs::exists(path)) {
            record.file_size = fs::file_size(path);
            record.mtime = fs::last_write_time(path).time_since_epoch().count();
        }

        // Try to get git OID
        record.git_oid = get_git_oid(path);

        // If no git, compute content hash
        if (record.git_oid.empty()) {
            record.content_hash = compute_file_hash(path);
        }

        files_[record.path] = record;
    }

    // Check if a file has changed since last indexing
    bool has_changed(const std::string& path) const {
        std::string norm_path = normalize_path(path);
        auto it = files_.find(norm_path);
        if (it == files_.end()) {
            return true;  // Never indexed
        }

        const auto& record = it->second;

        // Quick check: mtime
        if (fs::exists(path)) {
            uint64_t current_mtime = fs::last_write_time(path).time_since_epoch().count();
            if (current_mtime != record.mtime) {
                // mtime changed, verify with hash
                if (!record.git_oid.empty()) {
                    std::string current_oid = get_git_oid(path);
                    return current_oid != record.git_oid;
                } else {
                    std::string current_hash = compute_file_hash(path);
                    return current_hash != record.content_hash;
                }
            }
        } else {
            return true;  // File deleted
        }

        return false;
    }

    // Get all changed files in a directory
    std::vector<std::string> get_changed_files(const std::string& dir) const {
        std::vector<std::string> changed;

        // First check via git if available
        std::vector<std::string> git_changed = get_git_changed_files(dir);
        if (!git_changed.empty()) {
            return git_changed;
        }

        // Fallback to mtime/hash checking
        for (const auto& [path, record] : files_) {
            if (path.find(dir) == 0 || dir.empty()) {
                if (has_changed(path)) {
                    changed.push_back(path);
                }
            }
        }

        return changed;
    }

    // Get files that need re-indexing (extractor version changed)
    std::vector<std::string> get_outdated_files(const std::string& current_version) const {
        std::vector<std::string> outdated;
        for (const auto& [path, record] : files_) {
            if (record.extractor_version != current_version) {
                outdated.push_back(path);
            }
        }
        return outdated;
    }

    // Get file record
    const FileRecord* get_record(const std::string& path) const {
        std::string norm_path = normalize_path(path);
        auto it = files_.find(norm_path);
        return (it != files_.end()) ? &it->second : nullptr;
    }

    // Remove file record
    void remove_file(const std::string& path) {
        files_.erase(normalize_path(path));
    }

    // Get all tracked files
    const std::unordered_map<std::string, FileRecord>& files() const {
        return files_;
    }

    size_t size() const { return files_.size(); }

    // Persistence
    void save(const std::string& path) const {
        std::ofstream out(path, std::ios::binary);
        if (!out) return;

        uint32_t count = files_.size();
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));

        for (const auto& [key, record] : files_) {
            write_string(out, record.path);
            write_string(out, record.git_oid);
            write_string(out, record.content_hash);
            out.write(reinterpret_cast<const char*>(&record.last_indexed_at), sizeof(record.last_indexed_at));
            write_string(out, record.extractor_version);
            out.write(reinterpret_cast<const char*>(&record.file_size), sizeof(record.file_size));
            out.write(reinterpret_cast<const char*>(&record.mtime), sizeof(record.mtime));
        }
    }

    void load(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return;

        uint32_t count;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));

        files_.clear();
        for (uint32_t i = 0; i < count; i++) {
            FileRecord record;
            record.path = read_string(in);
            record.git_oid = read_string(in);
            record.content_hash = read_string(in);
            in.read(reinterpret_cast<char*>(&record.last_indexed_at), sizeof(record.last_indexed_at));
            record.extractor_version = read_string(in);
            in.read(reinterpret_cast<char*>(&record.file_size), sizeof(record.file_size));
            in.read(reinterpret_cast<char*>(&record.mtime), sizeof(record.mtime));

            files_[record.path] = record;
        }
    }

private:
    std::unordered_map<std::string, FileRecord> files_;

    static std::string normalize_path(const std::string& path) {
        try {
            return fs::weakly_canonical(path).string();
        } catch (...) {
            return path;
        }
    }

    static std::string get_git_oid(const std::string& path) {
        std::string cmd = "git hash-object \"" + path + "\" 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "";

        char buffer[64];
        std::string result;
        if (fgets(buffer, sizeof(buffer), pipe)) {
            result = buffer;
            if (!result.empty() && result.back() == '\n') {
                result.pop_back();
            }
        }
        pclose(pipe);
        return result;
    }

    static std::vector<std::string> get_git_changed_files(const std::string& dir) {
        std::vector<std::string> changed;

        std::string cmd = "cd \"" + dir + "\" && git diff --name-only 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return changed;

        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            std::string file(buffer);
            if (!file.empty() && file.back() == '\n') file.pop_back();
            if (!file.empty()) {
                changed.push_back(dir + "/" + file);
            }
        }
        pclose(pipe);

        // Also get untracked files
        cmd = "cd \"" + dir + "\" && git ls-files --others --exclude-standard 2>/dev/null";
        pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            while (fgets(buffer, sizeof(buffer), pipe)) {
                std::string file(buffer);
                if (!file.empty() && file.back() == '\n') file.pop_back();
                if (!file.empty()) {
                    changed.push_back(dir + "/" + file);
                }
            }
            pclose(pipe);
        }

        return changed;
    }

    static std::string compute_file_hash(const std::string& path) {
        // Simple hash: file size + first/last 1KB
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return "";

        size_t size = file.tellg();
        file.seekg(0);

        std::string data;
        data.reserve(2048 + 8);

        // Add size
        data.append(reinterpret_cast<const char*>(&size), sizeof(size));

        // Add first 1KB
        char buffer[1024];
        file.read(buffer, std::min(size, size_t(1024)));
        data.append(buffer, file.gcount());

        // Add last 1KB if file > 2KB
        if (size > 2048) {
            file.seekg(-1024, std::ios::end);
            file.read(buffer, 1024);
            data.append(buffer, file.gcount());
        }

        // Simple hash (djb2)
        uint64_t hash = 5381;
        for (char c : data) {
            hash = ((hash << 5) + hash) + static_cast<unsigned char>(c);
        }

        char hex[17];
        snprintf(hex, sizeof(hex), "%016lx", hash);
        return hex;
    }

    static void write_string(std::ostream& out, const std::string& s) {
        uint32_t len = s.size();
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(s.data(), len);
    }

    static std::string read_string(std::istream& in) {
        uint32_t len;
        in.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string s(len, '\0');
        in.read(&s[0], len);
        return s;
    }
};

} // namespace chitta
