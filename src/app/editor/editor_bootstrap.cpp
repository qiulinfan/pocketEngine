#include "app/editor/EditorApp.h"
#include "input/SDLEventHelper.h"
#include "SDL2/SDL.h"
#include "SDL2_image/SDL_image.h"
#include "shared/resources/ResourcePath.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

std::string QuotePosixShell(const std::string &value) {
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

#ifdef _WIN32
std::string QuoteWindowsShell(const std::string &value) {
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
}
#endif

bool RunPlatformCommand(const std::string &command) {
    if (command.empty()) return false;
    return std::system(command.c_str()) == 0;
}

bool ReplaceAllInPlace(std::string &text, const std::string &needle,
                       const std::string &replacement) {
    if (needle.empty()) return false;
    bool replaced = false;
    std::size_t cursor = 0;
    while ((cursor = text.find(needle, cursor)) != std::string::npos) {
        text.replace(cursor, needle.size(), replacement);
        cursor += replacement.size();
        replaced = true;
    }
    return replaced;
}

bool RunExternalCommand(const std::string &command,
                        const std::filesystem::path &clicked_file,
                        const std::filesystem::path &resources_root) {
    const std::string file_value = clicked_file.string();
    if (command.empty() || file_value.empty()) return false;

    const std::string resources_value = resources_root.string();
#ifdef _WIN32
    const std::string quoted_file = QuoteWindowsShell(file_value);
    const std::string quoted_resources = QuoteWindowsShell(resources_value);
#else
    const std::string quoted_file = QuotePosixShell(file_value);
    const std::string quoted_resources = QuotePosixShell(resources_value);
#endif

    std::string expanded_command = command;
    bool used_placeholder = false;
    used_placeholder |= ReplaceAllInPlace(expanded_command, "{file}", quoted_file);
    used_placeholder |= ReplaceAllInPlace(expanded_command, "{resources}", quoted_resources);

    // Backward-compatible default: if no placeholders are present, pass the
    // clicked file path as the final argument so double-click opens that file.
    if (!used_placeholder) {
        expanded_command += " ";
        expanded_command += quoted_file;
    }

#ifdef _WIN32
    return RunPlatformCommand("cmd /C " + expanded_command);
#else
    return RunPlatformCommand(expanded_command);
#endif
}

bool IsBlankCommand(const std::string &command) {
    return std::all_of(command.begin(), command.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

std::filesystem::path ResolveExistingPathOrFallback(
    const std::filesystem::path &candidate,
    const std::filesystem::path &fallback) {
    if (!candidate.empty() && std::filesystem::exists(candidate)) {
        return candidate.lexically_normal();
    }
    return fallback.lexically_normal();
}

struct WindowedPlacement {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

int ClampToRange(int value, int minimum, int maximum) {
    if (minimum > maximum) return minimum;
    return std::max(minimum, std::min(value, maximum));
}

SDL_Rect QueryUsableDisplayBounds(SDL_Window *window) {
    SDL_Rect bounds{0, 0, 1600, 900};
    int display_index = 0;
    if (window != nullptr) {
        const int queried_display_index = SDL_GetWindowDisplayIndex(window);
        if (queried_display_index >= 0) {
            display_index = queried_display_index;
        }
    }

    if (SDL_GetDisplayUsableBounds(display_index, &bounds) == 0 &&
        bounds.w > 0 && bounds.h > 0) {
        return bounds;
    }
    if (SDL_GetDisplayBounds(display_index, &bounds) == 0 && bounds.w > 0 &&
        bounds.h > 0) {
        return bounds;
    }
    return bounds;
}

WindowedPlacement BuildWindowedPlacement(const EditorConfigData &editor_config,
                                         const GameConfigData &runtime_config,
                                         SDL_Window *window) {
    const SDL_Rect usable_bounds = QueryUsableDisplayBounds(window);
    WindowedPlacement placement;
    placement.width = ClampToRange(
        std::max(editor_config.window_width, runtime_config.window_width),
        runtime_config.window_width,
        std::max(runtime_config.window_width, usable_bounds.w));
    placement.height = ClampToRange(
        std::max(editor_config.window_height, runtime_config.window_height),
        runtime_config.window_height,
        std::max(runtime_config.window_height, usable_bounds.h));

    const int max_x = usable_bounds.x + std::max(0, usable_bounds.w - placement.width);
    const int max_y = usable_bounds.y + std::max(0, usable_bounds.h - placement.height);
    if (editor_config.window_has_position) {
        placement.x = ClampToRange(editor_config.window_x, usable_bounds.x, max_x);
        placement.y = ClampToRange(editor_config.window_y, usable_bounds.y, max_y);
    } else {
        placement.x = usable_bounds.x + std::max(0, (usable_bounds.w - placement.width) / 2);
        placement.y = usable_bounds.y + std::max(0, (usable_bounds.h - placement.height) / 2);
    }
    return placement;
}

bool OpenExternalEditor(const EditorConfigData &editor_config,
                        EditorExternalFileType file_type,
                        const std::filesystem::path &clicked_file,
                        const std::filesystem::path &resources_root) {
    const std::filesystem::path normalized_resources =
        ResolveExistingPathOrFallback(resources_root, "resources");
    const std::filesystem::path normalized_file =
        ResolveExistingPathOrFallback(clicked_file, normalized_resources);

    const std::string configured_command =
        EditorConfig::ExternalEditorCommand(editor_config, file_type);
    if (IsBlankCommand(configured_command)) return false;
    return RunExternalCommand(configured_command, normalized_file,
                              normalized_resources);
}

void ApplyEditorWindowIcon(SDL_Window *window) {
    if (window == nullptr) return;

    const std::filesystem::path primary_icon_path = ResourcePath::EngineSystemIconPath();
    const std::filesystem::path fallback_icon_path = "docs/icon.png";
    const std::filesystem::path icon_path =
        std::filesystem::exists(primary_icon_path)
            ? primary_icon_path
            : fallback_icon_path;

    if (!std::filesystem::exists(icon_path)) return;

    SDL_Surface *icon_surface = IMG_Load(icon_path.string().c_str());
    if (icon_surface == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "EditorApp: failed to load icon from %s: %s",
                    icon_path.string().c_str(), IMG_GetError());
        return;
    }

    SDL_SetWindowIcon(window, icon_surface);
    SDL_FreeSurface(icon_surface);
}

bool AreEditorConfigsEqualForConfirmation(const EditorConfigData &lhs,
                                          const EditorConfigData &rhs) {
    return lhs.window_width == rhs.window_width &&
           lhs.window_height == rhs.window_height &&
           lhs.window_fullscreen == rhs.window_fullscreen &&
           lhs.window_maximized == rhs.window_maximized &&
           lhs.window_title == rhs.window_title &&
           lhs.ui_scale == rhs.ui_scale &&
           lhs.reference_width == rhs.reference_width &&
           lhs.reference_height == rhs.reference_height &&
           lhs.external_editor_audio == rhs.external_editor_audio &&
           lhs.external_editor_lua == rhs.external_editor_lua &&
           lhs.external_editor_config == rhs.external_editor_config &&
           lhs.external_editor_template == rhs.external_editor_template &&
           lhs.external_editor_image == rhs.external_editor_image &&
           lhs.external_editor_font == rhs.external_editor_font &&
           lhs.external_editor_scene == rhs.external_editor_scene &&
           lhs.external_editor_generic == rhs.external_editor_generic;
}

void CopyAutoAcceptedEditorPlacement(EditorConfigData &destination,
                                     const EditorConfigData &source) {
    destination.window_x = source.window_x;
    destination.window_y = source.window_y;
    destination.window_has_position = source.window_has_position;
}

} // namespace

void EditorApp::PersistConfirmedEditorConfig() {
    confirmed_editor_config_ = editor_config_;
    EditorConfig::Write(confirmed_editor_config_);
    editor_config_dirty_ = false;
    confirmed_editor_config_persist_dirty_ = false;
}

void EditorApp::RevertUnconfirmedEditorConfigChanges() {
    editor_config_ = confirmed_editor_config_;
    ApplyEditorWindowSettings();
    SyncWindowStateFromLiveWindow();
    editor_config_dirty_ = false;
}

// Apply the cached editor host config to the live SDL window.
void EditorApp::ApplyEditorWindowSettings() {
    SDL_Window *window = engine_.GetWindow();
    if (window == nullptr) return;

    const GameConfigData &runtime_config = engine_.GetConfig();
    const WindowedPlacement placement = BuildWindowedPlacement(editor_config_, runtime_config, window);

    // The editor host window is independent from the runtime resolution.
    SDL_SetWindowResizable(window, SDL_TRUE);
    SDL_SetWindowMinimumSize(window, runtime_config.window_width, runtime_config.window_height);
    SDL_RestoreWindow(window);
    if (editor_config_.window_fullscreen) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        editor_config_.window_width = placement.width;
        editor_config_.window_height = placement.height;
        editor_config_.window_x = placement.x;
        editor_config_.window_y = placement.y;
        editor_config_.window_has_position = true;
        SDL_SetWindowFullscreen(window, 0);
        SDL_SetWindowSize(window, placement.width, placement.height);
        SDL_SetWindowPosition(window, placement.x, placement.y);
        if (editor_config_.window_maximized) {
            SDL_MaximizeWindow(window);
        }
    }
    SDL_SetWindowTitle(window, "PocketEngine Editor");
}

void EditorApp::SetEditorFullscreen(bool enabled) {
    SDL_Window *window = engine_.GetWindow();
    if (window == nullptr) return;

    if (enabled) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_SetWindowFullscreen(window, 0);
        const WindowedPlacement placement =
            BuildWindowedPlacement(editor_config_, engine_.GetConfig(), window);
        SDL_RestoreWindow(window);
        SDL_SetWindowSize(window, placement.width, placement.height);
        SDL_SetWindowPosition(window, placement.x, placement.y);
        if (editor_config_.window_maximized) {
            SDL_MaximizeWindow(window);
        }
    }

    editor_config_.window_fullscreen = enabled;
    SyncWindowStateFromLiveWindow();
}

