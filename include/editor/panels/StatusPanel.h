#ifndef EDITOR_STATUS_PANEL_H
#define EDITOR_STATUS_PANEL_H

class Engine;
class SceneDocument;

namespace EditorPanels {

// Render one dockable editor status panel that groups runtime/editor
// diagnostics together instead of scattering them across overlay-local HUDs.
void RenderStatusPanel(const Engine &engine, const SceneDocument &scene_document,
                       int selected_actor_index, int selected_runtime_actor_id,
                       bool play_mode_active, bool play_mode_paused,
                       float applied_ui_scale);

} // namespace EditorPanels

#endif
