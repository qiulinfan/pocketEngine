#include "editor/panels/HierarchyPanel.h"
#include "editor/core/EditorDragDrop.h"
#include "editor/documents/SceneDocument.h"
#include "engine/core/Engine.h"
#include "shared/resources/ResourcePath.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace EditorPanels {
namespace {

/*
Keep scene-document selection stable while still allowing "no selection"
during blank-click and runtime-only selection flows.
*/
void EnsureValidSceneSelection(SceneDocument &scene_document,
                               int &selected_actor_index) {
    if (scene_document.GetActorCount() == 0) {
        selected_actor_index = -1;
        return;
    }
    if (selected_actor_index >=
        static_cast<int>(scene_document.GetActorCount())) {
            selected_actor_index = static_cast<int>(scene_document.GetActorCount()) - 1;
    }
}

/*
Runtime selection can outlive the actor when play-mode mutations destroy it.
Clamp invalid selections back to "none" before the panel renders.
*/
void EnsureValidRuntimeSelection(const Engine &engine,
                                 Actor::UID &selected_runtime_actor_uid) {
    if (selected_runtime_actor_uid == Actor::kInvalidUID) return;
    if (engine.GetRuntimeActorByUID(selected_runtime_actor_uid) != nullptr) {
        return;
    }
    selected_runtime_actor_uid = Actor::kInvalidUID;
}

/* Small string helpers used by add-actor filtering and rename popups. */
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
bool InputTextString(const char *label, const std::string &current_value,
                     std::string &updated_value) {
    std::vector<char> buffer(
        std::max<std::size_t>(256, current_value.size() + 64), '\0');
    std::memcpy(buffer.data(), current_value.c_str(), current_value.size());
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) {
        return false;
    }
    updated_value = buffer.data();
    return true;
}

/*
Draw the scene/runtime provenance badge directly into the tree node row so the
Hierarchy can distinguish authored actors from transient play-mode actors.
*/
void DrawRuntimeActorSourceBadge(ImDrawList *draw_list, const Actor &runtime_actor,
                                 const ImVec2 &item_min,
                                 const ImVec2 &item_max) {
    if (draw_list == nullptr) return;

    const bool scene_backed = runtime_actor.IsSceneBacked();
    const char *badge_text = scene_backed ? "[Scene]" : "[Runtime]";
    const ImU32 badge_text_color =
        scene_backed ? IM_COL32(170, 220, 255, 255)
                     : IM_COL32(255, 210, 150, 255);
    const ImU32 badge_fill_color =
        scene_backed ? IM_COL32(34, 74, 110, 235)
                     : IM_COL32(110, 76, 28, 235);

    const ImVec2 badge_text_size = ImGui::CalcTextSize(badge_text);
    const ImVec2 badge_padding(8.0f, 2.0f);
    const ImVec2 badge_size(badge_text_size.x + badge_padding.x * 2.0f,
                            badge_text_size.y + badge_padding.y * 2.0f);
    const ImVec2 badge_min(item_max.x - badge_size.x - 8.0f,
                           item_min.y + std::max(0.0f, (item_max.y - item_min.y -
                                                        badge_size.y) * 0.5f));
    const ImVec2 badge_max(badge_min.x + badge_size.x,
                           badge_min.y + badge_size.y);
    draw_list->AddRectFilled(badge_min, badge_max, badge_fill_color, 8.0f);
    draw_list->AddText(ImVec2(badge_min.x + badge_padding.x,
                              badge_min.y + badge_padding.y),
                       badge_text_color, badge_text);
}

/*
Collect available actor templates for the add-actor popup and template drops.
Template names are sorted with scene-local folders preferred over global ones.
*/
std::vector<std::string> CollectTemplateNames(const std::filesystem::path &scene_subdirectory) {
    const std::filesystem::path template_root =
        ResourcePath::ResourceSubdirectory("actor_templates");
    std::vector<std::filesystem::path> template_files = ResourcePath::CollectFilesRecursively(template_root, ".template");
    ResourcePath::SortPathsWithPreference(template_files, template_root, scene_subdirectory);

    std::vector<std::string> template_names;
    template_names.reserve(template_files.size());
    for (const std::filesystem::path &template_file : template_files) {
        std::error_code relative_error;
        std::filesystem::path relative_path = std::filesystem::relative(template_file, template_root, relative_error);
        if (relative_error || relative_path.empty()) continue;

        relative_path.replace_extension();
        const std::string template_name = relative_path.generic_string();
        if (template_name.empty()) continue;
        template_names.emplace_back(template_name);
    }
    return template_names;
}

