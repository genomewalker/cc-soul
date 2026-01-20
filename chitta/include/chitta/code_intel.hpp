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

    // Find child by type
    TSNode find_child(TSNode node, const char* type) {
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_child(node, i);
            if (strcmp(ts_node_type(child), type) == 0) {
                return child;
            }
        }
        return ts_node_child(node, 0);  // Return first child as fallback
    }

    // C/C++ extraction
    void extract_cpp(TSNode node, const std::string& source,
                     const std::string& path, std::vector<ExtractedSymbol>& symbols,
                     const std::string& parent = "") {
        const char* type = ts_node_type(node);

        if (strcmp(type, "function_definition") == 0 ||
            strcmp(type, "function_declarator") == 0) {
            TSNode declarator = find_child(node, "function_declarator");
            if (ts_node_is_null(declarator)) declarator = node;

            TSNode name_node = find_child(declarator, "identifier");
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
