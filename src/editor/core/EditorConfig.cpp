#include "editor/core/EditorConfig.h"
#include "shared/resources/ResourcePath.h"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

namespace {

const std::filesystem::path kEditorConfigPath = ResourcePath::EditorConfigPath();
const std::filesystem::path kEditorProjectsConfigPath =
    ResourcePath::EditorProjectsConfigPath();
const std::filesystem::path kLegacyEditorConfigPath = "resources/editor.config";

void ReadJsonFile(const std::string &path, rapidjson::Document &out_document) {
    FILE *file_pointer = nullptr;
#ifdef _WIN32
    fopen_s(&file_pointer, path.c_str(), "rb");
#else
    file_pointer = fopen(path.c_str(), "rb");
#endif

    if (file_pointer == nullptr) {
        std::cout << "error: unable to open [" << path << "]" << std::endl;
        exit(0);
    }

    char buffer[65536];
    rapidjson::FileReadStream stream(file_pointer, buffer, sizeof(buffer));
    out_document.ParseStream(stream);
    std::fclose(file_pointer);

    if (out_document.HasParseError()) {
        std::cout << "error parsing json at [" << path << "]" << std::endl;
        exit(0);
    }
}

const std::array<EditorExternalFileType, 8> kExternalEditorFileTypes = {
    EditorExternalFileType::Audio,    EditorExternalFileType::Lua,
    EditorExternalFileType::Config,   EditorExternalFileType::Template,
    EditorExternalFileType::Image,    EditorExternalFileType::Font,
    EditorExternalFileType::Scene,    EditorExternalFileType::Generic};

std::string *GetExternalEditorCommandPtr(EditorConfigData &config,
                                         EditorExternalFileType type) {
    switch (type) {
    case EditorExternalFileType::Audio:
        return &config.external_editor_audio;
    case EditorExternalFileType::Lua:
        return &config.external_editor_lua;
    case EditorExternalFileType::Config:
        return &config.external_editor_config;
    case EditorExternalFileType::Template:
        return &config.external_editor_template;
    case EditorExternalFileType::Image:
        return &config.external_editor_image;
    case EditorExternalFileType::Font:
        return &config.external_editor_font;
    case EditorExternalFileType::Scene:
        return &config.external_editor_scene;
    case EditorExternalFileType::Generic:
    default:
        return &config.external_editor_generic;
    }
}

const std::string *GetExternalEditorCommandPtr(const EditorConfigData &config,
                                               EditorExternalFileType type) {
    switch (type) {
    case EditorExternalFileType::Audio:
        return &config.external_editor_audio;
    case EditorExternalFileType::Lua:
        return &config.external_editor_lua;
    case EditorExternalFileType::Config:
        return &config.external_editor_config;
    case EditorExternalFileType::Template:
        return &config.external_editor_template;
    case EditorExternalFileType::Image:
        return &config.external_editor_image;
    case EditorExternalFileType::Font:
        return &config.external_editor_font;
    case EditorExternalFileType::Scene:
        return &config.external_editor_scene;
    case EditorExternalFileType::Generic:
    default:
        return &config.external_editor_generic;
    }
}

const char *GetExternalEditorDisplayLabel(EditorExternalFileType type) {
    switch (type) {
    case EditorExternalFileType::Audio:
        return "Audio";
    case EditorExternalFileType::Lua:
        return "Lua";
    case EditorExternalFileType::Config:
        return "Config";
    case EditorExternalFileType::Template:
        return "Template";
    case EditorExternalFileType::Image:
        return "Image";
    case EditorExternalFileType::Font:
        return "Font";
    case EditorExternalFileType::Scene:
        return "Scene";
    case EditorExternalFileType::Generic:
    default:
        return "Generic";
    }
}

const char *GetExternalEditorConfigKey(EditorExternalFileType type) {
    switch (type) {
    case EditorExternalFileType::Audio:
        return "audio";
    case EditorExternalFileType::Lua:
        return "lua";
    case EditorExternalFileType::Config:
        return "config";
    case EditorExternalFileType::Template:
        return "template";
    case EditorExternalFileType::Image:
        return "image";
    case EditorExternalFileType::Font:
        return "font";
    case EditorExternalFileType::Scene:
        return "scene";
    case EditorExternalFileType::Generic:
    default:
        return "generic";
    }
}

std::filesystem::path ComparableProjectRoot(
    const std::filesystem::path &resources_root) {
    const std::filesystem::path normalized =
        ResourcePath::NormalizeProjectRoot(resources_root);
    std::error_code absolute_error;
    const std::filesystem::path absolute_path =
        std::filesystem::absolute(normalized, absolute_error);
    return (absolute_error ? normalized : absolute_path).lexically_normal();
}

bool AreSameProjectRoot(const std::filesystem::path &lhs,
                        const std::filesystem::path &rhs) {
    return ComparableProjectRoot(lhs) == ComparableProjectRoot(rhs);
}

void ReadProjectHistoryDocument(const rapidjson::Document &project_config,
                                EditorConfigData &config) {
    if (project_config.HasMember("current_project_resources_root") &&
        project_config["current_project_resources_root"].IsString()) {
        config.current_project_resources_root =
            ResourcePath::NormalizeProjectRoot(
                project_config["current_project_resources_root"].GetString());
    }

    if (!project_config.HasMember("recent_project_resources_roots") ||
        !project_config["recent_project_resources_roots"].IsArray()) {
        EditorConfig::RememberProject(config,
                                      config.current_project_resources_root);
        return;
    }

    config.recent_project_resources_roots.clear();
    const rapidjson::Value &recent_projects =
        project_config["recent_project_resources_roots"];
    for (rapidjson::SizeType index = 0; index < recent_projects.Size();
         ++index) {
        if (!recent_projects[index].IsString()) continue;
        const std::filesystem::path project_root =
            ResourcePath::NormalizeProjectRoot(
                recent_projects[index].GetString());
        bool duplicate = false;
        for (const std::filesystem::path &existing :
             config.recent_project_resources_roots) {
            if (AreSameProjectRoot(existing, project_root)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            config.recent_project_resources_roots.emplace_back(project_root);
        }
    }

    EditorConfig::RememberProject(config, config.current_project_resources_root);
}

void ReadProjectHistoryConfig(EditorConfigData &config,
                              const rapidjson::Document *legacy_editor_config) {
    if (std::filesystem::exists(kEditorProjectsConfigPath)) {
        rapidjson::Document project_config;
        ReadJsonFile(kEditorProjectsConfigPath.string(), project_config);
        ReadProjectHistoryDocument(project_config, config);
        return;
    }

    // Backward compatibility: early builds stored project MRU data directly in
    // editor.config. Read it once, then future writes go to projects.config.
    if (legacy_editor_config != nullptr) {
        ReadProjectHistoryDocument(*legacy_editor_config, config);
        return;
    }

    EditorConfig::RememberProject(config, config.current_project_resources_root);
}

void WriteProjectHistoryConfig(
    rapidjson::PrettyWriter<rapidjson::StringBuffer> &writer,
    const EditorConfigData &config) {
    writer.Key("current_project_resources_root");
    writer.String(config.current_project_resources_root.generic_string().c_str());
    writer.Key("recent_project_resources_roots");
    writer.StartArray();
    for (const std::filesystem::path &project_root :
         config.recent_project_resources_roots) {
        writer.String(project_root.generic_string().c_str());
    }
    writer.EndArray();
}

void WriteProjectHistoryFile(const EditorConfigData &config) {
    ResourcePath::EnsureDirectoryExists(kEditorProjectsConfigPath.parent_path());

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    WriteProjectHistoryConfig(writer, config);
    writer.EndObject();

    std::ofstream output_file(kEditorProjectsConfigPath,
                              std::ios::out | std::ios::trunc);
    if (!output_file.is_open()) {
        std::cout << "error: unable to write [" << kEditorProjectsConfigPath
                  << "]" << std::endl;
        exit(0);
    }
    output_file << buffer.GetString() << std::endl;
}

void ReadExternalEditorConfig(const rapidjson::Document &editor_config,
                              EditorConfigData &config) {
    if (!editor_config.HasMember("external_editors") ||
        !editor_config["external_editors"].IsObject()) {
        return;
    }

    const rapidjson::Value &external_editors = editor_config["external_editors"];
    for (const EditorExternalFileType type : kExternalEditorFileTypes) {
        const char *key = GetExternalEditorConfigKey(type);
        if (!external_editors.HasMember(key) || !external_editors[key].IsString()) {
            continue;
        }
        *GetExternalEditorCommandPtr(config, type) = external_editors[key].GetString();
    }
}

void WriteExternalEditorConfig(rapidjson::PrettyWriter<rapidjson::StringBuffer> &writer,
                               const EditorConfigData &config) {
    writer.Key("external_editors");
    writer.StartObject();
    for (const EditorExternalFileType type : kExternalEditorFileTypes) {
        writer.Key(GetExternalEditorConfigKey(type));
        writer.String(GetExternalEditorCommandPtr(config, type)->c_str());
    }
    writer.EndObject();
}

} // namespace

// Read optional editor.config overrides for the editor host window.
EditorConfigData EditorConfig::Read() {
    EditorConfigData config;

    std::filesystem::path config_path = kEditorConfigPath;
    if (!std::filesystem::exists(config_path) &&
        std::filesystem::exists(kLegacyEditorConfigPath)) {
        config_path = kLegacyEditorConfigPath;
    }

    // Missing editor.config is fine; the editor falls back to sensible defaults.
    if (!std::filesystem::exists(config_path)) {
        ReadProjectHistoryConfig(config, nullptr);
        return config;
    }

    rapidjson::Document editor_config;
    ReadJsonFile(config_path.string(), editor_config);

    if (editor_config.HasMember("window_width") &&
        editor_config["window_width"].IsInt()) {
        const int value = editor_config["window_width"].GetInt();
        if (value > 0) config.window_width = value;
    }

    if (editor_config.HasMember("window_height") &&
        editor_config["window_height"].IsInt()) {
        const int value = editor_config["window_height"].GetInt();
        if (value > 0) config.window_height = value;
    }

    if (editor_config.HasMember("window_x") &&
        editor_config["window_x"].IsInt() &&
        editor_config.HasMember("window_y") &&
        editor_config["window_y"].IsInt()) {
        config.window_x = editor_config["window_x"].GetInt();
        config.window_y = editor_config["window_y"].GetInt();
        config.window_has_position = true;
    }

    if (editor_config.HasMember("window_fullscreen") &&
        editor_config["window_fullscreen"].IsBool()) {
        config.window_fullscreen = editor_config["window_fullscreen"].GetBool();
    }

    if (editor_config.HasMember("window_maximized") &&
        editor_config["window_maximized"].IsBool()) {
        config.window_maximized = editor_config["window_maximized"].GetBool();
    }

    if (editor_config.HasMember("window_title") &&
        editor_config["window_title"].IsString()) {
        config.window_title = editor_config["window_title"].GetString();
    }

    if (editor_config.HasMember("ui_scale") &&
        editor_config["ui_scale"].IsNumber()) {
        const float value = editor_config["ui_scale"].GetFloat();
        if (value > 0.0f) config.ui_scale = value;
    }

    if (editor_config.HasMember("reference_width") &&
        editor_config["reference_width"].IsInt()) {
        const int value = editor_config["reference_width"].GetInt();
        if (value > 0) config.reference_width = value;
    }

    if (editor_config.HasMember("reference_height") &&
        editor_config["reference_height"].IsInt()) {
        const int value = editor_config["reference_height"].GetInt();
        if (value > 0) config.reference_height = value;
    }

    if (editor_config.HasMember("scene_view_width") &&
        editor_config["scene_view_width"].IsInt()) {
        const int value = editor_config["scene_view_width"].GetInt();
        if (value > 0) config.scene_view_width = value;
    }

    if (editor_config.HasMember("scene_view_height") &&
        editor_config["scene_view_height"].IsInt()) {
        const int value = editor_config["scene_view_height"].GetInt();
        if (value > 0) config.scene_view_height = value;
    }

    ReadExternalEditorConfig(editor_config, config);
    ReadProjectHistoryConfig(config, &editor_config);

    return config;
}

// Persist the current editor host settings back to editor.config.
void EditorConfig::Write(const EditorConfigData &config) {
    ResourcePath::EnsureDirectoryExists(kEditorConfigPath.parent_path());

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);

