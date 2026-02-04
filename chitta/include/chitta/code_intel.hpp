#pragma once
// CodeIntel: Tree-sitter based symbol extraction
//
// Extracts functions, classes, methods from source files.
// Supports: C/C++, Python, JavaScript/TypeScript, Go, Rust, Java, Ruby, C#

#include "duckdb_store.hpp"
#include <tree_sitter/api.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>

// External tree-sitter language functions (from tree-sitter-parsers library)
extern "C" {
    TSLanguage* tree_sitter_cpp();
    TSLanguage* tree_sitter_python();
    TSLanguage* tree_sitter_javascript();
    TSLanguage* tree_sitter_typescript();
    TSLanguage* tree_sitter_go();
    TSLanguage* tree_sitter_rust();
    TSLanguage* tree_sitter_java();
    TSLanguage* tree_sitter_ruby();
    TSLanguage* tree_sitter_c_sharp();
}

namespace chitta {

// Extracted symbol with location info
struct ExtractedSymbol {
    std::string kind;       // function, class, method, variable
    std::string name;
    std::string signature;  // For functions: return type + params
    std::string file_path;
    int32_t line_start = 0;
    int32_t line_end = 0;
    std::string parent;     // Parent class/module name (if method)
};

// ═══════════════════════════════════════════════════════════════════════════
// Callsite Extraction (Phase 1: Code Intelligence)
// ═══════════════════════════════════════════════════════════════════════════

enum class CallKind {
    Call,        // foo(...)
    MemberCall,  // obj.method(...), ptr->method(...)
    Qualified,   // ns::foo(...), Class::staticMethod(...)
    New,         // new Foo(...)
    Ctor,        // Foo x(...), Foo{...}, Foo(...) temporaries
    Indirect,    // (*fp)(...), fp(...), unknown callee
    LambdaCall   // []{}(...)
};

inline std::string call_kind_to_string(CallKind kind) {
    switch (kind) {
        case CallKind::Call: return "call";
        case CallKind::MemberCall: return "member_call";
        case CallKind::Qualified: return "qualified";
        case CallKind::New: return "new";
        case CallKind::Ctor: return "ctor";
        case CallKind::Indirect: return "indirect";
        case CallKind::LambdaCall: return "lambda_call";
    }
    return "unknown";
}

// Extracted callsite with full context
struct Callsite {
    // Identity (cpp:callsite:file:start:end)
    std::string file_path;
    uint32_t start_byte = 0;
    uint32_t end_byte = 0;

    // Location
    uint32_t line = 0;       // 1-based
    uint32_t column = 0;     // 1-based

    // Caller context
    std::string caller_symbol;  // Symbol ID of containing function/method

    // Callee surface form (always present)
    CallKind kind = CallKind::Call;
    std::string callee_text;     // Exact source slice for callee expr
    std::string callee_leaf;     // Just the name: "foo", "method"
    std::string scope_text;      // For qualified: "ns::Class"

    // Member-call detail
    std::string receiver_text;   // "obj", "ptr", "this"
    std::string member_op;       // ".", "->", "::"

    // Args
    uint32_t arg_count = 0;

    // Template info
    std::string template_args;   // "<int, T>" if present

    // Resolution (populated by resolver, not extraction)
    std::string resolved_symbol; // Target symbol ID when resolved
    float resolve_confidence = 0.0f;

    // Generate callsite ID
    std::string id() const {
        return "cpp:callsite:" + file_path + ":" +
               std::to_string(start_byte) + ":" + std::to_string(end_byte);
    }

    // Generate location string
    std::string location() const {
        return file_path + ":" + std::to_string(line) + ":" + std::to_string(column);
    }
};

// Type hierarchy relationship (extends/implements)
struct TypeRelationship {
    std::string derived_name;   // The class/type that inherits
    std::string base_name;      // The class/interface being inherited from
    std::string relationship;   // "extends", "implements", "embeds"
    std::string file_path;
    uint32_t line = 0;
};

// Import statement tracking
struct ImportStatement {
    std::string file_path;      // File containing the import
    std::string import_path;    // The module/file being imported
    std::string alias;          // Optional alias (e.g., "import foo as bar")
    std::vector<std::string> names;  // Specific names imported (e.g., "from foo import a, b")
    uint32_t line = 0;
};

// Combined extraction result
struct ExtractionResult {
    std::vector<ExtractedSymbol> symbols;
    std::vector<Callsite> callsites;
    std::vector<TypeRelationship> type_relationships;
    std::vector<ImportStatement> imports;
};

// Language detection and parser management
class CodeIntel {
public:
    CodeIntel() {
        // Initialize parsers for each language
        parsers_["cpp"] = ts_parser_new();
        ts_parser_set_language(parsers_["cpp"], tree_sitter_cpp());

        parsers_["python"] = ts_parser_new();
        ts_parser_set_language(parsers_["python"], tree_sitter_python());

        parsers_["javascript"] = ts_parser_new();
        ts_parser_set_language(parsers_["javascript"], tree_sitter_javascript());

        parsers_["typescript"] = ts_parser_new();
        ts_parser_set_language(parsers_["typescript"], tree_sitter_typescript());

        parsers_["go"] = ts_parser_new();
        ts_parser_set_language(parsers_["go"], tree_sitter_go());

        parsers_["rust"] = ts_parser_new();
        ts_parser_set_language(parsers_["rust"], tree_sitter_rust());

        parsers_["java"] = ts_parser_new();
        ts_parser_set_language(parsers_["java"], tree_sitter_java());

        parsers_["ruby"] = ts_parser_new();
        ts_parser_set_language(parsers_["ruby"], tree_sitter_ruby());

        parsers_["csharp"] = ts_parser_new();
        ts_parser_set_language(parsers_["csharp"], tree_sitter_c_sharp());
    }

    ~CodeIntel() {
        for (auto& [_, parser] : parsers_) {
            ts_parser_delete(parser);
        }
    }

    // Detect language from file extension
    std::string detect_language(const std::string& path) {
        std::filesystem::path p(path);
        std::string ext = p.extension().string();

        if (ext == ".c" || ext == ".h" || ext == ".cpp" || ext == ".hpp" ||
            ext == ".cc" || ext == ".cxx" || ext == ".hxx") {
            return "cpp";
        }
        if (ext == ".py" || ext == ".pyw") return "python";
        if (ext == ".js" || ext == ".jsx" || ext == ".mjs") return "javascript";
        if (ext == ".ts" || ext == ".tsx") return "typescript";
        if (ext == ".go") return "go";
        if (ext == ".rs") return "rust";
        if (ext == ".java") return "java";
        if (ext == ".rb") return "ruby";
        if (ext == ".cs") return "csharp";

        return "";
    }

    // Extract symbols from a single file
    std::vector<ExtractedSymbol> extract_file(const std::string& path) {
        std::vector<ExtractedSymbol> symbols;

        std::string lang = detect_language(path);
        if (lang.empty()) return symbols;

        auto it = parsers_.find(lang);
        if (it == parsers_.end()) return symbols;

        // Read file content
        std::ifstream file(path);
        if (!file) return symbols;

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();

        // Parse
        TSTree* tree = ts_parser_parse_string(
            it->second, nullptr,
            source.c_str(), source.length()
        );
        if (!tree) return symbols;

        TSNode root = ts_tree_root_node(tree);

        // Extract symbols based on language
        if (lang == "cpp") {
            extract_cpp(root, source, path, symbols);
        } else if (lang == "python") {
            extract_python(root, source, path, symbols);
        } else if (lang == "javascript" || lang == "typescript") {
            extract_js(root, source, path, symbols);
        } else if (lang == "go") {
            extract_go(root, source, path, symbols);
        } else if (lang == "rust") {
            extract_rust(root, source, path, symbols);
        } else if (lang == "java") {
            extract_java(root, source, path, symbols);
        } else if (lang == "ruby") {
            extract_ruby(root, source, path, symbols);
        } else if (lang == "csharp") {
            extract_csharp(root, source, path, symbols);
        }

        ts_tree_delete(tree);
        return symbols;
    }

    // Extract symbols AND callsites from a single file (Phase 1 Code Intelligence)
    ExtractionResult extract_file_full(const std::string& path) {
        ExtractionResult result;

        std::string lang = detect_language(path);
        if (lang.empty()) return result;

        auto it = parsers_.find(lang);
        if (it == parsers_.end()) return result;

        // Read file content
        std::ifstream file(path);
        if (!file) return result;

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();

        // Parse
        TSTree* tree = ts_parser_parse_string(
            it->second, nullptr,
            source.c_str(), source.length()
        );
        if (!tree) return result;

        TSNode root = ts_tree_root_node(tree);

        // Extract symbols, callsites, type relationships, and imports based on language
        if (lang == "cpp") {
            extract_cpp_full(root, source, path, result.symbols, result.callsites,
                           result.type_relationships, result.imports);
        } else if (lang == "python") {
            extract_python_full(root, source, path, result.symbols, result.callsites,
                              result.type_relationships, result.imports);
        } else if (lang == "javascript" || lang == "typescript") {
            extract_js_full(root, source, path, result.symbols, result.callsites,
                          result.type_relationships, result.imports);
        } else if (lang == "go") {
            extract_go_full(root, source, path, result.symbols, result.callsites,
                          result.type_relationships, result.imports);
        } else if (lang == "rust") {
            extract_rust_full(root, source, path, result.symbols, result.callsites,
                            result.type_relationships, result.imports);
        } else {
            // Languages without full extraction yet - symbols only
            if (lang == "java") {
                extract_java(root, source, path, result.symbols);
            } else if (lang == "ruby") {
                extract_ruby(root, source, path, result.symbols);
            } else if (lang == "csharp") {
                extract_csharp(root, source, path, result.symbols);
            }
        }

        ts_tree_delete(tree);
        return result;
    }

