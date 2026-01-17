#pragma once
// HierarchicalState: Token-efficient context injection
//
// Three-level compression inspired by state-space models (Mamba/RWKV):
// - Level 0: ProjectEssence (~50 tokens) - always injected
// - Level 1: ModuleState (~20 tokens each) - relevant modules injected
// - Level 2: PatternState (~10 tokens each) - on-demand expansion
// - Level 3: Raw facts - only for deep dives (from Mind recall)
//
// Key insight: text-first, structured-core. Store what Claude needs
// to inject directly, not what needs transformation.

#include "types.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace chitta {

// Level 0: Project essence (~50 tokens)
// Always injected at session start
struct ProjectEssence {
    std::string thesis;              // "chitta: memory substrate for Claude..."
    std::vector<std::string> core_modules;  // ["Mind", "Storage", "WAL"]
    std::string current_focus;       // What we're working on now
    float tau = 0.0f;                // Global coherence
    float psi = 0.0f;                // Global vitality (ojas)
    uint64_t updated_at = 0;         // Last update timestamp
    std::string rendered;            // Pre-computed injection text (~50 tokens)

    // Render to injection-ready text
    void render() {
        rendered = "Project: " + thesis + "\n";
        rendered += "Modules: ";
        for (size_t i = 0; i < core_modules.size(); ++i) {
            if (i > 0) rendered += ", ";
            rendered += core_modules[i];
        }
        rendered += "\n";
        if (!current_focus.empty()) {
            rendered += "Focus: " + current_focus + "\n";
        }
        rendered += "State: τ=" + std::to_string(static_cast<int>(tau * 100)) + "% "
                  + "ψ=" + std::to_string(static_cast<int>(psi * 100)) + "%";
    }
};

// Level 1: Module state (~20 tokens each)
// Injected based on relevance to current query
struct ModuleState {
    std::string name;                // Short name: "Mind"
    std::string ns;                  // Namespace: "chitta" (for disambiguation)
    std::string summary;             // "orchestrator: recall/resonate/remember"
    std::vector<std::string> entrypoints;  // Key functions: ["recall", "observe"]
    std::vector<std::string> files;  // Source files
    std::vector<std::string> depends_on;  // Other modules this uses
    float importance = 0.5f;         // How central to the project
    float staleness = 0.0f;          // 0=fresh, 1=completely stale
    uint64_t updated_at = 0;
    std::string rendered;            // Pre-computed injection text (~20 tokens)

    // Render to injection-ready text
    void render() {
        rendered = name + ": " + summary;
        if (!entrypoints.empty()) {
            rendered += " [";
            for (size_t i = 0; i < entrypoints.size() && i < 3; ++i) {
                if (i > 0) rendered += ", ";
                rendered += entrypoints[i];
            }
            if (entrypoints.size() > 3) rendered += "...";
            rendered += "]";
        }
    }
};

// Level 2: Active pattern (~10 tokens each)
// Expanded on-demand during recall
struct PatternState {
    std::string seed;                // SSL: "recall→yantra→spreading_activation"
    std::vector<std::string> modules;  // Related modules
    std::string anchor;              // File:line reference "@mind.hpp:450"
    float importance = 0.5f;         // Access frequency
    float confidence = 0.8f;         // From underlying node kappa
    uint64_t accessed_at = 0;        // Last access timestamp
};

// Token budget configuration
struct InjectionBudget {
    size_t level0_tokens = 50;       // ProjectEssence: always injected
    size_t level1_tokens = 200;      // ModuleState: ~10 modules max
    size_t level2_tokens = 100;      // PatternState: ~10 patterns max
    size_t total_max = 500;          // Hard cap on injection

    // How many Level 1 modules to inject based on relevance
    size_t max_modules = 5;
    float module_relevance_threshold = 0.3f;

    // How many Level 2 patterns to inject
    size_t max_patterns = 5;
    float pattern_relevance_threshold = 0.4f;
};

