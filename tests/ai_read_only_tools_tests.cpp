#include "editor/ai/AIReadOnlyTools.h"
#include "editor/documents/SceneDocument.h"
#include "rapidjson/document.h"
#include "shared/resources/ResourcePath.h"
#include "scripting/ComponentManager.h"
#include <filesystem>
#include <iostream>
#include <string>

#ifndef POCKET_ENGINE_TEST_PROJECT_ROOT
#define POCKET_ENGINE_TEST_PROJECT_ROOT ""
#endif

namespace {

bool ExecuteAndParse(const std::string &tool, const std::string &arguments,
                     const AIEditorContext &context,
                     rapidjson::Document &out_document) {
    std::string result;
    std::string code;
    std::string message;
    if (!AIReadOnlyTools::Execute(tool, arguments, context, result, code,
                                  message)) {
        std::cerr << tool << " failed: " << code << ": " << message
                  << std::endl;
        return false;
    }
    out_document.Parse(result.c_str());
    if (out_document.HasParseError() || !out_document.IsObject()) {
        std::cerr << tool << " returned invalid JSON." << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main() {
    const std::filesystem::path project_root =
        POCKET_ENGINE_TEST_PROJECT_ROOT;
    ResourcePath::SetResourcesRootPath(project_root);
    ComponentManager::Initialize();
    SceneDocument scene;
    if (!scene.LoadFromSceneName("ballgame") ||
        scene.GetActorCount() == 0) {
        std::cerr << "Could not load the default ballgame scene."
                  << std::endl;
        return 1;
    }

    AIEditorContext context;
    context.scene_document = &scene;
    context.project_root = project_root;
    context.selected_actor_index = 0;

    rapidjson::Document state;
    if (!ExecuteAndParse("get_editor_state", "{}", context, state) ||
        !state.HasMember("writes_enabled") ||
        state["writes_enabled"].GetBool()) {
        return 1;
    }

    rapidjson::Document summary;
    if (!ExecuteAndParse("get_current_scene",
                         "{\"include_components\":true,\"max_actors\":2}",
                         context, summary) ||
        !summary.HasMember("actors") || !summary["actors"].IsArray() ||
        summary["actors"].Empty()) {
        return 1;
    }

    const std::string inspect_arguments =
        "{\"actor_uid\":" + std::to_string(scene.GetActorUID(0)) + "}";
    rapidjson::Document actor;
    if (!ExecuteAndParse("inspect_actor", inspect_arguments, context,
                         actor) ||
        !actor.HasMember("components") ||
        !actor["components"].IsArray()) {
        return 1;
    }

    rapidjson::Document assets;
    if (!ExecuteAndParse("search_assets",
                         "{\"query\":\"ballgame\",\"type\":\"scene\"}",
                         context, assets) ||
        !assets.HasMember("assets") || !assets["assets"].IsArray() ||
        assets["assets"].Empty()) {
        return 1;
    }

    rapidjson::Document component_types;
    if (!ExecuteAndParse("list_component_types",
                         "{\"query\":\"Transform\",\"limit\":10}",
                         context, component_types) ||
        !component_types.HasMember("component_types") ||
        !component_types["component_types"].IsArray() ||
        component_types["component_types"].Empty()) {
        return 1;
    }

    std::string result;
    std::string code;
    std::string message;
    if (AIReadOnlyTools::Execute("inspect_actor",
                                 "{\"actor_uid\":999999999}", context,
                                 result, code, message) ||
        code != "ACTOR_NOT_FOUND") {
        std::cerr << "inspect_actor should return ACTOR_NOT_FOUND."
                  << std::endl;
        return 1;
    }

    ComponentManager::Shutdown();
    std::cout << "AI read-only tools tests passed." << std::endl;
    return 0;
}
