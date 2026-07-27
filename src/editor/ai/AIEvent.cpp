#include "editor/ai/AIEvent.h"

const char *AgentEventKindName(AgentEventKind kind) {
    switch (kind) {
    case AgentEventKind::SessionStarted:
        return "Session started";
    case AgentEventKind::AssistantTextDelta:
        return "Assistant text";
    case AgentEventKind::AssistantMessage:
        return "Assistant message";
    case AgentEventKind::PlanUpdated:
        return "Plan updated";
    case AgentEventKind::ToolCallStarted:
        return "Tool call started";
    case AgentEventKind::ToolCallCompleted:
        return "Tool call completed";
    case AgentEventKind::ApprovalRequested:
        return "Approval requested";
    case AgentEventKind::UsageUpdated:
        return "Usage updated";
    case AgentEventKind::TurnCompleted:
        return "Turn completed";
    case AgentEventKind::TurnFailed:
        return "Turn failed";
    case AgentEventKind::SessionClosed:
        return "Session closed";
    case AgentEventKind::Unknown:
        return "Unknown";
    }
    return "Unknown";
}
