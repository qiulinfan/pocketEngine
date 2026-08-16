#include "Internal.h"
#include "rendering/SpriteRenderer.h"
#include "rendering/MeshRenderer.h"
#include "scripting/ComponentManager.h"
#include "particles/ParticleSystem.h"
#include "physics/Rigidbody.h"
#include "scene/Transform.h"
#include "scene/Transform3D.h"
#include "scene/Camera3D.h"
#include <algorithm>
#include <unordered_set>

using namespace ManagerDetail;

namespace {

luabridge::LuaRef BuildChildrenResult(Actor::UID actor_uid) {
    if (actor_uid == Actor::kInvalidUID) return MakeEmptyArrayTable();

    luabridge::LuaRef result_table = MakeEmptyArrayTable();
    int lua_index = 1;
    for (Actor::UID candidate_uid : g_runtime.actor_uids_sorted) {
        auto actor_it = g_runtime.actor_by_uid.find(candidate_uid);
        if (actor_it == g_runtime.actor_by_uid.end() ||
            actor_it->second == nullptr) {
            continue;
        }

        const Actor *candidate_actor = actor_it->second;
        if (candidate_actor->parent_uid != actor_uid) continue;
        result_table[lua_index++] =
            luabridge::LuaRef(g_runtime.lua_state, actor_it->second);
    }

    return result_table;
}

bool IsActorSelfOrDescendantOf(Actor::UID candidate_uid, Actor::UID root_uid) {
    if (candidate_uid == Actor::kInvalidUID || root_uid == Actor::kInvalidUID) {
        return false;
    }

    Actor::UID current_uid = candidate_uid;
    std::unordered_set<Actor::UID> visited_uids;
    while (current_uid != Actor::kInvalidUID &&
           visited_uids.insert(current_uid).second) {
        if (current_uid == root_uid) return true;

        auto actor_it = g_runtime.actor_by_uid.find(current_uid);
        if (actor_it == g_runtime.actor_by_uid.end() ||
            actor_it->second == nullptr) {
            break;
        }

        const Actor *actor = actor_it->second;
        if (actor->parent_uid == current_uid) break;
        current_uid = actor->parent_uid;
    }

    return false;
}

luabridge::LuaRef BuildComponentsInChildrenResult(Actor::UID actor_uid,
                                                  const std::string &type_name,
                                                  bool first_only) {
    if (actor_uid == Actor::kInvalidUID || type_name.empty()) {
        return first_only ? MakeNilRef() : MakeEmptyArrayTable();
    }

    luabridge::LuaRef result_table = MakeEmptyArrayTable();
    int lua_index = 1;

    for (Actor::UID candidate_uid : g_runtime.actor_uids_sorted) {
        if (!IsActorSelfOrDescendantOf(candidate_uid, actor_uid)) continue;

        luabridge::LuaRef components =
            ComponentManager::GetComponentsByType(candidate_uid, type_name);
        if (!components.isTable()) continue;

        components.push(g_runtime.lua_state);
        const int components_stack_index = lua_absindex(g_runtime.lua_state, -1);
        const int component_count = static_cast<int>(
            lua_rawlen(g_runtime.lua_state, components_stack_index));
        for (int component_index = 1; component_index <= component_count;
             ++component_index) {
            luabridge::LuaRef component_ref = components[component_index];
            if (component_ref.isNil()) continue;
            if (first_only) {
                lua_pop(g_runtime.lua_state, 1);
                return component_ref;
            }
            result_table[lua_index++] = component_ref;
        }
        lua_pop(g_runtime.lua_state, 1);
    }

    return first_only ? MakeNilRef() : result_table;
}

enum class LuaArrayType {
    Bool,
    Int,
    Double,
    String,
    Unknown
};

LuaArrayType ExpectedArrayType(const Actor::ComponentPropertyValue *expected_value) {
    if (expected_value == nullptr) return LuaArrayType::Unknown;
    if (std::holds_alternative<Actor::BoolArray>(*expected_value)) {
        return LuaArrayType::Bool;
    }
    if (std::holds_alternative<Actor::IntArray>(*expected_value)) {
        return LuaArrayType::Int;
    }
    if (std::holds_alternative<Actor::DoubleArray>(*expected_value)) {
        return LuaArrayType::Double;
    }
    if (std::holds_alternative<Actor::StringArray>(*expected_value)) {
        return LuaArrayType::String;
    }
    return LuaArrayType::Unknown;
}

LuaArrayType InferArrayTypeFromFirstLuaElement(int table_index) {
    lua_rawgeti(g_runtime.lua_state, table_index, 1);
    LuaArrayType array_type = LuaArrayType::Unknown;
    const int value_type = lua_type(g_runtime.lua_state, -1);
    if (value_type == LUA_TBOOLEAN) {
        array_type = LuaArrayType::Bool;
    } else if (lua_isinteger(g_runtime.lua_state, -1)) {
        array_type = LuaArrayType::Int;
    } else if (lua_isnumber(g_runtime.lua_state, -1)) {
        array_type = LuaArrayType::Double;
    } else if (value_type == LUA_TSTRING) {
        array_type = LuaArrayType::String;
    }
    lua_pop(g_runtime.lua_state, 1);
    return array_type;
}

bool TryReadArrayValueFromLuaStack(int stack_index,
                                   const Actor::ComponentPropertyValue *expected_value,
                                   Actor::ComponentPropertyValue &out_value) {
    const int table_index = lua_absindex(g_runtime.lua_state, stack_index);
    if (!lua_istable(g_runtime.lua_state, table_index)) return false;

    const int array_length =
        static_cast<int>(lua_rawlen(g_runtime.lua_state, table_index));
    LuaArrayType array_type = ExpectedArrayType(expected_value);
    if (array_type == LuaArrayType::Unknown) {
        if (array_length <= 0) return false;
        array_type = InferArrayTypeFromFirstLuaElement(table_index);
    }
    if (array_type == LuaArrayType::Unknown) return false;

    switch (array_type) {
    case LuaArrayType::Bool: {
        Actor::BoolArray values;
        values.reserve(array_length);
        for (int index = 1; index <= array_length; ++index) {
            lua_rawgeti(g_runtime.lua_state, table_index, index);
            if (lua_type(g_runtime.lua_state, -1) != LUA_TBOOLEAN) {
                lua_pop(g_runtime.lua_state, 1);
                return false;
            }
            values.emplace_back(lua_toboolean(g_runtime.lua_state, -1) != 0);
            lua_pop(g_runtime.lua_state, 1);
        }
        out_value = std::move(values);
        return true;
    }
    case LuaArrayType::Int: {
        Actor::IntArray values;
        values.reserve(array_length);
        for (int index = 1; index <= array_length; ++index) {
            lua_rawgeti(g_runtime.lua_state, table_index, index);
            if (!lua_isinteger(g_runtime.lua_state, -1)) {
                lua_pop(g_runtime.lua_state, 1);
                return false;
            }
            values.emplace_back(
                static_cast<int>(lua_tointeger(g_runtime.lua_state, -1)));
            lua_pop(g_runtime.lua_state, 1);
        }
        out_value = std::move(values);
        return true;
    }
    case LuaArrayType::Double: {
        Actor::DoubleArray values;
        values.reserve(array_length);
        for (int index = 1; index <= array_length; ++index) {
            lua_rawgeti(g_runtime.lua_state, table_index, index);
            if (!lua_isnumber(g_runtime.lua_state, -1)) {
                lua_pop(g_runtime.lua_state, 1);
                return false;
            }
            values.emplace_back(lua_tonumber(g_runtime.lua_state, -1));
            lua_pop(g_runtime.lua_state, 1);
        }
        out_value = std::move(values);
        return true;
    }
    case LuaArrayType::String: {
        Actor::StringArray values;
        values.reserve(array_length);
        for (int index = 1; index <= array_length; ++index) {
            lua_rawgeti(g_runtime.lua_state, table_index, index);
            if (lua_type(g_runtime.lua_state, -1) != LUA_TSTRING) {
                lua_pop(g_runtime.lua_state, 1);
                return false;
            }
            values.emplace_back(lua_tostring(g_runtime.lua_state, -1));
            lua_pop(g_runtime.lua_state, 1);
        }
        out_value = std::move(values);
        return true;
    }
    case LuaArrayType::Unknown:
    default:
        return false;
    }
}

bool TryReadPropertyValueFromLuaStack(
    int stack_index, Actor::ComponentPropertyValue &out_value,
    const Actor::ComponentPropertyValue *expected_value = nullptr) {
    const int value_type = lua_type(g_runtime.lua_state, stack_index);
    switch (value_type) {
    case LUA_TBOOLEAN:
        out_value = lua_toboolean(g_runtime.lua_state, stack_index) != 0;
        return true;
    case LUA_TNUMBER:
        if (lua_isinteger(g_runtime.lua_state, stack_index)) {
            out_value = static_cast<int>(
                lua_tointeger(g_runtime.lua_state, stack_index));
        } else {
            out_value = lua_tonumber(g_runtime.lua_state, stack_index);
        }
        return true;
    case LUA_TSTRING:
        out_value = std::string(lua_tostring(g_runtime.lua_state, stack_index));
        return true;
    case LUA_TTABLE:
        return TryReadArrayValueFromLuaStack(stack_index, expected_value,
                                             out_value);
    default:
        return false;
    }
}

bool TryReadPropertyValueFromLuaRef(
    const luabridge::LuaRef &value_ref, Actor::ComponentPropertyValue &out_value,
    const Actor::ComponentPropertyValue *expected_value = nullptr) {
    if (g_runtime.lua_state == nullptr || value_ref.isNil()) return false;

    value_ref.push(g_runtime.lua_state);
    const bool converted = TryReadPropertyValueFromLuaStack(
        -1, out_value, expected_value);
    lua_pop(g_runtime.lua_state, 1);
    return converted;
}

std::vector<Actor::ComponentProperty> GetBuiltinComponentDefaultProperties(const std::string &type_name) {
    
    std::vector<Actor::ComponentProperty> properties;
    if (type_name == "Transform") {
        const Transform defaults;
        properties = {
            {"enabled", defaults.enabled},
            {"x", static_cast<double>(defaults.x)},
            {"y", static_cast<double>(defaults.y)},
            {"rotation", static_cast<double>(defaults.rotation)},
        };
        return properties;
    }

    if (type_name == "Transform3D") {
        const Transform3D defaults;
        properties = {
            {"enabled", defaults.enabled},
            {"position_x", static_cast<double>(defaults.position_x)},
            {"position_y", static_cast<double>(defaults.position_y)},
            {"position_z", static_cast<double>(defaults.position_z)},
            {"rotation_x", static_cast<double>(defaults.rotation_x)},
            {"rotation_y", static_cast<double>(defaults.rotation_y)},
            {"rotation_z", static_cast<double>(defaults.rotation_z)},
            {"rotation_w", static_cast<double>(defaults.rotation_w)},
            {"scale_x", static_cast<double>(defaults.scale_x)},
            {"scale_y", static_cast<double>(defaults.scale_y)},
            {"scale_z", static_cast<double>(defaults.scale_z)},
        };
        return properties;
    }

    if (type_name == "Camera3D") {
        const Camera3D defaults;
        properties = {
            {"enabled", defaults.enabled},
            {"primary", defaults.primary},
            {"orthographic", defaults.orthographic},
            {"vertical_fov_degrees",
             static_cast<double>(defaults.vertical_fov_degrees)},
            {"near_clip", static_cast<double>(defaults.near_clip)},
            {"far_clip", static_cast<double>(defaults.far_clip)},
            {"orthographic_height",
             static_cast<double>(defaults.orthographic_height)},
        };
        return properties;
    }

    if (type_name == "MeshRenderer") {
        const MeshRenderer defaults;
        properties = {
            {"enabled", defaults.enabled},
            {"mesh", defaults.mesh},
            {"color_r", static_cast<double>(defaults.color_r)},
            {"color_g", static_cast<double>(defaults.color_g)},
            {"color_b", static_cast<double>(defaults.color_b)},
            {"color_a", static_cast<double>(defaults.color_a)},
        };
        return properties;
    }

    if (type_name == "SpriteRenderer") {
        const SpriteRenderer defaults;
        properties = {
            {"enabled", defaults.enabled},
            {"sprite", defaults.sprite},
            {"r", defaults.r},
            {"g", defaults.g},
            {"b", defaults.b},
            {"a", defaults.a},
            {"pivot_x", static_cast<double>(defaults.pivot_x)},
            {"pivot_y", static_cast<double>(defaults.pivot_y)},
            {"scale_x", static_cast<double>(defaults.scale_x)},
            {"scale_y", static_cast<double>(defaults.scale_y)},
            {"sprite_row", defaults.sprite_row},
            {"sprite_column", defaults.sprite_column},
            {"sorting_order", defaults.sorting_order},
            {"auto_sorting_order", defaults.auto_sorting_order},
        };
        return properties;
    }

    if (type_name == "Rigidbody") {
        const Rigidbody defaults;
        properties = {
            {"enabled", defaults.enabled},
            {"x", static_cast<double>(defaults.x)},
            {"y", static_cast<double>(defaults.y)},
            {"body_type", defaults.body_type},
            {"precise", defaults.precise},
            {"gravity_scale", static_cast<double>(defaults.gravity_scale)},
            {"density", static_cast<double>(defaults.density)},
            {"angular_friction",
             static_cast<double>(defaults.angular_friction)},
            {"rotation", static_cast<double>(defaults.rotation)},
            {"has_collider", defaults.has_collider},
            {"has_trigger", defaults.has_trigger},
            {"collider_type", defaults.collider_type},
            {"width", static_cast<double>(defaults.width)},
            {"height", static_cast<double>(defaults.height)},
            {"radius", static_cast<double>(defaults.radius)},
            {"trigger_type", defaults.trigger_type},
            {"trigger_width", static_cast<double>(defaults.trigger_width)},
            {"trigger_height", static_cast<double>(defaults.trigger_height)},
            {"trigger_radius", static_cast<double>(defaults.trigger_radius)},
            {"friction", static_cast<double>(defaults.friction)},
            {"bounciness", static_cast<double>(defaults.bounciness)},
        };
        return properties;
    }

    if (type_name == "ParticleSystem") {
        const ParticleSystem defaults;
        properties = {
            {"enabled", defaults.enabled},
            {"x", static_cast<double>(defaults.x)},
            {"y", static_cast<double>(defaults.y)},
            {"frames_between_bursts", defaults.frames_between_bursts},
            {"burst_quantity", defaults.burst_quantity},
            {"duration_frames", defaults.duration_frames},
            {"start_scale_min", static_cast<double>(defaults.start_scale_min)},
            {"start_scale_max", static_cast<double>(defaults.start_scale_max)},
            {"start_speed_min", static_cast<double>(defaults.start_speed_min)},
            {"start_speed_max", static_cast<double>(defaults.start_speed_max)},
            {"rotation_min", static_cast<double>(defaults.rotation_min)},
            {"rotation_max", static_cast<double>(defaults.rotation_max)},
            {"rotation_speed_min",
             static_cast<double>(defaults.rotation_speed_min)},
            {"rotation_speed_max",
             static_cast<double>(defaults.rotation_speed_max)},
            {"start_color_r", defaults.start_color_r},
            {"start_color_g", defaults.start_color_g},
            {"start_color_b", defaults.start_color_b},
            {"start_color_a", defaults.start_color_a},
            {"end_color_r", defaults.end_color_r},
            {"end_color_g", defaults.end_color_g},
            {"end_color_b", defaults.end_color_b},
            {"end_color_a", defaults.end_color_a},
            {"image", defaults.image},
            {"sorting_order", defaults.sorting_order},
            {"emit_angle_min", static_cast<double>(defaults.emit_angle_min)},
            {"emit_angle_max", static_cast<double>(defaults.emit_angle_max)},
            {"emit_radius_min", static_cast<double>(defaults.emit_radius_min)},
            {"emit_radius_max", static_cast<double>(defaults.emit_radius_max)},
            {"gravity_scale_x",
             static_cast<double>(defaults.gravity_scale_x)},
            {"gravity_scale_y",
             static_cast<double>(defaults.gravity_scale_y)},
            {"drag_factor", static_cast<double>(defaults.drag_factor)},
            {"angular_drag_factor",
             static_cast<double>(defaults.angular_drag_factor)},
            {"end_scale", static_cast<double>(defaults.end_scale)},
        };
        return properties;
    }

    return properties;
}

void MergeLiveRuntimePropertyValues(
    const ComponentRecord &component,
    std::vector<Actor::ComponentProperty> &properties) {
    for (Actor::ComponentProperty &property : properties) {
        Actor::ComponentPropertyValue live_value;
        if (!TryReadPropertyValueFromLuaRef(component.instance_table[property.name],
                                            live_value, &property.value)) {
            continue;
        }
        property.value = live_value;
    }
}

void MergeDynamicLuaTableProperties(
    const ComponentRecord &component,
    std::vector<Actor::ComponentProperty> &properties) {
    if (g_runtime.lua_state == nullptr) return;

    std::unordered_set<std::string> known_names;
    known_names.reserve(properties.size());
    for (const Actor::ComponentProperty &property : properties) {
        known_names.insert(property.name);
    }

    component.instance_table.push(g_runtime.lua_state);
    if (!lua_istable(g_runtime.lua_state, -1)) {
        lua_pop(g_runtime.lua_state, 1);
        return;
    }

    lua_pushnil(g_runtime.lua_state);
    while (lua_next(g_runtime.lua_state, -2) != 0) {
        if (lua_type(g_runtime.lua_state, -2) != LUA_TSTRING) {
            lua_pop(g_runtime.lua_state, 1);
            continue;
        }

        const std::string property_name = lua_tostring(g_runtime.lua_state, -2);
        if (property_name == "actor" || property_name == "key") {
            lua_pop(g_runtime.lua_state, 1);
            continue;
        }
        if (known_names.find(property_name) != known_names.end()) {
            lua_pop(g_runtime.lua_state, 1);
            continue;
        }

        Actor::ComponentProperty property;
        property.name = property_name;
        if (!TryReadPropertyValueFromLuaStack(-1, property.value)) {
            lua_pop(g_runtime.lua_state, 1);
            continue;
        }

        properties.emplace_back(std::move(property));
        known_names.insert(property_name);
        lua_pop(g_runtime.lua_state, 1);
    }

    lua_pop(g_runtime.lua_state, 1);
}

} // namespace

