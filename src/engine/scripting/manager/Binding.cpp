#include "Internal.h"
#include "core/Engine.h"
#include "rendering/SpriteRenderer.h"
#include "rendering/MeshRenderer.h"
#include "scripting/ComponentManager.h"
#include "particles/ParticleSystem.h"
#include "physics/Rigidbody.h"
#include "scene/Transform.h"
#include "scene/Transform3D.h"
#include "scene/Camera3D.h"
#include "scene/Scene.h"
#include "../luaapi/RegistrationDetail.h"
#include "shared/scene_format/SceneFormat.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <type_traits>

using namespace ManagerDetail;

namespace {

template <typename ValueType>
luabridge::LuaRef BuildLuaArrayTable(const std::vector<ValueType> &values) {
    luabridge::LuaRef table = MakeEmptyArrayTable();
    int lua_index = 1;
    for (const ValueType &element : values) {
        table[lua_index++] = element;
    }
    return table;
}

luabridge::LuaRef BuildLuaBoolArrayTable(const Actor::BoolArray &values) {
    luabridge::LuaRef table = MakeEmptyArrayTable();
    int lua_index = 1;
    for (bool element : values) {
        table[lua_index++] = element;
    }
    return table;
}

} // namespace

namespace ManagerDetail {

bool AssignPropertyValueToLuaField(luabridge::LuaRef &instance_table,
                                   const std::string &property_name,
                                   const Actor::ComponentPropertyValue &value) {
    std::visit(
        [&](const auto &typed_value) {
            using ValueType = std::decay_t<decltype(typed_value)>;
            if constexpr (std::is_same_v<ValueType, Actor::BoolArray>) {
                instance_table[property_name] =
                    BuildLuaBoolArrayTable(typed_value);
            } else if constexpr (std::is_same_v<ValueType, Actor::IntArray> ||
                                 std::is_same_v<ValueType, Actor::DoubleArray> ||
                                 std::is_same_v<ValueType, Actor::StringArray>) {
                instance_table[property_name] =
                    BuildLuaArrayTable(typed_value);
            } else {
                instance_table[property_name] = typed_value;
            }
        },
        value);
    return true;
}

} // namespace ManagerDetail

