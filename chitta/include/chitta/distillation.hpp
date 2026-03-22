#pragma once
// Distillation helpers: run_distillation, generate_stats.
// Extracted from simple_cli.cpp.

#include <chitta/field_store.hpp>
#include <chitta/native_distiller.hpp>
#include <chitta/daemon_config.hpp>
#include <string>

namespace chitta {
class VakYantra;
class FieldRpcHandler;

bool run_distillation(
    FieldStore& field_store,
    VakYantra* yantra,
    const TranscriptState& state,
    const DistillConfig& config,
    FieldRpcHandler* handler = nullptr,
    bool queue_triggered = false
);

std::string generate_stats(FieldStore& field_store, VakYantra* yantra);

} // namespace chitta
