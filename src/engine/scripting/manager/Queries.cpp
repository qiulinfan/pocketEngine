#include "Internal.h"
#include "rendering/SpriteRenderer.h"
#include "scripting/ComponentManager.h"
#include "particles/ParticleSystem.h"
#include "physics/Rigidbody.h"
#include "scene/Transform.h"
#include <algorithm>
#include <unordered_set>

using namespace ManagerDetail;

namespace {

bool TryReadScalarValueFromLuaRef(const luabridge::LuaRef &value_ref,
                                  Actor::ComponentPropertyValue &out_value) {
    if (g_runtime.lua_state == nullptr || value_ref.isNil()) return false;

    value_ref.push(g_runtime.lua_state);
    const int value_type = lua_type(g_runtime.lua_state, -1);
    bool converted = true;
    switch (value_type) {
    case LUA_TBOOLEAN:
        out_value = lua_toboolean(g_runtime.lua_state, -1) != 0;
        break;
    case LUA_TNUMBER:
        if (lua_isinteger(g_runtime.lua_state, -1)) {
            out_value =
                static_cast<int>(lua_tointeger(g_runtime.lua_state, -1));
        } else {
            out_value = lua_tonumber(g_runtime.lua_state, -1);
        }
        break;
    case LUA_TSTRING:
        out_value =
            std::string(lua_tostring(g_runtime.lua_state, -1));
        break;
    default:
        converted = false;
        break;
    }
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
        if (!TryReadScalarValueFromLuaRef(component.instance_table[property.name],
                                          live_value)) {
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
        const int value_type = lua_type(g_runtime.lua_state, -1);
        if (value_type == LUA_TBOOLEAN) {
            property.value = lua_toboolean(g_runtime.lua_state, -1) != 0;
        } else if (lua_isinteger(g_runtime.lua_state, -1)) {
            property.value =
                static_cast<int>(lua_tointeger(g_runtime.lua_state, -1));
        } else if (lua_isnumber(g_runtime.lua_state, -1)) {
            property.value = lua_tonumber(g_runtime.lua_state, -1);
        } else if (value_type == LUA_TSTRING) {
            property.value =
                std::string(lua_tostring(g_runtime.lua_state, -1));
        } else {
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
luabridge::LuaRef ComponentManager::GetComponentByKey(
    int actor_id, const std::string &key) {
    // removed 组件会被 FindComponentRecord 过滤, 查询结果为 nil
    ComponentRecord *component = FindComponentRecord(actor_id, key);
    if (component == nullptr) return MakeNilRef();
    return component->instance_table;
}

// component queries used by Actor's Lua-facing methods
luabridge::LuaRef ComponentManager::GetComponentByType(
    int actor_id, const std::string &type_name) {
    auto actor_it = g_runtime.component_first_key_by_type.find(actor_id);
    if (actor_it == g_runtime.component_first_key_by_type.end()) return MakeNilRef();

    auto type_it = actor_it->second.find(type_name);
    if (type_it == actor_it->second.end()) return MakeNilRef();

    ComponentRecord *component = FindComponentRecord(actor_id, type_it->second);
    if (component == nullptr) return MakeNilRef();
    return component->instance_table;
}

// component queries used by Actor's Lua-facing methods
luabridge::LuaRef ComponentManager::GetComponentsByType(
    int actor_id, const std::string &type_name) {
    auto actor_it = g_runtime.component_keys_by_type.find(actor_id);
    if (actor_it == g_runtime.component_keys_by_type.end()) {
        return MakeEmptyArrayTable();
    }

    auto type_it = actor_it->second.find(type_name);
    if (type_it == actor_it->second.end()) return MakeEmptyArrayTable();

    luabridge::LuaRef result_table = MakeEmptyArrayTable();
    int lua_index = 1;
    for (const std::string &component_key : type_it->second) {
        ComponentRecord *component = FindComponentRecord(actor_id, component_key);
        if (component == nullptr) continue;
        // Lua arrays are 1-indexed for ipairs().
        result_table[lua_index++] = component->instance_table;
    }
    return result_table;
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
luabridge::LuaRef ComponentManager::FindAllActorsByName(
    const std::string &name) {
    // 返回 Lua 数组(Note: 1-based), 便于 Lua 端 ipairs()
    auto it = g_runtime.actors_by_name.find(name);
    luabridge::LuaRef result_table = MakeEmptyArrayTable();
    if (it == g_runtime.actors_by_name.end()) return result_table;

    const std::vector<Actor *> &matched_actors = it->second;
    for (size_t i = 0; i < matched_actors.size(); i++) {
        result_table[static_cast<int>(i + 1)] =
            luabridge::LuaRef(g_runtime.lua_state, matched_actors[i]);
    }
    return result_table;
}

// editor-facing metadata queries for available component types/defaults
std::vector<std::string> ComponentManager::GetRegisteredComponentTypes() {
    std::vector<std::string> type_names;
    type_names.reserve(g_runtime.component_type_tables.size() + 4);
    type_names.emplace_back("SpriteRenderer");
    type_names.emplace_back("ParticleSystem");
    type_names.emplace_back("Rigidbody");
    type_names.emplace_back("Transform");

    for (const auto &entry : g_runtime.component_type_tables) {
        type_names.emplace_back(entry.first);
    }

    std::sort(type_names.begin(), type_names.end());
    type_names.erase(std::unique(type_names.begin(), type_names.end()),
                     type_names.end());
    return type_names;
}

// editor-facing metadata queries for available component types/defaults
std::vector<Actor::ComponentProperty>
ComponentManager::GetComponentTypeDefaultProperties(
    const std::string &type_name) {
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

        if (lua_type(g_runtime.lua_state, -1) == LUA_TBOOLEAN) {
            property.value = lua_toboolean(g_runtime.lua_state, -1) != 0;
        } else if (lua_isinteger(g_runtime.lua_state, -1)) {
            property.value = static_cast<int>(lua_tointeger(g_runtime.lua_state, -1));
        } else if (lua_isnumber(g_runtime.lua_state, -1)) {
            property.value = lua_tonumber(g_runtime.lua_state, -1);
        } else if (lua_type(g_runtime.lua_state, -1) == LUA_TSTRING) {
            property.value =
                std::string(lua_tostring(g_runtime.lua_state, -1));
        } else {
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
ComponentManager::GetRuntimeComponentSpecs(int actor_id) {
    std::vector<Actor::ComponentSpec> component_specs;
    auto actor_it = g_runtime.actor_components.find(actor_id);
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
ComponentManager::GetRuntimeComponentProperties(int actor_id,
                                                const std::string &component_key) {
    std::vector<Actor::ComponentProperty> properties;
    ComponentRecord *component = FindComponentRecord(actor_id, component_key);
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
