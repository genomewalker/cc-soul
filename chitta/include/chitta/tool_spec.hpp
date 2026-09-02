// CLI tool-parameter spec types and the spec-driven argument coercion.
// The TOOL_SPECS table itself stays in rpc_server.cpp; only the types and the
// generic rules live here, so tool_spec_test compiles the same code the CLI runs.
#pragma once

#include <cstring>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace chitta {

struct ToolParam {
    const char* name;
    const char* description;
    bool required;
    const char* default_val; // nullptr if no default
};

struct ToolSpec {
    const char* name;
    const char* description;
    std::vector<ToolParam> params;
};

// A param is boolean when its spec says so: either the documented default is
// "true"/"false", or the description opens with the "true|false" enumeration
// (the form required params use, since they carry no default to key off).
inline bool is_bool_param(const ToolParam& p) {
    if (p.default_val &&
        (std::strcmp(p.default_val, "true") == 0 || std::strcmp(p.default_val, "false") == 0))
        return true;
    return p.description && std::strncmp(p.description, "true|false", 10) == 0;
}

// The CLI argument parser types values without consulting the spec, so a
// boolean that reaches it as a string stays a string and the daemon rejects it.
// Coerce every spec-declared boolean here rather than per-tool special cases —
// `memory_outcome`'s `success` used to have its own hardcoded branch.
inline void coerce_bool_params(const ToolSpec& spec, nlohmann::json& args) {
    for (const auto& p : spec.params) {
        if (!is_bool_param(p)) continue;
        auto it = args.find(p.name);
        if (it == args.end() || !it->is_string()) continue;
        const std::string& v = it->get_ref<const std::string&>();
        if (v == "true")
            *it = true;
        else if (v == "false")
            *it = false;
    }
}

} // namespace chitta
