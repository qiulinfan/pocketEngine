#ifndef EDITOR_SCENE_PANEL_H
#define EDITOR_SCENE_PANEL_H

#include <cstddef>
#include "shared/scene_format/SceneMutation.h"
#include <vector>

class Engine;
class SceneDocument;

namespace EditorPanels {

struct ScenePanelResult {
    bool scene_changed = false;
    bool play_mode_start_requested = false;
    bool play_mode_pause_toggle_requested = false;
    bool play_mode_stop_requested = false;
    bool has_runtime_image = false;
    float runtime_image_min_x = 0.0f;
    float runtime_image_min_y = 0.0f;
    float runtime_image_max_x = 0.0f;
    float runtime_image_max_y = 0.0f;
    bool has_transport_bar = false;
    float transport_bar_min_x = 0.0f;
    float transport_bar_min_y = 0.0f;
    float transport_bar_max_x = 0.0f;
    float transport_bar_max_y = 0.0f;
    std::size_t immediate_apply_begin_index = 0;
    std::size_t immediate_apply_end_index = 0;
};

// Render the editor-facing scene panel on top of the latest runtime texture.
// This panel owns scene selection and transform-gizmo interactions.
ScenePanelResult RenderScenePanel(
    Engine &engine, SceneDocument &scene_document,
    int &selected_actor_index, int &selected_runtime_actor_id,
    int scene_view_width, int scene_view_height, bool play_mode_active,
    bool play_mode_paused, bool scene_editing_enabled,
    std::vector<SceneFormat::SceneEditCommand> *out_edit_commands);

} // namespace EditorPanels

#endif