namespace {

void RotateClockwise(float x, float y, float rotation_degrees, float &out_x,
                     float &out_y) {
    const float radians = rotation_degrees * (3.14159265358979323846f / 180.0f);
    const float cos_theta = std::cos(radians);
    const float sin_theta = std::sin(radians);
    out_x = cos_theta * x + sin_theta * y;
    out_y = -sin_theta * x + cos_theta * y;
}

void ResolveRuntimeLocalTransformFromWorld(
    Actor::UID actor_uid, float world_x, float world_y, float world_rotation,
    float &out_local_x, float &out_local_y, float &out_local_rotation) {
    out_local_x = world_x;
    out_local_y = world_y;
    out_local_rotation = world_rotation;

    auto actor_it = g_runtime.actor_by_uid.find(actor_uid);
    if (actor_it == g_runtime.actor_by_uid.end() || actor_it->second == nullptr) {
        return;
    }

    Actor *actor = actor_it->second;
    if (actor->parent_uid == Actor::kInvalidUID || actor->parent_uid == actor_uid) {
        return;
    }

    /*
    Runtime transform editing speaks in world space for some call sites. If the
    actor has a parent, convert the requested world pose back into local space
    before writing into the built-in Transform component.
    */
    float parent_world_x = 0.0f;
    float parent_world_y = 0.0f;
    float parent_world_rotation = 0.0f;
    if (!ComponentManager::TryGetRuntimeTransformWorld(
            actor->parent_uid, parent_world_x, parent_world_y,
            parent_world_rotation, nullptr)) {
        return;
    }

    const float relative_world_x = world_x - parent_world_x;
    const float relative_world_y = world_y - parent_world_y;
    RotateClockwise(relative_world_x, relative_world_y, -parent_world_rotation,
                    out_local_x, out_local_y);
    out_local_rotation = world_rotation - parent_world_rotation;
}

void SyncTransformAndRigidbodyAfterPropertyEdit(
    Actor::UID actor_uid, ComponentRecord &edited_component,
    const std::string &property_name,
    const Actor::ComponentPropertyValue &) {
    if (property_name != "x" && property_name != "y" &&
        property_name != "rotation") {
        return;
    }

    if (edited_component.type == "Transform") {
        /*
        Transform edits must immediately push a matching pose into Rigidbody so
        rendering and physics do not disagree for one frame in editor/runtime
        inspection paths.
        */
        ComponentRecord *rigidbody_component =
            FindPrimaryComponentByType(actor_uid, "Rigidbody");
        if (rigidbody_component == nullptr) return;

        Transform *transform = nullptr;
        Rigidbody *rigidbody = nullptr;
        try {
            transform = edited_component.instance_table.cast<Transform *>();
            rigidbody = rigidbody_component->instance_table.cast<Rigidbody *>();
        } catch (const luabridge::LuaException &) {
            lua_settop(g_runtime.lua_state, 0);
            return;
        }
        if (transform == nullptr || rigidbody == nullptr) return;

        float world_x = transform->x;
        float world_y = transform->y;
        float world_rotation = transform->rotation;
        if (ComponentManager::TryGetRuntimeTransformWorld(
                actor_uid, world_x, world_y, world_rotation, nullptr)) {
            rigidbody->SetPosition(b2Vec2(world_x, world_y));
            rigidbody->SetRotation(world_rotation);
            return;
        }

        rigidbody->SetPosition(b2Vec2(transform->x, transform->y));
        rigidbody->SetRotation(transform->rotation);
        return;
    }

    if (edited_component.type != "Rigidbody") return;

    /*
    The reverse direction matters too: when Rigidbody properties are edited
    directly, keep the built-in Transform aligned so hierarchy/world resolve
    continues to see one coherent pose.
    */
    ComponentRecord *transform_component =
        FindPrimaryComponentByType(actor_uid, "Transform");
    if (transform_component == nullptr) return;

    Transform *transform = nullptr;
    Rigidbody *rigidbody = nullptr;
    try {
        transform = transform_component->instance_table.cast<Transform *>();
        rigidbody = edited_component.instance_table.cast<Rigidbody *>();
    } catch (const luabridge::LuaException &) {
        lua_settop(g_runtime.lua_state, 0);
        return;
    }
    if (transform == nullptr || rigidbody == nullptr) return;

    float local_x = rigidbody->x;
    float local_y = rigidbody->y;
    float local_rotation = rigidbody->rotation;
    ResolveRuntimeLocalTransformFromWorld(actor_uid, rigidbody->x,
                                          rigidbody->y,
                                          rigidbody->rotation, local_x, local_y,
                                          local_rotation);
    transform->x = local_x;
    transform->y = local_y;
    transform->rotation = local_rotation;
}

} // namespace

