#ifndef EDITOR_CORE_EDITOR_OVERLAY_H
#define EDITOR_CORE_EDITOR_OVERLAY_H

#include "editor/core/EditorConfig.h"
#include "scene/Actor.h"
#include "shared/config/GameConfig.h"
#include "shared/scene_format/SceneMutation.h"
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

union SDL_Event;
struct SDL_Window;
struct SDL_Renderer;

class Engine;
class SceneDocument;
struct EditorConfigData;

struct EditorOverlayResult {
    bool save_scene_requested = false;
    bool scene_changed = false;
    std::vector<SceneFormat::SceneEditCommand> scene_edit_commands;
    bool editor_config_changed = false;
    bool editor_window_settings_changed = false;
    bool editor_config_apply_requested = false;
    bool editor_config_revert_requested = false;
    bool open_scene_requested = false;
    std::filesystem::path requested_scene_path;
    bool open_external_editor_requested = false;
    EditorExternalFileType requested_external_file_type = EditorExternalFileType::Generic;
    std::filesystem::path requested_external_file_path;
    std::filesystem::path requested_external_resources_root;
    bool play_mode_start_requested = false;
    bool play_mode_pause_toggle_requested = false;
    bool play_mode_stop_requested = false;
    bool toggle_fullscreen_requested = false;
    std::size_t scene_edit_commands_runtime_synced_begin_index = 0;
    std::size_t scene_edit_commands_runtime_synced_end_index = 0;
};

class EditorOverlay {
public:
    ~EditorOverlay();

    // Create the ImGui context and bind it to the shared SDL window/renderer.
    // 在共享的 SDL 窗口/渲染器上创建 ImGui 上下文并绑定.
    bool Initialize(SDL_Window *window, SDL_Renderer *renderer);
    // Apply cached editor settings to the live ImGui style and font scale.
    void ApplyConfig(const EditorConfigData &config, int window_width,
                     int window_height);
    // Update routing context used to decide whether input goes to editor or
    // runtime while the viewport is embedded.
    void SetRuntimeInputRoutingState(bool play_mode_active,
                                     bool play_mode_paused);
    // Forward one SDL event to ImGui and report whether the editor captured it.
    // 将一个 SDL 事件转发给 ImGui, 并报告编辑器是否捕获了它.
    bool ProcessEvent(const SDL_Event &event);
    // Draw the editor UI for the current scene cache and runtime frame.
    // 绘制当前场景缓存和运行时帧对应的编辑器 UI.
    EditorOverlayResult Render(Engine &engine, SceneDocument &scene_document,
                               EditorConfigData &editor_config,
                               bool editor_config_confirmation_pending,
                               bool play_mode_active, bool play_mode_paused,
                               bool scene_editing_enabled,
                               bool scene_save_enabled);
    // Release ImGui backend resources and destroy the ImGui context.
    // 释放 ImGui 后端资源并销毁 ImGui 上下文.
    void Shutdown();

private:
    // Lazily load the project-visible game/rendering config cache from disk.
    void EnsureProjectConfigLoaded();
    // Queue a short-lived top-right notice for editor actions and failures.
    void ShowTransientNotice(const std::string &text,
                             double duration_seconds = 4.0);
    // Queue a short-lived popup when a file type has no external tool command.
    void ShowExternalEditorMissingConfigNotice(EditorExternalFileType type);
    // Render the project-level config editors that write directly to disk.
    void RenderProjectConfigWindows();
    // Render the editor-local settings window that edits overlay/window
    // parameters instead of asking users to edit editor.config by hand.
    void RenderEditorSettingsWindow(Engine &engine, EditorConfigData &editor_config,
                                    EditorOverlayResult &result);
    // Render the centered non-modal confirmation window for pending overlay
    // layout/settings changes.
    void RenderEditorConfigConfirmationWindow( bool editor_config_confirmation_pending, EditorOverlayResult &result);
    // Render non-blocking top-right notifications for editor actions.
    void RenderTransientNotifications();

    // Overlay keeps only editor-session UI state here. Authoritative scene
    // content lives in SceneDocument, and authoritative runtime state lives in
    // Engine / the runtime copy of the scene.
    bool initialized_ = false;
    bool show_metrics_window_ = false;
    bool show_editor_settings_window_ = false;
    bool show_game_config_window_ = false;
    bool show_rendering_config_window_ = false;
    bool show_external_editors_window_ = false;
    float applied_ui_scale_ = 1.0f;
    bool project_config_loaded_ = false;
    GameConfigData project_config_cache_;
    int selected_actor_index_ = -1;
    Actor::UID selected_runtime_actor_uid_ = Actor::kInvalidUID;
    bool play_mode_active_for_input_ = false;
    bool play_mode_paused_for_input_ = false;
    bool runtime_input_focus_ = false;
    bool viewport_runtime_image_valid_ = false;
    float viewport_runtime_image_min_x_ = 0.0f;
    float viewport_runtime_image_min_y_ = 0.0f;
    float viewport_runtime_image_max_x_ = 0.0f;
    float viewport_runtime_image_max_y_ = 0.0f;
    bool viewport_transport_bar_valid_ = false;
    float viewport_transport_bar_min_x_ = 0.0f;
    float viewport_transport_bar_min_y_ = 0.0f;
    float viewport_transport_bar_max_x_ = 0.0f;
    float viewport_transport_bar_max_y_ = 0.0f;
    bool show_transient_notice_ = false;
    double transient_notice_expire_time_ = 0.0;
    std::string transient_notice_text_;
};

#endif