    // Extract from directory with full callsite extraction
    ExtractionResult extract_directory_full(
        const std::string& path,
        const std::vector<std::string>& exclude = {"node_modules", ".git", "build", "__pycache__", "venv"},
        size_t max_files = 1000
    ) {
        ExtractionResult result;
        size_t file_count = 0;

        std::function<void(const std::filesystem::path&)> traverse;
        traverse = [&](const std::filesystem::path& dir) {
            if (file_count >= max_files) return;

            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (file_count >= max_files) return;

                std::string name = entry.path().filename().string();

                if (entry.is_directory()) {
                    bool skip = false;
                    for (const auto& ex : exclude) {
                        if (name == ex) { skip = true; break; }
                    }
                    if (!skip) traverse(entry.path());
                } else if (entry.is_regular_file()) {
                    std::string lang = detect_language(entry.path().string());
                    if (!lang.empty()) {
                        auto file_result = extract_file_full(entry.path().string());
                        result.symbols.insert(result.symbols.end(),
                                             file_result.symbols.begin(),
                                             file_result.symbols.end());
                        result.callsites.insert(result.callsites.end(),
                                               file_result.callsites.begin(),
                                               file_result.callsites.end());
                        result.type_relationships.insert(result.type_relationships.end(),
                                                        file_result.type_relationships.begin(),
                                                        file_result.type_relationships.end());
                        result.imports.insert(result.imports.end(),
                                             file_result.imports.begin(),
                                             file_result.imports.end());
                        file_count++;
                    }
                }
            }
        };

        if (std::filesystem::is_directory(path)) {
            traverse(path);
        } else if (std::filesystem::is_regular_file(path)) {
            result = extract_file_full(path);
        }

        return result;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Incremental Extraction (Only process changed files)
    // ═══════════════════════════════════════════════════════════════════════════

    struct IncrementalResult {
        ExtractionResult extracted;     // Newly extracted symbols/callsites
        size_t files_processed = 0;     // Files that were re-indexed
        size_t files_skipped = 0;       // Files that were up-to-date
        size_t symbols_deleted = 0;     // Old symbols removed
        size_t triplets_deleted = 0;    // Old triplets removed
    };

    // Check if file needs re-indexing based on mtime
    bool is_file_stale(DuckDBStore& store, const std::string& path) {
        auto stored = store.get_file_metadata(path);
        if (!stored) return true;  // Never indexed

        auto current_mtime = std::filesystem::last_write_time(path);
        auto mtime_sec = std::chrono::duration_cast<std::chrono::seconds>(
            current_mtime.time_since_epoch()).count();

        return mtime_sec > stored->mtime;
    }

    // Extract from directory incrementally (only changed files)
    IncrementalResult extract_directory_incremental(
        DuckDBStore& store,
        const std::string& path,
        const std::string& project,
        const std::vector<std::string>& exclude = {"node_modules", ".git", "build", "__pycache__", "venv"},
        size_t max_files = 1000
    ) {
        IncrementalResult result;
        size_t file_count = 0;

        std::function<void(const std::filesystem::path&)> traverse;
        traverse = [&](const std::filesystem::path& dir) {
            if (file_count >= max_files) return;

            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (file_count >= max_files) return;

                std::string name = entry.path().filename().string();

                if (entry.is_directory()) {
                    bool skip = false;
                    for (const auto& ex : exclude) {
                        if (name == ex) { skip = true; break; }
                    }
                    if (!skip) traverse(entry.path());
                } else if (entry.is_regular_file()) {
                    std::string lang = detect_language(entry.path().string());
                    if (!lang.empty()) {
                        std::string file_path = entry.path().string();

                        // Check if file is stale
                        if (!is_file_stale(store, file_path)) {
                            result.files_skipped++;
                            file_count++;
                            continue;
                        }

                        // Delete old data for this file
                        result.symbols_deleted += store.delete_file_symbols(file_path);
                        result.triplets_deleted += store.delete_file_triplets(file_path);

                        // Extract new data
                        auto file_result = extract_file_full(file_path);
                        result.extracted.symbols.insert(result.extracted.symbols.end(),
                                                       file_result.symbols.begin(),
                                                       file_result.symbols.end());
                        result.extracted.callsites.insert(result.extracted.callsites.end(),
                                                         file_result.callsites.begin(),
                                                         file_result.callsites.end());
                        result.extracted.type_relationships.insert(result.extracted.type_relationships.end(),
                                                                  file_result.type_relationships.begin(),
                                                                  file_result.type_relationships.end());
                        result.extracted.imports.insert(result.extracted.imports.end(),
                                                       file_result.imports.begin(),
                                                       file_result.imports.end());

                        // Update file metadata
                        auto current_mtime = std::filesystem::last_write_time(file_path);
                        auto mtime_sec = std::chrono::duration_cast<std::chrono::seconds>(
                            current_mtime.time_since_epoch()).count();

                        CodeFile file_meta;
                        file_meta.path = file_path;
                        file_meta.project = project;
                        file_meta.mtime = mtime_sec;
                        file_meta.indexed_at = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        file_meta.symbols_count = file_result.symbols.size();
                        file_meta.callsites_count = file_result.callsites.size();
                        store.set_file_metadata(file_meta);

                        result.files_processed++;
                        file_count++;
                    }
                }
            }
        };

        if (std::filesystem::is_directory(path)) {
            traverse(path);
        } else if (std::filesystem::is_regular_file(path)) {
            std::string file_path = path;
            if (is_file_stale(store, file_path)) {
                result.symbols_deleted += store.delete_file_symbols(file_path);
                result.triplets_deleted += store.delete_file_triplets(file_path);
                result.extracted = extract_file_full(file_path);

                auto current_mtime = std::filesystem::last_write_time(file_path);
                auto mtime_sec = std::chrono::duration_cast<std::chrono::seconds>(
                    current_mtime.time_since_epoch()).count();

                CodeFile file_meta;
                file_meta.path = file_path;
                file_meta.project = project;
                file_meta.mtime = mtime_sec;
                file_meta.indexed_at = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                file_meta.symbols_count = result.extracted.symbols.size();
                file_meta.callsites_count = result.extracted.callsites.size();
                store.set_file_metadata(file_meta);

                result.files_processed = 1;
            } else {
                result.files_skipped = 1;
            }
        }

        return result;
    }

    // Extract symbols from directory (recursive)
    std::vector<ExtractedSymbol> extract_directory(
        const std::string& path,
        const std::vector<std::string>& exclude = {"node_modules", ".git", "build", "__pycache__", "venv"},
        size_t max_files = 1000
    ) {
        std::vector<ExtractedSymbol> all_symbols;
        size_t file_count = 0;

        std::function<void(const std::filesystem::path&)> traverse;
        traverse = [&](const std::filesystem::path& dir) {
            if (file_count >= max_files) return;

            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (file_count >= max_files) return;

                std::string name = entry.path().filename().string();

                // Skip excluded directories
                if (entry.is_directory()) {
                    bool skip = false;
                    for (const auto& ex : exclude) {
                        if (name == ex) { skip = true; break; }
                    }
                    if (!skip) traverse(entry.path());
                } else if (entry.is_regular_file()) {
                    std::string lang = detect_language(entry.path().string());
                    if (!lang.empty()) {
                        auto file_symbols = extract_file(entry.path().string());
                        all_symbols.insert(all_symbols.end(),
                                          file_symbols.begin(), file_symbols.end());
                        file_count++;
                    }
                }
            }
        };

        if (std::filesystem::is_directory(path)) {
            traverse(path);
        } else if (std::filesystem::is_regular_file(path)) {
            all_symbols = extract_file(path);
        }

