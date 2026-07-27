#ifndef EDITOR_AI_EDITOR_SERVICE_H
#define EDITOR_AI_EDITOR_SERVICE_H

#include "editor/ai/AIEvent.h"
#include "editor/ai/AIReadOnlyTools.h"
#include "editor/bridge/EditorBridgeClient.h"
#include <filesystem>
#include <string>
#include <vector>

enum class AIEditorServiceState {
    Stopped,
    StartingHost,
    Ready,
    StartingSession,
    Idle,
    RunningTurn,
    Cancelling,
    Error
};

struct AIConversationMessage {
    std::string role;
    std::string text;
};

struct AIAgentProviderStatus {
    std::string provider;
    bool installed = false;
    bool authenticated = false;
    std::string auth_status;
    std::string version;
    bool supports_json_stream = false;
    bool supports_session_resume = false;
    bool supports_mcp = false;
    std::string error;
};

class AIEditorService {
public:
    AIEditorService() = default;
    ~AIEditorService();

    AIEditorService(const AIEditorService &) = delete;
    AIEditorService &operator=(const AIEditorService &) = delete;

    bool Start(const std::filesystem::path &project_root);
    void Stop();
    void SetEditorContext(const AIEditorContext &context);
    void Update();

    bool ProbeAgents();
    bool StartSession(const std::string &provider);
    bool StartFakeSession();
    bool SendMessage(const std::string &message);
    bool CancelTurn();
    bool CloseSession();

    AIEditorServiceState GetState() const;
    const char *GetStateLabel() const;
    const std::string &GetLastError() const;
    const std::string &GetActiveProvider() const;
    const std::string &GetActiveSessionID() const;
    const std::string &GetStreamingAssistantText() const;
    const std::vector<AgentEvent> &GetEvents() const;
    const std::vector<AIConversationMessage> &GetConversation() const;
    const std::vector<AIAgentProviderStatus> &GetProviderStatuses() const;
    const std::string &GetMcpEndpoint() const;
    bool HasActiveSession() const;
    bool CanSendMessage() const;

private:
    void HandleBridgeMessage(const EditorBridge::Message &message);
    void HandleAgentEventPayload(const std::string &payload_json);
    void HandleAgentProbePayload(const std::string &payload_json);
    void HandleMcpExecuteRequest(const std::string &payload_json);
    void RecordAgentEvent(AgentEvent event);
    void SetError(const std::string &error);

    EditorBridgeClient bridge_;
    AIEditorServiceState state_ = AIEditorServiceState::Stopped;
    std::filesystem::path project_root_;
    std::string last_error_;
    std::string active_provider_;
    std::string active_session_id_;
    std::string streaming_assistant_text_;
    std::string mcp_endpoint_;
    std::vector<AgentEvent> events_;
    std::vector<AIConversationMessage> conversation_;
    std::vector<AIAgentProviderStatus> provider_statuses_;
    AIEditorContext editor_context_;
};

#endif
