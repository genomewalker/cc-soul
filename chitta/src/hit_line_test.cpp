// Guards the frozen recall hit-line contract that hooks/prompt-core.sh and the
// outcome-ledger tap parse. Compiles the shipped hit_line(), not a copy.
#include <cassert>
#include <cstdio>

#include <chitta/hit_line.hpp>

using chitta::hit_line;

int main() {
    // Full lane (recall): id, pct, type, date, text — the widest form.
    // 1735689600000 ms = 2025-01-01T00:00:00Z.
    assert(hit_line("42", 87, "wisdom", 1735689600000LL, "the text", true, true) ==
           "#42 [87%] [wisdom] (on: 2025-01-01) the text\n");

    // Keyword/hybrid lanes drop the type; nothing else about the line moves.
    assert(hit_line("42", 87, "wisdom", 1735689600000LL, "the text", false, false) ==
           "#42 [87%] the text\n");

    // smart_recall / full_resonate: type, no date.
    assert(hit_line("7", 100, "episode", 0, "x", true, false) == "#7 [100%] [episode] x\n");

    // ts_ms <= 0 means "unknown", so the date field is omitted even when asked for.
    assert(hit_line("7", 0, "episode", 0, "x", true, true) == "#7 [0%] [episode] x\n");
    assert(hit_line("7", 0, "episode", -1, "x", true, true) == "#7 [0%] [episode] x\n");

    // The id prefix must survive every combination — losing it silently kills
    // exposure/credit tracking downstream, which is the bug this test exists for.
    for (bool show_type : {false, true})
        for (bool show_date : {false, true})
            assert(hit_line("999", 5, "belief", 1735689600000LL, "t", show_type, show_date)
                       .rfind("#999 ", 0) == 0);

    // Text is truncated at kHitTextChars, and the trailing newline is still there.
    const std::string long_text(chitta::kHitTextChars + 50, 'a');
    const std::string line = hit_line("1", 50, "x", 0, long_text, false, false);
    assert(line.size() == std::string("#1 [50%] ").size() + chitta::kHitTextChars + 1);
    assert(line.back() == '\n');

    // Shorter-than-limit text is not padded.
    assert(hit_line("1", 50, "x", 0, "ab", false, false) == "#1 [50%] ab\n");

    std::printf("hit_line_test: all assertions passed\n");
    return 0;
}
