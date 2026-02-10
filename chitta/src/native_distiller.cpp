#include "../include/chitta/native_distiller.hpp"
#include "../include/chitta/ssl_prompt.hpp"
#include <array>
#include <cstdio>
#include <sstream>
#include <chrono>
#include <thread>

// POSIX
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

namespace chitta {

NativeDistiller::NativeDistiller(DuckDBMind& mind, const NativeDistillConfig& config)
    : mind_(mind), config_(config) {}

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

void NativeDistiller::log(const std::string& msg) {
    if (log_callback_) {
        log_callback_(msg);
    } else if (config_.verbose) {
        std::cerr << msg << "\n";
    }
}

std::string NativeDistiller::call_opencode(const std::string& prompt) {
    // Create pipes for stdin and stdout
    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        return "";
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return "";
    }

    if (pid == 0) {
        // Child process
        close(stdin_pipe[1]);  // Close write end of stdin
        close(stdout_pipe[0]); // Close read end of stdout

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        // Redirect stderr to /dev/null
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        // Execute opencode run -m <model>
        execlp("opencode", "opencode", "run", "-m", config_.model.c_str(), nullptr);
        _exit(1);
    }

    // Parent process
    close(stdin_pipe[0]);   // Close read end of stdin
    close(stdout_pipe[1]);  // Close write end of stdout

    // Write prompt to stdin
    ssize_t written = 0;
    size_t total = prompt.size();
    const char* data = prompt.data();
    while (written < static_cast<ssize_t>(total)) {
        ssize_t n = write(stdin_pipe[1], data + written, total - written);
        if (n <= 0) break;
        written += n;
    }
    close(stdin_pipe[1]);  // Signal EOF

    // Read stdout with timeout
    std::string output;
    std::array<char, 4096> buffer;

    // Set non-blocking read
    int flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

    auto start = std::chrono::steady_clock::now();
    bool finished = false;

    while (!finished) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= config_.timeout_secs) {
            // Timeout
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            close(stdout_pipe[0]);
            log("[distill] Timeout after " + std::to_string(config_.timeout_secs) + "s");
            return "";
        }

        // Check if child finished
        int status;
        int result = waitpid(pid, &status, WNOHANG);
        if (result > 0) {
            finished = true;
        } else if (result < 0) {
            finished = true;
        }

        // Read available data
        ssize_t n;
        while ((n = read(stdout_pipe[0], buffer.data(), buffer.size())) > 0) {
            output.append(buffer.data(), n);
        }

        if (!finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Final read
    ssize_t n;
    while ((n = read(stdout_pipe[0], buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), n);
    }

    close(stdout_pipe[0]);
    return output;
}

int NativeDistiller::store_learnings(
    const SSLParser::Result& result,
    const std::string& session_id,
    const std::string& realm,
    int64_t episode_id
) {
    int stored = 0;

    for (const auto& learning : result.learnings) {
        float confidence = category_to_confidence(learning.category);

        // Combine title and content
        std::string full_text = learning.title + "\n" + learning.content;

        // Store via DuckDBMind::remember (handles embedding internally)
        NodeId nid = mind_.remember(
            full_text,
            NodeType::Wisdom,
            realm,
            RealmVisibility::Private,
            confidence
        );

        int64_t id = static_cast<int64_t>(nid.low);
        if (nid.low != 0) {
            stored++;
            log("[distill]   +" + learning.category + ": " + learning.title.substr(0, 60) + "...");

            // Link to episode if we have one
            if (episode_id > 0) {
                mind_.connect(std::to_string(id), "derived_from", std::to_string(episode_id));
            }
        }
    }

    // Store triplets
    for (const auto& triplet : result.triplets) {
        if (mind_.connect(triplet.subject, triplet.predicate, triplet.object)) {
            log("[distill]   triplet: " + triplet.subject + "→" + triplet.predicate + "→" + triplet.object);
        }
    }

    return stored;
}

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
    int end_turn = turns.empty() ? 0 : turns.back().turn_index;

    // 6. Create dialogue episode
    int64_t episode_id = mind_.store().create_dialogue_episode(
        session_id,
        "Distillation " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()),
        start_turn,
        "distillation",
        realm
    );

    if (episode_id > 0) {
        // Set end_turn
        std::ostringstream sql;
        sql << "UPDATE dialogue_episode SET end_turn = " << end_turn
            << ", turn_count = " << (end_turn - start_turn + 1)
            << ", outcome = 'completed' WHERE id = " << episode_id;
        mind_.store().execute_raw(sql.str());
        result.episode_id = episode_id;
        log("[distill]   Episode: " + std::to_string(episode_id) +
            " (turns " + std::to_string(start_turn) + "-" + std::to_string(end_turn) + ")");
    }

    // 7. Call opencode for distillation
    log("[distill] Calling OpenCode (" + config_.model + ")...");
    auto llm_output = call_opencode(prompt);

    if (llm_output.empty()) {
        result.error = "No result from OpenCode";
        return result;
    }

    // 8. Parse SSL output
    log("[distill] Processing SSL results...");
    auto ssl_result = ssl_parser_.parse(llm_output);

    // 9. Store learnings
    result.learnings_stored = store_learnings(ssl_result, session_id, realm, episode_id);
    result.triplets_created = static_cast<int>(ssl_result.triplets.size());

    log("[distill] Session " + session_id + ": Done (+" +
        std::to_string(result.learnings_stored) + " learnings, " +
        std::to_string(result.triplets_created) + " triplets)");

    result.last_line = last_line;
    result.success = true;
    return result;
}

} // namespace chitta
