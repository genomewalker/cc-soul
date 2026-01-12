#pragma once
// Realm Scoping Graph: Project/user isolation
//
// Nodes can be scoped to realms (projects, users, contexts).
// Recall is gated by current realm - only see relevant knowledge.
// Cross-realm transfer policies control knowledge sharing.
//
// Use cases:
// - Per-project memory isolation
// - User-specific customizations
// - Context switching without pollution

#include "types.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chitta {

// Realm identifier
struct RealmId {
    std::string name;           // e.g., "project:cc-soul", "user:alice"
    std::string parent;         // Parent realm (for hierarchy), empty = root

    bool operator==(const RealmId& other) const {
        return name == other.name;
    }

    bool is_root() const { return parent.empty(); }
};

struct RealmIdHash {
    size_t operator()(const RealmId& r) const {
        return std::hash<std::string>{}(r.name);
    }
};

// Realm visibility rules
enum class RealmVisibility : uint8_t {
    Private = 0,      // Only visible in owning realm
    Inherited = 1,    // Visible in realm and child realms
    Shared = 2,       // Visible in specified peer realms
    Global = 3,       // Visible everywhere (Brahman)
};

// Realm membership for a node
struct RealmMembership {
    RealmId primary_realm;                    // Main realm this node belongs to
    RealmVisibility visibility = RealmVisibility::Private;
    std::vector<RealmId> shared_with;         // For Shared visibility
    Timestamp scoped_at = 0;                  // When scope was assigned
};

// Transfer policy between realms
struct TransferPolicy {
    RealmId from_realm;
    RealmId to_realm;
    bool allowed = false;
    bool requires_approval = true;
    float min_confidence = 0.7f;     // Minimum confidence to transfer
    std::vector<NodeType> allowed_types;  // Empty = all types
};

// Realm scoping manager
class RealmScoping {
public:
    RealmScoping() {
        // Create root realm (Brahman - universal)
        RealmId root;
        root.name = "brahman";
        realms_[root.name] = root;
        current_realm_ = root;
    }

    // Create a new realm
    void create_realm(const std::string& name, const std::string& parent = "") {
        RealmId realm;
        realm.name = name;
        realm.parent = parent.empty() ? "brahman" : parent;
        realms_[name] = realm;
    }

    // Set current realm (for recall gating)
    void set_current_realm(const std::string& name) {
        auto it = realms_.find(name);
        if (it != realms_.end()) {
            current_realm_ = it->second;
        }
    }

    const RealmId& current_realm() const { return current_realm_; }

    // Assign node to a realm
    void assign(const NodeId& node, const RealmId& realm,
               RealmVisibility visibility = RealmVisibility::Private,
               Timestamp now = 0) {
        RealmMembership m;
        m.primary_realm = realm;
        m.visibility = visibility;
        m.scoped_at = now;
        memberships_[node] = m;

        // Track nodes per realm
        realm_nodes_[realm.name].insert(node);
    }

    // Share node with additional realms
    void share_with(const NodeId& node, const std::vector<RealmId>& realms) {
        auto it = memberships_.find(node);
        if (it != memberships_.end()) {
            it->second.visibility = RealmVisibility::Shared;
            it->second.shared_with = realms;
        }
    }

    // Make node global
    void make_global(const NodeId& node) {
        auto it = memberships_.find(node);
        if (it != memberships_.end()) {
            it->second.visibility = RealmVisibility::Global;
        }
    }

    // Check if node is visible in current realm
    bool is_visible(const NodeId& node) const {
        return is_visible_in(node, current_realm_);
    }

    bool is_visible_in(const NodeId& node, const RealmId& realm) const {
        auto it = memberships_.find(node);
        if (it == memberships_.end()) {
            // No membership = global (Brahman default)
            return true;
        }

        const auto& m = it->second;

        switch (m.visibility) {
            case RealmVisibility::Global:
                return true;

            case RealmVisibility::Private:
                return m.primary_realm.name == realm.name;

            case RealmVisibility::Inherited:
                return is_ancestor_or_same(m.primary_realm, realm);

            case RealmVisibility::Shared:
                if (m.primary_realm.name == realm.name) return true;
                for (const auto& shared : m.shared_with) {
                    if (shared.name == realm.name) return true;
                }
                return false;

            default:
                return false;
        }
    }

    // Filter recall results by current realm
    std::vector<NodeId> filter_by_realm(const std::vector<NodeId>& nodes) const {
        std::vector<NodeId> result;
        for (const auto& node : nodes) {
            if (is_visible(node)) {
                result.push_back(node);
            }
        }
        return result;
    }

