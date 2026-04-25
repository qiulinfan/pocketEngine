#ifndef EDITOR_CONFIG_H
#define EDITOR_CONFIG_H

#include <array>
#include <filesystem>
#include <string>
#include <vector>

enum class EditorExternalFileType {
    Audio,
    Lua,
    Config,
    Template,
    Image,
    Font,
    Scene,
    Generic
};

struct EditorConfigData {
    int window_width = 1600;
    int window_height = 960;
    int window_x = 160;
    int window_y = 60;
    bool window_has_position = true;
    bool window_fullscreen = false;
    bool window_maximized = false;
    std::string window_title = "PocketEngine Editor";
    float ui_scale = 1.0f;
    int reference_width = 1600;
    int reference_height = 960;
    int scene_view_width = 1600;
    int scene_view_height = 960;

    // External editor command map for built-in project file types.
    // Empty command means "use platform default behavior".
    std::string external_editor_audio;
    std::string external_editor_lua;
    std::string external_editor_config;
    std::string external_editor_template;
    std::string external_editor_image;
    std::string external_editor_font;
    std::string external_editor_scene;
    std::string external_editor_generic;

    // Engine/editor-level project history. The selected path is the logical
    // project root used by the editor; project-private state lives inside
    // that folder rather than under .engine.
    std::filesystem::path current_project_root =
        std::filesystem::path("Projects") / "Default";
    std::vector<std::filesystem::path> recent_project_roots;
};

class EditorConfig {
public:
    // Read optional editor.config overrides for the editor host window.
    static EditorConfigData Read();
    // Persist the current editor host settings back to editor.config.
    static void Write(const EditorConfigData &config);
    // Persist frequently updated project history to editor/projects.config.
    static void WriteProjectHistory(const EditorConfigData &config);
    // Return all built-in file types that support external editor mapping.
    static const std::array<EditorExternalFileType, 8> &
    ExternalEditorFileTypes();
    // Return a short display label for one external editor file type.
    static const char *ExternalEditorDisplayLabel(EditorExternalFileType type);
    // Return JSON key name used in editor.config for one file type.
    static const char *ExternalEditorConfigKey(EditorExternalFileType type);
    // Mutable / immutable accessors to command string for one file type.
    static std::string &ExternalEditorCommand(EditorConfigData &config,
                                              EditorExternalFileType type);
    static const std::string &
    ExternalEditorCommand(const EditorConfigData &config,
                          EditorExternalFileType type);
    static void RememberProject(EditorConfigData &config,
                                const std::filesystem::path &project_root);
};

#endif
