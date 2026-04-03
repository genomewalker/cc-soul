#include "../include/chitta/native_distiller.hpp"
#include "../include/chitta/ssl_prompt.hpp"
#include <sstream>
#include <cmath>
#include <iostream>

namespace chitta {

// ── Constructor ──────────────────────────────────────────────────────────────

NativeDistiller::NativeDistiller(FieldStore& field, EmbedFn embedder,
                                 const NativeDistillConfig& config)
    : field_store_(&field), embedder_(std::move(embedder)), config_(config) {}

// ── Helpers ──────────────────────────────────────────────────────────────────

float NativeDistiller::category_to_confidence(const std::string& category) {
    if (category == "correction") return 0.95f;
    if (category == "preference") return 0.90f;
    if (category == "solution")   return 0.90f;
    if (category == "decision")   return 0.85f;
    if (category == "failure")    return 0.85f;
    if (category == "gotcha")     return 0.85f;
    if (category == "pattern")    return 0.80f;
    return 0.80f;  // wisdom, etc.
}

float NativeDistiller::category_to_decay(const std::string& category) {
    // Corrections and preferences are long-lived; general wisdom decays faster
    if (category == "correction") return 0.0001f;
    if (category == "preference") return 0.0001f;
    if (category == "solution")   return 0.001f;
    if (category == "decision")   return 0.001f;
    return 0.005f;
}

void NativeDistiller::log(const std::string& msg) {
    if (log_callback_) {
        log_callback_(msg);
    } else if (config_.verbose) {
        std::cerr << msg << "\n";
    }
}

// ── LLM HTTP call ───────────────────────────────────────────────────────────

std::string NativeDistiller::call_llm(const std::string& prompt) {
    if (cached_endpoint_.empty()) {
        cached_endpoint_ = config_.endpoint;
        if (cached_endpoint_.empty()) {
            cached_endpoint_ = discover_gpu_endpoint(config_.model,
                [this](const std::string& msg) { log(msg); });
        }
    }

    if (cached_endpoint_.empty()) {
        log("[distill] No GPU endpoint found — cannot distill");
        return "";
    }

    return call_llm_http(
        cached_endpoint_, config_.model, prompt,
        "You are a knowledge distiller. Extract learnings in SSL v0.2 format. "
        "Output ONLY SSL-formatted learnings.",
        config_.timeout_secs, 0.3f, 4096,
        [this](const std::string& msg) { log(msg); });
}

// ── store_learnings (FieldStore) ─────────────────────────────────────────────

void NativeDistiller::store_learnings(
    const SSLParser::Result& ssl_result,
    const std::string& realm,
    uint64_t episode_mem_id,
    DistillResult& result
) {
    for (const auto& learning : ssl_result.learnings) {
        float confidence = category_to_confidence(learning.category);
        float decay      = category_to_decay(learning.category);
        std::string full_text = learning.title + "\n" + learning.content;

        // Embed the learning text
        std::vector<float> embedding;
        if (embedder_) {
            embedding = embedder_(full_text);
        }

        // Dedup: if a near-identical memory exists, strengthen it instead
        bool deduped = false;
        if (!embedding.empty() && config_.dedup_threshold > 0.0f) {
            auto hits = field_store_->recall(embedding, 5, realm);
            for (const auto& hit : hits) {
                if (hit.semantic_score >= config_.dedup_threshold) {
                    field_store_->strengthen(hit.memory_id, 0.05f);
                    // Promote provisional hook memories to distillation-tier confidence
                    if (hit.confidence < 0.75f) {
                        float target = category_to_confidence(learning.category);
                        field_store_->update_confidence(hit.memory_id, target - hit.confidence);
                        log("[distill]   promoted confidence: " + learning.category +
                            " " + std::to_string(hit.confidence) + "→" + std::to_string(target));
                    }
                    result.learnings_deduped++;
                    log("[distill]   ~dup " + learning.category + ": " +
                        learning.title.substr(0, 50) + " (strengthened existing)");
                    deduped = true;
                    break;
                }
            }
        }

        if (deduped) continue;

        uint64_t mem_id = 0;
        try {
            mem_id = field_store_->remember("wisdom", realm, full_text,
                                            embedding, confidence, decay);
        } catch (...) {
            continue;
        }

        if (mem_id == 0) continue;

        result.learnings_stored++;
        log("[distill]   +" + learning.category + ": " +
            learning.title.substr(0, 60) + "...");

        // Link to episode memory via DerivedFrom edge (edge_type=0)
        if (episode_mem_id > 0) {
            field_store_->add_edge(mem_id, episode_mem_id, 0, 1.0f);
            field_store_->add_triplet(std::to_string(mem_id),
                                      "derived_from",
                                      std::to_string(episode_mem_id));
        }

        // Code citations
        for (const auto& cite : learning.citations) {
            std::string cite_target = cite.file;
            if (cite.line > 0) {
                cite_target += ":" + std::to_string(cite.line);
            }
            field_store_->add_triplet(std::to_string(mem_id), "cites", cite_target);
            result.citations_linked++;
            log("[distill]     cite: " + cite_target);

            // Bridge to symbol at that file:line if available
            if (cite.line > 0) {
                auto syms = field_store_->symbols_in_file(cite.file);
                for (const auto& sym : syms) {
                    if (sym.line_start <= static_cast<uint32_t>(cite.line) &&
                        static_cast<uint32_t>(cite.line) <= sym.line_end) {
                        std::string sym_ref = "symbol:" + std::to_string(sym.symbol_id);
                        field_store_->add_triplet(std::to_string(mem_id), "cites", sym_ref);
                        log("[distill]     → symbol: " +
                            std::string(reinterpret_cast<const char*>(sym.name)));
                        break;
                    }
                }
            }
        }
    }

    // Store triplets
    for (const auto& triplet : ssl_result.triplets) {
        field_store_->add_triplet(triplet.subject, triplet.predicate, triplet.object);
        log("[distill]   triplet: " + triplet.subject + "→" +
            triplet.predicate + "→" + triplet.object);
    }
}

// ── distill_session ──────────────────────────────────────────────────────────

DistillResult NativeDistiller::distill_session(
    const std::string& session_id,
    const std::string& transcript_path,
    const std::string& realm,
    int64_t skip_lines,
    bool queue_triggered
) {
    DistillResult result;

    // 1. Parse transcript
    TranscriptParseOptions parse_opts;
    parse_opts.skip_lines = skip_lines;

    int64_t last_line = 0;
    auto turns = parser_.parse(transcript_path, parse_opts, &last_line);

    if (turns.empty()) {
        result.error = parser_.last_error().empty() ? "No turns found" : parser_.last_error();
        return result;
    }

    // 2. Check minimum turns
    int effective_min_turns = queue_triggered ? 1 : config_.min_turns;
    if (static_cast<int>(turns.size()) < effective_min_turns) {
        result.error = "Insufficient turns: " + std::to_string(turns.size()) +
                       " < " + std::to_string(effective_min_turns);
        return result;
    }

    log("[distill] Session " + session_id + ": " + std::to_string(turns.size()) + " turns");

    // 3. Build conversation with smart truncation
    auto conversation = TranscriptParser::build_conversation(turns);

    // 4. Build SSL prompt
    auto prompt = ssl::build_prompt(conversation);

    // 5. Get turn range for episode
    int start_turn = turns.empty() ? 0 : turns.front().turn_index;
    int end_turn   = turns.empty() ? 0 : turns.back().turn_index;

    // 6. Create episode record in FieldStore
    int64_t episode_id = 0;
    std::ostringstream ep_content;
    ep_content << "[episode] session=" << session_id
               << " turns=" << start_turn << "-" << end_turn
               << " realm=" << realm;
    std::vector<float> ep_embedding;
    if (embedder_) {
        ep_embedding = embedder_(ep_content.str());
    }
    try {
        episode_id = static_cast<int64_t>(
            field_store_->remember("episode", realm, ep_content.str(),
                                   ep_embedding, 1.0f, 0.0f));
    } catch (...) {}

    if (episode_id > 0) {
        result.episode_id = episode_id;
        log("[distill]   Episode: " + std::to_string(episode_id) +
            " (turns " + std::to_string(start_turn) + "-" + std::to_string(end_turn) + ")");
    }

    // 7. Call LLM for distillation via HTTP (Ollama/vLLM)
    log("[distill] Calling " + config_.model + " via HTTP...");
    std::string llm_output = call_llm(prompt);

    if (llm_output.empty()) {
        result.error = "No result from LLM";
        return result;
    }

    // 8. Parse SSL output
    log("[distill] Processing SSL results...");
    auto ssl_result = ssl_parser_.parse(llm_output);

    // 9. Store learnings and citations
    store_learnings(ssl_result, realm, static_cast<uint64_t>(episode_id), result);
    result.triplets_created = static_cast<int>(ssl_result.triplets.size());

    log("[distill] Session " + session_id + ": Done (+" +
        std::to_string(result.learnings_stored) + " new, " +
        std::to_string(result.learnings_deduped) + " deduped, " +
        std::to_string(result.triplets_created) + " triplets, " +
        std::to_string(result.citations_linked) + " citations)");

    result.last_line = last_line;
    result.success = true;
    return result;
}

} // namespace chitta