/*
Render the add-actor popup that can either create an empty actor or instantiate
one from a template. All structural edits still flow through SceneDocument
commands so runtime mirroring stays consistent.
*/
bool RenderAddActorPopup(
    SceneDocument &scene_document, int &selected_actor_index,
    std::vector<SceneFormat::SceneEditCommand> *out_edit_commands) {
    bool scene_changed = false;
    if (!ImGui::BeginPopup("add_actor_popup")) {
        return false;
    }

    if (ImGui::MenuItem("Empty Actor")) {
        std::size_t new_actor_index = 0;
        SceneFormat::SceneEditCommand command;
        if (scene_document.AppendEmptyActor(new_actor_index, &command)) {
            selected_actor_index = static_cast<int>(new_actor_index);
            scene_changed = true;
            if (out_edit_commands != nullptr) {
                out_edit_commands->emplace_back(std::move(command));
            }
        }
        ImGui::CloseCurrentPopup();
    }

    ImGui::Separator();

    ImGui::TextUnformatted("From Template");
    static char template_filter[128] = "";
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##actor_template_filter", "Filter templates...", template_filter, IM_ARRAYSIZE(template_filter));

    const std::string filter = template_filter;
    const std::vector<std::string> template_names = CollectTemplateNames(scene_document.GetSceneSubdirectory());
    if (template_names.empty()) {
        ImGui::TextDisabled("No .template file found in actor_templates/.");
    } 
    else {
        for (const std::string &template_name : template_names) {
            if (!MatchesFilter(template_name, filter)) continue;

            if (ImGui::Selectable(template_name.c_str())) {
                std::size_t new_actor_index = 0;
                SceneFormat::SceneEditCommand command;
                if (scene_document.AppendActorFromTemplate(template_name, new_actor_index, &command)) {
                    selected_actor_index = static_cast<int>(new_actor_index);
                    scene_changed = true;
                    if (out_edit_commands != nullptr) {
                        out_edit_commands->emplace_back(std::move(command));
                    }
                }
                ImGui::CloseCurrentPopup();
                break;
            }
        }
    }

    ImGui::EndPopup();
    return scene_changed;
}

