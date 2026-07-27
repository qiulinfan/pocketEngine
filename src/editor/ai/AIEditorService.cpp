#include "editor/ai/AIEditorService.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <algorithm>
#include <cstdlib>

#ifndef POCKET_ENGINE_SOURCE_DIR
#define POCKET_ENGINE_SOURCE_DIR "."
#endif

namespace {

constexpr std::size_t kMaximumRetainedEvents = 500;
constexpr std::size_t kMaximumRetainedMessages = 200;

std::string ReadEnvironmentString(const char *name) {
    const char *value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

std::filesystem::path ResolveNodeExecutable() {
    const std::string override_path = ReadEnvironmentString("POCKET_AGENT_NODE");
    return override_path.empty() ? std::filesystem::path("node")
                                 : std::filesystem::path(override_path);
}

std::filesystem::path ResolveHostEntry() {
    const std::string override_path =
        ReadEnvironmentString("POCKET_AGENT_HOST_ENTRY");
    if (!override_path.empty()) return std::filesystem::path(override_path);
    return std::filesystem::path(POCKET_ENGINE_SOURCE_DIR) / "tools" /
           "pocket-agent-host" / "dist" / "index.js";
}

std::string BuildStringPayload(const char *key, const std::string &value) {
    rapidjson::Document document(rapidjson::kObjectType);
    rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
    document.AddMember(rapidjson::Value(key, allocator),
                       rapidjson::Value(value.c_str(), allocator), allocator);
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);
    return buffer.GetString();
}

std::string BuildProviderPayload(const std::string &provider) {
    return BuildStringPayload("provider", provider);
}

std::string BuildMcpCompletionPayload(const std::string &request_id,
                                      bool ok,
                                      const std::string &result_json,
                                      const std::string &error_code,
                                      const std::string &error_message) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("request_id");
    writer.String(request_id.c_str());
    writer.Key("ok");
    writer.Bool(ok);
    if (ok) {
        writer.Key("result");
        writer.RawValue(result_json.c_str(), result_json.size(),
                        rapidjson::kObjectType);
    } else {
        writer.Key("error");
        writer.StartObject();
        writer.Key("code");
        writer.String(error_code.c_str());
        writer.Key("message");
        writer.String(error_message.c_str());
        writer.EndObject();
    }
    writer.EndObject();
    return buffer.GetString();
}

std::string SerializeValue(const rapidjson::Value &value) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);
    return buffer.GetString();
}

std::string ReadOptionalString(const rapidjson::Value &object,
                               const char *member_name) {
    if (!object.IsObject() || !object.HasMember(member_name) ||
        !object[member_name].IsString()) {
        return "";
    }
    return object[member_name].GetString();
}

AgentEventKind ParseAgentEventKind(const std::string &kind) {
    if (kind == "session_started") return AgentEventKind::SessionStarted;
    if (kind == "assistant_text_delta") {
        return AgentEventKind::AssistantTextDelta;
    }
    if (kind == "assistant_message") return AgentEventKind::AssistantMessage;
    if (kind == "plan_updated") return AgentEventKind::PlanUpdated;
    if (kind == "tool_call_started") return AgentEventKind::ToolCallStarted;
    if (kind == "tool_call_completed") {
        return AgentEventKind::ToolCallCompleted;
    }
    if (kind == "approval_requested") {
        return AgentEventKind::ApprovalRequested;
    }
    if (kind == "usage_updated") return AgentEventKind::UsageUpdated;
    if (kind == "turn_completed") return AgentEventKind::TurnCompleted;
    if (kind == "turn_failed") return AgentEventKind::TurnFailed;
    if (kind == "session_closed") return AgentEventKind::SessionClosed;
    return AgentEventKind::Unknown;
}

} // namespace

AIEditorService::~AIEditorService() {
    Stop();
}

