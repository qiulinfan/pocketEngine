#include "shared/config/GameConfig.h"
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

std::filesystem::path GameConfigPath() {
    return ResourcePath::ResourcesRootPath() / "game.config";
}

std::filesystem::path RenderingConfigPath() {
    return ResourcePath::ResourcesRootPath() / "rendering.config";
}

void ReadJsonFile(const std::string &path, rapidjson::Document &out_document) {
    FILE *file_pointer = nullptr;
#ifdef _WIN32
    fopen_s(&file_pointer, path.c_str(), "rb");
#else
    file_pointer = fopen(path.c_str(), "rb");
#endif
    if (file_pointer == nullptr) {
        out_document.SetObject();
        return;
    }
    char buffer[65536];
    rapidjson::FileReadStream stream(file_pointer, buffer, sizeof(buffer));
    out_document.ParseStream(stream);
    std::fclose(file_pointer);

    if (out_document.HasParseError()) {
        std::cout << "error parsing json at [" << path << "]" << std::endl;
        std::exit(0);
    }
}

bool TryReadJsonObjectFile(const std::filesystem::path &path,
                           rapidjson::Document &out_document) {
    if (!std::filesystem::exists(path)) {
        out_document.SetObject();
        return true;
    }

    FILE *file_pointer = nullptr;
#ifdef _WIN32
    fopen_s(&file_pointer, path.string().c_str(), "rb");
#else
    file_pointer = fopen(path.string().c_str(), "rb");
#endif
    if (file_pointer == nullptr) {
        return false;
    }

    char buffer[65536];
    rapidjson::FileReadStream stream(file_pointer, buffer, sizeof(buffer));
    out_document.ParseStream(stream);
    std::fclose(file_pointer);
    if (out_document.HasParseError() || !out_document.IsObject()) {
        return false;
    }
    return true;
}

void UpsertStringMember(rapidjson::Document &document, const char *key,
                        const std::string &value) {
    rapidjson::Value key_value(key, document.GetAllocator());
    rapidjson::Value string_value(value.c_str(), document.GetAllocator());
    if (document.HasMember(key)) {
        document[key].SetString(value.c_str(), document.GetAllocator());
        return;
    }
    document.AddMember(key_value.Move(), string_value.Move(),
                       document.GetAllocator());
}

void UpsertIntMember(rapidjson::Document &document, const char *key, int value) {
    rapidjson::Value key_value(key, document.GetAllocator());
    rapidjson::Value int_value(value);
    if (document.HasMember(key)) {
        document[key].SetInt(value);
        return;
    }
    document.AddMember(key_value.Move(), int_value.Move(),
                       document.GetAllocator());
}

void UpsertFloatMember(rapidjson::Document &document, const char *key,
                       float value) {
    rapidjson::Value key_value(key, document.GetAllocator());
    rapidjson::Value float_value(value);
    if (document.HasMember(key)) {
        document[key].SetFloat(value);
        return;
    }
    document.AddMember(key_value.Move(), float_value.Move(),
                       document.GetAllocator());
}

bool WriteJsonFile(const std::filesystem::path &path,
                   const rapidjson::Document &document) {
    std::error_code directory_error;
    if (!path.parent_path().empty() &&
        !std::filesystem::exists(path.parent_path(), directory_error)) {
        std::filesystem::create_directories(path.parent_path(), directory_error);
        if (directory_error) return false;
    }

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    std::ofstream output_file(path, std::ios::out | std::ios::trunc);
    if (!output_file.is_open()) {
        return false;
    }
    output_file << buffer.GetString() << std::endl;
    return true;
}

} // namespace

