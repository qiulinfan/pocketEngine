#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "editor/core/EditorOverlay.h"
#include "editor/core/EditorConfig.h"
#include "editor/documents/SceneDocument.h"
#include "editor/panels/HierarchyPanel.h"
#include "editor/panels/InspectorPanel.h"
#include "editor/panels/ProjectPanel.h"
#include "editor/panels/StatusPanel.h"
#include "editor/panels/ViewportPanel.h"
#include "editor/panels/SceneViewPanel.h"
#include "engine/core/Engine.h"
#include "shared/resources/ResourcePath.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "SDL2/SDL.h"
#if defined(_WIN32)
#include <objbase.h>
#include <shobjidl.h>
#include <windows.h>
#endif
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr float kEditorFontPixelSize = 16.0f;

/*
These helpers decide whether an SDL event should stay in editor land 
or should be forwarded into runtime input processing.
(以下 helpers: 一个 SDL event 是否应该被转发到 runtime)
*/ 
bool ShouldCaptureMouseEvent(Uint32 event_type) {
    switch (event_type) {
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
        return true;
    default:
        return false;
    }
}
bool ShouldCaptureKeyboardEvent(Uint32 event_type) {
    switch (event_type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    case SDL_TEXTEDITING:
    case SDL_TEXTINPUT:
        return true;
    default:
        return false;
    }
}

bool IsEditorPlaybackHotkeyEvent(const SDL_Event &event) {
    if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) {
        return false;
    }
    const SDL_Scancode scancode = event.key.keysym.scancode;
    return scancode == SDL_SCANCODE_F5 || scancode == SDL_SCANCODE_F6;
}

bool IsPointInsideRect(int x, int y, float min_x, float min_y, float max_x,
                       float max_y) {
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    return fx >= min_x && fx <= max_x && fy >= min_y && fy <= max_y;
}

bool TryGetMousePositionFromEvent(const SDL_Event &event, int &x, int &y) {
    switch (event.type) {
    case SDL_MOUSEMOTION:
        x = event.motion.x;
        y = event.motion.y;
        return true;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        x = event.button.x;
        y = event.button.y;
        return true;
    case SDL_MOUSEWHEEL:
        SDL_GetMouseState(&x, &y);
        return true;
    default:
        return false;
    }
}

bool IsWindowFullscreen(SDL_Window *window) {
    if (window == nullptr) return false;
    const Uint32 window_flags = SDL_GetWindowFlags(window);
    return (window_flags & SDL_WINDOW_FULLSCREEN) != 0 ||
           (window_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

#if defined(__APPLE__)
std::string TrimTrailingLineBreaks(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}
#endif

std::string TrimWhitespace(std::string value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

bool IsValidProjectFolderName(const std::string &name, std::string &error) {
    if (name.empty()) {
        error = "Project name is empty.";
        return false;
    }
    if (name == "." || name == "..") {
        error = "Project name cannot be '.' or '..'.";
        return false;
    }
    if (name.back() == '.') {
        error = "Project name cannot end with a dot.";
        return false;
    }
    for (const char ch : name) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (uch < 32) {
            error = "Project name cannot contain control characters.";
            return false;
        }
        if (ch == '/' || ch == '\\') {
            error = "Project name cannot contain path separators.";
            return false;
        }
        if (ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
            ch == '|' || ch == '?' || ch == '*') {
            error = "Project name contains a character Windows cannot use.";
            return false;
        }
    }
    return true;
}

std::filesystem::path ProjectsRootPath() {
    return std::filesystem::path("Projects");
}

std::string GenerateDefaultProjectName() {
    const std::filesystem::path projects_root = ProjectsRootPath();
    for (int index = 1; index < 10000; ++index) {
        const std::string candidate = "NewProject(" + std::to_string(index) + ")";
        if (!std::filesystem::exists(projects_root / candidate)) {
            return candidate;
        }
    }
    return "NewProject";
}

std::filesystem::path DefaultNewProjectPath() {
    return (ProjectsRootPath() / GenerateDefaultProjectName())
        .lexically_normal();
}

std::filesystem::path RemoveTrailingPathSeparators(
    std::filesystem::path path) {
    path = path.lexically_normal();
    while (!path.empty() && path.filename().empty()) {
        const std::filesystem::path parent_path = path.parent_path();
        if (parent_path == path) break;
        path = parent_path.lexically_normal();
    }
    return path;
}

std::filesystem::path NormalizeNewProjectPathInput(
    const std::string &project_path) {
    const std::string trimmed_path = TrimWhitespace(project_path);
    if (trimmed_path.empty()) {
        return DefaultNewProjectPath();
    }
    return RemoveTrailingPathSeparators(
        std::filesystem::path(trimmed_path)).lexically_normal();
}

bool WriteTextFileIfMissing(const std::filesystem::path &path,
                            const std::string &contents) {
    if (std::filesystem::exists(path)) return true;
    std::ofstream output_file(path, std::ios::out | std::ios::trunc);
    if (!output_file.is_open()) return false;
    output_file << contents;
    return output_file.good();
}

bool CreateProjectSkeleton(const std::filesystem::path &project_root,
                           const std::string &project_name,
                           std::string &error) {
    std::error_code fs_error;
    if (std::filesystem::exists(project_root, fs_error)) {
        if (fs_error) {
            error = "Could not inspect project folder.";
            return false;
        }
        if (!std::filesystem::is_directory(project_root, fs_error) ||
            fs_error) {
            error = "Project path already exists and is not a folder.";
            return false;
        }
        if (!std::filesystem::is_empty(project_root, fs_error) ||
            fs_error) {
            error = "Project folder already exists and is not empty.";
            return false;
        }
    } else if (!std::filesystem::create_directories(project_root, fs_error) ||
               fs_error) {
        error = "Could not create project folder.";
        return false;
    }

    const char *subdirectories[] = {
        "actor_templates", "audio", "component_types", "fonts",
        "images", "scenes"
    };
    for (const char *subdirectory : subdirectories) {
        if (!std::filesystem::create_directories(project_root / subdirectory,
                                                 fs_error) ||
            fs_error) {
            error = "Could not create project subfolders.";
            return false;
        }
    }

    const std::string game_config =
        "{\n"
        "  \"game_title\": \"" + project_name + "\",\n"
        "  \"initial_scene\": \"main\"\n"
        "}\n";
    if (!WriteTextFileIfMissing(project_root / "game.config", game_config)) {
        error = "Could not write game.config.";
        return false;
    }

    const std::string rendering_config =
        "{\n"
        "  \"clear_color_r\": 255,\n"
        "  \"clear_color_g\": 255,\n"
        "  \"clear_color_b\": 255,\n"
        "  \"x_resolution\": 640,\n"
        "  \"y_resolution\": 360\n"
        "}\n";
    if (!WriteTextFileIfMissing(project_root / "rendering.config",
                                rendering_config)) {
        error = "Could not write rendering.config.";
        return false;
    }

    const std::string main_scene =
        "{\n"
        "  \"actors\": []\n"
        "}\n";
    if (!WriteTextFileIfMissing(project_root / "scenes" / "main.scene",
                                main_scene)) {
        error = "Could not write scenes/main.scene.";
        return false;
    }

    const std::string empty_template =
        "{\n"
        "  \"name\": \"empty\",\n"
        "  \"components\": {}\n"
        "}\n";
    if (!WriteTextFileIfMissing(project_root / "actor_templates" /
                                    "empty.template",
                                empty_template)) {
        error = "Could not write actor_templates/empty.template.";
        return false;
    }

    return true;
}

std::optional<std::filesystem::path> OpenNativeProjectFolderPicker() {
#if defined(_WIN32)
    HRESULT init_result =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                    COINIT_DISABLE_OLE1DDE);
    const bool should_uninitialize = SUCCEEDED(init_result);
    if (FAILED(init_result) && init_result != RPC_E_CHANGED_MODE) {
        return std::nullopt;
    }

    IFileOpenDialog *dialog = nullptr;
    HRESULT dialog_result = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(dialog_result) || dialog == nullptr) {
        if (should_uninitialize) CoUninitialize();
        return std::nullopt;
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                           FOS_PATHMUSTEXIST);
    }
    dialog->SetTitle(L"Choose PocketEngine Project Folder");

    std::optional<std::filesystem::path> selected_path;
    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
            PWSTR wide_path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,
                                               &wide_path)) &&
                wide_path != nullptr) {
                selected_path = std::filesystem::path(wide_path);
                CoTaskMemFree(wide_path);
            }
            item->Release();
        }
    }
    dialog->Release();
    if (should_uninitialize) CoUninitialize();
    return selected_path;
