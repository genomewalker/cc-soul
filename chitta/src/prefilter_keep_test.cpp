// Keep-rule check for the recall-biased pre-filter. The block below is
// EXTRACTED VERBATIM from field_memory_recall.cpp between PREFILTER_BEGIN and
// PREFILTER_END — this compiles the shipped source, not a copy.
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cassert>
namespace {
// PREFILTER_BEGIN — self-contained (scalars + <vector>/<string> only) so the
// keep-rule check can compile this block verbatim without the daemon.
struct PrefilterCand {
    float       score;     // fused score after the term-overlap rescore
    bool        has_lex;   // >=1 distinctive query token literally present
    bool        neighbor;  // 1-hop assoc neighbour of a top-3 hit
    std::string realm;
    std::string kind;
};

// `c` is in RANK order (the order the caller would return). Keep rules:
//   (a) rank < limit                       — never drop what we'd have returned
//   (b) has_lex                            — literal query-term evidence
//   (c) realm+kind of a top-5 hit AND score >= 0.5 * max   — same answer class
//   (d) neighbor                           — graph-adjacent to the head
// The budget caps the survivor count; because we walk in rank order the ones
// dropped at the cap are the lowest-ranked survivors. budget is floored at
// `limit` so the filter can never return fewer hits than the response limit.
std::vector<char> prefilter_keep(const std::vector<PrefilterCand>& c,
                                 size_t limit, size_t budget) {
    std::vector<char> keep(c.size(), 0);
    if (c.empty()) return keep;
    if (budget < limit) budget = limit;

    float max_score = c[0].score;
    for (const auto& x : c) max_score = std::max(max_score, x.score);
    const float half = 0.5f * max_score;

    std::vector<std::pair<std::string, std::string>> head_class;
    for (size_t i = 0; i < c.size() && i < 5; ++i) {
        std::pair<std::string, std::string> k{c[i].realm, c[i].kind};
        bool seen = false;
        for (const auto& h : head_class) if (h == k) { seen = true; break; }
        if (!seen) head_class.push_back(std::move(k));
    }
    auto in_head_class = [&](const PrefilterCand& x) {
        for (const auto& h : head_class)
            if (h.first == x.realm && h.second == x.kind) return true;
        return false;
    };

    size_t kept = 0;
    for (size_t i = 0; i < c.size(); ++i) {
        const bool hit = (i < limit)
                      || c[i].has_lex
                      || c[i].neighbor
                      || (max_score > 0.0f && c[i].score >= half && in_head_class(c[i]));
        if (!hit) continue;
        if (kept >= budget) break;
        keep[i] = 1;
        ++kept;
    }
    return keep;
}
// PREFILTER_END
} // namespace

static std::vector<char> run(const std::vector<PrefilterCand>& c, size_t limit, size_t budget) {
    return prefilter_keep(c, limit, budget);
}
static size_t kept_n(const std::vector<char>& k) {
    size_t n = 0; for (char x : k) n += (x != 0); return n;
}

int main() {
    // Head of the pool: top-5 define the (realm,kind) head classes.
    // 60 candidates, monotonically decreasing score.
    std::vector<PrefilterCand> pool;
    for (int i = 0; i < 60; ++i)
        pool.push_back({1.0f - 0.01f * i, false, false, "cc-soul", "insight"});

    // (a) rank < limit always survives, even with every other rule dead.
    {
        std::vector<PrefilterCand> p = pool;
        for (auto& x : p) { x.realm = "other"; x.kind = "episode"; }
        for (int i = 0; i < 5; ++i) { p[i].realm = "cc-soul"; p[i].kind = "insight"; }
        // p[5..] are class-mismatched, lexless, non-neighbour -> only rule (a) can fire.
        auto k = run(p, 10, 24);
        for (int i = 0; i < 10; ++i) assert(k[i] == 1 && "rule (a): top-limit must survive");
        assert(kept_n(k) == 10 && "nothing but rule (a) should have fired");
    }

    // (b) one distinctive query token rescues a deep, low-scoring candidate.
    {
        std::vector<PrefilterCand> p = pool;
        for (auto& x : p) { x.realm = "other"; x.kind = "episode"; }
        for (int i = 0; i < 5; ++i) { p[i].realm = "cc-soul"; p[i].kind = "insight"; }
        p[52].has_lex = true;   // the gold at union rank 52 from the pool-60 run
        auto k = run(p, 10, 24);
        assert(k[52] == 1 && "rule (b): lexical hit at rank 52 must survive the cut");
        assert(k[51] == 0 && "no rule fires for its neighbour");
    }

    // (c) realm+kind of a head hit, at >= 0.5 * max. The 0.5 boundary is
    //     inclusive; just below it the rule must NOT fire.
    {
        std::vector<PrefilterCand> p = pool;      // all share the head class
        for (auto& x : p) x.score = 0.2f;         // below half of max
        p[0].score = 1.0f;
        p[30].score = 0.5f;                       // exactly 0.5 * max -> keep
        p[31].score = 0.49f;                      // just under -> drop
        auto k = run(p, 10, 24);
        assert(k[30] == 1 && "rule (c): score == 0.5*max is inclusive");
        assert(k[31] == 0 && "rule (c): score < 0.5*max must not fire");
    }

    // (c) must not fire across a realm boundary even at a high score.
    {
        std::vector<PrefilterCand> p = pool;
        p[40].realm = "other-project";
        p[40].score = 0.99f;
        auto k = run(p, 10, 24);
        assert(k[40] == 0 && "rule (c): different realm is not the head class");
    }

    // (d) graph neighbour of a top-3 hit survives with no lexical/class evidence.
    {
        std::vector<PrefilterCand> p = pool;
        for (auto& x : p) { x.realm = "other"; x.kind = "episode"; x.score = 0.01f; }
        for (int i = 0; i < 5; ++i) { p[i].realm = "cc-soul"; p[i].kind = "insight"; }
        p[0].score = 1.0f;
        p[43].neighbor = true;   // the gold at union rank 43
        auto k = run(p, 10, 24);
        assert(k[43] == 1 && "rule (d): 1-hop neighbour must survive");
    }

    // Budget caps the survivor count, and it caps by RANK (weakest go first).
    {
        std::vector<PrefilterCand> p = pool;
        for (auto& x : p) x.has_lex = true;   // every candidate qualifies
        auto k = run(p, 10, 24);
        assert(kept_n(k) == 24 && "budget must cap the survivor set");
        for (int i = 0; i < 24; ++i) assert(k[i] == 1 && "cap keeps the highest-ranked");
        for (int i = 24; i < 60; ++i) assert(k[i] == 0);
    }

    // Budget is floored at limit: never return fewer than the response limit.
    {
        std::vector<PrefilterCand> p = pool;
        for (auto& x : p) { x.realm = "other"; x.kind = "episode"; }
        auto k = run(p, 20, 5);
        assert(kept_n(k) == 20 && "budget < limit must be floored at limit");
    }

    // Degenerate inputs.
    {
        std::vector<PrefilterCand> empty;
        assert(run(empty, 10, 24).empty());
        std::vector<PrefilterCand> zeros(30, {0.0f, false, false, "r", "k"});
        auto k = run(zeros, 10, 24);
        assert(kept_n(k) == 10 && "all-zero scores must not make rule (c) fire for everyone");
    }

    std::printf("prefilter keep-rule check: all assertions passed\n");
    return 0;
}
