#pragma once
// Golden Recall Harness: Regression testing for recall quality
//
// Maintains a canonical query set with expected results.
// Validates seed reconstruction and recall accuracy.
// Integrates with CI for quality metrics.
//
// Use to detect recall degradation before it impacts users.

#include "types.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <cmath>

namespace chitta {

// Expected result for a test query
struct ExpectedResult {
    NodeId id;
    float min_score;      // Minimum acceptable score
    uint32_t max_rank;    // Maximum acceptable rank (1-indexed)
    bool required;        // Must be in results or test fails
};

// A golden test case
struct GoldenTestCase {
    std::string name;              // Test case identifier
    std::string query;             // Query text
    std::vector<std::string> tags; // Optional tag filters
    std::vector<ExpectedResult> expected;
    size_t k = 10;                 // Number of results to check
};

// Test result
struct TestResult {
    std::string test_name;
    bool passed;
    std::string failure_reason;

    // Metrics
    float precision;      // Fraction of returned that are expected
    float recall;         // Fraction of expected that are returned
    float mrr;            // Mean Reciprocal Rank
    float ndcg;           // Normalized Discounted Cumulative Gain

    // Details
    std::vector<NodeId> missing_required;  // Required nodes not found
    std::vector<NodeId> wrong_rank;        // Nodes at wrong rank
};

// Harness statistics
struct HarnessStats {
    size_t total_tests;
    size_t passed;
    size_t failed;
    float avg_precision;
    float avg_recall;
    float avg_mrr;
    float avg_ndcg;
};

// Golden recall harness
class EvalHarness {
public:
    EvalHarness() = default;

    // Add a test case
    void add_test(const GoldenTestCase& test) {
        tests_[test.name] = test;
    }

    // Remove a test case
    void remove_test(const std::string& name) {
        tests_.erase(name);
    }

    // Get all test names
    std::vector<std::string> test_names() const {
        std::vector<std::string> names;
        for (const auto& [name, _] : tests_) {
            names.push_back(name);
        }
        return names;
    }

    // Evaluate a single test case
    // recall_fn: function that performs recall and returns (id, score) pairs
    TestResult evaluate(
        const std::string& test_name,
        const std::function<std::vector<std::pair<NodeId, float>>(
            const std::string& query, const std::vector<std::string>& tags, size_t k)>& recall_fn) const
    {
        TestResult result;
        result.test_name = test_name;
        result.passed = true;

        auto it = tests_.find(test_name);
        if (it == tests_.end()) {
            result.passed = false;
            result.failure_reason = "Test not found: " + test_name;
            return result;
        }

        const auto& test = it->second;

        // Run recall
        auto results = recall_fn(test.query, test.tags, test.k);

        // Build result set for quick lookup
        std::unordered_map<NodeId, size_t, NodeIdHash> result_ranks;
        std::unordered_map<NodeId, float, NodeIdHash> result_scores;
        for (size_t i = 0; i < results.size(); ++i) {
            result_ranks[results[i].first] = i + 1;  // 1-indexed rank
            result_scores[results[i].first] = results[i].second;
        }

        // Check expected results
        size_t found_expected = 0;
        size_t total_expected = test.expected.size();
        float reciprocal_rank_sum = 0.0f;
        float dcg = 0.0f;
        float ideal_dcg = 0.0f;

        for (size_t i = 0; i < test.expected.size(); ++i) {
            const auto& exp = test.expected[i];

            // Ideal DCG (assuming expected are in order of relevance)
            float relevance = 1.0f / (i + 1);  // Decreasing relevance
            ideal_dcg += relevance / std::log2(i + 2);

            auto rank_it = result_ranks.find(exp.id);
            if (rank_it == result_ranks.end()) {
                // Not found in results
                if (exp.required) {
                    result.passed = false;
                    result.missing_required.push_back(exp.id);
                }
                continue;
            }

            size_t rank = rank_it->second;
            float score = result_scores[exp.id];

            // Check rank
            if (rank > exp.max_rank) {
                result.wrong_rank.push_back(exp.id);
                if (exp.required) {
                    result.passed = false;
                }
            }

            // Check score
            if (score < exp.min_score) {
                if (exp.required) {
                    result.passed = false;
                }
            }

            found_expected++;
            reciprocal_rank_sum += 1.0f / rank;
            dcg += relevance / std::log2(rank + 1);
        }

        // Calculate metrics
        result.recall = total_expected > 0 ?
            static_cast<float>(found_expected) / total_expected : 1.0f;

        size_t relevant_returned = 0;
        std::unordered_set<NodeId, NodeIdHash> expected_set;
        for (const auto& exp : test.expected) {
            expected_set.insert(exp.id);
        }
        for (const auto& [id, _] : results) {
            if (expected_set.count(id)) relevant_returned++;
        }
        result.precision = results.empty() ? 0.0f :
            static_cast<float>(relevant_returned) / results.size();

        result.mrr = total_expected > 0 ?
            reciprocal_rank_sum / total_expected : 0.0f;

        result.ndcg = ideal_dcg > 0 ? dcg / ideal_dcg : 0.0f;

        if (!result.passed && result.failure_reason.empty()) {
            std::ostringstream oss;
            oss << "Missing required: " << result.missing_required.size()
                << ", Wrong rank: " << result.wrong_rank.size();
            result.failure_reason = oss.str();
        }

        return result;
    }