#elif defined(__APPLE__)
    FILE *pipe = popen(
        "osascript -e 'POSIX path of (choose folder with prompt "
        "\"Choose PocketEngine project folder\")'",
        "r");
    if (pipe == nullptr) return std::nullopt;

    std::string output;
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    const int close_result = pclose(pipe);
    output = TrimTrailingLineBreaks(output);
    if (close_result != 0 || output.empty()) return std::nullopt;
    return std::filesystem::path(output);
#else
    return std::nullopt;
#endif
}

// Top-level menu owns document-wide commands such as save. Panels below stay
// focused on selection and property editing.
void BuildMainMenuBar(bool &show_metrics_window,
                      bool &show_editor_settings_window,
                      bool &show_game_config_window,
                      bool &show_rendering_config_window,
                      bool &show_external_editors_window,
                      bool &show_new_project_window,
                      bool &show_open_project_window,
                      std::string &new_project_path,
                      std::string &new_project_error,
                      std::string &open_project_path,
                      const EditorConfigData &editor_config,
                      const SceneDocument &scene_document,
                      bool play_mode_active, bool play_mode_paused,
                      bool window_fullscreen,
                      bool scene_save_enabled,
                      EditorOverlayResult &result) {
#if defined(_WIN32) || defined(__APPLE__)
    (void)show_open_project_window;
    (void)open_project_path;
#endif
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Project...")) {
            new_project_path = DefaultNewProjectPath().string();
            new_project_error.clear();
            show_new_project_window = true;
        }
        if (ImGui::MenuItem("Open Project...")) {
            const std::optional<std::filesystem::path> selected_path =
                OpenNativeProjectFolderPicker();
            if (selected_path.has_value()) {
                result.open_project_requested = true;
                result.requested_project_root =
                    selected_path->lexically_normal();
            }
#if !defined(_WIN32) && !defined(__APPLE__)
            else {
                open_project_path = ResourcePath::ResourcesRootPath().string();
                show_open_project_window = true;
            }
#endif
        }
        if (!editor_config.recent_project_roots.empty() &&
            ImGui::BeginMenu("Recent Projects")) {
            for (const std::filesystem::path &project_root :
                 editor_config.recent_project_roots) {
                if (ImGui::MenuItem(project_root.string().c_str())) {
                    result.open_project_requested = true;
                    result.requested_project_root =
                        project_root.lexically_normal();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save Scene", "Ctrl+S", false,
                            scene_save_enabled)) {
            result.save_scene_requested = true;
        }
        ImGui::Separator();
        ImGui::TextDisabled("Project: %s",
                            ResourcePath::ResourcesRootPath()
                                .string()
                                .c_str());
        ImGui::TextDisabled("%s", scene_document.GetScenePath().string().c_str());
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Editor")) {
        ImGui::MenuItem("Editor Settings", nullptr,
                        &show_editor_settings_window);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Project")) {
        ImGui::MenuItem("Game Config", nullptr, &show_game_config_window);
        ImGui::MenuItem("Rendering Config", nullptr,
                        &show_rendering_config_window);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Runtime")) {
        if (!play_mode_active) {
            if (ImGui::MenuItem("Play", "F5", false, true)) {
                result.play_mode_start_requested = true;
            }
        } else {
            if (ImGui::MenuItem(play_mode_paused ? "Resume" : "Pause",
                                "F6", false, true)) {
                result.play_mode_pause_toggle_requested = true;
            }
            if (ImGui::MenuItem("Stop", "F5", false, true)) {
                result.play_mode_stop_requested = true;
            }
        }
        ImGui::EndMenu();
    }

    // imgui demo and metrics
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Fullscreen", "F11", window_fullscreen, true)) {
            result.toggle_fullscreen_requested = true;
        }
        ImGui::Separator();
        ImGui::MenuItem("ImGui Metrics", nullptr, &show_metrics_window);
        ImGui::EndMenu();
    }

    // tools: currrently only external editor path configuration for files (lua, etc)
    if (ImGui::BeginMenu("Tools")) {
        ImGui::MenuItem("External Editors", nullptr,
                        &show_external_editors_window);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

float ComputeEffectiveUiScale(const EditorConfigData &config, int window_width,
                              int window_height) {
                                  const float safe_reference_width = static_cast<float>(std::max(1, config.reference_width));
                                  const float safe_reference_height = static_cast<float>(std::max(1, config.reference_height));
                                  const float width_scale = static_cast<float>(std::max(1, window_width)) / safe_reference_width;
                                  const float height_scale = static_cast<float>(std::max(1, window_height)) / safe_reference_height;
    const float auto_scale = std::min(width_scale, height_scale);
    return std::max(0.5f, config.ui_scale * auto_scale);
}

bool IsBlankCommand(const std::string &command) {
    return std::all_of(command.begin(), command.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

// a unity like gray theme for the editor, AI-generated.
void ApplyUnityLikeGrayTheme(ImGuiStyle &style, float ui_scale) {
    // Start from Dear ImGui dark theme
    ImGui::StyleColorsDark(&style);

    style.ScaleAllSizes(ui_scale);
    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.ScrollbarRounding = 6.0f;

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.86f, 0.88f, 0.90f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.60f, 0.63f, 1.0f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.19f, 0.21f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.20f, 0.21f, 0.23f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.20f, 0.21f, 0.23f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.29f, 0.30f, 0.33f, 1.0f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.25f, 0.26f, 0.28f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.31f, 0.33f, 0.35f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.37f, 0.40f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.16f, 0.17f, 0.19f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.22f, 0.23f, 0.25f, 1.0f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.16f, 0.17f, 0.19f, 1.0f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.15f, 0.16f, 0.18f, 1.0f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.33f, 0.35f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.38f, 0.40f, 0.43f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.43f, 0.45f, 0.48f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.62f, 0.70f, 0.84f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.55f, 0.61f, 0.70f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.65f, 0.72f, 0.82f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.29f, 0.30f, 0.33f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.37f, 0.40f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.41f, 0.43f, 0.46f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.29f, 0.31f, 0.33f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.37f, 0.40f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.42f, 0.45f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.31f, 0.32f, 0.35f, 1.0f);
    colors[ImGuiCol_Tab] = ImVec4(0.23f, 0.24f, 0.26f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.33f, 0.35f, 0.38f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.30f, 0.32f, 0.35f, 1.0f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.21f, 0.22f, 0.24f, 1.0f);
}

// system font: source is inter.ttf
void LoadEditorDefaultFont(ImGuiIO &io) {
    io.Fonts->Clear();
    std::string font_path = ResourcePath::ResolveResourcePath(
        ResourcePath::EngineSystemFontsRoot(), "system", {".ttf"});
    if (font_path.empty()) {
        // Backward compatibility for projects that have not moved local editor
        // assets out of resources yet.
        font_path = ResourcePath::ResolveResourcePath(
            ResourcePath::ResourceSubdirectory("fonts"), "system", {".ttf"});
    }
    if (!font_path.empty()) {
        if (ImFont *font = io.Fonts->AddFontFromFileTTF(font_path.c_str(), kEditorFontPixelSize)) {
            io.FontDefault = font;
            return;
        }
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "EditorOverlay: editor system font not found, fallback to Dear ImGui default font.");
    }

    io.FontDefault = io.Fonts->AddFontDefault();
}

/*
helper: ImGui doesn't have a built-in InputText variant that works with std::string, 
so we gotta  provide that functionality.
std::string <-> ImGui char buffer
*/
bool InputTextString(const char *label, std::string &value, const char *hint) {
    std::vector<char> buffer(std::max<std::size_t>(1024, value.size() + 256), '\0');
    std::memcpy(buffer.data(), value.c_str(), value.size());

    bool changed = false;
    if (hint != nullptr && hint[0] != '\0') {
        changed = ImGui::InputTextWithHint(label, hint, buffer.data(),
                                           buffer.size());
    } else {
        changed = ImGui::InputText(label, buffer.data(), buffer.size());
    }
    if (!changed) {
        return false;
    }

    value = buffer.data();
    return true;
}

const char *ExternalEditorCommandHint(EditorExternalFileType type) {
    if (type == EditorExternalFileType::Lua) return "e.g. code {file} (supports {file}, {resources})";
    return "Default appends clicked file path. Supports {file}, {resources}";
}

std::vector<std::string> CollectAvailableSceneNames() {
    const std::filesystem::path scene_root =
        ResourcePath::ResourceSubdirectory("scenes");
    std::vector<std::string> scene_names;
    if (!std::filesystem::exists(scene_root) ||
        !std::filesystem::is_directory(scene_root)) {
        return scene_names;
    }

    const std::vector<std::filesystem::path> scene_files =
        ResourcePath::CollectFilesRecursively(scene_root, ".scene");
    scene_names.reserve(scene_files.size());
    for (const std::filesystem::path &scene_file : scene_files) {
        std::error_code relative_error;
        const std::filesystem::path relative_path =
            std::filesystem::relative(scene_file, scene_root, relative_error);
        if (relative_error || relative_path.empty()) continue;

        std::filesystem::path scene_name = relative_path;
        scene_name.replace_extension();
        scene_names.emplace_back(scene_name.generic_string());
    }

    std::sort(scene_names.begin(), scene_names.end());
    return scene_names;
}

void RenderExternalEditorsWindow(EditorConfigData &editor_config,
                                 bool &show_window,
                                 EditorOverlayResult &result) {
    if (!show_window) return;

    if (!ImGui::Begin("External Editors", &show_window)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Double-click non-.scene files in Project to open external editor.");
    ImGui::TextUnformatted("By default the clicked file path is appended as command argument.");
    ImGui::TextDisabled("Placeholders: {file} and {resources}");
    ImGui::TextDisabled("Leave command empty to disable external open for that file type.");
    ImGui::Separator();

    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersV | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("external_editors_table", 3, table_flags)) {
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Config Key", ImGuiTableColumnFlags_WidthFixed,
                                120.0f);
        ImGui::TableSetupColumn("Command");
        ImGui::TableHeadersRow();

        for (const EditorExternalFileType type :
             EditorConfig::ExternalEditorFileTypes()) {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(EditorConfig::ExternalEditorDisplayLabel(type));

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s",
                                EditorConfig::ExternalEditorConfigKey(type));

            ImGui::TableNextColumn();
            std::string &command = EditorConfig::ExternalEditorCommand(editor_config, type);
            const std::string input_id = std::string("##external_editor_cmd_") +
                                         EditorConfig::ExternalEditorConfigKey(type);
            if (InputTextString(input_id.c_str(), command,
                                ExternalEditorCommandHint(type))) {
                result.editor_config_changed = true;
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace

EditorOverlay::~EditorOverlay() {
    Shutdown();
}

void EditorOverlay::EnsureProjectConfigLoaded() {
    if (project_config_loaded_) return;
    project_config_cache_ = GameConfig::Read();
    project_config_loaded_ = true;
}

void EditorOverlay::ShowTransientNotice(const std::string &text,
                                        double duration_seconds) {
    transient_notice_text_ = text;
    show_transient_notice_ = true;
    transient_notice_expire_time_ = ImGui::GetTime() + duration_seconds;
}

void EditorOverlay::ShowNotice(const std::string &text,
                               double duration_seconds) {
    ShowTransientNotice(text, duration_seconds);
}

// Create the ImGui context and bind it to the shared SDL window and renderer3
bool EditorOverlay::Initialize(SDL_Window *window, SDL_Renderer *renderer) {
    if (initialized_) return true;
    if (window == nullptr || renderer == nullptr) return false;

    IMGUI_CHECKVERSION();
    // create the global context for imgui
    ImGui::CreateContext();

    // config IO style: enable docking and set up font/theme.
    // Keyboard navigation fights gameplay controls in play mode: arrow keys
    // should remain runtime input instead of moving focus across toolbar buttons.
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "imgui.ini";
    LoadEditorDefaultFont(io);
    ImGuiStyle &style = ImGui::GetStyle();
    ApplyUnityLikeGrayTheme(style, 1.0f);

    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplSDLRenderer2_Init(renderer)) {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    initialized_ = true;
    return true;
}

// Apply cached editor settings to the live ImGui style and font scale.
void EditorOverlay::ApplyConfig(const EditorConfigData &config, int window_width,
                                int window_height) {
    if (!initialized_) return;

    const float effective_ui_scale = ComputeEffectiveUiScale(config, window_width, window_height);
    if (std::fabs(effective_ui_scale - applied_ui_scale_) < 0.001f) {
        return;
    }

    ImGuiStyle &style = ImGui::GetStyle();
    ApplyUnityLikeGrayTheme(style, effective_ui_scale);

    style.SeparatorSize = std::max(style.SeparatorSize, 1.0f);
    style.SeparatorTextBorderSize = std::max(style.SeparatorTextBorderSize, 1.0f);
    style.DockingSeparatorSize = std::max(style.DockingSeparatorSize, 1.0f);
    // Dear ImGui sanity checks require this value to remain strictly positive
    style.WindowBorderHoverPadding = std::max(style.WindowBorderHoverPadding, 1.0f);
    // Since Dear ImGui 1.92, global font scaling lives on style.FontScaleMain
    style.FontScaleMain = effective_ui_scale;
    ImGuiIO &io = ImGui::GetIO();
    io.FontGlobalScale = 1.0f;
    applied_ui_scale_ = effective_ui_scale;
}

// Update routing context used to decide whether input goes to editor or
// runtime while the viewport is embedded.
void EditorOverlay::SetRuntimeInputRoutingState(bool play_mode_active,
                                                bool play_mode_paused) {
    const bool entering_play = !play_mode_active_for_input_ && play_mode_active;
    const bool resuming_from_pause =
        play_mode_active_for_input_ && play_mode_paused_for_input_ &&
        play_mode_active && !play_mode_paused;
    play_mode_active_for_input_ = play_mode_active;
    play_mode_paused_for_input_ = play_mode_paused;
    if (entering_play || resuming_from_pause) {
        runtime_input_focus_ = true;
        return;
    }
    if (!play_mode_active_for_input_) {
        runtime_input_focus_ = false;
    }
}

void EditorOverlay::NotifyProjectChanged() {
    project_config_loaded_ = false;
    selected_actor_index_ = -1;
    selected_runtime_actor_uid_ = Actor::kInvalidUID;
    play_mode_active_for_input_ = false;
    play_mode_paused_for_input_ = false;
    runtime_input_focus_ = false;
    viewport_runtime_image_valid_ = false;
    viewport_transport_bar_valid_ = false;
    applied_ui_scale_ = 0.0f;
    show_open_project_window_ = false;
    open_project_error_.clear();
}

// Forward one SDL event to ImGui and report whether the editor/runtime capture it
/*
Editor not initialized -> runtime
EditorPlaybackHotkey -> editor
playmode + runtime focus -> runtime

then: dependes on whether Imgui wants to capture it.
*/
bool EditorOverlay::ProcessEvent(const SDL_Event &event) {
    if (!initialized_) return false;
    /*
    In play mode, runtime keyboard input wins before ImGui sees the event.
    Otherwise ImGui keyboard navigation can move focus across Play/Pause/Stop
    while the user is using arrow keys or WASD for gameplay.
    */
    if (play_mode_active_for_input_ && ShouldCaptureKeyboardEvent(event.type) &&
        !IsEditorPlaybackHotkeyEvent(event)) {
        return false;
    }

    // give SDL input event to ImGui for processing
    ImGui_ImplSDL2_ProcessEvent(&event);
    const ImGuiIO &io = ImGui::GetIO();
    if (IsEditorPlaybackHotkeyEvent(event)) return true;
    if (!play_mode_active_for_input_) runtime_input_focus_ = false;
    /*
    Gameplay keyboard should feel immediate after pressing Play.  ImGui can
    still capture text-entry fields, but normal key events go to runtime while
    play mode is active so WASD/arrow controls are not eaten by docked panels.
    */
    if (play_mode_active_for_input_ && ShouldCaptureKeyboardEvent(event.type) &&
        !io.WantTextInput) {
        return false;
    }

    // check if the the event's position is inside the runtime viewport
    int mouse_x = 0;
    int mouse_y = 0;
    const bool has_mouse_pos = TryGetMousePositionFromEvent(event, mouse_x, mouse_y);
    const bool mouse_in_runtime_view =
        has_mouse_pos && viewport_runtime_image_valid_ &&
        IsPointInsideRect(mouse_x, mouse_y, viewport_runtime_image_min_x_,
                          viewport_runtime_image_min_y_,
                          viewport_runtime_image_max_x_,
                          viewport_runtime_image_max_y_);
    const bool mouse_in_transport_bar =
        has_mouse_pos && viewport_transport_bar_valid_ &&
        IsPointInsideRect(mouse_x, mouse_y, viewport_transport_bar_min_x_,
                          viewport_transport_bar_min_y_,
                          viewport_transport_bar_max_x_,
                          viewport_transport_bar_max_y_);

    /*
    Clicking the runtime viewport grants runtime focus for subsequent input.
    Clicking outside returns focus to editor.
    */
    if (event.type == SDL_MOUSEBUTTONDOWN) {
        runtime_input_focus_ = play_mode_active_for_input_ && mouse_in_runtime_view && !mouse_in_transport_bar;
    }

    /*
    While runtime has input focus in play mode,
    route keyboard and viewport mouse input to runtime even if ImGui requests capture.
    */
    if (play_mode_active_for_input_ && runtime_input_focus_) {
        if (ShouldCaptureKeyboardEvent(event.type)) {
            return false;
        }
        if (ShouldCaptureMouseEvent(event.type) && mouse_in_runtime_view &&
            !mouse_in_transport_bar) {
            return false;
        }
    }

    /*
    Mirror Imgui's capture flags so the editor can consume UI input
    without blocking essential runtime window events.
    */
    if (ShouldCaptureMouseEvent(event.type)) {
        return io.WantCaptureMouse;
    }
    if (ShouldCaptureKeyboardEvent(event.type)) {
        return io.WantCaptureKeyboard;
    }
    return false;
}

// Draw the editor UI for the current scene cache and runtime frame
EditorOverlayResult EditorOverlay::Render(Engine &engine,
                                          SceneDocument &scene_document,
                                          EditorConfigData &editor_config,
                                          bool editor_config_confirmation_pending,
                                          bool play_mode_active,
                                          bool play_mode_paused,
                                          bool scene_editing_enabled,
                                          bool scene_save_enabled) {
    EditorOverlayResult result;
    if (!initialized_) return result;

    // get the SDL_Renderer 
    SDL_Renderer *renderer = engine.GetRenderer();
    if (renderer == nullptr) return result;

    // standard new frame setup for imgui with SDL_Renderer backend
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Global save should stay out of the way while the user is typing into
    // rename fields, editor settings, or other focused widgets.
    const ImGuiIO &io = ImGui::GetIO();
    const bool allow_global_save_shortcut = !io.WantTextInput && !ImGui::IsAnyItemActive();

    // Ctrl+S: shortcut key for save scene
    if (scene_save_enabled && allow_global_save_shortcut &&
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S,
                        ImGuiInputFlags_RouteGlobal)) {
        result.save_scene_requested = true;
    }
    // F5: shortcut key for play/stop
    if (ImGui::Shortcut(ImGuiKey_F5, ImGuiInputFlags_RouteGlobal)) {
        if (play_mode_active) {
            result.play_mode_stop_requested = true;
        } else {
            result.play_mode_start_requested = true;
        }
    }
    // F6: shortcut key for pause/resume
    if (play_mode_active &&
        ImGui::Shortcut(ImGuiKey_F6, ImGuiInputFlags_RouteGlobal)) {
        result.play_mode_pause_toggle_requested = true;
    }
    if (ImGui::Shortcut(ImGuiKey_F11, ImGuiInputFlags_RouteGlobal)) {
        result.toggle_fullscreen_requested = true;
    }

    /*
    build main menu and the dock space, then render panels:
    status, project, viewport, scene, hierarchy, inspector
    */
    const bool window_fullscreen = IsWindowFullscreen(engine.GetWindow());
    BuildMainMenuBar(show_metrics_window_,
                     show_editor_settings_window_, show_game_config_window_,
                     show_rendering_config_window_,
                     show_external_editors_window_, show_new_project_window_,
                     show_open_project_window_, new_project_path_,
                     new_project_error_, open_project_path_, editor_config,
                     scene_document, play_mode_active, play_mode_paused,
                     window_fullscreen, scene_save_enabled, result);

    ImGuiViewport *main_viewport = ImGui::GetMainViewport();
    ImGui::DockSpaceOverViewport(0, main_viewport, ImGuiDockNodeFlags_None);

    EditorPanels::RenderStatusPanel(engine, scene_document,
                                    selected_actor_index_,
                                    selected_runtime_actor_uid_,
                                    play_mode_active, play_mode_paused,
                                    applied_ui_scale_);

    const EditorPanels::ProjectPanelResult project_panel_result =
        EditorPanels::RenderProjectPanel(ResourcePath::ResourcesRootPath(),
                                         renderer);
    if (project_panel_result.open_scene_requested) {
        result.open_scene_requested = true;
        result.requested_scene_path = project_panel_result.requested_scene_path;
        selected_actor_index_ = -1;
        selected_runtime_actor_uid_ = Actor::kInvalidUID;
    }
    if (project_panel_result.open_external_editor_requested) {
        const EditorExternalFileType file_type = project_panel_result.requested_external_file_type;
        const std::string &configured_command =
            EditorConfig::ExternalEditorCommand(editor_config, file_type);
        // Missing command should never block the editor loop. Surface a short
        // notice and let users jump directly to Tools -> External Editors.
        if (IsBlankCommand(configured_command)) {
            ShowExternalEditorMissingConfigNotice(file_type);
        } else {
            result.open_external_editor_requested = true;
            result.requested_external_file_type = file_type;
            result.requested_external_file_path = project_panel_result.requested_external_file_path;
            result.requested_external_project_root = project_panel_result.requested_external_project_root;
        }
    }
    if (!play_mode_active) {
        selected_runtime_actor_uid_ = Actor::kInvalidUID;
    } else if (selected_actor_index_ >= 0 &&
               selected_actor_index_ <
                   static_cast<int>(scene_document.GetActorCount())) {
        const SceneDocument::ActorUID selected_actor_uid = scene_document.GetActorUID( static_cast<std::size_t>(selected_actor_index_));
        const Actor *selected_runtime_actor =
            engine.GetRuntimeActorByUID(selected_runtime_actor_uid_);
        if (selected_runtime_actor == nullptr ||
            selected_runtime_actor->uid != selected_actor_uid) {
                const Actor *runtime_actor =
                    engine.GetRuntimeActorByUID(selected_actor_uid);
                selected_runtime_actor_uid_ =
                    (runtime_actor != nullptr) ? runtime_actor->uid
                                               : Actor::kInvalidUID;
        }
    }

    result.scene_changed |= EditorPanels::RenderHierarchyPanel(
        engine, scene_document, selected_actor_index_,
        selected_runtime_actor_uid_,
        play_mode_active, scene_editing_enabled, &result.scene_edit_commands);
    result.scene_changed |= EditorPanels::RenderInspectorPanel(
        engine, scene_document, selected_actor_index_,
        selected_runtime_actor_uid_,
        play_mode_active, scene_editing_enabled, &result.scene_edit_commands);

    const EditorPanels::SceneViewPanelResult scene_view_panel_result =
        EditorPanels::RenderSceneViewPanel(
            engine, scene_document, selected_actor_index_,
            selected_runtime_actor_uid_, editor_config.scene_view_width,
            editor_config.scene_view_height, play_mode_active,
            play_mode_paused, scene_editing_enabled,
            &result.scene_edit_commands);
    result.scene_changed |= scene_view_panel_result.scene_changed;
    result.scene_edit_commands_runtime_synced_begin_index =
        scene_view_panel_result.immediate_apply_begin_index;
    result.scene_edit_commands_runtime_synced_end_index =
        scene_view_panel_result.immediate_apply_end_index;

    const EditorPanels::ViewportControlsResult viewport_controls =
        EditorPanels::RenderViewportPanel(engine, play_mode_active,
                                          play_mode_paused);
    result.play_mode_start_requested |= viewport_controls.play_mode_start_requested;
    result.play_mode_pause_toggle_requested |= viewport_controls.play_mode_pause_toggle_requested;
    result.play_mode_stop_requested |= viewport_controls.play_mode_stop_requested;

    // Runtime input routing should continue to follow the game viewport,
    // not the editable scene view.
    viewport_runtime_image_valid_ = viewport_controls.has_runtime_image;
    viewport_runtime_image_min_x_ = viewport_controls.runtime_image_min_x;
    viewport_runtime_image_min_y_ = viewport_controls.runtime_image_min_y;
    viewport_runtime_image_max_x_ = viewport_controls.runtime_image_max_x;
    viewport_runtime_image_max_y_ = viewport_controls.runtime_image_max_y;
    viewport_transport_bar_valid_ = viewport_controls.has_transport_bar;
    viewport_transport_bar_min_x_ = viewport_controls.transport_bar_min_x;
    viewport_transport_bar_min_y_ = viewport_controls.transport_bar_min_y;
    viewport_transport_bar_max_x_ = viewport_controls.transport_bar_max_x;
    viewport_transport_bar_max_y_ = viewport_controls.transport_bar_max_y;

    if (show_metrics_window_) ImGui::ShowMetricsWindow(&show_metrics_window_);

    RenderEditorSettingsWindow(engine, editor_config, result);
    RenderProjectConfigWindows();
    RenderNewProjectWindow(result);
    RenderOpenProjectWindow(result);
    RenderEditorConfigConfirmationWindow(editor_config_confirmation_pending,
                                         result);
    RenderExternalEditorsWindow(editor_config, show_external_editors_window_,
                                result);
    RenderTransientNotifications();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    return result;
}

// Queue a short-lived popup when a file type has no external tool command.
void EditorOverlay::ShowExternalEditorMissingConfigNotice(EditorExternalFileType type) {
    ShowTransientNotice(std::string("No external tool configured for ") +
                            EditorConfig::ExternalEditorDisplayLabel(type) +
                            ". Open Tools -> External Editors to configure one.",
                        4.0);
}

void EditorOverlay::RenderEditorSettingsWindow(Engine &engine,
                                               EditorConfigData &editor_config,
                                               EditorOverlayResult &result) {
    if (!show_editor_settings_window_) return;

    if (!ImGui::Begin("Editor Settings", &show_editor_settings_window_)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted( "These values control the editor window and overlay only.");
    ImGui::TextDisabled(
        "Window moves are auto-accepted. Fullscreen/layout changes still need Apply/Revert.");
    ImGui::Separator();

    bool config_changed = false;
    bool reapply_window_settings = false;

    int window_width = editor_config.window_width;
    int window_height = editor_config.window_height;
    int reference_width = editor_config.reference_width;
    int reference_height = editor_config.reference_height;
    int scene_view_width = editor_config.scene_view_width;
    int scene_view_height = editor_config.scene_view_height;
    float ui_scale = editor_config.ui_scale;
    bool window_fullscreen = editor_config.window_fullscreen;
    bool window_maximized = editor_config.window_maximized;

    if (ImGui::InputInt("Editor Width", &window_width)) {
        editor_config.window_width = std::max(640, window_width);
        config_changed = true;
        reapply_window_settings = true;
    }
    if (ImGui::InputInt("Editor Height", &window_height)) {
        editor_config.window_height = std::max(360, window_height);
        config_changed = true;
        reapply_window_settings = true;
    }
    if (ImGui::Checkbox("Fullscreen", &window_fullscreen)) {
        editor_config.window_fullscreen = window_fullscreen;
        if (window_fullscreen) {
            editor_config.window_maximized = false;
        }
        config_changed = true;
        reapply_window_settings = true;
    }
    if (ImGui::Checkbox("Maximized", &window_maximized)) {
        editor_config.window_maximized = window_maximized;
        if (window_maximized) {
            editor_config.window_fullscreen = false;
        }
        config_changed = true;
        reapply_window_settings = true;
    }
    if (ImGui::InputFloat("UI Scale", &ui_scale, 0.05f, 0.25f, "%.2f")) {
        editor_config.ui_scale = std::max(0.5f, ui_scale);
        config_changed = true;
    }
    if (ImGui::InputInt("Reference Width", &reference_width)) {
        editor_config.reference_width = std::max(1, reference_width);
        config_changed = true;
    }
    if (ImGui::InputInt("Reference Height", &reference_height)) {
        editor_config.reference_height = std::max(1, reference_height);
        config_changed = true;
    }
    if (ImGui::InputInt("Scene View Width", &scene_view_width)) {
        editor_config.scene_view_width = std::max(64, scene_view_width);
        config_changed = true;
    }
    if (ImGui::InputInt("Scene View Height", &scene_view_height)) {
        editor_config.scene_view_height = std::max(64, scene_view_height);
        config_changed = true;
    }

    ImGui::Separator();
    ImGui::TextDisabled("Current window position: %d, %d",
                        editor_config.window_x, editor_config.window_y);

    if (config_changed) {
        ApplyConfig(editor_config, engine.GetWindowWidth(),
                    engine.GetWindowHeight());
        result.editor_config_changed = true;
    }
    if (reapply_window_settings) {
        result.editor_window_settings_changed = true;
    }

    ImGui::End();
}

void EditorOverlay::RenderProjectConfigWindows() {
    EnsureProjectConfigLoaded();

    auto persist_project_config = [this]() {
        if (!GameConfig::Write(project_config_cache_)) {
            ShowTransientNotice(
                "Unable to write project config. Check the current project's game.config and rendering.config.",
                5.0);
        }
    };

    if (show_game_config_window_) {
        if (ImGui::Begin("Game Config", &show_game_config_window_)) {
            ImGui::Text("These values write to %s immediately.",
                        (ResourcePath::ResourcesRootPath() / "game.config")
                            .string()
                            .c_str());
            ImGui::TextDisabled( "Current runtime/editor session keeps its already-loaded config.");
            if (ImGui::Button("Reload from Disk")) {
                project_config_cache_ = GameConfig::Read();
            }
            ImGui::Separator();

            bool config_changed = false;
            config_changed |= InputTextString(
                "Game Title", project_config_cache_.game_title,
                "Shown by the standalone runtime window");

                const std::vector<std::string> scene_names = CollectAvailableSceneNames();
            const char *preview_text =
                project_config_cache_.initial_scene_name.empty()
                    ? "<none>"
                    : project_config_cache_.initial_scene_name.c_str();
            if (ImGui::BeginCombo("Initial Scene", preview_text)) {
                for (const std::string &scene_name : scene_names) {
                    const bool selected = project_config_cache_.initial_scene_name == scene_name;
                    if (ImGui::Selectable(scene_name.c_str(), selected)) {
                        project_config_cache_.initial_scene_name = scene_name;
                        config_changed = true;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            config_changed |= InputTextString(
                "Initial Scene Path", project_config_cache_.initial_scene_name,
                "e.g. ballgame/hw8_platformer_ballgame");

            if (config_changed) {
                persist_project_config();
            }
        }
        ImGui::End();
    }

    if (show_rendering_config_window_) {
        if (ImGui::Begin("Rendering Config", &show_rendering_config_window_)) {
            ImGui::Text("These values write to %s immediately.",
                        (ResourcePath::ResourcesRootPath() /
                         "rendering.config")
                            .string()
                            .c_str());
            ImGui::TextDisabled( "Current runtime/editor session keeps its already-loaded config.");
            if (ImGui::Button("Reload from Disk")) {
                project_config_cache_ = GameConfig::Read();
            }
            ImGui::Separator();

            bool config_changed = false;
            int width = project_config_cache_.window_width;
            int height = project_config_cache_.window_height;
            float zoom_factor = project_config_cache_.zoom_factor;
            float clear_color[3] = {
                project_config_cache_.clear_color_r / 255.0f,
                project_config_cache_.clear_color_g / 255.0f,
                project_config_cache_.clear_color_b / 255.0f};

            if (ImGui::InputInt("Runtime Width", &width)) {
                project_config_cache_.window_width = std::max(1, width);
                config_changed = true;
            }
            if (ImGui::InputInt("Runtime Height", &height)) {
                project_config_cache_.window_height = std::max(1, height);
                config_changed = true;
            }
            if (ImGui::InputFloat("Zoom Factor", &zoom_factor, 0.1f, 0.5f,
                                  "%.2f")) {
                                      project_config_cache_.zoom_factor = std::max(0.05f, zoom_factor);
                config_changed = true;
            }
            if (ImGui::ColorEdit3("Clear Color", clear_color,
                                  ImGuiColorEditFlags_DisplayRGB |
                                      ImGuiColorEditFlags_Float)) {
                project_config_cache_.clear_color_r = static_cast<int>(
                    std::round(std::max(0.0f, std::min(1.0f, clear_color[0])) *
                               255.0f));
                project_config_cache_.clear_color_g = static_cast<int>(
                    std::round(std::max(0.0f, std::min(1.0f, clear_color[1])) *
                               255.0f));
                project_config_cache_.clear_color_b = static_cast<int>(
                    std::round(std::max(0.0f, std::min(1.0f, clear_color[2])) *
                               255.0f));
                config_changed = true;
            }
            ImGui::TextDisabled("RGB: %d, %d, %d",
                                project_config_cache_.clear_color_r,
                                project_config_cache_.clear_color_g,
                                project_config_cache_.clear_color_b);

            if (config_changed) {
                persist_project_config();
            }
        }
        ImGui::End();
    }
}

void EditorOverlay::RenderNewProjectWindow(EditorOverlayResult &result) {
    if (!show_new_project_window_) return;

    ImGui::SetNextWindowSize(ImVec2(640.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin("New Project", &show_new_project_window_)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Create a new project folder.");
    ImGui::TextDisabled(
        "Default is Projects/NewProject(n), but any absolute or relative path works.");
    ImGui::Separator();

    if (new_project_path_.empty()) {
        new_project_path_ = DefaultNewProjectPath().string();
    }

    InputTextString("Project Path", new_project_path_,
                    "e.g. Projects/NewProject(1), ../MyGame, C:\\\\Games\\\\MyProject");
#if defined(_WIN32) || defined(__APPLE__)
    if (ImGui::Button("Choose Parent Folder...", ImVec2(190.0f, 0.0f))) {
        const std::optional<std::filesystem::path> selected_parent =
            OpenNativeProjectFolderPicker();
        if (selected_parent.has_value()) {
            const std::filesystem::path current_path =
                NormalizeNewProjectPathInput(new_project_path_);
            std::string project_folder_name = current_path.filename().string();
            if (project_folder_name.empty()) {
                project_folder_name = GenerateDefaultProjectName();
            }
            new_project_path_ =
                (selected_parent.value() / project_folder_name)
                    .lexically_normal()
                    .string();
            new_project_error_.clear();
        }
    }
    ImGui::SameLine();
#endif
    if (ImGui::Button("Reset Default", ImVec2(120.0f, 0.0f))) {
        new_project_path_ = DefaultNewProjectPath().string();
        new_project_error_.clear();
    }

    const std::filesystem::path preview_path =
        NormalizeNewProjectPathInput(new_project_path_);
    ImGui::TextDisabled("Will create/fill: %s",
                        preview_path.lexically_normal().string().c_str());
    ImGui::TextDisabled(
        "Target may be missing or an existing empty folder.");

    if (!new_project_error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s",
                           new_project_error_.c_str());
    }

    if (ImGui::Button("Create", ImVec2(120.0f, 0.0f))) {
        const std::filesystem::path project_root =
            NormalizeNewProjectPathInput(new_project_path_);
        const std::string project_name = project_root.filename().string();
        std::string validation_error;
        if (project_root.empty() || project_root == ".") {
            new_project_error_ = "Project path is empty.";
        } else if (!IsValidProjectFolderName(project_name, validation_error)) {
            new_project_error_ = validation_error;
        } else {
            std::string creation_error;
            if (!CreateProjectSkeleton(project_root, project_name,
                                       creation_error)) {
                new_project_error_ = creation_error;
            } else {
                result.open_project_requested = true;
                result.requested_project_root = project_root;
                new_project_path_.clear();
                new_project_error_.clear();
                show_new_project_window_ = false;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
        show_new_project_window_ = false;
        new_project_path_.clear();
        new_project_error_.clear();
    }

    ImGui::End();
}

void EditorOverlay::RenderOpenProjectWindow(EditorOverlayResult &result) {
    if (!show_open_project_window_) return;

    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin("Open Project", &show_open_project_window_)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(
        "Choose the folder that should behave as this project's root.");
    ImGui::TextDisabled(
        "Absolute paths and paths relative to the engine working directory both work.");
    ImGui::Separator();

    InputTextString("Project Folder", open_project_path_,
                    "e.g. Projects/Default, Projects/MyGame, ../my-game, C:\\\\Games\\\\MyProject");
    if (!open_project_error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s",
                           open_project_error_.c_str());
    }

    if (ImGui::Button("Open", ImVec2(120.0f, 0.0f))) {
        const std::filesystem::path candidate =
            ResourcePath::NormalizeProjectRoot(open_project_path_);
        if (candidate.empty()) {
            open_project_error_ = "Path is empty.";
        } else if (!std::filesystem::exists(candidate) ||
                   !std::filesystem::is_directory(candidate)) {
            open_project_error_ = "Folder does not exist.";
        } else {
            result.open_project_requested = true;
            result.requested_project_root = candidate;
            open_project_error_.clear();
            show_open_project_window_ = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
        show_open_project_window_ = false;
        open_project_error_.clear();
    }

    ImGui::End();
}

void EditorOverlay::RenderEditorConfigConfirmationWindow( bool editor_config_confirmation_pending, EditorOverlayResult &result) {
    if (!editor_config_confirmation_pending) return;

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) return;

    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
               viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.97f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("Confirm Overlay Change", nullptr, flags)) {
        ImGui::TextUnformatted( "Editor overlay/window changes are pending confirmation.");
        ImGui::TextDisabled( "You can keep editing. Apply will persist the latest layout/settings.");
        ImGui::Spacing();
        if (ImGui::Button("Apply Overlay", ImVec2(150.0f, 0.0f))) {
            result.editor_config_apply_requested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert Overlay", ImVec2(150.0f, 0.0f))) {
            result.editor_config_revert_requested = true;
        }
    }
    ImGui::End();
}

// Render non-blocking top-right notifications for editor actions.
void EditorOverlay::RenderTransientNotifications() {
    if (!show_transient_notice_) return;

    if (ImGui::GetTime() >= transient_notice_expire_time_) {
        show_transient_notice_ = false;
        return;
    }

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) return;

    const float margin = 16.0f;
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin,
               viewport->WorkPos.y + margin),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.95f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##external_editor_notice", nullptr, flags)) {
        ImGui::TextWrapped("%s", transient_notice_text_.c_str());
        if (ImGui::SmallButton("Configure")) {
            show_external_editors_window_ = true;
            show_transient_notice_ = false;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss")) {
            show_transient_notice_ = false;
        }
    }
    ImGui::End();
}

// Release ImGui backend resources and destroy the ImGui context.
void EditorOverlay::Shutdown() {
    if (!initialized_) return;

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
    NotifyProjectChanged();
}