// rebind actor pointers after a scene container changes
void ComponentManager::BindActorsForScene(std::deque<Actor> &actors) {
    // Bind after scene vector is finalized, so pointers injected into Lua
    // refer to stable actor objects for this scene lifetime.
    g_runtime.scene_actors = &actors;
    g_runtime.actor_by_uid.clear();
    g_runtime.actors_by_name.clear();
    g_runtime.actor_order_by_uid.clear();
    g_runtime.actor_uids_sorted.clear();
    g_runtime.component_index_by_key.clear();
    g_runtime.component_first_key_by_type.clear();
    g_runtime.component_keys_by_type.clear();
    g_runtime.on_update_components.clear();
    g_runtime.on_late_update_components.clear();
    g_runtime.dirty_component_actor_uids.clear();
    g_runtime.actor_by_uid.reserve(actors.size());
    g_runtime.actor_order_by_uid.reserve(actors.size());
    g_runtime.actor_uids_sorted.reserve(actors.size());

    for (Actor &actor : actors) {
        if (actor.runtime_destroyed) continue;
        const size_t actor_order = g_runtime.actor_uids_sorted.size();
        g_runtime.actor_by_uid[actor.uid] = &actor;
        g_runtime.actors_by_name[actor.actor_name].push_back(&actor);
        g_runtime.actor_order_by_uid[actor.uid] = actor_order;
        g_runtime.actor_uids_sorted.push_back(actor.uid);
        SortComponentsForActor(actor.uid);
        auto component_it = g_runtime.actor_components.find(actor.uid);
        if (component_it == g_runtime.actor_components.end()) continue;
        for (std::unique_ptr<ComponentRecord> &component_ptr :
             component_it->second) {
            ComponentRecord &component = *component_ptr;
            // Flexible reference: each component can reach its owner actor.
            component.instance_table["actor"] = &actor;
            if (component.removed) continue;
            if (component.has_on_update) {
                g_runtime.on_update_components.emplace_back(actor.uid,
                                                           &component);
            }
            if (component.has_on_late_update) {
                g_runtime.on_late_update_components.emplace_back(actor.uid,
                                                                 &component);
            }
        }
    }
    std::sort(g_runtime.on_update_components.begin(),
              g_runtime.on_update_components.end(),
              CompareLifecycleComponentRef);
    std::sort(g_runtime.on_late_update_components.begin(),
              g_runtime.on_late_update_components.end(),
              CompareLifecycleComponentRef);
}

// -----------------------------------------------------------------------------
// ComponentManager public API: component / actor creation
// -----------------------------------------------------------------------------

// instantiate one component from parsed scene / template spec
void ComponentManager::InstantiateComponentForActor(
    Actor::UID actor_uid, const Actor::ComponentSpec &component_spec) {
    luabridge::LuaRef instance_table(g_runtime.lua_state);
    if (IsBuiltinComponentType(component_spec.type)) {
        if (component_spec.type == "Rigidbody") {
            instance_table = luabridge::LuaRef(g_runtime.lua_state, Rigidbody());
        } else if (component_spec.type == "ParticleSystem") {
            instance_table = luabridge::LuaRef(g_runtime.lua_state, ParticleSystem());
        } else if (component_spec.type == "Transform") {
            instance_table = luabridge::LuaRef(g_runtime.lua_state, Transform());
        } else if (component_spec.type == "SpriteRenderer") {
            instance_table = luabridge::LuaRef(g_runtime.lua_state, SpriteRenderer());
        } else if (component_spec.type == "Transform3D") {
            instance_table = luabridge::LuaRef(g_runtime.lua_state, Transform3D());
        } else if (component_spec.type == "Camera3D") {
            instance_table = luabridge::LuaRef(g_runtime.lua_state, Camera3D());
        } else if (component_spec.type == "MeshRenderer") {
            instance_table = luabridge::LuaRef(g_runtime.lua_state, MeshRenderer());
        }
    } else {
        auto type_it = g_runtime.component_type_tables.find(component_spec.type);
        if (type_it == g_runtime.component_type_tables.end()) {
            std::cout << "warning: failed to locate component "
                      << component_spec.type << "; skipping component"
                      << std::endl;
            return;
        }

        // 创建实例 table + inherit + 注入系统字段
        instance_table = luabridge::newTable(g_runtime.lua_state);
        luabridge::LuaRef parent_table = type_it->second;
        EstablishInheritance(instance_table, parent_table);
    }

    // Requirement: every component can read self.key in Lua.
    instance_table["key"] = component_spec.key;
    // Requirement: all components start enabled.
    instance_table["enabled"] = true;
    auto actor_ptr_it = g_runtime.actor_by_uid.find(actor_uid);
    if (actor_ptr_it != g_runtime.actor_by_uid.end()) {
        instance_table["actor"] = actor_ptr_it->second;
    }
    ApplyPropertyOverrides(instance_table, component_spec.overrides);

    // 生命周期函数拥有关系在实例化时一次性缓存，避免每帧 isFunction() 开销
    const bool has_on_start = instance_table["OnStart"].isFunction();
    const bool has_on_destroy = instance_table["OnDestroy"].isFunction();
    const bool has_on_update = instance_table["OnUpdate"].isFunction();
    const bool has_on_late_update = instance_table["OnLateUpdate"].isFunction();
    const bool has_on_collision_enter = instance_table["OnCollisionEnter"].isFunction();
    const bool has_on_collision_exit = instance_table["OnCollisionExit"].isFunction();
    const bool has_on_trigger_enter = instance_table["OnTriggerEnter"].isFunction();
    const bool has_on_trigger_exit = instance_table["OnTriggerExit"].isFunction();

    auto actor_it = g_runtime.actor_components.try_emplace(actor_uid).first;
    actor_it->second.emplace_back(std::make_unique<ComponentRecord>(
        component_spec.key, component_spec.type, std::move(instance_table),
        has_on_start, has_on_destroy, has_on_update, has_on_late_update,
        has_on_collision_enter, has_on_collision_exit, has_on_trigger_enter,
        has_on_trigger_exit));
    // 排队到下一帧执行 OnStart 的 list
    g_runtime.pending_on_start.emplace_back(actor_uid, component_spec.key);

    // 运行时增删改由帧末统一重建生命周期列表
    if (actor_ptr_it != g_runtime.actor_by_uid.end()) {
        g_runtime.dirty_component_actor_uids.insert(actor_uid);
    }
}