    // Run all tests
    std::vector<TestResult> run_all(
        const std::function<std::vector<std::pair<NodeId, float>>(
            const std::string& query, const std::vector<std::string>& tags, size_t k)>& recall_fn) const
    {
        std::vector<TestResult> results;
        for (const auto& [name, _] : tests_) {
            results.push_back(evaluate(name, recall_fn));
        }
        return results;
    }

    // Get aggregate statistics
    HarnessStats get_stats(const std::vector<TestResult>& results) const {
        HarnessStats stats{};
        stats.total_tests = results.size();

        if (results.empty()) return stats;

        for (const auto& r : results) {
            if (r.passed) stats.passed++;
            else stats.failed++;

            stats.avg_precision += r.precision;
            stats.avg_recall += r.recall;
            stats.avg_mrr += r.mrr;
            stats.avg_ndcg += r.ndcg;
        }

        stats.avg_precision /= results.size();
        stats.avg_recall /= results.size();
        stats.avg_mrr /= results.size();
        stats.avg_ndcg /= results.size();

        return stats;
    }

    // Load test cases from file
    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f) return false;

        tests_.clear();
        std::string line;
        GoldenTestCase current;

        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;

            if (line.substr(0, 5) == "TEST:") {
                if (!current.name.empty()) {
                    tests_[current.name] = current;
                }
                current = GoldenTestCase{};
                current.name = line.substr(5);
            } else if (line.substr(0, 6) == "QUERY:") {
                current.query = line.substr(6);
            } else if (line.substr(0, 5) == "TAGS:") {
                std::istringstream iss(line.substr(5));
                std::string tag;
                while (iss >> tag) {
                    current.tags.push_back(tag);
                }
            } else if (line.substr(0, 2) == "K:") {
                current.k = std::stoul(line.substr(2));
            } else if (line.substr(0, 7) == "EXPECT:") {
                // Format: EXPECT:id_high:id_low:min_score:max_rank:required
                std::istringstream iss(line.substr(7));
                ExpectedResult exp;
                char delim;
                int required;
                iss >> exp.id.high >> delim >> exp.id.low >> delim
                    >> exp.min_score >> delim >> exp.max_rank >> delim >> required;
                exp.required = (required != 0);
                current.expected.push_back(exp);
            }
        }

        if (!current.name.empty()) {
            tests_[current.name] = current;
        }

        return true;
    }

    // Save test cases to file
    bool save(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return false;

        f << "# Golden Recall Test Suite\n";
        f << "# Format: TEST:name, QUERY:text, TAGS:tag1 tag2, K:num, EXPECT:id_high:id_low:min_score:max_rank:required\n\n";

        for (const auto& [name, test] : tests_) {
            f << "TEST:" << test.name << "\n";
            f << "QUERY:" << test.query << "\n";
            if (!test.tags.empty()) {
                f << "TAGS:";
                for (const auto& tag : test.tags) {
                    f << tag << " ";
                }
                f << "\n";
            }
            f << "K:" << test.k << "\n";
            for (const auto& exp : test.expected) {
                f << "EXPECT:" << exp.id.high << ":" << exp.id.low << ":"
                  << exp.min_score << ":" << exp.max_rank << ":"
                  << (exp.required ? 1 : 0) << "\n";
            }
            f << "\n";
        }

        return true;
    }

    size_t test_count() const { return tests_.size(); }

private:
    std::unordered_map<std::string, GoldenTestCase> tests_;
};

} // namespace chitta
