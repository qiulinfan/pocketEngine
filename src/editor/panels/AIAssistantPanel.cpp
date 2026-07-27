#include "editor/panels/AIAssistantPanel.h"
#include "editor/ai/AIEditorService.h"
#include "imgui.h"
#include <array>
#include <cstring>
#include <string>

namespace EditorPanels {
namespace {

struct AIAssistantPanelState {
    std::array<char, 2048> prompt{};
    bool show_event_log = false;
    std::string selected_provider = "codex";
};

AIAssistantPanelState &GetPanelState() {
    static AIAssistantPanelState state;
    return state;
}

void RenderConversation(const AIEditorService &ai_service) {
    const bool keep_scrolled_to_bottom =
        ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;

    for (const AIConversationMessage &message : ai_service.GetConversation()) {
        const bool is_user = message.role == "user";
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            is_user ? ImVec4(0.55f, 0.78f, 1.0f, 1.0f)
                    : ImVec4(0.80f, 0.92f, 0.76f, 1.0f));
        const std::string assistant_label =
            ai_service.GetActiveProvider().empty()
                ? "Agent"
                : ai_service.GetActiveProvider();
        ImGui::TextUnformatted(
            is_user ? "You" : assistant_label.c_str());
        ImGui::PopStyleColor();
        ImGui::TextWrapped("%s", message.text.c_str());
        ImGui::Spacing();
    }

    if (!ai_service.GetStreamingAssistantText().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(0.80f, 0.92f, 0.76f, 1.0f));
        ImGui::TextUnformatted(
            ai_service.GetActiveProvider().empty()
                ? "Agent"
                : ai_service.GetActiveProvider().c_str());
        ImGui::PopStyleColor();
        ImGui::TextWrapped("%s", ai_service.GetStreamingAssistantText().c_str());
    }

    if (keep_scrolled_to_bottom) ImGui::SetScrollHereY(1.0f);
}

void RenderEventLog(const AIEditorService &ai_service) {
    if (!ImGui::TreeNode("Protocol event log")) return;
    if (ImGui::BeginChild("AIProtocolEvents", ImVec2(0.0f, 140.0f),
                          ImGuiChildFlags_Borders)) {
        for (const AgentEvent &event : ai_service.GetEvents()) {
            ImGui::TextDisabled("%s", AgentEventKindName(event.kind));
            if (!event.display_text.empty()) {
                ImGui::SameLine();
                ImGui::TextWrapped("%s", event.display_text.c_str());
            }
        }
    }
    ImGui::EndChild();
    ImGui::TreePop();
}

} // namespace

void RenderAIAssistantPanel(AIEditorService &ai_service) {
    if (!ImGui::Begin("AI Assistant")) {
        ImGui::End();
        return;
    }

    AIAssistantPanelState &panel_state = GetPanelState();
    ImGui::Text("Host: %s", ai_service.GetStateLabel());
    if (!ai_service.GetActiveProvider().empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Provider: %s",
                            ai_service.GetActiveProvider().c_str());
    }
    ImGui::Text("MCP: %s",
                ai_service.GetMcpEndpoint().empty()
                    ? "starting"
                    : "connected (read-only)");

    if (ai_service.GetState() == AIEditorServiceState::Error) {
        ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.35f, 1.0f), "Host error");
        ImGui::TextWrapped("%s", ai_service.GetLastError().c_str());
        ImGui::Separator();
        ImGui::TextDisabled(
            "Build tools/pocket-agent-host and restart the editor.");
        ImGui::End();
        return;
    }

    if (!ai_service.HasActiveSession()) {
        const bool can_start =
            ai_service.GetState() == AIEditorServiceState::Ready;
        const std::vector<AIAgentProviderStatus> &providers =
            ai_service.GetProviderStatuses();
        if (ImGui::BeginCombo("Provider",
                              panel_state.selected_provider.c_str())) {
            for (const AIAgentProviderStatus &provider : providers) {
                const bool selected =
                    provider.provider == panel_state.selected_provider;
                std::string label = provider.provider;
                label += !provider.installed
                             ? "  [not found]"
                             : (provider.authenticated ? "  [ready]"
                                                       : "  [login required]");
                if (ImGui::Selectable(label.c_str(), selected)) {
                    panel_state.selected_provider = provider.provider;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        const AIAgentProviderStatus *selected_status = nullptr;
        for (const AIAgentProviderStatus &provider : providers) {
            if (provider.provider == panel_state.selected_provider) {
                selected_status = &provider;
                break;
            }
        }
        if (selected_status != nullptr) {
            if (selected_status->installed &&
                selected_status->authenticated) {
                ImGui::TextDisabled(
                    "%s | %s | JSON: %s | MCP: %s",
                    selected_status->version.c_str(),
                    selected_status->auth_status.c_str(),
                    selected_status->supports_json_stream ? "yes" : "no",
                    selected_status->supports_mcp ? "yes" : "no");
            } else if (!selected_status->error.empty()) {
                ImGui::TextWrapped("%s",
                                   selected_status->error.c_str());
            }
        } else {
            ImGui::TextDisabled("Probing local Agent CLIs...");
        }
        const bool provider_ready =
            selected_status != nullptr && selected_status->installed &&
            selected_status->authenticated;
        if (!can_start || !provider_ready) ImGui::BeginDisabled();
        if (ImGui::Button("Start Read-only Session")) {
            ai_service.StartSession(panel_state.selected_provider);
        }
        if (!can_start || !provider_ready) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Probe Again")) ai_service.ProbeAgents();
    } else {
        ImGui::TextDisabled("Session: %s",
                            ai_service.GetActiveSessionID().c_str());
    }

    ImGui::Separator();
    if (ImGui::BeginChild("AIConversation", ImVec2(0.0f, -128.0f),
                          ImGuiChildFlags_Borders)) {
        if (ai_service.GetConversation().empty() &&
            ai_service.GetStreamingAssistantText().empty()) {
            ImGui::TextDisabled(
                "Choose Codex or Claude Code, then ask about the current "
                "scene. Phase 1 exposes read-only editor context.");
        }
        RenderConversation(ai_service);
    }
    ImGui::EndChild();

    ImGui::InputTextMultiline("##AIPrompt", panel_state.prompt.data(),
                              panel_state.prompt.size(), ImVec2(-1.0f, 58.0f));
    const bool has_prompt = std::strlen(panel_state.prompt.data()) > 0;
    const bool can_send = ai_service.CanSendMessage() && has_prompt;
    if (!can_send) ImGui::BeginDisabled();
    if (ImGui::Button("Send")) {
        if (ai_service.SendMessage(panel_state.prompt.data())) {
            panel_state.prompt.fill('\0');
        }
    }
    if (!can_send) ImGui::EndDisabled();

    ImGui::SameLine();
    const bool can_cancel =
        ai_service.GetState() == AIEditorServiceState::RunningTurn;
    if (!can_cancel) ImGui::BeginDisabled();
    if (ImGui::Button("Stop")) ai_service.CancelTurn();
    if (!can_cancel) ImGui::EndDisabled();

    ImGui::SameLine();
    const bool can_close =
        ai_service.HasActiveSession() &&
        ai_service.GetState() == AIEditorServiceState::Idle;
    if (!can_close) ImGui::BeginDisabled();
    if (ImGui::Button("Close Session")) ai_service.CloseSession();
    if (!can_close) ImGui::EndDisabled();

    RenderEventLog(ai_service);
    ImGui::End();
}

} // namespace EditorPanels
