#include "editor/panels/InspectorPanel.h"
#include "editor/core/EditorDragDrop.h"
#include "editor/documents/SceneDocument.h"
#include "engine/core/Engine.h"
#include "scripting/ComponentManager.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <utility>
#include <vector>

namespace EditorPanels {
namespace {

/*
Inspector keeps a scene-backed selection valid while still allowing the empty
selection state used by runtime-only actor inspection and blank clicks.
*/
void EnsureValidSelection(SceneDocument &scene_document,  int &selected_actor_index) {
    if (scene_document.GetActorCount() == 0) {
        selected_actor_index = -1;
        return;
    }
    if (selected_actor_index >= static_cast<int>(scene_document.GetActorCount())) {
        selected_actor_index = static_cast<int>(scene_document.GetActorCount()) - 1;
    }
}

/* Edit one string input buffer and report whether the value changed. */
bool InputTextString(const char *label, const std::string &current_value, std::string &updated_value) {
    std::vector<char> buffer(std::max<std::size_t>(256, current_value.size() + 64), '\0');
    std::memcpy(buffer.data(), current_value.c_str(), current_value.size());
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) {
        return false;
    }
    updated_value = buffer.data();
    return true;
}

/*
Render one scalar property editor and return true only when the user committed
an actual value change. Inspector uses this helper for both scene-backed and
runtime-only property editing paths.
*/
bool EditPropertyValue(const char *label,
                       const Actor::ComponentPropertyValue &current_value,
                       Actor::ComponentPropertyValue &updated_value) {
    if (const bool *typed_value = std::get_if<bool>(&current_value)) {
        bool next_value = *typed_value;
        if (!ImGui::Checkbox(label, &next_value)) return false;
        updated_value = next_value;
        return true;
    }

    if (const int *typed_value = std::get_if<int>(&current_value)) {
        int next_value = *typed_value;
        if (!ImGui::InputInt(label, &next_value)) return false;
        updated_value = next_value;
        return true;
    }

    if (const double *typed_value = std::get_if<double>(&current_value)) {
        double next_value = *typed_value;
        if (!ImGui::InputDouble(label, &next_value)) return false;
        updated_value = next_value;
        return true;
    }

    if (const std::string *typed_value = std::get_if<std::string>(&current_value)) {
        std::string next_value;
        if (!InputTextString(label, *typed_value, next_value)) return false;
        updated_value = next_value;
        return true;
    }

    return false;
}

/* Small string helpers shared by the inspector's filtering UI. */
std::string ToLowerCopy(const std::string &value) {
    std::string lowered = value;
    std::transform(
        lowered.begin(), lowered.end(), lowered.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}
bool IsBlank(const std::string &value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}
bool MatchesFilter(const std::string &value, const std::string &filter) {
    if (filter.empty() || IsBlank(filter)) return true;
    return ToLowerCopy(value).find(ToLowerCopy(filter)) != std::string::npos;
}

/*
Inspector accepts dragged Lua component types so authors can add components
directly from the Project panel without opening a separate add dialog first.
*/
bool RenderLuaComponentDropTarget(
    SceneDocument &scene_document, std::size_t actor_index,
    std::vector<SceneFormat::SceneEditCommand> *out_edit_commands) {
    bool scene_changed = false;

    /*
    Inspector accepts project-side Lua component types as an alternative to
    the add-component popup so authors can work more directly from Project.
    */
    ImGui::Button("Drop Lua Component Here",
                  ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EditorDragDrop::kLuaComponentPayload)) {
            const char *component_type = static_cast<const char *>(payload->Data);
            if (component_type != nullptr && component_type[0] != '\0') {
                SceneFormat::SceneEditCommand command;
                const bool added = scene_document.AddComponentToActor(actor_index, component_type, &command);
                scene_changed |= added;
                if (added && out_edit_commands != nullptr) {
                    out_edit_commands->emplace_back(std::move(command));
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::TextDisabled( "Drag a component_types/*.lua asset from Project into this actor.");
    return scene_changed;
}

struct PropertyAssetDrop {
    std::string resource_name;
    bool has_sprite_cell = false;
    int sprite_row = 1;
    int sprite_column = 1;
};

bool AcceptPropertyAssetDrop(PropertyAssetDrop &out_drop) {
    if (!ImGui::BeginDragDropTarget()) return false;

    bool accepted = false;
    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload(EditorDragDrop::kSpriteAssetPayload)) {
        if (payload->Data != nullptr &&
            payload->DataSize == sizeof(EditorDragDrop::SpriteAssetPayload)) {
            const auto *sprite_payload =
                static_cast<const EditorDragDrop::SpriteAssetPayload *>(
                    payload->Data);
            if (sprite_payload->image_resource_name[0] != '\0') {
                out_drop.resource_name = sprite_payload->image_resource_name;
                out_drop.has_sprite_cell = true;
                out_drop.sprite_row = std::max(1, sprite_payload->row);
                out_drop.sprite_column = std::max(1, sprite_payload->column);
                accepted = true;
            }
        }
    } else if (const ImGuiPayload *payload =
                   ImGui::AcceptDragDropPayload(
                       EditorDragDrop::kImageAssetPayload)) {
        const char *resource_name = static_cast<const char *>(payload->Data);
        if (resource_name != nullptr && resource_name[0] != '\0') {
            out_drop.resource_name = resource_name;
            accepted = true;
        }
    } else if (const ImGuiPayload *payload =
                   ImGui::AcceptDragDropPayload(
                       EditorDragDrop::kAudioAssetPayload)) {
        const char *resource_name = static_cast<const char *>(payload->Data);
        if (resource_name != nullptr && resource_name[0] != '\0') {
            out_drop.resource_name = resource_name;
            accepted = true;
        }
    }

    ImGui::EndDragDropTarget();
    return accepted;
}

bool IsSpriteRendererSpriteProperty(const Actor::ComponentSpec &component_spec,
                                    const std::string &property_name) {
    return component_spec.type == "SpriteRenderer" && property_name == "sprite";
}

bool ApplyRuntimePropertyAssetDrop(Actor::UID actor_uid,
                                   const Actor::ComponentSpec &component_spec,
                                   const std::string &property_name,
                                   const PropertyAssetDrop &drop) {
    bool changed = false;
    changed |= ComponentManager::SetRuntimeComponentPropertyValue(
        actor_uid, component_spec.key, property_name, drop.resource_name);

    if (IsSpriteRendererSpriteProperty(component_spec, property_name)) {
        const int sprite_row = drop.has_sprite_cell ? drop.sprite_row : 1;
        const int sprite_column = drop.has_sprite_cell ? drop.sprite_column : 1;
        changed |= ComponentManager::SetRuntimeComponentPropertyValue(
            actor_uid, component_spec.key, "sprite_row", sprite_row);
        changed |= ComponentManager::SetRuntimeComponentPropertyValue(
            actor_uid, component_spec.key, "sprite_column", sprite_column);
    }

    return changed;
}

bool ApplyScenePropertyAssetDrop(
    SceneDocument &scene_document, std::size_t actor_index,
    const Actor::ComponentSpec &component_spec, const std::string &property_name,
    const PropertyAssetDrop &drop,
    std::vector<SceneFormat::SceneEditCommand> *out_edit_commands) {
    bool changed = false;
    SceneFormat::SceneEditCommand command;
    if (scene_document.SetComponentProperty(actor_index, component_spec.key,
                                            property_name, drop.resource_name,
                                            &command)) {
        changed = true;
        if (out_edit_commands != nullptr) {
            out_edit_commands->emplace_back(std::move(command));
        }
    }

    if (IsSpriteRendererSpriteProperty(component_spec, property_name)) {
        const int sprite_row = drop.has_sprite_cell ? drop.sprite_row : 1;
        const int sprite_column = drop.has_sprite_cell ? drop.sprite_column : 1;

        if (scene_document.SetComponentProperty(actor_index, component_spec.key,
                                                "sprite_row", sprite_row,
                                                &command)) {
            changed = true;
            if (out_edit_commands != nullptr) {
                out_edit_commands->emplace_back(std::move(command));
            }
        }

        if (scene_document.SetComponentProperty(actor_index, component_spec.key,
                                                "sprite_column",
                                                sprite_column, &command)) {
            changed = true;
            if (out_edit_commands != nullptr) {
                out_edit_commands->emplace_back(std::move(command));
            }
        }
    }

    return changed;
}

/*
Show the derived Rigidbody hierarchy state that explains whether a dynamic body
is active, overridden, or attached under another dynamic physics root.
*/
void RenderPhysicsHierarchyInfo(const PhysicsHierarchy::State &physics_state) {
    if (!physics_state.has_rigidbody_self) return;

    ImGui::Spacing();
    ImGui::TextDisabled("Physics Hierarchy");
    ImGui::BulletText("Requested body type: %s",
                      physics_state.requested_body_type.c_str());
    ImGui::BulletText("Effective body type: %s",
                      physics_state.effective_body_type.c_str());

    if (physics_state.nearest_dynamic_body_ancestor_uid !=
        PhysicsHierarchy::kInvalidActorUID) {
        ImGui::BulletText("Nearest dynamic ancestor UID: %llu",
                          static_cast<unsigned long long>( physics_state.nearest_dynamic_body_ancestor_uid));
    } else {
        ImGui::BulletText("Nearest dynamic ancestor: none");
    }

    if (physics_state.physics_root_uid != PhysicsHierarchy::kInvalidActorUID) {
        ImGui::BulletText("Physics root UID: %llu",
                          static_cast<unsigned long long>( physics_state.physics_root_uid));
    }

    if (physics_state.requested_body_type != physics_state.effective_body_type &&
        physics_state.requested_body_type != PhysicsHierarchy::kNoBodyType) {
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f),
                           "Dynamic overridden by dynamic ancestor");
    }
}

/*
Render the runtime-only inspector path. This is used for transient actors that
exist only during play mode and therefore cannot write changes back to the
authoring SceneDocument.
*/
void RenderRuntimeOnlyActorInspector(const Engine &engine,
                                     const Actor &runtime_actor) {
    ImGui::Text("Runtime UID: %llu",
                static_cast<unsigned long long>(runtime_actor.uid));
    ImGui::TextDisabled("Runtime-spawned actor. Changes here are transient and will be discarded when play mode stops.");
    ImGui::Separator();

    const std::vector<Actor::ComponentSpec> runtime_component_specs =
        ComponentManager::GetRuntimeComponentSpecs(runtime_actor.uid);
    if (runtime_component_specs.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("Runtime actor currently has no live components.");
        return;
    }

    for (const Actor::ComponentSpec &component_spec : runtime_component_specs) {
        ImGui::PushID(component_spec.key.c_str());

        std::ostringstream header_label;
        header_label << component_spec.key << " : " << component_spec.type;
        const bool is_component_open = ImGui::CollapsingHeader( header_label.str().c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        if (is_component_open) {
            const std::vector<Actor::ComponentProperty> runtime_properties =
                ComponentManager::GetRuntimeComponentProperties(
                    runtime_actor.uid, component_spec.key);
            if (runtime_properties.empty()) {
                ImGui::TextDisabled( "No scalar runtime properties are currently available.");
            }

            for (const Actor::ComponentProperty &property : runtime_properties) {
                ImGui::PushID(property.name.c_str());
                Actor::ComponentPropertyValue updated_value;
                bool property_changed = false;
                if (EditPropertyValue(property.name.c_str(), property.value,
                                      updated_value)) {
                    property_changed |=
                        ComponentManager::SetRuntimeComponentPropertyValue(
                        runtime_actor.uid, component_spec.key, property.name,
                        updated_value);
                }

                if (std::holds_alternative<std::string>(property.value)) {
                    PropertyAssetDrop drop;
                    if (AcceptPropertyAssetDrop(drop)) {
                        property_changed |= ApplyRuntimePropertyAssetDrop(
                            runtime_actor.uid, component_spec, property.name,
                            drop);
                    }
                }
                ImGui::PopID();
            }

            if (component_spec.type == "Rigidbody") {
                RenderPhysicsHierarchyInfo(
                    engine.GetRuntimePhysicsHierarchyStateByUID(
                        runtime_actor.uid));
            }
        }

        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::TextDisabled(
        "Runtime-only structural editing is not wired yet. Component add / "
        "remove / rename will come later.");
}

} // namespace

bool RenderInspectorPanel(const Engine &engine, SceneDocument &scene_document,
                          int &selected_actor_index,
                          Actor::UID selected_runtime_actor_uid,
                          bool play_mode_active,
                          bool scene_editing_enabled,
                          std::vector<SceneFormat::SceneEditCommand> *out_edit_commands) {
    /*
    Inspector edits the editor-owned SceneDocument cache. Runtime decides later
    when to mirror those document changes into its live copy, except for the
    dedicated runtime-only branch used for transient play-mode actors.
    */
    bool scene_changed = false;

    ImGui::Begin("Inspector");
    EnsureValidSelection(scene_document, selected_actor_index);

    if (selected_actor_index < 0) {
        if (play_mode_active &&
            selected_runtime_actor_uid != Actor::kInvalidUID) {
            const Actor *runtime_actor =
                engine.GetRuntimeActorByUID(selected_runtime_actor_uid);
            if (runtime_actor != nullptr) {
                RenderRuntimeOnlyActorInspector(engine, *runtime_actor);
                ImGui::End();
                return false;
            }
        }

        ImGui::TextUnformatted("Select one actor from the hierarchy.");
        ImGui::End();
        return false;
    }
    /* Build the effective actor view shown by inspector controls. */
    const std::size_t actor_index = static_cast<std::size_t>(selected_actor_index);
    const Actor effective_actor = scene_document.BuildEffectiveActor(actor_index);

    /*
    Notice: flags for rename popup need to be static, since this edition may last for several frames.
    */
    static bool component_rename_popup_active = false;
    static std::size_t component_rename_actor_index = 0;
    static std::string component_rename_original_key;
    static std::string component_rename_draft_key;
    static bool component_rename_open_requested = false;
    static bool component_rename_anchor_valid = false;
    static ImVec2 component_rename_anchor = ImVec2(0.0f, 0.0f);

    ImGui::Text("Scene: %s", scene_document.GetSceneName().c_str());
    ImGui::Text("Source: %s", scene_document.GetScenePath().string().c_str());
    ImGui::Separator();
    if (!scene_editing_enabled) {
        ImGui::TextDisabled("Play mode is active. Inspector editing is temporarily disabled.");
    }
    ImGui::BeginDisabled(!scene_editing_enabled);

    /*
    then: iterate overcomponents. 
    draw a section for each component
    handle component type change, property edits, component rename/delete
    */ 
    for (std::size_t component_index = 0;
         component_index < effective_actor.component_specs.size();
         ++component_index) {
             const Actor::ComponentSpec &component_spec = effective_actor.component_specs[component_index];
        ImGui::PushID(component_spec.key.c_str());

        std::ostringstream header_label;
        header_label << component_spec.key << " : " << component_spec.type;
        bool is_component_open = false;

        bool component_structure_changed = false;
        const ImVec2 action_button_padding(5.0f, 3.0f);
        const ImGuiStyle &style = ImGui::GetStyle();
        const float rename_button_width = ImGui::CalcTextSize("Rename").x + action_button_padding.x * 2.0f;
        const float delete_button_width = ImGui::CalcTextSize("Delete").x + action_button_padding.x * 2.0f;
        const float actions_column_width =
            rename_button_width + style.ItemSpacing.x + delete_button_width +
            10.0f;

        /*
        Split the header row into dedicated columns so action buttons do not
        share hitbox space with the collapsing header widget.
        */
        const ImGuiTableFlags header_table_flags = ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("component_header_row", 2, header_table_flags)) {
            ImGui::TableSetupColumn("Header", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed,
                                    actions_column_width);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            is_component_open = ImGui::CollapsingHeader(header_label.str().c_str(), ImGuiTreeNodeFlags_DefaultOpen);

            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, action_button_padding);
            /*
            Right-align the compact action row so scaling changes do not
            squeeze the trailing Delete button.
            */
            const float remaining_width = ImGui::GetContentRegionAvail().x;
            if (remaining_width > actions_column_width) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (remaining_width - actions_column_width));
            }
            // if component renamed: open a popup (no immediate edit since need user to enter the final name)
            if (ImGui::SmallButton("Rename")) {
                component_rename_popup_active = true;
                component_rename_actor_index = actor_index;
                component_rename_original_key = component_spec.key;
                component_rename_draft_key = component_spec.key;
                component_rename_open_requested = true;
                component_rename_anchor = ImGui::GetItemRectMin();
                component_rename_anchor_valid = true;
            }
            ImGui::SameLine();
            // if component delete: generate SceneEditCommand immediately
            if (ImGui::SmallButton("Delete")) {
                SceneFormat::SceneEditCommand command;
                component_structure_changed |= scene_document.DeleteComponent(actor_index, component_spec.key, &command);
                scene_changed |= component_structure_changed;
                if (component_structure_changed && out_edit_commands != nullptr) {
                    out_edit_commands->emplace_back(std::move(command));
                }
            }
            ImGui::PopStyleVar();
            ImGui::EndTable();
        }

        /*
        Component add/remove/rename changes the key list. Stop processing this
        stale snapshot entry and let the next frame redraw from fresh state.
        */
        if (component_structure_changed) {
            ImGui::PopID();
            continue;
        }

        // if component section is open, draw property editors
        if (is_component_open) {
            // get properties list for the component
            const std::vector<Actor::ComponentProperty> inspectable_properties =
                scene_document.GetInspectableProperties(actor_index, component_spec.key);
            if (inspectable_properties.empty()) {
                ImGui::TextDisabled("No scalar default/override properties are available yet.");
            }

            // handle property edits
            for (const Actor::ComponentProperty &property : inspectable_properties) {
                ImGui::PushID(property.name.c_str());
                Actor::ComponentPropertyValue updated_value;
                bool property_changed = false;
                if (EditPropertyValue(property.name.c_str(), property.value, updated_value)) {
                    SceneFormat::SceneEditCommand command;
                    property_changed |= scene_document.SetComponentProperty(
                        actor_index, component_spec.key, property.name,
                        updated_value, &command);
                    scene_changed |= property_changed;
                    if (property_changed && out_edit_commands != nullptr) {
                        out_edit_commands->emplace_back(std::move(command));
                    }
                }

                if (std::holds_alternative<std::string>(property.value)) {
                    PropertyAssetDrop drop;
                    if (AcceptPropertyAssetDrop(drop)) {
                        property_changed |= ApplyScenePropertyAssetDrop(
                            scene_document, actor_index, component_spec,
                            property.name, drop, out_edit_commands);
                        scene_changed |= property_changed;
                    }
                }
                ImGui::PopID();
            }

            if (component_spec.type == "Rigidbody") {
                RenderPhysicsHierarchyInfo(
                    scene_document.GetPhysicsHierarchyState(actor_index));
            }
        }

        ImGui::PopID();

        /*
        Give each component block a subtle visual boundary so long inspectors
        stay readable as component counts grow.
        */
        if (component_index + 1 < effective_actor.component_specs.size()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
    }

    /* Handle the rename-component popup if rename was requested. */
    if (component_rename_open_requested) {
        /* Open popup from stable ID scope (outside component PushID) so it appears. */
        ImGui::OpenPopup("rename_component_popup_global");
        component_rename_open_requested = false;
    }
    if (component_rename_anchor_valid) {
        /* Place popup slightly above the selected component header row. */
        ImGui::SetNextWindowPos(
            ImVec2(component_rename_anchor.x, component_rename_anchor.y - 6.0f),
            ImGuiCond_Appearing, ImVec2(0.0f, 1.0f));
    }
    if (ImGui::BeginPopup("rename_component_popup_global")) {
        std::string updated_key;
        if (InputTextString("##rename_component_key", component_rename_draft_key, updated_key)) {
            component_rename_draft_key = updated_key;
        }
        if (ImGui::Button("Apply")) {
            if (component_rename_popup_active &&
                component_rename_actor_index < scene_document.GetActorCount()) {
                SceneFormat::SceneEditCommand command;
                const bool renamed = scene_document.RenameComponent(
                    component_rename_actor_index, component_rename_original_key,
                    component_rename_draft_key, &command);
                scene_changed |= renamed;
                if (renamed && out_edit_commands != nullptr) {
                    out_edit_commands->emplace_back(std::move(command));
                }
            }
            component_rename_popup_active = false;
            component_rename_original_key.clear();
            component_rename_draft_key.clear();
            component_rename_anchor_valid = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            component_rename_popup_active = false;
            component_rename_original_key.clear();
            component_rename_draft_key.clear();
            component_rename_anchor_valid = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    /* Final section: add-component button and popup. */
    ImGui::Separator();
    ImGui::TextUnformatted("Components");
    scene_changed |= RenderLuaComponentDropTarget(scene_document, actor_index, out_edit_commands);
    if (ImGui::Button("+ Add Component")) {
        ImGui::OpenPopup("add_component_popup");
    }

    if (ImGui::BeginPopup("add_component_popup")) {
        static char component_filter[128] = "";
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##component_filter", "Filter components...",
                                 component_filter,
                                 IM_ARRAYSIZE(component_filter));

        const std::string filter = component_filter;
        std::vector<std::string> component_types = ComponentManager::GetRegisteredComponentTypes();
        std::sort(component_types.begin(), component_types.end());
        component_types.erase(std::unique(component_types.begin(), component_types.end()), component_types.end());

        if (component_types.empty()) {
            ImGui::TextDisabled("No component type is currently registered.");
        } 
        else {
            for (const std::string &component_type : component_types) {
                if (!MatchesFilter(component_type, filter)) continue;
                if (ImGui::Selectable(component_type.c_str())) {
                    SceneFormat::SceneEditCommand command;
                    const bool added = scene_document.AddComponentToActor(actor_index, component_type, &command);
                    scene_changed |= added;
                    if (added && out_edit_commands != nullptr) {
                        out_edit_commands->emplace_back(std::move(command));
                    }
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }
        }
        ImGui::EndPopup();
    }

    ImGui::EndDisabled();
    ImGui::End();
    return scene_changed;
}

} // namespace EditorPanels
