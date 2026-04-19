#ifndef COMPONENT_MANAGER_H
#define COMPONENT_MANAGER_H

#include "scene/Actor.h"
#include "box2d/box2d.h"
#include <deque>
#include <vector>

class Engine;

class ComponentManager {
public:
    // create the Lua VM, register host APIs, and load component type scripts
    // 创建 Lua VM, 注册宿主 API, 并加载组件类型脚本
    static void Initialize();
    static void ReloadComponentTypes();

    static void Shutdown();

    // clear all runtime component state tied to scene actors
    // 清空与场景 actor 绑定的组件运行时状态
    static void ClearActorComponents();

    // rebind actor pointers after a scene container changes
    // 在场景 actor 容器变化后重新绑定 actor 指针
    static void BindActorsForScene(std::deque<Actor> &actors);
    static void BindEngine(Engine *engine);

    // instantiate one component from parsed scene / template spec
    // 根据解析后的场景或模板数据实例化一个组件
    static void InstantiateComponentForActor( int actor_id, const Actor::ComponentSpec &component_spec);

    // runtime component add / remove
    // 运行时组件增删接口
    static luabridge::LuaRef AddComponent(int actor_id,
                                          const std::string &type_name);
    static void RemoveComponent(int actor_id, luabridge::LuaRef component_ref);
    static bool RenameComponentKey(int actor_id, const std::string &old_key,
                                   const std::string &new_key);
    static bool SetComponentPropertyValue(
        int actor_id, const std::string &component_key,
        const std::string &property_name,
        const Actor::ComponentPropertyValue &value);

    // per-frame lifecycle entry points
    // 每帧生命周期入口
    static void ProcessPendingOnStart();
    static void ApplyEffectiveRigidbodyBodyTypes();
    static void ProcessOnUpdate();
    static void ProcessOnLateUpdate();
    static void FinalizePrePhysicsDestructions();
    static void StepPhysics();
    static void FinalizeFrameMutations();

    // immediate physics event dispatch helpers
    // 立即派发物理事件的辅助接口
    static void QueueCollisionEvent(Actor *actor, Actor *other,
                                    const b2Vec2 &point,
                                    const b2Vec2 &relative_velocity,
                                    const b2Vec2 &normal, bool is_enter);
    static void QueueTriggerEvent(Actor *actor, Actor *other,
                                  const b2Vec2 &point,
                                  const b2Vec2 &relative_velocity,
                                  const b2Vec2 &normal, bool is_enter);

    // component queries used by Actor's Lua-facing methods
    // 给 Actor 的 Lua 接口使用的组件查询
    static luabridge::LuaRef GetComponentByKey(int actor_id,
                                               const std::string &key);
    static luabridge::LuaRef GetComponentByType(int actor_id,
                                                const std::string &type_name);
    static luabridge::LuaRef GetComponentsByType( int actor_id, const std::string &type_name);

    // editor-facing metadata queries for available component types/defaults
    // 给编辑器使用的组件元数据查询接口
    static std::vector<std::string> GetRegisteredComponentTypes();
    static std::vector<Actor::ComponentProperty>
    GetComponentTypeDefaultProperties(const std::string &type_name);
    static std::vector<Actor::ComponentSpec>
    GetRuntimeComponentSpecs(int actor_id);
    static std::vector<Actor::ComponentProperty>
    GetRuntimeComponentProperties(int actor_id, const std::string &component_key);
    static bool SetRuntimeComponentPropertyValue(int actor_id, const std::string &component_key, const std::string &property_name, const Actor::ComponentPropertyValue &value);
    static void QueueBuiltinRenderers(bool scene_backed_only = false);
    static void ResolveTransformHierarchy();
    static bool TryGetRuntimeTransformWorld(int actor_id, float &x, float &y,
                                            float &rotation,
                                            std::string *out_component_key = nullptr);
    static bool SetRuntimeTransformWorldPosition(int actor_id, float world_x,
                                                float world_y);

    // actor-level helpers exposed through Actor static APIs
    // 通过 Actor 静态接口暴露出去的 actor 级辅助函数
    static luabridge::LuaRef InstantiateActor(const std::string &template_name);
    static void DestroyActor(Actor *actor);
    static bool SetActorParent(int actor_id, Actor *parent_actor);
    static luabridge::LuaRef FindActorByName(const std::string &name);
    static luabridge::LuaRef FindAllActorsByName(const std::string &name);
};

#endif
