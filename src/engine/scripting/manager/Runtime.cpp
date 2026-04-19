#include "Internal.h"
#include "shared/resources/ResourcePath.h"
#include "scripting/ComponentManager.h"
#include "particles/ParticleManager.h"
#include "physics/Rigidbody.h"
#include "scene/Scene.h"
#include "scripting/APIRegistration.h"
#include "scripting/EventBus.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>

using namespace ManagerDetail;

namespace {

void ClearBoundActorState() {
    g_runtime.pending_on_start.clear();
    for (auto &actor_components_entry : g_runtime.actor_components) {
        for (std::unique_ptr<ComponentRecord> &component_ptr :
             actor_components_entry.second) {
            RunComponentOnDestroyIfNeeded(*component_ptr,
                                          actor_components_entry.first);
        }
    }
    g_runtime.actor_components.clear();
    ParticleManager::Clear();
    Rigidbody::DestroyPhysicsWorld();
    g_runtime.component_index_by_key.clear();
    g_runtime.component_first_key_by_type.clear();
    g_runtime.component_keys_by_type.clear();
    g_runtime.on_update_components.clear();
    g_runtime.on_late_update_components.clear();
    g_runtime.actor_by_id.clear();
    g_runtime.actors_by_name.clear();
    g_runtime.actor_order_by_id.clear();
    g_runtime.actor_ids_sorted.clear();
    g_runtime.pending_actor_ids_to_activate.clear();
    g_runtime.pending_destroy_actor_ids.clear();
    g_runtime.dirty_component_actor_ids.clear();
    g_runtime.scene_actors = nullptr;
}

} // namespace

namespace ManagerDetail {

void EstablishInheritance(luabridge::LuaRef &instance_table,
                          luabridge::LuaRef &parent_table) {
    // annoymous 中介, between instance and component type,
    // to avoid polluting component type table with instance-level state (e.g. self.key).
    // 这样 parent_table 只需要包含纯粹的方法和属性, 不需要感知自己是否被当作元表使用.
    luabridge::LuaRef new_metatable = luabridge::newTable(g_runtime.lua_state);
    new_metatable["__index"] = parent_table;
    instance_table.push(g_runtime.lua_state);
    new_metatable.push(g_runtime.lua_state);
    lua_setmetatable(g_runtime.lua_state, -2);
    lua_pop(g_runtime.lua_state, 1);
}

void LoadComponentTypes() {
    g_runtime.component_type_tables.clear();

    const std::filesystem::path component_root = "resources/component_types";
    if (!std::filesystem::exists(component_root) ||
        !std::filesystem::is_directory(component_root)) {
        return;
    }

    std::vector<std::filesystem::path> lua_files =
        ResourcePath::CollectFilesRecursively(component_root, ".lua");
    ResourcePath::SortPathsWithPreference(
        lua_files, component_root, Scene::GetActiveSceneSubdirectory());

    for (const std::filesystem::path &lua_file : lua_files) {
        const std::string component_type = lua_file.stem().string();
        if (g_runtime.component_type_tables.find(component_type) !=
            g_runtime.component_type_tables.end()) {
            continue;
        }

        // Load and execute component file so it registers global table <type>.
        if (luaL_dofile(g_runtime.lua_state, lua_file.string().c_str()) != LUA_OK) {
            std::cout << "problem with lua file " << component_type;
            std::exit(0);
        }

        // Each valid component file must expose a table with the same name.
        luabridge::LuaRef base_table = luabridge::getGlobal(g_runtime.lua_state, component_type.c_str());
        if (!base_table.isTable()) {
            std::cout << "problem with lua file " << component_type;
            std::exit(0);
        }
        g_runtime.component_type_tables.emplace(component_type,
                                                std::move(base_table));
    }
}

void ApplyPropertyOverrides(
    luabridge::LuaRef &instance_table,
    const std::vector<Actor::ComponentProperty> &property_overrides) {
    // 把 JSON override 覆盖写入实例 table
    for (const Actor::ComponentProperty &property : property_overrides) {
        std::visit(
            [&](const auto &value) {
                // Write JSON override into Lua instance table before lifecycle
                instance_table[property.name] = value;
            },
            property.value);
    }
}


} // namespace ManagerDetail

// -----------------------------------------------------------------------------
// ComponentManager public API: initialization / teardown
// -----------------------------------------------------------------------------

// update the current engine pointer used by Lua APIs
void ComponentManager::BindEngine(Engine *engine) {
    APIRegistration::BindEngine(engine);
}

// create the Lua VM, register host APIs, and load component type scripts
void ComponentManager::Initialize() {
    // 只初始化一次, 全程复用同一个 Lua VM
    if (g_runtime.lua_state != nullptr) return;

    g_runtime.lua_state = luaL_newstate();
    if (g_runtime.lua_state == nullptr) std::exit(0);

    luaL_openlibs(g_runtime.lua_state);
    EventBus::Hooks event_bus_hooks;
    event_bus_hooks.lua_state = g_runtime.lua_state;
    event_bus_hooks.are_same_ref = &AreSameLuaRef;
    event_bus_hooks.try_extract_component_identity = &TryExtractComponentIdentity;
    event_bus_hooks.is_component_ref_alive = &IsComponentRefAlive;
    event_bus_hooks.report_error = &ReportEventBusError;
    EventBus::Initialize(event_bus_hooks);

    APIRegistration::Initialize(g_runtime.lua_state);
    LoadComponentTypes();
}

// create the Lua VM, register host APIs, and load component type scripts
void ComponentManager::ReloadComponentTypes() {
    if (g_runtime.lua_state == nullptr) return;
    LoadComponentTypes();
}

// Tear down the shared Lua runtime and every cached component-type table.
void ComponentManager::Shutdown() {
    // 先清空所有持有 LuaRef 的容器, 再关闭 lua_state
    if (g_runtime.lua_state == nullptr) return;

    EventBus::Shutdown();
    ClearBoundActorState();
    APIRegistration::BindEngine(nullptr);
    g_runtime.runtime_add_component_counter = 0;
    g_runtime.component_type_tables.clear();
    lua_close(g_runtime.lua_state);
    g_runtime.lua_state = nullptr;
}

// -----------------------------------------------------------------------------
// ComponentManager public API: scene binding
// -----------------------------------------------------------------------------

// clear all runtime component state tied to scene actors
void ComponentManager::ClearActorComponents() {
    // 场景切换时清理与 actor 相关的状态
    EventBus::Clear();
    ClearBoundActorState();
}