// -----------------------------------------------------------------------------
// ComponentManager public API: queries
// -----------------------------------------------------------------------------

// component queries used by Actor's Lua-facing methods
luabridge::LuaRef ComponentManager::GetComponentByKey(Actor::UID actor_uid,
                                                      const std::string &key) {
    // removed 组件会被 FindComponentRecord 过滤, 查询结果为 nil
    ComponentRecord *component = FindComponentRecord(actor_uid, key);
    if (component == nullptr) return MakeNilRef();
    return component->instance_table;
}

// component queries used by Actor's Lua-facing methods
luabridge::LuaRef ComponentManager::GetComponentByType(Actor::UID actor_uid,
                                                       const std::string &type_name) {
    auto actor_it = g_runtime.component_first_key_by_type.find(actor_uid);
    if (actor_it == g_runtime.component_first_key_by_type.end()) return MakeNilRef();

    auto type_it = actor_it->second.find(type_name);
    if (type_it == actor_it->second.end()) return MakeNilRef();

    ComponentRecord *component = FindComponentRecord(actor_uid, type_it->second);
    if (component == nullptr) return MakeNilRef();
    return component->instance_table;
}

// component queries used by Actor's Lua-facing methods
luabridge::LuaRef ComponentManager::GetComponentsByType(Actor::UID actor_uid,
                                                        const std::string &type_name) {
    auto actor_it = g_runtime.component_keys_by_type.find(actor_uid);
    if (actor_it == g_runtime.component_keys_by_type.end()) {
        return MakeEmptyArrayTable();
    }

    auto type_it = actor_it->second.find(type_name);
    if (type_it == actor_it->second.end()) return MakeEmptyArrayTable();

    luabridge::LuaRef result_table = MakeEmptyArrayTable();
    int lua_index = 1;
    for (const std::string &component_key : type_it->second) {
        ComponentRecord *component = FindComponentRecord(actor_uid,
                                                         component_key);
        if (component == nullptr) continue;
        // Lua arrays are 1-indexed for ipairs().
        result_table[lua_index++] = component->instance_table;
    }
    return result_table;
}