    writer.StartObject();
    writer.Key("window_title");
    writer.String(config.window_title.c_str());
    writer.Key("window_width");
    writer.Int(config.window_width);
    writer.Key("window_height");
    writer.Int(config.window_height);
    if (config.window_has_position) {
        writer.Key("window_x");
        writer.Int(config.window_x);
        writer.Key("window_y");
        writer.Int(config.window_y);
    }
    writer.Key("window_fullscreen");
    writer.Bool(config.window_fullscreen);
    writer.Key("window_maximized");
    writer.Bool(config.window_maximized);
    writer.Key("ui_scale");
    writer.Double(config.ui_scale);
    writer.Key("reference_width");
    writer.Int(config.reference_width);
    writer.Key("reference_height");
    writer.Int(config.reference_height);
    writer.Key("scene_view_width");
    writer.Int(config.scene_view_width);
    writer.Key("scene_view_height");
    writer.Int(config.scene_view_height);
    WriteExternalEditorConfig(writer, config);
    writer.EndObject();

    std::ofstream output_file(kEditorConfigPath,
                              std::ios::out | std::ios::trunc);
    if (!output_file.is_open()) {
        std::cout << "error: unable to write [" << kEditorConfigPath << "]"
                  << std::endl;
        exit(0);
    }

    output_file << buffer.GetString() << std::endl;
}

