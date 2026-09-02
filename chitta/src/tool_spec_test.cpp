// Spec-driven boolean coercion for the CLI argument parser. Replaces the
// hardcoded memory_outcome/success branch that used to live in rpc_server.cpp.
#include <cassert>
#include <cstdio>

#include <chitta/tool_spec.hpp>

using chitta::coerce_bool_params;
using chitta::is_bool_param;
using chitta::ToolSpec;
using json = nlohmann::json;

int main() {
    // Boolean by documented default, either polarity.
    assert(is_bool_param({"prefilter", "Recall-biased pre-filter", false, "true"}));
    assert(is_bool_param({"apply", "Create the edges", false, "false"}));
    // Boolean by description, for required params that carry no default —
    // this is the memory_outcome/success shape.
    assert(is_bool_param({"success", "true|false: did the action succeed", true, nullptr}));
    // Not boolean: no default, no marker; or a non-boolean default.
    assert(!is_bool_param({"query", "Search text", true, nullptr}));
    assert(!is_bool_param({"limit", "Max results", false, "10"}));
    assert(!is_bool_param({"type", "Node type: wisdom|belief", false, "episode"}));

    const ToolSpec spec{"memory_outcome",
                        "",
                        {{"id", "Node ID", true, nullptr},
                         {"success", "true|false: did the action succeed", true, nullptr},
                         {"weight", "Observation weight", false, "1.0"},
                         {"dry_run", "Preview only", false, "true"}}};

    json args{{"id", "123"}, {"success", "true"}, {"weight", "2.0"}, {"dry_run", "false"}};
    coerce_bool_params(spec, args);
    assert(args["success"].is_boolean() && args["success"].get<bool>());
    assert(args["dry_run"].is_boolean() && !args["dry_run"].get<bool>());
    // Non-boolean params are left exactly as the parser produced them.
    assert(args["id"].is_string() && args["id"].get<std::string>() == "123");
    assert(args["weight"].is_string() && args["weight"].get<std::string>() == "2.0");

    // Already-typed booleans (the common path — the parser types bare
    // true/false itself) are untouched, not double-converted.
    json typed{{"success", true}, {"dry_run", false}};
    coerce_bool_params(spec, typed);
    assert(typed["success"].is_boolean() && typed["success"].get<bool>());
    assert(typed["dry_run"].is_boolean() && !typed["dry_run"].get<bool>());

    // A string that is not "true"/"false" is left alone for the daemon to
    // reject with its own message, rather than being silently coerced.
    json junk{{"success", "yes"}};
    coerce_bool_params(spec, junk);
    assert(junk["success"].is_string());

    // Absent params must not be conjured into existence.
    json empty = json::object();
    coerce_bool_params(spec, empty);
    assert(empty.empty());

    std::printf("tool_spec_test: all assertions passed\n");
    return 0;
}