int ComponentManager::GetChildCount(Actor::UID actor_uid) {
    if (actor_uid == Actor::kInvalidUID) return 0;

    int child_count = 0;
    for (Actor::UID candidate_uid : g_runtime.actor_uids_sorted) {
        auto actor_it = g_runtime.actor_by_uid.find(candidate_uid);
        if (actor_it == g_runtime.actor_by_uid.end() ||
            actor_it->second == nullptr) {
            continue;
        }

        if (actor_it->second->parent_uid == actor_uid) {
            ++child_count;
        }
    }

    return child_count;
}

luabridge::LuaRef ComponentManager::GetChildren(Actor::UID actor_uid) {
    /*
    This is the direct-child query that pairs with GetChildCount. It does not
    recurse; callers that want descendants should use GetChildren recursively
    or the GetComponent(s)InChildren helpers above.
    */
    return BuildChildrenResult(actor_uid);
}

luabridge::LuaRef ComponentManager::GetComponentInChildrenByType(
    Actor::UID actor_uid, const std::string &type_name) {
    /*
    Match the familiar Unity-style query shape: inspect this actor first, then
    walk descendants in current runtime scene order until the first component
    of the requested type is found.
    */
    return BuildComponentsInChildrenResult(actor_uid, type_name, true);
}

luabridge::LuaRef ComponentManager::GetComponentsInChildrenByType(
    Actor::UID actor_uid, const std::string &type_name) {
    /*
    The runtime stores only parent links, so child queries are derived on
    demand by scanning the live actor order and checking ancestry chains.
    */
    return BuildComponentsInChildrenResult(actor_uid, type_name, false);
}