// runtime component add / remove
luabridge::LuaRef ComponentManager::AddComponent(Actor::UID actor_uid,
                                                 const std::string &type_name) {
    // AddComponent 的语义: 立即创建并返回 ref, 但生命周期从下一帧开始.
    if (g_runtime.lua_state == nullptr) return MakeNilRef();
    auto actor_ptr_it = g_runtime.actor_by_uid.find(actor_uid);
    if (actor_ptr_it == g_runtime.actor_by_uid.end() ||
        actor_ptr_it->second == nullptr) {
        return MakeNilRef();
    }
    if (g_runtime.pending_destroy_actor_uids.find(actor_uid) !=
        g_runtime.pending_destroy_actor_uids.end()) {
        return MakeNilRef();
    }

    Actor::ComponentSpec component_spec;
    component_spec.type = type_name;
    // 运行时组件 key: r<n>
    component_spec.key = "r" + std::to_string(g_runtime.runtime_add_component_counter++);
    InstantiateComponentForActor(actor_uid, component_spec);
    SortComponentsForActor(actor_uid);

    ComponentRecord *created = FindComponentRecord(actor_uid,
                                                   component_spec.key);
    if (created == nullptr) return MakeNilRef();
    return created->instance_table;
}

// runtime component add / remove
void ComponentManager::RemoveComponent(Actor::UID actor_uid,
                                       luabridge::LuaRef component_ref) {
    // 立即逻辑移除: 标记 removed + enabled=false
    // 物理清理由 FinalizeFrameMutations 在帧末统一做
    auto actor_it = g_runtime.actor_components.find(actor_uid);
    if (actor_it == g_runtime.actor_components.end()) return;

    ComponentRecord *target = nullptr;
    if (component_ref.isTable()) {
        luabridge::LuaRef key_ref = component_ref["key"];
        if (key_ref.isString()) {
            const std::string key = key_ref.cast<std::string>();
            target = FindComponentRecord(actor_uid, key);
        }
    }

    if (target == nullptr) {
        for (std::unique_ptr<ComponentRecord> &component_ptr :
             actor_it->second) {
            ComponentRecord &component = *component_ptr;
            if (component.removed) continue;
            if (AreSameLuaRef(component.instance_table, component_ref)) {
                target = &component;
                break;
            }
        }
    }

    if (target == nullptr) return;
    target->removed = true;
    target->instance_table["enabled"] = false;
    auto index_it = g_runtime.component_index_by_key.find(actor_uid);
    if (index_it != g_runtime.component_index_by_key.end()) {
        index_it->second.erase(target->key);
    }
    RebuildTypeIndexForActor(actor_uid);
    g_runtime.dirty_component_actor_uids.insert(actor_uid);
}

