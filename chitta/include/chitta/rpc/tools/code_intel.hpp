#pragma once
// RPC Code Intelligence Tools: analyze_code, code_context, code_search
//
// Uses tree-sitter for proper AST parsing of C++/Python code.
// Extracts symbols, relationships, and enables targeted search by location.

#include "../types.hpp"
#include "../protocol.hpp"
#include "../../mind.hpp"
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <tree_sitter/api.h>

// External tree-sitter parser declarations
extern "C" {
    const TSLanguage* tree_sitter_cpp();
    const TSLanguage* tree_sitter_python();
    const TSLanguage* tree_sitter_javascript();
    const TSLanguage* tree_sitter_typescript();
    const TSLanguage* tree_sitter_go();
    const TSLanguage* tree_sitter_rust();
    const TSLanguage* tree_sitter_java();
    const TSLanguage* tree_sitter_ruby();
    const TSLanguage* tree_sitter_c_sharp();
}

namespace chitta::rpc::tools::code_intel {

using json = nlohmann::json;
namespace fs = std::filesystem;

// Symbol kinds
enum class SymbolKind {
    Function,
    Class,
    Struct,
    Method,
    Variable,
    Namespace,
    Include,
    Enum,
    Field,
    Unknown
};

inline std::string symbol_kind_str(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::Function: return "function";
        case SymbolKind::Class: return "class";
        case SymbolKind::Struct: return "struct";
        case SymbolKind::Method: return "method";
        case SymbolKind::Variable: return "variable";
        case SymbolKind::Namespace: return "namespace";
        case SymbolKind::Include: return "include";
        case SymbolKind::Enum: return "enum";
        case SymbolKind::Field: return "field";
        default: return "symbol";
    }
}

struct Symbol {
    std::string name;
    SymbolKind kind;
    size_t line;
    size_t end_line;
    std::string scope;
    std::string signature;  // Function signature if applicable
};

// Helper: get node text from source
inline std::string get_node_text(TSNode node, const std::string& source) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (end > source.size()) end = source.size();
    if (start >= end) return "";
    return source.substr(start, end - start);
}

// Helper: find child by field name
inline TSNode find_child_by_field(TSNode node, const char* field) {
    return ts_node_child_by_field_name(node, field, strlen(field));
}

// Helper: find first child by type
inline TSNode find_child_by_type(TSNode node, const char* type) {
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (strcmp(ts_node_type(child), type) == 0) {
            return child;
        }
    }
    return ts_node_child(node, 0);  // Return null node
}

// Extract name from declarator (handles pointers, references, etc.)
inline std::string extract_name_from_declarator(TSNode node, const std::string& source) {
    const char* type = ts_node_type(node);

    if (strcmp(type, "identifier") == 0 || strcmp(type, "field_identifier") == 0) {
        return get_node_text(node, source);
    }
    if (strcmp(type, "destructor_name") == 0) {
        return "~" + get_node_text(node, source);
    }
    if (strcmp(type, "qualified_identifier") == 0) {
        TSNode name = find_child_by_field(node, "name");
        if (!ts_node_is_null(name)) {
            return get_node_text(name, source);
        }
    }
    if (strcmp(type, "function_declarator") == 0 ||
        strcmp(type, "pointer_declarator") == 0 ||
        strcmp(type, "reference_declarator") == 0) {
        TSNode declarator = find_child_by_field(node, "declarator");
        if (!ts_node_is_null(declarator)) {
            return extract_name_from_declarator(declarator, source);
        }
    }
    if (strcmp(type, "template_function") == 0) {
        TSNode name = find_child_by_field(node, "name");
        if (!ts_node_is_null(name)) {
            return get_node_text(name, source);
        }
    }
    if (strcmp(type, "operator_name") == 0 || strcmp(type, "operator_cast") == 0) {
        return get_node_text(node, source);
    }

    // Fallback: try to find any identifier child
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "identifier") == 0 || strcmp(child_type, "field_identifier") == 0) {
            return get_node_text(child, source);
        }
    }
    return "";
}