/*
Render the explicit template drop zone kept at the top of Hierarchy. This
matches the scene-panel template drop feature but stays focused on list-level
object management.
*/
bool RenderHierarchyTemplateDropTarget(
    SceneDocument &scene_document, int &selected_actor_index,
    std::vector<SceneFormat::SceneEditCommand> *out_edit_commands) {
    bool scene_changed = false;

    /*
    Keep the drop zone explicit so template instancing feels predictable even
    before we expand drag-and-drop to the full panel surface.
    */
    ImGui::Button("Drop Actor Template Here",
                  ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EditorDragDrop::kActorTemplatePayload)) {
            const char *template_name = static_cast<const char *>(payload->Data);
            if (template_name != nullptr && template_name[0] != '\0') {
                std::size_t new_actor_index = 0;
                SceneFormat::SceneEditCommand command;
                if (scene_document.AppendActorFromTemplate( template_name, new_actor_index, &command)) {
                    selected_actor_index = static_cast<int>(new_actor_index);
                    scene_changed = true;
                    if (out_edit_commands != nullptr) {
                        out_edit_commands->emplace_back(std::move(command));
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::TextDisabled("Drag a .template from Project, or use + Add Actor.");
    return scene_changed;
}

void SelectRuntimeActor(SceneDocument &scene_document,
                        const Actor &runtime_actor,
                        int &selected_actor_index,
                        Actor::UID &selected_runtime_actor_uid) {
    selected_runtime_actor_uid = runtime_actor.uid;
    if (!runtime_actor.IsSceneBacked()) {
        selected_actor_index = -1;
        return;
    }

    const std::optional<std::size_t> actor_index = scene_document.FindActorIndexByUID(runtime_actor.uid);
    selected_actor_index = actor_index.has_value() ? static_cast<int>(*actor_index) : -1;
}

bool BeginSceneActorDragSource(SceneDocument::ActorUID actor_uid, const std::string &actor_label) {
    if (actor_uid == SceneDocument::kInvalidActorUID) return false;
    if (!ImGui::BeginDragDropSource()) return false;

    ImGui::SetDragDropPayload(EditorDragDrop::kSceneActorPayload, &actor_uid, sizeof(actor_uid));
    ImGui::TextUnformatted(actor_label.c_str());
    ImGui::EndDragDropSource();
    return true;
}

/*
Accept one scene-backed actor drag and convert it into a reparent command. The
Hierarchy only emits scene-document mutations here; runtime-only actors keep
their own editing path and are not reparented from this panel.
*/
bool HandleSceneActorReparentDropTarget(SceneDocument &scene_document, std::optional<std::size_t> parent_actor_index,
                                        int &selected_actor_index, std::vector<SceneFormat::SceneEditCommand> *out_edit_commands) {
    if (!ImGui::BeginDragDropTarget()) return false;

    bool scene_changed = false;
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EditorDragDrop::kSceneActorPayload)) {
        if (payload->DataSize == sizeof(SceneDocument::ActorUID) && payload->Data != nullptr) {
            const SceneDocument::ActorUID actor_uid = *static_cast<const SceneDocument::ActorUID *>(payload->Data);
            const std::optional<std::size_t> actor_index = scene_document.FindActorIndexByUID(actor_uid);
            if (actor_index.has_value()) {
                SceneFormat::SceneEditCommand command;
                if (scene_document.SetActorParent(*actor_index, parent_actor_index, &command)) {
                    selected_actor_index = static_cast<int>(*actor_index);
                    scene_changed = true;
                    if (out_edit_commands != nullptr) {
                        out_edit_commands->emplace_back(std::move(command));
                    }
                }
            }
        }
    }

    ImGui::EndDragDropTarget();
    return scene_changed;
}

/* Recursively mark a collapsed scene subtree as already accounted for. */
void MarkSceneHierarchySubtreeVisited(
    SceneDocument &scene_document, std::size_t actor_index,
    std::unordered_set<std::size_t> &visited_actor_indices) {
    if (!visited_actor_indices.insert(actor_index).second) return;

    for (std::size_t child_actor_index :
         scene_document.GetChildActorIndices(actor_index)) {
        MarkSceneHierarchySubtreeVisited(scene_document, child_actor_index,
                                        visited_actor_indices);
    }
}

/*
Render one scene-backed hierarchy node, including drag source, drop target, and
recursive child rendering. The tree itself is a derived view over the flat
SceneDocument actor list.
*/
void RenderSceneHierarchyNode(SceneDocument &scene_document,
                              std::size_t actor_index,
                              int &selected_actor_index,
                              bool scene_editing_enabled,
                              std::vector<SceneFormat::SceneEditCommand>
                                  *out_edit_commands,
                              std::unordered_set<std::size_t>
                                  &visited_actor_indices,
                              bool &out_scene_changed) {
    if (!visited_actor_indices.insert(actor_index).second) return;

    const std::vector<std::size_t> child_actor_indices = scene_document.GetChildActorIndices(actor_index);
    const bool has_children = !child_actor_indices.empty();
    const std::string actor_label = scene_document.GetActorDisplayName(actor_index);

    ImGui::PushID(static_cast<int>(actor_index));
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (!has_children) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (selected_actor_index == static_cast<int>(actor_index)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool node_open = ImGui::TreeNodeEx(actor_label.c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selected_actor_index = static_cast<int>(actor_index);
    }

    /*
    Hierarchy drag/drop uses stable scene actor UIDs so reparenting still
    works after list reorderings and while the tree is partially collapsed
    */
    if (scene_editing_enabled) {
        BeginSceneActorDragSource(scene_document.GetActorUID(actor_index), actor_label);
        out_scene_changed |= HandleSceneActorReparentDropTarget(scene_document, actor_index, selected_actor_index, out_edit_commands);
    }

    if (has_children && node_open) {
        for (std::size_t child_actor_index : child_actor_indices) {
            RenderSceneHierarchyNode(scene_document, child_actor_index,
                                     selected_actor_index,
                                     scene_editing_enabled,
                                     out_edit_commands,
                                     visited_actor_indices, out_scene_changed);
        }
        ImGui::TreePop();
    } else if (has_children) {
        for (std::size_t child_actor_index : child_actor_indices) {
            MarkSceneHierarchySubtreeVisited(scene_document, child_actor_index, visited_actor_indices);
        }
    }
    ImGui::PopID();
}

struct RuntimeHierarchyTree {
    std::vector<const Actor *> root_actors;
    std::unordered_map<Actor::UID, std::vector<const Actor *>>
        children_by_parent_uid;
};

/*
Build a temporary runtime tree view from the flat live actor container. Runtime
does not maintain a permanent children array here; the UI derives it from
parent_uid each frame it needs to draw the hierarchy.
*/
RuntimeHierarchyTree BuildRuntimeHierarchyTree(const Engine &engine) {
    RuntimeHierarchyTree tree;

    std::unordered_map<Actor::UID, const Actor *> runtime_actor_by_uid;
    for (const Actor &runtime_actor : engine.GetRuntimeActors()) {
        if (runtime_actor.runtime_destroyed) continue;
        runtime_actor_by_uid[runtime_actor.uid] = &runtime_actor;
    }

    for (const Actor &runtime_actor : engine.GetRuntimeActors()) {
        if (runtime_actor.runtime_destroyed) continue;

        const bool has_valid_parent =
            runtime_actor.parent_uid != Actor::kInvalidUID &&
            runtime_actor.parent_uid != runtime_actor.uid &&
            runtime_actor_by_uid.find(runtime_actor.parent_uid) !=
                runtime_actor_by_uid.end();
        if (!has_valid_parent) {
            tree.root_actors.emplace_back(&runtime_actor);
            continue;
        }
        tree.children_by_parent_uid[runtime_actor.parent_uid].emplace_back(
            &runtime_actor);
    }

    return tree;
}

/* Recursively mark a collapsed runtime subtree as already accounted for. */
void MarkRuntimeHierarchySubtreeVisited(
    const RuntimeHierarchyTree &tree, Actor::UID actor_uid,
    std::unordered_set<Actor::UID> &visited_actor_uids) {
    if (!visited_actor_uids.insert(actor_uid).second) return;

    auto children_it = tree.children_by_parent_uid.find(actor_uid);
    if (children_it == tree.children_by_parent_uid.end()) return;
    for (const Actor *child_actor : children_it->second) {
        if (child_actor == nullptr) continue;
        MarkRuntimeHierarchySubtreeVisited(tree, child_actor->uid,
                                           visited_actor_uids);
    }
}

/*
Render one live runtime node in play mode. Scene-backed runtime actors can
still participate in authoring-side reparent commands, while runtime-spawned
actors are displayed and selectable but remain transient.
*/
void RenderRuntimeHierarchyNode(
    SceneDocument &scene_document, const Actor &runtime_actor,
    const RuntimeHierarchyTree &tree, int &selected_actor_index,
    Actor::UID &selected_runtime_actor_uid,
    bool scene_editing_enabled,
    std::vector<SceneFormat::SceneEditCommand> *out_edit_commands,
    std::unordered_set<Actor::UID> &visited_actor_uids,
    bool &out_scene_changed) {
    if (!visited_actor_uids.insert(runtime_actor.uid).second) return;

    auto children_it = tree.children_by_parent_uid.find(runtime_actor.uid);
    const bool has_children = children_it != tree.children_by_parent_uid.end() && !children_it->second.empty();
    const std::string actor_label = runtime_actor.actor_name.empty() ? "Unnamed Actor"  : runtime_actor.actor_name;

    ImGui::PushID(static_cast<int>(runtime_actor.uid));
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (!has_children) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (selected_runtime_actor_uid == runtime_actor.uid) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool node_open = ImGui::TreeNodeEx(actor_label.c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        SelectRuntimeActor(scene_document, runtime_actor, selected_actor_index,
                           selected_runtime_actor_uid);
    }

    if (scene_editing_enabled && runtime_actor.IsSceneBacked()) {
        BeginSceneActorDragSource(runtime_actor.uid, actor_label);
        const std::optional<std::size_t> parent_actor_index = scene_document.FindActorIndexByUID(runtime_actor.uid);
        out_scene_changed |= HandleSceneActorReparentDropTarget(scene_document, parent_actor_index, selected_actor_index, out_edit_commands);
    }

    DrawRuntimeActorSourceBadge(ImGui::GetWindowDrawList(), runtime_actor,
                                ImGui::GetItemRectMin(),
                                ImGui::GetItemRectMax());

    if (has_children && node_open) {
        for (const Actor *child_actor : children_it->second) {
                if (child_actor == nullptr) continue;
                RenderRuntimeHierarchyNode(scene_document, *child_actor, tree,
                                           selected_actor_index,
                                           selected_runtime_actor_uid,
                                           scene_editing_enabled,
                                           out_edit_commands,
                                           visited_actor_uids, out_scene_changed);
        }
        ImGui::TreePop();
    } else if (has_children) {
        for (const Actor *child_actor : children_it->second) {
            if (child_actor == nullptr) continue;
            MarkRuntimeHierarchySubtreeVisited(tree, child_actor->uid,
                                               visited_actor_uids);
        }
    }
    ImGui::PopID();
}

} // namespace

/*
Hierarchy owns actor selection plus actor creation entrypoints. In edit mode it
renders the scene-document tree; in play mode it switches to a live runtime
tree, while still routing scene-backed structural edits through SceneDocument.
*/
bool RenderHierarchyPanel(Engine &engine, SceneDocument &scene_document,
                          int &selected_actor_index,
                          Actor::UID &selected_runtime_actor_uid,
                          bool play_mode_active,
                          bool scene_editing_enabled,
                          std::vector<SceneFormat::SceneEditCommand>
                              *out_edit_commands) {
    bool scene_changed = false;
    if (!ImGui::Begin("Hierarchy")) {
        ImGui::End();
        return false;
    }
    EnsureValidSceneSelection(scene_document, selected_actor_index);
    EnsureValidRuntimeSelection(engine, selected_runtime_actor_uid);

    if (!play_mode_active) {
        selected_runtime_actor_uid = Actor::kInvalidUID;
    }

    const Actor *selected_runtime_actor =
        play_mode_active ? engine.GetRuntimeActorByUID(selected_runtime_actor_uid)
                         : nullptr;
    const bool runtime_only_actor_selected = selected_runtime_actor != nullptr && selected_runtime_actor->IsRuntimeSpawned();
    const bool runtime_scene_backed_actor_selected = selected_runtime_actor != nullptr &&
                                                     selected_runtime_actor->IsSceneBacked() && selected_actor_index >= 0;
    const bool duplicate_delete_available = (!play_mode_active && selected_actor_index >= 0) || (play_mode_active && selected_runtime_actor != nullptr);

    if (play_mode_active) {
        ImGui::TextDisabled("Play mode: Hierarchy reflects the live runtime actor list.");
    }
    if (runtime_only_actor_selected) {
        ImGui::TextDisabled( "Runtime-spawned actor selected. Duplicate/Delete affect only the live runtime.");
    }

    /*
    Actor creation entrypoint. The popup provides both empty actor and
    template-based creation paths.
    */
    ImGui::BeginDisabled(!scene_editing_enabled);
    if (ImGui::Button("+ Add Actor")) {
        ImGui::OpenPopup("add_actor_popup");
    }
    scene_changed |= RenderAddActorPopup(scene_document, selected_actor_index, out_edit_commands);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!scene_editing_enabled);
    scene_changed |= RenderHierarchyTemplateDropTarget(scene_document, selected_actor_index, out_edit_commands);
    ImGui::EndDisabled();
    ImGui::Separator();

    /*
    Actor-level edit operations stay in hierarchy so users can manage object
    list shape quickly without switching to inspector first.
    */
    ImGui::BeginDisabled(!scene_editing_enabled || !duplicate_delete_available);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
    if (ImGui::SmallButton("Duplicate")) {
        if (play_mode_active && runtime_scene_backed_actor_selected) {
            std::size_t duplicated_actor_index = 0;
            SceneFormat::SceneEditCommand command;
            if (scene_document.DuplicateActor(static_cast<std::size_t>(selected_actor_index), duplicated_actor_index, &command)) {
                selected_actor_index = static_cast<int>(duplicated_actor_index);
                selected_runtime_actor_uid = Actor::kInvalidUID;
                scene_changed = true;
                if (out_edit_commands != nullptr) {
                    out_edit_commands->emplace_back(std::move(command));
                }
            }
        } 
        else if (play_mode_active && runtime_only_actor_selected) {
            Actor::UID duplicated_runtime_actor_uid = Actor::kInvalidUID;
            if (engine.DuplicateRuntimeActorByUID(selected_runtime_actor_uid,
                                                  &duplicated_runtime_actor_uid)) {
                selected_runtime_actor_uid = duplicated_runtime_actor_uid;
                selected_actor_index = -1;
            }
        } 
        // not in play mode 
        else if (selected_actor_index >= 0) {
            std::size_t duplicated_actor_index = 0;
            SceneFormat::SceneEditCommand command;
            if (scene_document.DuplicateActor(static_cast<std::size_t>(selected_actor_index), duplicated_actor_index, &command)) {
                selected_actor_index = static_cast<int>(duplicated_actor_index);
                scene_changed = true;
                if (out_edit_commands != nullptr) {
                    out_edit_commands->emplace_back(std::move(command));
                }
            }
        }
    }
    ImGui::SameLine();
    // handle delete actor button and popup
    if (ImGui::SmallButton("Delete")) {
        if (play_mode_active && runtime_scene_backed_actor_selected) {
            SceneFormat::SceneEditCommand command;
            if (scene_document.DeleteActor(static_cast<std::size_t>(selected_actor_index), &command)) {
                selected_runtime_actor_uid = Actor::kInvalidUID;
                if (scene_document.GetActorCount() == 0) {
                    selected_actor_index = -1;
                } else if (selected_actor_index >=
                           static_cast<int>(scene_document.GetActorCount())) {
                               selected_actor_index = static_cast<int>(scene_document.GetActorCount()) - 1;
                }
                scene_changed = true;
                if (out_edit_commands != nullptr) {
                    out_edit_commands->emplace_back(std::move(command));
                }
            }
        } else if (play_mode_active && runtime_only_actor_selected) {
            if (engine.DeleteRuntimeActorByUID(selected_runtime_actor_uid)) {
                selected_runtime_actor_uid = Actor::kInvalidUID;
                selected_actor_index = -1;
            }
        } else if (selected_actor_index >= 0) {
            SceneFormat::SceneEditCommand command;
            if (scene_document.DeleteActor(static_cast<std::size_t>(selected_actor_index), &command)) {
                if (scene_document.GetActorCount() == 0) {
                    selected_actor_index = -1;
                }  
                else if (selected_actor_index >= static_cast<int>(scene_document.GetActorCount())) {
                    selected_actor_index = static_cast<int>(scene_document.GetActorCount()) - 1;
                }
                scene_changed = true;
                if (out_edit_commands != nullptr) {
                    out_edit_commands->emplace_back(std::move(command));
                }
            }
        }
    }
    ImGui::PopStyleVar();
    if (!play_mode_active) {
        ImGui::SameLine();
    }
    if (!play_mode_active &&
        ImGui::SmallButton("Rename") && selected_actor_index >= 0) {
        ImGui::OpenPopup("rename_actor_popup");
    }
    if (!play_mode_active && ImGui::BeginPopup("rename_actor_popup")) {
        static std::string renamed_actor_name;
        if (renamed_actor_name.empty() && selected_actor_index >= 0) {
            renamed_actor_name = scene_document.GetActorDisplayName(static_cast<std::size_t>(selected_actor_index));
        }
        std::string updated_name;
        if (InputTextString("##rename_actor_name", renamed_actor_name, updated_name)) {
            renamed_actor_name = updated_name;
        }
        // if Apply is clicked, attempt to rename the actor. On success, close the popup and clear the temporary name buffer
        if (ImGui::Button("Apply") && selected_actor_index >= 0) {
            SceneFormat::SceneEditCommand command;
            const bool renamed = scene_document.SetActorName( static_cast<std::size_t>(selected_actor_index), renamed_actor_name, &command);
            scene_changed |= renamed;
            if (renamed && out_edit_commands != nullptr) {
                out_edit_commands->emplace_back(std::move(command));
            }
            renamed_actor_name.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        // otherwise if Cancel is clicked, just close the popup and clear the temporary name buffer, do nothing
        if (ImGui::Button("Cancel")) {
            renamed_actor_name.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    if (!play_mode_active && scene_document.GetActorCount() == 0) {
        ImGui::BeginDisabled(!scene_editing_enabled);
        ImGui::Button("Drop Here To Make Root",
                      ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
        if (scene_editing_enabled) {
            scene_changed |= HandleSceneActorReparentDropTarget(
                scene_document, std::nullopt, selected_actor_index,
                out_edit_commands);
        }
        ImGui::EndDisabled();
        ImGui::TextUnformatted("Scene has no actors.");
        ImGui::End();
        return scene_changed;
    }

    if (play_mode_active) {
        const RuntimeHierarchyTree runtime_tree = BuildRuntimeHierarchyTree(engine);
        bool has_live_runtime_actor = false;
        for (const Actor &runtime_actor : engine.GetRuntimeActors()) {
            if (runtime_actor.runtime_destroyed) continue;
            has_live_runtime_actor = true;
            break;
        }

        if (!has_live_runtime_actor) {
            ImGui::TextUnformatted("Runtime currently has no live actors.");
        } else {
            std::unordered_set<Actor::UID> visited_actor_uids;
            for (const Actor *root_actor : runtime_tree.root_actors) {
                if (root_actor == nullptr) continue;
                RenderRuntimeHierarchyNode(scene_document, *root_actor,
                                           runtime_tree, selected_actor_index,
                                           selected_runtime_actor_uid,
                                           scene_editing_enabled,
                                           out_edit_commands,
                                           visited_actor_uids, scene_changed);
            }
            for (const Actor &runtime_actor : engine.GetRuntimeActors()) {
                if (runtime_actor.runtime_destroyed) continue;
                if (visited_actor_uids.find(runtime_actor.uid) !=
                    visited_actor_uids.end()) {
                    continue;
                }
                RenderRuntimeHierarchyNode(scene_document, runtime_actor,
                                           runtime_tree, selected_actor_index,
                                           selected_runtime_actor_uid,
                                           scene_editing_enabled,
                                           out_edit_commands,
                                           visited_actor_uids, scene_changed);
            }
        }
    } else {
        std::unordered_set<std::size_t> visited_actor_indices;
        const std::vector<std::size_t> root_actor_indices = scene_document.GetRootActorIndices();
        for (std::size_t actor_index : root_actor_indices) {
            RenderSceneHierarchyNode(scene_document, actor_index,
                                     selected_actor_index,
                                     scene_editing_enabled,
                                     out_edit_commands,
                                     visited_actor_indices, scene_changed);
        }
        for (std::size_t actor_index = 0;
             actor_index < scene_document.GetActorCount(); ++actor_index) {
            if (visited_actor_indices.find(actor_index) !=
                visited_actor_indices.end()) {
                continue;
            }
            RenderSceneHierarchyNode(scene_document, actor_index,
                                     selected_actor_index,
                                     scene_editing_enabled,
                                     out_edit_commands,
                                     visited_actor_indices, scene_changed);
        }
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!scene_editing_enabled);
    ImGui::Button("Drop Here To Make Root",
                  ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
    if (scene_editing_enabled) {
        scene_changed |= HandleSceneActorReparentDropTarget(
            scene_document, std::nullopt, selected_actor_index,
            out_edit_commands);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Drag a scene-backed actor here to clear its parent.");

    ImGui::End();
    return scene_changed;
}

} // namespace EditorPanels
