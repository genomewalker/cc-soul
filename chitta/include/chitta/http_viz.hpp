#pragma once

#define CPPHTTPLIB_NO_SSL
#include "httplib.h"
#include "field_store.hpp"
#include "version.hpp"
#include <chitta_field.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <vector>
#include <atomic>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cstring>
#include <set>
#include <cstdio>
#include <sys/stat.h>

namespace chitta {

using json = nlohmann::json;

// Truncate a UTF-8 string at a valid character boundary (not in the middle of a multibyte seq)
static inline std::string utf8_trunc(const std::string& s, size_t max_bytes) {
    if (s.size() <= max_bytes) return s;
    size_t i = max_bytes;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return s.substr(0, i);
}

class VizServer {
public:
    VizServer(int port, const std::string& static_dir, FieldStore* field)
        : port_(port), static_dir_(static_dir), field_(field) {}

    ~VizServer() { stop(); }

    VizServer(const VizServer&) = delete;
    VizServer& operator=(const VizServer&) = delete;

    void start() {
        if (running_.load()) return;
        running_ = true;

        svr_.Get("/", [this](const httplib::Request&, httplib::Response& res) {
            serve_file(res, "/index.html", "text/html");
        });

        svr_.Get("/index.html", [this](const httplib::Request&, httplib::Response& res) {
            serve_file(res, "/index.html", "text/html");
        });

        svr_.Get("/health", [this](const httplib::Request&, httplib::Response& res) {
            json j;
            j["status"] = "ok";
            j["version"] = CHITTA_VERSION;
            j["memories"] = field_->memory_count();
            j["port"] = port_;
            res.set_content(j.dump(), "application/json");
        });

        svr_.Get("/graph", [this](const httplib::Request& req, httplib::Response& res) {
            size_t limit = 200;
            if (req.has_param("limit")) {
                try { limit = std::stoul(req.get_param_value("limit")); }
                catch (...) {}
            }
            std::string kinds_filter;
            if (req.has_param("kinds")) {
                kinds_filter = req.get_param_value("kinds");
            }
            try {
                res.set_content(fetch_graph(limit, kinds_filter), "application/json");
            } catch (const std::exception& ex) {
                fprintf(stderr, "[VizServer] /graph error: %s\n", ex.what());
                res.status = 500;
                res.set_content(std::string(R"({"error":")") + ex.what() + "\"}", "application/json");
            } catch (...) {
                fprintf(stderr, "[VizServer] /graph unknown error\n");
                res.status = 500;
            }
        });

        svr_.Get("/instances", [](const httplib::Request&, httplib::Response& res) {
            // Scan ~/.claude/projects/ for recently modified .jsonl transcript files
            static const std::vector<std::string> COLORS = {
                "#89b4fa","#a6e3a1","#f9e2af","#cba6f7",
                "#f38ba8","#94e2d5","#fab387","#89dceb"
            };
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            json instances = json::array();
            namespace fs = std::filesystem;
            const char* home = std::getenv("HOME");
            if (home) {
                fs::path projects_dir = fs::path(home) / ".claude" / "projects";
                std::error_code ec;
                for (const auto& proj : fs::directory_iterator(projects_dir, ec)) {
                    if (!proj.is_directory()) continue;
                    for (const auto& f : fs::directory_iterator(proj.path(), ec)) {
                        if (f.path().extension() != ".jsonl") continue;
                        auto mtime = fs::last_write_time(f.path(), ec);
                        if (ec) continue;
                        // Convert to epoch ms
                        auto dur = mtime.time_since_epoch();
                        int64_t mtime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
                        // file_time_type epoch differs from system_clock epoch on some systems
                        // Adjust: file_time uses 2174 years offset on Linux
                        // Use stat instead for portability
                        struct stat st{};
                        if (::stat(f.path().c_str(), &st) != 0) continue;
                        mtime_ms = (int64_t)st.st_mtime * 1000;

                        int64_t age_secs = (now_ms - mtime_ms) / 1000;
                        if (age_secs > 1800 || age_secs < 0) continue;

                        std::string sid = f.path().stem().string();
                        std::string proj_name = proj.path().filename().string();
                        // Replace leading dashes with slashes for display
                        if (!proj_name.empty() && proj_name[0] == '-') proj_name = proj_name.substr(1);
                        std::replace(proj_name.begin(), proj_name.end(), '-', '/');
                        if (proj_name.size() > 40) proj_name = "…" + proj_name.substr(proj_name.size()-37);

                        std::string short_id = sid.size() > 8 ? sid.substr(sid.size()-8) : sid;
                        size_t hash = 0;
                        for (char c : sid) hash = hash * 31 + (unsigned char)c;

                        json inst;
                        inst["id"]             = sid;
                        inst["short_id"]       = short_id;
                        inst["project"]        = proj_name;
                        inst["last_active_ms"] = mtime_ms;
                        inst["age_secs"]       = age_secs;
                        inst["status"]         = age_secs < 120 ? "active" : "idle";
                        inst["color"]          = COLORS[hash % COLORS.size()];
                        instances.push_back(inst);
                    }
                }
            }
            // Sort by last_active_ms descending
            std::sort(instances.begin(), instances.end(), [](const json& a, const json& b) {
                return a.value("last_active_ms", int64_t(0)) > b.value("last_active_ms", int64_t(0));
            });
            if (instances.size() > 10) instances.erase(instances.begin()+10, instances.end());

            json result;
            result["instances"] = instances;
            res.set_content(result.dump(), "application/json");
        });

        svr_.Get(R"(/memory/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
            uint64_t id = 0;
            try { id = std::stoull(req.matches[1]); }
            catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"invalid id"})", "application/json");
                return;
            }
            res.set_content(fetch_memory_detail(id), "application/json");
        });

        // ── Memory search/browse API ──────────────────────────────────────
        svr_.Get("/api/memories", [this](const httplib::Request& req, httplib::Response& res) {
            std::string kind = req.has_param("kind") ? req.get_param_value("kind") : "";
            std::string query = req.has_param("q") ? req.get_param_value("q") : "";
            size_t limit = 50;
            size_t offset = 0;
            if (req.has_param("limit")) try { limit = std::stoul(req.get_param_value("limit")); } catch(...) {}
            if (req.has_param("offset")) try { offset = std::stoul(req.get_param_value("offset")); } catch(...) {}

            std::string raw;
            if (!query.empty()) {
                // Keyword search
                CfRecallHit hits[100];
                size_t written = 0;
                size_t max_hits = std::min(limit, size_t(100));
                cf_recall_keyword(field_->handle(), query.c_str(), max_hits, nullptr, hits, max_hits, &written);
                json arr = json::array();
                for (size_t i = 0; i < written; ++i) {
                    json m;
                    m["id"] = std::to_string(hits[i].memory_id);
                    m["score"] = hits[i].score;
                    m["strength"] = hits[i].strength;
                    m["confidence"] = hits[i].confidence;
                    m["ts_ms"] = hits[i].ts_ms;
                    std::string content = field_->get_content(hits[i].memory_id);
                    m["content"] = utf8_trunc(content, 200);
                    // Get kind
                    char kbuf[256] = {};
                    if (cf_get_kind(field_->handle(), hits[i].memory_id,
                                    reinterpret_cast<uint8_t*>(kbuf), sizeof(kbuf)) == 0) {
                        m["kind"] = std::string(kbuf);
                    }
                    arr.push_back(m);
                }
                res.set_content(arr.dump(), "application/json");
            } else {
                raw = field_->list_memories(kind, "", "strength", limit, offset);
                res.set_content(raw, "application/json");
            }
        });

        // ── Memory mutation API ──────────────────────────────────────────
        svr_.Delete(R"(/api/memories/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
            uint64_t id = 0;
            try { id = std::stoull(req.matches[1]); } catch (...) {
                res.status = 400; res.set_content(R"({"error":"invalid id"})", "application/json"); return;
            }
            field_->forget(id);
            res.set_content(R"({"ok":true})", "application/json");
        });

        svr_.Patch(R"(/api/memories/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
            uint64_t id = 0;
            try { id = std::stoull(req.matches[1]); } catch (...) {
                res.status = 400; res.set_content(R"({"error":"invalid id"})", "application/json"); return;
            }
            try {
                auto body = json::parse(req.body);
                if (body.contains("content")) {
                    std::string content = body["content"].get<std::string>();
                    if (field_->update_memory_content(id, content) != 0) {
                        res.status = 500; res.set_content(R"({"error":"update failed"})", "application/json"); return;
                    }
                }
                res.set_content(fetch_memory_detail(id), "application/json");
            } catch (...) {
                res.status = 400; res.set_content(R"({"error":"invalid json"})", "application/json");
            }
        });

        // ── Triplet graph API ────────────────────────────────────────────
        svr_.Get("/api/triplets", [this](const httplib::Request& req, httplib::Response& res) {
            std::string entity = req.has_param("entity") ? req.get_param_value("entity") : "";
            std::string subject = req.has_param("subject") ? req.get_param_value("subject") : "";
            size_t limit = 100;
            if (req.has_param("limit")) try { limit = std::stoul(req.get_param_value("limit")); } catch(...) {}

            std::string raw;
            if (!entity.empty()) {
                raw = field_->list_triplets_for_entity(entity, limit);
            } else if (!subject.empty()) {
                raw = field_->query_subject(subject);
            } else {
                raw = "[]";
            }
            res.set_content(raw, "application/json");
        });

        // ── Skill registry API ───────────────────────────────────────────
        svr_.Get("/api/skills", [this](const httplib::Request&, httplib::Response& res) {
            std::string raw = field_->skill_list();
            res.set_content(raw, "application/json");
        });

        svr_.Get(R"(/api/skills/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
            std::string skill_id = req.matches[1];
            uint32_t version = 0;
            if (req.has_param("version")) try { version = std::stoul(req.get_param_value("version")); } catch(...) {}
            std::string raw = field_->skill_read(skill_id, version);
            if (raw.empty()) { res.status = 404; res.set_content(R"({"error":"not found"})", "application/json"); return; }
            res.set_content(raw, "application/json");
        });

        // ── Agent registry API ───────────────────────────────────────────
        svr_.Get("/api/agents", [this](const httplib::Request&, httplib::Response& res) {
            std::string raw = field_->agent_list();
            res.set_content(raw, "application/json");
        });

        svr_.Get(R"(/api/agents/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
            std::string agent_id = req.matches[1];
            std::string raw = field_->agent_get(agent_id);
            if (raw.empty()) { res.status = 404; res.set_content(R"({"error":"not found"})", "application/json"); return; }
            res.set_content(raw, "application/json");
        });

        svr_.Get("/events", [this](const httplib::Request&, httplib::Response& res) {
            res.set_header("Cache-Control", "no-cache");
            res.set_header("X-Accel-Buffering", "no");
            res.set_chunked_content_provider(
                "text/event-stream",
                [this](size_t, httplib::DataSink& sink) {
                    {
                        std::lock_guard<std::mutex> lk(sse_mtx_);
                        sse_sinks_.push_back(&sink);
                    }
                    std::string stats_json = get_stats_json();
                    std::string msg = "event: stats\ndata: " + stats_json + "\n\n";
                    if (!sink.write(msg.c_str(), msg.size())) {
                        remove_sink(&sink);
                        return false;
                    }
                    while (running_.load() && sink.is_writable()) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    remove_sink(&sink);
                    return false;
                }
            );
        });

        thread_ = std::thread([this]() {
            svr_.listen("0.0.0.0", port_);
        });
    }

    void stop() {
        if (!running_.exchange(false)) return;
        svr_.stop();
        if (thread_.joinable()) thread_.join();
    }

    void push_recall_event(const std::vector<uint64_t>& ids, int passes) {
        json j;
        j["ids"] = json::array();
        for (uint64_t id : ids) j["ids"].push_back(std::to_string(id));
        j["passes"] = passes;
        j["ts_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::string msg = "event: recall\ndata: " + j.dump() + "\n\n";
        std::lock_guard<std::mutex> lk(sse_mtx_);
        std::vector<httplib::DataSink*> dead;
        for (auto* sink : sse_sinks_) {
            if (!sink->write(msg.c_str(), msg.size())) dead.push_back(sink);
        }
        for (auto* d : dead) {
            sse_sinks_.erase(
                std::remove(sse_sinks_.begin(), sse_sinks_.end(), d),
                sse_sinks_.end());
        }
    }

private:
    httplib::Server svr_;
    std::thread thread_;
    std::mutex sse_mtx_;
    std::vector<httplib::DataSink*> sse_sinks_;
    std::atomic<bool> running_{false};
    int port_;
    std::string static_dir_;
    FieldStore* field_;

    void remove_sink(httplib::DataSink* sink) {
        std::lock_guard<std::mutex> lk(sse_mtx_);
        sse_sinks_.erase(
            std::remove(sse_sinks_.begin(), sse_sinks_.end(), sink),
            sse_sinks_.end());
    }

    void serve_file(httplib::Response& res, const std::string& path, const std::string& content_type) {
        std::string full_path = static_dir_ + path;
        std::ifstream ifs(full_path, std::ios::binary);
        if (!ifs) {
            res.status = 404;
            res.set_content("Not found: " + full_path, "text/plain");
            return;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        res.set_content(ss.str(), content_type);
    }

    std::string get_stats_json() {
        json j;
        j["memories"] = field_->memory_count();
        j["symbols"] = field_->symbol_count();
        j["version"] = CHITTA_VERSION;

        std::string stats_raw = field_->memory_stats();
        auto stats = json::parse(stats_raw, nullptr, false);
        if (!stats.is_discarded() && stats.is_object()) {
            j["stats"] = stats;
        }
        return j.dump();
    }

    std::string fetch_graph(size_t limit, const std::string& kinds_filter) {
        // Parse requested kinds
        std::vector<std::string> requested_kinds;
        if (!kinds_filter.empty()) {
            std::istringstream iss(kinds_filter);
            std::string k;
            while (std::getline(iss, k, ',')) {
                k.erase(0, k.find_first_not_of(' '));
                k.erase(k.find_last_not_of(' ') + 1);
                if (!k.empty()) requested_kinds.push_back(k);
            }
        }

        // Fetch per-kind for even distribution; enforce a per-kind cap then fill remainder
        json all_mems = json::array();
        if (requested_kinds.empty()) {
            auto raw = field_->list_memories("", "", "strength", limit, 0);
            auto arr = json::parse(raw, nullptr, false);
            if (arr.is_array()) all_mems = arr;
        } else {
            size_t n_kinds = requested_kinds.size();
            size_t base_quota = std::max(size_t(1), limit / n_kinds);
            // Round 1: fetch base_quota per kind
            struct KindBucket { std::vector<json> items; size_t quota_used; };
            std::vector<KindBucket> buckets(n_kinds);
            std::unordered_map<uint64_t, bool> seen_ids;
            for (size_t ki = 0; ki < n_kinds; ++ki) {
                auto raw = field_->list_memories(requested_kinds[ki], "", "strength", base_quota, 0);
                auto arr = json::parse(raw, nullptr, false);
                if (!arr.is_array()) continue;
                for (auto& m : arr) {
                    uint64_t id = m.value("id", uint64_t(0));
                    if (!id || seen_ids.count(id)) continue;
                    seen_ids[id] = true;
                    buckets[ki].items.push_back(m);
                }
                buckets[ki].quota_used = buckets[ki].items.size();
            }
            // Round 2: fill remaining slots from surplus kinds (those that gave fewer)
            size_t filled = seen_ids.size();
            if (filled < limit) {
                size_t remaining = limit - filled;
                // Re-fetch larger amounts from kinds that have surplus
                for (size_t ki = 0; ki < n_kinds && remaining > 0; ++ki) {
                    if (buckets[ki].quota_used >= base_quota) {
                        // This kind may have more; fetch extra
                        size_t extra = std::min(remaining, base_quota);
                        auto raw = field_->list_memories(requested_kinds[ki], "", "strength",
                                                         base_quota + extra, base_quota);
                        auto arr = json::parse(raw, nullptr, false);
                        if (!arr.is_array()) continue;
                        for (auto& m : arr) {
                            if (remaining == 0) break;
                            uint64_t id = m.value("id", uint64_t(0));
                            if (!id || seen_ids.count(id)) continue;
                            seen_ids[id] = true;
                            buckets[ki].items.push_back(m);
                            --remaining;
                        }
                    }
                }
            }
            // Interleave by kind (round-robin) so graph looks varied, then collect
            bool any = true;
            size_t total = 0;
            for (size_t round = 0; any && total < limit; ++round) {
                any = false;
                for (auto& b : buckets) {
                    if (round < b.items.size() && total < limit) {
                        all_mems.push_back(b.items[round]);
                        ++total;
                        any = true;
                    }
                }
            }
        }

        if (all_mems.empty()) return R"({"nodes":[],"edges":[]})";

        json nodes = json::array();
        std::vector<uint64_t> node_ids;
        std::vector<std::string> node_labels;
        std::set<uint64_t> node_id_set;

        for (const auto& m : all_mems) {
            std::string kind = m.value("kind", "");
            uint64_t id = m.value("id", uint64_t(0));
            if (id == 0) continue;

            std::string content = m.value("content", "");
            std::string label = utf8_trunc(content, 80);
            float confidence = m.value("confidence", 0.0f);
            float strength = m.value("strength", 0.0f);
            int64_t ts_ms = m.value("ts_ms", int64_t(0));
            std::string realm = m.value("realm", "");

            json node;
            node["id"] = std::to_string(id);
            node["label"] = label;
            node["kind"] = kind;
            node["strength"] = strength > 0 ? strength : confidence;
            node["confidence"] = confidence;
            node["ts_ms"] = ts_ms;
            node["realm"] = realm;
            nodes.push_back(node);
            node_ids.push_back(id);
            node_labels.push_back(utf8_trunc(content, 60));
            node_id_set.insert(id);
        }

        // Build edges via keyword similarity: for sampled nodes, find text-similar neighbors
        std::set<std::string> seen_edges;
        json deduped_edges = json::array();

        size_t edge_seed_count = std::min(node_ids.size(), size_t(50));
        for (size_t i = 0; i < edge_seed_count; ++i) {
            const std::string& q = node_labels[i];
            if (q.size() < 5) continue;

            CfRecallHit hits[6];
            size_t written = 0;
            cf_recall_keyword(field_->handle(), q.c_str(), 5, nullptr, hits, 6, &written);

            for (size_t h = 0; h < written; ++h) {
                uint64_t tgt_id = hits[h].memory_id;
                if (tgt_id == node_ids[i]) continue;
                if (node_id_set.find(tgt_id) == node_id_set.end()) continue;

                std::string src = std::to_string(node_ids[i]);
                std::string tgt = std::to_string(tgt_id);
                if (src > tgt) std::swap(src, tgt);
                std::string key = src + "-" + tgt;
                if (!seen_edges.insert(key).second) continue;

                json edge;
                edge["source"] = src;
                edge["target"] = tgt;
                edge["weight"] = std::max(0.1f, hits[h].score);
                deduped_edges.push_back(edge);
            }

            // Also try Hebbian edges (populated after resonance runs)
            std::vector<uint64_t> seeds = {node_ids[i]};
            auto assoc = field_->expand_associations(seeds, 1, 4);
            for (const auto& a : assoc) {
                if (a.memory_id == node_ids[i]) continue;
                if (node_id_set.find(a.memory_id) == node_id_set.end()) continue;
                std::string src = std::to_string(node_ids[i]);
                std::string tgt = std::to_string(a.memory_id);
                if (src > tgt) std::swap(src, tgt);
                std::string key = src + "-" + tgt;
                if (!seen_edges.insert(key).second) continue;
                json edge;
                edge["source"] = src;
                edge["target"] = tgt;
                edge["weight"] = std::max(0.1f, a.score);
                edge["hebbian"] = true;
                deduped_edges.push_back(edge);
            }
        }

        json result;
        result["nodes"] = nodes;
        result["edges"] = deduped_edges;
        return result.dump();
    }

    std::string fetch_memory_detail(uint64_t id) {
        std::string content = field_->get_content(id);
        if (content.empty()) {
            return R"({"error":"not found"})";
        }

        std::string meta_raw = field_->get_memory_metadata(id);
        json meta = meta_raw.empty() ? json::object() : json::parse(meta_raw, nullptr, false);
        if (meta.is_discarded()) meta = json::object();

        json result;
        result["id"] = std::to_string(id);
        result["content"] = content;
        if (meta.is_object()) {
            result["kind"] = meta.value("kind", "");
            result["realm"] = meta.value("realm", "");
            result["confidence"] = meta.value("confidence", 0.0f);
            result["strength"] = meta.value("strength", 0.0f);
            result["ts_ms"] = meta.value("ts_ms", int64_t(0));
        }

        // k-NN neighbors via association expansion
        std::vector<uint64_t> seeds = {id};
        auto neighbors = field_->expand_associations(seeds, 2, 10);
        json assoc = json::array();
        for (const auto& h : neighbors) {
            if (h.memory_id == id) continue;
            json a;
            a["id"] = std::to_string(h.memory_id);
            a["score"] = h.score;
            a["kind"] = h.kind;
            a["preview"] = h.content.substr(0, 100);
            assoc.push_back(a);
        }
        result["associations"] = assoc;

        return result.dump();
    }
};

} // namespace chitta
