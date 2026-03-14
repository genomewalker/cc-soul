// Included into DuckDBHandler class body — not a standalone header

    DuckDBToolResult tool_realm_list() {
        auto realms = mind_->store().list_realms();

        std::ostringstream ss;
        ss << "Known realms (" << realms.size() << "):\n";
        for (const auto& r : realms) {
            ss << "  - " << r << "\n";
        }

        return DuckDBToolResult::ok(ss.str(), {{"realms", realms}, {"count", realms.size()}});
    }

    DuckDBToolResult tool_realm_get(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        auto realms = mind_->store().get_realms(db_id);
        if (realms.empty()) {
            return DuckDBToolResult::ok("Memory not found or has no realms", {{"realms", json::array()}});
        }

        std::ostringstream ss;
        ss << "Memory " << id_str << " belongs to:\n";
        ss << "  Primary: " << realms[0] << "\n";
        if (realms.size() > 1) {
            ss << "  Shared: ";
            for (size_t i = 1; i < realms.size(); ++i) {
                if (i > 1) ss << ", ";
                ss << realms[i];
            }
            ss << "\n";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"primary", realms[0]},
            {"shared", json(std::vector<std::string>(realms.begin() + 1, realms.end()))},
            {"all", realms}
        });
    }

    DuckDBToolResult tool_realm_set(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        std::string realm = params.value("realm", "");

        if (id_str.empty() || realm.empty()) {
            return DuckDBToolResult::error("ID and realm are required");
        }

        bool ok = mind_->store().set_realm(db_id, realm);
        if (!ok) {
            return DuckDBToolResult::error("Failed to set realm");
        }

        return DuckDBToolResult::ok("Set primary realm to '" + realm + "' for memory " + id_str);
    }

    DuckDBToolResult tool_realm_add(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        std::string realm = params.value("realm", "");

        if (id_str.empty() || realm.empty()) {
            return DuckDBToolResult::error("ID and realm are required");
        }

        bool ok = mind_->store().add_to_realm(db_id, realm);
        if (!ok) {
            return DuckDBToolResult::error("Failed to add to realm");
        }

        return DuckDBToolResult::ok("Added memory " + id_str + " to realm '" + realm + "'");
    }

    DuckDBToolResult tool_realm_remove(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        std::string realm = params.value("realm", "");

        if (id_str.empty() || realm.empty()) {
            return DuckDBToolResult::error("ID and realm are required");
        }

        bool ok = mind_->store().remove_from_realm(db_id, realm);
        if (!ok) {
            return DuckDBToolResult::error("Failed to remove from realm");
        }

        return DuckDBToolResult::ok("Removed memory " + id_str + " from realm '" + realm + "'");
    }

    DuckDBToolResult tool_realm_visibility(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        int visibility = params.value("visibility", 0);

        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        if (visibility < 0 || visibility > 2) {
            return DuckDBToolResult::error("Visibility must be 0 (Private), 1 (Shared), or 2 (Global)");
        }

        bool ok = mind_->store().set_visibility(db_id, static_cast<RealmVisibility>(visibility));
        if (!ok) {
            return DuckDBToolResult::error("Failed to set visibility");
        }

        std::string vis_name = visibility == 0 ? "Private" : (visibility == 1 ? "Shared" : "Global");
        return DuckDBToolResult::ok("Set visibility to " + vis_name + " for memory " + id_str);
    }

    DuckDBToolResult tool_realm_detect() {
        // Detect realm from environment/git/config
        // Priority: CHITTA_REALM env > .cc-soul-realm file > git repo name > "brahman"

        // 1. Environment variable
        if (const char* env_realm = std::getenv("CHITTA_REALM")) {
            std::string realm = env_realm;
            return DuckDBToolResult::ok("Realm detected from environment: " + realm, {{"realm", realm}, {"source", "env"}});
        }

        // 2. Config file in current directory
        std::ifstream realm_file(".cc-soul-realm");
        if (realm_file.good()) {
            std::string realm;
            std::getline(realm_file, realm);
            // Trim whitespace
            realm.erase(0, realm.find_first_not_of(" \t\n\r"));
            realm.erase(realm.find_last_not_of(" \t\n\r") + 1);
            if (!realm.empty()) {
                return DuckDBToolResult::ok("Realm detected from .cc-soul-realm file: " + realm, {{"realm", realm}, {"source", "file"}});
            }
        }

        // 3. Git repository name
        std::array<char, 256> buffer;
        std::string git_root;
        FILE* pipe = popen("git rev-parse --show-toplevel 2>/dev/null", "r");
        if (pipe) {
            if (fgets(buffer.data(), buffer.size(), pipe)) {
                git_root = buffer.data();
                // Remove trailing newline
                if (!git_root.empty() && git_root.back() == '\n') {
                    git_root.pop_back();
                }
            }
            pclose(pipe);
        }

        if (!git_root.empty()) {
            // Extract repo name from path
            size_t last_slash = git_root.rfind('/');
            std::string repo_name = (last_slash != std::string::npos)
                ? git_root.substr(last_slash + 1)
                : git_root;
            std::string realm = "project:" + repo_name;
            return DuckDBToolResult::ok("Realm detected from git repository: " + realm, {{"realm", realm}, {"source", "git"}});
        }

        // 4. Default
        std::string realm = "brahman";
        return DuckDBToolResult::ok("Realm defaulted to: " + realm, {{"realm", realm}, {"source", "default"}});
    }
