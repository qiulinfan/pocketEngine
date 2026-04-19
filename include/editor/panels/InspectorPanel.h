#ifndef EDITOR_INSPECTOR_PANEL_H
#define EDITOR_INSPECTOR_PANEL_H

#include "scene/Actor.h"
#include "shared/scene_format/SceneMutation.h"
#include <vector>

class SceneDocument;
class Engine;

namespace EditorPanels {

// Render the inspector for the selected actor and report whether the scene
// document cache changed during this frame.
bool RenderInspectorPanel(const Engine &engine,
                          SceneDocument &scene_document,
                          int &selected_actor_index,
                          Actor::UID selected_runtime_actor_uid,
                          bool play_mode_active,
                          bool scene_editing_enabled,
                          std::vector<SceneFormat::SceneEditCommand>
                              *out_edit_commands = nullptr);

} // namespace EditorPanels

#endif