bool AIEditorService::Start(const std::filesystem::path &project_root) {
    Stop();
    project_root_ = project_root.lexically_normal();
    last_error_.clear();
    events_.clear();
    conversation_.clear();
    streaming_assistant_text_.clear();
    mcp_endpoint_.clear();
    provider_statuses_.clear();
    state_ = AIEditorServiceState::StartingHost;

    std::string start_error;
    if (!bridge_.Start(ResolveNodeExecutable(), ResolveHostEntry(),
                       start_error)) {
        SetError(start_error +
                 " Build it with: npm --prefix tools/pocket-agent-host "
                 "install && npm --prefix tools/pocket-agent-host run build");
        return false;
    }
    if (!bridge_.SendRequest(
            "host.initialize",
            BuildStringPayload("project_root", project_root_.string()))) {
        SetError("Could not initialize pocket-agent-host: " +
                 bridge_.GetLastError());
        bridge_.Stop();
        return false;
    }
    return true;
}

void AIEditorService::Stop() {
    if (bridge_.IsRunning() && !active_session_id_.empty()) {
        bridge_.SendRequest("agent.close_session", "{}");
    }
    bridge_.Stop();
    state_ = AIEditorServiceState::Stopped;
    active_provider_.clear();
    active_session_id_.clear();
    streaming_assistant_text_.clear();
    mcp_endpoint_.clear();
    provider_statuses_.clear();
    editor_context_ = {};
}

void AIEditorService::SetEditorContext(const AIEditorContext &context) {
    editor_context_ = context;
}

void AIEditorService::Update() {
    const std::vector<EditorBridge::Message> messages = bridge_.DrainMessages();
    for (const EditorBridge::Message &message : messages) {
        HandleBridgeMessage(message);
    }
    if (state_ != AIEditorServiceState::Stopped &&
        state_ != AIEditorServiceState::Error && !bridge_.IsRunning() &&
        messages.empty()) {
        SetError("pocket-agent-host exited unexpectedly.");
    }
}

bool AIEditorService::ProbeAgents() {
    if (!bridge_.IsRunning()) return false;
    return bridge_.SendRequest("agent.probe", "{}");
}

bool AIEditorService::StartSession(const std::string &provider) {
    if (state_ != AIEditorServiceState::Ready || !bridge_.IsRunning() ||
        provider.empty()) {
        return false;
    }
    if (!bridge_.SendRequest("agent.start_session",
                             BuildProviderPayload(provider))) {
        SetError("Could not start " + provider + " session.");
        return false;
    }
    state_ = AIEditorServiceState::StartingSession;
    active_provider_ = provider;
    return true;
}

bool AIEditorService::StartFakeSession() {
    return StartSession("fake");
}

bool AIEditorService::SendMessage(const std::string &message) {
    if (!CanSendMessage() || message.empty()) return false;
    if (!bridge_.SendRequest("agent.send_message",
                             BuildStringPayload("message", message))) {
        SetError("Could not send message to " + active_provider_ + ".");
        return false;
    }
    conversation_.push_back({"user", message});
    if (conversation_.size() > kMaximumRetainedMessages) {
        conversation_.erase(conversation_.begin());
    }
    streaming_assistant_text_.clear();
    state_ = AIEditorServiceState::RunningTurn;
    return true;
}

bool AIEditorService::CancelTurn() {
    if (state_ != AIEditorServiceState::RunningTurn) return false;
    if (!bridge_.SendRequest("agent.cancel_turn", "{}")) return false;
    state_ = AIEditorServiceState::Cancelling;
    return true;
}

bool AIEditorService::CloseSession() {
    if (!HasActiveSession() || state_ == AIEditorServiceState::RunningTurn ||
        state_ == AIEditorServiceState::Cancelling) {
        return false;
    }
    return bridge_.SendRequest("agent.close_session", "{}");
}

AIEditorServiceState AIEditorService::GetState() const {
    return state_;
}

const char *AIEditorService::GetStateLabel() const {
    switch (state_) {
    case AIEditorServiceState::Stopped: return "Stopped";
    case AIEditorServiceState::StartingHost: return "Starting host";
    case AIEditorServiceState::Ready: return "Ready";
    case AIEditorServiceState::StartingSession: return "Starting session";
    case AIEditorServiceState::Idle: return "Idle";
    case AIEditorServiceState::RunningTurn: return "Running";
    case AIEditorServiceState::Cancelling: return "Cancelling";
    case AIEditorServiceState::Error: return "Error";
    }
    return "Unknown";
}

