// GradMemWriter implementation — launches gradmemd subprocess,
// reads result, stores gradmem_snapshot in chitta-field.

#include <chitta/gradmem_writer.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <chrono>
#include <climits>

using json = nlohmann::json;

namespace chitta {

std::string GradMemWriter::build_job_json(
    const std::string& session_id,
    const std::string& transcript_text,
    const std::string& realm
) const {
    json job = {
        {"session_id",    session_id},
        {"realm",         realm},
        {"text",          transcript_text},
        {"model_ts",      config_.model_ts_path},
        {"gguf",          config_.gguf_path},
        {"n_mem_tokens",  config_.n_mem_tokens},
        {"K",             config_.K},
        {"inner_lr",      config_.inner_lr},
        {"max_ctx_tokens", config_.max_ctx_tokens},
    };
    return job.dump();
}

GradMemResult GradMemWriter::parse_result(const std::string& json_str) const {
    GradMemResult r;
    try {
        auto j = json::parse(json_str);
        if (j.contains("error")) {
            r.error = j["error"].get<std::string>();
            return r;
        }
        r.session_id     = j.value("session_id", "");
        r.M_fp16_b64     = j.value("M_fp16_b64", "");
        r.write_loss     = j.value("write_loss", 0.0f);
        r.n_mem_tokens   = j.value("n_mem_tokens", 0);
        r.d_model        = j.value("d_model", (int64_t)0);
        r.K              = j.value("K", 0);
        if (j.contains("proxy_embedding") && j["proxy_embedding"].is_array())
            r.proxy_embedding = j["proxy_embedding"].get<std::vector<float>>();
        r.success = !r.M_fp16_b64.empty() && r.n_mem_tokens > 0;
    } catch (const std::exception& e) {
        r.error = std::string("parse error: ") + e.what();
    }
    return r;
}

GradMemResult GradMemWriter::write_sync(
    const std::string& session_id,
    const std::string& transcript_text,
    const std::string& realm
) {
    GradMemResult r;
    r.session_id = session_id;

    if (!is_enabled()) {
        r.error = "gradmem disabled";
        return r;
    }

    std::string job = build_job_json(session_id, transcript_text, realm);

    // stdin pipe: parent writes job JSON → child reads it
    int stdin_pipe[2];
    // stdout pipe: child writes result JSON → parent reads it
    int stdout_pipe[2];

    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
        r.error = "pipe() failed";
        return r;
    }

    pid_t pid = fork();
    if (pid < 0) {
        r.error = "fork() failed";
        close(stdin_pipe[0]);  close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return r;
    }

    if (pid == 0) {
        // Child: redirect stdin/stdout, exec gradmemd
        dup2(stdin_pipe[0],  STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]);  close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        // Redirect stderr to /dev/null to avoid polluting daemon logs
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        execl(config_.gradmemd_path.c_str(), "gradmemd", nullptr);
        // exec failed — write error to stdout and exit
        const char* err = R"({"error":"execl failed"})";
        write(STDOUT_FILENO, err, strlen(err));
        _exit(1);
    }

    // Parent: write job to child stdin, read result from stdout
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    // Write job JSON
    ssize_t written = write(stdin_pipe[1], job.c_str(), job.size());
    (void)written;
    close(stdin_pipe[1]);  // EOF signals end of input to child

    // Read result (with 5-minute timeout)
    std::string result_buf;
    char buf[4096];
    ssize_t n;
    auto start = std::chrono::steady_clock::now();
    while ((n = read(stdout_pipe[0], buf, sizeof(buf))) > 0) {
        result_buf.append(buf, n);
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > std::chrono::minutes(5)) {
            kill(pid, SIGKILL);
            r.error = "gradmemd timed out (5 min)";
            close(stdout_pipe[0]);
            waitpid(pid, nullptr, 0);
            return r;
        }
    }
    close(stdout_pipe[0]);

    int wstatus = 0;
    waitpid(pid, &wstatus, 0);

    if (result_buf.empty()) {
        r.error = "gradmemd produced no output";
        return r;
    }

    r = parse_result(result_buf);
    r.session_id = session_id;
    return r;
}

void GradMemWriter::write_async(
    const std::string& session_id,
    const std::string& transcript_text,
    const std::string& realm
) {
    if (!is_enabled()) return;

    // Double-fork: grandchild is fully detached (no zombie risk)
    pid_t pid = fork();
    if (pid < 0) { log("[gradmem] fork failed"); return; }
    if (pid > 0) { waitpid(pid, nullptr, 0); return; }  // parent reaps intermediate child immediately

    // Intermediate child: fork grandchild then exit
    pid_t gchild = fork();
    if (gchild < 0) _exit(1);
    if (gchild > 0) _exit(0);  // intermediate exits, grandchild is reparented to init

    // Grandchild: do the actual work (fully detached)
    auto result = write_sync(session_id, transcript_text, realm);
    if (result.success) {
        // Re-open field store connection in the child process would be complex.
        // Instead: write result to a temp file for the daemon to pick up.
        std::string tmp_path = "/tmp/gradmem_result_" + session_id + ".json";
        FILE* f = fopen(tmp_path.c_str(), "w");
        if (f) {
            json j = {
                {"session_id",     result.session_id},
                {"realm",          realm},
                {"M_fp16_b64",     result.M_fp16_b64},
                {"write_loss",     result.write_loss},
                {"n_mem_tokens",   result.n_mem_tokens},
                {"d_model",        result.d_model},
                {"K",              result.K},
                {"proxy_embedding", result.proxy_embedding},
            };
            fprintf(f, "%s\n", j.dump().c_str());
            fclose(f);
            // Enqueue result for the daemon to persist via the queue file
            // (The daemon's queue processor picks up gradmem_result events)
            std::string queue_path = "/tmp/chitta-queue.jsonl";
            FILE* q = fopen(queue_path.c_str(), "a");
            if (q) {
                json qmsg = {
                    {"tool", "gradmem_result"},
                    {"args", {
                        {"result_path", tmp_path},
                        {"session_id",  session_id},
                        {"realm",       realm},
                    }}
                };
                fprintf(q, "%s\n", qmsg.dump().c_str());
                fclose(q);
            }
        }
    }
    _exit(0);
}

void GradMemWriter::store_result(const GradMemResult& result) {
    if (!result.success || !field_store_) return;

    // Build content JSON — the M blob + metadata as memory content
    json content_j = {
        {"kind",         "gradmem_snapshot"},
        {"session_id",   result.session_id},
        {"M_fp16_b64",   result.M_fp16_b64},
        {"write_loss",   result.write_loss},
        {"n_mem_tokens", result.n_mem_tokens},
        {"d_model",      result.d_model},
        {"K",            result.K},
    };
    std::string content = content_j.dump();

    // Use proxy embedding as the HNSW vector (mean of M rows)
    auto& proxy = result.proxy_embedding;

    try {
        uint64_t memory_id = field_store_->remember(
            "gradmem_snapshot",   // kind
            result.session_id,    // realm (session-scoped)
            content,
            proxy,
            0.7f,    // confidence
            0.001f   // very slow decay — snapshots should persist
        );
        log("[gradmem] stored snapshot id=" + std::to_string(memory_id)
            + " loss=" + std::to_string(result.write_loss)
            + " session=" + result.session_id);
    } catch (const std::exception& e) {
        log(std::string("[gradmem] store_result failed: ") + e.what());
    }
}

} // namespace chitta
