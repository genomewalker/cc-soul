#pragma once
// Provenance Spine: Trust and debuggability at scale
//
// Tracks metadata for every node:
// - Source: where did this knowledge come from?
// - Session: which conversation created it?
// - Tool: was it from a tool call, user input, or synthesis?
// - User: optional user identifier
// - Timestamp: when was it created?
//
// Enables trust filtering at recall time and debugging knowledge provenance.

#include "types.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace chitta {

// Source of knowledge
enum class ProvenanceSource : uint8_t {
    Unknown = 0,
    UserInput = 1,        // Direct user statement
    ToolOutput = 2,       // From tool call result
    WebFetch = 3,         // From web content
    FileRead = 4,         // From reading a file
    Synthesis = 5,        // LLM-generated synthesis
    Inference = 6,        // Inferred from other knowledge
    Import = 7,           // Bulk imported
    Migration = 8,        // From format migration
};

// Provenance record for a node
struct Provenance {
    ProvenanceSource source = ProvenanceSource::Unknown;
    std::string session_id;       // Session that created this
    std::string tool_name;        // Tool that generated this (if applicable)
    std::string user_id;          // User identifier (optional)
    std::string source_url;       // URL if from web/file
    Timestamp created_at = 0;     // When this provenance was recorded
    NodeId derived_from;          // Parent node if synthesized/inferred
    float trust_score = 0.5f;     // Base trust level (0-1)

    // Serialize to compact format
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;

        // Source byte
        data.push_back(static_cast<uint8_t>(source));

        // Trust score (4 bytes)
        auto* trust_ptr = reinterpret_cast<const uint8_t*>(&trust_score);
        data.insert(data.end(), trust_ptr, trust_ptr + sizeof(trust_score));

        // Created timestamp (8 bytes)
        auto* ts_ptr = reinterpret_cast<const uint8_t*>(&created_at);
        data.insert(data.end(), ts_ptr, ts_ptr + sizeof(created_at));

        // Derived from (16 bytes)
        auto* df_ptr = reinterpret_cast<const uint8_t*>(&derived_from);
        data.insert(data.end(), df_ptr, df_ptr + sizeof(derived_from));

        // Variable-length strings (length-prefixed)
        auto write_string = [&data](const std::string& s) {
            uint16_t len = static_cast<uint16_t>(std::min(s.size(), size_t(65535)));
            data.push_back(len & 0xFF);
            data.push_back((len >> 8) & 0xFF);
            data.insert(data.end(), s.begin(), s.begin() + len);
        };

        write_string(session_id);
        write_string(tool_name);
        write_string(user_id);
        write_string(source_url);

        return data;
    }

    // Deserialize from compact format
    static Provenance deserialize(const uint8_t* data, size_t size) {
        Provenance p;
        if (size < 29) return p;  // Minimum size

        size_t pos = 0;

        p.source = static_cast<ProvenanceSource>(data[pos++]);

        std::memcpy(&p.trust_score, data + pos, sizeof(p.trust_score));
        pos += sizeof(p.trust_score);

        std::memcpy(&p.created_at, data + pos, sizeof(p.created_at));
        pos += sizeof(p.created_at);

        std::memcpy(&p.derived_from, data + pos, sizeof(p.derived_from));
        pos += sizeof(p.derived_from);

        auto read_string = [&data, &pos, size]() -> std::string {
            if (pos + 2 > size) return "";
            uint16_t len = data[pos] | (data[pos + 1] << 8);
            pos += 2;
            if (pos + len > size) return "";
            std::string s(reinterpret_cast<const char*>(data + pos), len);
            pos += len;
            return s;
        };

        p.session_id = read_string();
        p.tool_name = read_string();
        p.user_id = read_string();
        p.source_url = read_string();

        return p;
    }
};

// Trust configuration for provenance filtering
struct TrustConfig {
    float min_trust = 0.0f;           // Minimum trust to include in recall
    bool require_user_input = false;  // Only include user-provided knowledge
    bool exclude_synthesis = false;   // Exclude LLM-synthesized knowledge
    bool require_source_url = false;  // Only include knowledge with source URL
    std::vector<std::string> allowed_tools;  // Empty = all tools allowed
    std::vector<std::string> allowed_sessions;  // Empty = all sessions
};

// Provenance spine - manages provenance for all nodes
class ProvenanceSpine {
public:
    ProvenanceSpine() = default;

    // Record provenance for a node
    void record(const NodeId& id, const Provenance& prov) {
        provenance_[id] = prov;
    }

    // Get provenance for a node
    const Provenance* get(const NodeId& id) const {
        auto it = provenance_.find(id);
        return (it != provenance_.end()) ? &it->second : nullptr;
    }

