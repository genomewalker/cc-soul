#include "../include/chitta/transcript_parser.hpp"
#include <sstream>
#include <unordered_set>

namespace chitta {

using json = nlohmann::json;

std::vector<ConversationTurn> TranscriptParser::parse(
    const std::string& path,
    const TranscriptParseOptions& options,
    int64_t* last_line
) {
    std::vector<ConversationTurn> turns;
    last_error_.clear();
    hit_line_cap_ = false;

    std::ifstream file(path);
    if (!file) {
        last_error_ = "Cannot open file: " + path;
        return turns;
    }

    std::string line;
    int64_t current_line = 0;
    int turn_index = 0;

    // Skip to checkpoint
    while (current_line < options.skip_lines && std::getline(file, line)) {
        current_line++;
    }

    while (std::getline(file, line)) {
        // BOUND: stop after max_lines lines past the checkpoint. The already-read
        // line is left for the next pass (last_line = current_line points before it).
        if (options.max_lines > 0 && current_line - options.skip_lines >= options.max_lines) {
            hit_line_cap_ = true;
            break;
        }
        current_line++;
        if (line.empty()) continue;

        try {
            auto entry = json::parse(line);
            std::string type = entry.value("type", "");

            std::string role;
            std::string content;

            if (type == "user" || type == "assistant") {
                // Claude format: top-level role, content under entry.message.content
                role = type;
                content = extract_content(entry, options.include_thinking, options.filter_system_reminders);
            } else if (type == "response_item" && entry.contains("payload")) {
                // Codex format: role + content nested under payload
                const auto& p = entry["payload"];
                std::string ptype = p.value("type", "");
                std::string prole = p.value("role", "");

                if (ptype == "message" && (prole == "user" || prole == "assistant")) {
                    role = prole;
                    content = extract_content_codex(p, options.filter_system_reminders);
                } else if (options.include_thinking && ptype == "reasoning") {
                    role = "assistant";
                    content = extract_reasoning_codex(p);
                } else if (ptype == "function_call") {
                    role = "assistant";
                    content = extract_function_call_codex(p);
                } else {
                    continue;  // developer role, tool outputs, etc.
                }
            } else {
                continue;  // session_meta, event_msg, unknown
            }

            if (!content.empty()) {
                ConversationTurn turn;
                turn.role = role;
                turn.content = content;
                turn.line_number = current_line;
                turn.turn_index = turn_index++;
                turns.push_back(std::move(turn));
            }
        } catch (const json::exception& e) {
            // Skip malformed lines silently
            continue;
        }
    }

    if (last_line) {
        *last_line = current_line;
    }

    return turns;
}

std::string TranscriptParser::extract_content(const nlohmann::json& entry, bool include_thinking, bool filter_reminders) {
    std::string content;

    if (!entry.contains("message")) return content;

    const auto& msg = entry["message"];
    if (!msg.contains("content")) return content;

    const auto& msg_content = msg["content"];

    if (msg_content.is_string()) {
        content = msg_content.get<std::string>();
        if (filter_reminders) {
            content = filter_system_reminders(content);
        }
    } else if (msg_content.is_array()) {
        for (const auto& block : msg_content) {
            std::string block_type = block.value("type", "");

            if (block_type == "text" && block.contains("text")) {
                std::string text = block["text"].get<std::string>();

                if (filter_reminders) {
                    text = filter_system_reminders(text);
                }

                // Skip if only whitespace remains
                if (text.find_first_not_of(" \t\n\r") == std::string::npos) continue;

                if (!content.empty()) content += "\n";
                content += text;
            }
            else if (include_thinking && block_type == "thinking" && block.contains("thinking")) {
                std::string thinking = block["thinking"].get<std::string>();
                // Only include substantial thinking (>100 chars)
                if (thinking.size() > 100) {
                    if (!content.empty()) content += "\n";
                    content += "<thinking>\n" + thinking + "\n</thinking>";
                }
            }
            else if (block_type == "tool_use" && block.contains("name") && block.contains("input")) {
                std::string tool_name = block.value("name", "");
                static const std::unordered_set<std::string> ops = {"Bash","Read","Write","Edit","Glob","Grep"};
                if (ops.count(tool_name)) {
                    std::string input_str;
                    for (const auto& key : {"command", "file_path", "pattern"}) {
                        if (block["input"].contains(key)) {
                            if (!input_str.empty()) input_str += " ";
                            std::string val = block["input"][key].get<std::string>();
                            if (val.size() > 200) val = val.substr(0, 200);
                            input_str += std::string(key) + "=" + val;
                        }
                    }
                    if (!input_str.empty()) {
                        if (!content.empty()) content += "\n";
                        content += "[tool:" + tool_name + "] " + input_str;
                    }
                }
            }
            // Skip tool_result - just noise for distillation
        }
    }

    return content;
}

std::string TranscriptParser::extract_content_codex(const nlohmann::json& payload, bool filter_reminders) {
    std::string content;
    if (!payload.contains("content")) return content;
    const auto& c = payload["content"];
    if (!c.is_array()) return content;

    for (const auto& block : c) {
        std::string btype = block.value("type", "");
        if ((btype == "input_text" || btype == "output_text") && block.contains("text")) {
            std::string text = block["text"].get<std::string>();
            if (filter_reminders) text = filter_system_reminders(text);
            if (text.find_first_not_of(" \t\n\r") == std::string::npos) continue;
            if (!content.empty()) content += "\n";
            content += text;
        }
    }
    return content;
}

std::string TranscriptParser::extract_reasoning_codex(const nlohmann::json& payload) {
    std::string content;
    if (payload.contains("summary") && payload["summary"].is_array()) {
        for (const auto& s : payload["summary"]) {
            if (s.is_object() && s.contains("text")) {
                std::string t = s["text"].get<std::string>();
                if (t.size() > 100) {
                    if (!content.empty()) content += "\n";
                    content += t;
                }
            }
        }
    }
    if (content.empty()) return "";
    return "<thinking>\n" + content + "\n</thinking>";
}

std::string TranscriptParser::extract_function_call_codex(const nlohmann::json& payload) {
    std::string name = payload.value("name", "");
    if (name.empty()) return "";
    static const std::unordered_set<std::string> ops = {"Bash","Read","Write","Edit","Glob","Grep"};
    if (!ops.count(name)) return "";
    std::string args = payload.value("arguments", "");
    if (args.size() > 200) args = args.substr(0, 200);
    return "[tool:" + name + "] " + args;
}

std::string TranscriptParser::filter_system_reminders(const std::string& text) {
    std::string result = text;
    size_t pos;

    while ((pos = result.find("<system-reminder>")) != std::string::npos) {
        size_t end = result.find("</system-reminder>", pos);
        if (end != std::string::npos) {
            result.erase(pos, end - pos + 18); // 18 = length of "</system-reminder>"
        } else {
            result.erase(pos);
        }
    }

    return result;
}

std::string TranscriptParser::build_conversation(
    const std::vector<ConversationTurn>& turns,
    const TruncationParams& params
) {
    std::ostringstream ss;

    for (const auto& turn : turns) {
        ss << "[" << turn.role << "]\n" << turn.content << "\n\n";
    }

    std::string conversation = ss.str();

    // Apply smart truncation if needed
    if (conversation.size() > params.max_chars) {
        std::string head_part = conversation.substr(0, params.head_chars);
        std::string tail_part = conversation.substr(conversation.size() - params.tail_chars);

        std::ostringstream truncated;
        truncated << head_part
                  << "\n\n[... truncated " << conversation.size() << " chars, keeping first "
                  << params.head_chars << " + last " << params.tail_chars << " ...]\n\n"
                  << tail_part;

        return truncated.str();
    }

    return conversation;
}

} // namespace chitta