        return all_symbols;
    }

    // Store symbols in DuckDB
    size_t store_symbols(DuckDBStore& store, const std::vector<ExtractedSymbol>& symbols,
                         int64_t repo_id = 0) {
        size_t stored = 0;
        for (const auto& sym : symbols) {
            Symbol s;
            s.kind = sym.kind;
            s.name = sym.name;
            s.signature = sym.signature;
            s.file_path = sym.file_path;
            s.line_start = sym.line_start;
            s.line_end = sym.line_end;
            s.repo_id = repo_id;

            if (store.add_symbol(s) > 0) {
                stored++;

                // Create file→contains→symbol triplet
                std::filesystem::path p(sym.file_path);
                store.connect(p.filename().string(), "contains", sym.name);

                // Create parent→contains→symbol triplet for methods
                if (!sym.parent.empty()) {
                    store.connect(sym.parent, "contains", sym.name);
                }
            }
        }
        return stored;
    }

    // Store callsites as triplets in DuckDB (batch insert for speed)
    size_t store_callsites(DuckDBStore& store, const std::vector<Callsite>& callsites) {
        if (callsites.empty()) return 0;

        // Collect all triplets for batch insert
        // Tuple: (subject, predicate, object, source_file)
        std::vector<std::tuple<std::string, std::string, std::string, std::string>> triplets;
        triplets.reserve(callsites.size() * 8);  // ~8 triplets per callsite

        for (const auto& cs : callsites) {
            std::string callsite_id = cs.id();
            const std::string& src = cs.file_path;
            std::filesystem::path p(cs.file_path);

            // caller_symbol contains callsite
            if (!cs.caller_symbol.empty()) {
                triplets.emplace_back(cs.caller_symbol, "contains", callsite_id, src);
                triplets.emplace_back(callsite_id, "in_symbol", cs.caller_symbol, src);
            }

            // Core triplets
            triplets.emplace_back(callsite_id, "in_file", p.filename().string(), src);
            triplets.emplace_back(callsite_id, "at", cs.location(), src);
            triplets.emplace_back(callsite_id, "kind", call_kind_to_string(cs.kind), src);
            triplets.emplace_back(callsite_id, "arg_count", std::to_string(cs.arg_count), src);

            // Optional triplets
            if (!cs.callee_text.empty()) {
                triplets.emplace_back(callsite_id, "calls_text", cs.callee_text, src);
            }
            if (!cs.callee_leaf.empty()) {
                triplets.emplace_back(callsite_id, "callee_leaf", cs.callee_leaf, src);
                // Direct caller → calls → callee triplet for easy querying
                if (!cs.caller_symbol.empty()) {
                    triplets.emplace_back(cs.caller_symbol, "calls", cs.callee_leaf, src);
                }
            }
            if (!cs.scope_text.empty()) {
                triplets.emplace_back(callsite_id, "scope_text", cs.scope_text, src);
            }
            if (!cs.receiver_text.empty()) {
                triplets.emplace_back(callsite_id, "receiver_text", cs.receiver_text, src);
            }
            if (!cs.member_op.empty()) {
                triplets.emplace_back(callsite_id, "member_op", cs.member_op, src);
            }
            if (!cs.template_args.empty()) {
                triplets.emplace_back(callsite_id, "template_args", cs.template_args, src);
            }
            if (!cs.resolved_symbol.empty()) {
                triplets.emplace_back(callsite_id, "calls_symbol", cs.resolved_symbol, src);
            }
        }

        // Single batch insert with transaction
        store.connect_batch(triplets);
        return callsites.size();
    }

    // Store type relationships as triplets
    size_t store_type_relationships(DuckDBStore& store, const std::vector<TypeRelationship>& type_rels) {
        if (type_rels.empty()) return 0;

        std::vector<std::tuple<std::string, std::string, std::string, std::string>> triplets;
        triplets.reserve(type_rels.size());

        for (const auto& rel : type_rels) {
            // DerivedClass → extends → BaseClass
            triplets.emplace_back(rel.derived_name, rel.relationship, rel.base_name, rel.file_path);
        }

        store.connect_batch(triplets);
        return type_rels.size();
    }

    // Store imports as triplets
    size_t store_imports(DuckDBStore& store, const std::vector<ImportStatement>& imports) {
        if (imports.empty()) return 0;

        std::vector<std::tuple<std::string, std::string, std::string, std::string>> triplets;
        triplets.reserve(imports.size() * 2);

        for (const auto& imp : imports) {
            std::filesystem::path p(imp.file_path);
            std::string filename = p.filename().string();

            // file → imports → module
            triplets.emplace_back(filename, "imports", imp.import_path, imp.file_path);

            // Store specific names if present
            for (const auto& name : imp.names) {
                triplets.emplace_back(filename, "imports_name", name, imp.file_path);
            }

            // Store alias if present
            if (!imp.alias.empty()) {
                triplets.emplace_back(filename, "imports_as", imp.alias, imp.file_path);
            }
        }

        store.connect_batch(triplets);
        return imports.size();
    }

    // Store full extraction result (symbols + callsites + type relationships + imports)
    struct StoreResult {
        size_t symbols = 0;
        size_t callsites = 0;
        size_t type_relationships = 0;
        size_t imports = 0;
    };

    StoreResult store_full_v2(DuckDBStore& store, const ExtractionResult& result, int64_t repo_id = 0) {
        StoreResult r;
        r.symbols = store_symbols(store, result.symbols, repo_id);
        r.callsites = store_callsites(store, result.callsites);
        r.type_relationships = store_type_relationships(store, result.type_relationships);
        r.imports = store_imports(store, result.imports);
        return r;
    }

    // Legacy interface for backwards compatibility
    std::pair<size_t, size_t> store_full(DuckDBStore& store, const ExtractionResult& result,
                                          int64_t repo_id = 0) {
        auto r = store_full_v2(store, result, repo_id);
        return {r.symbols, r.callsites};
    }