    // Check if node passes trust filter
    bool passes_trust_filter(const NodeId& id, const TrustConfig& config) const {
        auto it = provenance_.find(id);
        if (it == provenance_.end()) {
            // No provenance = unknown trust, pass only if min_trust is 0
            return config.min_trust <= 0.0f;
        }

        const auto& prov = it->second;

        // Check trust score
        if (prov.trust_score < config.min_trust) return false;

        // Check user input requirement
        if (config.require_user_input && prov.source != ProvenanceSource::UserInput) {
            return false;
        }

        // Check synthesis exclusion
        if (config.exclude_synthesis &&
            (prov.source == ProvenanceSource::Synthesis ||
             prov.source == ProvenanceSource::Inference)) {
            return false;
        }

        // Check source URL requirement
        if (config.require_source_url && prov.source_url.empty()) {
            return false;
        }

        // Check tool whitelist
        if (!config.allowed_tools.empty() && !prov.tool_name.empty()) {
            bool found = false;
            for (const auto& t : config.allowed_tools) {
                if (t == prov.tool_name) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }

        // Check session whitelist
        if (!config.allowed_sessions.empty() && !prov.session_id.empty()) {
            bool found = false;
            for (const auto& s : config.allowed_sessions) {
                if (s == prov.session_id) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }

        return true;
    }

    // Filter a list of node IDs by trust
    std::vector<NodeId> filter_by_trust(
        const std::vector<NodeId>& ids,
        const TrustConfig& config) const
    {
        std::vector<NodeId> result;
        for (const auto& id : ids) {
            if (passes_trust_filter(id, config)) {
                result.push_back(id);
            }
        }
        return result;
    }

    // Calculate effective trust for scoring
    // Combines provenance trust with confidence
    float effective_trust(const NodeId& id, float confidence) const {
        auto it = provenance_.find(id);
        float prov_trust = (it != provenance_.end()) ? it->second.trust_score : 0.5f;
        return prov_trust * confidence;
    }

    // Update trust score based on feedback
    void update_trust(const NodeId& id, float delta) {
        auto it = provenance_.find(id);
        if (it != provenance_.end()) {
            it->second.trust_score = std::clamp(
                it->second.trust_score + delta, 0.0f, 1.0f);
        }
    }

    // Remove provenance for deleted node
    void remove(const NodeId& id) {
        provenance_.erase(id);
    }

    // Clear all provenance
    void clear() {
        provenance_.clear();
    }

    // Statistics
    size_t count() const { return provenance_.size(); }

    // Source name for debugging
    static std::string source_name(ProvenanceSource source) {
        switch (source) {
            case ProvenanceSource::Unknown: return "unknown";
            case ProvenanceSource::UserInput: return "user_input";
            case ProvenanceSource::ToolOutput: return "tool_output";
            case ProvenanceSource::WebFetch: return "web_fetch";
            case ProvenanceSource::FileRead: return "file_read";
            case ProvenanceSource::Synthesis: return "synthesis";
            case ProvenanceSource::Inference: return "inference";
            case ProvenanceSource::Import: return "import";
            case ProvenanceSource::Migration: return "migration";
            default: return "unknown";
        }
    }

    // Persistence
    bool save(const std::string& path) const {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) return false;

        uint32_t magic = 0x50524F56;  // "PROV"
        uint32_t version = 1;
        uint64_t count = provenance_.size();

        fwrite(&magic, sizeof(magic), 1, f);
        fwrite(&version, sizeof(version), 1, f);
        fwrite(&count, sizeof(count), 1, f);

        for (const auto& [id, prov] : provenance_) {
            fwrite(&id.high, sizeof(id.high), 1, f);
            fwrite(&id.low, sizeof(id.low), 1, f);

            auto data = prov.serialize();
            uint32_t data_size = static_cast<uint32_t>(data.size());
            fwrite(&data_size, sizeof(data_size), 1, f);
            fwrite(data.data(), 1, data_size, f);
        }

        fclose(f);
        return true;
    }

    bool load(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;

        uint32_t magic, version;
        uint64_t count;

        if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x50524F56 ||
            fread(&version, sizeof(version), 1, f) != 1 || version != 1 ||
            fread(&count, sizeof(count), 1, f) != 1 || count > 100000000) {
            fclose(f);
            return false;
        }

        provenance_.clear();
        for (uint64_t i = 0; i < count; ++i) {
            NodeId id;
            if (fread(&id.high, sizeof(id.high), 1, f) != 1 ||
                fread(&id.low, sizeof(id.low), 1, f) != 1) {
                fclose(f);
                return false;
            }

            uint32_t data_size;
            if (fread(&data_size, sizeof(data_size), 1, f) != 1 || data_size > 10000) {
                fclose(f);
                return false;
            }

            std::vector<uint8_t> data(data_size);
            if (fread(data.data(), 1, data_size, f) != data_size) {
                fclose(f);
                return false;
            }

            provenance_[id] = Provenance::deserialize(data.data(), data_size);
        }

        fclose(f);
        return true;
    }

private:
    std::unordered_map<NodeId, Provenance, NodeIdHash> provenance_;
};

} // namespace chitta
