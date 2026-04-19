#include "Internal.h"
#include "core/Engine.h"
#include "scripting/ComponentManager.h"
#include "physics/Rigidbody.h"
#include "scripting/EventBus.h"
#include "scene/Transform.h"
#include "../luaapi/RegistrationDetail.h"
#include <algorithm>
#include <cmath>

using namespace ManagerDetail;
using APIRegistrationDetail::g_engine;

namespace {

struct PendingCollisionEvent {
    int actor_id = -1;
    bool is_enter = true;
    Collision collision;

    PendingCollisionEvent(int actor_id_in, bool is_enter_in,
                          const Collision &collision_in)
        : actor_id(actor_id_in), is_enter(is_enter_in),
          collision(collision_in) {}
};

struct PendingTriggerEvent {
    int actor_id = -1;
    bool is_enter = true;
    Collision collision;

    PendingTriggerEvent(int actor_id_in, bool is_enter_in,
                        const Collision &collision_in)
        : actor_id(actor_id_in), is_enter(is_enter_in),
          collision(collision_in) {}
};

ComponentRecord *FindRuntimeRigidbodyComponent(int actor_id) {
    ComponentRecord *rigidbody_component = FindComponentRecord(actor_id, "Rigidbody");
    if (rigidbody_component != nullptr) return rigidbody_component;

    auto type_it = g_runtime.component_first_key_by_type.find(actor_id);
    if (type_it == g_runtime.component_first_key_by_type.end()) return nullptr;
    auto rigidbody_key_it = type_it->second.find("Rigidbody");
    if (rigidbody_key_it == type_it->second.end()) return nullptr;
    return FindComponentRecord(actor_id, rigidbody_key_it->second);
}

ComponentRecord *FindRuntimeTransformComponent(int actor_id) {
    auto type_it = g_runtime.component_first_key_by_type.find(actor_id);
    if (type_it == g_runtime.component_first_key_by_type.end()) return nullptr;
    auto transform_key_it = type_it->second.find("Transform");
    if (transform_key_it == type_it->second.end()) return nullptr;
    return FindComponentRecord(actor_id, transform_key_it->second);
}

Actor *FindRuntimeActor(int actor_id) {
    auto actor_it = g_runtime.actor_by_id.find(actor_id);
    if (actor_it == g_runtime.actor_by_id.end()) return nullptr;
    return actor_it->second;
}

bool TryCastRuntimeRigidbody(ComponentRecord *component, Rigidbody *&out_rigidbody) {
    out_rigidbody = nullptr;
    if (component == nullptr || component->removed) return false;
    try {
        out_rigidbody = component->instance_table.cast<Rigidbody *>();
    } catch (const luabridge::LuaException &) {
        lua_settop(g_runtime.lua_state, 0);
        return false;
    }
    return out_rigidbody != nullptr;
}

bool TryCastRuntimeTransform(ComponentRecord *component, Transform *&out_transform) {
    out_transform = nullptr;
    if (component == nullptr || component->removed) return false;
    try {
        out_transform = component->instance_table.cast<Transform *>();
    } catch (const luabridge::LuaException &) {
        lua_settop(g_runtime.lua_state, 0);
        return false;
    }
    return out_transform != nullptr;
}

void RotateClockwiseLocal(float x, float y, float rotation_degrees,
                          float &out_x, float &out_y) {
    const float radians =
        rotation_degrees * (3.14159265358979323846f / 180.0f);
    const float cos_theta = std::cos(radians);
    const float sin_theta = std::sin(radians);
    out_x = cos_theta * x + sin_theta * y;
    out_y = -sin_theta * x + cos_theta * y;
}

void ResolveLocalTransformFromWorld(Actor *actor, float world_x, float world_y,
                                    float world_rotation, float &out_local_x,
                                    float &out_local_y,
                                    float &out_local_rotation) {
    out_local_x = world_x;
    out_local_y = world_y;
    out_local_rotation = world_rotation;
    if (actor == nullptr || actor->parent_id < 0 || actor->parent_id == actor->id) {
        return;
    }

    ComponentRecord *parent_transform_component = FindRuntimeTransformComponent(actor->parent_id);
    Transform *parent_transform = nullptr;
    if (!TryCastRuntimeTransform(parent_transform_component, parent_transform) ||
        parent_transform == nullptr) {
        return;
    }

    const float relative_world_x = world_x - parent_transform->world_x;
    const float relative_world_y = world_y - parent_transform->world_y;
    RotateClockwiseLocal(relative_world_x, relative_world_y,
                         -parent_transform->world_rotation, out_local_x,
                         out_local_y);
    out_local_rotation = world_rotation - parent_transform->world_rotation;
}

void DispatchCollisionEventToActor(const PendingCollisionEvent &event) {
    auto actor_it = g_runtime.actor_components.find(event.actor_id);
    if (actor_it == g_runtime.actor_components.end()) return;

    // Components are already stored in key order, so iterating this vector
    // preserves the required OnCollisionEnter/Exit dispatch order.
    for (const std::unique_ptr<ComponentRecord> &component_ptr :
         actor_it->second) {
        ComponentRecord &component = *component_ptr;
        if (!component.on_start_called) continue;
        if (!IsComponentEnabled(component)) continue;

        const bool should_call = event.is_enter ? component.has_on_collision_enter
                                                : component.has_on_collision_exit;
        if (!should_call) continue;

        try {
            if (event.is_enter) {
                component.instance_table["OnCollisionEnter"](component.instance_table,
                                                             event.collision);
            } else {
                component.instance_table["OnCollisionExit"](component.instance_table,
                                                            event.collision);
            }
        } catch (const luabridge::LuaException &e) {
            ReportError(GetActorNameByID(event.actor_id), e);
            lua_settop(g_runtime.lua_state, 0);
        }
    }
}

void DispatchTriggerEventToActor(const PendingTriggerEvent &event) {
    auto actor_it = g_runtime.actor_components.find(event.actor_id);
    if (actor_it == g_runtime.actor_components.end()) return;

    for (const std::unique_ptr<ComponentRecord> &component_ptr :
         actor_it->second) {
        ComponentRecord &component = *component_ptr;
        if (!component.on_start_called) continue;
        if (!IsComponentEnabled(component)) continue;

        const bool should_call =
            event.is_enter ? component.has_on_trigger_enter
                           : component.has_on_trigger_exit;
        if (!should_call) continue;

        try {
            if (event.is_enter) {
                component.instance_table["OnTriggerEnter"](component.instance_table,
                                                           event.collision);
            } else {
                component.instance_table["OnTriggerExit"](component.instance_table,
                                                          event.collision);
            }
        } catch (const luabridge::LuaException &e) {
            ReportError(GetActorNameByID(event.actor_id), e);
            lua_settop(g_runtime.lua_state, 0);
        }
    }
}

} // namespace