// actor-level helpers exposed through Actor static APIs
luabridge::LuaRef ComponentManager::FindActorByName(const std::string &name) {
    // 按维护的名字索引返回首个匹配 actor
    auto it = g_runtime.actors_by_name.find(name);
    if (it == g_runtime.actors_by_name.end() || it->second.empty()) {
        return MakeNilRef();
    }
    return luabridge::LuaRef(g_runtime.lua_state, it->second[0]);
}

// actor-level helpers exposed through Actor static APIs
luabridge::LuaRef ComponentManager::FindAllActorsByName( const std::string &name) {
    // 返回 Lua 数组(Note: 1-based), 便于 Lua 端 ipairs()
    auto it = g_runtime.actors_by_name.find(name);
    luabridge::LuaRef result_table = MakeEmptyArrayTable();
    if (it == g_runtime.actors_by_name.end()) return result_table;

    const std::vector<Actor *> &matched_actors = it->second;
    for (size_t i = 0; i < matched_actors.size(); i++) {
        result_table[static_cast<int>(i + 1)] = luabridge::LuaRef(g_runtime.lua_state, matched_actors[i]);
    }
    return result_table;
}

// editor-facing metadata queries for available component types/defaults
std::vector<std::string> ComponentManager::GetRegisteredComponentTypes() {
    std::vector<std::string> type_names;
    type_names.reserve(g_runtime.component_type_tables.size() + 7);
    type_names.emplace_back("SpriteRenderer");
    type_names.emplace_back("ParticleSystem");
    type_names.emplace_back("Rigidbody");
    type_names.emplace_back("Transform");
    type_names.emplace_back("Transform3D");
    type_names.emplace_back("Camera3D");
    type_names.emplace_back("MeshRenderer");

    for (const auto &entry : g_runtime.component_type_tables) {
        type_names.emplace_back(entry.first);
    }

    std::sort(type_names.begin(), type_names.end());
    type_names.erase(std::unique(type_names.begin(), type_names.end()),
                     type_names.end());
    return type_names;
}

