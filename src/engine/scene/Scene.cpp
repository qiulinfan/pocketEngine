#include "scene/Scene.h"
#include "scene/Actor.h"
#include "shared/scene_format/SceneFormat.h"
#include "scripting/ComponentManager.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <utility>

// Initialize the static ID counter
int Scene::next_actor_id = 0;
std::filesystem::path Scene::active_scene_subdirectory;

// allocate a globally unique actor id
int Scene::AllocateActorID() {
    return next_actor_id++;
}

// Override the active scene subdirectory used for resource preference.
void Scene::SetActiveSceneSubdirectory(
    const std::filesystem::path &subdirectory) {
    active_scene_subdirectory = subdirectory.lexically_normal();
    if (active_scene_subdirectory == ".") {
        active_scene_subdirectory.clear();
    }
}

// relative subdirectory under resources/scenes for the currently active scene
const std::filesystem::path &Scene::GetActiveSceneSubdirectory() {
    return active_scene_subdirectory;
}

// load actors from resources/scenes/<scene_name>.scene
std::vector<Actor> Scene::LoadScene(const std::string &scene_name) {
    std::vector<Actor> actors;

    const SceneFormat::SceneAsset scene_asset =
        SceneFormat::LoadSceneAsset(scene_name, Scene::GetActiveSceneSubdirectory());
    if (scene_asset.scene_path.empty()) {
        std::cout << "error: scene " << scene_name << " is missing";
        exit(0);
    }

    // Runtime keeps the active scene subdirectory in sync so later resource
    // lookups prefer files that live next to the active scene.
    SetActiveSceneSubdirectory(scene_asset.scene_subdirectory);
    ComponentManager::ReloadComponentTypes();

    actors.reserve(scene_asset.actors.size());
    for (const SceneFormat::ActorRecord &actor_record : scene_asset.actors) {
        Actor actor;
        if (!actor_record.template_name.empty()) {
            actor = Actor::LoadTemplate(actor_record.template_name);
        }
        actor = SceneFormat::ApplyActorRecordToActor(std::move(actor), actor_record);

        // Assign unique, strictly-increasing ID (after template load)
        actor.id = AllocateActorID();

        if (!actor.component_specs.empty()) {
            // Lifecycle requirement: process components by key lexical order.
            for (const Actor::ComponentSpec &component_spec : actor.component_specs) {
                ComponentManager::InstantiateComponentForActor(actor.id,
                                                               component_spec);
            }
        }

        actors.emplace_back(std::move(actor));
    }
    return actors;
}