// -----------------------------------------------------------------------------
// ComponentManager public API: per-frame lifecycle
// -----------------------------------------------------------------------------

void ComponentManager::ApplyEffectiveRigidbodyBodyTypes() {
    if (g_engine == nullptr) return;

    for (const auto &actor_components_entry : g_runtime.actor_components) {
        const int actor_id = actor_components_entry.first;
        ComponentRecord *rigidbody_component = FindRuntimeRigidbodyComponent(actor_id);
        Rigidbody *rigidbody = nullptr;
        if (!TryCastRuntimeRigidbody(rigidbody_component, rigidbody) ||
            rigidbody == nullptr) {
            continue;
        }

        const PhysicsHierarchy::State physics_state =
            g_engine->GetRuntimePhysicsHierarchyStateByID(actor_id);
        const std::string effective_body_type =
            physics_state.has_rigidbody_self
                ? physics_state.effective_body_type
                : rigidbody->body_type;
        /*
        Runtime rigidbodies keep both authored and effective body types. This
        bridge copies the hierarchy-derived decision into the live instance
        before OnStart / physics step consume it.
        */
        rigidbody->SetEffectiveBodyType(effective_body_type);
    }
}

// per-frame lifecycle entry points
void ComponentManager::ProcessPendingOnStart() {
    if (g_runtime.pending_on_start.empty()) return;

    // 关键点：先把队列 move 到局部变量
    // 这样 OnStart 里新增的组件会留在 g_runtime.pending_on_start
    // 自动延后到下一帧处理
    std::vector<PendingOnStartRecord> pending_records = std::move(g_runtime.pending_on_start);
    g_runtime.pending_on_start.clear();

    for (const PendingOnStartRecord &pending : pending_records) {
        ComponentRecord *component = FindComponentRecord(pending.actor_id, pending.component_key);
        if (component == nullptr) continue;
        if (component->on_start_called) continue;

        if (component->has_on_start && IsComponentEnabled(*component)) {
            try {
                // Pass self explicitly: ref["OnStart"](ref)
                component->instance_table["OnStart"](component->instance_table);
            } catch (const luabridge::LuaException &e) {
                ReportError(GetActorNameByID(pending.actor_id), e);
                // LuaBridge exception path may leave values on the Lua stack.
                // Reset stack so later calls/shutdown always see a clean state.
                lua_settop(g_runtime.lua_state, 0);
            }
        }
        // OnStart 只尝试一次 (即便当时 disabled 也记为已尝试)
        component->on_start_called = true;
    }
}

