#include "RegistrationDetail.h"
#include "scripting/EventBus.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"
#include <string>

namespace {

using APIRegistrationDetail::g_lua_state;

void CppEventPublish(const std::string &event_type,
                     luabridge::LuaRef event_object) {
    EventBus::Publish(event_type, event_object);
}

void CppEventSubscribe(const std::string &event_type,
                       luabridge::LuaRef component_ref,
                       luabridge::LuaRef function_ref) {
    EventBus::Subscribe(event_type, component_ref, function_ref);
}

void CppEventUnsubscribe(const std::string &event_type,
                         luabridge::LuaRef component_ref,
                         luabridge::LuaRef function_ref) {
    EventBus::Unsubscribe(event_type, component_ref, function_ref);
}

void InjectEventAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginNamespace("Event")
        .addFunction("Publish", &CppEventPublish)
        .addFunction("Subscribe", &CppEventSubscribe)
        .addFunction("Unsubscribe", &CppEventUnsubscribe)
        .endNamespace();
}

} // namespace

namespace APIRegistrationDetail {

void RegisterEventAPI() {
    InjectEventAPI();
}

} // namespace APIRegistrationDetail