// Tree-sitter C++ symbol extractor
inline std::vector<Symbol> extract_cpp_symbols(const std::string& source) {
    std::vector<Symbol> symbols;

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_cpp());

    TSTree* tree = ts_parser_parse_string(parser, nullptr, source.c_str(), source.size());
    if (!tree) {
        ts_parser_delete(parser);
        return symbols;
    }

    TSNode root = ts_tree_root_node(tree);

    // Stack for traversal: (node, scope)
    struct TraversalItem {
        TSNode node;
        std::string scope;
    };
    std::vector<TraversalItem> stack;
    stack.push_back({root, ""});

    while (!stack.empty()) {
        TraversalItem item = stack.back();
        stack.pop_back();

        TSNode node = item.node;
        std::string scope = item.scope;
        const char* type = ts_node_type(node);

        Symbol sym;
        sym.line = ts_node_start_point(node).row + 1;
        sym.end_line = ts_node_end_point(node).row + 1;
        sym.scope = scope;

        bool add_symbol = false;
        std::string new_scope = scope;

        // Function definition
        if (strcmp(type, "function_definition") == 0) {
            TSNode declarator = find_child_by_field(node, "declarator");
            if (!ts_node_is_null(declarator)) {
                sym.name = extract_name_from_declarator(declarator, source);
                if (!sym.name.empty()) {
                    sym.kind = scope.empty() ? SymbolKind::Function : SymbolKind::Method;
                    sym.signature = get_node_text(declarator, source);
                    add_symbol = true;
                }
            }
        }
        // Class/struct specifier
        else if (strcmp(type, "class_specifier") == 0 || strcmp(type, "struct_specifier") == 0) {
            TSNode name = find_child_by_field(node, "name");
            if (!ts_node_is_null(name)) {
                sym.name = get_node_text(name, source);
                sym.kind = (strcmp(type, "class_specifier") == 0) ? SymbolKind::Class : SymbolKind::Struct;
                add_symbol = true;
                new_scope = scope.empty() ? sym.name : (scope + "::" + sym.name);
            }
        }
        // Namespace
        else if (strcmp(type, "namespace_definition") == 0) {
            TSNode name = find_child_by_field(node, "name");
            if (!ts_node_is_null(name)) {
                sym.name = get_node_text(name, source);
                sym.kind = SymbolKind::Namespace;
                add_symbol = true;
                new_scope = scope.empty() ? sym.name : (scope + "::" + sym.name);
            }
        }
        // Enum
        else if (strcmp(type, "enum_specifier") == 0) {
            TSNode name = find_child_by_field(node, "name");
            if (!ts_node_is_null(name)) {
                sym.name = get_node_text(name, source);
                sym.kind = SymbolKind::Enum;
                add_symbol = true;
            }
        }
        // Field declaration (class members)
        else if (strcmp(type, "field_declaration") == 0 && !scope.empty()) {
            TSNode declarator = find_child_by_field(node, "declarator");
            if (!ts_node_is_null(declarator)) {
                sym.name = extract_name_from_declarator(declarator, source);
                if (!sym.name.empty()) {
                    // Check if it's a function declaration (method)
                    const char* decl_type = ts_node_type(declarator);
                    if (strcmp(decl_type, "function_declarator") == 0 ||
                        (strcmp(decl_type, "pointer_declarator") == 0 &&
                         get_node_text(declarator, source).find('(') != std::string::npos)) {
                        sym.kind = SymbolKind::Method;
                        sym.signature = get_node_text(declarator, source);
                    } else {
                        sym.kind = SymbolKind::Field;
                    }
                    add_symbol = true;
                }
            }
        }
        // Preproc include
        else if (strcmp(type, "preproc_include") == 0) {
            TSNode path = find_child_by_field(node, "path");
            if (!ts_node_is_null(path)) {
                std::string include = get_node_text(path, source);
                // Remove quotes/brackets
                if (include.size() > 2) {
                    sym.name = include.substr(1, include.size() - 2);
                    sym.kind = SymbolKind::Include;
                    add_symbol = true;
                }
            }
        }
        // Template declaration - look inside for function/class
        else if (strcmp(type, "template_declaration") == 0) {
            // Will be processed through children
        }

        if (add_symbol && !sym.name.empty()) {
            symbols.push_back(sym);
        }

        // Add children to stack (reverse order to maintain traversal order)
        uint32_t count = ts_node_child_count(node);
        for (int i = count - 1; i >= 0; i--) {
            TSNode child = ts_node_child(node, i);
            if (!ts_node_is_null(child)) {
                stack.push_back({child, new_scope});
            }
        }
    }

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return symbols;
}