// runtime component add / remove
bool ComponentManager::RenameComponentKey(Actor::UID actor_uid,
                                          const std::string &old_key,
                                          const std::string &new_key) {
    if (old_key.empty() || new_key.empty()) return false;
    if (old_key == new_key) return false;
    if (FindComponentRecord(actor_uid, new_key) != nullptr) return false;

    ComponentRecord *component = FindComponentRecord(actor_uid, old_key);
    if (component == nullptr) return false;

    try {
        component->instance_table["key"] = new_key;
    } catch (const luabridge::LuaException &) {
        lua_settop(g_runtime.lua_state, 0);
        return false;
    }

    component->key = new_key;
    SortComponentsForActor(actor_uid);
    RebuildLifecycleListsForDirtyActors(
        std::unordered_set<Actor::UID>{actor_uid});
    return true;
}

// runtime component add / remove
bool ComponentManager::SetComponentPropertyValue(
    Actor::UID actor_uid, const std::string &component_key,
    const std::string &property_name,
    const Actor::ComponentPropertyValue &value) {
    if (property_name.empty()) return false;

    ComponentRecord *component = FindComponentRecord(actor_uid, component_key);
    if (component == nullptr) return false;

    try {
        AssignPropertyValueToLuaField(component->instance_table, property_name,
                                      value);

        if (component->type == "ParticleSystem" && property_name == "enabled") {
            ParticleSystem *system = component->instance_table.cast<ParticleSystem *>();
            if (system != nullptr) {
                system->SyncRuntimeState();
            }
        }

        // Built-in Transform and Rigidbody now coexist in authoring/runtime
        // scenes. Keep their pose values aligned immediately so editor edits do
        // not leave rendering and physics looking out of sync for a frame.
        SyncTransformAndRigidbodyAfterPropertyEdit(actor_uid, *component,
                                                   property_name, value);
    } catch (const luabridge::LuaException &) {
        lua_settop(g_runtime.lua_state, 0);
        return false;
    }

    return true;
}

bool ComponentManager::SetRuntimeComponentPropertyValue(
    Actor::UID actor_uid, const std::string &component_key,
    const std::string &property_name,
    const Actor::ComponentPropertyValue &value) {
    if (property_name.empty()) return false;

    auto actor_it = g_runtime.actor_by_uid.find(actor_uid);
    if (actor_it == g_runtime.actor_by_uid.end() || actor_it->second == nullptr) {
        return false;
    }

    Actor *actor = actor_it->second;
    if (actor->runtime_destroyed) return false;

    Actor::ComponentSpec *component_spec =
        SceneFormat::FindComponentSpec(actor->component_specs, component_key);
    if (component_spec == nullptr) return false;

    SceneFormat::UpsertComponentProperty(*component_spec, property_name, value);
    const bool can_patch_in_place =
        !IsBuiltinComponentType(component_spec->type) ||
        component_spec->type == "Transform" ||
        component_spec->type == "SpriteRenderer";
    if (can_patch_in_place &&
        SetComponentPropertyValue(actor_uid, component_key, property_name,
                                  value)) {
        return true;
    }

    /*
    Some built-ins rebuild internal runtime state from their serialized spec.
    For those, mutate the authored spec first, then destroy/recreate the live
    component so the instance is reconstructed from the updated data.
    */
    const luabridge::LuaRef component_ref =
        ComponentManager::GetComponentByKey(actor_uid, component_key);
    if (component_ref.isNil()) return false;

    ComponentManager::RemoveComponent(actor_uid, component_ref);
    ComponentManager::FinalizeFrameMutations();
    ComponentManager::InstantiateComponentForActor(actor_uid, *component_spec);
    if (g_runtime.scene_actors != nullptr) {
        ComponentManager::BindActorsForScene(*g_runtime.scene_actors);
    }
    return true;
}