// per-frame lifecycle entry points
void ComponentManager::ProcessOnUpdate() {
    // 只遍历“声明了 OnUpdate 的组件”，不再全量扫 + isFunction()
    const size_t update_component_count = g_runtime.on_update_components.size();
    for (size_t i = 0; i < update_component_count; i++) {
        const LifecycleComponentRef &entry = g_runtime.on_update_components[i];
        ComponentRecord *component = entry.component;
        if (component == nullptr) continue;
        SyncBuiltinParticleSystemState(*component);
        if (!component->on_start_called) continue;
        if (!IsComponentEnabled(*component)) continue;

        try {
            component->instance_table["OnUpdate"](component->instance_table);
        } catch (const luabridge::LuaException &e) {
            ReportError(GetActorNameByID(entry.actor_id), e);
            // Keep Lua stack balanced after script errors.
            lua_settop(g_runtime.lua_state, 0);
        }
    }
}

// per-frame lifecycle entry points
void ComponentManager::ProcessOnLateUpdate() {
    // 只遍历“声明了 OnLateUpdate 的组件”，不再全量扫 + isFunction()
    const size_t late_update_component_count = g_runtime.on_late_update_components.size();
    for (size_t i = 0; i < late_update_component_count; i++) {
        const LifecycleComponentRef &entry = g_runtime.on_late_update_components[i];
        ComponentRecord *component = entry.component;
        if (component == nullptr) continue;
        if (!component->on_start_called) continue;
        if (!IsComponentEnabled(*component)) continue;

        try {
            component->instance_table["OnLateUpdate"](component->instance_table);
        } catch (const luabridge::LuaException &e) {
            ReportError(GetActorNameByID(entry.actor_id), e);
            // Keep Lua stack balanced after script errors.
            lua_settop(g_runtime.lua_state, 0);
        }
    }
}