private:
    std::unordered_map<std::string, TSParser*> parsers_;

    // Get text for a node
    std::string node_text(TSNode node, const std::string& source) {
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        if (end > source.length()) end = source.length();
        return source.substr(start, end - start);
    }

    // Get line number (1-based)
    int32_t node_line(TSNode node) {
        return static_cast<int32_t>(ts_node_start_point(node).row + 1);
    }

    int32_t node_end_line(TSNode node) {
        return static_cast<int32_t>(ts_node_end_point(node).row + 1);
    }

    // Find child by type (returns null node if not found)
    TSNode find_child(TSNode node, const char* type) {
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_child(node, i);
            if (strcmp(ts_node_type(child), type) == 0) {
                return child;
            }
        }
        TSNode null_node = {};
        null_node.id = nullptr;
        return null_node;
    }

    // Find child by field name
    TSNode find_child_by_field(TSNode node, const char* field_name) {
        return ts_node_child_by_field_name(node, field_name, strlen(field_name));
    }

    // Extract function/method name from declarator tree
    // Handles nested declarators like: function_declarator -> identifier
    // or: function_declarator -> field_identifier
    // or: function_declarator -> qualified_identifier -> identifier
    std::string extract_declarator_name(TSNode node, const std::string& source) {
        const char* type = ts_node_type(node);

        // Direct identifier
        if (strcmp(type, "identifier") == 0 ||
            strcmp(type, "field_identifier") == 0) {
            return node_text(node, source);
        }

        // Qualified identifier (e.g., ClassName::methodName)
        if (strcmp(type, "qualified_identifier") == 0) {
            TSNode name_node = find_child_by_field(node, "name");
            if (!ts_node_is_null(name_node)) {
                return node_text(name_node, source);
            }
        }

        // Destructor (e.g., ~ClassName)
        if (strcmp(type, "destructor_name") == 0) {
            return node_text(node, source);
        }

        // Template function
        if (strcmp(type, "template_function") == 0) {
            TSNode name_node = find_child_by_field(node, "name");
            if (!ts_node_is_null(name_node)) {
                return extract_declarator_name(name_node, source);
            }
        }

        // Try declarator field (nested declarator)
        TSNode declarator = find_child_by_field(node, "declarator");
        if (!ts_node_is_null(declarator)) {
            return extract_declarator_name(declarator, source);
        }

        // Fallback: search children for identifier types
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_child(node, i);
            const char* child_type = ts_node_type(child);
            if (strcmp(child_type, "identifier") == 0 ||
                strcmp(child_type, "field_identifier") == 0) {
                return node_text(child, source);
            }
        }

        return "";
    }

    // C/C++ extraction
    void extract_cpp(TSNode node, const std::string& source,
                     const std::string& path, std::vector<ExtractedSymbol>& symbols,
                     const std::string& parent = "") {
        const char* type = ts_node_type(node);

        if (strcmp(type, "function_definition") == 0) {
            // Get declarator field (contains function_declarator or other declarator types)
            TSNode declarator = find_child_by_field(node, "declarator");
            if (ts_node_is_null(declarator)) declarator = node;

            // Extract function name - recursively find identifier in declarator tree
            std::string name = extract_declarator_name(declarator, source);
            if (!name.empty()) {
                ExtractedSymbol sym;
                sym.kind = parent.empty() ? "function" : "method";
                sym.name = name;
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                sym.parent = parent;
                symbols.push_back(sym);
            }
            return;  // Don't recurse into function body for basic extraction
        } else if (strcmp(type, "class_specifier") == 0 ||
                   strcmp(type, "struct_specifier") == 0) {
            TSNode name_node = find_child(node, "type_identifier");
            if (!ts_node_is_null(name_node)) {
                std::string class_name = node_text(name_node, source);
                ExtractedSymbol sym;
                sym.kind = "class";
                sym.name = class_name;
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                symbols.push_back(sym);

                // Extract methods
                uint32_t count = ts_node_child_count(node);
                for (uint32_t i = 0; i < count; i++) {
                    extract_cpp(ts_node_child(node, i), source, path, symbols, class_name);
                }
                return;
            }
        }

        // Recurse to children
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            extract_cpp(ts_node_child(node, i), source, path, symbols, parent);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // C/C++ Full Extraction (Symbols + Callsites)
    // ═══════════════════════════════════════════════════════════════════════

    // Generate symbol ID for a function/method
    std::string make_symbol_id(const std::string& kind, const std::string& name,
                               const std::string& parent, const std::string& path) {
        std::string fqn = parent.empty() ? name : (parent + "::" + name);
        // Simple hash for now - could be improved with signature info
        return "cpp:" + kind + ":" + fqn;
    }

    // Count arguments in an argument_list node
    uint32_t count_args(TSNode args_node) {
        if (ts_node_is_null(args_node)) return 0;
        uint32_t count = 0;
        uint32_t child_count = ts_node_child_count(args_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(args_node, i);
            const char* type = ts_node_type(child);
            // Skip punctuation: ( ) ,
            if (strcmp(type, "(") != 0 && strcmp(type, ")") != 0 &&
                strcmp(type, ",") != 0) {
                count++;
            }
        }
        return count;
    }

    // Extract template args from callee text
    std::string extract_template_args(const std::string& text) {
        size_t start = text.find('<');
        if (start == std::string::npos) return "";
        size_t end = text.rfind('>');
        if (end == std::string::npos || end <= start) return "";
        return text.substr(start, end - start + 1);
    }

    // Extract leaf name (after last ::, before <)
    std::string extract_leaf_name(const std::string& text) {
        std::string result = text;
        // Remove template args
        size_t templ = result.find('<');
        if (templ != std::string::npos) {
            result = result.substr(0, templ);
        }
        // Get after last ::
        size_t scope = result.rfind("::");
        if (scope != std::string::npos) {
            result = result.substr(scope + 2);
        }
        return result;
    }

    // Extract scope (everything before last ::)
    std::string extract_scope(const std::string& text) {
        std::string result = text;
        // Remove template args from the end
        size_t templ = result.find('<');
        if (templ != std::string::npos) {
            result = result.substr(0, templ);
        }
        size_t scope = result.rfind("::");
        if (scope != std::string::npos) {
            return result.substr(0, scope);
        }
        return "";
    }

    // Extract callsite from a call_expression node
    Callsite extract_callsite_from_call(TSNode call_node, const std::string& source,
                                         const std::string& path,
                                         const std::string& caller_symbol) {
        Callsite cs;
        cs.file_path = path;
        cs.start_byte = ts_node_start_byte(call_node);
        cs.end_byte = ts_node_end_byte(call_node);
        cs.line = ts_node_start_point(call_node).row + 1;
        cs.column = ts_node_start_point(call_node).column + 1;
        cs.caller_symbol = caller_symbol;

        // Get function node (the thing being called)
        TSNode fn_node = find_child_by_field(call_node, "function");
        if (ts_node_is_null(fn_node)) {
            fn_node = ts_node_child(call_node, 0);  // Fallback
        }

        // Get arguments
        TSNode args_node = find_child_by_field(call_node, "arguments");
        cs.arg_count = count_args(args_node);

        // Classify based on function node type
        const char* fn_type = ts_node_type(fn_node);
        cs.callee_text = node_text(fn_node, source);

        if (strcmp(fn_type, "identifier") == 0) {
            // Simple call: foo()
            cs.kind = CallKind::Call;
            cs.callee_leaf = cs.callee_text;
        } else if (strcmp(fn_type, "field_expression") == 0) {
            // Member call: obj.method() or ptr->method()
            cs.kind = CallKind::MemberCall;

            // Extract receiver and field
            TSNode receiver = find_child_by_field(fn_node, "argument");
            if (ts_node_is_null(receiver)) {
                receiver = ts_node_child(fn_node, 0);
            }
            TSNode field = find_child_by_field(fn_node, "field");
            if (ts_node_is_null(field)) {
                // Try to find field_identifier
                for (uint32_t i = 0; i < ts_node_child_count(fn_node); i++) {
                    TSNode child = ts_node_child(fn_node, i);
                    if (strcmp(ts_node_type(child), "field_identifier") == 0) {
                        field = child;
                        break;
                    }
                }
            }

            if (!ts_node_is_null(receiver)) {
                cs.receiver_text = node_text(receiver, source);
            }
            if (!ts_node_is_null(field)) {
                cs.callee_leaf = node_text(field, source);
            } else {
                cs.callee_leaf = extract_leaf_name(cs.callee_text);
            }

            // Detect member operator
            if (cs.callee_text.find("->") != std::string::npos) {
                cs.member_op = "->";
            } else if (cs.callee_text.find(".") != std::string::npos) {
                cs.member_op = ".";
            }
        } else if (strcmp(fn_type, "qualified_identifier") == 0 ||
                   strcmp(fn_type, "scoped_identifier") == 0 ||
                   strcmp(fn_type, "template_function") == 0) {
            // Qualified call: ns::foo() or Class::method()
            cs.kind = CallKind::Qualified;
            cs.callee_leaf = extract_leaf_name(cs.callee_text);
            cs.scope_text = extract_scope(cs.callee_text);
            cs.template_args = extract_template_args(cs.callee_text);
            cs.member_op = "::";
        } else if (strcmp(fn_type, "lambda_expression") == 0) {
            // Lambda call: [](){}()
            cs.kind = CallKind::LambdaCall;
            cs.callee_leaf = "lambda";
        } else if (strcmp(fn_type, "type_identifier") == 0 ||
                   strcmp(fn_type, "scoped_type_identifier") == 0) {
            // Constructor call as expression: Foo(...)
            cs.kind = CallKind::Ctor;
            cs.callee_leaf = extract_leaf_name(cs.callee_text);
            cs.scope_text = extract_scope(cs.callee_text);
        } else {
            // Indirect/unknown: (*fp)(), fp(), etc.
            cs.kind = CallKind::Indirect;
            cs.callee_leaf = extract_leaf_name(cs.callee_text);
        }

        return cs;
    }

    // Extract callsite from a new_expression node
    Callsite extract_callsite_from_new(TSNode new_node, const std::string& source,
                                        const std::string& path,
                                        const std::string& caller_symbol) {
        Callsite cs;
        cs.file_path = path;
        cs.start_byte = ts_node_start_byte(new_node);
        cs.end_byte = ts_node_end_byte(new_node);
        cs.line = ts_node_start_point(new_node).row + 1;
        cs.column = ts_node_start_point(new_node).column + 1;
        cs.caller_symbol = caller_symbol;
        cs.kind = CallKind::New;

        // Find type being constructed
        TSNode type_node = find_child(new_node, "type_identifier");
        if (ts_node_is_null(type_node)) {
            type_node = find_child(new_node, "scoped_type_identifier");
        }
        if (!ts_node_is_null(type_node)) {
            cs.callee_text = node_text(type_node, source);
            cs.callee_leaf = extract_leaf_name(cs.callee_text);
            cs.scope_text = extract_scope(cs.callee_text);
        } else {
            cs.callee_text = node_text(new_node, source);
            cs.callee_leaf = "unknown";
        }

        // Find arguments if present
        TSNode args_node = find_child(new_node, "argument_list");
        cs.arg_count = count_args(args_node);

        return cs;
    }

    // Extract callsites from a function body
    void extract_callsites_from_body(TSNode body, const std::string& source,
                                      const std::string& path,
                                      const std::string& caller_symbol,
                                      std::vector<Callsite>& callsites) {
        const char* type = ts_node_type(body);

        if (strcmp(type, "call_expression") == 0) {
            callsites.push_back(extract_callsite_from_call(body, source, path, caller_symbol));
        } else if (strcmp(type, "new_expression") == 0) {
            callsites.push_back(extract_callsite_from_new(body, source, path, caller_symbol));
        }

        // Recurse
        uint32_t count = ts_node_child_count(body);
        for (uint32_t i = 0; i < count; i++) {
            extract_callsites_from_body(ts_node_child(body, i), source, path,
                                        caller_symbol, callsites);
        }
    }

    // C/C++ full extraction (symbols + callsites + type relationships + imports)
    void extract_cpp_full(TSNode node, const std::string& source,
                          const std::string& path,
                          std::vector<ExtractedSymbol>& symbols,
                          std::vector<Callsite>& callsites,
                          std::vector<TypeRelationship>& type_rels,
                          std::vector<ImportStatement>& imports,
                          const std::string& parent = "") {
        const char* type = ts_node_type(node);

        // #include statements
        if (strcmp(type, "preproc_include") == 0) {
            TSNode path_node = find_child(node, "string_literal");
            if (ts_node_is_null(path_node)) {
                path_node = find_child(node, "system_lib_string");
            }
            if (!ts_node_is_null(path_node)) {
                ImportStatement imp;
                imp.file_path = path;
                imp.import_path = node_text(path_node, source);
                // Remove quotes/angle brackets
                if (imp.import_path.size() >= 2) {
                    imp.import_path = imp.import_path.substr(1, imp.import_path.size() - 2);
                }
                imp.line = node_line(node);
                imports.push_back(imp);
            }
        }
        // Function definitions
        else if (strcmp(type, "function_definition") == 0) {
            // Get declarator field (contains function_declarator or other declarator types)
            TSNode declarator = find_child_by_field(node, "declarator");
            if (ts_node_is_null(declarator)) declarator = node;

            // Extract function name - recursively find identifier in declarator tree
            std::string name = extract_declarator_name(declarator, source);
            if (!name.empty()) {
                std::string kind = parent.empty() ? "function" : "method";

                // Add symbol
                ExtractedSymbol sym;
                sym.kind = kind;
                sym.name = name;
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                sym.parent = parent;
                symbols.push_back(sym);

                // Generate symbol ID for callsites
                std::string symbol_id = make_symbol_id(kind, name, parent, path);

                // Extract callsites from function body
                TSNode body = find_child_by_field(node, "body");
                if (!ts_node_is_null(body)) {
                    extract_callsites_from_body(body, source, path, symbol_id, callsites);
                }
            }
        }
        // Class/struct definitions with base class extraction
        else if (strcmp(type, "class_specifier") == 0 ||
                   strcmp(type, "struct_specifier") == 0) {
            TSNode name_node = find_child(node, "type_identifier");
            if (!ts_node_is_null(name_node)) {
                std::string class_name = node_text(name_node, source);

                ExtractedSymbol sym;
                sym.kind = "class";
                sym.name = class_name;
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                symbols.push_back(sym);

                // Extract base classes from base_class_clause
                TSNode base_clause = find_child(node, "base_class_clause");
                if (!ts_node_is_null(base_clause)) {
                    uint32_t bc_count = ts_node_child_count(base_clause);
                    for (uint32_t i = 0; i < bc_count; i++) {
                        TSNode child = ts_node_child(base_clause, i);
                        const char* child_type = ts_node_type(child);
                        // Look for type_identifier (base class name)
                        if (strcmp(child_type, "type_identifier") == 0 ||
                            strcmp(child_type, "qualified_identifier") == 0 ||
                            strcmp(child_type, "template_type") == 0) {
                            TypeRelationship rel;
                            rel.derived_name = class_name;
                            rel.base_name = node_text(child, source);
                            rel.relationship = "extends";
                            rel.file_path = path;
                            rel.line = node_line(child);
                            type_rels.push_back(rel);
                        }
                    }
                }

                // Extract methods and their callsites
                uint32_t count = ts_node_child_count(node);
                for (uint32_t i = 0; i < count; i++) {
                    extract_cpp_full(ts_node_child(node, i), source, path,
                                    symbols, callsites, type_rels, imports, class_name);
                }
                return;
            }
        }

        // Recurse to children
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            extract_cpp_full(ts_node_child(node, i), source, path,
                            symbols, callsites, type_rels, imports, parent);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Python Full Extraction (Symbols + Callsites + Type Relationships + Imports)
    // ═══════════════════════════════════════════════════════════════════════

    void extract_python_full(TSNode node, const std::string& source,
                             const std::string& path,
                             std::vector<ExtractedSymbol>& symbols,
                             std::vector<Callsite>& callsites,
                             std::vector<TypeRelationship>& type_rels,
                             std::vector<ImportStatement>& imports,
                             const std::string& parent = "") {
        const char* type = ts_node_type(node);

        // Import statements: import foo, import foo as bar
        if (strcmp(type, "import_statement") == 0) {
            uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; i++) {
                TSNode child = ts_node_child(node, i);
                const char* child_type = ts_node_type(child);
                if (strcmp(child_type, "dotted_name") == 0) {
                    ImportStatement imp;
                    imp.file_path = path;
                    imp.import_path = node_text(child, source);
                    imp.line = node_line(node);
                    imports.push_back(imp);
                } else if (strcmp(child_type, "aliased_import") == 0) {
                    TSNode name_node = find_child(child, "dotted_name");
                    TSNode alias_node = find_child(child, "identifier");
                    ImportStatement imp;
                    imp.file_path = path;
                    if (!ts_node_is_null(name_node)) {
                        imp.import_path = node_text(name_node, source);
                    }
                    if (!ts_node_is_null(alias_node)) {
                        imp.alias = node_text(alias_node, source);
                    }
                    imp.line = node_line(node);
                    imports.push_back(imp);
                }
            }
        }
        // From imports: from foo import bar, baz
        else if (strcmp(type, "import_from_statement") == 0) {
            ImportStatement imp;
            imp.file_path = path;
            imp.line = node_line(node);

            TSNode module_node = find_child(node, "dotted_name");
            if (!ts_node_is_null(module_node)) {
                imp.import_path = node_text(module_node, source);
            }

            // Extract imported names
            uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; i++) {
                TSNode child = ts_node_child(node, i);
                const char* child_type = ts_node_type(child);
                if (strcmp(child_type, "identifier") == 0) {
                    imp.names.push_back(node_text(child, source));
                } else if (strcmp(child_type, "aliased_import") == 0) {
                    TSNode name_node = find_child(child, "identifier");
                    if (!ts_node_is_null(name_node)) {
                        imp.names.push_back(node_text(name_node, source));
                    }
                }
            }
            if (!imp.import_path.empty() || !imp.names.empty()) {
                imports.push_back(imp);
            }
        }
        // Function/method definitions
        else if (strcmp(type, "function_definition") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (!ts_node_is_null(name_node)) {
                ExtractedSymbol sym;
                sym.kind = parent.empty() ? "function" : "method";
                sym.name = node_text(name_node, source);
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                sym.parent = parent;
                symbols.push_back(sym);
            }
        }
        // Class definitions with base class extraction
        else if (strcmp(type, "class_definition") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (!ts_node_is_null(name_node)) {
                std::string class_name = node_text(name_node, source);
                ExtractedSymbol sym;
                sym.kind = "class";
                sym.name = class_name;
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                symbols.push_back(sym);

                // Extract base classes from argument_list (Python class bases)
                TSNode args = find_child(node, "argument_list");
                if (!ts_node_is_null(args)) {
                    uint32_t args_count = ts_node_child_count(args);
                    for (uint32_t i = 0; i < args_count; i++) {
                        TSNode arg = ts_node_child(args, i);
                        const char* arg_type = ts_node_type(arg);
                        if (strcmp(arg_type, "identifier") == 0 ||
                            strcmp(arg_type, "attribute") == 0) {
                            TypeRelationship rel;
                            rel.derived_name = class_name;
                            rel.base_name = node_text(arg, source);
                            rel.relationship = "extends";
                            rel.file_path = path;
                            rel.line = node_line(arg);
                            type_rels.push_back(rel);
                        }
                    }
                }

                TSNode body = find_child(node, "block");
                if (!ts_node_is_null(body)) {
                    uint32_t count = ts_node_child_count(body);
                    for (uint32_t i = 0; i < count; i++) {
                        extract_python_full(ts_node_child(body, i), source, path,
                                           symbols, callsites, type_rels, imports, class_name);
                    }
                }
                return;
            }
        }
        // Call expressions: func(...) or obj.method(...)
        else if (strcmp(type, "call") == 0) {
            TSNode func_node = find_child(node, "function");
            TSNode args_node = find_child(node, "arguments");
            if (ts_node_is_null(func_node)) {
                // Try first child as function
                if (ts_node_child_count(node) > 0) {
                    func_node = ts_node_child(node, 0);
                }
            }
            if (!ts_node_is_null(func_node)) {
                Callsite cs;
                cs.file_path = path;
                cs.start_byte = ts_node_start_byte(node);
                cs.end_byte = ts_node_end_byte(node);
                cs.line = node_line(node);
                cs.column = ts_node_start_point(node).column;
                cs.caller_symbol = parent;
                cs.arg_count = count_args(args_node);
                cs.callee_text = node_text(func_node, source);

                const char* func_type = ts_node_type(func_node);
                if (strcmp(func_type, "attribute") == 0) {
                    // obj.method() - attribute call
                    cs.kind = CallKind::MemberCall;
                    TSNode attr_node = find_child(func_node, "attribute");
                    if (!ts_node_is_null(attr_node)) {
                        cs.callee_leaf = node_text(attr_node, source);
                    }
                    TSNode obj_node = find_child(func_node, "object");
                    if (!ts_node_is_null(obj_node)) {
                        cs.receiver_text = node_text(obj_node, source);
                    }
                    cs.member_op = ".";
                } else if (strcmp(func_type, "identifier") == 0) {
                    // Simple function call
                    cs.kind = CallKind::Call;
                    cs.callee_leaf = cs.callee_text;
                } else {
                    // Other call types (subscript, etc.)
                    cs.kind = CallKind::Indirect;
                    cs.callee_leaf = cs.callee_text;
                }
                callsites.push_back(cs);
            }
        }

        // Recurse to children
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            extract_python_full(ts_node_child(node, i), source, path,
                               symbols, callsites, type_rels, imports, parent);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // JavaScript/TypeScript Full Extraction (Symbols + Callsites + Types + Imports)
    // ═══════════════════════════════════════════════════════════════════════

    void extract_js_full(TSNode node, const std::string& source,
                         const std::string& path,
                         std::vector<ExtractedSymbol>& symbols,
                         std::vector<Callsite>& callsites,
                         std::vector<TypeRelationship>& type_rels,
                         std::vector<ImportStatement>& imports,
                         const std::string& parent = "") {
        const char* type = ts_node_type(node);

        // Import statements
        if (strcmp(type, "import_statement") == 0) {
            ImportStatement imp;
            imp.file_path = path;
            imp.line = node_line(node);

            // Find source (the module path)
            TSNode source_node = find_child(node, "string");
            if (!ts_node_is_null(source_node)) {
                imp.import_path = node_text(source_node, source);
                // Remove quotes
                if (imp.import_path.size() >= 2) {
                    imp.import_path = imp.import_path.substr(1, imp.import_path.size() - 2);
                }
            }

            // Find imported names
            TSNode clause = find_child(node, "import_clause");
            if (!ts_node_is_null(clause)) {
                uint32_t clause_count = ts_node_child_count(clause);
                for (uint32_t i = 0; i < clause_count; i++) {
                    TSNode child = ts_node_child(clause, i);
                    const char* child_type = ts_node_type(child);
                    if (strcmp(child_type, "identifier") == 0) {
                        imp.names.push_back(node_text(child, source));
                    } else if (strcmp(child_type, "named_imports") == 0) {
                        uint32_t spec_count = ts_node_child_count(child);
                        for (uint32_t j = 0; j < spec_count; j++) {
                            TSNode spec = ts_node_child(child, j);
                            if (strcmp(ts_node_type(spec), "import_specifier") == 0) {
                                TSNode name_node = find_child(spec, "identifier");
                                if (!ts_node_is_null(name_node)) {
                                    imp.names.push_back(node_text(name_node, source));
                                }
                            }
                        }
                    }
                }
            }
            imports.push_back(imp);
        }
        // Function declarations
        else if (strcmp(type, "function_declaration") == 0 ||
            strcmp(type, "method_definition") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (ts_node_is_null(name_node)) {
                name_node = find_child(node, "property_identifier");
            }
            if (!ts_node_is_null(name_node)) {
                ExtractedSymbol sym;
                sym.kind = parent.empty() ? "function" : "method";
                sym.name = node_text(name_node, source);
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                sym.parent = parent;
                symbols.push_back(sym);
            }
        }
        // Arrow functions with variable declaration
        else if (strcmp(type, "lexical_declaration") == 0 ||
                 strcmp(type, "variable_declaration") == 0) {
            TSNode decl = find_child(node, "variable_declarator");
            if (!ts_node_is_null(decl)) {
                TSNode name_node = find_child(decl, "identifier");
                TSNode value_node = find_child(decl, "value");
                if (!ts_node_is_null(name_node) && !ts_node_is_null(value_node)) {
                    const char* val_type = ts_node_type(value_node);
                    if (strcmp(val_type, "arrow_function") == 0 ||
                        strcmp(val_type, "function_expression") == 0) {
                        ExtractedSymbol sym;
                        sym.kind = "function";
                        sym.name = node_text(name_node, source);
                        sym.file_path = path;
                        sym.line_start = node_line(node);
                        sym.line_end = node_end_line(node);
                        symbols.push_back(sym);
                    }
                }
            }
        }
        // Class declarations with extends/implements
        else if (strcmp(type, "class_declaration") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (!ts_node_is_null(name_node)) {
                std::string class_name = node_text(name_node, source);
                ExtractedSymbol sym;
                sym.kind = "class";
                sym.name = class_name;
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                symbols.push_back(sym);

                // Extract extends clause
                TSNode heritage = find_child(node, "class_heritage");
                if (!ts_node_is_null(heritage)) {
                    TSNode extends_clause = find_child(heritage, "extends_clause");
                    if (!ts_node_is_null(extends_clause)) {
                        TSNode base_node = find_child(extends_clause, "identifier");
                        if (!ts_node_is_null(base_node)) {
                            TypeRelationship rel;
                            rel.derived_name = class_name;
                            rel.base_name = node_text(base_node, source);
                            rel.relationship = "extends";
                            rel.file_path = path;
                            rel.line = node_line(base_node);
                            type_rels.push_back(rel);
                        }
                    }

                    // TypeScript implements clause
                    TSNode implements_clause = find_child(heritage, "implements_clause");
                    if (!ts_node_is_null(implements_clause)) {
                        uint32_t impl_count = ts_node_child_count(implements_clause);
                        for (uint32_t i = 0; i < impl_count; i++) {
                            TSNode impl = ts_node_child(implements_clause, i);
                            if (strcmp(ts_node_type(impl), "type_identifier") == 0) {
                                TypeRelationship rel;
                                rel.derived_name = class_name;
                                rel.base_name = node_text(impl, source);
                                rel.relationship = "implements";
                                rel.file_path = path;
                                rel.line = node_line(impl);
                                type_rels.push_back(rel);
                            }
                        }
                    }
                }

                TSNode body = find_child(node, "class_body");
                if (!ts_node_is_null(body)) {
                    uint32_t count = ts_node_child_count(body);
                    for (uint32_t i = 0; i < count; i++) {
                        extract_js_full(ts_node_child(body, i), source, path,
                                       symbols, callsites, type_rels, imports, class_name);
                    }
                }
                return;
            }
        }
        // Call expressions
        else if (strcmp(type, "call_expression") == 0) {
            TSNode func_node = find_child(node, "function");
            TSNode args_node = find_child(node, "arguments");
            if (ts_node_is_null(func_node)) {
                if (ts_node_child_count(node) > 0) {
                    func_node = ts_node_child(node, 0);
                }
            }
            if (!ts_node_is_null(func_node)) {
                Callsite cs;
                cs.file_path = path;
                cs.start_byte = ts_node_start_byte(node);
                cs.end_byte = ts_node_end_byte(node);
                cs.line = node_line(node);
                cs.column = ts_node_start_point(node).column;
                cs.caller_symbol = parent;
                cs.arg_count = count_args(args_node);
                cs.callee_text = node_text(func_node, source);

                const char* func_type = ts_node_type(func_node);
                if (strcmp(func_type, "member_expression") == 0) {
                    cs.kind = CallKind::MemberCall;
                    TSNode prop = find_child(func_node, "property");
                    if (!ts_node_is_null(prop)) {
                        cs.callee_leaf = node_text(prop, source);
                    }
                    TSNode obj = find_child(func_node, "object");
                    if (!ts_node_is_null(obj)) {
                        cs.receiver_text = node_text(obj, source);
                    }
                    cs.member_op = ".";
                } else if (strcmp(func_type, "identifier") == 0) {
                    cs.kind = CallKind::Call;
                    cs.callee_leaf = cs.callee_text;
                } else {
                    cs.kind = CallKind::Indirect;
                    cs.callee_leaf = cs.callee_text;
                }
                callsites.push_back(cs);
            }
        }
        // new expression
        else if (strcmp(type, "new_expression") == 0) {
            TSNode constructor = find_child(node, "constructor");
            TSNode args_node = find_child(node, "arguments");
            if (ts_node_is_null(constructor) && ts_node_child_count(node) > 0) {
                constructor = ts_node_child(node, 1);  // Skip 'new' keyword
            }
            if (!ts_node_is_null(constructor)) {
                Callsite cs;
                cs.file_path = path;
                cs.start_byte = ts_node_start_byte(node);
                cs.end_byte = ts_node_end_byte(node);
                cs.line = node_line(node);
                cs.column = ts_node_start_point(node).column;
                cs.caller_symbol = parent;
                cs.kind = CallKind::New;
                cs.callee_text = node_text(constructor, source);
                cs.callee_leaf = cs.callee_text;
                cs.arg_count = count_args(args_node);
                callsites.push_back(cs);
            }
        }

        // Recurse
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            extract_js_full(ts_node_child(node, i), source, path,
                           symbols, callsites, type_rels, imports, parent);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Go Full Extraction (Symbols + Callsites + Types + Imports)
    // ═══════════════════════════════════════════════════════════════════════

    void extract_go_full(TSNode node, const std::string& source,
                         const std::string& path,
                         std::vector<ExtractedSymbol>& symbols,
                         std::vector<Callsite>& callsites,
                         std::vector<TypeRelationship>& type_rels,
                         std::vector<ImportStatement>& imports,
                         const std::string& parent = "") {
        const char* type = ts_node_type(node);

        // Import declarations
        if (strcmp(type, "import_declaration") == 0) {
            uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; i++) {
                TSNode child = ts_node_child(node, i);
                const char* child_type = ts_node_type(child);

                if (strcmp(child_type, "import_spec") == 0) {
                    ImportStatement imp;
                    imp.file_path = path;
                    imp.line = node_line(child);

                    TSNode path_node = find_child(child, "interpreted_string_literal");
                    if (!ts_node_is_null(path_node)) {
                        imp.import_path = node_text(path_node, source);
                        // Remove quotes
                        if (imp.import_path.size() >= 2) {
                            imp.import_path = imp.import_path.substr(1, imp.import_path.size() - 2);
                        }
                    }

                    // Check for alias
                    TSNode name_node = find_child(child, "package_identifier");
                    if (!ts_node_is_null(name_node)) {
                        imp.alias = node_text(name_node, source);
                    }

                    imports.push_back(imp);
                } else if (strcmp(child_type, "import_spec_list") == 0) {
                    // Multiple imports: import ( "foo" "bar" )
                    uint32_t spec_count = ts_node_child_count(child);
                    for (uint32_t j = 0; j < spec_count; j++) {
                        TSNode spec = ts_node_child(child, j);
                        if (strcmp(ts_node_type(spec), "import_spec") == 0) {
                            ImportStatement imp;
                            imp.file_path = path;
                            imp.line = node_line(spec);

                            TSNode path_node = find_child(spec, "interpreted_string_literal");
                            if (!ts_node_is_null(path_node)) {
                                imp.import_path = node_text(path_node, source);
                                if (imp.import_path.size() >= 2) {
                                    imp.import_path = imp.import_path.substr(1, imp.import_path.size() - 2);
                                }
                            }

                            TSNode name_node = find_child(spec, "package_identifier");
                            if (!ts_node_is_null(name_node)) {
                                imp.alias = node_text(name_node, source);
                            }

                            imports.push_back(imp);
                        }
                    }
                }
            }
        }
        // Function declarations
        else if (strcmp(type, "function_declaration") == 0 ||
            strcmp(type, "method_declaration") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (ts_node_is_null(name_node)) {
                name_node = find_child(node, "field_identifier");
            }
            if (!ts_node_is_null(name_node)) {
                ExtractedSymbol sym;
                sym.kind = parent.empty() ? "function" : "method";
                sym.name = node_text(name_node, source);
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                sym.parent = parent;
                symbols.push_back(sym);
            }
        }
        // Type declarations (structs, interfaces) with embedded types
        else if (strcmp(type, "type_declaration") == 0) {
            TSNode spec = find_child(node, "type_spec");
            if (!ts_node_is_null(spec)) {
                TSNode name_node = find_child(spec, "type_identifier");
                if (!ts_node_is_null(name_node)) {
                    std::string type_name = node_text(name_node, source);
                    ExtractedSymbol sym;
                    sym.kind = "struct";
                    sym.name = type_name;
                    sym.file_path = path;
                    sym.line_start = node_line(node);
                    sym.line_end = node_end_line(node);
                    symbols.push_back(sym);

                    // Extract embedded types from struct fields
                    TSNode struct_type = find_child(spec, "struct_type");
                    if (!ts_node_is_null(struct_type)) {
                        TSNode field_list = find_child(struct_type, "field_declaration_list");
                        if (!ts_node_is_null(field_list)) {
                            uint32_t field_count = ts_node_child_count(field_list);
                            for (uint32_t i = 0; i < field_count; i++) {
                                TSNode field = ts_node_child(field_list, i);
                                if (strcmp(ts_node_type(field), "field_declaration") == 0) {
                                    // Check if this is an embedded type (no name, just type)
                                    TSNode field_type = find_child(field, "type_identifier");
                                    TSNode field_name = find_child(field, "field_identifier");
                                    if (!ts_node_is_null(field_type) && ts_node_is_null(field_name)) {
                                        TypeRelationship rel;
                                        rel.derived_name = type_name;
                                        rel.base_name = node_text(field_type, source);
                                        rel.relationship = "embeds";
                                        rel.file_path = path;
                                        rel.line = node_line(field_type);
                                        type_rels.push_back(rel);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        // Call expressions
        else if (strcmp(type, "call_expression") == 0) {
            TSNode func_node = find_child(node, "function");
            TSNode args_node = find_child(node, "arguments");
            if (ts_node_is_null(func_node) && ts_node_child_count(node) > 0) {
                func_node = ts_node_child(node, 0);
            }
            if (!ts_node_is_null(func_node)) {
                Callsite cs;
                cs.file_path = path;
                cs.start_byte = ts_node_start_byte(node);
                cs.end_byte = ts_node_end_byte(node);
                cs.line = node_line(node);
                cs.column = ts_node_start_point(node).column;
                cs.caller_symbol = parent;
                cs.arg_count = count_args(args_node);
                cs.callee_text = node_text(func_node, source);

                const char* func_type = ts_node_type(func_node);
                if (strcmp(func_type, "selector_expression") == 0) {
                    cs.kind = CallKind::MemberCall;
                    TSNode field = find_child(func_node, "field");
                    if (!ts_node_is_null(field)) {
                        cs.callee_leaf = node_text(field, source);
                    }
                    TSNode operand = find_child(func_node, "operand");
                    if (!ts_node_is_null(operand)) {
                        cs.receiver_text = node_text(operand, source);
                    }
                    cs.member_op = ".";
                } else if (strcmp(func_type, "identifier") == 0) {
                    cs.kind = CallKind::Call;
                    cs.callee_leaf = cs.callee_text;
                } else if (strcmp(func_type, "qualified_identifier") == 0) {
                    cs.kind = CallKind::Qualified;
                    // package.Function
                    uint32_t cc = ts_node_child_count(func_node);
                    if (cc > 0) {
                        cs.scope_text = node_text(ts_node_child(func_node, 0), source);
                    }
                    if (cc > 2) {
                        cs.callee_leaf = node_text(ts_node_child(func_node, 2), source);
                    }
                } else {
                    cs.kind = CallKind::Indirect;
                    cs.callee_leaf = cs.callee_text;
                }
                callsites.push_back(cs);
            }
        }

        // Recurse
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            extract_go_full(ts_node_child(node, i), source, path,
                           symbols, callsites, type_rels, imports, parent);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Rust Full Extraction (Symbols + Callsites + Types + Imports)
    // ═══════════════════════════════════════════════════════════════════════

    void extract_rust_full(TSNode node, const std::string& source,
                           const std::string& path,
                           std::vector<ExtractedSymbol>& symbols,
                           std::vector<Callsite>& callsites,
                           std::vector<TypeRelationship>& type_rels,
                           std::vector<ImportStatement>& imports,
                           const std::string& parent = "") {
        const char* type = ts_node_type(node);

        // Use declarations (imports)
        if (strcmp(type, "use_declaration") == 0) {
            // Recursively extract use paths
            std::function<void(TSNode, std::string)> extract_use_paths;
            extract_use_paths = [&](TSNode n, std::string prefix) {
                const char* t = ts_node_type(n);

                if (strcmp(t, "identifier") == 0 || strcmp(t, "scoped_identifier") == 0) {
                    ImportStatement imp;
                    imp.file_path = path;
                    imp.import_path = prefix.empty() ? node_text(n, source)
                                                    : prefix + "::" + node_text(n, source);
                    imp.line = node_line(n);
                    imports.push_back(imp);
                } else if (strcmp(t, "use_wildcard") == 0) {
                    ImportStatement imp;
                    imp.file_path = path;
                    imp.import_path = prefix + "::*";
                    imp.line = node_line(n);
                    imports.push_back(imp);
                } else if (strcmp(t, "use_list") == 0) {
                    uint32_t count = ts_node_child_count(n);
                    for (uint32_t i = 0; i < count; i++) {
                        extract_use_paths(ts_node_child(n, i), prefix);
                    }
                } else if (strcmp(t, "scoped_use_list") == 0) {
                    TSNode scope = find_child(n, "path");
                    TSNode list = find_child(n, "use_list");
                    std::string new_prefix = prefix;
                    if (!ts_node_is_null(scope)) {
                        new_prefix = prefix.empty() ? node_text(scope, source)
                                                   : prefix + "::" + node_text(scope, source);
                    }
                    if (!ts_node_is_null(list)) {
                        extract_use_paths(list, new_prefix);
                    }
                } else if (strcmp(t, "use_as_clause") == 0) {
                    TSNode path_node = find_child(n, "path");
                    TSNode alias_node = find_child(n, "identifier");
                    ImportStatement imp;
                    imp.file_path = path;
                    if (!ts_node_is_null(path_node)) {
                        imp.import_path = prefix.empty() ? node_text(path_node, source)
                                                        : prefix + "::" + node_text(path_node, source);
                    }
                    if (!ts_node_is_null(alias_node)) {
                        imp.alias = node_text(alias_node, source);
                    }
                    imp.line = node_line(n);
                    imports.push_back(imp);
                } else {
                    uint32_t count = ts_node_child_count(n);
                    for (uint32_t i = 0; i < count; i++) {
                        extract_use_paths(ts_node_child(n, i), prefix);
                    }
                }
            };

            uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; i++) {
                extract_use_paths(ts_node_child(node, i), "");
            }
        }
        // Function definitions
        else if (strcmp(type, "function_item") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (!ts_node_is_null(name_node)) {
                ExtractedSymbol sym;
                sym.kind = parent.empty() ? "function" : "method";
                sym.name = node_text(name_node, source);
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                sym.parent = parent;
                symbols.push_back(sym);
            }
        }
        // Impl blocks with trait detection
        else if (strcmp(type, "impl_item") == 0) {
            TSNode type_node = find_child(node, "type_identifier");
            std::string impl_name;
            if (!ts_node_is_null(type_node)) {
                impl_name = node_text(type_node, source);
                ExtractedSymbol sym;
                sym.kind = "impl";
                sym.name = impl_name;
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                symbols.push_back(sym);

                // Check for trait implementation: impl Trait for Type
                // Look for "for" keyword which indicates trait impl
                uint32_t child_count = ts_node_child_count(node);
                TSNode trait_node = {};
                bool found_for = false;
                for (uint32_t i = 0; i < child_count; i++) {
                    TSNode child = ts_node_child(node, i);
                    const char* child_type = ts_node_type(child);
                    if (strcmp(child_type, "type_identifier") == 0 && !found_for) {
                        trait_node = child;
                    }
                    if (strcmp(node_text(child, source).c_str(), "for") == 0) {
                        found_for = true;
                    }
                }
                if (found_for && !ts_node_is_null(trait_node)) {
                    TypeRelationship rel;
                    rel.derived_name = impl_name;
                    rel.base_name = node_text(trait_node, source);
                    rel.relationship = "implements";
                    rel.file_path = path;
                    rel.line = node_line(trait_node);
                    type_rels.push_back(rel);
                }
            }
            // Extract methods inside impl block
            TSNode body = find_child(node, "declaration_list");
            if (!ts_node_is_null(body)) {
                uint32_t count = ts_node_child_count(body);
                for (uint32_t i = 0; i < count; i++) {
                    extract_rust_full(ts_node_child(body, i), source, path,
                                     symbols, callsites, type_rels, imports, impl_name);
                }
                return;
            }
        }
        // Struct definitions
        else if (strcmp(type, "struct_item") == 0) {
            TSNode name_node = find_child(node, "type_identifier");
            if (!ts_node_is_null(name_node)) {
                ExtractedSymbol sym;
                sym.kind = "struct";
                sym.name = node_text(name_node, source);
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                symbols.push_back(sym);
            }
        }
        // Trait definitions
        else if (strcmp(type, "trait_item") == 0) {
            TSNode name_node = find_child(node, "type_identifier");
            if (!ts_node_is_null(name_node)) {
                ExtractedSymbol sym;
                sym.kind = "trait";
                sym.name = node_text(name_node, source);
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                symbols.push_back(sym);
            }
        }
        // Call expressions
        else if (strcmp(type, "call_expression") == 0) {
            TSNode func_node = find_child(node, "function");
            TSNode args_node = find_child(node, "arguments");
            if (ts_node_is_null(func_node) && ts_node_child_count(node) > 0) {
                func_node = ts_node_child(node, 0);
            }
            if (!ts_node_is_null(func_node)) {
                Callsite cs;
                cs.file_path = path;
                cs.start_byte = ts_node_start_byte(node);
                cs.end_byte = ts_node_end_byte(node);
                cs.line = node_line(node);
                cs.column = ts_node_start_point(node).column;
                cs.caller_symbol = parent;
                cs.arg_count = count_args(args_node);
                cs.callee_text = node_text(func_node, source);

                const char* func_type = ts_node_type(func_node);
                if (strcmp(func_type, "field_expression") == 0) {
                    cs.kind = CallKind::MemberCall;
                    TSNode field = find_child(func_node, "field");
                    if (!ts_node_is_null(field)) {
                        cs.callee_leaf = node_text(field, source);
                    }
                    TSNode value = find_child(func_node, "value");
                    if (!ts_node_is_null(value)) {
                        cs.receiver_text = node_text(value, source);
                    }
                    cs.member_op = ".";
                } else if (strcmp(func_type, "scoped_identifier") == 0) {
                    cs.kind = CallKind::Qualified;
                    TSNode path_node = find_child(func_node, "path");
                    if (!ts_node_is_null(path_node)) {
                        cs.scope_text = node_text(path_node, source);
                    }
                    TSNode name = find_child(func_node, "name");
                    if (!ts_node_is_null(name)) {
                        cs.callee_leaf = node_text(name, source);
                    }
                } else if (strcmp(func_type, "identifier") == 0) {
                    cs.kind = CallKind::Call;
                    cs.callee_leaf = cs.callee_text;
                } else {
                    cs.kind = CallKind::Indirect;
                    cs.callee_leaf = cs.callee_text;
                }
                callsites.push_back(cs);
            }
        }

        // Recurse
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            extract_rust_full(ts_node_child(node, i), source, path,
                             symbols, callsites, type_rels, imports, parent);
        }
    }

    // Python extraction
    void extract_python(TSNode node, const std::string& source,
                        const std::string& path, std::vector<ExtractedSymbol>& symbols,
                        const std::string& parent = "") {
        const char* type = ts_node_type(node);

        if (strcmp(type, "function_definition") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (!ts_node_is_null(name_node)) {
                ExtractedSymbol sym;
                sym.kind = parent.empty() ? "function" : "method";
                sym.name = node_text(name_node, source);
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                sym.parent = parent;
                symbols.push_back(sym);
            }
        } else if (strcmp(type, "class_definition") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (!ts_node_is_null(name_node)) {
                std::string class_name = node_text(name_node, source);
                ExtractedSymbol sym;
                sym.kind = "class";
                sym.name = class_name;
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                symbols.push_back(sym);

                // Extract methods from class body
                TSNode body = find_child(node, "block");
                if (!ts_node_is_null(body)) {
                    uint32_t count = ts_node_child_count(body);
                    for (uint32_t i = 0; i < count; i++) {
                        extract_python(ts_node_child(body, i), source, path, symbols, class_name);
                    }
                }
                return;
            }
        }

        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            extract_python(ts_node_child(node, i), source, path, symbols, parent);
        }
    }

    // JavaScript/TypeScript extraction
    void extract_js(TSNode node, const std::string& source,
                    const std::string& path, std::vector<ExtractedSymbol>& symbols,
                    const std::string& parent = "") {
        const char* type = ts_node_type(node);

        if (strcmp(type, "function_declaration") == 0 ||
            strcmp(type, "method_definition") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (ts_node_is_null(name_node)) {
                name_node = find_child(node, "property_identifier");
            }
            if (!ts_node_is_null(name_node)) {
                ExtractedSymbol sym;
                sym.kind = parent.empty() ? "function" : "method";
                sym.name = node_text(name_node, source);
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                sym.parent = parent;
                symbols.push_back(sym);
            }
        } else if (strcmp(type, "class_declaration") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (!ts_node_is_null(name_node)) {
                std::string class_name = node_text(name_node, source);
                ExtractedSymbol sym;
                sym.kind = "class";
                sym.name = class_name;
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                symbols.push_back(sym);

                TSNode body = find_child(node, "class_body");
                if (!ts_node_is_null(body)) {
                    uint32_t count = ts_node_child_count(body);
                    for (uint32_t i = 0; i < count; i++) {
                        extract_js(ts_node_child(body, i), source, path, symbols, class_name);
                    }
                }
                return;
            }
        }

        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            extract_js(ts_node_child(node, i), source, path, symbols, parent);
        }
    }

    // Go extraction
    void extract_go(TSNode node, const std::string& source,
                    const std::string& path, std::vector<ExtractedSymbol>& symbols,
                    const std::string& parent = "") {
        const char* type = ts_node_type(node);

        if (strcmp(type, "function_declaration") == 0 ||
            strcmp(type, "method_declaration") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (ts_node_is_null(name_node)) {
                name_node = find_child(node, "field_identifier");
            }
            if (!ts_node_is_null(name_node)) {
                ExtractedSymbol sym;
                sym.kind = strcmp(type, "method_declaration") == 0 ? "method" : "function";
                sym.name = node_text(name_node, source);
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                symbols.push_back(sym);
            }
        } else if (strcmp(type, "type_declaration") == 0) {
            TSNode spec = find_child(node, "type_spec");
            if (!ts_node_is_null(spec)) {
                TSNode name_node = find_child(spec, "type_identifier");
                if (!ts_node_is_null(name_node)) {
                    ExtractedSymbol sym;
                    sym.kind = "type";
                    sym.name = node_text(name_node, source);
                    sym.file_path = path;
                    sym.line_start = node_line(node);
                    sym.line_end = node_end_line(node);
                    symbols.push_back(sym);
                }
            }
        }

        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            extract_go(ts_node_child(node, i), source, path, symbols, parent);
        }
    }

    // Rust extraction
    void extract_rust(TSNode node, const std::string& source,
                      const std::string& path, std::vector<ExtractedSymbol>& symbols,
                      const std::string& parent = "") {
        const char* type = ts_node_type(node);

        if (strcmp(type, "function_item") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (!ts_node_is_null(name_node)) {
                ExtractedSymbol sym;
                sym.kind = parent.empty() ? "function" : "method";
                sym.name = node_text(name_node, source);
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                sym.parent = parent;
                symbols.push_back(sym);
            }
        } else if (strcmp(type, "struct_item") == 0 || strcmp(type, "impl_item") == 0) {
            TSNode name_node = find_child(node, "type_identifier");
            if (!ts_node_is_null(name_node)) {
                std::string struct_name = node_text(name_node, source);
                if (strcmp(type, "struct_item") == 0) {
                    ExtractedSymbol sym;
                    sym.kind = "struct";
                    sym.name = struct_name;
                    sym.file_path = path;
                    sym.line_start = node_line(node);
                    sym.line_end = node_end_line(node);
                    symbols.push_back(sym);
                }

                // Extract impl methods
                uint32_t count = ts_node_child_count(node);
                for (uint32_t i = 0; i < count; i++) {
                    extract_rust(ts_node_child(node, i), source, path, symbols, struct_name);
                }
                return;
            }
        }

        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            extract_rust(ts_node_child(node, i), source, path, symbols, parent);
        }
    }

    // Java extraction
    void extract_java(TSNode node, const std::string& source,
                      const std::string& path, std::vector<ExtractedSymbol>& symbols,
                      const std::string& parent = "") {
        const char* type = ts_node_type(node);

        if (strcmp(type, "method_declaration") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (!ts_node_is_null(name_node)) {
                ExtractedSymbol sym;
                sym.kind = "method";
                sym.name = node_text(name_node, source);
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                sym.parent = parent;
                symbols.push_back(sym);
            }
        } else if (strcmp(type, "class_declaration") == 0 ||
                   strcmp(type, "interface_declaration") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (!ts_node_is_null(name_node)) {
                std::string class_name = node_text(name_node, source);
                ExtractedSymbol sym;
                sym.kind = strcmp(type, "interface_declaration") == 0 ? "interface" : "class";
                sym.name = class_name;
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                symbols.push_back(sym);

                TSNode body = find_child(node, "class_body");
                if (!ts_node_is_null(body)) {
                    uint32_t count = ts_node_child_count(body);
                    for (uint32_t i = 0; i < count; i++) {
                        extract_java(ts_node_child(body, i), source, path, symbols, class_name);
                    }
                }
                return;
            }
        }

        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            extract_java(ts_node_child(node, i), source, path, symbols, parent);
        }
    }

    // Ruby extraction
    void extract_ruby(TSNode node, const std::string& source,
                      const std::string& path, std::vector<ExtractedSymbol>& symbols,
                      const std::string& parent = "") {
        const char* type = ts_node_type(node);

        if (strcmp(type, "method") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (!ts_node_is_null(name_node)) {
                ExtractedSymbol sym;
                sym.kind = parent.empty() ? "function" : "method";
                sym.name = node_text(name_node, source);
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                sym.parent = parent;
                symbols.push_back(sym);
            }
        } else if (strcmp(type, "class") == 0 || strcmp(type, "module") == 0) {
            TSNode name_node = find_child(node, "constant");
            if (!ts_node_is_null(name_node)) {
                std::string class_name = node_text(name_node, source);
                ExtractedSymbol sym;
                sym.kind = strcmp(type, "module") == 0 ? "module" : "class";
                sym.name = class_name;
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                symbols.push_back(sym);

                uint32_t count = ts_node_child_count(node);
                for (uint32_t i = 0; i < count; i++) {
                    extract_ruby(ts_node_child(node, i), source, path, symbols, class_name);
                }
                return;
            }
        }

        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            extract_ruby(ts_node_child(node, i), source, path, symbols, parent);
        }
    }

    // C# extraction
    void extract_csharp(TSNode node, const std::string& source,
                        const std::string& path, std::vector<ExtractedSymbol>& symbols,
                        const std::string& parent = "") {
        const char* type = ts_node_type(node);

        if (strcmp(type, "method_declaration") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (!ts_node_is_null(name_node)) {
                ExtractedSymbol sym;
                sym.kind = "method";
                sym.name = node_text(name_node, source);
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                sym.parent = parent;
                symbols.push_back(sym);
            }
        } else if (strcmp(type, "class_declaration") == 0 ||
                   strcmp(type, "interface_declaration") == 0) {
            TSNode name_node = find_child(node, "identifier");
            if (!ts_node_is_null(name_node)) {
                std::string class_name = node_text(name_node, source);
                ExtractedSymbol sym;
                sym.kind = strcmp(type, "interface_declaration") == 0 ? "interface" : "class";
                sym.name = class_name;
                sym.file_path = path;
                sym.line_start = node_line(node);
                sym.line_end = node_end_line(node);
                symbols.push_back(sym);

                TSNode body = find_child(node, "declaration_list");
                if (!ts_node_is_null(body)) {
                    uint32_t count = ts_node_child_count(body);
                    for (uint32_t i = 0; i < count; i++) {
                        extract_csharp(ts_node_child(body, i), source, path, symbols, class_name);
                    }
                }
                return;
            }
        }

        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            extract_csharp(ts_node_child(node, i), source, path, symbols, parent);
        }
    }
};

}  // namespace chitta
