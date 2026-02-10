#pragma once
// TranscriptParser: Stream JSONL transcript files with smart truncation
//
// Features:
// - Line-by-line streaming (handles any file size)
// - Extracts user/assistant text content
// - Filters out <system-reminder> noise
// - Smart truncation preserving head + tail

#include <string>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>

namespace chitta {

struct ConversationTurn {
    std::string role;       // "user" or "assistant"
    std::string content;    // Text only (filtered from content array)
    int64_t line_number;    // Source line in JSONL
    int turn_index;
};

struct TranscriptParseOptions {
    int64_t skip_lines = 0;              // Skip first N lines (resume from checkpoint)
    bool include_thinking = true;        // Include <thinking> blocks (>100 chars only)
    bool filter_system_reminders = true; // Remove <system-reminder> tags
};

struct TruncationParams {
    size_t max_chars = 80000;
    size_t head_chars = 25000;
    size_t tail_chars = 55000;
};

class TranscriptParser {
public:
    // Parse JSONL transcript file, extract conversation turns
    // Returns turns and sets last_line to the final line number processed
    std::vector<ConversationTurn> parse(
        const std::string& path,
        const TranscriptParseOptions& options,
        int64_t* last_line = nullptr
    );

    // Overload with default options
    std::vector<ConversationTurn> parse(const std::string& path, int64_t* last_line = nullptr) {
        return parse(path, TranscriptParseOptions{}, last_line);
    }

    // Build formatted conversation string with smart truncation
    // Format: [user]\ncontent\n\n[assistant]\ncontent\n\n...
    static std::string build_conversation(
        const std::vector<ConversationTurn>& turns,
        const TruncationParams& params
    );

    // Overload with default params
    static std::string build_conversation(const std::vector<ConversationTurn>& turns) {
        return build_conversation(turns, TruncationParams{});
    }

    // Get last error message
    const std::string& last_error() const { return last_error_; }

private:
    std::string last_error_;

    // Extract text content from message JSON
    static std::string extract_content(const nlohmann::json& entry, bool include_thinking, bool filter_reminders);

    // Filter out <system-reminder> tags from text
    static std::string filter_system_reminders(const std::string& text);
};

} // namespace chitta