    // Filter with scores preserved
    std::vector<std::pair<NodeId, float>> filter_by_realm(
        const std::vector<std::pair<NodeId, float>>& results) const
    {
        std::vector<std::pair<NodeId, float>> filtered;
        for (const auto& [node, score] : results) {
            if (is_visible(node)) {
                filtered.push_back({node, score});
            }
        }
        return filtered;
    }

    // Get all nodes in a realm
    std::vector<NodeId> get_realm_nodes(const std::string& realm_name) const {
        auto it = realm_nodes_.find(realm_name);
        if (it == realm_nodes_.end()) return {};
        return std::vector<NodeId>(it->second.begin(), it->second.end());
    }

    // Transfer node to another realm
    bool transfer(const NodeId& node, const RealmId& to_realm,
                 float node_confidence = 1.0f) {
        auto it = memberships_.find(node);
        if (it == memberships_.end()) return true;  // Global, no transfer needed

        const auto& from_realm = it->second.primary_realm;

        // Check transfer policy
        if (!can_transfer(from_realm, to_realm, node_confidence)) {
            return false;
        }

        // Remove from old realm tracking
        auto rit = realm_nodes_.find(from_realm.name);
        if (rit != realm_nodes_.end()) {
            rit->second.erase(node);
        }

        // Update membership
        it->second.primary_realm = to_realm;
        realm_nodes_[to_realm.name].insert(node);

        return true;
    }

    // Set transfer policy
    void set_transfer_policy(const TransferPolicy& policy) {
        auto key = policy.from_realm.name + "->" + policy.to_realm.name;
        transfer_policies_[key] = policy;
    }

    // Remove node from realm tracking
    void remove_node(const NodeId& node) {
        auto it = memberships_.find(node);
        if (it != memberships_.end()) {
            auto rit = realm_nodes_.find(it->second.primary_realm.name);
            if (rit != realm_nodes_.end()) {
                rit->second.erase(node);
            }
            memberships_.erase(it);
        }
    }

    // Get membership info
    const RealmMembership* get_membership(const NodeId& node) const {
        auto it = memberships_.find(node);
        return (it != memberships_.end()) ? &it->second : nullptr;
    }

    // Statistics
    size_t realm_count() const { return realms_.size(); }
    size_t scoped_node_count() const { return memberships_.size(); }

    // Persistence
    bool save(const std::string& path) const {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) return false;

        uint32_t magic = 0x52454C4D;  // "RELM"
        uint32_t version = 1;

        fwrite(&magic, sizeof(magic), 1, f);
        fwrite(&version, sizeof(version), 1, f);

        // Save current realm
        uint16_t cur_len = static_cast<uint16_t>(current_realm_.name.size());
        fwrite(&cur_len, sizeof(cur_len), 1, f);
        fwrite(current_realm_.name.data(), 1, cur_len, f);

        // Save realms
        uint32_t realm_count = static_cast<uint32_t>(realms_.size());
        fwrite(&realm_count, sizeof(realm_count), 1, f);
        for (const auto& [name, realm] : realms_) {
            uint16_t name_len = static_cast<uint16_t>(name.size());
            uint16_t parent_len = static_cast<uint16_t>(realm.parent.size());
            fwrite(&name_len, sizeof(name_len), 1, f);
            fwrite(name.data(), 1, name_len, f);
            fwrite(&parent_len, sizeof(parent_len), 1, f);
            fwrite(realm.parent.data(), 1, parent_len, f);
        }

        // Save memberships
        uint64_t member_count = memberships_.size();
        fwrite(&member_count, sizeof(member_count), 1, f);
        for (const auto& [node, m] : memberships_) {
            fwrite(&node.high, sizeof(node.high), 1, f);
            fwrite(&node.low, sizeof(node.low), 1, f);

            uint16_t realm_len = static_cast<uint16_t>(m.primary_realm.name.size());
            fwrite(&realm_len, sizeof(realm_len), 1, f);
            fwrite(m.primary_realm.name.data(), 1, realm_len, f);
            fwrite(&m.visibility, sizeof(m.visibility), 1, f);
            fwrite(&m.scoped_at, sizeof(m.scoped_at), 1, f);

            uint16_t shared_count = static_cast<uint16_t>(m.shared_with.size());
            fwrite(&shared_count, sizeof(shared_count), 1, f);
            for (const auto& sr : m.shared_with) {
                uint16_t sr_len = static_cast<uint16_t>(sr.name.size());
                fwrite(&sr_len, sizeof(sr_len), 1, f);
                fwrite(sr.name.data(), 1, sr_len, f);
            }
        }

