// Distillation implementation.
// Extracted from simple_cli.cpp.

#include <chitta/distillation.hpp>
#include <chitta/native_distiller.hpp>
#include <chitta/rpc/field_handler.hpp>
#include <chitta/version.hpp>
#ifdef CHITTA_WITH_ONNX
#include <chitta/vak_onnx.hpp>
#endif
#include <iostream>
#include <sstream>
#include <atomic>
#include <shared_mutex>

// Global flags shared with simple_cli.cpp
extern std::atomic<bool> daemon_running;
extern std::atomic<bool> verbose_mode;

namespace chitta {

bool run_distillation(
    FieldStore& field_store,
    VakYantra* yantra,
    const TranscriptState& state,
    const DistillConfig& config,
    FieldRpcHandler* handler,
    bool queue_triggered
) {
    NativeDistillConfig native_config;
    native_config.model = handler ? handler->get_distill_model() : config.model;
    native_config.timeout_secs = 180;
    native_config.min_turns = config.min_turns;
    native_config.verbose = verbose_mode;

    auto embed_fn = [yantra](const std::string& text) -> std::vector<float> {
        if (!yantra) return {};
        try { return yantra->transform(text).nu.data; } catch (...) {}
        return {};
    };
    auto distiller = std::make_unique<NativeDistiller>(field_store, embed_fn, native_config);

    distiller->set_cancel_callback([]() {
        return !daemon_running.load();
    });

    if (verbose_mode) {
        distiller->set_log_callback([](const std::string& msg) {
            std::cerr << msg << "\n";
        });
    }

    // Hold the exclusive rpc lock across distill_session: NativeDistiller writes
    // to field_store directly (episode, learnings, triplets, edges, affect,
    // strengthen) and these MUST be serialized with main-thread RPC writers or
    // the chain-hash invariant breaks ("chain record warning ... resetting
    // chain") and concurrent FFI writes have corrupted the heap (double-free /
    // fasttop). The LLM HTTP call inside takes 10-30s, but with the
    // health_check fast-path readers no longer poll-spam during it.
    DistillResult result;
    {
        std::unique_lock<std::shared_mutex> _lk;
        if (handler) _lk = handler->acquire_lock();
        result = distiller->distill_session(
            state.session_id,
            state.transcript_path,
            state.realm,
            state.last_processed_line,
            queue_triggered
        );

        if (!result.success) {
            if (!result.error.empty())
                std::cerr << "[distill] " << state.session_id << ": " << result.error << "\n";
            return false;
        }

        field_store.emit_event("transcript", "progress",
                               state.session_id,
                               "{\"last_line\":" + std::to_string(result.last_line) + "}");
        field_store.emit_event("transcript", "distilled", state.session_id, "{}");
    }

    if (verbose_mode) {
        std::cerr << "[distill] Completed " << state.session_id
                  << " (line " << result.last_line << ", +"
                  << result.learnings_stored << " new, "
                  << result.learnings_deduped << " deduped, "
                  << result.triplets_created << " triplets)\n";
    }
    return true;
}

std::string generate_stats(FieldStore& field_store, VakYantra* yantra) {
    std::ostringstream oss;
    oss << "{"
        << "\"version\":\"" << CHITTA_VERSION << "\","
        << "\"memories\":" << field_store.memory_count() << ","
        << "\"symbols\":" << field_store.symbol_count() << ","
        << "\"yantra\":" << (yantra ? "true" : "false")
        << "}";
    return oss.str();
}

} // namespace chitta
