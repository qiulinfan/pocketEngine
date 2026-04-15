#ifndef EDITOR_VIEWPORT_PANEL_H
#define EDITOR_VIEWPORT_PANEL_H

class Engine;

namespace EditorPanels {

struct ViewportControlsResult {
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
};

// Render the embedded runtime texture into the docked viewport panel.
ViewportControlsResult RenderViewportPanel(const Engine &engine,
                                           bool play_mode_active,
                                           bool play_mode_paused);

} // namespace EditorPanels

#endif