        fclose(f);
        return true;
    }

    bool load(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;

        uint32_t magic, version;
        if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x52454C4D ||
            fread(&version, sizeof(version), 1, f) != 1 || version != 1) {
            fclose(f);
            return false;
        }

        // Load current realm
        uint16_t cur_len;
        if (fread(&cur_len, sizeof(cur_len), 1, f) != 1 || cur_len > 1000) {
            fclose(f);
            return false;
        }
        current_realm_.name.resize(cur_len);
        if (fread(&current_realm_.name[0], 1, cur_len, f) != cur_len) {
            fclose(f);
            return false;
        }

        // Load realms
        uint32_t realm_count;
        if (fread(&realm_count, sizeof(realm_count), 1, f) != 1 || realm_count > 10000) {
            fclose(f);
            return false;
        }

        realms_.clear();
        for (uint32_t i = 0; i < realm_count; ++i) {
            uint16_t name_len, parent_len;
            if (fread(&name_len, sizeof(name_len), 1, f) != 1 || name_len > 1000) {
                fclose(f);
                return false;
            }

            std::string name(name_len, '\0');
            if (fread(&name[0], 1, name_len, f) != name_len) {
                fclose(f);
                return false;
            }

            if (fread(&parent_len, sizeof(parent_len), 1, f) != 1 || parent_len > 1000) {
                fclose(f);
                return false;
            }

            std::string parent(parent_len, '\0');
            if (fread(&parent[0], 1, parent_len, f) != parent_len) {
                fclose(f);
                return false;
            }

            RealmId realm;
            realm.name = name;
            realm.parent = parent;
            realms_[name] = realm;
        }

        // Load memberships
        uint64_t member_count;
        if (fread(&member_count, sizeof(member_count), 1, f) != 1 || member_count > 100000000) {
            fclose(f);
            return false;
        }

        memberships_.clear();
        realm_nodes_.clear();
        for (uint64_t i = 0; i < member_count; ++i) {
            NodeId node;
            if (fread(&node.high, sizeof(node.high), 1, f) != 1 ||
                fread(&node.low, sizeof(node.low), 1, f) != 1) {
                fclose(f);
                return false;
            }

            RealmMembership m;
            uint16_t realm_len;
            if (fread(&realm_len, sizeof(realm_len), 1, f) != 1 || realm_len > 1000) {
                fclose(f);
                return false;
            }

            m.primary_realm.name.resize(realm_len);
            if (fread(&m.primary_realm.name[0], 1, realm_len, f) != realm_len) {
                fclose(f);
                return false;
            }

            if (fread(&m.visibility, sizeof(m.visibility), 1, f) != 1 ||
                fread(&m.scoped_at, sizeof(m.scoped_at), 1, f) != 1) {
                fclose(f);
                return false;
            }

            uint16_t shared_count;
            if (fread(&shared_count, sizeof(shared_count), 1, f) != 1 || shared_count > 1000) {
                fclose(f);
                return false;
            }

            for (uint16_t j = 0; j < shared_count; ++j) {
                uint16_t sr_len;
                if (fread(&sr_len, sizeof(sr_len), 1, f) != 1 || sr_len > 1000) {
                    fclose(f);
                    return false;
                }
                RealmId sr;
                sr.name.resize(sr_len);
                if (fread(&sr.name[0], 1, sr_len, f) != sr_len) {
                    fclose(f);
                    return false;
                }
                m.shared_with.push_back(sr);
            }

            memberships_[node] = m;
            realm_nodes_[m.primary_realm.name].insert(node);
        }

        fclose(f);
        return true;
    }

private:
    // Check if 'ancestor' is an ancestor of 'descendant' (or same)
    bool is_ancestor_or_same(const RealmId& ancestor, const RealmId& descendant) const {
        if (ancestor.name == descendant.name) return true;

        std::string current = descendant.parent;
        while (!current.empty()) {
            if (current == ancestor.name) return true;
            auto it = realms_.find(current);
            if (it == realms_.end()) break;
            current = it->second.parent;
        }
        return false;
    }

    // Check transfer policy
    bool can_transfer(const RealmId& from, const RealmId& to, float confidence) const {
        auto key = from.name + "->" + to.name;
        auto it = transfer_policies_.find(key);
        if (it == transfer_policies_.end()) {
            // Default: allow if same parent
            return from.parent == to.parent;
        }
        return it->second.allowed && confidence >= it->second.min_confidence;
    }

    std::unordered_map<std::string, RealmId> realms_;
    std::unordered_map<NodeId, RealmMembership, NodeIdHash> memberships_;
    std::unordered_map<std::string, std::unordered_set<NodeId, NodeIdHash>> realm_nodes_;
    std::unordered_map<std::string, TransferPolicy> transfer_policies_;
    RealmId current_realm_;
};

} // namespace chitta