const std::string &AIEditorService::GetLastError() const {
    return last_error_;
}

const std::string &AIEditorService::GetActiveProvider() const {
    return active_provider_;
}

const std::string &AIEditorService::GetActiveSessionID() const {
    return active_session_id_;
}

const std::string &AIEditorService::GetStreamingAssistantText() const {
    return streaming_assistant_text_;
}

const std::vector<AgentEvent> &AIEditorService::GetEvents() const {
    return events_;
}

const std::vector<AIConversationMessage> &
AIEditorService::GetConversation() const {
    return conversation_;
}

const std::vector<AIAgentProviderStatus> &
AIEditorService::GetProviderStatuses() const {
    return provider_statuses_;
}

const std::string &AIEditorService::GetMcpEndpoint() const {
    return mcp_endpoint_;
}

bool AIEditorService::HasActiveSession() const {
    return !active_session_id_.empty() ||
           state_ == AIEditorServiceState::StartingSession;
}

bool AIEditorService::CanSendMessage() const {
    return state_ == AIEditorServiceState::Idle &&
           !active_session_id_.empty();
}

void AIEditorService::HandleBridgeMessage(
    const EditorBridge::Message &message) {
    rapidjson::Document payload;
    payload.Parse(message.payload_json.c_str());
    if (payload.HasParseError() || !payload.IsObject()) {
        SetError("Received an invalid sidecar payload.");
        return;
    }

    if (message.type == EditorBridge::MessageType::Request &&
        message.method == "mcp.execute_request") {
        HandleMcpExecuteRequest(message.payload_json);
        return;
    }

    if (message.type == EditorBridge::MessageType::Response) {
        if (payload.HasMember("ok") && payload["ok"].IsBool() &&
            !payload["ok"].GetBool()) {
            std::string error_text = "Sidecar request failed.";
            if (payload.HasMember("error") && payload["error"].IsObject()) {
                const std::string code =
                    ReadOptionalString(payload["error"], "code");
                const std::string detail =
                    ReadOptionalString(payload["error"], "message");
                error_text = code.empty() ? detail : code + ": " + detail;
            }
            if (message.method != "mcp.complete_request") {
                SetError(error_text);
            }
            return;
        }
        if (message.method == "agent.probe") {
            HandleAgentProbePayload(message.payload_json);
        }
        return;
    }

    if (message.method == "host.ready") {
        mcp_endpoint_ = ReadOptionalString(payload, "mcp_endpoint");
        state_ = AIEditorServiceState::Ready;
        last_error_.clear();
        ProbeAgents();
        return;
    }
    if (message.method == "host.error") {
        const std::string code = ReadOptionalString(payload, "code");
        const std::string detail = ReadOptionalString(payload, "message");
        SetError(code.empty() ? detail : code + ": " + detail);
        return;
    }
    if (message.method == "agent.event") {
        HandleAgentEventPayload(message.payload_json);
    }
}

void AIEditorService::HandleAgentProbePayload(
    const std::string &payload_json) {
    rapidjson::Document payload;
    payload.Parse(payload_json.c_str());
    if (payload.HasParseError() || !payload.IsObject() ||
        !payload.HasMember("agents") || !payload["agents"].IsArray()) {
        return;
    }
    provider_statuses_.clear();
    for (const rapidjson::Value &agent : payload["agents"].GetArray()) {
        if (!agent.IsObject()) continue;
        AIAgentProviderStatus status;
        status.provider = ReadOptionalString(agent, "provider");
        status.version = ReadOptionalString(agent, "version");
        status.auth_status = ReadOptionalString(agent, "auth_status");
        status.error = ReadOptionalString(agent, "error");
        status.installed =
            agent.HasMember("installed") && agent["installed"].IsBool() &&
            agent["installed"].GetBool();
        status.authenticated =
            agent.HasMember("authenticated") &&
            agent["authenticated"].IsBool() &&
            agent["authenticated"].GetBool();
        status.supports_json_stream =
            agent.HasMember("supports_json_stream") &&
            agent["supports_json_stream"].IsBool() &&
            agent["supports_json_stream"].GetBool();
        status.supports_session_resume =
            agent.HasMember("supports_session_resume") &&
            agent["supports_session_resume"].IsBool() &&
            agent["supports_session_resume"].GetBool();
        status.supports_mcp =
            agent.HasMember("supports_mcp") &&
            agent["supports_mcp"].IsBool() &&
            agent["supports_mcp"].GetBool();
        provider_statuses_.emplace_back(std::move(status));
    }
}