// per-frame lifecycle entry points
void ComponentManager::StepPhysics() {
    // Subscribe/Unsubscribe become visible at the very end of the frame,
    // after LateUpdate and before the physics step starts.
    EventBus::ApplyPendingOperations();

    if (!Rigidbody::HasPhysicsWorld()) return;

    if (g_engine != nullptr) {
        ComponentManager::ResolveTransformHierarchy();
        for (const auto &actor_components_entry : g_runtime.actor_components) {
            const int actor_id = actor_components_entry.first;
            ComponentRecord *rigidbody_component = FindRuntimeRigidbodyComponent(actor_id);
            Rigidbody *rigidbody = nullptr;
            if (!TryCastRuntimeRigidbody(rigidbody_component, rigidbody) ||
                rigidbody == nullptr) {
                continue;
            }

            const PhysicsHierarchy::State physics_state =
                g_engine->GetRuntimePhysicsHierarchyStateByID(actor_id);
            if (!physics_state.has_rigidbody_self ||
                physics_state.effective_body_type == "dynamic") {
                continue;
            }

            ComponentRecord *transform_component = FindRuntimeTransformComponent(actor_id);
            Transform *transform = nullptr;
            if (!TryCastRuntimeTransform(transform_component, transform) ||
                transform == nullptr) {
                continue;
            }

            /*
            Non-dynamic rigidbodies follow hierarchy/world-transform state into
            Box2D, rather than competing with physics as a second motion owner.
            */
            rigidbody->SetPosition(b2Vec2(transform->world_x,
                                          transform->world_y));
            rigidbody->SetRotation(transform->world_rotation);
        }
    }

    Rigidbody::StepPhysicsWorld();

    for (const auto &actor_components_entry : g_runtime.actor_components) {
        const int actor_id = actor_components_entry.first;
        ComponentRecord *rigidbody_component = FindRuntimeRigidbodyComponent(actor_id);
        Rigidbody *rigidbody = nullptr;
        if (!TryCastRuntimeRigidbody(rigidbody_component, rigidbody) ||
            rigidbody == nullptr) {
            continue;
        }

        ComponentRecord *transform_component = FindRuntimeTransformComponent(actor_id);
        Transform *transform = nullptr;
        if (!TryCastRuntimeTransform(transform_component, transform) ||
            transform == nullptr) {
            continue;
        }

        const PhysicsHierarchy::State physics_state =
            (g_engine != nullptr)
                ? g_engine->GetRuntimePhysicsHierarchyStateByID(actor_id)
                : PhysicsHierarchy::State{};
        if (!physics_state.has_rigidbody_self ||
            physics_state.effective_body_type != "dynamic") {
            continue;
        }

        const b2Vec2 position = rigidbody->GetPosition();
        const float rotation = rigidbody->GetRotation();
        // Keep the Lua-facing Rigidbody fields in sync with the Box2D body so
        // inspector/runtime reads reflect physics-driven motion immediately.
        rigidbody_component->instance_table["x"] = position.x;
        rigidbody_component->instance_table["y"] = position.y;
        rigidbody_component->instance_table["rotation"] = rotation;
        Actor *actor = FindRuntimeActor(actor_id);
        float local_x = position.x;
        float local_y = position.y;
        float local_rotation = rotation;
        /*
        Dynamic roots run in the opposite direction: physics owns the world
        pose, so we convert that world pose back into local Transform under the
        current parent chain after the Box2D step.
        */
        ResolveLocalTransformFromWorld(actor, position.x, position.y, rotation,
                                       local_x, local_y, local_rotation);
        transform_component->instance_table["x"] = local_x;
        transform_component->instance_table["y"] = local_y;
        transform_component->instance_table["rotation"] = local_rotation;
    }
}

