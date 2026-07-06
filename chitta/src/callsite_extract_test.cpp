// Regression test: member/qualified callsites must carry a non-empty
// callee_leaf. Guards the find_child_by_field fix in the language
// extractors — find_child (by node TYPE) silently returned null for
// tree-sitter FIELD names like "field"/"value"/"object"/"attribute",
// leaving callee_leaf empty for every member call.
#include <chitta/code_intel.hpp>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace chitta;

static std::string write_tmp(const std::string& name, const std::string& body) {
    auto dir = std::filesystem::temp_directory_path() / "callsite_extract_test";
    std::filesystem::create_directories(dir);
    auto p = dir / name;
    std::ofstream(p) << body;
    return p.string();
}

static const Callsite* find_callee(const std::vector<Callsite>& cs,
                                   const std::string& leaf) {
    for (const auto& c : cs)
        if (c.callee_leaf == leaf) return &c;
    return nullptr;
}

int main() {
    CodeIntel intel;

    // Rust: self.method() (field_expression) + Foo::bar() (scoped_identifier)
    {
        auto path = write_tmp("t.rs",
            "impl Store {\n"
            "    fn put_memory(&self) {\n"
            "        self.span_link_memory();\n"
            "        Helper::rrf_merge();\n"
            "        plain_call();\n"
            "    }\n"
            "}\n");
        auto r = intel.extract_file_full(path);
        assert(find_callee(r.callsites, "span_link_memory") && "rust member call");
        assert(find_callee(r.callsites, "rrf_merge") && "rust qualified call");
        assert(find_callee(r.callsites, "plain_call") && "rust bare call");
        auto* m = find_callee(r.callsites, "span_link_memory");
        assert(m->kind == CallKind::MemberCall);
        assert(m->receiver_text == "self");
    }

    // Python: self.method() (attribute)
    {
        auto path = write_tmp("t.py",
            "class Store:\n"
            "    def put(self):\n"
            "        self.link_memory()\n"
            "        bare_call()\n");
        auto r = intel.extract_file_full(path);
        assert(find_callee(r.callsites, "link_memory") && "python member call");
        assert(find_callee(r.callsites, "bare_call") && "python bare call");
        auto* m = find_callee(r.callsites, "link_memory");
        assert(m->receiver_text == "self");
    }

    // JS: obj.method() (member_expression)
    {
        auto path = write_tmp("t.js",
            "function put() {\n"
            "  store.linkMemory();\n"
            "  bareCall();\n"
            "}\n");
        auto r = intel.extract_file_full(path);
        assert(find_callee(r.callsites, "linkMemory") && "js member call");
        assert(find_callee(r.callsites, "bareCall") && "js bare call");
    }

    // Go: recv.Method() (selector_expression)
    {
        auto path = write_tmp("t.go",
            "package main\n"
            "func (s *Store) Put() {\n"
            "\ts.LinkMemory()\n"
            "\tbareCall()\n"
            "}\n");
        auto r = intel.extract_file_full(path);
        assert(find_callee(r.callsites, "LinkMemory") && "go member call");
        assert(find_callee(r.callsites, "bareCall") && "go bare call");
    }

    // C++ sanity (was already correct via find_child_by_field)
    {
        auto path = write_tmp("t.cpp",
            "struct Store { void link(); };\n"
            "void put(Store& s) {\n"
            "    s.link();\n"
            "    bare_call();\n"
            "}\n");
        auto r = intel.extract_file_full(path);
        assert(find_callee(r.callsites, "link") && "cpp member call");
        assert(find_callee(r.callsites, "bare_call") && "cpp bare call");
    }

    std::printf("callsite_extract_test: OK\n");
    return 0;
}