// Read game.config and rendering.config into one lightweight shared value
// object. Engine owns the behavior; this type only describes startup data.
GameConfigData GameConfig::Read() {
    GameConfigData config;

    // Shared startup config is intentionally simple: the runtime and editor
    // both consume the same read-only values, then diverge into their own
    // session state once bootstrapping is complete.
    const std::filesystem::path resources_root =
        ResourcePath::ResourcesRootPath();
    const std::filesystem::path game_config_path = GameConfigPath();
    const std::filesystem::path rendering_config_path = RenderingConfigPath();

    if (!std::filesystem::exists(resources_root) ||
        !std::filesystem::is_directory(resources_root)) {
        std::cout << "warning: project resources folder missing ["
                  << resources_root.string() << "]; using default config"
                  << std::endl;
        return config;
    }
    if (!std::filesystem::exists(game_config_path)) {
        std::cout << "warning: project game.config missing ["
                  << game_config_path.string() << "]; using default config"
                  << std::endl;
        return config;
    }

    rapidjson::Document game_config;
    ReadJsonFile(game_config_path.string(), game_config);

    if (game_config.HasMember("game_title") &&
        game_config["game_title"].IsString()) {
        config.game_title = game_config["game_title"].GetString();
    }

    if (game_config.HasMember("initial_scene") &&
        game_config["initial_scene"].IsString()) {
        config.initial_scene_name = game_config["initial_scene"].GetString();
    }

    if (std::filesystem::exists(rendering_config_path)) {
        rapidjson::Document rendering_config;
        ReadJsonFile(rendering_config_path.string(), rendering_config);

        if (rendering_config.HasMember("x_resolution") &&
            rendering_config["x_resolution"].IsInt()) {
            config.window_width = rendering_config["x_resolution"].GetInt();
        }
        if (rendering_config.HasMember("y_resolution") &&
            rendering_config["y_resolution"].IsInt()) {
            config.window_height = rendering_config["y_resolution"].GetInt();
        }
        if (rendering_config.HasMember("clear_color_r") &&
            rendering_config["clear_color_r"].IsInt()) {
            const int value = rendering_config["clear_color_r"].GetInt();
            if (value >= 0 && value <= 255) config.clear_color_r = value;
        }
        if (rendering_config.HasMember("clear_color_g") &&
            rendering_config["clear_color_g"].IsInt()) {
            const int value = rendering_config["clear_color_g"].GetInt();
            if (value >= 0 && value <= 255) config.clear_color_g = value;
        }
        if (rendering_config.HasMember("clear_color_b") &&
            rendering_config["clear_color_b"].IsInt()) {
            const int value = rendering_config["clear_color_b"].GetInt();
            if (value >= 0 && value <= 255) config.clear_color_b = value;
        }
        if (rendering_config.HasMember("zoom_factor") &&
            rendering_config["zoom_factor"].IsNumber()) {
            const float zoom = rendering_config["zoom_factor"].GetFloat();
            if (zoom > 0.0f) config.zoom_factor = zoom;
        }
    }

    return config;
}

bool GameConfig::Write(const GameConfigData &config) {
    const std::filesystem::path game_config_path = GameConfigPath();
    const std::filesystem::path rendering_config_path = RenderingConfigPath();

    rapidjson::Document game_config;
    if (!TryReadJsonObjectFile(game_config_path, game_config)) {
        std::cout << "error: unable to read [" << game_config_path << "]"
                  << std::endl;
        return false;
    }
    if (!game_config.IsObject()) {
        game_config.SetObject();
    }

    rapidjson::Document rendering_config;
    if (!TryReadJsonObjectFile(rendering_config_path, rendering_config)) {
        std::cout << "error: unable to read [" << rendering_config_path << "]"
                  << std::endl;
        return false;
    }
    if (!rendering_config.IsObject()) {
        rendering_config.SetObject();
    }

    UpsertStringMember(game_config, "game_title", config.game_title);
    UpsertStringMember(game_config, "initial_scene", config.initial_scene_name);

    UpsertIntMember(rendering_config, "x_resolution",
                    std::max(1, config.window_width));
    UpsertIntMember(rendering_config, "y_resolution",
                    std::max(1, config.window_height));
    UpsertIntMember(rendering_config, "clear_color_r",
                    std::max(0, std::min(255, config.clear_color_r)));
    UpsertIntMember(rendering_config, "clear_color_g",
                    std::max(0, std::min(255, config.clear_color_g)));
    UpsertIntMember(rendering_config, "clear_color_b",
                    std::max(0, std::min(255, config.clear_color_b)));
    UpsertFloatMember(rendering_config, "zoom_factor",
                      config.zoom_factor > 0.0f ? config.zoom_factor : 1.0f);

    if (!WriteJsonFile(game_config_path, game_config)) {
        std::cout << "error: unable to write [" << game_config_path << "]"
                  << std::endl;
        return false;
    }
    if (!WriteJsonFile(rendering_config_path, rendering_config)) {
        std::cout << "error: unable to write [" << rendering_config_path << "]"
                  << std::endl;
        return false;
    }
    return true;
}
