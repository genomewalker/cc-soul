// chitta_migrate — unified migration tool for chitta memory stores
//
// Supports two source formats (auto-detected or explicit):
//   1. soul.db      (old SQLite store, pre-DuckDB era)
//   2. chitta.duckdb / chitta.db  (DuckDB store, pre-chitta-field era)
//
// Usage:
//   chitta_migrate [--source PATH] [--field-dir PATH] [--force] [--dry-run] [--verbose]
//
// The source format is auto-detected from the file extension and header.
// If --source is omitted, common paths are searched:
//   ~/.claude/mind/chitta.duckdb
//   ~/.claude/mind/chitta.db
//   ~/.claude/mind/soul.db

#include <chitta/version.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#ifdef __linux__
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

static const char* USAGE = R"(chitta_migrate — migrate legacy memory stores to chitta-field

Usage:
  chitta_migrate [options]

Options:
  --source PATH     Path to source DB (auto-detected if omitted)
  --field-dir PATH  Destination chitta-field directory
                    (default: ~/.claude/mind/chitta-field)
  --force           Overwrite existing chitta-field data
  --dry-run         Show what would be migrated without writing
  --verbose, -v     Verbose output
  --help, -h        Show this help

Auto-detected source formats:
  *.duckdb, *.db    DuckDB store (uses chitta_import internally)
  soul.db           Legacy SQLite store (uses built-in SQLite migration)

Examples:
  chitta_migrate                              # auto-detect source
  chitta_migrate --source ~/.claude/mind/chitta.duckdb
  chitta_migrate --source ~/.claude/mind/soul.db
  chitta_migrate --dry-run
)";

static std::vector<std::string> CANDIDATE_PATHS = {
    ".claude/mind/chitta.duckdb",
    ".claude/mind/chitta.db",
    ".claude/mind/soul.db",
};

enum class SourceFormat { DuckDB, SoulDB, Unknown };

static SourceFormat detect_format(const fs::path& path) {
    const std::string name = path.filename().string();
    const std::string ext  = path.extension().string();
    if (ext == ".duckdb") return SourceFormat::DuckDB;
    if (name == "soul.db") return SourceFormat::SoulDB;
    // Peek at first 4 bytes for SQLite magic
    if (std::ifstream f{path, std::ios::binary}) {
        char buf[16] = {};
        f.read(buf, 16);
        if (std::string(buf, 6) == "SQLite") return SourceFormat::SoulDB;
    }
    if (ext == ".db") return SourceFormat::DuckDB;  // assume DuckDB .db
    return SourceFormat::Unknown;
}

static fs::path resolve_binary(const std::string& name) {
    // Look next to this binary first (Linux), then rely on PATH
#ifdef __linux__
    try {
        fs::path self = fs::read_symlink("/proc/self/exe");
        fs::path sibling = self.parent_path() / name;
        if (fs::exists(sibling)) return sibling;
    } catch (...) {}
#endif
    return fs::path(name);
}

int main(int argc, char* argv[]) {
    std::string source_path;
    std::string field_dir;
    bool force   = false;
    bool dry_run = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { std::cout << USAGE; return 0; }
        if (arg == "--force")              { force   = true; continue; }
        if (arg == "--dry-run")            { dry_run = true; continue; }
        if (arg == "--verbose" || arg == "-v") { verbose = true; continue; }
        if ((arg == "--source" || arg == "--soul-db" || arg == "--db-path") && i + 1 < argc)
            { source_path = argv[++i]; continue; }
        if (arg == "--field-dir" && i + 1 < argc)
            { field_dir = argv[++i]; continue; }
        std::cerr << "Unknown argument: " << arg << "\n" << USAGE;
        return 1;
    }

    // Default field-dir
    if (field_dir.empty()) {
        const char* home = std::getenv("HOME");
        field_dir = std::string(home ? home : ".") + "/.claude/mind/chitta-field";
    }

    // Auto-detect source
    if (source_path.empty()) {
        const char* home = std::getenv("HOME");
        std::string home_str(home ? home : ".");
        for (const auto& rel : CANDIDATE_PATHS) {
            fs::path candidate = home_str + "/" + rel;
            if (fs::exists(candidate)) {
                source_path = candidate.string();
                if (verbose)
                    std::cerr << "[migrate] Auto-detected source: " << source_path << "\n";
                break;
            }
        }
    }

    if (source_path.empty()) {
        std::cerr << "ERROR: No source database found.\n"
                  << "Searched: ~/.claude/mind/chitta.duckdb, chitta.db, soul.db\n"
                  << "Specify with --source PATH\n";
        return 1;
    }

    if (!fs::exists(source_path)) {
        std::cerr << "ERROR: Source not found: " << source_path << "\n";
        return 1;
    }

    SourceFormat fmt = detect_format(fs::path(source_path));
    if (fmt == SourceFormat::Unknown) {
        std::cerr << "ERROR: Cannot determine format for: " << source_path << "\n"
                  << "Supported: *.duckdb, *.db (DuckDB), soul.db (SQLite)\n";
        return 1;
    }

    std::cout << "=== chitta_migrate ===\n"
              << "  source:    " << source_path << "\n"
              << "  format:    " << (fmt == SourceFormat::DuckDB ? "DuckDB" : "SQLite/soul.db") << "\n"
              << "  field-dir: " << field_dir << "\n";
    if (dry_run)  std::cout << "  mode:      DRY-RUN\n";
    if (force)    std::cout << "  mode:      FORCE\n";
    std::cout << "\n";

    if (fmt == SourceFormat::DuckDB) {
        // Delegate to chitta_import
        fs::path import_bin = resolve_binary("chitta_import");
        std::string cmd = import_bin.string()
            + " --db-path "   + source_path
            + " --field-dir " + field_dir;
        if (force)   cmd += " --force";
        if (dry_run) cmd += " --dry-run";
        if (verbose) std::cerr << "[migrate] Running: " << cmd << "\n";
        int rc = std::system(cmd.c_str());
        return WEXITSTATUS(rc);
    }

    // SQLite / soul.db path — built-in migration via FieldStore
    std::cerr << "SQLite/soul.db migration via chitta-field import is not yet implemented.\n"
              << "Use the legacy chitta_migrate binary for soul.db sources.\n";
    return 1;
}