// Hierarchical state manager
class HierarchicalState {
public:
    // Get/set project essence
    ProjectEssence& essence() { return essence_; }
    const ProjectEssence& essence() const { return essence_; }

    // Module management
    void add_module(const std::string& name, ModuleState state) {
        state.render();
        modules_[name] = std::move(state);
    }

    ModuleState* get_module(const std::string& name) {
        auto it = modules_.find(name);
        return it != modules_.end() ? &it->second : nullptr;
    }

    const std::unordered_map<std::string, ModuleState>& modules() const {
        return modules_;
    }

    // Pattern management
    void add_pattern(const std::string& seed, PatternState state) {
        patterns_[seed] = std::move(state);
    }

    PatternState* get_pattern(const std::string& seed) {
        auto it = patterns_.find(seed);
        return it != patterns_.end() ? &it->second : nullptr;
    }

    // Generate injection context based on query relevance
    // Returns token-budgeted text ready for context injection
    std::string generate_injection(
        const std::vector<std::string>& relevant_modules,
        const std::vector<std::string>& relevant_patterns,
        const InjectionBudget& budget = InjectionBudget()) const
    {
        std::string result;

        // Level 0: Always include essence
        if (!essence_.rendered.empty()) {
            result += essence_.rendered + "\n\n";
        }

        // Level 1: Include relevant modules up to budget
        size_t module_count = 0;
        for (const auto& name : relevant_modules) {
            if (module_count >= budget.max_modules) break;
            auto it = modules_.find(name);
            if (it != modules_.end() && !it->second.rendered.empty()) {
                result += it->second.rendered + "\n";
                module_count++;
            }
        }

        // Level 2: Include relevant patterns
        if (!relevant_patterns.empty() && module_count > 0) {
            result += "\nPatterns:\n";
            size_t pattern_count = 0;
            for (const auto& seed : relevant_patterns) {
                if (pattern_count >= budget.max_patterns) break;
                auto it = patterns_.find(seed);
                if (it != patterns_.end()) {
                    result += "  " + it->second.seed;
                    if (!it->second.anchor.empty()) {
                        result += " " + it->second.anchor;
                    }
                    result += "\n";
                    pattern_count++;
                }
            }
        }

        return result;
    }

    // Bootstrap from code intelligence (tree-sitter symbols)
    // Infers module boundaries from classes and namespaces
    void bootstrap_from_symbols(const std::string& project_name,
                                const std::vector<std::pair<std::string, std::string>>& class_files)
    {
        essence_.thesis = project_name;
        essence_.core_modules.clear();
        modules_.clear();

        for (const auto& [class_name, file_path] : class_files) {
            // Create module for each major class
            ModuleState mod;
            mod.name = class_name;
            mod.files.push_back(file_path);
            mod.importance = 0.5f;
            mod.updated_at = now();
            mod.render();

            modules_[class_name] = std::move(mod);
            essence_.core_modules.push_back(class_name);
        }

        essence_.updated_at = now();
        essence_.render();
    }

    // Mark modules as potentially stale when files change
    void mark_files_stale(const std::vector<std::string>& changed_files) {
        for (auto& [name, mod] : modules_) {
            for (const auto& file : mod.files) {
                for (const auto& changed : changed_files) {
                    if (file == changed || file.find(changed) != std::string::npos) {
                        mod.staleness = std::min(mod.staleness + 0.3f, 1.0f);
                        break;
                    }
                }
            }
        }
    }

    // Update metrics from Mind state
    void update_metrics(float tau, float psi) {
        essence_.tau = tau;
        essence_.psi = psi;
        essence_.updated_at = now();
        essence_.render();
    }

private:
    ProjectEssence essence_;
    std::unordered_map<std::string, ModuleState> modules_;
    std::unordered_map<std::string, PatternState> patterns_;
};

} // namespace chitta
