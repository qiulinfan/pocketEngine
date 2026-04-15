#include "RegistrationDetail.h"
#include "physics/Collision.h"
#include "physics/RayCast.h"
#include "scene/Actor.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"
#include "LuaBridge/detail/ClassInfo.h"
#include <optional>
#include <vector>

namespace {

using APIRegistrationDetail::g_lua_state;

float CppVector2Distance(const b2Vec2 &a, const b2Vec2 &b) {
    return b2Distance(a, b);
}

template <typename T>
void InstallClassMetamethod(const char *name, lua_CFunction function) {
    lua_rawgetp(g_lua_state, LUA_REGISTRYINDEX,
                luabridge::detail::getClassRegistryKey<T>());
    if (lua_istable(g_lua_state, -1)) {
        lua_pushstring(g_lua_state, name);
        lua_pushcfunction(g_lua_state, function);
        lua_rawset(g_lua_state, -3);
    }
    lua_pop(g_lua_state, 1);
}

int LuaVector2Add(lua_State *L) {
    const b2Vec2 &a = luabridge::Stack<const b2Vec2 &>::get(L, 1);
    const b2Vec2 &b = luabridge::Stack<const b2Vec2 &>::get(L, 2);
    luabridge::Stack<b2Vec2>::push(L, a + b);
    return 1;
}

int LuaVector2Sub(lua_State *L) {
    const b2Vec2 &a = luabridge::Stack<const b2Vec2 &>::get(L, 1);
    const b2Vec2 &b = luabridge::Stack<const b2Vec2 &>::get(L, 2);
    luabridge::Stack<b2Vec2>::push(L, a - b);
    return 1;
}

int LuaVector2Mul(lua_State *L) {
    const bool lhs_is_vector = luabridge::Stack<b2Vec2>::isInstance(L, 1);
    const bool rhs_is_vector = luabridge::Stack<b2Vec2>::isInstance(L, 2);
    const bool lhs_is_number = lua_isnumber(L, 1) != 0;
    const bool rhs_is_number = lua_isnumber(L, 2) != 0;

    if (lhs_is_vector && rhs_is_number) {
        const b2Vec2 &vec = luabridge::Stack<const b2Vec2 &>::get(L, 1);
        const float scalar = luabridge::Stack<float>::get(L, 2);
        luabridge::Stack<b2Vec2>::push(L, scalar * vec);
        return 1;
    }

    if (lhs_is_number && rhs_is_vector) {
        const float scalar = luabridge::Stack<float>::get(L, 1);
        const b2Vec2 &vec = luabridge::Stack<const b2Vec2 &>::get(L, 2);
        luabridge::Stack<b2Vec2>::push(L, scalar * vec);
        return 1;
    }

    return luaL_error(
        L, "Vector2 multiplication expects (Vector2, number) or (number, Vector2)");
}

void InjectVector2API() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginClass<b2Vec2>("Vector2")
        .addConstructor<void (*)(float, float)>()
        .addProperty("x", &b2Vec2::x)
        .addProperty("y", &b2Vec2::y)
        .addFunction("Normalize", &b2Vec2::Normalize)
        .addFunction("Length", &b2Vec2::Length)
        .addStaticFunction("Distance",
                           static_cast<float (*)(const b2Vec2 &, const b2Vec2 &)>(
                               &CppVector2Distance))
        .addStaticFunction("Dot",
                           static_cast<float (*)(const b2Vec2 &, const b2Vec2 &)>(
                               &b2Dot))
        .endClass();

    InstallClassMetamethod<b2Vec2>("__add", &LuaVector2Add);
    InstallClassMetamethod<b2Vec2>("__sub", &LuaVector2Sub);
    InstallClassMetamethod<b2Vec2>("__mul", &LuaVector2Mul);
}

void InjectCollisionAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginClass<Collision>("Collision")
        .addConstructor<void (*)()>()
        .addProperty("other", &Collision::other)
        .addProperty("point", &Collision::point)
        .addProperty("relative_velocity", &Collision::relative_velocity)
        .addProperty("normal", &Collision::normal)
        .endClass();
}

luabridge::LuaRef CppPhysicsRaycast(const b2Vec2 &position,
                                    const b2Vec2 &direction, float distance) {
    std::optional<RayCast::HitResult> hit =
        RayCast::Cast(position, direction, distance);
    if (!hit.has_value()) return luabridge::LuaRef(g_lua_state);
    return luabridge::LuaRef(g_lua_state, *hit);
}

luabridge::LuaRef CppPhysicsRaycastAll(const b2Vec2 &position,
                                       const b2Vec2 &direction,
                                       float distance) {
    luabridge::LuaRef results = luabridge::newTable(g_lua_state);
    const std::vector<RayCast::HitResult> hits =
        RayCast::CastAll(position, direction, distance);
    for (size_t i = 0; i < hits.size(); i++) {
        results[static_cast<int>(i + 1)] = hits[i];
    }
    return results;
}

void InjectPhysicsAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginClass<RayCast::HitResult>("HitResult")
        .addConstructor<void (*)()>()
        .addProperty("actor", &RayCast::HitResult::actor)
        .addProperty("point", &RayCast::HitResult::point)
        .addProperty("normal", &RayCast::HitResult::normal)
        .addProperty("is_trigger", &RayCast::HitResult::is_trigger)
        .endClass();

    luabridge::getGlobalNamespace(g_lua_state)
        .beginNamespace("Physics")
        .addFunction("Raycast", &CppPhysicsRaycast)
        .addFunction("RaycastAll", &CppPhysicsRaycastAll)
        .endNamespace();
}

} // namespace

namespace APIRegistrationDetail {

void RegisterPhysicsAPI() {
    InjectVector2API();
    InjectCollisionAPI();
    InjectPhysicsAPI();
}

} // namespace APIRegistrationDetail