// Tree-sitter Python symbol extractor
inline std::vector<Symbol> extract_python_symbols(const std::string& source) {
    std::vector<Symbol> symbols;

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_python());

    TSTree* tree = ts_parser_parse_string(parser, nullptr, source.c_str(), source.size());
    if (!tree) {
        ts_parser_delete(parser);
        return symbols;
    }

    TSNode root = ts_tree_root_node(tree);

    struct TraversalItem {
        TSNode node;
        std::string scope;
    };
    std::vector<TraversalItem> stack;
    stack.push_back({root, ""});

    while (!stack.empty()) {
        TraversalItem item = stack.back();
        stack.pop_back();

        TSNode node = item.node;
        std::string scope = item.scope;
        const char* type = ts_node_type(node);

        Symbol sym;
        sym.line = ts_node_start_point(node).row + 1;
        sym.end_line = ts_node_end_point(node).row + 1;
        sym.scope = scope;

        bool add_symbol = false;
        std::string new_scope = scope;

        // Function definition
        if (strcmp(type, "function_definition") == 0) {
            TSNode name = find_child_by_field(node, "name");
            if (!ts_node_is_null(name)) {
                sym.name = get_node_text(name, source);
                sym.kind = scope.empty() ? SymbolKind::Function : SymbolKind::Method;

                // Get parameters for signature
                TSNode params = find_child_by_field(node, "parameters");
                if (!ts_node_is_null(params)) {
                    sym.signature = sym.name + get_node_text(params, source);
                }
                add_symbol = true;
            }
        }
        // Class definition
        else if (strcmp(type, "class_definition") == 0) {
            TSNode name = find_child_by_field(node, "name");
            if (!ts_node_is_null(name)) {
                sym.name = get_node_text(name, source);
                sym.kind = SymbolKind::Class;
                add_symbol = true;
                new_scope = scope.empty() ? sym.name : (scope + "." + sym.name);
            }
        }
        // Import statement
        else if (strcmp(type, "import_statement") == 0) {
            // import X, Y, Z
            uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; i++) {
                TSNode child = ts_node_child(node, i);
                const char* child_type = ts_node_type(child);
                if (strcmp(child_type, "dotted_name") == 0 || strcmp(child_type, "aliased_import") == 0) {
                    Symbol imp_sym;
                    imp_sym.line = sym.line;
                    imp_sym.end_line = sym.end_line;
                    imp_sym.kind = SymbolKind::Include;
                    if (strcmp(child_type, "aliased_import") == 0) {
                        TSNode name_node = find_child_by_field(child, "name");
                        if (!ts_node_is_null(name_node)) {
                            imp_sym.name = get_node_text(name_node, source);
                        }
                    } else {
                        imp_sym.name = get_node_text(child, source);
                    }
                    if (!imp_sym.name.empty()) {
                        symbols.push_back(imp_sym);
                    }
                }
            }
        }
        // Import from statement
        else if (strcmp(type, "import_from_statement") == 0) {
            TSNode module = find_child_by_field(node, "module_name");
            if (!ts_node_is_null(module)) {
                sym.name = get_node_text(module, source);
                sym.kind = SymbolKind::Include;
                add_symbol = true;
            }
        }

        if (add_symbol && !sym.name.empty()) {
            symbols.push_back(sym);
        }

        // Add children to stack
        uint32_t count = ts_node_child_count(node);
        for (int i = count - 1; i >= 0; i--) {
            TSNode child = ts_node_child(node, i);
            if (!ts_node_is_null(child)) {
                stack.push_back({child, new_scope});
            }
        }
    }

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return symbols;
}

