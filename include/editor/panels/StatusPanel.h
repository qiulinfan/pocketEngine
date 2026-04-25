#ifndef EDITOR_STATUS_PANEL_H
#define EDITOR_STATUS_PANEL_H

#include "scene/Actor.h"

class Engine;
class SceneDocument;

namespace EditorPanels {

// Render one dockable editor status panel that groups runtime/editor
// diagnostics together instead of scattering them across overlay-local HUDs.
void RenderStatusPanel(const Engine &engine, const SceneDocument &scene_document,
                       int selected_actor_index,
                       Actor::UID selected_runtime_actor_uid,
                       bool play_mode_active, bool play_mode_paused,
                       bool edit_mode_live_preview_enabled,
                       float applied_ui_scale);

} // namespace EditorPanels

#endif
