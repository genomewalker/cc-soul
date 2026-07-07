#pragma once
// value_fact_extractor: deterministic (regex/adjacency, NO LLM) extraction of
// atomic (identifier, value) facts from a conversation transcript.
//
// Compensates for Tier-2 SSL compression dropping exact value tokens (ssl_prompt.hpp:41-44):
// the LLM distiller narrates decisions but often omits the scalar. This pass runs
// over the SAME conversation text right after store_learnings and emits one atomic
// memory per (distinctive-identifier, precise-value) pair found adjacent in the text.
//
// Pilot result: +0.907 coverage lift, 0.000 degradation (degradation held to zero by
// per-fact store dedup at the write site — this unit only proposes candidates).

#include <string>
#include <vector>

namespace chitta {

struct ValueFact {
    std::string identifier;   // file path, symbol, config key, table, commit token, ...
    std::string value;        // integer/float, size+unit, line N/N-M, 0x.., sha/hex, version
    std::string content;      // "<identifier> <value> | <short predicate/context>" — stored + embedded
};

// Extract atomic value-facts from raw conversation text. Deterministic and pure:
// no store access, no LLM, no I/O. Dedup against the store happens at the call site.
std::vector<ValueFact> extract_value_facts(const std::string& conversation);

} // namespace chitta