// Generic symbol extractor for languages with similar AST structure
inline std::vector<Symbol> extract_generic_symbols(const std::string& source, const TSLanguage* language,
    const std::vector<std::string>& func_types,
    const std::vector<std::string>& class_types,
    const std::vector<std::string>& import_types,
    const std::string& scope_separator = ".")
{
    std::vector<Symbol> symbols;

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, language);

    TSTree* tree = ts_parser_parse_string(parser, nullptr, source.c_str(), source.size());
    if (!tree) {
        ts_parser_delete(parser);
        return symbols;
    }

    TSNode root = ts_tree_root_node(tree);

    struct TraversalItem {
        TSNode node;
        std::string scope;
    };
    std::vector<TraversalItem> stack;
    stack.push_back({root, ""});

    while (!stack.empty()) {
        TraversalItem item = stack.back();
        stack.pop_back();

        TSNode node = item.node;
        std::string scope = item.scope;
        const char* type = ts_node_type(node);

        Symbol sym;
        sym.line = ts_node_start_point(node).row + 1;
        sym.end_line = ts_node_end_point(node).row + 1;
        sym.scope = scope;

        bool add_symbol = false;
        std::string new_scope = scope;

        // Check for function types
        for (const auto& func_type : func_types) {
            if (strcmp(type, func_type.c_str()) == 0) {
                TSNode name = find_child_by_field(node, "name");
                if (!ts_node_is_null(name)) {
                    sym.name = get_node_text(name, source);
                    sym.kind = scope.empty() ? SymbolKind::Function : SymbolKind::Method;
                    TSNode params = find_child_by_field(node, "parameters");
                    if (!ts_node_is_null(params)) {
                        sym.signature = sym.name + get_node_text(params, source);
                    }
                    add_symbol = true;
                }
                break;
            }
        }

        // Check for class types
        if (!add_symbol) {
            for (const auto& class_type : class_types) {
                if (strcmp(type, class_type.c_str()) == 0) {
                    TSNode name = find_child_by_field(node, "name");
                    if (!ts_node_is_null(name)) {
                        sym.name = get_node_text(name, source);
                        sym.kind = SymbolKind::Class;
                        add_symbol = true;
                        new_scope = scope.empty() ? sym.name : (scope + scope_separator + sym.name);
                    }
                    break;
                }
            }
        }

        // Check for import types
        if (!add_symbol) {
            for (const auto& import_type : import_types) {
                if (strcmp(type, import_type.c_str()) == 0) {
                    // Try different field names for module/path
                    TSNode module = find_child_by_field(node, "module_name");
                    if (ts_node_is_null(module)) module = find_child_by_field(node, "path");
                    if (ts_node_is_null(module)) module = find_child_by_field(node, "source");
                    if (!ts_node_is_null(module)) {
                        std::string import_path = get_node_text(module, source);
                        // Remove quotes if present
                        if (import_path.size() >= 2 &&
                            ((import_path.front() == '"' && import_path.back() == '"') ||
                             (import_path.front() == '\'' && import_path.back() == '\''))) {
                            import_path = import_path.substr(1, import_path.size() - 2);
                        }
                        sym.name = import_path;
                        sym.kind = SymbolKind::Include;
                        add_symbol = true;
                    }
                    break;
                }
            }
        }

        if (add_symbol && !sym.name.empty()) {
            symbols.push_back(sym);
        }

        // Add children to stack
        uint32_t count = ts_node_child_count(node);
        for (int i = count - 1; i >= 0; i--) {
            TSNode child = ts_node_child(node, i);
            if (!ts_node_is_null(child)) {
                stack.push_back({child, new_scope});
            }
        }
    }

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return symbols;
}

// JavaScript/TypeScript symbol extractor
inline std::vector<Symbol> extract_js_symbols(const std::string& source, bool typescript = false) {
    return extract_generic_symbols(
        source,
        typescript ? tree_sitter_typescript() : tree_sitter_javascript(),
        {"function_declaration", "method_definition", "arrow_function", "function"},
        {"class_declaration", "interface_declaration"},
        {"import_statement", "import_specifier"},
        "."
    );
}

// Go symbol extractor
inline std::vector<Symbol> extract_go_symbols(const std::string& source) {
    return extract_generic_symbols(
        source,
        tree_sitter_go(),
        {"function_declaration", "method_declaration"},
        {"type_declaration", "type_spec"},
        {"import_declaration", "import_spec"},
        "."
    );
}

// Rust symbol extractor
inline std::vector<Symbol> extract_rust_symbols(const std::string& source) {
    return extract_generic_symbols(
        source,
        tree_sitter_rust(),
        {"function_item", "impl_item"},
        {"struct_item", "enum_item", "trait_item", "impl_item"},
        {"use_declaration"},
        "::"
    );
}

// Java symbol extractor
inline std::vector<Symbol> extract_java_symbols(const std::string& source) {
    return extract_generic_symbols(
        source,
        tree_sitter_java(),
        {"method_declaration", "constructor_declaration"},
        {"class_declaration", "interface_declaration", "enum_declaration"},
        {"import_declaration"},
        "."
    );
}

// Ruby symbol extractor
inline std::vector<Symbol> extract_ruby_symbols(const std::string& source) {
    return extract_generic_symbols(
        source,
        tree_sitter_ruby(),
        {"method", "singleton_method"},
        {"class", "module"},
        {"require", "require_relative"},
        "::"
    );
}

// C# symbol extractor
inline std::vector<Symbol> extract_csharp_symbols(const std::string& source) {
    return extract_generic_symbols(
        source,
        tree_sitter_c_sharp(),
        {"method_declaration", "constructor_declaration"},
        {"class_declaration", "interface_declaration", "struct_declaration", "enum_declaration"},
        {"using_directive"},
        "."
    );
}

