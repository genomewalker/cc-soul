#pragma once

#include <string>
#include <unordered_map>
#include <fstream>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace chitta {

// Signal statistics for feedback learning
struct SignalStats {
    size_t approved = 0;
    size_t rejected = 0;

    float learned_confidence() const {
        size_t total = approved + rejected;
        if (total < 10) return 0.5f;  // Not enough data
        float rate = static_cast<float>(approved) / total;
        return 0.3f + 0.5f * rate;  // Range: 0.3 to 0.8
    }
};

// Global signal stats (singleton-like)
inline std::unordered_map<std::string, SignalStats>& signal_stats() {
    static std::unordered_map<std::string, SignalStats> stats;
    static bool loaded = false;

    if (!loaded) {
        loaded = true;
        const char* home = std::getenv("HOME");
        if (home) {
            std::string path = std::string(home) + "/.claude/mind/.signal_stats.json";
            std::ifstream f(path);
            if (f.is_open()) {
                try {
                    nlohmann::json j;
                    f >> j;
                    for (auto& [key, val] : j.items()) {
                        stats[key].approved = val.value("approved", 0);
                        stats[key].rejected = val.value("rejected", 0);
                    }
                } catch (...) {}
            }
        }
    }
    return stats;
}

inline void save_signal_stats() {
    const char* home = std::getenv("HOME");
    if (!home) return;

    std::string path = std::string(home) + "/.claude/mind/.signal_stats.json";
    nlohmann::json j;
    for (const auto& [key, stats] : signal_stats()) {
        j[key] = {{"approved", stats.approved}, {"rejected", stats.rejected}};
    }

    std::ofstream f(path);
    if (f.is_open()) {
        f << j.dump(2);
    }
}

inline void update_signal_stats(const std::string& signal_key, bool approved) {
    auto& stats = signal_stats();
    if (approved) {
        stats[signal_key].approved++;
    } else {
        stats[signal_key].rejected++;
    }
    save_signal_stats();
}

inline float get_signal_confidence(const std::string& signal_key, float default_conf) {
    auto& stats = signal_stats();
    auto it = stats.find(signal_key);
    if (it == stats.end()) return default_conf;
    float learned = it->second.learned_confidence();
    size_t total = it->second.approved + it->second.rejected;
    if (total < 10) return default_conf;
    return 0.8f * learned + 0.2f * default_conf;
}

}  // namespace chitta
