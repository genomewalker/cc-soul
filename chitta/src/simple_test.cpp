// Simple test for SimpleMind
// Build: g++ -std=c++17 -I../include -o simple_test simple_test.cpp

#include "chitta/mind/simple_mind.hpp"
#include "chitta/rpc/simple_handler.hpp"
#include "chitta/vak_onnx.hpp"
#include <iostream>
#include <cassert>
#include <cstdlib>

using namespace chitta;

int main(int argc, char** argv) {
    std::string test_path = "/tmp/simple_mind_test";
    std::string model_path = argc > 1 ? argv[1] : "";
    std::string vocab_path = argc > 2 ? argv[2] : "";

    // Clean up
    std::system(("rm -f " + test_path + ".*").c_str());

    std::cout << "=== SimpleMind Test ===\n\n";

    // Create and open
    SimpleMindConfig config;
    config.path = test_path;
    config.initial_capacity = 1000;

    SimpleMind mind(config);
    if (!mind.open()) {
        std::cerr << "Failed to open SimpleMind\n";
        return 1;
    }
    std::cout << "Opened SimpleMind at " << test_path << "\n";

    // Attach yantra if model provided
    if (!model_path.empty() && !vocab_path.empty()) {
        auto yantra = std::make_shared<AntahkaranaYantra>();
        if (yantra->awaken(model_path, vocab_path)) {
            mind.attach_yantra(yantra);
            std::cout << "Attached Yantra\n";
        } else {
            std::cerr << "Failed to load model\n";
        }
    }

    // Test remember
    if (mind.has_yantra()) {
        std::cout << "\n--- Testing remember ---\n";

        auto id1 = mind.remember("The quick brown fox jumps over the lazy dog", NodeType::Episode);
        std::cout << "Remembered: " << id1.to_string() << "\n";

        auto id2 = mind.remember("Machine learning models can understand natural language", NodeType::Wisdom);
        std::cout << "Remembered: " << id2.to_string() << "\n";

        auto id3 = mind.remember("Ancient DNA reveals human migration patterns", NodeType::Wisdom, std::vector<std::string>{"biology", "history"});
        std::cout << "Remembered with tags: " << id3.to_string() << "\n";

        // Test recall
        std::cout << "\n--- Testing recall ---\n";
        std::vector<Recall> results = mind.recall("natural language understanding", 5);
        std::cout << "Found " << results.size() << " results for 'natural language understanding':\n";
        for (const auto& r : results) {
            std::cout << "  [" << int(r.relevance * 100) << "%] " << r.text.substr(0, 60) << "\n";
        }

        // Test triplets
        std::cout << "\n--- Testing triplets ---\n";
        mind.connect("Claude", "is_a", "AI_assistant");
        mind.connect("Claude", "created_by", "Anthropic");
        mind.connect("SimpleMind", "is_a", "memory_system");
        mind.graph().sync();

        auto triplets = mind.query_subject("Claude");
        std::cout << "Triplets for 'Claude':\n";
        for (const auto& [pred, obj, weight] : triplets) {
            std::cout << "  -> " << pred << " -> " << obj << "\n";
        }
    } else {
        std::cout << "No yantra attached, skipping embedding tests\n";
    }

    // Test RPC handler
    std::cout << "\n--- Testing RPC Handler ---\n";
    SimpleRpcHandler handler(&mind);

    // tools/list
    nlohmann::json list_req = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "tools/list"},
        {"params", nlohmann::json::object()}
    };
    auto list_resp = handler.handle(list_req);
    std::cout << "tools/list returned " << list_resp["result"]["tools"].size() << " tools\n";

    // soul_context
    nlohmann::json ctx_req = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/call"},
        {"params", {{"name", "soul_context"}, {"arguments", nlohmann::json::object()}}}
    };
    auto ctx_resp = handler.handle(ctx_req);
    std::cout << "soul_context: " << ctx_resp["result"]["content"][0]["text"].get<std::string>() << "\n";

    // Cleanup
    mind.close();
    std::cout << "\n=== Test Complete ===\n";

    return 0;
}
