#ifndef MANAGER_INTERNAL_H
#define MANAGER_INTERNAL_H

#include "physics/Collision.h"
#include "scene/Actor.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ComponentManager owns three big responsibilities:
// 1) Lua VM / host API registration
// 2) component instance indexing and lifecycle dispatch
// 3) runtime scene mutation + physics event bridging
// register APIs -> build/query component state -> execute per-frame work.

namespace ManagerDetail {

// -----------------------------------------------------------------------------
// Internal runtime records
// -----------------------------------------------------------------------------

// component instace 在 C++ 侧的 record
// Note: 真正的数据和函数都在 instance_table（Lua table）里. 这里为了快速做生命周期调度和查询.
struct ComponentRecord {
    std::string key;
    std::string type;
    luabridge::LuaRef instance_table;
    bool has_on_start = false;
    bool has_on_destroy = false;
    bool has_on_update = false;
    bool has_on_late_update = false;
    bool has_on_collision_enter = false;
    bool has_on_collision_exit = false;
    bool has_on_trigger_enter = false;
    bool has_on_trigger_exit = false;
    bool on_start_called = false;
    bool on_destroy_called = false;
    bool removed = false;

    ComponentRecord(std::string key_in, std::string type_in,
                    luabridge::LuaRef instance_table_in, bool has_on_start_in,
                    bool has_on_destroy_in, bool has_on_update_in,
                    bool has_on_late_update_in,
                    bool has_on_collision_enter_in,
                    bool has_on_collision_exit_in,
                    bool has_on_trigger_enter_in,
                    bool has_on_trigger_exit_in)
        : key(std::move(key_in)), type(std::move(type_in)),
          instance_table(std::move(instance_table_in)),
          has_on_start(has_on_start_in), has_on_destroy(has_on_destroy_in),
          has_on_update(has_on_update_in),
          has_on_late_update(has_on_late_update_in),
          has_on_collision_enter(has_on_collision_enter_in),
          has_on_collision_exit(has_on_collision_exit_in),
          has_on_trigger_enter(has_on_trigger_enter_in),
          has_on_trigger_exit(has_on_trigger_exit_in) {}
};

// 待执行 OnStart 的队列元素 (按 actor_id + component_key 定位)
// 只在“帧开始”时统一消费, 避免边迭代边修改容器
struct PendingOnStartRecord {
    int actor_id = -1;
    std::string component_key;

    PendingOnStartRecord(int actor_id_in, std::string component_key_in)
        : actor_id(actor_id_in), component_key(std::move(component_key_in)) {}
};

// 预筛出的生命周期调用列表 (仅包含声明了该函数的组件)
struct LifecycleComponentRef {
    int actor_id = -1;
    ComponentRecord *component = nullptr;

