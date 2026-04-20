#include "RegistrationDetail.h"
#include "scene/Actor.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"

namespace {

using APIRegistrationDetail::g_lua_state;

void InjectActorAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginClass<Actor>("Actor")
        .addFunction("GetName", &Actor::GetName)
        .addFunction("GetUID", &Actor::GetUID)
        .addFunction("SetParent", &Actor::SetParent)
        .addFunction("AddComponent", &Actor::AddComponent)
        .addFunction("RemoveComponent", &Actor::RemoveComponent)
        .addFunction("GetComponentByKey", &Actor::GetComponentByKey)
        .addFunction("GetComponent", &Actor::GetComponent)
        .addFunction("GetComponents", &Actor::GetComponents)
        .addFunction("GetChildCount", &Actor::GetChildCount)
        .addFunction("GetChildren", &Actor::GetChildren)
        .addFunction("GetComponentInChildren", &Actor::GetComponentInChildren)
        .addFunction("GetComponentsInChildren",
                     &Actor::GetComponentsInChildren)
        .endClass();

    luabridge::getGlobalNamespace(g_lua_state)
        .beginNamespace("Actor")
        .addFunction("Instantiate", &Actor::Instantiate)
        .addFunction("Destroy", &Actor::Destroy)
        .addFunction("Find", &Actor::Find)
        .addFunction("FindAll", &Actor::FindAll)
        .endNamespace();
}

} // namespace

namespace APIRegistrationDetail {

void RegisterActorAPI() {
    InjectActorAPI();
}

} // namespace APIRegistrationDetail
