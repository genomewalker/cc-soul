#include <chitta/queue_processor.hpp>
#include <chitta/rpc/field_handler.hpp>
#include <chitta/rpc/sandbox.hpp>
#include <chitta/vak.hpp>
#include <iostream>

extern std::atomic<bool> daemon_running;
extern std::atomic<bool> verbose_mode;

using json = nlohmann::json;

namespace chitta {

QueueProcessor::QueueProcessor(FieldStore& field_store,
                               VakYantra* yantra,
                               const DistillConfig& distill_config,
                               FieldRpcHandler& handler,
                               const std::string& queue_path,
                               const std::string& failed_queue_path,
                               std::atomic<size_t>& queue_count,
                               std::atomic<size_t>& queue_distill_count,
                               std::atomic<size_t>& queue_fail_count)
    : field_store_(field_store)
    , yantra_(yantra)
    , distill_config_(distill_config)
    , handler_(handler)
    , queue_path_(queue_path)
    , failed_queue_path_(failed_queue_path)
    , queue_count_(queue_count)
    , queue_distill_count_(queue_distill_count)
    , queue_fail_count_(queue_fail_count)
{}

void QueueProcessor::start() {
    thread_ = std::thread([this]() { run(); });
}

void QueueProcessor::stop() {
    if (thread_.joinable()) thread_.join();
}

float QueueProcessor::category_to_confidence(const std::string& category) {
    if (category == "correction") return 0.95f;
    if (category == "preference") return 0.90f;
    if (category == "solution")   return 0.90f;
    if (category == "milestone")  return 0.90f;
    if (category == "decision")   return 0.85f;
    if (category == "failure")    return 0.85f;
    if (category == "gotcha")     return 0.85f;
    if (category == "episode")    return 0.70f;
    return 0.80f;  // wisdom, pattern, insight, belief, etc.
}

std::vector<float> QueueProcessor::embed_text(const std::string& text) {
    if (!yantra_) return {};
    try {
        return yantra_->transform(text).nu.data;
    } catch (...) {}
    return {};
}

std::string QueueProcessor::category_to_kind(const std::string& cat) {
    if (cat == "episode") return "episode";
    if (cat == "belief")  return "belief";
    return "wisdom";
}

void QueueProcessor::write_failed_item(const std::string& line, const std::exception& e) {
    try {
        json entry;
        entry["error"] = e.what();
        entry["retry_count"] = 0;
        entry["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto parsed = json::parse(line, nullptr, false);
        if (!parsed.is_discarded()) {
            entry["tool"] = parsed.value("tool", "unknown");
            entry["args"] = parsed.value("args", json::object());
        } else {
            entry["raw"] = line.substr(0, 300);
        }
        if (!sandbox::append_line_atomic(
                failed_queue_path_,
                entry.dump(-1, ' ', false, json::error_handler_t::replace))) {
            std::cerr << "[queue] FAILED to write dead-letter entry to "
                      << failed_queue_path_
                      << " (original error: " << e.what() << ")\n";
        }
        queue_fail_count_++;
    } catch (const std::exception& inner) {
        std::cerr << "[queue] write_failed_item threw: " << inner.what()
                  << " (original error: " << e.what() << ")\n";
    } catch (...) {
        std::cerr << "[queue] write_failed_item threw unknown exception"
                  << " (original error: " << e.what() << ")\n";
    }
}

void QueueProcessor::run() {
    // In-process transcript registry: session_id -> {transcript_path, realm, last_line}
    // Updated by transcript_register/transcript_progress ops; queried by distill_trigger.
    std::unordered_map<std::string, json> transcript_reg;

    while (daemon_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Atomically claim queue file via rename (prevents data loss from concurrent writes)
        std::string processing_path = queue_path_ + ".processing";
        if (std::rename(queue_path_.c_str(), processing_path.c_str()) != 0) continue;

        // Read the claimed file
        std::vector<std::string> lines;
        {
            std::ifstream in(processing_path);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty()) lines.push_back(line);
            }
        }

        // Remove processed file
        std::remove(processing_path.c_str());

        if (lines.empty()) continue;

        // Process each queued request — all writes go to chitta-field
        for (const auto& line : lines) {
            if (!daemon_running) break;

            try {
                auto _lk = handler_.acquire_lock();
                auto j = json::parse(line);
                std::string tool = j.value("tool", "");
                auto args = j.value("args", json::object());

                // FieldStore is always ready (synchronous init)

                if (tool == "observe") {
                    std::string category = args.value("category", "wisdom");
                    std::string title = args.value("title", "");
                    std::string content = args.value("content", "");
                    if (!content.empty()) {
                        std::string source   = args.value("source", "hook_regex");
                        std::string evidence = args.value("evidence", "");

                        // ── Source trust policy (contract enforcement) ─────
                        auto policy = source_policy(source);
                        float confidence = args.contains("confidence")
                            ? args.value("confidence", 0.50f)
                            : category_to_confidence(category);
                        float decay = policy.allow_durable ? 0.005f : 0.02f;

                        // Clamp confidence to policy bounds
                        float orig_confidence = confidence;
                        confidence = std::min(confidence, policy.max_confidence);
                        confidence = std::max(confidence, policy.min_confidence);
                        if (orig_confidence != confidence) {
                            std::cerr << "[contract] confidence clamped for source=" << source
                                      << ": " << orig_confidence << "→" << confidence << "\n";
                        }
                        // Reject: distillation below floor is a bug, not a policy violation
                        if (source == "distillation" && confidence < policy.min_confidence) {
                            std::cerr << "[contract] REJECT: distillation event confidence="
                                      << confidence << " below floor=" << policy.min_confidence
                                      << " — check distillation pipeline\n";
                            queue_fail_count_++;
                            continue;  // skip this queue item
                        }
                        std::string full_text = title.empty() ? content : title + "\n" + content;
                        std::string realm = args.value("realm", "brahman");
                        auto new_id = field_store_.remember(category_to_kind(category), realm,
                                                  full_text, embed_text(full_text),
                                                  confidence, decay);
                        // Set initial epistemic status and memory status based on source
                        if (new_id > 0) {
                            uint8_t es = epistemic_status_for_source(source);
                            uint8_t ms = initial_status_for_source(source);
                            if (es != 1) field_store_.set_epistemic_status(new_id, es);
                            if (ms != 0) field_store_.set_memory_status(new_id, ms);
                        }
                        // Store provenance as triplets
                        if (new_id > 0 && !source.empty()) {
                            field_store_.add_triplet(std::to_string(new_id), "source", source);
                            if (!evidence.empty())
                                field_store_.add_triplet(std::to_string(new_id), "evidence", evidence);
                        }
                        queue_count_++;
                        // Correction supersession: find semantically similar memories
                        // and mark them as superseded via a triplet relation
                        if (category == "correction" && new_id > 0) {
                            // Hard contradiction policy (CONTRACTS.md §3):
                            // A correction supersedes ONLY:
                            // 1. If target_id is explicit → supersede that specific memory
                            // 2. Otherwise → same realm + cosine > 0.92 (tight threshold)
                            //    AND same kind (corrections don't supersede different memory types)
                            // force_supersede_ids: explicit list bypasses cosine threshold
                            if (args.contains("force_supersede_ids")) {
                                auto fsi = args["force_supersede_ids"];
                                std::vector<std::string> ids;
                                if (fsi.is_array()) {
                                    for (auto& v : fsi) ids.push_back(v.get<std::string>());
                                } else if (fsi.is_string()) {
                                    std::istringstream ss(fsi.get<std::string>());
                                    std::string tok;
                                    while (std::getline(ss, tok, ',')) {
                                        tok.erase(0, tok.find_first_not_of(' '));
                                        tok.erase(tok.find_last_not_of(' ') + 1);
                                        if (!tok.empty()) ids.push_back(tok);
                                    }
                                }
                                for (const auto& sid : ids) {
                                    try {
                                        uint64_t tid = std::stoull(sid);
                                        field_store_.add_triplet(std::to_string(new_id), "supersedes", sid, 1.0f, new_id);
                                        field_store_.weaken(tid, 0.15f);
                                        field_store_.set_memory_status(tid, 1);
                                    } catch (...) {}
                                }
                            }
                            auto emb = embed_text(full_text);
                            std::string target_id_str = args.value("target_id", "");
                            if (!target_id_str.empty()) {
                                // Explicit target: targeted supersession
                                try {
                                    uint64_t tid = std::stoull(target_id_str);
                                    field_store_.add_triplet(std::to_string(new_id), "supersedes", target_id_str, 1.0f, new_id);
                                    field_store_.weaken(tid, 0.15f);
                                    field_store_.set_memory_status(tid, 1);
                                    std::cerr << "[contract] explicit supersession: " << new_id << "→" << target_id_str << "\n";
                                } catch (...) {}
                            } else if (!emb.empty()) {
                                // Semantic supersession: strict — same realm, same kind, very high threshold
                                auto hits = field_store_.recall(emb, 5, realm);
                                for (const auto& h : hits) {
                                    if (h.memory_id == new_id) continue;
                                    if (h.realm != realm) continue;           // same realm only
                                    if (h.kind == "correction") continue;     // don't supersede other corrections
                                    if (h.score < 0.92f) continue;            // tight: 0.92 not 0.85
                                    field_store_.add_triplet(std::to_string(new_id), "supersedes", std::to_string(h.memory_id), 1.0f, new_id);
                                    field_store_.weaken(h.memory_id, 0.15f);
                                    field_store_.set_memory_status(h.memory_id, 1);
                                    std::cerr << "[contract] semantic supersession: " << new_id << "→" << h.memory_id << " (score=" << h.score << ")\n";
                                }
                            }
                        }
                    }
                } else if (tool == "strengthen") {
                    std::string id_str = args.value("id", "");
                    float amount = static_cast<float>(args.value("amount", 0.1));
                    if (!id_str.empty()) {
                        try {
                            uint64_t cf_id = std::stoull(id_str);
                            field_store_.strengthen(cf_id, amount);
                            queue_count_++;
                        } catch (...) {}
                    }
                } else if (tool == "connect") {
                    std::string subj = args.value("subject", "");
                    std::string pred = args.value("predicate", "");
                    std::string obj = args.value("object", "");
                    if (!subj.empty() && !pred.empty() && !obj.empty()) {
                        field_store_.add_triplet(subj, pred, obj);
                        queue_count_++;
                    }
                } else if (tool == "curiosity_note_gap") {
                    std::string gap = args.value("gap", "");
                    if (!gap.empty()) {
                        std::string content = "[curiosity] " + gap;
                        field_store_.remember("episode", "brahman", content,
                                              embed_text(content), 0.7f, 0.0f);
                        queue_count_++;
                    }
                } else if (tool == "store_policy") {
                    std::string policy_type = args.value("type", "");
                    std::string content = args.value("content", "");
                    float confidence = args.value("confidence", 0.5f);
                    if (!policy_type.empty() && !content.empty()) {
                        std::string full = "[policy:" + policy_type + "] " + content;
                        field_store_.remember("wisdom", "brahman", full,
                                              embed_text(full), confidence, 0.0f);
                        queue_count_++;
                    }
                } else if (tool == "store_claim") {
                    std::string subject = args.value("subject", "");
                    std::string predicate = args.value("predicate", "");
                    std::string object_norm = args.value("object", "");
                    if (!subject.empty() && !predicate.empty() && !object_norm.empty()) {
                        field_store_.add_triplet(subject, predicate, object_norm);
                        queue_count_++;
                    }
                } else if (tool == "learn_outcome") {
                    std::string id_str = args.contains("memory-id")
                        ? args.value("memory-id", "")
                        : args.value("memory_id", "");
                    std::string outcome = args.value("outcome", "");
                    std::string context = args.value("context", "");
                    if (!id_str.empty() && !outcome.empty()) {
                        try {
                            uint64_t cf_id = std::stoull(id_str);
                            json payload = {{"outcome", outcome}, {"context", context}};
                            field_store_.emit_event("analytics", "outcome",
                                                    id_str, payload.dump());
                            if (outcome == "positive") field_store_.strengthen(cf_id, 0.1f);
                            else if (outcome == "negative") field_store_.weaken(cf_id, 0.15f);
                            queue_count_++;
                        } catch (...) {}
                    }
                } else if (tool == "session_register") {
                    std::string sid = args.value("session_id", "");
                    if (!sid.empty()) {
                        field_store_.emit_event("session", "register", sid, args.dump());
                        queue_count_++;
                    }
                } else if (tool == "session_heartbeat") {
                    std::string sid = args.value("session_id", "");
                    if (!sid.empty()) {
                        field_store_.emit_event("session", "heartbeat", sid,
                                                args.value("metadata", "{}"));
                        queue_count_++;
                    }
                } else if (tool == "session_deregister") {
                    std::string sid = args.value("session_id", "");
                    if (!sid.empty()) {
                        field_store_.emit_event("session", "deregister", sid, "{}");
                        queue_count_++;
                    }
                } else if (tool == "transcript_register") {
                    std::string session_id = args.value("session_id", "");
                    std::string path = args.value("transcript_path", "");
                    if (!path.empty()) {
                        std::cerr << "[queue] transcript_register: session=" << session_id << " path=" << path << "\n";
                        transcript_reg[session_id] = args;
                        field_store_.emit_event("transcript", "register",
                                                session_id, args.dump());
                        queue_count_++;
                    }
                } else if (tool == "ledger_save") {
                    std::string session_id = args.value("session_id", "");
                    if (!session_id.empty()) {
                        field_store_.emit_event("admin", "ledger_save",
                                                session_id, args.dump());
                        queue_count_++;
                    }
                } else if (tool == "narrative_log") {
                    std::string session_id = args.value("session_id", "");
                    std::string summary = args.value("summary", "");
                    if (!session_id.empty() && !summary.empty()) {
                        field_store_.emit_event("analytics", "narrative_log",
                                                session_id, args.dump());
                        queue_count_++;
                    }
                } else if (tool == "calibration_record") {
                    std::string domain = args.value("domain", "");
                    if (!domain.empty()) {
                        field_store_.emit_event("analytics", "calibration",
                                                domain, args.dump());
                        queue_count_++;
                    }
                } else if (tool == "habit_observe") {
                    std::string trigger = args.value("trigger", "");
                    std::string response = args.value("response", "");
                    if (!trigger.empty() && !response.empty()) {
                        field_store_.emit_event("narrative", "habit",
                                                trigger, args.dump());
                        queue_count_++;
                    }
                } else if (tool == "anticipation_success") {
                    int64_t id = args.value("id", (int64_t)0);
                    if (id > 0) {
                        field_store_.emit_event("narrative", "anticipation_success",
                                                std::to_string(id), "{}");
                        queue_count_++;
                    }
                } else if (tool == "store_turn") {
                    std::string session_id = args.value("session_id", "");
                    std::string content = args.value("content", "");
                    if (!session_id.empty() && !content.empty()) {
                        field_store_.emit_event("transcript", "turn",
                                                session_id, args.dump());
                        queue_count_++;
                    }
                } else if (tool == "store_relationship_event") {
                    std::string event_type = args.value("event_type", "");
                    std::string session_id = args.value("session_id", "");
                    if (!event_type.empty()) {
                        field_store_.emit_event("relationship", event_type,
                                                session_id, args.dump());
                        queue_count_++;
                    }
                } else if (tool == "log_session_tokens") {
                    std::string sid = args.value("session_id", "");
                    if (!sid.empty()) {
                        field_store_.emit_event("analytics", "session_tokens",
                                                sid, args.dump());
                        queue_count_++;
                    }
                } else if (tool == "log_correction_outcome") {
                    std::string sid = args.value("session_id", "");
                    if (!sid.empty()) {
                        field_store_.emit_event("analytics", "correction_outcome",
                                                sid, args.dump());
                        queue_count_++;
                    }
                } else if (tool == "log_exposure") {
                    std::string session_id = args.value("session_id", "");
                    if (!session_id.empty()) {
                        field_store_.emit_event("analytics", "exposure",
                                                session_id, args.dump());
                        queue_count_++;
                    }
                } else if (tool == "distill_trigger") {
                    if (!distill_config_.enabled) {
                        if (verbose_mode)
                            std::cerr << "[queue] distill_trigger ignored (distillation disabled)\n";
                    } else {
                        std::string session_id = args.value("session_id", "");
                        if (!session_id.empty()) {
                            // Look up transcript path from in-process registry first,
                            // then fall back to FieldStore event log.
                            std::string transcript_path;
                            std::string realm = "brahman";
                            auto it = transcript_reg.find(session_id);
                            if (it != transcript_reg.end()) {
                                transcript_path = it->second.value("transcript_path", "");
                                realm = it->second.value("realm", "brahman");
                            } else {
                                auto reg = field_store_.get_latest_event("transcript", "register", session_id);
                                if (reg) {
                                    try {
                                        auto r = json::parse(*reg);
                                        transcript_path = r.value("transcript_path", "");
                                        realm = r.value("realm", "brahman");
                                    } catch (...) {}
                                }
                            }
                            if (!transcript_path.empty()) {
                                std::cerr << "[queue] distill_trigger: path=" << transcript_path << " realm=" << realm << "\n";
                                TranscriptState ts;
                                ts.session_id = session_id;
                                ts.transcript_path = transcript_path;
                                ts.realm = realm;
                                ts.last_processed_line = 0;
                                // Check progress from in-process registry
                                if (it != transcript_reg.end()) {
                                    ts.last_processed_line = it->second.value("last_line", (int64_t)0);
                                } else {
                                    auto progress = field_store_.get_latest_event("transcript", "progress", session_id);
                                    if (progress) {
                                        try {
                                            auto p = json::parse(*progress);
                                            ts.last_processed_line = p.value("last_line", (int64_t)0);
                                        } catch (...) {}
                                    }
                                }
                                if (run_distillation(field_store_, yantra_, ts, distill_config_, &handler_, true)) {
                                    queue_distill_count_++;
                                    std::cerr << "[queue] distill_trigger: success (total=" << queue_distill_count_.load() << ")\n";
                                } else {
                                    std::cerr << "[queue] distill_trigger: run_distillation returned false\n";
                                }
                            } else {
                                std::cerr << "[queue] distill_trigger: no transcript registered for " << session_id << "\n";
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                // Always log — never silently drop a learning event
                std::cerr << "[queue] FAILED: " << e.what() << "\n";
                // Write to dead-letter file for inspection/retry
                write_failed_item(line, e);
            }
        }

        if (verbose_mode && !lines.empty()) {
            std::cerr << "[queue] Processed " << lines.size() << " items, total=" << queue_count_ << "\n";
        }
    }
}

} // namespace chitta
