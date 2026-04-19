#include "scene/Actor.h"
#include "scene/Scene.h"
#include "shared/scene_format/SceneFormat.h"
#include "scripting/ComponentManager.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"
#include <cstdlib>
#include <iostream>
#include <unordered_map>

namespace {

std::unordered_map<std::string, Actor> g_template_cache;

} // namespace

std::string Actor::GetName() const {
    return actor_name;
}

Actor::UID Actor::GetUID() const {
    return uid;
}

bool Actor::IsSceneBacked() const {
    return scene_backed;
}

bool Actor::IsRuntimeSpawned() const {
    return !scene_backed;
}

bool Actor::SetParent(Actor *parent) const {
    return ComponentManager::SetActorParent(uid, parent);
}

luabridge::LuaRef Actor::AddComponent(const std::string &type_name) const {
    return ComponentManager::AddComponent(uid, type_name);
}

void Actor::RemoveComponent(luabridge::LuaRef component_ref) const {
    ComponentManager::RemoveComponent(uid, component_ref);
}

// get component by key / type. returns nil if not found.
// if multiple components of the same type exist, returns the first one.
luabridge::LuaRef Actor::GetComponentByKey(const std::string &key) const {
    return ComponentManager::GetComponentByKey(uid, key);
}

// get component by key / type. returns nil if not found.
// if multiple components of the same type exist, returns the first one.
luabridge::LuaRef Actor::GetComponent(const std::string &type_name) const {
    return ComponentManager::GetComponentByType(uid, type_name);
}

// returns all component instances of the given type as a Lua array.
luabridge::LuaRef Actor::GetComponents(const std::string &type_name) const {
    return ComponentManager::GetComponentsByType(uid, type_name);
}

// loads an actor data copy from an actor template file.
Actor Actor::LoadTemplate(const std::string &template_name) {
    // Template parsing now lives in shared/scene_format so runtime scene loads
    // and editor effective-actor reconstruction obey the same JSON rules.
    const std::string template_path = SceneFormat::ResolveTemplatePath(
        template_name, Scene::GetActiveSceneSubdirectory());
    auto cache_it = g_template_cache.find(template_path);
    if (cache_it != g_template_cache.end()) {
        return cache_it->second;
    }

    Actor actor;

    if (template_path.empty()) {
        std::cout << "error: template " << template_name << " is missing";
        exit(0);
    }

    actor = SceneFormat::LoadActorTemplateAsset(
                template_name, Scene::GetActiveSceneSubdirectory())
                .actor;
    SceneFormat::EnsureBuiltinTransformComponent(actor.component_specs);

    auto inserted = g_template_cache.emplace(template_path, actor);
    return inserted.first->second;
}

// return 第一个名称匹配的 actor.
luabridge::LuaRef Actor::Find(const std::string &name) {
    return ComponentManager::FindActorByName(name);
}

// return所有名称匹配的 actor, 结果是 Lua 数组.
luabridge::LuaRef Actor::FindAll(const std::string &name) {
    return ComponentManager::FindAllActorsByName(name);
}

// 当前场景中实例化一个 actor, 返回其 Lua 引用.
luabridge::LuaRef Actor::Instantiate(const std::string &template_name) {
    return ComponentManager::InstantiateActor(template_name);
}

// 请求在本帧末销毁指定 actor.
void Actor::Destroy(Actor *actor) {
    ComponentManager::DestroyActor(actor);
}