void EditorConfig::WriteProjectHistory(const EditorConfigData &config) {
    WriteProjectHistoryFile(config);
}

// Return all built-in file types that support external editor mapping.
const std::array<EditorExternalFileType, 8> &
EditorConfig::ExternalEditorFileTypes() {
    return kExternalEditorFileTypes;
}

// Return a short display label for one external editor file type.
const char *EditorConfig::ExternalEditorDisplayLabel( EditorExternalFileType type) {
    return GetExternalEditorDisplayLabel(type);
}

// Return JSON key name used in editor.config for one file type.
const char *EditorConfig::ExternalEditorConfigKey(EditorExternalFileType type) {
    return GetExternalEditorConfigKey(type);
}

// Mutable / immutable accessors to command string for one file type.
std::string &EditorConfig::ExternalEditorCommand(EditorConfigData &config,
                                                 EditorExternalFileType type) {
    return *GetExternalEditorCommandPtr(config, type);
}

// Mutable / immutable accessors to command string for one file type.
const std::string &EditorConfig::ExternalEditorCommand( const EditorConfigData &config, EditorExternalFileType type) {
    return *GetExternalEditorCommandPtr(config, type);
}

void EditorConfig::RememberProject(
    EditorConfigData &config, const std::filesystem::path &resources_root) {
    constexpr std::size_t kMaxRecentProjects = 12;
    const std::filesystem::path normalized_root =
        ResourcePath::NormalizeProjectRoot(resources_root);
    config.current_project_resources_root = normalized_root;

    std::vector<std::filesystem::path> next_recent_projects;
    next_recent_projects.emplace_back(normalized_root);
    for (const std::filesystem::path &existing_root :
         config.recent_project_resources_roots) {
        if (AreSameProjectRoot(existing_root, normalized_root)) continue;
        bool duplicate = false;
        for (const std::filesystem::path &kept_root : next_recent_projects) {
            if (AreSameProjectRoot(kept_root, existing_root)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        next_recent_projects.emplace_back(
            ResourcePath::NormalizeProjectRoot(existing_root));
        if (next_recent_projects.size() >= kMaxRecentProjects) break;
    }
    config.recent_project_resources_roots = std::move(next_recent_projects);
}
