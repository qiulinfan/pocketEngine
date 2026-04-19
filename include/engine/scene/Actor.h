#ifndef ACTOR_H
#define ACTOR_H

#include "glm/glm.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace luabridge {
class LuaRef;
}

struct Actor {
public:
    // Zero means "no identity". Both scene-backed actors and runtime-only
    // actors may own non-zero UIDs; provenance is tracked separately.
    static inline constexpr std::uint64_t kInvalidEditorActorUID = 0;
    static inline constexpr std::uint64_t kRuntimeGeneratedEditorActorUIDStart = 10000;

    // supported json value types for component property overrides
    // 组件属性支持的 JSON 值类型
    using ComponentPropertyValue = std::variant<bool, int, double, std::string>;

    // property: name and value. this is for overrides
    // 仅供 override default property
    struct ComponentProperty {
        std::string name;
        ComponentPropertyValue value;
    };

    struct ComponentSpec {
        // this component 在 actor 上的 key. 也是生命周期排序依据.
        std::string key;
        // this component 的 type
        std::string type;
        // property overrides applied before the component begins its lifecycle
        std::vector<ComponentProperty> overrides;
    };

    // runtime actor uniqueid. assigned during scene loading
    int id = -1;
    // Stable identity used to map editor/runtime actors. Scene documents
    // persist this UID; runtime-only actors receive transient UIDs.
    std::uint64_t editor_actor_uid = 0;
    // Runtime-only actors now also receive UIDs, so scene provenance needs an
    // explicit flag instead of inferring it from UID!=0.
    bool scene_backed = false;
    // Scene-authored parent link expressed in the same persistent UID space.
    std::uint64_t parent_editor_actor_uid = 0;
    // Runtime-resolved parent actor id. Runtime-only actors can also use this.
    int parent_id = -1;
    std::string actor_name = "";

    // 设为 true 后该 actor 不应再参与正常逻辑.
    bool runtime_destroyed = false;

    // Cross-scene persistence flag. 
    // 设为 true 后切换场景时不会自动销毁.
    bool dont_destroy_on_scene_load = false;

    // Parsed component specs stored before the real component instances are created.
    // 在真正实例化组件前暂存于此.
    std::vector<ComponentSpec> component_specs;

    std::string GetName() const;
    int GetID() const;
    // Scene-backed actors originated from an editor scene document snapshot.
    bool IsSceneBacked() const;
    // Runtime-spawned actors only exist in the live runtime world.
    bool IsRuntimeSpawned() const;
    // Runtime parenting changes only the live scene graph and keeps world pose.
    bool SetParent(Actor *parent) const;

    luabridge::LuaRef AddComponent(const std::string &type_name) const;
    void RemoveComponent(luabridge::LuaRef component_ref) const;

    // get component by key / type. returns nil if not found. 
    // if multiple components of the same type exist, returns the first one.
    // 根据 key / type 获取组件. 如果找不到返回 nil. 如果存在多个同类型组件, 返回第一个.
    luabridge::LuaRef GetComponentByKey(const std::string &key) const;
    luabridge::LuaRef GetComponent(const std::string &type_name) const;

    // returns all component instances of the given type as a Lua array.
    // 返回所有 given type 的 component instances
    luabridge::LuaRef GetComponents(const std::string &type_name) const;

    // loads an actor data copy from an actor template file.
    static Actor LoadTemplate(const std::string &template_name);

    // 当前场景中实例化一个 actor, 返回其 Lua 引用.
    static luabridge::LuaRef Instantiate(const std::string &template_name);
    // 请求在本帧末销毁指定 actor.
    static void Destroy(Actor *actor);
    // return 第一个名称匹配的 actor.
    static luabridge::LuaRef Find(const std::string &name);
    // return所有名称匹配的 actor, 结果是 Lua 数组.
    static luabridge::LuaRef FindAll(const std::string &name);

    Actor() {}
};

#endif
