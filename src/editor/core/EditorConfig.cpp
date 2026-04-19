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

namespace {

const std::filesystem::path kEditorConfigPath = ResourcePath::EditorConfigPath();
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