// Helper: check if string ends with suffix
inline bool str_ends_with(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Detect language from extension
inline std::string detect_language(const std::string& path) {
    // C/C++
    if (str_ends_with(path, ".cpp") || str_ends_with(path, ".cc") || str_ends_with(path, ".cxx")) return "cpp";
    if (str_ends_with(path, ".hpp") || str_ends_with(path, ".h") || str_ends_with(path, ".hxx")) return "cpp";
    if (str_ends_with(path, ".c")) return "c";
    // Python
    if (str_ends_with(path, ".py") || str_ends_with(path, ".pyw")) return "python";
    // JavaScript/TypeScript
    if (str_ends_with(path, ".js") || str_ends_with(path, ".jsx") || str_ends_with(path, ".mjs")) return "javascript";
    if (str_ends_with(path, ".ts") || str_ends_with(path, ".tsx")) return "typescript";
    // Go
    if (str_ends_with(path, ".go")) return "go";
    // Rust
    if (str_ends_with(path, ".rs")) return "rust";
    // Java
    if (str_ends_with(path, ".java")) return "java";
    // Ruby
    if (str_ends_with(path, ".rb")) return "ruby";
    // C#
    if (str_ends_with(path, ".cs")) return "csharp";
    return "unknown";
}

// Get language for displaying in results
inline const TSLanguage* get_ts_language(const std::string& lang) {
    if (lang == "cpp" || lang == "c") return tree_sitter_cpp();
    if (lang == "python") return tree_sitter_python();
    if (lang == "javascript") return tree_sitter_javascript();
    if (lang == "typescript") return tree_sitter_typescript();
    if (lang == "go") return tree_sitter_go();
    if (lang == "rust") return tree_sitter_rust();
    if (lang == "java") return tree_sitter_java();
    if (lang == "ruby") return tree_sitter_ruby();
    if (lang == "csharp") return tree_sitter_c_sharp();
    return nullptr;
}

// Extract symbols for any supported language
inline std::vector<Symbol> extract_symbols(const std::string& source, const std::string& lang) {
    if (lang == "cpp" || lang == "c") return extract_cpp_symbols(source);
    if (lang == "python") return extract_python_symbols(source);
    if (lang == "javascript") return extract_js_symbols(source, false);
    if (lang == "typescript") return extract_js_symbols(source, true);
    if (lang == "go") return extract_go_symbols(source);
    if (lang == "rust") return extract_rust_symbols(source);
    if (lang == "java") return extract_java_symbols(source);
    if (lang == "ruby") return extract_ruby_symbols(source);
    if (lang == "csharp") return extract_csharp_symbols(source);
    return {};
}

// Register code intelligence tool schemas
inline void register_schemas(std::vector<ToolSchema>& tools) {
    tools.push_back({
        "analyze_code",
        "Analyze a source file using tree-sitter AST parsing and store symbols with line numbers. "
        "Supports: C/C++, Python, JavaScript, TypeScript, Go, Rust, Java, Ruby, C#. "
        "Creates entities for functions/classes and triplets for relationships (contains, calls). "
        "Enables targeted search without reading full files.",
        {
            {"type", "object"},
            {"properties", {
                {"file", {{"type", "string"}, {"description", "Path to source file to analyze"}}},
                {"project", {{"type", "string"}, {"default", ""},
                           {"description", "Project name for tagging (auto-detected if empty)"}}},
                {"update", {{"type", "boolean"}, {"default", true},
                          {"description", "Update existing symbols (vs skip if exists)"}}}
            }},
            {"required", {"file"}}
        }
    });

    tools.push_back({
        "extract_symbols",
        "Extract symbols from source files using tree-sitter AST parsing. "
        "Returns raw symbol data (functions, classes, methods, imports) for Claude to process into SSL. "
        "Supports: C/C++, Python, JavaScript, TypeScript, Go, Rust, Java, Ruby, C#. "
        "Use this to get raw data, then generate SSL patterns and triplets yourself.",
        {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "File or directory path to analyze"}}},
                {"recursive", {{"type", "boolean"}, {"default", true},
                             {"description", "Recursively traverse directories"}}},
                {"exclude", {{"type", "array"}, {"items", {{"type", "string"}}}, {"default", json::array()},
                           {"description", "Directory names to exclude (e.g., [\"node_modules\", \"build\"])"}}}
            }},
            {"required", {"path"}}
        }
    });

    tools.push_back({
        "code_context",
        "Get code context around a specific location. Returns lines around the target "
        "without needing to read the full file. Use after finding a symbol via search.",
        {
            {"type", "object"},
            {"properties", {
                {"file", {{"type", "string"}, {"description", "Path to source file"}}},
                {"line", {{"type", "integer"}, {"description", "Target line number"}}},
                {"context", {{"type", "integer"}, {"default", 10},
                           {"description", "Lines of context before and after"}}}
            }},
            {"required", {"file", "line"}}
        }
    });

    tools.push_back({
        "code_search",
        "Search for code symbols by name, type, or file. Returns locations without "
        "reading files. Use code_context to get actual code when needed.",
        {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "Symbol name or pattern to search"}}},
                {"kind", {{"type", "string"}, {"enum", {"function", "class", "struct", "method", "any"}},
                        {"default", "any"}, {"description", "Filter by symbol kind"}}},
                {"file", {{"type", "string"}, {"default", ""},
                        {"description", "Filter by file path pattern"}}},
                {"limit", {{"type", "integer"}, {"default", 20},
                         {"description", "Max results to return"}}}
            }},
            {"required", {"query"}}
        }
    });
}

