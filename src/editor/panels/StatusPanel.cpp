#include "editor/panels/StatusPanel.h"
#include "editor/documents/SceneDocument.h"
#include "engine/core/Engine.h"
#include "imgui.h"
#include <string>

namespace EditorPanels {
namespace {

struct StatusSampleState {
    double last_sample_time = -1.0;
    float displayed_editor_fps = 0.0f;
    float displayed_editor_frame_time_ms = 0.0f;
};

StatusSampleState &GetStatusSampleState() {
    static StatusSampleState state;
    return state;
}

void UpdateEditorFrameSample() {
    StatusSampleState &state = GetStatusSampleState();
    const double now = ImGui::GetTime();
    if (state.last_sample_time >= 0.0 && now - state.last_sample_time < 1.0) {
        return;
    }

    const ImGuiIO &io = ImGui::GetIO();
    state.displayed_editor_fps = io.Framerate;
    state.displayed_editor_frame_time_ms =
        state.displayed_editor_fps > 0.0f ? 1000.0f / state.displayed_editor_fps
                                          : 0.0f;
    state.last_sample_time = now;
}

void RenderPhysicsHierarchySummary(const PhysicsHierarchy::State &physics_state) {
    if (!physics_state.has_rigidbody_self) {
        ImGui::TextDisabled("Physics: no Rigidbody on current selection");
        return;
    }

    ImGui::Text("Requested Body: %s",
                physics_state.requested_body_type.c_str());
    ImGui::Text("Effective Body: %s",
                physics_state.effective_body_type.c_str());
    ImGui::Text("Under Dynamic Hierarchy: %s",
                physics_state.is_under_dynamic_hierarchy ? "Yes" : "No");

    if (physics_state.nearest_dynamic_body_ancestor_uid !=
        PhysicsHierarchy::kInvalidActorUID) {
        ImGui::Text("Nearest Dynamic Ancestor UID: %llu",
                    static_cast<unsigned long long>( physics_state.nearest_dynamic_body_ancestor_uid));
    } else {
        ImGui::TextDisabled("Nearest Dynamic Ancestor: none");
    }

    if (physics_state.physics_root_uid != PhysicsHierarchy::kInvalidActorUID) {
        ImGui::Text("Physics Root UID: %llu",
                    static_cast<unsigned long long>( physics_state.physics_root_uid));
    }

    if (physics_state.requested_body_type != physics_state.effective_body_type &&
        physics_state.requested_body_type != PhysicsHierarchy::kNoBodyType) {
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f),
                           "Dynamic overridden by dynamic ancestor");
    }
}

} // namespace

void RenderStatusPanel(const Engine &engine, const SceneDocument &scene_document,
                       int selected_actor_index, int selected_runtime_actor_id,
                       bool play_mode_active, bool play_mode_paused,
                       float applied_ui_scale) {
    UpdateEditorFrameSample();
    const StatusSampleState &state = GetStatusSampleState();

    ImGui::Begin("Status");

    const char *mode_label = !play_mode_active
                                 ? "Edit"
                                 : (play_mode_paused ? "Play (Paused)" : "Play");
    ImGui::Text("Mode: %s", mode_label);
    ImGui::Text("Runtime Scene: %s", engine.GetCurrentSceneName().c_str());
    ImGui::Text("Authoring Scene: %s", scene_document.GetSceneName().c_str());
    ImGui::Text("Actors: %zu", engine.GetActorCount());

    ImGui::Separator();
    ImGui::Text("Editor FPS: %.1f", state.displayed_editor_fps);
    ImGui::Text("Frame: %.2f ms", state.displayed_editor_frame_time_ms);
    ImGui::Text("Gameplay FPS: %.1f", engine.GetGameplayFPS());
    ImGui::TextDisabled("Sampled every 1.0s");

    ImGui::Separator();
    ImGui::TextUnformatted("Selection");
    if (play_mode_active && selected_runtime_actor_id >= 0) {
        const Actor *runtime_actor =
            engine.GetRuntimeActorByID(selected_runtime_actor_id);
        if (runtime_actor != nullptr) {
            const std::string actor_name =
                runtime_actor->actor_name.empty() ? "Unnamed Actor"
                                                  : runtime_actor->actor_name;
            ImGui::Text("Actor: %s", actor_name.c_str());
            RenderPhysicsHierarchySummary( engine.GetRuntimePhysicsHierarchyStateByID( selected_runtime_actor_id));
        } else {
            ImGui::TextDisabled("Selected runtime actor is no longer valid.");
        }
    } else if (selected_actor_index >= 0 &&
               selected_actor_index <
                   static_cast<int>(scene_document.GetActorCount())) {
        const std::size_t actor_index =
            static_cast<std::size_t>(selected_actor_index);
        ImGui::Text("Actor: %s",
                    scene_document.GetActorDisplayName(actor_index).c_str());
        RenderPhysicsHierarchySummary(
            scene_document.GetPhysicsHierarchyState(actor_index));
    } else {
        ImGui::TextDisabled("No actor selected.");
    }

    ImGui::Separator();
    ImGui::Text("Zoom: %.2f", engine.GetCameraZoom());
    ImGui::Text("Runtime: %d x %d", engine.GetConfig().window_width,
                engine.GetConfig().window_height);
    ImGui::Text("Editor Window: %d x %d", engine.GetWindowWidth(),
                engine.GetWindowHeight());
    ImGui::Text("GUI Scale: %.2f", applied_ui_scale);

    ImGui::End();
}

} // namespace EditorPanels
