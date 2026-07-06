// Fail-fast probe invariants: the slow lane's whole design rests on a dead
// endpoint being cheap to detect. Assert-style, no framework.
#include <chitta/llm_http.hpp>
#include <cassert>
#include <chrono>
#include <iostream>

int main() {
    using clock = std::chrono::steady_clock;

    // Dead endpoint: probe must return false in well under the 180s distill
    // timeout (curl --max-time 3 on connection-refused).
    auto t0 = clock::now();
    bool up = chitta::probe_endpoint("http://127.0.0.1:9");  // discard port, nothing listens
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - t0).count();
    assert(!up);
    assert(secs < 6);

    // Garbage URL: false, not a hang or throw.
    assert(!chitta::probe_endpoint("http://no-such-host.invalid:11434"));

    std::cout << "llm_probe_test: OK (dead-port probe " << secs << "s)\n";
    return 0;
}