bool ComponentManager::IsRegisteredComponentType(const std::string &type_name) {
    if (IsBuiltinComponentType(type_name)) return true;
    return g_runtime.component_type_tables.find(type_name) !=
           g_runtime.component_type_tables.end();
}

// editor-facing metadata queries for available component types/defaults
std::vector<Actor::ComponentProperty>
ComponentManager::GetComponentTypeDefaultProperties( const std::string &type_name) {
    std::vector<Actor::ComponentProperty> properties;
    if (IsBuiltinComponentType(type_name)) {
        return GetBuiltinComponentDefaultProperties(type_name);
    }
    if (g_runtime.lua_state == nullptr) return properties;

    auto type_it = g_runtime.component_type_tables.find(type_name);
    if (type_it == g_runtime.component_type_tables.end()) {
        return properties;
    }

    type_it->second.push(g_runtime.lua_state);
    lua_pushnil(g_runtime.lua_state);
    while (lua_next(g_runtime.lua_state, -2) != 0) {
        if (lua_type(g_runtime.lua_state, -2) != LUA_TSTRING) {
            lua_pop(g_runtime.lua_state, 1);
            continue;
        }

        Actor::ComponentProperty property;
        property.name = lua_tostring(g_runtime.lua_state, -2);

        if (!TryReadPropertyValueFromLuaStack(-1, property.value)) {
            lua_pop(g_runtime.lua_state, 1);
            continue;
        }

        properties.emplace_back(std::move(property));
        lua_pop(g_runtime.lua_state, 1);
    }
    lua_pop(g_runtime.lua_state, 1);

    std::sort(properties.begin(), properties.end(),
              [](const Actor::ComponentProperty &a,
                 const Actor::ComponentProperty &b) {
                  return a.name < b.name;
              });
    return properties;
}