    LifecycleComponentRef(int actor_id_in, ComponentRecord *component_in)
        : actor_id(actor_id_in), component(component_in) {}
};

// -----------------------------------------------------------------------------
// Global runtime state
// -----------------------------------------------------------------------------

struct RuntimeState {
    // Lua VM
    lua_State *lua_state = nullptr;
    // component type registry：type 名 -> component type table (blueprint)
    std::unordered_map<std::string, luabridge::LuaRef> component_type_tables;
    // actor_id -> component instance tables of the actor
    std::unordered_map<int, std::vector<std::unique_ptr<ComponentRecord>>>
        actor_components;
    // actor_id -> (component_key -> component_index in actor_components[actor_id])
    std::unordered_map<int, std::unordered_map<std::string, size_t>>
        component_index_by_key;
    // actor_id -> (type -> first component key of this type, by key order)
    std::unordered_map<int, std::unordered_map<std::string, std::string>>
        component_first_key_by_type;
    // actor_id -> (type -> all component keys of this type, already sorted by key)
    std::unordered_map<int, std::unordered_map<std::string,
                                               std::vector<std::string>>>
        component_keys_by_type;
    // 全局生命周期执行列表 (按 actor_id, key 排序)
    std::vector<LifecycleComponentRef> on_update_components;
    std::vector<LifecycleComponentRef> on_late_update_components;
    std::vector<PendingOnStartRecord> pending_on_start;
    // actor_id -> Actor*
    std::unordered_map<int, Actor *> actor_by_id;
    // name -> Actor*
    std::unordered_map<std::string, std::vector<Actor *>> actors_by_name;
    // actor_id -> current scene-order index among active actors
    std::unordered_map<int, size_t> actor_order_by_id;
    // ordered actor id vec (not distroyed yet)
    std::vector<int> actor_ids_sorted;
    // ptr to the actor container of current scene (stored by Engine)
    std::deque<Actor> *scene_actors = nullptr;
    // 本帧新建的 actors (finable, 但是到下一帧才进入生命周期迭代)
    std::vector<int> pending_actor_ids_to_activate;
    // 本帧请求销毁的 actors, 帧末统一处理
    std::unordered_set<int> pending_destroy_actor_ids;
    // 发生过组件增删改（主要是 remove）的 actor，帧末只处理这些
    std::unordered_set<int> dirty_component_actor_ids;
    // AddComponent global counter
    uint64_t runtime_add_component_counter = 0;
};

extern RuntimeState g_runtime;

bool IsBuiltinComponentType(const std::string &type_name);

// -----------------------------------------------------------------------------
// Script API injection helpers
// -----------------------------------------------------------------------------

// 建立 Lua inheritance:  instance_table 的 metatable.__index 指向 parent_table (blueprint for this component type)
// s.t. instance 可继承蓝图上的 default properties 和 functions, 同时保留自己的 (e.g. self.key).
void EstablishInheritance(luabridge::LuaRef &instance_table,
                          luabridge::LuaRef &parent_table);

// -----------------------------------------------------------------------------
// Component type loading
// -----------------------------------------------------------------------------

void LoadComponentTypes();

// Apply scene/template/runtime overrides after construction but before any
// lifecycle method runs.
void ApplyPropertyOverrides(
    luabridge::LuaRef &instance_table,
    const std::vector<Actor::ComponentProperty> &property_overrides);

// -----------------------------------------------------------------------------
// Component / actor indexing helpers
// -----------------------------------------------------------------------------

ComponentRecord *FindComponentRecord(int actor_id,
                                     const std::string &component_key);
ComponentRecord *FindPrimaryComponentByType(int actor_id,
                                            const std::string &type_name);
void RebuildComponentIndexForActor(int actor_id);
void RebuildTypeIndexForActor(int actor_id);
bool CompareLifecycleComponentRef(const LifecycleComponentRef &a,
                                  const LifecycleComponentRef &b);
void RebuildLifecycleListsForDirtyActors( const std::unordered_set<int> &dirty_actor_ids);
bool IsComponentEnabled(const ComponentRecord &component);
void SyncBuiltinParticleSystemState(ComponentRecord &component);
// Keep component order deterministic for all key-based lifecycle rules.
void SortComponentsForActor(int actor_id);
void RemoveActorFromNameIndex(Actor *actor_ptr);

bool AreSameLuaRef(const luabridge::LuaRef &a, const luabridge::LuaRef &b);
luabridge::LuaRef MakeNilRef();
luabridge::LuaRef MakeEmptyArrayTable();
bool TryExtractComponentIdentity(const luabridge::LuaRef &component_ref,
                                 int &actor_id, std::string &component_key);
bool IsComponentRefAlive(const luabridge::LuaRef &component_ref, int actor_id,
                         const std::string &component_key);

// -----------------------------------------------------------------------------
// Error handling and lifecycle dispatch helpers
// -----------------------------------------------------------------------------

std::string GetActorNameByID(int actor_id);
void ReportError(const std::string &actor_name,
                 const luabridge::LuaException &e);
void ReportEventBusError(int actor_id, const luabridge::LuaException &e);
void RunComponentOnDestroyIfNeeded(ComponentRecord &component, int actor_id);

} // namespace ManagerDetail

#endif