// actor-level helpers exposed through Actor static APIs
luabridge::LuaRef ComponentManager::InstantiateActor( const std::string &template_name) {
    // Actor.Instantiate 语义：
    // 立即可被 Find / FindAll 找到
    // 组件生命周期从下一帧开始
    using APIRegistrationDetail::g_engine;
    if (g_runtime.lua_state == nullptr || g_runtime.scene_actors == nullptr) {
        return MakeNilRef();
    }
    if (g_engine == nullptr) {
        return MakeNilRef();
    }

    Actor actor = Actor::LoadTemplate(template_name);
    actor.uid = g_engine->AllocateRuntimeGeneratedActorUID();
    actor.scene_backed = false;
    actor.parent_uid = Actor::kInvalidUID;
    actor.runtime_destroyed = false;
    std::sort(actor.component_specs.begin(), actor.component_specs.end(),
              [](const Actor::ComponentSpec &a, const Actor::ComponentSpec &b) {
                  return a.key < b.key;
              });

    g_runtime.scene_actors->emplace_back(std::move(actor));
    Actor *actor_ptr = &g_runtime.scene_actors->back();
    /*
    Instantiated actors are inserted into runtime lookup tables immediately so
    Lua can Find/SetParent them in the same frame, even though lifecycle
    callbacks still begin on the next frame boundary.
    */
    g_runtime.actor_by_uid[actor_ptr->uid] = actor_ptr;
    g_runtime.actors_by_name[actor_ptr->actor_name].push_back(actor_ptr);
    g_runtime.actor_order_by_uid[actor_ptr->uid] =
        g_runtime.scene_actors->empty() ? 0 : (g_runtime.scene_actors->size() - 1);
    g_runtime.pending_actor_uids_to_activate.push_back(actor_ptr->uid);

    for (const Actor::ComponentSpec &component_spec : actor_ptr->component_specs) {
        InstantiateComponentForActor(actor_ptr->uid, component_spec);
    }
    SortComponentsForActor(actor_ptr->uid);
    return luabridge::LuaRef(g_runtime.lua_state, actor_ptr);
}

// actor-level helpers exposed through Actor static APIs
void ComponentManager::DestroyActor(Actor *actor) {
    // Actor.Destroy 语义：
    // 立即禁用其所有组件 (本帧后续生命周期不再执行)
    // 真正从索引移除放到帧末处理
    if (actor == nullptr) return;
    if (actor->runtime_destroyed) return;

    const Actor::UID actor_uid = actor->uid;
    if (g_runtime.actor_by_uid.find(actor_uid) == g_runtime.actor_by_uid.end()) {
        return;
    }
    if (!g_runtime.pending_destroy_actor_uids.insert(actor_uid).second) return;

    // Destroy 之后应当立即无法通过 Actor.Find / FindAll 找到该 actor
    // 真实物理清理仍在帧末统一完成
    RemoveActorFromNameIndex(actor);

    auto components_it = g_runtime.actor_components.find(actor_uid);
    if (components_it != g_runtime.actor_components.end()) {
        for (std::unique_ptr<ComponentRecord> &component_ptr :
             components_it->second) {
            ComponentRecord &component = *component_ptr;
            component.removed = true;
            component.instance_table["enabled"] = false;
        }
        g_runtime.component_index_by_key.erase(actor_uid);
        g_runtime.component_first_key_by_type.erase(actor_uid);
        g_runtime.component_keys_by_type.erase(actor_uid);
        g_runtime.dirty_component_actor_uids.insert(actor_uid);
    }
}

// actor-level helpers exposed through Actor instance APIs
bool ComponentManager::SetActorParent(Actor::UID actor_uid, Actor *parent_actor) {
    using APIRegistrationDetail::g_engine;

    if (g_engine == nullptr) return false;
    const Actor::UID parent_uid =
        (parent_actor == nullptr) ? Actor::kInvalidUID : parent_actor->uid;
    return g_engine->SetRuntimeActorParentByUID(actor_uid, parent_uid);
}