std::vector<Actor::ComponentSpec>
ComponentManager::GetRuntimeComponentSpecs(Actor::UID actor_uid) {
    std::vector<Actor::ComponentSpec> component_specs;
    auto actor_it = g_runtime.actor_components.find(actor_uid);
    if (actor_it == g_runtime.actor_components.end()) {
        return component_specs;
    }

    component_specs.reserve(actor_it->second.size());
    for (const std::unique_ptr<ComponentRecord> &component_ptr :
         actor_it->second) {
        if (component_ptr == nullptr || component_ptr->removed) continue;
        Actor::ComponentSpec component_spec;
        component_spec.key = component_ptr->key;
        component_spec.type = component_ptr->type;
        component_specs.emplace_back(std::move(component_spec));
    }

    std::sort(component_specs.begin(), component_specs.end(),
              [](const Actor::ComponentSpec &a,
                 const Actor::ComponentSpec &b) { return a.key < b.key; });
    return component_specs;
}

std::vector<Actor::ComponentProperty>
ComponentManager::GetRuntimeComponentProperties(Actor::UID actor_uid,
                                                const std::string &component_key) {
    std::vector<Actor::ComponentProperty> properties;
    ComponentRecord *component = FindComponentRecord(actor_uid, component_key);
    if (component == nullptr) return properties;

    if (IsBuiltinComponentType(component->type)) {
        properties = GetBuiltinComponentDefaultProperties(component->type);
    } else {
        properties = GetComponentTypeDefaultProperties(component->type);
    }

    MergeLiveRuntimePropertyValues(*component, properties);
    MergeDynamicLuaTableProperties(*component, properties);

    std::sort(properties.begin(), properties.end(),
              [](const Actor::ComponentProperty &a,
                 const Actor::ComponentProperty &b) {
                  return a.name < b.name;
              });
    return properties;
}