// Register code intelligence tool handlers
inline void register_handlers(std::unordered_map<std::string, ToolHandler>& handlers, Mind* mind) {

    // analyze_code: Extract and store symbols from a source file
    handlers["analyze_code"] = [mind](const json& params) -> ToolResult {
        std::string file_path = params.at("file");
        std::string project = params.value("project", "");
        bool update = params.value("update", true);

        // Check file exists
        if (!fs::exists(file_path)) {
            return ToolResult::error("File not found: " + file_path);
        }

        // Auto-detect project from path
        if (project.empty()) {
            fs::path p(file_path);
            // Use parent directory name as project
            if (p.has_parent_path()) {
                project = p.parent_path().filename().string();
            }
            if (project.empty() || project == "." || project == "..") {
                project = "code";
            }
        }

        // Read file
        std::ifstream file(file_path);
        if (!file.is_open()) {
            return ToolResult::error("Cannot open file: " + file_path);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        file.close();

        // Detect language and extract symbols
        std::string lang = detect_language(file_path);
        if (lang == "unknown") {
            return ToolResult::error("Unsupported language for file: " + file_path);
        }

        std::vector<Symbol> symbols = extract_symbols(content, lang);

        // Get relative path for storage
        fs::path abs_path = fs::absolute(file_path);
        std::string rel_path = abs_path.filename().string();
        // Try to get path relative to common ancestors
        for (const auto& comp : abs_path) {
            if (comp == "include" || comp == "src" || comp == "lib") {
                rel_path = fs::relative(abs_path, abs_path.parent_path().parent_path()).string();
                break;
            }
        }

        int symbols_stored = 0;
        int triplets_created = 0;
        std::string file_entity = rel_path;

        // Create file entity
        auto file_id = mind->find_or_create_entity(file_entity);
        mind->add_tag(file_id, "file");
        mind->add_tag(file_id, "project:" + project);
        mind->add_tag(file_id, "lang:" + lang);

        for (const auto& sym : symbols) {
            // Skip includes for symbol storage (keep as triplets only)
            if (sym.kind == SymbolKind::Include) {
                mind->connect(project, "includes", sym.name, 0.8f);
                triplets_created++;
                continue;
            }

            // Create symbol text: [project] symbol @file:line
            std::string symbol_text = "[" + project + "] " + sym.name + " @" + rel_path + ":" + std::to_string(sym.line);
            if (sym.end_line != sym.line) {
                symbol_text += "-" + std::to_string(sym.end_line);
            }

            // Check if exists (by searching for exact location)
            auto existing = mind->recall(symbol_text, 1, 0.95f);
            if (!existing.empty() && !update) {
                continue;  // Skip existing
            }

            // Store as term node with location
            NodeId sym_id;
            if (mind->has_yantra()) {
                sym_id = mind->remember(symbol_text, NodeType::Term, Confidence(0.9f));
            } else {
                sym_id = mind->remember(NodeType::Term, Vector::zeros(), Confidence(0.9f),
                                       std::vector<uint8_t>(symbol_text.begin(), symbol_text.end()));
            }

            // Add tags for filtering
            mind->add_tag(sym_id, "code");
            mind->add_tag(sym_id, "project:" + project);
            mind->add_tag(sym_id, "file:" + rel_path);
            mind->add_tag(sym_id, "kind:" + symbol_kind_str(sym.kind));
            mind->add_tag(sym_id, "line:" + std::to_string(sym.line));

            // Create entity for the symbol
            auto sym_entity_id = mind->find_or_create_entity(sym.name);
            mind->add_tag(sym_entity_id, symbol_kind_str(sym.kind));

            // Triplet: file contains symbol
            mind->connect(file_entity, "contains", sym.name, 0.9f);
            triplets_created++;

            // If method, create scope relationship
            if (!sym.scope.empty()) {
                mind->connect(sym.scope, "contains", sym.name, 0.9f);
                triplets_created++;
            }

            // Link symbol node to entity
            mind->connect(sym_id, sym_entity_id, EdgeType::Mentions, 1.0f);

            symbols_stored++;
        }

        json result;
        result["file"] = rel_path;
        result["project"] = project;
        result["language"] = lang;
        result["parser"] = "tree-sitter";
        result["symbols_found"] = symbols.size();
        result["symbols_stored"] = symbols_stored;
        result["triplets_created"] = triplets_created;

        std::ostringstream ss;
        ss << "Analyzed " << rel_path << " (" << lang << ", tree-sitter):\n";
        ss << "  Symbols found: " << symbols.size() << "\n";
        ss << "  Symbols stored: " << symbols_stored << "\n";
        ss << "  Triplets created: " << triplets_created;

        return ToolResult::ok(ss.str(), result);
    };

    // extract_symbols: Extract raw symbol data for Claude to process
    handlers["extract_symbols"] = [mind](const json& params) -> ToolResult {
        std::string path = params.at("path");
        bool recursive = params.value("recursive", true);
        std::vector<std::string> exclude_dirs;
        if (params.contains("exclude") && params["exclude"].is_array()) {
            for (const auto& e : params["exclude"]) {
                exclude_dirs.push_back(e.get<std::string>());
            }
        }
        if (exclude_dirs.empty()) {
            exclude_dirs = {"node_modules", "build", "dist", ".git", "__pycache__", "target", "vendor", "deps"};
        }

        std::vector<std::string> extensions = {
            ".cpp", ".cc", ".cxx", ".hpp", ".h", ".hxx", ".c",
            ".py", ".pyw",
            ".js", ".jsx", ".mjs", ".ts", ".tsx",
            ".go", ".rs", ".java", ".rb", ".cs"
        };

        json files_data = json::array();
        int total_files = 0;
        int total_symbols = 0;

        auto process_file = [&](const std::string& file_path, const std::string& rel_path) {
            std::string lang = detect_language(file_path);
            if (lang == "unknown") return;

            std::ifstream file(file_path);
            if (!file.is_open()) return;

            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            file.close();

            std::vector<Symbol> symbols = extract_symbols(content, lang);
            if (symbols.empty()) return;

            json file_obj;
            file_obj["path"] = rel_path;
            file_obj["language"] = lang;
            file_obj["symbols"] = json::array();

            for (const auto& sym : symbols) {
                json sym_obj;
                sym_obj["name"] = sym.name;
                sym_obj["kind"] = symbol_kind_str(sym.kind);
                sym_obj["line"] = sym.line;
                sym_obj["end_line"] = sym.end_line;
                if (!sym.scope.empty()) {
                    sym_obj["scope"] = sym.scope;
                }
                if (!sym.signature.empty()) {
                    sym_obj["signature"] = sym.signature;
                }
                file_obj["symbols"].push_back(sym_obj);
                total_symbols++;
            }

            files_data.push_back(file_obj);
            total_files++;
        };

        if (fs::is_regular_file(path)) {
            // Single file
            process_file(path, fs::path(path).filename().string());
        } else if (fs::is_directory(path)) {
            // Directory
            auto iterator = recursive
                ? fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied)
                : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied);

            for (const auto& entry : fs::recursive_directory_iterator(path,
                    fs::directory_options::skip_permission_denied)) {
                if (!entry.is_regular_file()) continue;

                // Check exclusions
                bool excluded = false;
                for (const auto& parent : entry.path()) {
                    for (const auto& excl : exclude_dirs) {
                        if (parent.string() == excl) {
                            excluded = true;
                            break;
                        }
                    }
                    if (excluded) break;
                }
                if (excluded) continue;

                // Check extension
                std::string ext = entry.path().extension().string();
                bool supported = false;
                for (const auto& e : extensions) {
                    if (ext == e) { supported = true; break; }
                }
                if (!supported) continue;

                std::string rel_path;
                try {
                    rel_path = fs::relative(entry.path(), path).string();
                } catch (...) {
                    rel_path = entry.path().filename().string();
                }
                process_file(entry.path().string(), rel_path);
            }
        } else {
            return ToolResult::error("Path not found: " + path);
        }

        json result;
        result["path"] = path;
        result["files"] = files_data;
        result["total_files"] = total_files;
        result["total_symbols"] = total_symbols;

        std::ostringstream ss;
        ss << "Extracted " << total_symbols << " symbols from " << total_files << " files.\n\n";
        ss << "Files:\n";
        for (const auto& f : files_data) {
            ss << "  " << f["path"].get<std::string>() << " (" << f["symbols"].size() << " symbols)\n";
        }
        ss << "\nUse this data to generate SSL patterns and triplets.";

        return ToolResult::ok(ss.str(), result);
    };

    // code_context: Get code around a specific line
    handlers["code_context"] = [mind](const json& params) -> ToolResult {
        std::string file_path = params.at("file");
        int target_line = params.at("line");
        int context = params.value("context", 10);

        std::ifstream file(file_path);
        if (!file.is_open()) {
            return ToolResult::error("Cannot open file: " + file_path);
        }

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        file.close();

        if (target_line < 1 || target_line > static_cast<int>(lines.size())) {
            return ToolResult::error("Line " + std::to_string(target_line) +
                                    " out of range (1-" + std::to_string(lines.size()) + ")");
        }

        int start = std::max(1, target_line - context);
        int end = std::min(static_cast<int>(lines.size()), target_line + context);

        std::ostringstream ss;
        ss << file_path << ":" << target_line << "\n";
        ss << "─────────────────────────────────────────\n";

        for (int i = start; i <= end; i++) {
            bool is_target = (i == target_line);
            ss << (is_target ? ">>> " : "    ");
            ss << std::setw(4) << i << " │ " << lines[i - 1] << "\n";
        }

        json result;
        result["file"] = file_path;
        result["target_line"] = target_line;
        result["start_line"] = start;
        result["end_line"] = end;

        return ToolResult::ok(ss.str(), result);
    };

    // code_search: Search for symbols by name/pattern
    handlers["code_search"] = [mind](const json& params) -> ToolResult {
        std::string query = params.at("query");
        std::string kind_filter = params.value("kind", "any");
        std::string file_filter = params.value("file", "");
        size_t limit = params.value("limit", 20);

        // Search using tags for filtering
        std::vector<std::string> search_tags;
        search_tags.push_back("code");
        if (kind_filter != "any") {
            search_tags.push_back("kind:" + kind_filter);
        }
        if (!file_filter.empty()) {
            search_tags.push_back("file:" + file_filter);
        }

        // Do semantic search with tag filtering
        auto results = mind->recall(query, limit * 3);  // Get more to filter

        std::vector<Recall> filtered;
        for (const auto& r : results) {
            auto tags = mind->get_tags(r.id);

            // Check has "code" tag
            bool has_code = false;
            bool matches_kind = (kind_filter == "any");
            bool matches_file = file_filter.empty();

            for (const auto& tag : tags) {
                if (tag == "code") has_code = true;
                if (kind_filter != "any" && tag == "kind:" + kind_filter) matches_kind = true;
                if (!file_filter.empty() && tag.find("file:") == 0 &&
                    tag.find(file_filter) != std::string::npos) matches_file = true;
            }

            if (has_code && matches_kind && matches_file) {
                filtered.push_back(r);
                if (filtered.size() >= limit) break;
            }
        }

        json result_arr = json::array();
        std::ostringstream ss;
        ss << "Found " << filtered.size() << " symbols matching \"" << query << "\":\n\n";

        for (const auto& r : filtered) {
            // Parse location from text: [project] name @file:line
            std::string text = r.text;

            // Extract file:line
            size_t at_pos = text.rfind('@');
            std::string location = (at_pos != std::string::npos) ? text.substr(at_pos + 1) : "";

            // Extract line number
            size_t colon_pos = location.rfind(':');
            std::string file = (colon_pos != std::string::npos) ? location.substr(0, colon_pos) : location;
            std::string line_str = (colon_pos != std::string::npos) ? location.substr(colon_pos + 1) : "0";
            int line_num = 0;
            try { line_num = std::stoi(line_str); } catch (...) {}

            json item;
            item["text"] = text;
            item["file"] = file;
            item["line"] = line_num;
            item["score"] = r.relevance;
            result_arr.push_back(item);

            ss << "[" << static_cast<int>(r.relevance * 100) << "%] " << text << "\n";
        }

        json result;
        result["query"] = query;
        result["count"] = filtered.size();
        result["results"] = result_arr;

        return ToolResult::ok(ss.str(), result);
    };
}

} // namespace chitta::rpc::tools::code_intel
