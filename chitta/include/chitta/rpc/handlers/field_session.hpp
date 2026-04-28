// Included into FieldRpcHandler class body — declarations only.
// Bodies live in chitta/src/handlers/field_session.cpp.

    ToolResult tool_transcript_register(const json& params);
    ToolResult tool_transcript_get(const json& params);
    ToolResult tool_transcript_list(const json& params);
    ToolResult tool_transcript_update(const json& params);
    ToolResult tool_transcript_remove(const json& params);
    ToolResult tool_transcript_parse(const json& params);
    ToolResult tool_transcript_search(const json& params);
    ToolResult tool_read_transcript(const json& params);
    ToolResult tool_get_turns(const json& params);
    ToolResult tool_create_episode(const json& params);
    ToolResult tool_msg_send(const json& params);
    ToolResult tool_msg_inbox(const json& params);
    ToolResult tool_msg_respond(const json& params);
    ToolResult tool_msg_ack(const json& params);
    ToolResult tool_msg_ack_all(const json& params);
    ToolResult tool_msg_history(const json& params);
    ToolResult tool_session_register(const json& params);
    ToolResult tool_session_heartbeat(const json& params);
    ToolResult tool_session_list(const json& params);
    ToolResult tool_session_deregister(const json& params);
    ToolResult tool_session_sync(const json& params);
