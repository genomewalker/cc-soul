// Standalone test for the deterministic value-fact extractor.
// Build: g++ -std=c++17 value_fact_extract_test.cpp value_fact_extractor.cpp -o t
#include "../include/chitta/value_fact_extractor.hpp"
#include <cassert>
#include <iostream>
#include <string>

using chitta::extract_value_facts;
using chitta::ValueFact;

static int failures = 0;

static bool has_pair(const std::vector<ValueFact>& fs,
                     const std::string& id, const std::string& val) {
    for (const auto& f : fs)
        if (f.identifier == id && f.value == val) return true;
    return false;
}

static void expect_pair(const std::string& text,
                        const std::string& id, const std::string& val) {
    auto fs = extract_value_facts(text);
    bool ok = has_pair(fs, id, val);
    std::cout << (ok ? "  PASS " : "  FAIL ") << "emit (" << id << ", " << val
              << ")  <= \"" << text << "\"\n";
    if (!ok) {
        ++failures;
        std::cout << "        got: ";
        for (const auto& f : fs) std::cout << "(" << f.identifier << "," << f.value << ") ";
        std::cout << "\n";
    }
}

static void expect_empty(const std::string& text, const std::string& why) {
    auto fs = extract_value_facts(text);
    bool ok = fs.empty();
    std::cout << (ok ? "  PASS " : "  FAIL ") << "skip [" << why << "]  <= \""
              << text << "\"\n";
    if (!ok) {
        ++failures;
        std::cout << "        leaked: ";
        for (const auto& f : fs) std::cout << "(" << f.identifier << "," << f.value << ") ";
        std::cout << "\n";
    }
}

int main() {
    std::cout << "== EMIT: identifier+value pairs from the 43 miss cases ==\n";
    expect_pair("the merge dropped tpm_tissue_old, 4.2 GB reclaimed", "tpm_tissue_old", "4.2 GB");
    expect_pair("welford at stats.py:150", "stats.py", ":150");
    expect_pair("cherry-picked commit e241114 onto main", "commit", "e241114");
    expect_pair("rollback floor is chitta-field v2.1.0", "chitta-field", "v2.1.0");
    expect_pair("moved the daemon to port 7440 for the copy", "port", "7440");
    expect_pair("dedup_threshold defaults to 0.92 in the config", "dedup_threshold", "0.92");
    expect_pair("MAX_TOKENS was bumped to 8192", "MAX_TOKENS", "8192");
    expect_pair("wrote header byte 0x1f then the payload", "byte", "0x1f");
    expect_pair("HNSW ef_search floor set to 128 for recall", "ef_search", "128");
    expect_pair("index purged to 42k leaving junk_ratio 67% overall", "junk_ratio", "67%");

    std::cout << "\n== SKIP: prose / valueless / anchorless ==\n";
    expect_empty("I think we should probably refactor this later", "prose/affect");
    expect_empty("the user prefers boring over clever code", "preference no scalar");
    expect_empty("we reclaimed a lot of space after the merge", "no precise value");
    expect_empty("it finished in about a second, felt fast", "no anchor+no scalar");
    expect_empty("and then 4.2 GB was freed somehow", "anchorless value");

    std::cout << "\n== DEDUP: same (id,value) emitted once per extraction ==\n";
    {
        auto fs = extract_value_facts(
            "port 7440 was chosen\nlater we confirmed port 7440 again");
        int n = 0;
        for (const auto& f : fs) if (f.identifier == "port" && f.value == "7440") ++n;
        bool ok = (n == 1);
        std::cout << (ok ? "  PASS " : "  FAIL ") << "port 7440 appears exactly once (n="
                  << n << ")\n";
        if (!ok) ++failures;
    }

    std::cout << "\n== content format (leads with identifier value) ==\n";
    {
        auto fs = extract_value_facts("welford at stats.py:150");
        if (!fs.empty()) {
            std::cout << "  content = \"" << fs[0].content << "\"\n";
            bool ok = fs[0].content.rfind(fs[0].identifier + " " + fs[0].value, 0) == 0;
            std::cout << (ok ? "  PASS " : "  FAIL ") << "content leads with id+value\n";
            if (!ok) ++failures;
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL PASS" : std::to_string(failures) + " FAILURES")
              << "\n";
    return failures == 0 ? 0 : 1;
}