bool EditorApp::IsEditorWindowFullscreen() const {
    SDL_Window *window = engine_.GetWindow();
    if (window == nullptr) return false;

    const Uint32 window_flags = SDL_GetWindowFlags(window);
    return (window_flags & SDL_WINDOW_FULLSCREEN) != 0 ||
           (window_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

bool EditorApp::IsEditorWindowMaximized() const {
    SDL_Window *window = engine_.GetWindow();
    if (window == nullptr) return false;

    const Uint32 window_flags = SDL_GetWindowFlags(window);
    return (window_flags & SDL_WINDOW_MAXIMIZED) != 0;
}

// Pull the live SDL window geometry back into editor config.
void EditorApp::SyncWindowStateFromLiveWindow() {
    SDL_Window *window = engine_.GetWindow();
    if (window == nullptr) return;

    int width = engine_.GetWindowWidth();
    int height = engine_.GetWindowHeight();
    if (width <= 0 || height <= 0) return;

    const bool is_fullscreen = IsEditorWindowFullscreen();
    const bool is_maximized = !is_fullscreen && IsEditorWindowMaximized();
    editor_config_.window_fullscreen = is_fullscreen;
    if (!is_fullscreen) {
        editor_config_.window_maximized = is_maximized;
    }
    if (!is_fullscreen && !is_maximized &&
        (editor_config_.window_width != width ||
         editor_config_.window_height != height)) {
        editor_config_.window_width = width;
        editor_config_.window_height = height;
    }

    if (!is_fullscreen && !is_maximized) {
        int x = 0;
        int y = 0;
        SDL_GetWindowPosition(window, &x, &y);
        if (editor_config_.window_x != x || editor_config_.window_y != y ||
            !editor_config_.window_has_position) {
            editor_config_.window_x = x;
            editor_config_.window_y = y;
            editor_config_.window_has_position = true;
            CopyAutoAcceptedEditorPlacement(confirmed_editor_config_,
                                            editor_config_);
            confirmed_editor_config_persist_dirty_ = true;
        }
    }

    editor_config_dirty_ =
        !AreEditorConfigsEqualForConfirmation(editor_config_,
                                              confirmed_editor_config_);
    overlay_.ApplyConfig(editor_config_, width, height);
}

void EditorApp::HandleWindowEvent(const SDL_WindowEvent &window_event) {
    switch (window_event.event) {
    case SDL_WINDOWEVENT_MOVED:
    case SDL_WINDOWEVENT_SIZE_CHANGED:
    case SDL_WINDOWEVENT_MAXIMIZED:
    case SDL_WINDOWEVENT_RESTORED:
        SyncWindowStateFromLiveWindow();
        break;
    default:
        break;
    }
}

/*
editor app.
Initialize the editor host and runtime to be embedded.
then start the editor main loop:
- capture SDL events and route them to editor overlay / runtime.
- run the runtime in either play mode, pause mode, or edit mode.
*/
void EditorApp::Run() {
    // Boot the editor host first, then attach the shared runtime to the docked viewport
    editor_config_ = EditorConfig::Read();
    confirmed_editor_config_ = editor_config_;
    editor_config_dirty_ = false;
    confirmed_editor_config_persist_dirty_ = false;

    // start engine runtime
    engine_.InitializeRuntime();
    ApplyEditorWindowIcon(engine_.GetWindow());
    ApplyEditorWindowSettings();
    engine_.SetRenderRuntimeToTexture(true);
    scene_session_.LoadInitialScene(engine_);

    overlay_.Initialize(engine_.GetWindow(), engine_.GetRenderer());
    SyncWindowStateFromLiveWindow();
    confirmed_editor_config_ = editor_config_;
    editor_config_dirty_ = false;
    confirmed_editor_config_persist_dirty_ = false;

    while (engine_.IsRunning()) {
        /*
        Scene edits are mirrored into runtime at frame boundaries so the UI
        never mutates live runtime state in the middle of a frame.
        */
        scene_session_.SyncRuntimeMirrorIfDirty(engine_);
        overlay_.SetRuntimeInputRoutingState(scene_session_.IsPlayModeActive(), scene_session_.IsPlayModePaused());

        bool quit_requested_this_frame = false;
        SDL_Event event;
        while (SDLEventHelper::SDL_PollEvent(&event)) {
            const bool captured_by_editor = overlay_.ProcessEvent(event);
            if (event.type == SDL_WINDOWEVENT) {
                HandleWindowEvent(event.window);
            }
            /*
            Window lifecycle events must still reach the runtime 
            even when ImGui is actively capturing keyboard or mouse input.
            */
            const bool always_forward_to_runtime = event.type == SDL_QUIT || event.type == SDL_WINDOWEVENT;
            if (!captured_by_editor || always_forward_to_runtime) {
                engine_.ProcessSDLEvent(event, quit_requested_this_frame);
            }
        }

        const bool play_mode_active = scene_session_.IsPlayModeActive();
        const bool play_mode_paused = scene_session_.IsPlayModePaused();
        const bool advance_gameplay_frame = play_mode_active && !play_mode_paused;
        if (advance_gameplay_frame) {
            // Play mode advances simulation (OnUpdate + physics).
            engine_.RunSingleFrame(quit_requested_this_frame);
        } else if (play_mode_active && play_mode_paused) {
            // Pause keeps the exact last gameplay frame visible.
            engine_.RunPresentPausedFrame(quit_requested_this_frame);
        } else {
            // Edit mode renders a non-simulating preview from current state.
            engine_.RunRenderFrozenFrame(quit_requested_this_frame);
        }
        if (scene_session_.HasSceneDocument()) {
            const EditorOverlayResult overlay_result =
                overlay_.Render(engine_, scene_session_.GetSceneDocument(),
                                editor_config_,
                                editor_config_dirty_,
                                scene_session_.IsPlayModeActive(),
                                scene_session_.IsPlayModePaused(),
                                scene_session_.IsSceneEditingEnabled(),
                                scene_session_.IsSceneSaveEnabled());
            for (std::size_t command_index = 0;
                 command_index < overlay_result.scene_edit_commands.size();
                 ++command_index) {
                if (command_index >=
                        overlay_result
                            .scene_edit_commands_runtime_synced_begin_index &&
                    command_index <
                        overlay_result
                            .scene_edit_commands_runtime_synced_end_index) {
                    continue;
                }
                scene_session_.HandleSceneEditCommand( overlay_result.scene_edit_commands[command_index], engine_);
            }
            if (overlay_result.play_mode_start_requested) {
                scene_session_.EnterPlayMode(engine_);
            }
            if (overlay_result.play_mode_pause_toggle_requested) {
                scene_session_.TogglePlayPause();
            }
            if (overlay_result.play_mode_stop_requested) {
                scene_session_.ExitPlayMode(engine_);
            }
            /*
            Panels report scene_changed for any document mutation, but command-
            driven edits are already mirrored incrementally via
            HandleSceneEditCommands(). Only request a full mirror rebuild for
            scene changes that did not produce an explicit edit command.
            */
            if (overlay_result.scene_changed &&
                overlay_result.scene_edit_commands.empty()) {
                scene_session_.MarkRuntimeMirrorDirty();
            }
            if (overlay_result.save_scene_requested) {
                scene_session_.SaveDocumentIfDirty();
            }
            if (overlay_result.editor_config_changed) {
                editor_config_dirty_ =
                    !AreEditorConfigsEqualForConfirmation( editor_config_, confirmed_editor_config_);
            }
            if (overlay_result.editor_window_settings_changed) {
                ApplyEditorWindowSettings();
                SyncWindowStateFromLiveWindow();
            }
            if (overlay_result.editor_config_apply_requested) {
                PersistConfirmedEditorConfig();
            }
            if (overlay_result.editor_config_revert_requested) {
                RevertUnconfirmedEditorConfigChanges();
            }
            if (overlay_result.toggle_fullscreen_requested) {
                SetEditorFullscreen(!IsEditorWindowFullscreen());
            }
            if (overlay_result.open_scene_requested) {
                const std::string &scene_external_command =
                    EditorConfig::ExternalEditorCommand( editor_config_, EditorExternalFileType::Scene);
                if (!IsBlankCommand(scene_external_command)) {
                    OpenExternalEditor(editor_config_,
                                       EditorExternalFileType::Scene,
                                       overlay_result.requested_scene_path,
                                       "resources");
                } else {
                    /*
                    scene switch: flush dirty cache first, 
                    then load the new document and rebuild runtime mirror on the next frame boundary.
                    */
                    scene_session_.OpenSceneFromPath(overlay_result.requested_scene_path, engine_);
                }
            }
            if (overlay_result.open_external_editor_requested) {
                OpenExternalEditor(editor_config_,
                                   overlay_result.requested_external_file_type,
                                   overlay_result.requested_external_file_path,
                                   overlay_result.requested_external_resources_root);
            }
        }
        // Present once after both runtime and editor UI have been drawn
        engine_.PresentFrame(advance_gameplay_frame);
    }

    // On shutdown, persist only editor document cache and discard runtime-only play-state mutations
    scene_session_.PersistDocumentOnEditorShutdown();
    if (confirmed_editor_config_persist_dirty_) {
        EditorConfig::Write(confirmed_editor_config_);
        confirmed_editor_config_persist_dirty_ = false;
    }
    overlay_.Shutdown();
    engine_.ShutdownRuntime();
}
