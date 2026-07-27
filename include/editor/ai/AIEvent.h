#ifndef EDITOR_AI_EVENT_H
#define EDITOR_AI_EVENT_H

#include <string>

enum class AgentEventKind {
    SessionStarted,
    AssistantTextDelta,
    AssistantMessage,
    PlanUpdated,
    ToolCallStarted,
    ToolCallCompleted,
    ApprovalRequested,
    UsageUpdated,
    TurnCompleted,
    TurnFailed,
    SessionClosed,
    Unknown
};

struct AgentEvent {
    AgentEventKind kind = AgentEventKind::Unknown;
    std::string session_id;
    std::string turn_id;
    std::string item_id;
    std::string display_text;
    std::string structured_payload_json = "{}";
};

const char *AgentEventKindName(AgentEventKind kind);

#endif
