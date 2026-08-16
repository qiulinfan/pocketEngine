#include "app/runtime/RuntimeApp.h"
#include "engine/core/Engine.h"
#include "shared/resources/ResourcePath.h"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

bool TryReadJsonFile(const std::filesystem::path &path,
                     rapidjson::Document &out_document) {
    FILE *file_pointer = nullptr;
#ifdef _WIN32
    fopen_s(&file_pointer, path.string().c_str(), "rb");
#else
    file_pointer = fopen(path.string().c_str(), "rb");
#endif
    if (file_pointer == nullptr) return false;

    char buffer[65536];
    rapidjson::FileReadStream stream(file_pointer, buffer, sizeof(buffer));
    out_document.ParseStream(stream);
    std::fclose(file_pointer);
    return !out_document.HasParseError() && out_document.IsObject();
}

bool TryReadProjectRootFromDocument(const rapidjson::Document &document,
                                    std::filesystem::path &out_project_root) {
    const char *project_root_key = nullptr;
    if (document.HasMember("current_project_root") &&
        document["current_project_root"].IsString()) {
        project_root_key = "current_project_root";
    } else if (document.HasMember("current_project_resources_root") &&
               document["current_project_resources_root"].IsString()) {
        project_root_key = "current_project_resources_root";
    }
    if (project_root_key == nullptr) return false;

    const std::filesystem::path project_root =
        ResourcePath::NormalizeProjectRoot(document[project_root_key].GetString());
    if (!std::filesystem::exists(project_root) ||
        !std::filesystem::is_directory(project_root)) {
        return false;
    }

    out_project_root = project_root;
    return true;
}

bool TryReadEditorProjectRoot(std::filesystem::path &out_project_root) {
    rapidjson::Document project_config;
    if (TryReadJsonFile(ResourcePath::EditorProjectsConfigPath(),
                        project_config) &&
        TryReadProjectRootFromDocument(project_config, out_project_root)) {
        return true;
    }

    rapidjson::Document legacy_editor_config;
    if (TryReadJsonFile(ResourcePath::EditorConfigPath(),
                        legacy_editor_config) &&
        TryReadProjectRootFromDocument(legacy_editor_config,
                                       out_project_root)) {
        return true;
    }

    return false;
}

void ApplyEditorSelectedProjectRootForRuntime() {
    const char *environment_root = std::getenv("POCKET_PROJECT_ROOT");
    if (environment_root != nullptr && environment_root[0] != '\0') {
        const std::filesystem::path project_root =
            ResourcePath::NormalizeProjectRoot(environment_root);
        if (std::filesystem::exists(project_root) &&
            std::filesystem::is_directory(project_root)) {
            ResourcePath::SetResourcesRootPath(project_root);
            return;
        }
    }
    std::filesystem::path project_root;
    if (!TryReadEditorProjectRoot(project_root)) return;
    ResourcePath::SetResourcesRootPath(project_root);
}

} // namespace

// Boot the standalone runtime executable and stay in the traditional game
// loop until the engine requests quit.
int RuntimeApp::Run() {
    std::ios::sync_with_stdio(false);
    ApplyEditorSelectedProjectRootForRuntime();
    Engine engine;
    engine.GameLoop();
    return 0;
}