void AIEditorService::HandleMcpExecuteRequest(
    const std::string &payload_json) {
    rapidjson::Document payload;
    payload.Parse(payload_json.c_str());
    if (payload.HasParseError() || !payload.IsObject()) return;
    const std::string request_id =
        ReadOptionalString(payload, "request_id");
    const std::string tool = ReadOptionalString(payload, "tool");
    std::string arguments_json = "{}";
    if (payload.HasMember("arguments") &&
        payload["arguments"].IsObject()) {
        arguments_json = SerializeValue(payload["arguments"]);
    }
    std::string result_json;
    std::string error_code;
    std::string error_message;
    const bool ok = AIReadOnlyTools::Execute(
        tool, arguments_json, editor_context_, result_json, error_code,
        error_message);
    if (request_id.empty()) return;
    bridge_.SendRequest(
        "mcp.complete_request",
        BuildMcpCompletionPayload(request_id, ok, result_json, error_code,
                                  error_message));
}

void AIEditorService::HandleAgentEventPayload(
    const std::string &payload_json) {
    rapidjson::Document payload;
    payload.Parse(payload_json.c_str());
    if (payload.HasParseError() || !payload.IsObject() ||
        !payload.HasMember("event") || !payload["event"].IsObject()) {
        SetError("Received an invalid Agent event payload.");
        return;
    }

    const rapidjson::Value &event_value = payload["event"];
    AgentEvent event;
    event.kind =
        ParseAgentEventKind(ReadOptionalString(event_value, "kind"));
    event.session_id = ReadOptionalString(event_value, "session_id");
    event.turn_id = ReadOptionalString(event_value, "turn_id");
    event.item_id = ReadOptionalString(event_value, "item_id");
    event.display_text = ReadOptionalString(event_value, "display_text");
    if (event_value.HasMember("structured_payload") &&
        event_value["structured_payload"].IsObject()) {
        event.structured_payload_json =
            SerializeValue(event_value["structured_payload"]);
    }
    RecordAgentEvent(std::move(event));
}

void AIEditorService::RecordAgentEvent(AgentEvent event) {
    switch (event.kind) {
    case AgentEventKind::SessionStarted:
        active_session_id_ = event.session_id;
        state_ = AIEditorServiceState::Idle;
        break;
    case AgentEventKind::AssistantTextDelta:
        streaming_assistant_text_ += event.display_text;
        break;
    case AgentEventKind::AssistantMessage:
        conversation_.push_back({"assistant", event.display_text});
        streaming_assistant_text_.clear();
        if (conversation_.size() > kMaximumRetainedMessages) {
            conversation_.erase(conversation_.begin());
        }
        break;
    case AgentEventKind::TurnCompleted:
        state_ = AIEditorServiceState::Idle;
        break;
    case AgentEventKind::TurnFailed:
        streaming_assistant_text_.clear();
        state_ = AIEditorServiceState::Idle;
        break;
    case AgentEventKind::SessionClosed:
        active_session_id_.clear();
        active_provider_.clear();
        state_ = AIEditorServiceState::Ready;
        break;
    default:
        break;
    }

    events_.emplace_back(std::move(event));
    if (events_.size() > kMaximumRetainedEvents) {
        events_.erase(events_.begin(),
                      events_.begin() +
                          static_cast<std::ptrdiff_t>(events_.size() -
                                                      kMaximumRetainedEvents));
    }
}

void AIEditorService::SetError(const std::string &error) {
    state_ = AIEditorServiceState::Error;
    last_error_ = error.empty() ? "Unknown AI editor error." : error;
}