// per-frame lifecycle entry points
void ComponentManager::FinalizePrePhysicsDestructions() {
    while (true) {
        std::unordered_set<int> dirty_actor_ids = g_runtime.pending_destroy_actor_ids;
        for (int actor_id : g_runtime.dirty_component_actor_ids) {
            if (dirty_actor_ids.find(actor_id) != dirty_actor_ids.end()) continue;

            auto actor_it = g_runtime.actor_components.find(actor_id);
            if (actor_it == g_runtime.actor_components.end()) continue;

            bool has_removed_components = false;
            for (const std::unique_ptr<ComponentRecord> &component_ptr :
                 actor_it->second) {
                if (!component_ptr->removed) continue;
                has_removed_components = true;
                break;
            }
            if (has_removed_components) {
                dirty_actor_ids.insert(actor_id);
            }
        }
        const std::unordered_set<int> destroyed_actor_ids = g_runtime.pending_destroy_actor_ids;

        if (dirty_actor_ids.empty() && destroyed_actor_ids.empty()) {
            return;
        }

        for (int actor_id : dirty_actor_ids) {
            auto actor_it = g_runtime.actor_components.find(actor_id);
            if (actor_it == g_runtime.actor_components.end()) continue;

            std::vector<std::unique_ptr<ComponentRecord>> &components = actor_it->second;
            for (std::unique_ptr<ComponentRecord> &component_ptr : components) {
                if (!component_ptr->removed) continue;
                RunComponentOnDestroyIfNeeded(*component_ptr, actor_id);
            }

            /*
            Component teardown is batched here so physics, lifecycle dispatch,
            and lookup indices all see one stable runtime view during update.
            */
            components.erase(
                std::remove_if(
                    components.begin(), components.end(),
                    [](const std::unique_ptr<ComponentRecord> &component) {
                        return component->removed;
                    }),
                components.end());
            RebuildComponentIndexForActor(actor_id);
            RebuildTypeIndexForActor(actor_id);
        }

        RebuildLifecycleListsForDirtyActors(dirty_actor_ids);

        if (!destroyed_actor_ids.empty()) {
            g_runtime.pending_on_start.erase(
                std::remove_if(
                    g_runtime.pending_on_start.begin(),
                    g_runtime.pending_on_start.end(),
                    [&](const PendingOnStartRecord &pending) {
                        return destroyed_actor_ids.find(pending.actor_id) != destroyed_actor_ids.end();
                    }),
                g_runtime.pending_on_start.end());

            g_runtime.pending_actor_ids_to_activate.erase(
                std::remove_if(
                    g_runtime.pending_actor_ids_to_activate.begin(),
                    g_runtime.pending_actor_ids_to_activate.end(),
                    [&](int actor_id) {
                        return destroyed_actor_ids.find(actor_id) != destroyed_actor_ids.end();
                    }),
                g_runtime.pending_actor_ids_to_activate.end());
        }

        for (int actor_id : destroyed_actor_ids) {
            auto actor_ptr_it = g_runtime.actor_by_id.find(actor_id);
            Actor *actor_ptr =
                (actor_ptr_it == g_runtime.actor_by_id.end()) ? nullptr
                                                              : actor_ptr_it->second;
            if (actor_ptr != nullptr) {
                actor_ptr->runtime_destroyed = true;
            }
            g_runtime.actor_by_id.erase(actor_id);
            g_runtime.actor_components.erase(actor_id);
            g_runtime.component_index_by_key.erase(actor_id);
            g_runtime.component_first_key_by_type.erase(actor_id);
            g_runtime.component_keys_by_type.erase(actor_id);
            g_runtime.actor_order_by_id.erase(actor_id);
            g_runtime.actor_ids_sorted.erase(
                std::remove(g_runtime.actor_ids_sorted.begin(),
                            g_runtime.actor_ids_sorted.end(), actor_id),
                g_runtime.actor_ids_sorted.end());
        }

        for (int actor_id : dirty_actor_ids) {
            g_runtime.dirty_component_actor_ids.erase(actor_id);
        }
        for (int actor_id : destroyed_actor_ids) {
            g_runtime.pending_destroy_actor_ids.erase(actor_id);
        }
    }
}

// immediate physics event dispatch helpers
void ComponentManager::QueueCollisionEvent(Actor *actor, Actor *other, const b2Vec2 &point,
                                     const b2Vec2 &relative_velocity,
                                     const b2Vec2 &normal, bool is_enter) {
    if (actor == nullptr || other == nullptr) return;

    auto actor_it = g_runtime.actor_by_id.find(actor->id);
    if (actor_it == g_runtime.actor_by_id.end() || actor_it->second == nullptr) {
        return;
    }

    Collision collision;
    collision.other = other;
    collision.point = point;
    collision.relative_velocity = relative_velocity;
    collision.normal = normal;
    DispatchCollisionEventToActor(
        PendingCollisionEvent(actor->id, is_enter, collision));
}

// immediate physics event dispatch helpers
void ComponentManager::QueueTriggerEvent(Actor *actor, Actor *other, const b2Vec2 &point,
                                   const b2Vec2 &relative_velocity,
                                   const b2Vec2 &normal, bool is_enter) {
    if (actor == nullptr || other == nullptr) return;

    auto actor_it = g_runtime.actor_by_id.find(actor->id);
    if (actor_it == g_runtime.actor_by_id.end() || actor_it->second == nullptr) {
        return;
    }

    Collision collision;
    collision.other = other;
    collision.point = point;
    collision.relative_velocity = relative_velocity;
    collision.normal = normal;
    DispatchTriggerEventToActor(
        PendingTriggerEvent(actor->id, is_enter, collision));
}

// per-frame lifecycle entry points
void ComponentManager::FinalizeFrameMutations() {
    // 帧末统一收口 runtime mutation:
    // 1) 激活本帧新建 actor (下一帧开始参与生命周期)
    // 2) 清理 removed 组件
    // 3) 处理 Actor.Destroy 的最终移除
    if (g_runtime.pending_actor_ids_to_activate.empty() &&
        g_runtime.dirty_component_actor_ids.empty() &&
        g_runtime.pending_destroy_actor_ids.empty()) {
        return;
    }

    std::unordered_set<int> dirty_actor_ids = g_runtime.dirty_component_actor_ids;

    for (int actor_id : g_runtime.pending_actor_ids_to_activate) {
        if (g_runtime.pending_destroy_actor_ids.find(actor_id) !=
            g_runtime.pending_destroy_actor_ids.end()) {
            continue;
        }
        if (g_runtime.actor_order_by_id.find(actor_id) ==
            g_runtime.actor_order_by_id.end()) {
            g_runtime.actor_order_by_id[actor_id] = g_runtime.actor_ids_sorted.size();
        }
        auto actor_ptr_it = g_runtime.actor_by_id.find(actor_id);
        if (actor_ptr_it == g_runtime.actor_by_id.end() ||
            actor_ptr_it->second == nullptr) {
            continue;
        }
        // IDs are globally increasing, so runtime-instantiated actors keep order by append.
        g_runtime.actor_ids_sorted.push_back(actor_id);

        // Newly instantiated actors have no existing lifecycle entries yet.
        // Append them directly instead of forcing a full dirty-list rebuild/sort.
        auto actor_components_it = g_runtime.actor_components.find(actor_id);
        if (actor_components_it != g_runtime.actor_components.end()) {
            for (std::unique_ptr<ComponentRecord> &component_ptr :
                 actor_components_it->second) {
                ComponentRecord &component = *component_ptr;
                if (component.removed) continue;
                if (component.has_on_update) {
                    g_runtime.on_update_components.emplace_back(actor_id, &component);
                }
                if (component.has_on_late_update) {
                    g_runtime.on_late_update_components.emplace_back(actor_id,
                                                                     &component);
                }
            }
        }

        dirty_actor_ids.erase(actor_id);
    }
    g_runtime.pending_actor_ids_to_activate.clear();

    // Only touch actors that mutated this frame.
    for (int actor_id : dirty_actor_ids) {
        auto actor_it = g_runtime.actor_components.find(actor_id);
        if (actor_it == g_runtime.actor_components.end()) continue;
        std::vector<std::unique_ptr<ComponentRecord>> &components = actor_it->second;
        for (std::unique_ptr<ComponentRecord> &component_ptr : components) {
            if (!component_ptr->removed) continue;
            RunComponentOnDestroyIfNeeded(*component_ptr, actor_id);
        }
        components.erase(
            std::remove_if(
                components.begin(), components.end(),
                [](const std::unique_ptr<ComponentRecord> &component) {
                    return component->removed;
                }),
            components.end());
        RebuildComponentIndexForActor(actor_id);
        RebuildTypeIndexForActor(actor_id);
    }
    RebuildLifecycleListsForDirtyActors(dirty_actor_ids);
    g_runtime.dirty_component_actor_ids.clear();

    if (!g_runtime.pending_destroy_actor_ids.empty()) {
        g_runtime.pending_on_start.erase(
            std::remove_if(
                g_runtime.pending_on_start.begin(),
                g_runtime.pending_on_start.end(),
                [](const PendingOnStartRecord &pending) {
                    return g_runtime.pending_destroy_actor_ids.find( pending.actor_id) !=
                           g_runtime.pending_destroy_actor_ids.end();
                }),
            g_runtime.pending_on_start.end());
    }

    for (int actor_id : g_runtime.pending_destroy_actor_ids) {
        auto actor_ptr_it = g_runtime.actor_by_id.find(actor_id);
        Actor *actor_ptr = (actor_ptr_it == g_runtime.actor_by_id.end())
                               ? nullptr
                               : actor_ptr_it->second;
        if (actor_ptr != nullptr) {
            actor_ptr->runtime_destroyed = true;
        }
        g_runtime.actor_by_id.erase(actor_id);
        g_runtime.actor_components.erase(actor_id);
        g_runtime.component_index_by_key.erase(actor_id);
        g_runtime.component_first_key_by_type.erase(actor_id);
        g_runtime.component_keys_by_type.erase(actor_id);
        g_runtime.actor_order_by_id.erase(actor_id);
        g_runtime.actor_ids_sorted.erase(
            std::remove(g_runtime.actor_ids_sorted.begin(),
                        g_runtime.actor_ids_sorted.end(), actor_id),
            g_runtime.actor_ids_sorted.end());
    }
    g_runtime.pending_destroy_actor_ids.clear();
}
